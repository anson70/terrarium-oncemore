#pragma once
// Small DSP building blocks shared by effects. Header-only, float-only and
// allocation-free, so they are safe inside process().

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tfx {

// A linear ramp that lands exactly on its target after a fixed time. Switches
// (bypass, freeze...) use it instead of a Smoother when the spec promises an
// exact result once a fade is over (bit-exact bypass), which a one-pole only
// reaches after ~16 time constants.
class Ramp {
public:
    // May be called again at any time; the new length applies to the next
    // setTarget().
    void prepare(float sampleRate, float ms) {
        length_ = static_cast<uint32_t>(std::max(1.0f, ms * 0.001f * sampleRate));
    }
    void snap(float v) { current_ = target_ = v, left_ = 0; }
    void setTarget(float v) {
        if (v == target_) return;
        target_ = v;
        left_   = length_;
        step_   = (target_ - current_) / static_cast<float>(length_);
    }
    float next() {
        if (left_ > 0 && --left_ == 0) current_ = target_;
        // Never past the target on the way: rounding in the running sum could
        // otherwise take a 1 -> 0 ramp to -1e-8 (and sqrt of it to NaN).
        else if (left_ > 0) current_ = step_ > 0.0f ? std::min(current_ + step_, target_) : std::max(current_ + step_, target_);
        return current_;
    }
    float current() const { return current_; }
    float target() const { return target_; }

private:
    float    current_ = 0.0f, target_ = 0.0f, step_ = 0.0f;
    uint32_t length_ = 1, left_ = 0;
};

// Marsaglia, "Xorshift RNGs", Journal of Statistical Software 8(14), 2003:
// the 32-bit generator with shifts (13, 17, 5).
class Xorshift32 {
public:
    void seed(uint32_t s) { state_ = s != 0 ? s : 0x9E3779B9u; } // 0 is a fixed point
    uint32_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    // Uniform in [0, 1): the top 24 bits, which a float holds exactly.
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }

private:
    uint32_t state_ = 0x9E3779B9u;
};

// Avoids denormals without relying on the FPU's flush-to-zero setting.
inline float flushDenormal(float x) { return std::fabs(x) < 1e-20f ? 0.0f : x; }

// Reads a circular delay line at a fractional delay with the 4-point,
// 3rd-order Hermite interpolator (x-form), O. Niemitalo, "Polynomial
// Interpolators for High-Quality Resampling of Oversampled Audio", 2001.
// In delay coordinates: y0 = x(n), y1 = x(n+1) (older). At t = 0 it returns
// x(n) exactly.
//
// `write` is the slot the next sample goes into, so delay 1 is the newest
// sample written. The caller keeps `delay` within [2, size - 3].
inline float readHermite(const float* buf, size_t size, size_t write, float delay) {
    const size_t n    = static_cast<size_t>(delay);
    const float  t    = delay - static_cast<float>(n);
    const size_t base = write >= n ? write - n : write + size - n;    // x(n)
    const size_t iM1  = base + 1 < size ? base + 1 : base + 1 - size; // x(n-1), newer
    const size_t i1   = base >= 1 ? base - 1 : base + size - 1;       // x(n+1), older
    const size_t i2   = base >= 2 ? base - 2 : base + size - 2;       // x(n+2)
    const float  ym1 = buf[iM1], y0 = buf[base], y1 = buf[i1], y2 = buf[i2];
    const float  c1 = 0.5f * (y1 - ym1);
    const float  c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float  c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

} // namespace tfx
