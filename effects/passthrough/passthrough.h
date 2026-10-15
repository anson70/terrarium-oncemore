#pragma once
// Passthrough: the smallest possible effect, used to prove the whole pipeline.
//
//   Knob 1  Volume, 0..1 linear gain (fully clockwise = unity, bit-exact)
//   LED 1   shows the volume
//   LED 2   flashes on every footswitch gesture, at a brightness that tells
//           the gestures apart: tap 25%, double tap 50%, hold 75%, release 100%

#include "tfx/effect.h"

namespace tfx::fx {

class Passthrough final : public Effect {
public:
    enum : ParamId { kVolume = 0 };

    const char*       name() const override { return "passthrough"; }
    const char*       help() const override {
        return TFX_HELP("For checking the pedal and the hosts. LED 1 shows the volume. LED 2 flashes on "
                        "every footswitch gesture: tap 25%, double tap 50%, hold 75%, release 100%.");
    }
    const ParamTable& params() const override;

    void prepare(const PrepareInfo& info) override;
    void reset() override;
    void setSeed(uint32_t) override {}
    void setParam(ParamId id, float normalized) override;
    void onGesture(const Gesture& gesture) override;
    void process(const ProcessContext& ctx) override;
    LedState leds() const override;

private:
    static constexpr float kFlashMs = 150.0f;

    Smoother volume_;
    float    volumeNorm_     = 1.0f;
    bool     started_        = false; // before the first block, params snap instead of ramp
    float    flashLevel_     = 0.0f;
    long     flashRemaining_ = 0;     // samples left in the current LED 2 flash
    long     flashLength_    = 0;
};

} // namespace tfx::fx
