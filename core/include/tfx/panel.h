#pragma once
// The Terrarium front panel, and the one place that turns panel movements into
// effect calls. Every host fills a PanelState from its own source (ADC pins,
// an on-screen panel, a MIDI controller, a test script) and hands it to the
// same PanelDriver, so controls behave identically everywhere.

#include <cstddef>
#include <cstdint>

#include "tfx/effect.h"
#include "tfx/gesture.h"

namespace tfx {

struct PanelState {
    static constexpr size_t kKnobs   = 6;
    static constexpr size_t kToggles = 4;
    static constexpr size_t kStomps  = 2;

    float knobs[kKnobs]     = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f}; // 0..1
    bool  toggles[kToggles] = {false, false, false, false};         // true = up
    bool  stomps[kStomps]   = {false, false};                       // true = pressed
};

// Receives everything the driver does, for logs and reports.
class PanelObserver {
public:
    virtual ~PanelObserver() = default;
    virtual void onParam(const ParamSpec& spec, float normalized, uint64_t now) = 0;
    virtual void onGesture(const Gesture& gesture, uint64_t now) = 0;
};

class PanelDriver {
public:
    // Knob moves smaller than this are ignored, to keep ADC noise from
    // flooding the effect with setParam calls on the pedal.
    static constexpr float kKnobDeadband = 1.0f / 1024.0f;

    void bind(Effect* effect, PanelObserver* observer = nullptr);
    void prepare(float sampleRate, GestureTiming timing = {});

    // Push every bound control to the effect, regardless of change.
    // Call once after prepare() so the effect starts where the panel is.
    void sync(const PanelState& panel, uint64_t now);

    // Apply what changed since the last call. Call at the start of each block.
    void apply(const PanelState& panel, uint64_t now);

    // Set a parameter directly by key, bypassing the panel (scripts, plugins).
    // Returns false if the key is unknown.
    bool setParamByKey(const char* key, float normalized, uint64_t now);

    // Same, by id. Returns false if the id is unknown.
    bool setParamById(ParamId id, float normalized, uint64_t now);

private:
    float controlValue(Control control, const PanelState& panel) const;
    void  send(const ParamSpec& spec, float normalized, uint64_t now);

    Effect*        effect_   = nullptr;
    PanelObserver* observer_ = nullptr;
    GestureService gestures_;
    PanelState     last_;
};

} // namespace tfx
