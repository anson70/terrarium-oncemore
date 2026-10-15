#include "noise.h"

#include <algorithm>
#include <cmath>

namespace tfx::fx::noise {

namespace {
constexpr float kPi    = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

float onePoleCoeff(float hz, float fs) { return 1.0f - std::exp(-kTwoPi * std::min(hz, 0.45f * fs) / fs); }
} // namespace

// --- Hiss ---------------------------------------------------------------------

void Hiss::setup(float sampleRate) {
    aHp_ = onePoleCoeff(400.0f, sampleRate);
    aLp_ = onePoleCoeff(12000.0f, sampleRate);
    // Unit RMS: the shaping filters' power gain, integrated over 0..pi from
    // their frequency responses, times the variance of uniform noise (1/3).
    constexpr int kPoints = 2048;
    float         power   = 0.0f;
    for (int i = 0; i < kPoints; ++i) {
        const float w  = kPi * (static_cast<float>(i) + 0.5f) / kPoints;
        const float cw = std::cos(w), sw = std::sin(w);
        // One-pole low-pass a / (1 - (1 - a) z^-1); the high-pass is 1 minus one.
        auto lowpass = [&](float a, float& re, float& im) {
            const float dr = 1.0f - (1.0f - a) * cw, di = (1.0f - a) * sw; // 1 - (1-a) e^-jw
            const float d  = dr * dr + di * di;
            re = a * dr / d, im = -a * di / d;
        };
        float hr, hi, lr, li;
        lowpass(aHp_, hr, hi);
        hr = 1.0f - hr, hi = -hi;
        lowpass(aLp_, lr, li);
        const float mr = hr * lr - hi * li, mi = hr * li + hi * lr;
        power += mr * mr + mi * mi;
    }
    power /= kPoints;
    norm_ = 1.0f / std::sqrt(power / 3.0f);
}

float Hiss::next(Xorshift32& rng) {
    const float white = 2.0f * rng.uniform() - 1.0f;
    hp_ = flushDenormal(hp_ + aHp_ * (white - hp_));
    lp_ = flushDenormal(lp_ + aLp_ * ((white - hp_) - lp_));
    return lp_ * norm_;
}

// --- Hum ----------------------------------------------------------------------

float Hum::next() {
    phase_ += hz_ / fs_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    const float p = kTwoPi * phase_;
    // 1, 1/2, 1/4: RMS sqrt((1 + 1/4 + 1/16) / 2) = 0.810093
    return (std::sin(p) + 0.5f * std::sin(2.0f * p) + 0.25f * std::sin(3.0f * p)) * (1.0f / 0.810093f);
}

// --- Interference -------------------------------------------------------------

void Interference::setup(float sampleRate) {
    fs_ = sampleRate;
    // Harmonics of the buzz up to 4 kHz (and below Nyquist): M = 2 n + 1.
    const int n = static_cast<int>(std::min(4000.0f, 0.45f * fs_) / kBuzzHz);
    harmonics_  = 2 * n + 1;
    envCoeff_   = 1.0f - std::exp(-1.0f / (2.0f * fs_)); // time constant 2 s
    envPeriod_  = static_cast<int>(2.0f * fs_);          // a new target every ~2 s
    aHp_        = onePoleCoeff(1000.0f, fs_);
    aLp_        = onePoleCoeff(6000.0f, fs_);
}

void Interference::reset() {
    buzzPhase_ = whinePhase_ = 0.0f;
    env_ = envTarget_ = 0.5f;
    envLeft_     = 0;
    crackleLeft_ = 0;
    hp_ = lp_ = 0.0f;
}

Interference::Parts Interference::next(float speed, Xorshift32& rng) {
    // Four draws every sample, used or not.
    const float uEnv = rng.uniform(), uEvent = rng.uniform(), uLen = rng.uniform();
    const float white = 2.0f * rng.uniform() - 1.0f;
    Parts       p;

    // Buzz: BLIT sin(pi M x) / (M sin(pi x)) peaks at 1 once per period; its
    // mean is 1/M, removed so the buzz has no DC.
    buzzPhase_ += kBuzzHz / fs_;
    if (buzzPhase_ >= 1.0f) buzzPhase_ -= 1.0f;
    const float m   = static_cast<float>(harmonics_);
    const float den = m * std::sin(kPi * buzzPhase_);
    const float blit = std::fabs(den) < 1e-6f ? 1.0f : std::sin(kPi * m * buzzPhase_) / den;
    if (--envLeft_ <= 0) {
        envTarget_ = uEnv;
        envLeft_   = envPeriod_;
    }
    env_       = flushDenormal(env_ + envCoeff_ * (envTarget_ - env_));
    p.envelope = env_;
    p.buzz     = kBuzzPeak * env_ * (blit - 1.0f / m) / (1.0f - 1.0f / m);

    // Whine: its pitch rides on the tape speed.
    whinePhase_ += kWhineHz * std::max(speed, 0.0f) / fs_;
    if (whinePhase_ >= 1.0f) whinePhase_ -= std::floor(whinePhase_);
    p.whine = kWhinePeak * std::sin(kTwoPi * whinePhase_);

    // Crackles: Poisson events, each a burst of band-passed noise decaying
    // linearly over 1-5 ms. The noise is driven hard into a clip so a burst
    // starts at the full -36 dBFS.
    hp_ = flushDenormal(hp_ + aHp_ * (white - hp_));
    lp_ = flushDenormal(lp_ + aLp_ * ((white - hp_) - lp_));
    if (crackleLeft_ == 0 && uEvent < kCrackleRate / fs_) {
        crackleLen_  = std::max(1, static_cast<int>((1.0f + 4.0f * uLen) * 0.001f * fs_));
        crackleLeft_ = crackleLen_;
    }
    if (crackleLeft_ > 0) {
        const float env = static_cast<float>(crackleLeft_) / static_cast<float>(crackleLen_);
        p.crackle       = kCracklePeak * env * std::min(std::max(8.0f * lp_, -1.0f), 1.0f);
        --crackleLeft_;
    }
    return p;
}

// --- Digital ------------------------------------------------------------------

void Digital::setup(float sampleRate) {
    fs_ = sampleRate;
    // Peak follower, 5 ms up and 300 ms down (the branching detector of
    // Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design
    // - A Tutorial and Analysis", JAES 60(6), 2012).
    attack_  = 1.0f - std::exp(-1.0f / (0.005f * fs_));
    release_ = 1.0f - std::exp(-1.0f / (0.300f * fs_));
}

float Digital::process(float x, float ft, float bits, Xorshift32& rng) {
    const float ax = std::fabs(x);
    env_ = flushDenormal(env_ + (ax > env_ ? attack_ : release_) * (ax - env_));
    // TPDF dither: two uniform draws in -1/2..1/2 (Lipshitz, Wannamaker &
    // Vanderkooy, "Quantization and Dither: A Theoretical Survey", JAES 1992).
    const float r = rng.uniform() + rng.uniform() - 1.0f;
    if (bits != lastBits_) {
        lastBits_ = bits;
        q_        = std::exp2(1.0f - bits); // step 2^-(B-1)
    }
    // Zero-order hold at ft: a phase accumulator; a new value when it wraps.
    phase_ += ft / fs_;
    if (phase_ >= 1.0f) {
        phase_ -= std::floor(phase_);
        const float gain = agc_ ? std::min(kAgcTarget / std::max(env_, kAgcTarget / kAgcMaxGain), kAgcMaxGain)
                                : 1.0f;
        held_ = q_ * std::round(x * gain / q_ + r) / gain;
    }
    return held_;
}

} // namespace tfx::fx::noise
