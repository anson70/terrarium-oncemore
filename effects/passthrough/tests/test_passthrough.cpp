// M0 acceptance for the Passthrough effect, run through the offline host.

#include "doctest.h"

#include <cmath>
#include <string>
#include <vector>

#include "audio_io.h"
#include "engine.h"
#include "passthrough/passthrough.h"

using namespace tfx;
using namespace tfx::offline;
using namespace tfx::desktop;

namespace {

const char* kUnity = "initial: { knobs: [1.0, 0.5, 0.5, 0.5, 0.5, 0.5] }";

RunResult runPassthrough(const std::vector<float>& in, const std::string& script, RunOptions opt = {}) {
    fx::Passthrough fx;
    return run(fx, in, ControlScript::fromYamlString(script), opt);
}

} // namespace

TEST_CASE("unity gain reproduces the input bit for bit") {
    const auto in = makeSignal("noise:-1", 2.0, 48000.0f).samples;
    const auto r  = runPassthrough(in, kUnity);
    CHECK(r.output == in);
    CHECK(r.allocationsInProcess == 0);
}

TEST_CASE("bit-exact at any sample rate and block pattern") {
    for (float sr : {44100.0f, 48000.0f, 96000.0f}) {
        for (BlockPlan blocks : {BlockPlan{1, 1}, BlockPlan{48, 48}, BlockPlan{512, 512}, BlockPlan{1, 512}}) {
            CAPTURE(sr);
            CAPTURE(blocks.minBlock);
            CAPTURE(blocks.maxBlock);
            RunOptions opt;
            opt.sampleRate = sr;
            opt.blocks     = blocks;
            const auto in  = makeSignal("sweep:20:20000:-3", 0.5, sr).samples;
            CHECK(runPassthrough(in, kUnity, opt).output == in);
        }
    }
}

TEST_CASE("knob 1 sets the volume, smoothly") {
    const auto in = makeSignal("sine:440:-6", 1.0, 48000.0f).samples;
    const auto r  = runPassthrough(in, R"(
initial: { knobs: [1.0, 0.5, 0.5, 0.5, 0.5, 0.5] }
events: [ { t: 0.5, knob: 1, to: 0.25 } ]
)");
    // Before the move: untouched.
    for (size_t i = 0; i < 24000; ++i) REQUIRE(r.output[i] == in[i]);
    // Long after it: a quarter of the input.
    for (size_t i = 40000; i < 48000; ++i) REQUIRE(r.output[i] == doctest::Approx(0.25f * in[i]).epsilon(1e-4));
    // No jump: the largest step between neighbouring output samples stays
    // within what the 440 Hz sine itself produces.
    float maxStep = 0.0f, maxInStep = 0.0f;
    for (size_t i = 1; i < in.size(); ++i) {
        maxStep   = std::max(maxStep, std::fabs(r.output[i] - r.output[i - 1]));
        maxInStep = std::max(maxInStep, std::fabs(in[i] - in[i - 1]));
    }
    CHECK(maxStep <= maxInStep * 1.0001f);
}

TEST_CASE("the starting knob position applies from the first sample, without a ramp") {
    const auto in = makeSignal("sine:440:-6", 0.1, 48000.0f).samples;
    const auto r  = runPassthrough(in, "initial: { knobs: [0.5, 0.5, 0.5, 0.5, 0.5, 0.5] }");
    for (size_t i = 0; i < in.size(); ++i) REQUIRE(r.output[i] == 0.5f * in[i]);
}

TEST_CASE("LED 1 shows the volume") {
    const auto in = makeSignal("silence", 1.0, 48000.0f).samples;
    const auto r  = runPassthrough(in, R"(
initial: { knobs: [0.8, 0.5, 0.5, 0.5, 0.5, 0.5] }
events: [ { t: 0.5, knob: 1, to: 0.2 } ]
)");
    REQUIRE(r.leds.size() >= 2);
    CHECK(r.leds.front().led1 == doctest::Approx(0.8f));
    CHECK(r.leds.back().led1 == doctest::Approx(0.2f));
}

TEST_CASE("each gesture is logged as scripted and flashes LED 2 at its own level") {
    const auto in = makeSignal("silence", 3.0, 48000.0f).samples;
    const auto r  = runPassthrough(in, R"(
initial: { knobs: [1.0, 0.5, 0.5, 0.5, 0.5, 0.5] }
events:
  - { t: 0.20, stomp: 1, action: tap }
  - { t: 0.60, stomp: 1, action: tap }
  - { t: 0.75, stomp: 1, action: tap }
  - { t: 1.50, stomp: 1, action: hold, for: 1.0 }
)");
    struct Expect { double t; const char* type; float led; };
    const Expect expected[] = {
        {0.25, "tap", 0.25f}, {0.65, "tap", 0.25f}, {0.80, "double_tap", 0.5f},
        {2.00, "hold", 0.75f}, {2.50, "release", 1.0f},
    };
    REQUIRE(r.gestures.size() == 5);
    const double block = 48.0 / 48000.0;
    for (size_t i = 0; i < 5; ++i) {
        CAPTURE(i);
        CHECK(r.gestures[i].stomp == 1);
        CHECK(r.gestures[i].type == expected[i].type);
        // Applied at the first block boundary at or after the scripted time.
        CHECK(r.gestures[i].t >= expected[i].t - 1e-9);
        CHECK(r.gestures[i].t < expected[i].t + block);

        // LED 2 lights at the gesture's level right after it.
        bool lit = false;
        for (const auto& l : r.leds)
            if (l.t > r.gestures[i].t && l.t <= r.gestures[i].t + 2 * block && l.led2 == expected[i].led)
                lit = true;
        CHECK(lit);
    }
    // ... and goes dark again after the flash.
    CHECK(r.leds.back().led2 == 0.0f);
}

TEST_CASE("param events reach the effect by key") {
    const auto in = makeSignal("sine:440:-6", 0.5, 48000.0f).samples;
    const auto r  = runPassthrough(in, R"(
initial: { knobs: [1.0, 0.5, 0.5, 0.5, 0.5, 0.5] }
events: [ { t: 0.1, param: volume, to: 0.0 } ]
)");
    CHECK(std::fabs(r.output.back()) < 1e-6f);
    bool logged = false;
    for (const auto& p : r.params)
        if (p.key == "volume" && p.normalized == 0.0f && p.t >= 0.1) logged = true;
    CHECK(logged);
}
