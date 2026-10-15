// The offline host's own guarantees, independent of any real effect.

#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

#include "audio_io.h"
#include "engine.h"
#include "report.h"

using namespace tfx;
using namespace tfx::offline;
using namespace tfx::desktop;

namespace {

// Breaks the real-time rules on purpose, to prove the host notices.
class RuleBreaker final : public Effect {
public:
    bool allocate = false;
    bool emitNan  = false;

    const char*       name() const override { return "rule-breaker"; }
    const ParamTable& params() const override {
        static const ParamTable t{nullptr, 0};
        return t;
    }
    void prepare(const PrepareInfo& info) override { maxBlock = info.maxBlock; }
    void reset() override {}
    void setSeed(uint32_t) override {}
    void setParam(ParamId, float) override {}
    void onGesture(const Gesture&) override {}
    void process(const ProcessContext& ctx) override {
        if (allocate) scratch.assign(ctx.frames, 0.0f); // forbidden: may allocate
        for (size_t i = 0; i < ctx.frames; ++i)
            ctx.out[i] = emitNan && i == 0 ? std::numeric_limits<float>::quiet_NaN() : ctx.in[i];
        if (ctx.frames > maxBlock) oversized = true;
    }
    LedState leds() const override { return {}; }

    std::vector<float> scratch;
    size_t             maxBlock  = 0;
    bool               oversized = false;
};

std::vector<float> ramp(size_t n) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<float>(i % 100) / 100.0f;
    return v;
}

} // namespace

TEST_CASE("allocation inside process() is counted") {
    RuleBreaker fx;
    fx.allocate = true;
    const auto r = run(fx, ramp(4800), ControlScript::empty(), {});
    CHECK(r.allocationsInProcess > 0);

    RuleBreaker clean;
    const auto r2 = run(clean, ramp(4800), ControlScript::empty(), {});
    CHECK(r2.allocationsInProcess == 0);
}

TEST_CASE("NaN output is counted") {
    RuleBreaker fx;
    fx.emitNan = true;
    const auto r = run(fx, ramp(480), ControlScript::empty(), {});
    CHECK(r.nans == 10); // first sample of each 48-sample block
}

TEST_CASE("random block sizes stay within the plan and cover the input") {
    RuleBreaker fx;
    RunOptions  opt;
    opt.blocks = {1, 64};
    opt.seed   = 3;
    const auto in = ramp(10000);
    const auto r  = run(fx, in, ControlScript::empty(), opt);
    CHECK_FALSE(fx.oversized);
    CHECK(r.minBlockUsed >= 1);
    CHECK(r.maxBlockUsed <= 64);
    CHECK(r.minBlockUsed < r.maxBlockUsed);
    CHECK(r.output == in);
}

TEST_CASE("the block pattern depends on the seed only") {
    RuleBreaker fx;
    RunOptions  a;
    a.blocks = {1, 512};
    a.seed   = 11;
    RunOptions b = a;
    b.seed       = 12;
    const auto in = ramp(48000);
    const auto r1 = run(fx, in, ControlScript::empty(), a);
    const auto r2 = run(fx, in, ControlScript::empty(), a);
    const auto r3 = run(fx, in, ControlScript::empty(), b);
    CHECK(r1.blocks == r2.blocks);
    CHECK(r1.minBlockUsed == r2.minBlockUsed);
    CHECK(r1.blocks != r3.blocks);
}

TEST_CASE("unknown param keys in a script are rejected before running") {
    RuleBreaker fx;
    const auto  s = ControlScript::fromYamlString("events: [ { t: 0, param: nope, to: 0.5 } ]");
    CHECK_THROWS_AS(run(fx, ramp(480), s, {}), std::runtime_error);
}

TEST_CASE("a script longer than the input produces a warning") {
    RuleBreaker fx;
    const auto  s = ControlScript::fromYamlString("events: [ { t: 5, toggle: 1, set: up } ]");
    const auto  r = run(fx, ramp(4800), s, {});
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].find("only 0.1 s") != std::string::npos);
}

TEST_CASE("comparison: exact, near and different") {
    const std::vector<float> a = {0.1f, 0.2f, 0.3f};
    auto                     b = a;
    CHECK(compare(a, b, -100.0, true).pass);
    CHECK(compare(a, b, -100.0, true).exact);

    b[1] += 1e-7f;
    CHECK_FALSE(compare(a, b, -100.0, true).pass);
    CHECK(compare(a, b, -100.0, false).pass); // residual ~ -144 dBFS

    b[1] = 0.9f;
    CHECK_FALSE(compare(a, b, -100.0, false).pass);
    CHECK_FALSE(compare(a, {0.1f, 0.2f}, -100.0, false).pass); // length differs
}

TEST_CASE("wav round trip is lossless for float") {
    const std::string path = "tfx_roundtrip_test.wav";
    const auto        in   = makeSignal("noise:-3", 0.1, 44100.0f);
    writeWav(path, in.samples, in.sampleRate);
    const auto back = readWav(path);
    CHECK(back.sampleRate == 44100.0f);
    CHECK(back.samples == in.samples);
    std::remove(path.c_str());
}

TEST_CASE("built-in signals") {
    const auto imp = makeSignal("impulse", 0.01, 48000.0f);
    CHECK(imp.samples.size() == 480);
    CHECK(imp.samples[0] == 1.0f);
    CHECK(imp.samples[1] == 0.0f);

    const auto sine = makeSignal("sine:1000:-6", 1.0, 48000.0f);
    float      peak = 0.0f;
    for (float x : sine.samples) peak = std::max(peak, std::fabs(x));
    CHECK(peak == doctest::Approx(0.501187f).epsilon(1e-3));

    CHECK_THROWS(makeSignal("square:100", 1.0, 48000.0f));
    CHECK_THROWS(makeSignal("sine:abc", 1.0, 48000.0f));
}
