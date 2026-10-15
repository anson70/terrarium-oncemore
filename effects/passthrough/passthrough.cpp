#include "passthrough.h"

#include <cmath>

namespace tfx::fx {

namespace {
constexpr ParamSpec kSpecs[] = {
    {Passthrough::kVolume, "volume", "Volume", "", 0.0f, 1.0f, Curve::Linear, 1.0f, 20.0f, Control::Knob1,
     TFX_HELP("Output level. Fully clockwise passes the input through unchanged.")},
};
constexpr ParamTable kTable{kSpecs, sizeof(kSpecs) / sizeof(kSpecs[0])};
} // namespace

const ParamTable& Passthrough::params() const { return kTable; }

void Passthrough::prepare(const PrepareInfo& info) {
    const ParamSpec& spec = kSpecs[kVolume];
    volume_.prepare(info.sampleRate, spec.smoothingMs, toReal(spec, volumeNorm_));
    flashLength_ = std::lround(kFlashMs * 0.001f * info.sampleRate);
    reset();
}

void Passthrough::reset() {
    volume_.snap(volume_.target());
    started_        = false;
    flashRemaining_ = 0;
    flashLevel_     = 0.0f;
}

void Passthrough::setParam(ParamId id, float normalized) {
    if (id != kVolume) return;
    volumeNorm_ = normalized;
    const float real = toReal(kSpecs[kVolume], normalized);
    if (started_) volume_.setTarget(real);
    else          volume_.snap(real);
}

void Passthrough::onGesture(const Gesture& gesture) {
    switch (gesture.type) {
        case GestureType::Tap:       flashLevel_ = 0.25f; break;
        case GestureType::DoubleTap: flashLevel_ = 0.50f; break;
        case GestureType::Hold:      flashLevel_ = 0.75f; break;
        case GestureType::Release:   flashLevel_ = 1.00f; break;
    }
    flashRemaining_ = flashLength_;
}

void Passthrough::process(const ProcessContext& ctx) {
    started_ = true;
    for (size_t i = 0; i < ctx.frames; ++i) ctx.out[i] = ctx.in[i] * volume_.next();

    flashRemaining_ -= static_cast<long>(ctx.frames);
    if (flashRemaining_ < 0) flashRemaining_ = 0;
}

LedState Passthrough::leds() const {
    LedState s;
    s.led[0] = volumeNorm_;
    s.led[1] = flashRemaining_ > 0 ? flashLevel_ : 0.0f;
    return s;
}

} // namespace tfx::fx
