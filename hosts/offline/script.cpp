#include "script.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace tfx::offline {

namespace {

[[noreturn]] void fail(size_t index, const std::string& what) {
    std::ostringstream msg;
    msg << "control script: event " << index << ": " << what;
    throw std::runtime_error(msg.str());
}

bool parseUpDown(const YAML::Node& node, const std::string& where) {
    const auto s = node.as<std::string>();
    if (s == "up" || s == "true" || s == "1") return true;
    if (s == "down" || s == "false" || s == "0") return false;
    throw std::runtime_error("control script: " + where + ": expected up or down, got '" + s + "'");
}

float parseUnit(const YAML::Node& node, size_t index, const char* field) {
    const float v = node.as<float>();
    if (!(v >= 0.0f && v <= 1.0f)) fail(index, std::string(field) + " must be within 0..1");
    return v;
}

int parseIndex(const YAML::Node& node, size_t index, const char* what, int max) {
    const int n = node.as<int>();
    if (n < 1 || n > max) fail(index, std::string(what) + " must be 1.." + std::to_string(max));
    return n - 1;
}

} // namespace

class ScriptBuilder {
public:
    static ControlScript build(const YAML::Node& root) {
        ControlScript s;
        if (!root || root.IsNull()) return s;
        if (!root.IsMap()) throw std::runtime_error("control script: top level must be a map");

        if (const auto init = root["initial"]) {
            if (const auto k = init["knobs"]) {
                if (!k.IsSequence() || k.size() != PanelState::kKnobs)
                    throw std::runtime_error("control script: initial.knobs needs 6 values");
                for (size_t i = 0; i < PanelState::kKnobs; ++i) {
                    const float v = k[i].as<float>();
                    if (!(v >= 0.0f && v <= 1.0f))
                        throw std::runtime_error("control script: initial.knobs values must be 0..1");
                    s.initial_.knobs[i] = v;
                }
            }
            if (const auto t = init["toggles"]) {
                if (!t.IsSequence() || t.size() != PanelState::kToggles)
                    throw std::runtime_error("control script: initial.toggles needs 4 values");
                for (size_t i = 0; i < PanelState::kToggles; ++i)
                    s.initial_.toggles[i] = parseUpDown(t[i], "initial.toggles");
            }
        }

        // Sort event *indices*, never YAML::Node objects: assigning one Node
        // to another overwrites the referenced node's content, so moving
        // Nodes around in std::sort silently scrambles the script.
        struct Raw { double t; size_t index; };
        std::vector<Raw> events;
        const YAML::Node evs = root["events"];
        if (evs) {
            if (!evs.IsSequence()) throw std::runtime_error("control script: events must be a list");
            for (size_t i = 0; i < evs.size(); ++i) {
                if (!evs[i].IsMap()) fail(i, "must be a map like { t: 1.0, knob: 1, to: 0.5 }");
                if (!evs[i]["t"]) fail(i, "missing t");
                const double t = evs[i]["t"].as<double>();
                if (!(t >= 0.0)) fail(i, "t must be >= 0");
                events.push_back({t, i});
            }
        }
        std::stable_sort(events.begin(), events.end(),
                         [](const Raw& a, const Raw& b) { return a.t < b.t; });

        for (const Raw& e : events) {
            const YAML::Node n = evs[e.index];
            if (n["knob"]) {
                const int k = parseIndex(n["knob"], e.index, "knob", PanelState::kKnobs);
                if (!n["to"]) fail(e.index, "knob event needs 'to'");
                const float  to   = parseUnit(n["to"], e.index, "to");
                const double over = n["over"] ? n["over"].as<double>() : 0.0;
                if (!(over >= 0.0)) fail(e.index, "over must be >= 0");
                auto& segs = s.knobs_[k];
                // A new move interrupts any ramp still in progress.
                const float from = ControlScript::valueAt(segs, s.initial_.knobs[k], e.t);
                if (!segs.empty() && segs.back().t1 > e.t) {
                    segs.back().to = from;
                    segs.back().t1 = e.t;
                }
                segs.push_back({e.t, e.t + over, from, to});
                s.endTime_ = std::max(s.endTime_, e.t + over);
            } else if (n["toggle"]) {
                const int k = parseIndex(n["toggle"], e.index, "toggle", PanelState::kToggles);
                if (!n["set"]) fail(e.index, "toggle event needs 'set: up|down'");
                s.toggles_[k].push_back({e.t, parseUpDown(n["set"], "event " + std::to_string(e.index))});
                s.endTime_ = std::max(s.endTime_, e.t);
            } else if (n["stomp"]) {
                const int k = parseIndex(n["stomp"], e.index, "stomp", PanelState::kStomps);
                const std::string action = n["action"] ? n["action"].as<std::string>() : "tap";
                auto& tr = s.stomps_[k];
                if (action == "tap" || action == "hold") {
                    const double dflt = action == "tap" ? 0.05 : 1.0;
                    const double dur  = n["for"] ? n["for"].as<double>() : dflt;
                    if (!(dur > 0.0)) fail(e.index, "for must be > 0");
                    tr.push_back({e.t, true});
                    tr.push_back({e.t + dur, false});
                    s.endTime_ = std::max(s.endTime_, e.t + dur);
                } else if (action == "press") {
                    tr.push_back({e.t, true});
                    s.endTime_ = std::max(s.endTime_, e.t);
                } else if (action == "release") {
                    tr.push_back({e.t, false});
                    s.endTime_ = std::max(s.endTime_, e.t);
                } else {
                    fail(e.index, "unknown stomp action '" + action + "' (tap, hold, press, release)");
                }
            } else if (n["param"]) {
                if (!n["to"]) fail(e.index, "param event needs 'to'");
                s.params_.push_back({e.t, n["param"].as<std::string>(), parseUnit(n["to"], e.index, "to")});
                s.endTime_ = std::max(s.endTime_, e.t);
            } else {
                fail(e.index, "needs one of knob, toggle, stomp, param");
            }
        }

        for (size_t k = 0; k < PanelState::kStomps; ++k) {
            auto& tr = s.stomps_[k];
            std::stable_sort(tr.begin(), tr.end(),
                             [](const auto& a, const auto& b) { return a.t < b.t; });
            bool down = false;
            for (size_t i = 0; i < tr.size(); ++i) {
                if (tr[i].on == down)
                    throw std::runtime_error("control script: stomp " + std::to_string(k + 1) +
                                             " is pressed or released twice in a row near t=" +
                                             std::to_string(tr[i].t) + " (overlapping events?)");
                if (i > 0 && tr[i].t == tr[i - 1].t)
                    throw std::runtime_error("control script: stomp " + std::to_string(k + 1) +
                                             " changes twice at the same time t=" +
                                             std::to_string(tr[i].t));
                down = tr[i].on;
            }
        }
        for (auto& tr : s.toggles_)
            std::stable_sort(tr.begin(), tr.end(),
                             [](const auto& a, const auto& b) { return a.t < b.t; });
        return s;
    }
};

