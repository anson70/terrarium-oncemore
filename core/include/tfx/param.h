#pragma once
// Parameter definitions shared by every host.
//
// An effect describes its parameters once, as data. The Daisy host maps them
// to physical knobs and toggles, the live host draws them on screen, and the
// offline host lets a script set them by id. No host contains per-effect code.

#include <cmath>
#include <cstddef>
#include <cstdint>

// Help texts are for desktop hosts only: the firmware build defines
// TFX_NO_HELP so they cost no flash on the pedal.
#ifdef TFX_NO_HELP
#define TFX_HELP(text) nullptr
#else
#define TFX_HELP(text) text
#endif

namespace tfx {

using ParamId = uint16_t;

// How a normalized 0..1 control position maps to the real value.
enum class Curve : uint8_t {
    Linear,  // min + n * (max - min)
    Exp,     // min * (max / min) ^ n        -- requires min > 0 and max > 0
    Stepped, // round(min + n * (max - min)) -- for toggles and mode selectors
};

// Which physical control a parameter is bound to by default.
enum class Control : uint8_t {
    None,
    Knob1, Knob2, Knob3, Knob4, Knob5, Knob6,
    Toggle1, Toggle2, Toggle3, Toggle4,
};

struct ParamSpec {
    ParamId     id;
    const char* key;          // stable identifier used in scripts and presets, e.g. "volume"
    const char* name;         // display name
    const char* unit;         // display unit, e.g. "dB", "ms", "" for none
    float       min;
    float       max;
    Curve       curve;
    float       defaultNorm;  // default position, 0..1
    float       smoothingMs;  // one-pole smoothing time; 0 = no smoothing
    Control     control;
    const char* help = nullptr; // what it does, for host UIs; wrap in TFX_HELP()
};

struct ParamTable {
    const ParamSpec* specs;
    size_t           count;

    const ParamSpec* find(const char* key) const;
    const ParamSpec* byId(ParamId id) const;
};

// Normalized 0..1 -> real value according to the spec's curve.
float toReal(const ParamSpec& spec, float normalized);

// Real value -> normalized 0..1 (inverse of toReal, clamped).
float toNormalized(const ParamSpec& spec, float real);

// One-pole smoother advanced once per sample.
//
// Initialised directly to its target, so a parameter left at its default
// produces no ramp at all -- a passthrough at unity stays bit-exact.
class Smoother {
public:
    void prepare(float sampleRate, float timeMs, float initial);
    void setTarget(float target) { target_ = target; }
    void snap(float value) { current_ = target_ = value; }

    float next() {
        const float stepped = current_ + coeff_ * (target_ - current_);
        // In float, a slow smoother stalls short of its target once each step
        // rounds to nothing (e.g. stuck at 0.50003 instead of 0.5). Snap when
        // the step stops making progress or the gap is negligible, so a
        // finished ramp lands exactly and leaves no denormal tail.
        if (stepped == current_ || std::fabs(target_ - stepped) < 1e-7f) current_ = target_;
        else current_ = stepped;
        return current_;
    }

    float current() const { return current_; }
    float target() const { return target_; }
    bool  settled() const { return current_ == target_; }

private:
    float coeff_   = 1.0f;
    float current_ = 0.0f;
    float target_  = 0.0f;
};

} // namespace tfx
