#include "doctest.h"

#include <vector>

#include "tfx/panel.h"

using namespace tfx;

namespace {

// Records every call the driver makes into it.
class SpyEffect final : public Effect {
public:
    enum : ParamId { kA = 0, kB = 1, kT = 2, kFree = 3 };
    struct Call { ParamId id; float value; };

    const char*       name() const override { return "spy"; }
    const ParamTable& params() const override {
        static const ParamSpec specs[] = {
            {kA, "a", "A", "", 0, 1, Curve::Linear, 0.5f, 0, Control::Knob1},
            {kB, "b", "B", "", 0, 1, Curve::Linear, 0.5f, 0, Control::Knob4},
            {kT, "t", "T", "", 0, 1, Curve::Stepped, 0.0f, 0, Control::Toggle2},
            {kFree, "free", "Free", "", 0, 1, Curve::Linear, 0.5f, 0, Control::None},
        };
        static const ParamTable table{specs, 4};
        return table;
    }
    void prepare(const PrepareInfo&) override {}
    void reset() override {}
    void setSeed(uint32_t) override {}
    void setParam(ParamId id, float v) override { calls.push_back({id, v}); }
    void onGesture(const Gesture& g) override { gestures.push_back(g); }
    void process(const ProcessContext&) override {}
    LedState leds() const override { return {}; }

    std::vector<Call>    calls;
    std::vector<Gesture> gestures;
};

} // namespace

TEST_CASE("sync pushes every bound control, and only bound ones") {
    SpyEffect fx;
    PanelDriver d;
    d.bind(&fx);
    d.prepare(48000.0f);
    PanelState p;
    p.knobs[0]   = 0.1f;
    p.knobs[3]   = 0.9f;
    p.toggles[1] = true;
    d.sync(p, 0);
    REQUIRE(fx.calls.size() == 3);
    CHECK(fx.calls[0].id == SpyEffect::kA);
    CHECK(fx.calls[0].value == 0.1f);
    CHECK(fx.calls[1].id == SpyEffect::kB);
    CHECK(fx.calls[1].value == 0.9f);
    CHECK(fx.calls[2].id == SpyEffect::kT);
    CHECK(fx.calls[2].value == 1.0f);
}

TEST_CASE("knob physical order follows the panel, not the ADC") {
    // Knob4 is bound to param b: moving knob 4 must reach b and nothing else.
    SpyEffect fx;
    PanelDriver d;
    d.bind(&fx);
    d.prepare(48000.0f);
    PanelState p;
    d.sync(p, 0);
    fx.calls.clear();
    p.knobs[3] = 0.2f;
    d.apply(p, 48);
    REQUIRE(fx.calls.size() == 1);
    CHECK(fx.calls[0].id == SpyEffect::kB);
}

TEST_CASE("knob noise below the deadband is ignored but slow turns accumulate") {
    SpyEffect fx;
    PanelDriver d;
    d.bind(&fx);
    d.prepare(48000.0f);
    PanelState p;
    d.sync(p, 0);
    fx.calls.clear();

    const float step = PanelDriver::kKnobDeadband * 0.4f;
    p.knobs[0] += step;
    d.apply(p, 1);
    CHECK(fx.calls.empty());
    p.knobs[0] += step;
    d.apply(p, 2);
    CHECK(fx.calls.empty());
    p.knobs[0] += step; // 1.2 deadbands from the last sent value
    d.apply(p, 3);
    REQUIRE(fx.calls.size() == 1);
    CHECK(fx.calls[0].value == doctest::Approx(0.5f + 3 * step));
}

TEST_CASE("a knob reaching either end is always sent") {
    SpyEffect fx;
    PanelDriver d;
    d.bind(&fx);
    d.prepare(48000.0f);
    PanelState p;
    p.knobs[0] = 1.0f - PanelDriver::kKnobDeadband * 0.5f;
    d.sync(p, 0);
    fx.calls.clear();
    p.knobs[0] = 1.0f;
    d.apply(p, 1);
    REQUIRE(fx.calls.size() == 1);
    CHECK(fx.calls[0].value == 1.0f);
}

TEST_CASE("toggles are sent on every flip") {
    SpyEffect fx;
    PanelDriver d;
    d.bind(&fx);
    d.prepare(48000.0f);
    PanelState p;
    d.sync(p, 0);
    fx.calls.clear();
    p.toggles[1] = true;
    d.apply(p, 1);
    p.toggles[1] = false;
    d.apply(p, 2);
    p.toggles[0] = true; // toggle 1 is unbound
    d.apply(p, 3);
    REQUIRE(fx.calls.size() == 2);
    CHECK(fx.calls[0].value == 1.0f);
    CHECK(fx.calls[1].value == 0.0f);
}

TEST_CASE("stomps become gestures delivered to the effect") {
    SpyEffect fx;
    PanelDriver d;
    d.bind(&fx);
    d.prepare(1000.0f);
    PanelState p;
    d.sync(p, 0);
    p.stomps[1] = true;
    d.apply(p, 10);
    p.stomps[1] = false;
    d.apply(p, 60);
    REQUIRE(fx.gestures.size() == 1);
    CHECK(fx.gestures[0].stomp == 1);
    CHECK(fx.gestures[0].type == GestureType::Tap);
}

TEST_CASE("params can be set by key, including unbound ones") {
    SpyEffect fx;
    PanelDriver d;
    d.bind(&fx);
    d.prepare(48000.0f);
    CHECK(d.setParamByKey("free", 0.25f, 0));
    CHECK_FALSE(d.setParamByKey("missing", 0.25f, 0));
    REQUIRE(fx.calls.size() == 1);
    CHECK(fx.calls[0].id == SpyEffect::kFree);
}
