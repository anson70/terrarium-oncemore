#include "doctest.h"

#include <stdexcept>

#include "script.h"

using namespace tfx;
using namespace tfx::offline;

TEST_CASE("empty script gives the default panel") {
    const auto s = ControlScript::fromYamlString("");
    const auto p = s.panelAt(10.0);
    CHECK(p.knobs[0] == 0.5f);
    CHECK_FALSE(p.toggles[0]);
    CHECK_FALSE(p.stomps[0]);
}

TEST_CASE("initial values") {
    const auto s = ControlScript::fromYamlString(
        "initial: { knobs: [0, 0.1, 0.2, 0.3, 0.4, 1], toggles: [up, down, up, down] }");
    const auto p = s.panelAt(0.0);
    CHECK(p.knobs[0] == 0.0f);
    CHECK(p.knobs[5] == 1.0f);
    CHECK(p.toggles[0]);
    CHECK_FALSE(p.toggles[1]);
    CHECK(p.toggles[2]);
}

TEST_CASE("knob jumps and ramps") {
    const auto s = ControlScript::fromYamlString(R"(
events:
  - { t: 1.0, knob: 2, to: 1.0 }
  - { t: 2.0, knob: 2, to: 0.0, over: 1.0 }
)");
    CHECK(s.panelAt(0.99).knobs[1] == 0.5f);
    CHECK(s.panelAt(1.0).knobs[1] == 1.0f);
    CHECK(s.panelAt(2.5).knobs[1] == doctest::Approx(0.5f));
    CHECK(s.panelAt(3.0).knobs[1] == 0.0f);
    CHECK(s.panelAt(9.0).knobs[1] == 0.0f);
}

TEST_CASE("a new move interrupts a ramp in progress") {
    const auto s = ControlScript::fromYamlString(R"(
events:
  - { t: 0.0, knob: 1, to: 1.0, over: 2.0 }
  - { t: 1.0, knob: 1, to: 0.0, over: 1.0 }
)");
    CHECK(s.panelAt(1.0).knobs[0] == doctest::Approx(0.75f)); // 0.5 -> 1.0, halfway
    CHECK(s.panelAt(1.5).knobs[0] == doctest::Approx(0.375f));
    CHECK(s.panelAt(2.0).knobs[0] == 0.0f);
}

TEST_CASE("events may be listed out of order") {
    const auto s = ControlScript::fromYamlString(R"(
events:
  - { t: 2.0, toggle: 1, set: down }
  - { t: 1.0, toggle: 1, set: up }
)");
    CHECK_FALSE(s.panelAt(0.5).toggles[0]);
    CHECK(s.panelAt(1.5).toggles[0]);
    CHECK_FALSE(s.panelAt(2.5).toggles[0]);
}

TEST_CASE("stomp actions") {
    const auto s = ControlScript::fromYamlString(R"(
events:
  - { t: 1.0, stomp: 1, action: tap }
  - { t: 2.0, stomp: 2, action: hold, for: 0.7 }
  - { t: 4.0, stomp: 1, action: press }
  - { t: 5.0, stomp: 1, action: release }
)");
    CHECK(s.panelAt(1.0).stomps[0]);
    CHECK(s.panelAt(1.049).stomps[0]);
    CHECK_FALSE(s.panelAt(1.05).stomps[0]);
    CHECK(s.panelAt(2.69).stomps[1]);
    CHECK_FALSE(s.panelAt(2.7).stomps[1]);
    CHECK(s.panelAt(4.5).stomps[0]);
    CHECK_FALSE(s.panelAt(5.0).stomps[0]);
    CHECK(s.endTime() == doctest::Approx(5.0));
}

TEST_CASE("param events keep their key and value") {
    const auto s = ControlScript::fromYamlString("events: [ { t: 0.5, param: volume, to: 0.25 } ]");
    REQUIRE(s.paramEvents().size() == 1);
    CHECK(s.paramEvents()[0].key == "volume");
    CHECK(s.paramEvents()[0].normalized == 0.25f);
}

TEST_CASE("mistakes are reported, not guessed") {
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ { t: 1, knob: 7, to: 0.5 } ]"), std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ { t: 1, knob: 1, to: 1.5 } ]"), std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ { t: 1, knob: 1 } ]"), std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ { knob: 1, to: 0.5 } ]"), std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ { t: 1, stomp: 1, action: stomp } ]"),
                    std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ { t: 1, wiggle: 1 } ]"), std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("initial: { knobs: [0.5, 0.5] }"), std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ { t: 1, toggle: 1, set: sideways } ]"),
                    std::runtime_error);
    // A tap inside a hold on the same stomp cannot happen with one foot.
    CHECK_THROWS_AS(ControlScript::fromYamlString(R"(
events:
  - { t: 1.0, stomp: 1, action: hold, for: 1.0 }
  - { t: 1.5, stomp: 1, action: tap }
)"),
                    std::runtime_error);
    CHECK_THROWS_AS(ControlScript::fromYamlString("events: [ {t: 1, knob: 1, to: 0.5" /* broken YAML */),
                    std::runtime_error);
}
