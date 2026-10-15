#pragma once
// Golden-file comparison and the JSON report written by fx-run.

#include <optional>
#include <string>
#include <vector>

#include "engine.h"

namespace tfx::offline {

struct Comparison {
    std::string golden;
    bool        lengthMatches = false;
    size_t      outputLength  = 0;
    size_t      goldenLength  = 0;
    double      maxAbsError   = 0.0;
    bool        exact         = false;     // every sample identical
    double      residualDbfs  = -1000.0;   // RMS of (output - golden); -1000 when exact
    double      thresholdDbfs = -100.0;
    bool        requireExact  = false;
    bool        pass          = false;
};

Comparison compare(const std::vector<float>& output, const std::vector<float>& golden,
                   double thresholdDbfs, bool requireExact);

struct ReportInfo {
    std::string effect;
    std::string input;      // file path or signal description
    std::string script;     // file path, or "" for none
    float       sampleRate = 48000.0f;
    BlockPlan   blocks;
    uint32_t    seed = 1;
    bool        includeTiming = true; // timing is never deterministic; omit for golden reports
};

std::string reportJson(const ReportInfo& info, const RunResult& result,
                       const std::optional<Comparison>& comparison);

} // namespace tfx::offline
