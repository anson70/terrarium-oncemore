#pragma once
// OnceMore: tape degradation (wow, flutter, saturation, head loss, noise,
// dropouts...). Behaviour, formulas and their sources: effects/oncemore/spec.md.
// Clean-room: written from that spec and the papers it cites only.
//
// Milestones N1-N4 (v1.0): transport, tape tone and machines, noise and
// digital, failures.
//   Knobs    Wow  Flutter  Saturate  /  Model  Failure  Mix
//   Toggles  Hiss  Hum  Static (interference)  FS2 hold (up: tape stop, down: Fail)
//   FS2      tap: one drag        hold: tape stop or Fail
//   FS1      tap: bypass          hold: Warp (wow and flutter to the max)
//   LED 1    on while active; with wow, its brightness follows the pitch drift
//
// Wet path: tape (wow, flutter) -> saturation (2x oversampled) -> + hiss ->
// head loss (linear-phase FIR) -> high-pass -> digital -> + hum, interference. Model picks the machine: its FIR and its
// high-pass. Wow and flutter are the knobs' alone.

#include <cstdint>

#include "oncemore/failure.h"
#include "oncemore/noise.h"
#include "oncemore/tape.h"
#include "tfx/dsp.h"
#include "tfx/effect.h"

namespace tfx::fx {

class OnceMore final : public Effect {
public:
    enum : ParamId {
        kWow = 0,
        kFlutter,
        kMix,
        kFlutterAm,
        kPerformanceMs,
        kDryAlign,
        kSaturate,
        kModel,
        kModelOverride,
        kBias,
        kHissOn,
        kHumOn,
        kInterferenceOn,
        kHissLevel,
        kHumLevel,
        kHumFreq,
        kInterferenceLevel,
        kDigitalDepth,
        kDigitalFilter,
        kLevel,
        kFailure,
        kFs2Action,
        kDropOn,
        kSnagOn,
        kCrinkleOn,
        kParamCount
    };

    OnceMore();

    const char*       name() const override { return "oncemore"; }
    const char*       help() const override {
        return TFX_HELP("Tape degradation, from a warm reel to a broken toy. Toggles: Hiss, Hum, Static, FS2 "
                        "hold. FS1: tap = bypass (the output is exactly the input), hold = Warp (wow and flutter to "
                        "the max until released). FS2: tap = one drag (the tape slows and catches up), hold = tape "
                        "stop or Fail (Toggle 4). LED 1: on while active; with wow it breathes with the pitch drift. "
                        "LED 2: flashes at every dropout, fault or crinkle; follows the tape speed during a stop; on "
                        "while Fail is held.");
    }
    const ParamTable& params() const override;

    void     prepare(const PrepareInfo& info) override;
    void     reset() override;
    void     setSeed(uint32_t seed) override;
    void     setParam(ParamId id, float normalized) override;
    void     onGesture(const Gesture& gesture) override;
    void     process(const ProcessContext& ctx) override;
    LedState leds() const override;
    int      latencySamples() const override { return d0_ + tape::Oversampler2::kLatency + firTaps_ / 2; }

    static constexpr int kMachines = 10; // 8 on the knob, 2 hidden

    // The machine in use (1-10), for tests and displays.
    int machine() const { return slotMachine_[active_] % 100; }

    // For tests: the wow + flutter offset from D0 used for the last sample, ms.
    float lastOffsetMs() const { return lastOffset_ * 1000.0f / fs_; }
    bool  bypassed() const { return bypassed_; }
    // For tests: the transport's extra lag (samples) and speed for the last sample.
    float lastLagSamples() const { return lastLag_; }
    float lastSpeed() const { return lastSpeed_; }
    // The longest delay the read head may reach, in samples.
    float maxDelaySamples() const { return static_cast<float>(size_ > 4 ? size_ - 4 : 0); }

private:
    // A smooth random swing between -1 and 1: each half cycle eases (raised
    // cosine) from the last target to a new one of the opposite sign. Bounded
    // by construction, and its steepest slope is that of a unit sine at the
    // same rate, which keeps the pitch deviation predictable (spec: Wow and
    // Flutter).
    struct Swing {
        float from = 0.0f, to = 0.0f, phase = 0.0f, stretch = 1.0f;
        float sign = 1.0f;

