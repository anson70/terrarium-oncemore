#include "tape.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "tfx/dsp.h"

namespace tfx::fx::tape {

namespace {
constexpr float kPi = 3.14159265f;

// At the edge frequency: gap term sin(0.8)/0.8 (-0.947 dB), spacing
// exp(-kd) for the remaining 2.063 dB, so kd = 2.063 / 8.686.
constexpr float kEdgeGapX    = 0.8f;
constexpr float kEdgeSpacing = 0.237553f;
} // namespace

float tanhApprox(float x) {
    const float c  = std::min(std::max(x, -3.5f), 3.5f);
    const float c2 = c * c;
    return c / (1.0f + c2 / (3.0f + c2 / (5.0f + c2 / (7.0f + c2 / 9.0f))));
}

float headLoss(float hz, float speed, float spacing, float gap) {
    const float k = 2.0f * kPi * hz / speed;
    float       l = std::exp(-k * spacing);
    const float x = 0.5f * k * gap;
    if (x > 1e-6f) l *= std::sin(x) / x;
    return l;
}

float edgeGain(const Edge& e, float hz) {
    const float r = hz / e.hz;
    if (e.digital) {
        const float r2 = r * r, r4 = r2 * r2;
        return 1.0f / std::sqrt(1.0f + r4 * r4);
    }
    const float x = kEdgeGapX * r;
    float       l = std::exp(-kEdgeSpacing * r);
    if (x > 1e-6f) l *= std::sin(x) / x;
    return l;
}

int firTaps(float sampleRate) {
    return 2 * std::max(1, static_cast<int>(std::lround(32.0f * sampleRate / 48000.0f))) + 1;
}

// --- Oversampler2 ------------------------------------------------------------

Oversampler2::Oversampler2() {
    // Windowed sinc for a cut-off at a quarter of the high rate: h[c + d] =
    // sin(pi d / 2) / (pi d), zero for even d != 0 (the half-band property).
    constexpr int c = (kTaps - 1) / 2;
    float         sum = 0.0f;
    for (int i = 0; i < kHalf; ++i) {
        const int   n = 2 * i; // even n: odd distance from the odd centre
        const float d = static_cast<float>(n - c);
        const float w = 0.42f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(n) / (kTaps - 1)) +
                        0.08f * std::cos(4.0f * kPi * static_cast<float>(n) / (kTaps - 1));
        h_[i] = std::sin(0.5f * kPi * d) / (kPi * d) * w;
        sum += h_[i];
    }
    for (float& v : h_) v *= 0.5f / sum; // the odd taps carry half the DC gain
}

void Oversampler2::reset() {
    std::fill(std::begin(upHist_), std::end(upHist_), 0.0f);
    std::fill(std::begin(evenHist_), std::end(evenHist_), 0.0f);
    std::fill(std::begin(oddHist_), std::end(oddHist_), 0.0f);
    upPos_ = downPos_ = 0;
}

void Oversampler2::up(float x, float& a, float& b) {
    // Zero-stuffed input through 2h: the even outputs use the 24 non-zero
    // taps, the odd ones only the centre tap (2 x 0.5), i.e. x delayed by 11.
    upPos_ = upPos_ == 0 ? kHalf - 1 : upPos_ - 1;
    upHist_[upPos_] = upHist_[upPos_ + kHalf] = x;
    const float* hist = upHist_ + upPos_; // hist[i] = x[n - i]
    float        acc  = 0.0f;
    for (int i = 0; i < kHalf; ++i) acc += h_[i] * hist[i];
    a = 2.0f * acc;
    b = hist[(kTaps - 1) / 4]; // x[n - 11]
}

float Oversampler2::down(float a, float b) {
    // Output aligned with the even sample: sum h[2i] a[n - i] + 0.5 b[n - 12].
    downPos_ = downPos_ == 0 ? kHalf - 1 : downPos_ - 1;
    evenHist_[downPos_] = evenHist_[downPos_ + kHalf] = a;
    oddHist_[downPos_] = oddHist_[downPos_ + kHalf] = b;
    const float* ev  = evenHist_ + downPos_;
    float        acc = 0.0f;
    for (int i = 0; i < kHalf; ++i) acc += h_[i] * ev[i];
    return flushDenormal(acc + 0.5f * oddHist_[downPos_ + (kTaps + 1) / 4]); // b[n - 12]
}

// --- Saturator ----------------------------------------------------------------

Saturator::Saturator() {
    // Make-up that returns a -18 dBFS sine at its own RMS, at 33 drives.
    constexpr int   kPoints = 256;
    constexpr float kAmp    = 0.125892541f; // -18 dBFS
    for (int i = 0; i < kTable; ++i) {
        const float g   = std::exp(std::log(kMaxDrive) * static_cast<float>(i) / (kTable - 1));
        float       sum = 0.0f;
        for (int n = 0; n < kPoints; ++n) {
            const float y = tanhApprox(g * kAmp * std::sin(2.0f * kPi * static_cast<float>(n) / kPoints));
            sum += y * y;
        }
        table_[i] = kAmp * 0.707106781f / std::sqrt(sum / kPoints);
    }
    set(3.0f, 0.03f);
}

float Saturator::makeup(float drive) const {
    const float pos = std::log(std::min(std::max(drive, kMinDrive), kMaxDrive)) / std::log(kMaxDrive) *
                      static_cast<float>(kTable - 1);
    const int   i = std::min(static_cast<int>(pos), kTable - 2);
    const float t = pos - static_cast<float>(i);
    return table_[i] + t * (table_[i + 1] - table_[i]);
}

void Saturator::set(float drive, float bias) {
    if (drive == lastDrive_ && bias == lastBias_) return;
    lastDrive_ = drive, lastBias_ = bias;
    g_      = drive;
    b_      = bias;
    offset_ = tanhApprox(g_ * b_); // so silence stays silence (no DC step)
    gain_   = makeup(drive);
}

float Saturator::process(float x) {
    float a, b;
    os_.up(x, a, b);
    return os_.down(shape(a), shape(b));
}

// --- HighPass -----------------------------------------------------------------

void HighPass::setup(float hz, float sampleRate) {
    const float fc = std::min(hz, 0.45f * sampleRate);
    const float g  = std::tan(kPi * fc / sampleRate);
    a1_ = 1.0f / (1.0f + g * (g + k_));
    a2_ = g * a1_;
    a3_ = g * a2_;
}

float HighPass::process(float x) {
    const float v3 = x - ic2_;
    const float v1 = a1_ * ic1_ + a2_ * v3;
    const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
    ic1_ = flushDenormal(2.0f * v1 - ic1_);
    ic2_ = flushDenormal(2.0f * v2 - ic2_);
    return x - k_ * v1 - v2;
}

} // namespace tfx::fx::tape
