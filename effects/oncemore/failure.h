#pragma once
// OnceMore's failures (milestone N4): the transport (drags, wobbles, tape
// stops and restarts), oxide dropouts and crinkles. effects/oncemore/spec.md,
// "Dropout 與 Failure" and the v0.13 notes. Events are Poisson processes run
// by integrating their rate against an exponential threshold, so a rate that
// changes (the Failure knob turning) is followed exactly, and random numbers
// are drawn only at events: the output never depends on the block size.

#include <cstdint>

#include "tfx/dsp.h"

namespace tfx::fx::failure {

// One Poisson process: next() is true at an event. Draws once per event.
class Poisson {
public:
    void reset(Xorshift32& rng);
    bool next(float ratePerSecond, float sampleRate, Xorshift32& rng);

private:
    float acc_ = 0.0f, threshold_ = 1.0f;
};

// The tape's speed ratio r (1 = normal) and the lag it builds up in the delay
// line, lag' = 1 - r per sample.
class Transport {
public:
    enum class State : uint8_t { Run, Drag, Stopping, Stopped, Starting };

    struct Out {
        float lag    = 0.0f; // samples of extra delay for the read head
        float wobble = 0.0f; // samples, added on top
        float mute   = 1.0f; // gain for the tape (0 while stopped)
        float skip   = 0.0f; // 0..1: crossfade from the lagging head to one at no lag
        float speed  = 1.0f; // r
    };

    void  setup(float sampleRate, float maxLagSamples);
    void  reset();
    State state() const { return state_; }
    bool  busy() const { return state_ != State::Run || skipping_ || wobbleLen_ > 0; }

    // depth01 scales the drag: r falls to 1 - 0.06 * depth01. u1, u2 are
    // random 0..1 for the drag's depth share and its hold time.
    void drag(float depth01, float u1, float u2);
    void wobble();
    void stop(float stopMs); // r to 0 over stopMs, then silence
    void start();            // r back to 1 over 300 ms, then a 20 ms skip to no lag
    // A stop that restarts by itself after 150 ms of silence (Failure's own).
    void autoStop(float stopMs);

    Out next();

private:
    void segment(float to, float ms, State s);

    float fs_ = 48000.0f, maxLag_ = 1.0f;
    State state_ = State::Run;
    float r_ = 1.0f, lag_ = 0.0f;
    // A linear segment of r, from r0_ to r1_ over len_ samples.
    float r0_ = 1.0f, r1_ = 1.0f;
    int   pos_ = 0, len_ = 1;
    // Drag: down, hold, up
    int   dragPhase_ = 0, dragHold_ = 0;
    float dragMin_ = 1.0f;
    // Wobble
    int   wobblePos_ = 0, wobbleLen_ = 0;
    // Stop and start
    Ramp  mute_;
    bool  autoRestart_ = false;
    int   stoppedFor_ = 0;
    Ramp  skip_;
    bool  skipping_ = false;
    float pendingStop_ = 0.0f; // a stop asked for during a skip, in ms
    float catchCoeff_ = 0.0f;
    float catchMax_   = 0.03f; // over speed when catching up, set by the last drag
};

// Oxide dropouts: the level falls 6-30 dB (deeper with u) for 20-250 ms with a
// 3 ms attack and 20 ms recovery, linear in dB, and the top end goes first.
class Dropouts {
public:
    struct Out {
        float gain = 1.0f;     // linear
        float lowpass = 1.0f;  // one-pole coefficient (1 = open)
        bool  started = false;
    };
    void setup(float sampleRate);
    void reset(Xorshift32& rng);
    Out  next(float u, bool enabled, Xorshift32& rng);

private:
    float   fs_ = 48000.0f;
    Poisson events_;
    int     pos_ = 0, len_ = 0, attack_ = 1, release_ = 1;
    float   depthDb_ = 0.0f;
};

// Crinkles: 2-10 ms clicks, a -12 dB dip in the tape with a burst of noise
// above 2 kHz on top.
class Crinkles {
public:
    static constexpr float kPeak = 0.01f; // -40 dBFS (v0.13; the spec's -30 was full-scale thinking)

    struct Out {
        float gain = 1.0f, noise = 0.0f;
        bool  started = false;
    };
    void setup(float sampleRate);
    void reset(Xorshift32& rng);
    Out  next(float ratePerSecond, Xorshift32& rng);

private:
    float   fs_ = 48000.0f, aHp_ = 0.2f, hp_ = 0.0f;
    Poisson events_;
    int     pos_ = 0, len_ = 0;
};

} // namespace tfx::fx::failure
