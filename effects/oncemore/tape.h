#pragma once
// OnceMore's tape stages: saturation (2x oversampled), the playback head's
// loss as a linear-phase FIR, and a high-pass for the low edge. Each formula
// cites its source; effects/oncemore/spec.md says how they are used.
//
// Coefficients are designed outside process(): in prepare() or, for the
// saturator's drive, from tables built in the constructor.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tfx::fx::tape {

// tanh by Lambert's continued fraction (as used in Chowdhury, "Real-time
// Physical Modelling for Analog Tape Machines", DAFx 2019, eq. 25), taken one
// term further than the paper (x^2/9): truncated after x^2/7 the error reaches
// 0.52% at x = 3. It turns back towards 0 for large |x|, so the argument is
// held at |x| <= 3.5, where the error is about 0.1% and the curve still rises:
// the result is monotonic everywhere.
float tanhApprox(float x);

// Spacing and gap loss of a playback head (Chowdhury 2019, eqs. 13-14), as a
// linear gain: k = 2 pi f / v, L = exp(-k d) * sin(k g / 2) / (k g / 2).
// v in m/s, d and g in metres; g = 0 leaves the gap term out.
float headLoss(float hz, float speed, float spacing, float gap);

// Half-band low-pass for 2x up- and down-sampling: a windowed sinc (Blackman
// window), 47 taps, centre tap 0.5 and every other tap zero. Passband to
// ~0.38 fs, stopband from ~0.62 fs at about -74 dB (fs = the low rate).
// Each direction delays by 11.5 low-rate samples, the pair by exactly 23.
class Oversampler2 {
public:
    static constexpr int kTaps    = 47;
    static constexpr int kHalf    = 24; // non-zero taps besides the centre
    static constexpr int kLatency = 23; // up + down, in low-rate samples

    Oversampler2();
    void reset();
    // One low-rate sample in, two high-rate samples out.
    void up(float x, float& a, float& b);
    // Two high-rate samples in (a first), one low-rate sample out.
    float down(float a, float b);

private:
    float h_[kHalf] = {}; // the taps at odd distance from the centre, x2 for up()
    // Histories, doubled so a window never wraps.
    float upHist_[2 * kHalf]   = {};
    int   upPos_               = 0;
    float evenHist_[2 * kHalf] = {}; // high-rate samples at even index
    float oddHist_[2 * kHalf]  = {}; // at odd index
    int   downPos_             = 0;
};

// Tape saturation: y = (tanh(g(x + b)) - tanh(g b)) * c(g), run at twice the
// sample rate. c(g) keeps a -18 dBFS sine at the level it came in, from a
// table over the drive range built in the constructor (spec: saturation).
class Saturator {
public:
    static constexpr float kMinDrive = 1.0f, kMaxDrive = 12.0f;

    Saturator();
    void  reset() { os_.reset(); }
    // drive 1..12, bias 0..0.1; cheap when unchanged.
    void  set(float drive, float bias);
    float process(float x);
    // The level make-up for a drive, for tests.
    float makeup(float drive) const;

private:
    float shape(float x) const { return (tanhApprox(g_ * (x + b_)) - offset_) * gain_; }

    static constexpr int kTable = 33;
    float table_[kTable] = {}; // make-up at drive 12^(i / 32)
    float g_ = 1.0f, b_ = 0.0f, offset_ = 0.0f, gain_ = 1.0f;
    float lastDrive_ = -1.0f, lastBias_ = -1.0f;
    Oversampler2 os_;
};

// A machine's high-frequency edge, as the target magnitude of the head FIR.
struct Edge {
    float hz      = 12000.0f; // where the response is down 3 dB
    bool  digital = false;    // brick-ish 4th-order Butterworth shape instead of a head
};

// Magnitude at `hz` of the edge. Analog machines use the head loss, with the
// gap and spacing worked back from the edge: the gap costs ~0.95 dB and the
// spacing the rest of the 3 dB at the edge (spec v0.6, implementation notes).
float edgeGain(const Edge& e, float hz);

// Linear-phase FIR by frequency sampling (Hann window).
// `taps` must be odd; writes taps coefficients. `magnitude(hz)` is any
// callable giving the target gain. Not renormalised after windowing: the
// head-loss targets have a cusp at DC, and scaling the whole response to fit
// it there would lift everything else by up to 1 dB.
template <class F>
void designFir(float* h, int taps, float sampleRate, F magnitude);

// Number of head-FIR taps at a rate: 65 at 48 kHz, scaled, always odd.
int firTaps(float sampleRate);

// State-variable high-pass, trapezoidal (A. Simper, "Solving the continuous
// SVF equations using trapezoidal integration and equivalent currents",
// Cytomic, 2013). Q = 1/sqrt(2): -3 dB exactly at the cutoff.
class HighPass {
public:
    void  setup(float hz, float sampleRate);
    void  reset() { ic1_ = ic2_ = 0.0f; }
    void  copyState(const HighPass& o) { ic1_ = o.ic1_, ic2_ = o.ic2_; }
    float process(float x);

private:
    float a1_ = 1.0f, a2_ = 0.0f, a3_ = 0.0f, k_ = 1.41421356f;
    float ic1_ = 0.0f, ic2_ = 0.0f;
};

// --- template implementation ------------------------------------------------

template <class F>
void designFir(float* h, int taps, float sampleRate, F magnitude) {
    // h[n] = 1/N (H0 + 2 sum_k Hk cos(2 pi k (n - M) / N)), Hk = |H(k fs / N)|
    // (frequency sampling; e.g. Proakis & Manolakis, Digital Signal
    // Processing, 4th ed., section 10.2.3), then a Hann window.
    const int   m      = (taps - 1) / 2;
    const float twoPiN = 6.28318531f / static_cast<float>(taps);
    for (int n = 0; n < taps; ++n) {
        float acc = magnitude(0.0f);
        for (int k = 1; k <= m; ++k)
            acc += 2.0f * magnitude(static_cast<float>(k) * sampleRate / static_cast<float>(taps)) *
                   std::cos(twoPiN * static_cast<float>(k * (n - m)));
        const float w = 0.5f - 0.5f * std::cos(6.28318531f * static_cast<float>(n + 1) / static_cast<float>(taps + 1));
        h[n] = acc / static_cast<float>(taps) * w;
    }
}

} // namespace tfx::fx::tape
