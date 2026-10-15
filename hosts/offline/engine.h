#pragma once
// The offline host: runs an effect over a buffer, driven by a control script,
// and records everything a test or a report needs. Time is counted in
// samples only, so the same inputs always give bit-identical output.

#include <cstdint>
#include <string>
#include <vector>

#include "script.h"
#include "tfx/effect.h"

namespace tfx::offline {

struct BlockPlan {
    size_t minBlock = 48;  // equal to maxBlock for a fixed size
    size_t maxBlock = 48;  // sizes are drawn uniformly from [min, max] with the run's seed
};

struct RunOptions {
    float     sampleRate = 48000.0f;
    BlockPlan blocks;
    uint32_t  seed = 1;
};

struct LedSample {
    double t;
    float  led1, led2;
};

struct GestureRecord {
    double      t;
    int         stomp; // 1-based, as printed on the pedal
    std::string type;
};

struct ParamRecord {
    double      t;
    std::string key;
    float       normalized;
    float       real;
};

struct RunResult {
    std::vector<float>         output;
    std::vector<LedSample>     leds;      // one entry per change (>= 1/255)
    std::vector<GestureRecord> gestures;
    std::vector<ParamRecord>   params;
    std::vector<std::string>   warnings;

    // Output statistics
    float  peak       = 0.0f;
    double rms        = 0.0;
    size_t clipped    = 0;  // |x| > 1
    size_t nans       = 0;
    size_t infs       = 0;

    // Block statistics
    size_t blocks       = 0;
    size_t minBlockUsed = 0;
    size_t maxBlockUsed = 0;

    // Real-time rule check: heap allocations made inside process()
    size_t allocationsInProcess = 0;

    // Desktop timing of process(); informational only, never deterministic
    double meanProcessUs = 0.0;
    double maxProcessUs  = 0.0;
};

// Throws std::runtime_error on unusable options or an unknown param key.
RunResult run(Effect& effect, const std::vector<float>& input, const ControlScript& script,
              const RunOptions& options);

} // namespace tfx::offline
