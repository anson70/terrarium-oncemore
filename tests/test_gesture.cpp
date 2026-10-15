#include "doctest.h"

#include <string>
#include <vector>

#include "tfx/gesture.h"

using namespace tfx;

namespace {

constexpr float kSr = 1000.0f; // 1 sample = 1 ms keeps the arithmetic readable

// Drives one stomp from a list of (time_ms, pressed) edges, stepping 1 ms at a
// time, and returns the gestures as "time:type".
std::vector<std::string> drive(GestureService& g, uint8_t stomp,
                               std::vector<std::pair<uint64_t, bool>> edges, uint64_t until) {
    std::vector<std::string> out;
    bool   pressed = false;
    size_t e       = 0;
    for (uint64_t t = 0; t <= until; ++t) {
        while (e < edges.size() && edges[e].first == t) pressed = edges[e++].second;
        g.update(stomp, pressed, t);
        for (size_t i = 0; i < g.pendingCount(); ++i)
            out.push_back(std::to_string(t) + ":" + gestureName(g.pending(i).type));
        g.clearPending();
    }
    return out;
}

} // namespace

TEST_CASE("short press is a tap, fired on release") {
    GestureService g;
    g.prepare(kSr);
    CHECK(drive(g, 0, {{100, true}, {150, false}}, 1000) == std::vector<std::string>{"150:tap"});
}

TEST_CASE("long press fires hold at the threshold and release on let go") {
    GestureService g;
    g.prepare(kSr);
    CHECK(drive(g, 0, {{100, true}, {900, false}}, 1000) ==
          std::vector<std::string>{"600:hold", "900:release"});
}

TEST_CASE("hold threshold boundary") {
    GestureService g;
    g.prepare(kSr);
    // 499 ms is still a tap; 500 ms is a hold.
    CHECK(drive(g, 0, {{0, true}, {499, false}}, 600) == std::vector<std::string>{"499:tap"});
    g.reset();
    CHECK(drive(g, 0, {{0, true}, {500, false}}, 600) ==
          std::vector<std::string>{"500:hold", "500:release"});
}

TEST_CASE("second tap inside the window is a double tap") {
    GestureService g;
    g.prepare(kSr);
    CHECK(drive(g, 0, {{100, true}, {150, false}, {300, true}, {350, false}}, 1000) ==
          std::vector<std::string>{"150:tap", "350:double_tap"});
}

TEST_CASE("double tap window boundary") {
    GestureService g;
    g.prepare(kSr);
    // Next press exactly 300 ms after the release: still a double tap.
    CHECK(drive(g, 0, {{0, true}, {50, false}, {350, true}, {400, false}}, 1000) ==
          std::vector<std::string>{"50:tap", "400:double_tap"});
    g.reset();
    // 301 ms: two separate taps.
    CHECK(drive(g, 0, {{0, true}, {50, false}, {351, true}, {400, false}}, 1000) ==
          std::vector<std::string>{"50:tap", "400:tap"});
}

TEST_CASE("a third quick tap starts over instead of chaining") {
    GestureService g;
    g.prepare(kSr);
    CHECK(drive(g, 0, {{0, true}, {50, false}, {100, true}, {150, false}, {200, true}, {250, false}}, 600) ==
          std::vector<std::string>{"50:tap", "150:double_tap", "250:tap"});
}

TEST_CASE("a long second press is a hold, not a double tap") {
    GestureService g;
    g.prepare(kSr);
    CHECK(drive(g, 0, {{0, true}, {50, false}, {100, true}, {800, false}}, 1000) ==
          std::vector<std::string>{"50:tap", "600:hold", "800:release"});
}

TEST_CASE("stomps are independent") {
    GestureService g;
    g.prepare(kSr);
    g.update(0, true, 0);
    g.update(1, true, 0);
    g.update(0, false, 50);
    g.update(1, false, 60);
    REQUIRE(g.pendingCount() == 2);
    CHECK(g.pending(0).stomp == 0);
    CHECK(g.pending(1).stomp == 1);
    CHECK(g.pending(0).type == GestureType::Tap);
    CHECK(g.pending(1).type == GestureType::Tap);
}

TEST_CASE("timing scales with the sample rate") {
    GestureService g;
    g.prepare(48000.0f);
    g.update(0, true, 0);
    g.update(0, true, 23999); // 499.98 ms
    CHECK(g.pendingCount() == 0);
    g.update(0, true, 24000); // 500 ms
    REQUIRE(g.pendingCount() == 1);
    CHECK(g.pending(0).type == GestureType::Hold);
}

TEST_CASE("every gesture says how long ago its press began") {
    GestureService g;
    g.prepare(kSr);
    std::vector<std::pair<std::string, uint32_t>> seen;
    const std::vector<std::pair<uint64_t, bool>> edges = {
        {100, true}, {180, false},  // tap: pressed 80 ms before it fires
        {300, true}, {340, false},  // double tap (gap 120 ms): 40 ms
        {1000, true}, {1700, false} // hold at 1500 (500 ms), release at 1700 (700 ms)
    };
    bool   pressed = false;
    size_t e       = 0;
    for (uint64_t t = 0; t <= 2000; ++t) {
        while (e < edges.size() && edges[e].first == t) pressed = edges[e++].second;
        g.update(0, pressed, t);
        for (size_t i = 0; i < g.pendingCount(); ++i)
            seen.emplace_back(gestureName(g.pending(i).type), g.pending(i).sinceDown);
        g.clearPending();
    }
    const std::vector<std::pair<std::string, uint32_t>> expected = {
        {"tap", 80}, {"double_tap", 40}, {"hold", 500}, {"release", 700}};
    CHECK(seen == expected);
}
