#pragma once
// The interface every effect implements.
//
// An effect is plain C++17 plus DaisySP. It must not include libDaisy or any
// host header: the same source builds for the pedal, the desktop and tests.
// The real-time rules process() follows, so that code that passes on the
// desktop also works on the pedal:
//   - no heap allocation, locks, I/O, exceptions or system calls;
//   - no assumed sample rate or block size: rate-dependent values come from
//     prepare(), and every call may bring a different number of frames;
//   - float, not double;
//   - large buffers only from PrepareInfo::allocator, inside prepare();
//   - randomness only from setSeed(), so a run is reproducible;
//   - no NaN, Inf or denormals in the output;
//   - parameters snap before the first process() call and ramp after.

#include <cstddef>
#include <cstdint>

#include "tfx/gesture.h"
#include "tfx/param.h"

namespace tfx {

// Large buffers (delay lines, loop memory) come from the host, so the Daisy
// host can place them in SDRAM while desktop hosts use the normal heap.
// Only valid to call from prepare(); never from process().
class Allocator {
public:
    virtual ~Allocator() = default;
    // Returns zeroed, 16-byte aligned memory, or nullptr when out of space.
    virtual void* allocate(size_t bytes) = 0;
};

struct PrepareInfo {
    float      sampleRate; // any rate the host chooses; never assume 48 kHz
    size_t     maxBlock;   // upper bound for ProcessContext::frames
    Allocator* allocator;
};

struct ProcessContext {
    const float* in;     // mono input
    float*       out;    // mono output; may alias `in`
    size_t       frames; // anywhere from 1 to PrepareInfo::maxBlock, varies per call
};

struct LedState {
    float led[2] = {0.0f, 0.0f}; // brightness 0..1
};

class Effect {
public:
    virtual ~Effect() = default;

    virtual const char*       name() const   = 0;
    virtual const ParamTable& params() const = 0;

    // Allocate and compute everything that depends on the sample rate.
    // Called before any processing and again whenever the rate changes.
    virtual void prepare(const PrepareInfo& info) = 0;

    // Clear all audio state (delay lines, filters) without reallocating.
    virtual void reset() = 0;

    // Seed every random source, so offline runs are reproducible.
    virtual void setSeed(uint32_t seed) = 0;

    // Set a parameter from a normalized 0..1 position.
    // Hosts call this only between process() calls.
    virtual void setParam(ParamId id, float normalized) = 0;

    // A footswitch gesture, delivered between process() calls.
    virtual void onGesture(const Gesture& gesture) = 0;

    virtual void process(const ProcessContext& ctx) = 0;

    // Current LED brightness, decided by the effect. Read after process().
    virtual LedState leds() const = 0;

    // Delay the effect adds to the signal, reported to plugin hosts.
    virtual int latencySamples() const { return 0; }

    // What the footswitches and LEDs do, for host UIs (wrap in TFX_HELP()).
    virtual const char* help() const { return nullptr; }
};

} // namespace tfx
