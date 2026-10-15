#include "tfx/panel.h"

#include <cmath>

namespace tfx {

void PanelDriver::bind(Effect* effect, PanelObserver* observer) {
    effect_   = effect;
    observer_ = observer;
}

void PanelDriver::prepare(float sampleRate, GestureTiming timing) {
    gestures_.prepare(sampleRate, timing);
}

float PanelDriver::controlValue(Control control, const PanelState& panel) const {
    switch (control) {
        case Control::Knob1:   return panel.knobs[0];
        case Control::Knob2:   return panel.knobs[1];
        case Control::Knob3:   return panel.knobs[2];
        case Control::Knob4:   return panel.knobs[3];
        case Control::Knob5:   return panel.knobs[4];
        case Control::Knob6:   return panel.knobs[5];
        case Control::Toggle1: return panel.toggles[0] ? 1.0f : 0.0f;
        case Control::Toggle2: return panel.toggles[1] ? 1.0f : 0.0f;
        case Control::Toggle3: return panel.toggles[2] ? 1.0f : 0.0f;
        case Control::Toggle4: return panel.toggles[3] ? 1.0f : 0.0f;
        case Control::None:
        default:               return -1.0f;
    }
}

void PanelDriver::send(const ParamSpec& spec, float normalized, uint64_t now) {
    effect_->setParam(spec.id, normalized);
    if (observer_) observer_->onParam(spec, normalized, now);
}

void PanelDriver::sync(const PanelState& panel, uint64_t now) {
    const ParamTable& table = effect_->params();
    for (size_t i = 0; i < table.count; ++i) {
        const ParamSpec& spec = table.specs[i];
        if (spec.control != Control::None) send(spec, controlValue(spec.control, panel), now);
    }
    last_ = panel;
}

void PanelDriver::apply(const PanelState& panel, uint64_t now) {
    const ParamTable& table = effect_->params();
    for (size_t i = 0; i < table.count; ++i) {
        const ParamSpec& spec = table.specs[i];
        if (spec.control == Control::None) continue;
        const float value = controlValue(spec.control, panel);
        const float prev  = controlValue(spec.control, last_);
        const bool  isKnob = spec.control >= Control::Knob1 && spec.control <= Control::Knob6;
        const bool  changed = isKnob ? std::fabs(value - prev) >= kKnobDeadband : value != prev;
        // A knob that reaches either end always lands exactly there, even if
        // the last step was smaller than the deadband.
        const bool  hitEnd = isKnob && value != prev && (value == 0.0f || value == 1.0f);
        if (changed || hitEnd) send(spec, value, now);
    }
    // Only knobs that were actually sent advance their reference value, so a
    // slow turn accumulates until it crosses the deadband.
    for (size_t k = 0; k < PanelState::kKnobs; ++k) {
        const float diff = std::fabs(panel.knobs[k] - last_.knobs[k]);
        const bool  end  = panel.knobs[k] != last_.knobs[k] &&
                           (panel.knobs[k] == 0.0f || panel.knobs[k] == 1.0f);
        if (diff >= kKnobDeadband || end) last_.knobs[k] = panel.knobs[k];
    }
    for (size_t t = 0; t < PanelState::kToggles; ++t) last_.toggles[t] = panel.toggles[t];

    for (uint8_t s = 0; s < PanelState::kStomps; ++s) gestures_.update(s, panel.stomps[s], now);
    for (size_t g = 0; g < gestures_.pendingCount(); ++g) {
        const Gesture& gesture = gestures_.pending(g);
        effect_->onGesture(gesture);
        if (observer_) observer_->onGesture(gesture, now);
    }
    gestures_.clearPending();
    for (size_t s = 0; s < PanelState::kStomps; ++s) last_.stomps[s] = panel.stomps[s];
}

bool PanelDriver::setParamByKey(const char* key, float normalized, uint64_t now) {
    const ParamSpec* spec = effect_->params().find(key);
    if (!spec) return false;
    send(*spec, normalized, now);
    return true;
}

bool PanelDriver::setParamById(ParamId id, float normalized, uint64_t now) {
    const ParamSpec* spec = effect_->params().byId(id);
    if (!spec) return false;
    send(*spec, normalized, now);
    return true;
}

} // namespace tfx
