#pragma once
// OnceMore's noise floor and digital section (milestone N3): tape hiss, mains
// hum, the machine's own interference, and sample-rate / bit reduction.
// effects/oncemore/spec.md, sections "Digital" and "雜訊". Every random draw comes
// from the Xorshift32 passed in, a fixed number per sample, so the output
// never depends on the block size.

#include <cstdint>

#include "tfx/dsp.h"

namespace tfx::fx::noise {

// Tape hiss: white noise through a one-pole high-pass at 400 Hz and a
// one-pole low-pass at 12 kHz (J. O. Smith, Introduction to Digital Filters,
// "One-Pole"), scaled to unit RMS. One draw per sample.
class Hiss {
public:
    void  setup(float sampleRate);
    void  reset() { hp_ = lp_ = 0.0f; }
    float next(Xorshift32& rng);
    float norm() const { return norm_; }

private:
    float aHp_ = 0.05f, aLp_ = 0.8f, norm_ = 1.0f;
    float hp_ = 0.0f, lp_ = 0.0f;
};

// Mains hum: fundamental plus 2nd and 3rd harmonics at -6 and -12 dB, scaled
// to unit RMS. No randomness.
class Hum {
public:
    void  setup(float sampleRate) { fs_ = sampleRate; }
    void  reset() { phase_ = 0.0f; }
    void  setFrequency(float hz) { hz_ = hz; }
    float next();

private:
    float fs_ = 48000.0f, hz_ = 60.0f, phase_ = 0.0f;
};

// The machine's own interference (our design, spec "干擾"): a head-switching
// buzz, a motor whine that follows the tape speed, and crackles.
class Interference {
public:
    static constexpr float kBuzzHz      = 59.94f;
    // Levels (v0.12) relative to a guitar at about -30 dBFS RMS
    static constexpr float kBuzzPeak    = 0.00199526f;  // -54 dBFS
    static constexpr float kWhineHz     = 1200.0f;
    static constexpr float kWhinePeak   = 0.000251189f; // -72 dBFS (sine peak)
    static constexpr float kCracklePeak = 0.00794328f;  // -42 dBFS
    static constexpr float kCrackleRate = 1.5f;         // per second

    struct Parts {
        float buzz = 0.0f, whine = 0.0f, crackle = 0.0f;
        float envelope = 0.0f; // the buzz's tracking envelope, 0..1
        float sum() const { return buzz + whine + crackle; }
    };

    void  setup(float sampleRate);
    void  reset();
    // speed: the tape's speed ratio right now (1 = normal). Four draws.
    Parts next(float speed, Xorshift32& rng);

private:
    float fs_ = 48000.0f;
    // Buzz: band-limited impulse train (Stilson & Smith, "Alias-Free Digital
    // Synthesis of Classic Analog Waveforms", ICMC 1996), harmonics to 4 kHz
    int   harmonics_ = 133; // odd: M in sin(pi M x) / (M sin(pi x))
    float buzzPhase_ = 0.0f;
    float env_ = 0.5f, envTarget_ = 0.5f, envCoeff_ = 0.0f;
    int   envLeft_ = 0, envPeriod_ = 96000;
    // Whine
    float whinePhase_ = 0.0f;
    // Crackles: 1-5 ms bursts of 1-6 kHz noise
    int   crackleLeft_ = 0, crackleLen_ = 1;
    float aHp_ = 0.1f, aLp_ = 0.5f, hp_ = 0.0f, lp_ = 0.0f;
};

// Digital degradation: zero-order hold at ft, then mid-tread quantisation to
// `bits` (continuous) with TPDF dither, taken when a new sample is held.
// Two draws per sample. The caller skips it entirely when it is off.
//
// With the AGC on (spec v0.11), the signal is brought up towards -6 dBFS
// before the converter and back down after, as the automatic gain control of
// a cheap recorder would: a guitar at -30 dBFS keeps the grit of B bits
// instead of drowning in a noise floor set for full scale. The gain is held
// with each sample, so the noise follows the playing and sinks when it stops.
class Digital {
public:
    static constexpr float kAgcTarget  = 0.5f;      // -6 dBFS
    static constexpr float kAgcMaxGain = 63.0957f;  // +36 dB

    void  setup(float sampleRate);
    void  reset() { phase_ = 1.0f, held_ = 0.0f, env_ = 0.0f; }
    void  setAgc(bool on) { agc_ = on; }
    // ft <= sample rate; bits 1..16.
    float process(float x, float ft, float bits, Xorshift32& rng);

private:
    float fs_ = 48000.0f, phase_ = 1.0f, held_ = 0.0f;
    float lastBits_ = -1.0f, q_ = 1.0f;
    bool  agc_ = true;
    float env_ = 0.0f, attack_ = 0.0f, release_ = 0.0f;
};

} // namespace tfx::fx::noise
