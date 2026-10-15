#pragma once
// Footswitch gesture recognition, shared by every host.
//
// Hosts only report the raw (already debounced) pressed state of each stomp
// switch together with the current time in samples. Everything that decides
// what a press *means* lives here, so a long press feels identical on the
// pedal, on the desktop and in an offline test -- and offline runs stay
// reproducible because time is counted in samples, never wall-clock.

#include <cstddef>
#include <cstdint>

namespace tfx {

enum class GestureType : uint8_t {
    Tap,       // short press, fired on release
    DoubleTap, // second short press within the double-tap window, fired on release
               // (replaces the Tap that release would otherwise produce)
    Hold,      // fired once when a press reaches the hold threshold, while still down
    Release,   // fired on release after a Hold
};

const char* gestureName(GestureType type);

struct Gesture {
    uint8_t     stomp; // 0 or 1
    GestureType type;
    // Samples from the moment the switch went down to the start of the block
    // the gesture is delivered before. A Tap is only known on release, so an
    // effect that needs the press itself (tap tempo) counts back by this much.
    uint32_t sinceDown = 0;
};

struct GestureTiming {
    float holdMs      = 500.0f; // press at least this long -> Hold
    float doubleTapMs = 300.0f; // max gap from one Tap release to the next press
};

class GestureService {
public:
    static constexpr size_t kStomps     = 2;
    static constexpr size_t kMaxPending = 8;

    void prepare(float sampleRate, GestureTiming timing = {});
    void reset();

    // Feed the current pressed state of one stomp at time `now` (in samples).
    // Call for every stomp at the start of every block.
    void update(uint8_t stomp, bool pressed, uint64_t now);

    // Gestures recognised by the update() calls since the last drain.
    size_t         pendingCount() const { return count_; }
    const Gesture& pending(size_t i) const { return pending_[i]; }
    void           clearPending() { count_ = 0; }

private:
    struct StompState {
        bool     down          = false;
        bool     holdFired     = false;
        uint64_t pressedAt     = 0;
        bool     tapArmed      = false; // a Tap happened and may become a DoubleTap
        uint64_t lastTapAt     = 0;     // release time of that Tap
        bool     doubleCandidate = false; // current press started inside the window
    };

    void push(uint8_t stomp, GestureType type, uint64_t sinceDown);

    StompState states_[kStomps];
    uint64_t   holdSamples_      = 0;
    uint64_t   doubleTapSamples_ = 0;
    Gesture    pending_[kMaxPending];
    size_t     count_ = 0;
};

} // namespace tfx
