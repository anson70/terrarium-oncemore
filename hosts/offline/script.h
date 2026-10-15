#pragma once
// Control scripts: a timeline of panel moves for offline runs.
//
//   initial:
//     knobs: [1.0, 0.5, 0.5, 0.5, 0.5, 0.5]
//     toggles: [down, down, up, down]
//   events:
//     - { t: 0.10, stomp: 1, action: tap }          # press, release 50 ms later
//     - { t: 1.00, knob: 3, to: 0.9, over: 2.0 }    # linear turn over 2 s
//     - { t: 3.50, stomp: 2, action: hold, for: 1.2 }
//     - { t: 4.00, toggle: 2, set: up }
//     - { t: 5.00, param: volume, to: 0.25 }        # by key, bypassing the panel
//
// Timing rule (same as the pedal, where controls are read between blocks):
// an event takes effect at the first block boundary at or after its time.

#include <cstdint>
#include <string>
#include <vector>

#include "tfx/panel.h"

namespace tfx::offline {

struct ParamEvent {
    double      t;
    std::string key;
    float       normalized;
};

class ControlScript {
public:
    // Throws std::runtime_error with a message naming the offending event.
    static ControlScript fromYamlFile(const std::string& path);
    static ControlScript fromYamlString(const std::string& text);

    // A script with only the default panel and no events.
    static ControlScript empty();

    // The panel as it stands at time t (seconds).
    PanelState panelAt(double t) const;

    const std::vector<ParamEvent>& paramEvents() const { return params_; }

    // Times (seconds) at which some stomp changes state, sorted.
    std::vector<double> stompTransitionTimes(uint8_t stomp) const;

    // Latest time any event touches, so hosts can warn about a too-short input.
    double endTime() const { return endTime_; }

private:
    struct KnobSegment {
        double t0, t1;    // ramp from t0 to t1 (t1 == t0 for a jump)
        float  from, to;
    };
    struct Transition {
        double t;
        bool   on;
    };

    static float valueAt(const std::vector<KnobSegment>& segs, float initial, double t);
    static bool  stateAt(const std::vector<Transition>& trans, bool initial, double t);

    PanelState                initial_;
    std::vector<KnobSegment>  knobs_[PanelState::kKnobs];
    std::vector<Transition>   toggles_[PanelState::kToggles];
    std::vector<Transition>   stomps_[PanelState::kStomps];
    std::vector<ParamEvent>   params_;
    double                    endTime_ = 0.0;

    friend class ScriptBuilder;
};

} // namespace tfx::offline