        void  restart(Xorshift32& rng);
        float next(float halfCyclesPerSample, Xorshift32& rng);
        // Slope relative to a unit sine's peak slope; about -1..1.
        float slope() const;

    private:
        void newTarget(Xorshift32& rng);
    };

    void setSmoothed(Smoother& s, float value);
    void chooseMachine();
    void updateNoise();
    void startFade();
    float firDot(int machine) const;
    void setRamp(Ramp& r, float value);
    float swingSamples(float cents, float hz, float maxMs) const;
    void updateWow(float k);
    void updateFlutter(float k);

    // Tape: the input, from the host's allocator (SDRAM on the pedal)
    float* buf_   = nullptr;
    size_t size_  = 0;
    size_t write_ = 0;
    int    d0_    = 192; // samples

    float    fs_      = 48000.0f;
    bool     started_ = false;
    uint32_t seed_    = 1;
    float    norm_[kParamCount] = {};

    Smoother wowKnob_, flutterKnob_, mix_, flutterAm_;
    Ramp     warp_, bypass_, align_;
    bool     warping_ = false, bypassed_ = false, dryAlign_ = false;

    // Wow and flutter generators; amplitudes in samples, rates in half cycles
    // per sample, recomputed only when the effective knob value moves
    Xorshift32 wowRng_, flutterRng_;
    Swing      wowSwing_, flutterSwing_;
    float      wowSine_ = 0.0f; // phase, cycles
    float      wowK_ = -1.0f, wowAmp_ = 0.0f, wowStep_ = 0.0f;
    float      flutterK_ = -1.0f, flutterAmp_ = 0.0f, flutterStep_ = 0.0f, flutterDepth_ = 0.0f;

    // Tape tone (N2)
    tape::Saturator sat_;
    Smoother        drive_, bias_;
    float*          fir_     = nullptr; // kMachines x firTaps_ coefficients, reversed
    float*          firHist_ = nullptr; // 2 x firTaps_, doubled so a window never wraps
    int             firTaps_ = 65, firPos_ = 0;

    // Machines: the knob (with hysteresis), the override, and two slots that
    // crossfade when the machine changes
    int            knobMachine_ = 2, override_ = 0, wanted_ = 2;
    int            slotMachine_[2] = {2, 2};
    tape::HighPass hp_[2];
    int            active_ = 0;
    bool           fading_ = false;
    Ramp           fade_;

    // Noise floor and digital section (N3)
    noise::Hiss         hiss_;
    noise::Hum          hum_;
    noise::Interference interference_;
    noise::Digital      digital_;
    Xorshift32          hissRng_, interfRng_, digitalRng_;
    Smoother            hissGain_, humGain_, interfGain_, level_;
    Smoother            digitalAmount_, digitalLnFt_, digitalBits_;
    bool                hissOn_ = true, humOn_ = false, interfOn_ = false, digitalFilter_ = true;
    float               hissLevelDb_ = 0.0f, humLevelDb_ = -66.0f, interfLevelDb_ = 0.0f, digitalDepth_ = 1.0f;
    float               lastDelay_ = 192.0f;

    // Failures (N4)
    failure::Transport transport_;
    failure::Dropouts  dropouts_;
    failure::Crinkles  crinkles_;
    failure::Poisson   snagEvents_;
    Xorshift32         dropRng_, snagRng_, crinkleRng_;
    Smoother           failureKnob_;
    Ramp               fail_;
    float              perfMs_ = 200.0f, dropLp_ = 0.0f, lastLag_ = 0.0f, lastSpeed_ = 1.0f, led2_ = 0.0f;
    bool               fs2Stop_ = true, heldStop_ = true, failHeld_ = false;
    bool               dropOn_ = true, snagOn_ = true, crinkleOn_ = true;
    int                flash_ = 0, flashLen_ = 3840;

    float lastOffset_ = 0.0f; // samples
    float led1_       = 1.0f;
};

} // namespace tfx::fx
