#include "failure.h"

#include <algorithm>
#include <cmath>

namespace tfx::fx::failure {

namespace {
constexpr float kPi    = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

int samples(float ms, float fs) { return std::max(1, static_cast<int>(ms * 0.001f * fs)); }
float expDraw(Xorshift32& rng) { return -std::log(1.0f - rng.uniform()); } // Exp(1)
} // namespace

// --- Poisson ------------------------------------------------------------------

void Poisson::reset(Xorshift32& rng) {
    acc_       = 0.0f;
    threshold_ = expDraw(rng);
}

bool Poisson::next(float ratePerSecond, float sampleRate, Xorshift32& rng) {
    // Integrated hazard against an Exp(1) threshold: exact for any rate curve.
    acc_ += ratePerSecond / sampleRate;
    if (acc_ < threshold_) return false;
    acc_       = 0.0f;
    threshold_ = expDraw(rng);
    return true;
}

// --- Transport ----------------------------------------------------------------

void Transport::setup(float sampleRate, float maxLagSamples) {
    fs_     = sampleRate;
    maxLag_ = maxLagSamples;
    mute_.prepare(fs_, 10.0f);
    skip_.prepare(fs_, 20.0f);
    catchCoeff_ = 1.0f - std::exp(-1.0f / (0.020f * fs_)); // r eases towards its catch-up target
    reset();
}

void Transport::reset() {
    catchMax_ = 0.03f;
    state_ = State::Run;
    r_ = 1.0f, lag_ = 0.0f;
    pos_ = 0, len_ = 1, dragPhase_ = 0;
    wobblePos_ = wobbleLen_ = 0;
    mute_.snap(1.0f);
    skip_.snap(0.0f);
    skipping_ = autoRestart_ = false;
    pendingStop_ = 0.0f;
    stoppedFor_ = 0;
}

void Transport::segment(float to, float ms, State s) {
    r0_ = r_, r1_ = to;
    pos_ = 0, len_ = samples(ms, fs_);
    state_ = s;
}

void Transport::drag(float depth01, float u1, float u2) {
    if (state_ != State::Run || skipping_) return;
    // Spec: r falls over 150 ms to 1 - 0.06 depth (x 0.5-1), holds 100-400 ms,
    // recovers over 300 ms; the lag is then caught up at up to r = 1.03.
    dragMin_   = 1.0f - 0.06f * depth01 * (0.5f + 0.5f * u1);
    // v0.14: catch up at half the drag's depth over speed (at most +3%, about
    // +50 cents), so a light drag is not followed by a heavy sharp stretch.
    catchMax_  = 0.5f * (1.0f - dragMin_);
    dragHold_  = samples(100.0f + 300.0f * u2, fs_);
    dragPhase_ = 0;
    segment(dragMin_, 150.0f, State::Drag);
}

void Transport::wobble() {
    if (wobbleLen_ > 0) return;
    wobbleLen_ = samples(300.0f, fs_);
    wobblePos_ = 0;
}

void Transport::stop(float stopMs) {
    if (state_ == State::Stopping || state_ == State::Stopped) return;
    if (skipping_) { pendingStop_ = stopMs; return; } // after the 20 ms skip
    autoRestart_ = false;
    segment(0.0f, stopMs, State::Stopping);
}

void Transport::autoStop(float stopMs) {
    if (state_ != State::Run || skipping_) return;
    stop(stopMs);
    autoRestart_ = true;
}

void Transport::start() {
    pendingStop_ = 0.0f;
    if (state_ == State::Stopped) {
        // The tape was silent: start reading fresh at no lag and fade in.
        lag_ = 0.0f;
        mute_.setTarget(1.0f);
        segment(1.0f, 300.0f, State::Starting);
    } else if (state_ == State::Stopping) {
        segment(1.0f, std::max(1.0f, 300.0f * (1.0f - r_)), State::Starting);
    }
}

Transport::Out Transport::next() {
    Out o;
    auto ramp = [this] {
        ++pos_;
        r_ = pos_ >= len_ ? r1_ : r0_ + (r1_ - r0_) * static_cast<float>(pos_) / static_cast<float>(len_);
        return pos_ >= len_;
    };

    switch (state_) {
        case State::Run:
            if (skipping_) {
                r_ = 1.0f;
            } else {
                // Catch up the lag a drag left: towards r = 1.03 (about +50
                // cents), easing off over the last 10 ms of lag.
                const float target = lag_ > 0.0f ? 1.0f + catchMax_ * std::min(1.0f, lag_ / (0.010f * fs_)) : 1.0f;
                r_ += catchCoeff_ * (target - r_);
                if (lag_ <= 0.0f && std::fabs(r_ - 1.0f) < 1e-5f) r_ = 1.0f;
            }
            break;
        case State::Drag:
            if (dragPhase_ == 1) {
                if (--dragHold_ <= 0) {
                    dragPhase_ = 2;
                    segment(1.0f, 300.0f, State::Drag);
                }
            } else if (ramp()) {
                if (dragPhase_ == 0) dragPhase_ = 1;
                else state_ = State::Run;
            }
            break;
        case State::Stopping:
            if (ramp()) {
                state_      = State::Stopped;
                stoppedFor_ = 0;
                mute_.setTarget(0.0f);
            }
            break;
        case State::Stopped:
            r_ = 0.0f;
            if (autoRestart_ && ++stoppedFor_ >= samples(150.0f, fs_)) {
                autoRestart_ = false;
                start();
            }
            break;
        case State::Starting:
            if (ramp()) {
                // Spinning up from 0 always leaves lag behind (half the ramp's
                // length); skip it in one 20 ms crossfade rather than racing.
                state_    = State::Run;
                skipping_ = lag_ > 0.0f;
                if (skipping_) skip_.setTarget(1.0f);
            }
            break;
    }

    // While stopped the read head does not move, but the wet is silent and
    // the restart begins afresh, so the lag simply stays where it was.
    if (state_ != State::Stopped) lag_ = std::min(std::max(lag_ + (1.0f - r_), 0.0f), maxLag_);

    if (skipping_) {
        o.skip = skip_.next();
        if (o.skip >= 1.0f) {
            lag_      = 0.0f;
            skipping_ = false;
            skip_.snap(0.0f);
            o.skip = 0.0f;
            if (pendingStop_ > 0.0f) stop(pendingStop_), pendingStop_ = 0.0f;
        }
    }

    if (wobbleLen_ > 0) {
        // 12 Hz, 0.5 ms peak, under a Hann window so it starts and ends at 0.
        const float t = static_cast<float>(wobblePos_) / static_cast<float>(wobbleLen_);
        o.wobble = 0.0005f * fs_ * std::sin(kTwoPi * 12.0f * static_cast<float>(wobblePos_) / fs_) *
                   (0.5f - 0.5f * std::cos(kTwoPi * t));
        if (++wobblePos_ >= wobbleLen_) wobbleLen_ = 0;
    }

    o.lag   = lag_;
    o.mute  = mute_.next();
    o.speed = r_;
    return o;
}

// --- Dropouts -----------------------------------------------------------------

void Dropouts::setup(float sampleRate) {
    fs_      = sampleRate;
    attack_  = samples(3.0f, fs_);
    release_ = samples(20.0f, fs_);
}

void Dropouts::reset(Xorshift32& rng) {
    events_.reset(rng);
    pos_ = len_ = 0;
}

Dropouts::Out Dropouts::next(float u, bool enabled, Xorshift32& rng) {
    Out o;
    // Spec: 2 u^2 per second (v0.5).
    // v0.14: 2 u^3 a second, up to 6 + 24 u^2 dB: the first half stays light.
    if (events_.next(enabled ? 2.0f * u * u * u : 0.0f, fs_, rng) && pos_ >= len_) {
        // 20-250 ms, log-uniform; depth up to 6 + 24 u dB, each 50-100% of it.
        len_     = samples(20.0f * std::exp(std::log(250.0f / 20.0f) * rng.uniform()), fs_);
        depthDb_ = (6.0f + 24.0f * u * u) * (0.5f + 0.5f * rng.uniform());
        pos_     = 0;
        o.started = true;
    }
    if (pos_ < len_) {
        float db;
        if (pos_ < attack_) db = depthDb_ * static_cast<float>(pos_) / static_cast<float>(attack_);
        else if (len_ - pos_ < release_) db = depthDb_ * static_cast<float>(len_ - pos_) / static_cast<float>(release_);
        else db = depthDb_;
        db       = std::min(db, depthDb_);
        o.gain   = std::pow(10.0f, -db / 20.0f);
        // The top goes first: 2 kHz at 30 dB down, opening to 20 kHz at 0.
        const float fc = std::min(20000.0f * std::pow(10.0f, -db / 30.0f), 0.45f * fs_);
        o.lowpass      = 1.0f - std::exp(-kTwoPi * fc / fs_);
        ++pos_;
    }
    return o;
}

// --- Crinkles -----------------------------------------------------------------

void Crinkles::setup(float sampleRate) {
    fs_  = sampleRate;
    aHp_ = 1.0f - std::exp(-kTwoPi * std::min(2000.0f, 0.45f * fs_) / fs_);
}

void Crinkles::reset(Xorshift32& rng) {
    events_.reset(rng);
    pos_ = len_ = 0;
    hp_  = 0.0f;
}

Crinkles::Out Crinkles::next(float ratePerSecond, Xorshift32& rng) {
    Out o;
    if (events_.next(ratePerSecond, fs_, rng) && pos_ >= len_) {
        len_ = samples(2.0f + 8.0f * rng.uniform(), fs_);
        pos_ = 0;
        hp_  = 0.0f;
        o.started = true;
    }
    if (pos_ < len_) {
        const float w     = std::sin(kPi * static_cast<float>(pos_) / static_cast<float>(len_));
        const float white = 2.0f * rng.uniform() - 1.0f;
        hp_               = hp_ + aHp_ * (white - hp_);
        o.gain            = 1.0f - 0.748811f * w; // -12 dB at the middle
        o.noise           = kPeak * w * std::min(std::max(3.0f * (white - hp_), -1.0f), 1.0f);
        ++pos_;
    }
    return o;
}

} // namespace tfx::fx::failure
