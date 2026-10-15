#include "tfx/gesture.h"

#include <cmath>

namespace tfx {

const char* gestureName(GestureType type) {
    switch (type) {
        case GestureType::Tap:       return "tap";
        case GestureType::DoubleTap: return "double_tap";
        case GestureType::Hold:      return "hold";
        case GestureType::Release:   return "release";
    }
    return "unknown";
}

void GestureService::prepare(float sampleRate, GestureTiming timing) {
    holdSamples_      = static_cast<uint64_t>(std::llround(timing.holdMs * 0.001f * sampleRate));
    doubleTapSamples_ = static_cast<uint64_t>(std::llround(timing.doubleTapMs * 0.001f * sampleRate));
    reset();
}

void GestureService::reset() {
    for (auto& s : states_) s = StompState{};
    count_ = 0;
}

void GestureService::push(uint8_t stomp, GestureType type, uint64_t sinceDown) {
    // Gestures are rare (human speed) and drained every block, so the queue
    // never fills in practice; if it ever does, drop rather than allocate.
    const uint32_t since = sinceDown > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(sinceDown);
    if (count_ < kMaxPending) pending_[count_++] = Gesture{stomp, type, since};
}

void GestureService::update(uint8_t stomp, bool pressed, uint64_t now) {
    if (stomp >= kStomps) return;
    StompState& s = states_[stomp];

    // An armed Tap that was not followed by a press in time can no longer
    // become a DoubleTap.
    if (s.tapArmed && !s.down && now - s.lastTapAt > doubleTapSamples_) s.tapArmed = false;

    if (pressed && !s.down) {
        s.down            = true;
        s.holdFired       = false;
        s.pressedAt       = now;
        s.doubleCandidate = s.tapArmed && (now - s.lastTapAt <= doubleTapSamples_);
        s.tapArmed        = false;
        return;
    }

    if (pressed && s.down) {
        if (!s.holdFired && now - s.pressedAt >= holdSamples_) {
            s.holdFired = true;
            push(stomp, GestureType::Hold, now - s.pressedAt);
        }
        return;
    }

    if (!pressed && s.down) {
        s.down = false;
        if (s.holdFired) {
            push(stomp, GestureType::Release, now - s.pressedAt);
        } else if (now - s.pressedAt >= holdSamples_) {
            // Released after the threshold but before any update saw it held
            // (only possible with very long blocks): still a hold.
            push(stomp, GestureType::Hold, now - s.pressedAt);
            push(stomp, GestureType::Release, now - s.pressedAt);
        } else if (s.doubleCandidate) {
            push(stomp, GestureType::DoubleTap, now - s.pressedAt);
        } else {
            push(stomp, GestureType::Tap, now - s.pressedAt);
            s.tapArmed  = true;
            s.lastTapAt = now;
        }
        s.doubleCandidate = false;
    }
}

} // namespace tfx
