#include "doctest.h"

#include "tfx/param.h"

using namespace tfx;

namespace {
ParamSpec spec(float min, float max, Curve curve) {
    return {0, "x", "X", "", min, max, curve, 0.5f, 0.0f, Control::None};
}
} // namespace

TEST_CASE("linear curve maps ends and middle") {
    const auto s = spec(-10.0f, 10.0f, Curve::Linear);
    CHECK(toReal(s, 0.0f) == -10.0f);
    CHECK(toReal(s, 1.0f) == 10.0f);
    CHECK(toReal(s, 0.5f) == doctest::Approx(0.0f));
    CHECK(toNormalized(s, 5.0f) == doctest::Approx(0.75f));
}

TEST_CASE("exp curve is geometric") {
    const auto s = spec(20.0f, 20000.0f, Curve::Exp);
    CHECK(toReal(s, 0.0f) == doctest::Approx(20.0f));
    CHECK(toReal(s, 1.0f) == doctest::Approx(20000.0f));
    CHECK(toReal(s, 0.5f) == doctest::Approx(632.456f).epsilon(1e-4));
    CHECK(toNormalized(s, toReal(s, 0.3f)) == doctest::Approx(0.3f));
}

TEST_CASE("stepped curve rounds to whole values") {
    const auto s = spec(0.0f, 3.0f, Curve::Stepped);
    CHECK(toReal(s, 0.0f) == 0.0f);
    CHECK(toReal(s, 0.49f) == 1.0f);
    CHECK(toReal(s, 0.51f) == 2.0f);
    CHECK(toReal(s, 1.0f) == 3.0f);
}

TEST_CASE("out-of-range input is clamped") {
    const auto s = spec(0.0f, 1.0f, Curve::Linear);
    CHECK(toReal(s, -1.0f) == 0.0f);
    CHECK(toReal(s, 2.0f) == 1.0f);
    CHECK(toNormalized(s, 5.0f) == 1.0f);
}

TEST_CASE("smoother starts on its initial value and settles exactly") {
    Smoother sm;
    sm.prepare(48000.0f, 20.0f, 1.0f);
    CHECK(sm.next() == 1.0f); // no ramp from the initial value
    sm.setTarget(0.5f);
    float v = 1.0f;
    for (int i = 0; i < 960; ++i) v = sm.next(); // one time constant (20 ms)
    CHECK(v == doctest::Approx(0.5f + 0.5f * 0.3679f).epsilon(0.01));
    for (int i = 0; i < 48000; ++i) v = sm.next();
    CHECK(v == 0.5f);
    CHECK(sm.settled());
}

TEST_CASE("smoother with zero time jumps immediately") {
    Smoother sm;
    sm.prepare(48000.0f, 0.0f, 0.0f);
    sm.setTarget(0.7f);
    CHECK(sm.next() == 0.7f);
}