ControlScript ControlScript::fromYamlString(const std::string& text) {
    try {
        return ScriptBuilder::build(YAML::Load(text));
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(std::string("control script: ") + e.what());
    }
}

ControlScript ControlScript::fromYamlFile(const std::string& path) {
    try {
        return ScriptBuilder::build(YAML::LoadFile(path));
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("control script " + path + ": " + e.what());
    }
}

ControlScript ControlScript::empty() { return ControlScript{}; }

float ControlScript::valueAt(const std::vector<KnobSegment>& segs, float initial, double t) {
    float v = initial;
    for (const auto& s : segs) {
        if (t < s.t0) break;
        if (t >= s.t1) { v = s.to; continue; }
        const double a = (t - s.t0) / (s.t1 - s.t0);
        return static_cast<float>(s.from + a * (s.to - s.from));
    }
    return v;
}

bool ControlScript::stateAt(const std::vector<Transition>& trans, bool initial, double t) {
    bool v = initial;
    for (const auto& tr : trans) {
        if (tr.t > t) break;
        v = tr.on;
    }
    return v;
}

PanelState ControlScript::panelAt(double t) const {
    PanelState p = initial_;
    for (size_t k = 0; k < PanelState::kKnobs; ++k) p.knobs[k] = valueAt(knobs_[k], initial_.knobs[k], t);
    for (size_t k = 0; k < PanelState::kToggles; ++k) p.toggles[k] = stateAt(toggles_[k], initial_.toggles[k], t);
    for (size_t k = 0; k < PanelState::kStomps; ++k) p.stomps[k] = stateAt(stomps_[k], false, t);
    return p;
}

std::vector<double> ControlScript::stompTransitionTimes(uint8_t stomp) const {
    std::vector<double> times;
    if (stomp < PanelState::kStomps)
        for (const auto& tr : stomps_[stomp]) times.push_back(tr.t);
    return times;
}

} // namespace tfx::offline
