// Acceptance tests for OnceMore: effects/oncemore/spec.md, 驗收測試.
// N1: items 1-5 for the transport (wow, flutter, mix, bypass, Warp, LED 1).
// N2: items 6-8 and the Model part of 13 (saturation, head loss, machines).

#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine.h"
#include "oncemore/oncemore.h"
#include "oncemore/tape.h"

using namespace tfx;
using namespace tfx::offline;
using fx::OnceMore;

namespace {

const float     kRates[]  = {44100.0f, 48000.0f, 96000.0f};
const BlockPlan kBlocks[] = {{1, 1}, {48, 48}, {512, 512}, {1, 512}};

float norm(ParamId id, float real) {
    OnceMore fx;
    return toNormalized(*fx.params().byId(id), real);
}

// The panel and hidden parameters, in real units. The dictaphone (6) is the
// default here: a 1 kHz test tone sits in its passband.
struct Panel {
    float wow = 0.0f, flutter = 0.0f, mixPct = 100.0f, flutterAm = 0.5f, drive = 1.0f, bias = 0.03f;
    // N3 is off unless a test asks: no hiss, hum or interference (all toggles
    // down), and the digital machines clean.
    float digital = 0.0f;
    bool  hiss = false, hum = false, interference = false, digitalFilter = true;
    float failure = 0.0f;
    bool  fs2Stop = true; // Toggle 4 up: FS2 hold = tape stop
    std::string extra; // more events, appended as-is
    int   model = 6, override = 0;
    bool  dryAlign = false;

    std::string yaml() const {
        char buf[1024];
        std::snprintf(buf, sizeof buf,
                      "initial:\n  knobs: [%.9g, %.9g, %.9g, %.9g, %.9g, %.9g]\n  toggles: [%s, %s, %s, %s]\n"
                      "events:\n  - { t: 0, param: flutter_am, to: %.9g }\n  - { t: 0, param: dry_align, to: %d }\n"
                      "  - { t: 0, param: bias, to: %.9g }\n  - { t: 0, param: model_override, to: %.9g }\n"
                      "  - { t: 0, param: digital_depth, to: %.9g }\n  - { t: 0, param: digital_filter, to: %d }\n",
                      static_cast<double>(wow), static_cast<double>(flutter),
                      static_cast<double>(norm(OnceMore::kSaturate, drive)),
                      static_cast<double>((static_cast<float>(std::min(model, 8)) - 1.0f) / 7.0f),
                      static_cast<double>(failure), static_cast<double>(mixPct / 100.0f), hiss ? "up" : "down",
                      hum ? "up" : "down", interference ? "up" : "down", fs2Stop ? "up" : "down", static_cast<double>(flutterAm), dryAlign ? 1 : 0,
                      static_cast<double>(bias / 0.1f), static_cast<double>(static_cast<float>(model > 8 ? model : override) / 10.0f),
                      static_cast<double>(digital), digitalFilter ? 1 : 0);
        return buf + extra;
    }
};

size_t argmaxAbs(const std::vector<float>& y) {
    size_t k = 0;
    for (size_t i = 1; i < y.size(); ++i)
        if (std::fabs(y[i]) > std::fabs(y[k])) k = i;
    return k;
}

// |Y(f)| / |X(f)| style measurements: the DTFT of a stretch of samples at one
// frequency (Goertzel would do; a direct sum is clearer).
double dtft(const std::vector<float>& y, double hz, float sr, size_t from = 0, size_t to = 0) {
    if (to == 0) to = y.size();
    // A rotating phasor instead of cos/sin per sample; renormalised now and
    // then so rounding cannot make it drift.
    const double w  = 6.283185307179586 * hz / sr;
    const double cr = std::cos(w), ci = -std::sin(w);
    double       pr = std::cos(w * static_cast<double>(from)), pi = -std::sin(w * static_cast<double>(from));
    double       re = 0.0, im = 0.0;
    for (size_t n = from; n < to; ++n) {
        re += y[n] * pr;
        im += y[n] * pi;
        const double t = pr * cr - pi * ci;
        pi             = pr * ci + pi * cr;
        pr             = t;
        if ((n & 1023) == 0) {
            const double g = 1.0 / std::sqrt(pr * pr + pi * pi);
            pr *= g, pi *= g;
        }
    }
    return std::sqrt(re * re + im * im);
}

double db(double x) { return 20.0 * std::log10(x); }

RunResult go(const std::vector<float>& in, const std::string& script, float sr = 48000.0f,
             BlockPlan blocks = {48, 48}, uint32_t seed = 3) {
    OnceMore   fx;
    RunOptions opt;
    opt.sampleRate = sr;
    opt.blocks     = blocks;
    opt.seed       = seed;
    RunResult r    = run(fx, in, ControlScript::fromYamlString(script), opt);
    // Item 1, checked on every run.
    REQUIRE(r.allocationsInProcess == 0);
    REQUIRE(r.nans == 0);
    REQUIRE(r.infs == 0);
    return r;
}

size_t at(double seconds, float sr) { return static_cast<size_t>(std::lround(seconds * sr)); }

std::vector<float> sine(float sr, double seconds, double hz, float amp) {
    std::vector<float> x(at(seconds, sr));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = amp * static_cast<float>(std::sin(6.283185307179586 * hz * static_cast<double>(i) / sr));
    return x;
}

std::vector<float> noise(float sr, double seconds, uint32_t seed) {
    Xorshift32 rng;
    rng.seed(seed);
    std::vector<float> x(at(seconds, sr));
    for (float& v : x) v = 2.0f * rng.uniform() - 1.0f;
    return x;
}

// Largest pitch deviation of a 1 kHz sine, in cents, from the time between
// rising zero crossings (linearly interpolated), after `skipS` seconds.
double peakCents(const std::vector<float>& y, float sr, double skipS) {
    double last = -1.0, worst = 0.0;
    for (size_t i = at(skipS, sr); i + 1 < y.size(); ++i) {
        if (y[i] < 0.0f && y[i + 1] >= 0.0f) {
            const double t = static_cast<double>(i) + y[i] / static_cast<double>(y[i] - y[i + 1]);
            if (last >= 0.0) {
                const double hz = sr / (t - last);
                worst = std::max(worst, std::fabs(1200.0 * std::log2(hz / 1000.0)));
            }
            last = t;
        }
    }
    return worst;
}

bool isDenormal(float x) { return x != 0.0f && std::fabs(x) < 1.17549435e-38f; }

} // namespace

TEST_CASE("1. no allocation, NaN or Inf anywhere; silence leaves no denormals") {
    const std::string events = "  - { t: 0.5, stomp: 1, action: hold, for: 1.0 }\n"
                               "  - { t: 2.0, stomp: 1, action: tap }\n"
                               "  - { t: 2.5, knob: 1, to: 1.0, over: 1.0 }\n"
                               "  - { t: 3.0, stomp: 1, action: tap }\n";
    for (float sr : kRates)
        for (BlockPlan blocks : kBlocks) {
            CAPTURE(sr);
            CAPTURE(blocks.minBlock);
            CAPTURE(blocks.maxBlock);
            Panel p;
            p.wow = 0.5f, p.flutter = 0.7f, p.mixPct = 60.0f;
            go(noise(sr, 4.0, 11), p.yaml() + events, sr, blocks); // checks inside go()

            // Ten seconds of silence after a burst: the tail must reach true zero.
            std::vector<float> in(at(10.0, sr), 0.0f);
            for (size_t i = 0; i < at(0.1, sr); ++i) in[i] = 0.5f * std::sin(0.1f * static_cast<float>(i));
            const auto r = go(in, p.yaml(), sr, blocks);
            CHECK(std::none_of(r.output.begin(), r.output.end(), isDenormal));
            CHECK(r.output.back() == 0.0f);
        }
}

TEST_CASE("2. same seed, same output; another seed differs; block size never matters") {
    for (float sr : kRates) {
        CAPTURE(sr);
        Panel p;
        p.wow = 0.8f, p.flutter = 0.6f, p.mixPct = 70.0f, p.flutterAm = 1.0f;
        const auto in  = sine(sr, 3.0, 440.0, 0.5f);
        const auto ref = go(in, p.yaml(), sr, {48, 48}, 7).output;
        for (BlockPlan blocks : kBlocks) {
            CAPTURE(blocks.minBlock);
            CAPTURE(blocks.maxBlock);
            CHECK(go(in, p.yaml(), sr, blocks, 7).output == ref);
        }
        CHECK(go(in, p.yaml(), sr, {48, 48}, 8).output != ref);
    }
}

TEST_CASE("3. every extreme, Warp held included: |out| <= 2 for full-scale white noise") {
    for (float sr : kRates)
        for (float wow : {0.0f, 1.0f})
            for (float flutter : {0.0f, 1.0f})
                for (float mix : {0.0f, 50.0f, 100.0f})
                    for (float am : {0.0f, 1.0f})
                        for (bool align : {false, true})
                            for (bool warp : {false, true}) {
                                CAPTURE(sr);
                                CAPTURE(wow);
                                CAPTURE(flutter);
                                CAPTURE(mix);
                                CAPTURE(am);
                                CAPTURE(align);
                                CAPTURE(warp);
                                Panel p;
                                p.wow = wow, p.flutter = flutter, p.mixPct = mix, p.flutterAm = am;
                                p.dryAlign = align;
                                std::string script = p.yaml();
                                if (warp) script += "  - { t: 0, stomp: 1, action: hold, for: 1.5 }\n";
                                const auto r = go(noise(sr, 1.5, 5), script, sr, {1, 512});
                                CHECK(r.peak <= 2.0f);
                            }
}

TEST_CASE("4. bypass is bit-exact after 10 ms; latency matches; dry_align lines the paths up") {
    for (float sr : kRates)
        for (BlockPlan blocks : kBlocks) {
            CAPTURE(sr);
            CAPTURE(blocks.minBlock);
            CAPTURE(blocks.maxBlock);
            Panel p;
            p.wow = 1.0f, p.flutter = 1.0f, p.mixPct = 70.0f;
            const auto in = noise(sr, 1.0, 9);
            const auto r  = go(in, p.yaml() + "  - { t: 0.5, stomp: 1, action: tap }\n", sr, blocks);
            // The tap fires on release; count 10 ms from there (plus a block).
            REQUIRE(r.gestures.size() == 1);
            const size_t first = at(r.gestures[0].t + 0.010, sr) + blocks.maxBlock;
            bool exact = true;
            for (size_t i = first; i < in.size(); ++i) exact = exact && r.output[i] == in[i];
            CHECK(exact);
            CHECK(r.leds.back().led1 == 0.0f);

            // Latency: D0 + the oversampler + half the head FIR, and the wet
            // impulse peaks there. Measured on the wide machines: a band as
            // narrow as the toy's rings, so its "peak" is not a delay.
            OnceMore probe;
            PrepareInfo info{sr, 512, nullptr};
            probe.prepare(info);
            const long latency = probe.latencySamples();
            CHECK(latency == std::lround(0.004 * sr) + 23 + fx::tape::firTaps(sr) / 2);
            std::vector<float> imp(at(0.1, sr), 0.0f);
            imp[100] = 0.1f;
            for (int m : {1, 2, 3, 4, 9}) {
                CAPTURE(m);
                Panel still;
                still.model = m;
                const auto w = go(imp, still.yaml(), sr, blocks);
                CHECK(std::labs(static_cast<long>(argmaxAbs(w.output)) - 100 - latency) <= 1);

                // dry_align: both paths peak in the same sample.
                still.mixPct   = 50.0f;
                still.dryAlign = true;
                const auto a   = go(imp, still.yaml(), sr, blocks);
                CHECK(static_cast<long>(argmaxAbs(a.output)) - 100 == latency);
                // ...and add up there: half the dry plus nearly half the wet's peak.
                const float wetPeak = std::fabs(w.output[argmaxAbs(w.output)]);
                CHECK(a.output[static_cast<size_t>(100 + latency)] > 0.05f + 0.45f * wetPeak);
            }
        }
}

TEST_CASE("5. wow and flutter: pitch deviation as specified, zero at zero") {
    // The swings are random, so the peak is checked for several seeds.
    for (float sr : kRates)
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            CAPTURE(sr);
            CAPTURE(seed);
            const auto in = sine(sr, 8.0, 1000.0, 0.5f);

            Panel wow;
            wow.wow        = 1.0f;
            const double w = peakCents(go(in, wow.yaml(), sr, {1, 512}, seed).output, sr, 0.3);
            CAPTURE(w);
            CHECK(w >= 40.0);
            CHECK(w <= 60.0);

            Panel fl;
            fl.flutter     = 1.0f;
            const double f = peakCents(go(in, fl.yaml(), sr, {1, 512}, seed).output, sr, 0.3);
            CAPTURE(f);
            CHECK(f >= 24.0);
            CHECK(f <= 36.0);
            MESSAGE("sr " << sr << " seed " << seed << ": wow " << w << " cents, flutter " << f << " cents");
        }
    for (float sr : kRates)
        for (BlockPlan blocks : kBlocks) {
            CAPTURE(sr);
            CAPTURE(blocks.minBlock);
            CAPTURE(blocks.maxBlock);
            const auto in = sine(sr, 2.0, 1000.0, 0.5f);

            // Both at zero: no pitch movement at all.
            Panel none;
            CHECK(peakCents(go(in, none.yaml(), sr, blocks).output, sr, 0.2) < 0.05);
        }
}

TEST_CASE("5. the wow + flutter offset never exceeds 2.65 ms, whatever the seed or setting") {
    struct Heap final : Allocator {
        std::vector<std::vector<float>> blocks;
        void* allocate(size_t bytes) override {
            blocks.emplace_back((bytes + 3) / 4, 0.0f);
            return blocks.back().data();
        }
    };
    for (float sr : {44100.0f, 96000.0f})
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            CAPTURE(sr);
            CAPTURE(seed);
            Heap     heap;
            OnceMore fx;
            fx.setSeed(seed);
            fx.prepare({sr, 1, &heap});
            fx.setParam(OnceMore::kWow, seed % 2 ? 1.0f : 0.6f);
            fx.setParam(OnceMore::kFlutter, 1.0f);
            fx.onGesture({0, GestureType::Hold, 0}); // Warp on top
            float worst = 0.0f, x = 0.0f, y = 0.0f;
            for (size_t i = 0; i < at(6.0, sr); ++i) {
                fx.process({&x, &y, 1});
                worst = std::max(worst, std::fabs(fx.lastOffsetMs()));
            }
            CHECK(worst <= 2.65f);
            CHECK(worst >= 2.0f); // and the swing really is used
        }
}

TEST_CASE("Warp: holding FS1 brings wow and flutter to the max, releasing brings the knobs back") {
    const float sr = 48000.0f;
    const auto  in = sine(sr, 6.0, 1000.0, 0.5f);
    Panel       p; // wow and flutter at zero
    const auto  r = go(in, p.yaml() + "  - { t: 1.0, stomp: 1, action: hold, for: 2.0 }\n", sr, {1, 512});
    // Hold fires 0.5 s into the press; the default performance speed is 200 ms.
    const std::vector<float> before(r.output.begin(), r.output.begin() + static_cast<long>(at(1.4, sr)));
    const std::vector<float> held(r.output.begin() + static_cast<long>(at(1.8, sr)),
                                  r.output.begin() + static_cast<long>(at(3.0, sr)));
    const std::vector<float> after(r.output.begin() + static_cast<long>(at(3.3, sr)), r.output.end());
    CHECK(peakCents(before, sr, 0.05) < 0.5);
    CHECK(peakCents(held, sr, 0.0) > 30.0);
    CHECK(peakCents(after, sr, 0.0) < 0.5);
    CHECK(r.leds.back().led1 == 1.0f); // a hold is not a bypass tap
}

TEST_CASE("knobs snap before the first block: Mix at 0 is dry from the first sample") {
    for (float sr : kRates) {
        CAPTURE(sr);
        Panel p;
        p.wow = 1.0f, p.flutter = 1.0f, p.mixPct = 0.0f;
        const auto in = noise(sr, 0.5, 4);
        CHECK(go(in, p.yaml(), sr).output == in);
    }
}

TEST_CASE("LED 1: steady without wow, swinging between 60 and 100% with it, off in bypass") {
    const float sr = 48000.0f;
    const auto  in = noise(sr, 4.0, 2);
    Panel       p;
    const auto  still = go(in, p.yaml(), sr);
    CHECK(still.leds.size() == 1);
    CHECK(still.leds.front().led1 == 1.0f);

    p.wow           = 1.0f;
    const auto wavy = go(in, p.yaml(), sr);
    float lo = 1.0f, hi = 0.0f;
    for (const LedSample& l : wavy.leds) lo = std::min(lo, l.led1), hi = std::max(hi, l.led1);
    CHECK(lo >= 0.6f);
    CHECK(hi <= 1.0f);
    CHECK(hi - lo > 0.25f);
}

TEST_CASE("v0.5 defaults: dry_align on, Warp no faster than 100 ms") {
    OnceMore         fx;
    const ParamSpec* align = fx.params().find("dry_align");
    const ParamSpec* perf  = fx.params().find("performance_ms");
    REQUIRE(align);
    REQUIRE(perf);
    CHECK(toReal(*align, align->defaultNorm) == 1.0f);
    CHECK(perf->min == 100.0f);
    CHECK(std::fabs(toReal(*perf, perf->defaultNorm) - 200.0f) < 0.05f);

    // With the defaults, partial Mix is a clean blend: no comb filter, so a
    // 1 kHz sine comes out at its own level whatever the wet's delay.
    Panel p;
    p.mixPct   = 50.0f;
    p.dryAlign = true;
    p.model    = 1;
    const auto in = sine(48000.0f, 1.0, 1000.0, 0.1f);
    const auto r  = go(in, p.yaml());
    CHECK(std::fabs(db(dtft(r.output, 1000.0, 48000.0f, 9600, 48000) / dtft(in, 1000.0, 48000.0f, 9600, 48000))) < 0.5);
}


// --- N2: saturation, head loss, machines -------------------------------------

namespace {

// Output/input level of a 1 kHz sine in dB, past the start-up.
double levelDb(const std::vector<float>& out, const std::vector<float>& in, double hz, float sr) {
    const size_t from = at(0.2, sr), to = in.size();
    return db(dtft(out, hz, sr, from, to) / dtft(in, hz, sr, from, to));
}

// Frequency response of the wet path from a small impulse (the saturator is
// linear there), on a log grid. The passband gain is 1 by design, so the table's
// edges are where the response is 3 dB below unity.
struct Response {
    std::vector<double> hz, gainDb;

    double edge(bool low) const { // first / last crossing of -3 dB, interpolated
        const double peak = 0.0;
        if (low) {
            for (size_t i = 1; i < hz.size(); ++i)
                if (gainDb[i - 1] - peak < -3.0 && gainDb[i] - peak >= -3.0)
                    return hz[i - 1] + (hz[i] - hz[i - 1]) * (-3.0 - (gainDb[i - 1] - peak)) / (gainDb[i] - gainDb[i - 1]);
        } else {
            for (size_t i = hz.size() - 1; i > 0; --i)
                if (gainDb[i] - peak < -3.0 && gainDb[i - 1] - peak >= -3.0)
                    return hz[i - 1] + (hz[i] - hz[i - 1]) * (-3.0 - (gainDb[i - 1] - peak)) / (gainDb[i] - gainDb[i - 1]);
        }
        return 0.0;
    }
};

Response response(int model, float sr) {
    Panel p;
    p.model = model;
    p.bias  = 0.0f;
    std::vector<float> imp(at(0.25, sr), 0.0f);
    imp[10]      = 0.01f;
    const auto r = go(imp, p.yaml(), sr, {1, 512});
    Response   res;
    for (double hz = 15.0; hz < 0.48 * sr; hz *= 1.01) {
        res.hz.push_back(hz);
        res.gainDb.push_back(db(dtft(r.output, hz, sr) / 0.01));
    }
    return res;
}

struct Edges { double low, high; };
const Edges kEdges[] = {{30, 18000}, {40, 12000}, {60, 9000}, {100, 10000}, {100, 5000},
                        {300, 5000}, {300, 3400}, {400, 2000}, {40, 15000}, {60, 7000}};

} // namespace

TEST_CASE("6. the tanh approximation: within 0.5% to |x| = 3, monotonic everywhere") {
    for (float x = 0.001f; x <= 3.0f; x += 0.001f) {
        const double t = std::tanh(static_cast<double>(x));
        CHECK(std::fabs(fx::tape::tanhApprox(x) - t) / t < 0.005);
        CHECK(fx::tape::tanhApprox(-x) == -fx::tape::tanhApprox(x));
    }
    float last = fx::tape::tanhApprox(-20.0f);
    for (float x = -20.0f; x <= 20.0f; x += 0.01f) {
        const float y = fx::tape::tanhApprox(x);
        CHECK(y >= last);
        last = y;
    }
}

TEST_CASE("6. a -18 dBFS sine keeps its level at every drive (+-1.5 dB)") {
    for (float sr : kRates)
        for (float drive : {1.0f, 2.0f, 3.0f, 6.0f, 12.0f}) {
            CAPTURE(sr);
            CAPTURE(drive);
            Panel p;
            p.model       = 1;
            p.drive       = drive;
            const auto in = sine(sr, 1.0, 1000.0, 0.125893f);
            CHECK(std::fabs(levelDb(go(in, p.yaml(), sr).output, in, 1000.0, sr)) <= 1.5);
        }
}

TEST_CASE("6. bias adds the 2nd harmonic and no DC; at full drive aliases stay below -60 dB") {
    const float sr = 48000.0f;
    const auto  in = sine(sr, 2.0, 1000.0, 0.125893f);
    Panel       p;
    p.model = 1, p.drive = 6.0f;
    p.bias           = 0.0f;
    const auto flat  = go(in, p.yaml(), sr).output;
    p.bias           = 0.1f;
    const auto bent  = go(in, p.yaml(), sr).output;
    const size_t a = at(0.5, sr), b = in.size();
    const double h1 = dtft(bent, 1000.0, sr, a, b);
    CHECK(db(dtft(flat, 2000.0, sr, a, b) / h1) < -80.0);
    CHECK(db(dtft(bent, 2000.0, sr, a, b) / h1) > -50.0);
    double mean = 0.0;
    for (size_t i = a; i < b; ++i) mean += bent[i];
    mean /= static_cast<double>(b - a);
    CHECK(db(std::fabs(mean)) < -60.0);

    // 5 kHz at full drive: the odd harmonics above Nyquist fold back to these
    // frequencies; the audible ones (below 20 kHz) must stay 60 dB down.
    p.drive = 12.0f, p.bias = 0.0f;
    const auto hi  = sine(sr, 2.0, 5000.0, 0.125893f);
    const auto out = go(hi, p.yaml(), sr).output;
    const double f1 = dtft(out, 5000.0, sr, a, b);
    for (int n = 5; n <= 25; n += 2) {
        double f = std::fmod(5000.0 * n, sr);
        if (f > sr / 2) f = sr - f;
        if (f > 20000.0 || std::fmod(f, 5000.0) == 0.0) continue;
        CAPTURE(n);
        CAPTURE(f);
        CHECK(db(dtft(out, f, sr, a, b) / f1) < -60.0);
    }
}

TEST_CASE("7. head loss: the paper's numbers, and the FIR follows its target") {
    const float ips75 = 0.1905f, ips375 = 0.09525f, d = 10e-6f;
    const double l1 = db(fx::tape::headLoss(1000.0f, ips75, d, 0.0f));
    const double l5 = db(fx::tape::headLoss(5000.0f, ips75, d, 0.0f));
    CHECK(std::fabs(l1 + 2.9) <= 0.5);
    CHECK(std::fabs(l5 + 14.3) <= 0.5);
    CHECK(std::fabs(db(fx::tape::headLoss(1000.0f, ips375, d, 0.0f)) - 2.0 * l1) <= 1.0);
    CHECK(std::fabs(db(fx::tape::headLoss(5000.0f, ips375, d, 0.0f)) - 2.0 * l5) <= 1.0);

    for (float sr : kRates) {
        CAPTURE(sr);
        const int          taps = fx::tape::firTaps(sr);
        std::vector<float> h(static_cast<size_t>(taps));
        fx::tape::designFir(h.data(), taps, sr, [&](float hz) { return fx::tape::headLoss(hz, ips75, d, 0.0f); });
        for (float hz : {1000.0f, 5000.0f}) {
            CAPTURE(hz);
            CHECK(std::fabs(db(dtft(h, hz, sr)) - db(fx::tape::headLoss(hz, ips75, d, 0.0f))) <= 0.5);
        }
    }
}

TEST_CASE("8, 13. every machine's -3 dB points within 10% of the table") {
    for (float sr : kRates)
        for (int m = 1; m <= OnceMore::kMachines; ++m) {
            CAPTURE(sr);
            CAPTURE(m);
            const Response r = response(m, sr);
            const Edges    e = kEdges[m - 1];
            CHECK(std::fabs(r.edge(true) / e.low - 1.0) <= 0.10);
            CHECK(std::fabs(r.edge(false) / e.high - 1.0) <= 0.10);
        }
}

TEST_CASE("7, 13. changing machines never clicks; the knob has hysteresis") {
    for (float sr : kRates) {
        CAPTURE(sr);
        const auto in = sine(sr, 5.0, 1000.0, 0.5f);
        Panel      p;
        p.model            = 1;
        std::string script = p.yaml();
        // Every machine in turn, each for a quarter second, up and back down.
        for (int i = 0; i < 16; ++i) {
            char ev[96];
            const int m = i < 8 ? i + 1 : 16 - i;
            std::snprintf(ev, sizeof ev, "  - { t: %g, knob: 4, to: %.9g }\n", 0.5 + 0.25 * i,
                          static_cast<double>((m - 1) / 7.0f));
            script += ev;
        }
        const auto   r        = go(in, script, sr, {1, 512});
        const double maxSlope = 6.283185307179586 * 1000.0 / sr * 0.5;
        double       worst    = 0.0;
        for (size_t i = at(0.3, sr); i < r.output.size(); ++i)
            worst = std::max(worst, std::fabs(static_cast<double>(r.output[i]) - r.output[i - 1]));
        CHECK(worst <= 2.0 * maxSlope);
    }

    // A knob resting on a boundary and jittering 1% either way stays put.
    OnceMore fx;
    struct Heap final : Allocator {
        std::vector<std::vector<float>> blocks;
        void* allocate(size_t bytes) override {
            blocks.emplace_back((bytes + 3) / 4, 0.0f);
            return blocks.back().data();
        }
    } heap;
    fx.prepare({48000.0f, 48, &heap});
    fx.setParam(OnceMore::kModel, 1.5f / 7.0f - 0.03f); // machine 2
    float x = 0.0f, y = 0.0f;
    fx.process({&x, &y, 1});
    for (int i = 0; i < 20; ++i) {
        fx.setParam(OnceMore::kModel, 1.5f / 7.0f + (i % 2 ? 0.01f : -0.01f));
        fx.process({&x, &y, 1});
    }
    CHECK(fx.machine() == 2);
    fx.setParam(OnceMore::kModel, 1.5f / 7.0f + 0.03f); // clearly past it
    for (int i = 0; i < 4800; ++i) fx.process({&x, &y, 1});
    CHECK(fx.machine() == 3);
}

TEST_CASE("13. wow and flutter are the knobs' alone: every machine moves the same (v0.7)") {
    const float sr = 48000.0f;
    const auto  in = sine(sr, 6.0, 1000.0, 0.5f);
    Panel       p;
    p.wow = 0.6f, p.flutter = 0.4f;
    std::vector<double> cents;
    for (int m : {1, 2, 3, 6, 7, 8}) { // a 1 kHz sine passes all of these
        CAPTURE(m);
        p.model = m;
        cents.push_back(peakCents(go(in, p.yaml(), sr).output, sr, 0.3));
        CHECK(cents.back() > 10.0);
    }
    const auto [lo, hi] = std::minmax_element(cents.begin(), cents.end());
    CHECK(*hi - *lo < 1.0); // the same swing; only the filters differ

    // The knob at zero means no movement, on any machine.
    p.wow = 0.0f, p.flutter = 0.0f, p.model = 3;
    CHECK(peakCents(go(in, p.yaml(), sr).output, sr, 0.3) < 0.05);
}

TEST_CASE("Warp released with the knobs at zero never undershoots into NaN (regression)") {
    // A linear ramp's running sum could end a hair below 0, and sqrt() of that
    // is NaN; go() fails on any NaN in the output.
    for (float sr : kRates)
        for (float perf : {0.0f, 0.13f, 0.37f, 0.5f, 0.8f, 0.91f, 1.0f}) {
            CAPTURE(sr);
            CAPTURE(perf);
            Panel       p; // wow, flutter 0; bypass never engaged
            char        ev[160];
            std::snprintf(ev, sizeof ev,
                          "  - { t: 0, param: performance_ms, to: %g }\n  - { t: 0.1, stomp: 1, action: hold, for: 0.7 }\n",
                          static_cast<double>(perf));
            go(sine(sr, 3.5, 440.0, 0.5f), p.yaml() + ev, sr, {1, 512});
            p.model = 8; // a digital machine: the knobs count for nothing
            go(sine(sr, 3.5, 440.0, 0.5f), p.yaml() + ev, sr, {1, 512});
        }
}

TEST_CASE("5. v0.9: the knob sets the pitch deviation, nearly linearly (50 / 30 cents x k^0.9)") {
    const float sr = 48000.0f;
    const auto  in = sine(sr, 8.0, 1000.0, 0.5f);
    for (float k : {0.1f, 0.25f, 0.5f, 0.75f}) {
        CAPTURE(k);
        Panel p;
        p.wow = k;
        const double w = peakCents(go(in, p.yaml(), sr).output, sr, 0.3);
        CAPTURE(w);
        // The swing is random: at slow rates few half cycles fit in 8 s, so
        // the largest one seen can fall well short of the design peak.
        CHECK(w >= 0.65 * 50.0 * std::pow(k, 0.9));
        CHECK(w <= 1.1 * 50.0 * std::pow(k, 0.9));
        p.wow = 0.0f, p.flutter = k;
        const double f = peakCents(go(in, p.yaml(), sr).output, sr, 0.3);
        CAPTURE(f);
        CHECK(f >= 0.65 * 30.0 * std::pow(k, 0.9));
        CHECK(f <= 1.1 * 30.0 * std::pow(k, 0.9));
    }
}


// --- N3: noise floor and digital section --------------------------------------

#include "oncemore/noise.h"

namespace {
double rmsDb(const std::vector<float>& y, size_t from) {
    double s = 0.0;
    for (size_t i = from; i < y.size(); ++i) s += static_cast<double>(y[i]) * y[i];
    return 10.0 * std::log10(s / static_cast<double>(y.size() - from));
}
} // namespace

TEST_CASE("9. Digital: B bits leave a -6.02 B dB noise floor; 8 kHz turns 5 kHz into 3 kHz") {
    for (float bits : {4.0f, 8.0f, 12.0f}) {
        CAPTURE(bits);
        fx::noise::Digital d;
        d.setup(48000.0f);
        d.reset();
        d.setAgc(false); // the converter alone, at full scale
        Xorshift32 rng;
        std::vector<float> y(48000);
        for (float& v : y) v = d.process(0.0f, 48000.0f, bits, rng);
        CHECK(std::fabs(rmsDb(y, 0) + 6.02 * bits) <= 3.0);
    }
    fx::noise::Digital d;
    d.setup(48000.0f);
    d.reset();
    Xorshift32 rng;
    const auto in = sine(48000.0f, 1.0, 5000.0, 0.5f);
    std::vector<float> y(in.size());
    for (size_t i = 0; i < in.size(); ++i) y[i] = d.process(in[i], 8000.0f, 16.0f, rng);
    CHECK(db(dtft(y, 3000.0, 48000.0f) / dtft(y, 5000.0, 48000.0f)) > -12.0);
}

TEST_CASE("9. Digital in the effect: depth 0 is clean, the filter tames the aliasing, silence stays silent") {
    const float sr = 48000.0f;
    // Depth 0: the toy recorder adds no quantisation noise at all.
    Panel p;
    p.model = 8;
    std::vector<float> silence(at(1.0, sr), 0.0f);
    const auto clean = go(silence, p.yaml(), sr).output;
    CHECK(std::all_of(clean.begin(), clean.end(), [](float v) { return v == 0.0f; }));
    // Full depth, nothing played: the AGC has turned up all the way (+36 dB),
    // so the 4-bit floor of -24 dBFS sits at about -60 dBFS.
    p.digital       = 1.0f;
    const auto grit = go(silence, p.yaml(), sr).output;
    CHECK(std::fabs(rmsDb(grit, at(0.5, sr)) + 60.1) <= 3.0);

    // A guitar-level note (-30 dBFS, 1 kHz) stays well above its own grit:
    // the same signal-to-noise at 4 bits wherever the input level is.
    for (float amp : {0.0316f, 0.1f, 0.5f}) {
        CAPTURE(amp);
        const auto note = sine(sr, 1.5, 1000.0, amp);
        const auto out  = go(note, p.yaml(), sr).output;
        const size_t a  = at(0.5, sr);
        double total = 0.0;
        for (size_t i = a; i < out.size(); ++i) total += static_cast<double>(out[i]) * out[i];
        // The note and the images the 4 kHz hold makes of it (3, 5, 7... kHz)
        // count as signal; what is left is the converter's noise.
        double sig = 0.0;
        for (double f = 1000.0; f < sr / 2; f += 2000.0)
            sig += 2.0 * std::pow(dtft(out, f, sr, a, out.size()), 2) / std::pow(static_cast<double>(out.size() - a), 2) *
                   static_cast<double>(out.size() - a);
        const double snr  = 10.0 * std::log10(sig / std::max(total - sig, 1e-30));
        CAPTURE(snr);
        CHECK(snr > 10.0);
        MESSAGE("toy at amplitude " << amp << ": " << snr << " dB signal-to-noise");
    }

    // The answering machine, 5 kHz in: without its filter the 3 kHz alias
    // is far stronger than with it.
    const auto in = sine(sr, 1.5, 5000.0, 0.5f);
    p.model = 7, p.digital = 1.0f;
    p.digitalFilter    = true;
    const auto on      = go(in, p.yaml(), sr).output;
    p.digitalFilter    = false;
    const auto off     = go(in, p.yaml(), sr).output;
    const size_t a     = at(0.3, sr);
    const double ratio = db(dtft(off, 3000.0, sr, a, in.size()) / dtft(on, 3000.0, sr, a, in.size()));
    CAPTURE(ratio);
    CHECK(ratio > 10.0);
}

TEST_CASE("10. hiss: unit RMS where it is added; the machine's level plus hiss_level") {
    // At the injection point the shaped noise has unit RMS, so the gain is
    // the level (spec: the machine's dBFS + hiss_level).
    for (float sr : kRates) {
        CAPTURE(sr);
        fx::noise::Hiss h;
        h.setup(sr);
        Xorshift32 rng;
        std::vector<float> y(at(4.0, sr));
        for (float& v : y) v = h.next(rng);
        CHECK(std::fabs(rmsDb(y, at(0.1, sr))) <= 0.2);
    }
    // At the output the head and bandwidth take a little of the top (most of
    // the hiss's energy is high up): up to 3 dB on the reel and chrome
    // cassette, never more than the set level. hiss_level moves it exactly.
    for (float sr : kRates)
        for (int m : {1, 9}) {
            CAPTURE(sr);
            CAPTURE(m);
            double base = 0.0;
            for (float lvl : {0.0f, 6.0f, -12.0f}) {
                CAPTURE(lvl);
                Panel p;
                p.model = m, p.hiss = true;
                char ev[96];
                std::snprintf(ev, sizeof ev, "  - { t: 0, param: hiss_level, to: %.9g }\n",
                              static_cast<double>((lvl + 24.0f) / 42.0f));
                p.extra            = ev;
                const auto   y     = go(std::vector<float>(at(3.0, sr), 0.0f), p.yaml(), sr).output;
                const double level = rmsDb(y, at(0.2, sr)) - lvl;
                const double set   = m == 1 ? -85.0 : -78.0;
                CHECK(level <= set + 0.5);
                CHECK(level >= set - 3.0);
                if (lvl == 0.0f) base = level;
                else CHECK(std::fabs(level - base) <= 0.3);
            }
        }
}

TEST_CASE("10. hum: 60 or 50 Hz at its level; -90 dB is silence") {
    for (float sr : kRates)
        for (float hz : {60.0f, 50.0f}) {
            CAPTURE(sr);
            CAPTURE(hz);
            Panel p;
            p.hum   = true;
            p.extra = hz == 50.0f ? "  - { t: 0, param: hum_freq, to: 0 }\n" : "";
            const auto y = go(std::vector<float>(at(4.0, sr), 0.0f), p.yaml(), sr).output;
            CHECK(std::fabs(rmsDb(y, at(0.2, sr)) + 66.0) <= 1.0);
            // The strongest line near the fundamental, to 0.1 Hz.
            double best = 0.0, bestHz = 0.0;
            for (double f = hz - 2.0; f <= hz + 2.0; f += 0.1) {
                const double v = dtft(y, f, sr, at(0.2, sr), y.size());
                if (v > best) best = v, bestHz = f;
            }
            CHECK(std::fabs(bestHz - hz) <= 0.5);
        }
    Panel p;
    p.hum   = true;
    p.extra = "  - { t: 0, param: hum_level, to: 0 }\n"; // -90 dBFS
    const auto y = go(std::vector<float>(48000, 0.0f), p.yaml()).output;
    CHECK(std::all_of(y.begin(), y.end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("10. interference: buzz, whine and crackles at their levels; the whine follows the tape") {
    const float sr = 48000.0f;
    fx::noise::Interference in;
    in.setup(sr);
    in.reset();
    Xorshift32 rng;
    rng.seed(9);
    float  buzzPeak = 0.0f, whinePeak = 0.0f, cracklePeak = 0.0f;
    int    crackles = 0;
    bool   inCrackle = false;
    const size_t n   = at(60.0, sr);
    for (size_t i = 0; i < n; ++i) {
        const auto p = in.next(1.0f, rng);
        if (p.envelope > 0.2f) buzzPeak = std::max(buzzPeak, std::fabs(p.buzz) / p.envelope);
        whinePeak   = std::max(whinePeak, std::fabs(p.whine));
        cracklePeak = std::max(cracklePeak, std::fabs(p.crackle));
        if (p.crackle != 0.0f && !inCrackle) ++crackles;
        inCrackle = p.crackle != 0.0f;
    }
    CHECK(std::fabs(db(buzzPeak) + 54.0) <= 2.0);
    CHECK(std::fabs(db(whinePeak) + 72.0) <= 2.0);
    CHECK(std::fabs(db(cracklePeak) + 42.0) <= 2.0);
    CHECK(crackles >= 0.7 * 1.5 * 60);
    CHECK(crackles <= 1.3 * 1.5 * 60);

    // The whine at 3% fast: 1236 Hz.
    in.reset();
    std::vector<float> w(at(1.0, sr));
    for (float& v : w) v = in.next(1.03f, rng).whine;
    CHECK(dtft(w, 1236.0, sr) > 10.0 * dtft(w, 1200.0, sr));
}

TEST_CASE("N3: with every noise and the digital section on, still seeded and block-size free") {
    for (float sr : kRates) {
        CAPTURE(sr);
        Panel p;
        p.wow = 0.5f, p.flutter = 0.5f, p.hiss = true, p.hum = true, p.interference = true;
        p.digital = 0.7f, p.model = 8, p.mixPct = 80.0f;
        const auto in  = sine(sr, 2.0, 440.0, 0.3f);
        const auto ref = go(in, p.yaml(), sr, {48, 48}, 5).output;
        for (BlockPlan blocks : kBlocks) {
            CAPTURE(blocks.minBlock);
            CAPTURE(blocks.maxBlock);
            CHECK(go(in, p.yaml(), sr, blocks, 5).output == ref);
        }
        CHECK(go(in, p.yaml(), sr, {48, 48}, 6).output != ref);
    }
}

// --- N4: failures ---------------------------------------------------------------

#include "oncemore/failure.h"

namespace {
struct Heap final : Allocator {
    std::vector<std::vector<float>> blocks;
    void* allocate(size_t bytes) override {
        blocks.emplace_back((bytes + 3) / 4, 0.0f);
        return blocks.back().data();
    }
};
} // namespace

TEST_CASE("11. dropouts: 2 u^3 a second, 20-250 ms, up to 6 + 24 u^2 dB deep, no clicks (v0.14)") {
    const float sr = 48000.0f;
    for (float u : {0.75f, 1.0f}) {
        CAPTURE(u);
        fx::failure::Dropouts d;
        d.setup(sr);
        Xorshift32 rng;
        rng.seed(21);
        d.reset(rng);
        int    events = 0, runLen = 0;
        double deepest = 0.0;
        std::vector<int>    lengths;
        std::vector<double> depths;
        for (size_t i = 0; i < at(240.0, sr); ++i) {
            const auto o = d.next(u, true, rng);
            if (o.started) ++events;
            if (o.gain < 1.0f) {
                ++runLen;
                deepest = std::max(deepest, -db(o.gain));
            } else if (runLen > 0) {
                lengths.push_back(runLen);
                depths.push_back(deepest);
                runLen = 0, deepest = 0.0;
            }
        }
        const double expected = 2.0 * u * u * u * 240.0;
        CAPTURE(events);
        CHECK(events >= 0.8 * expected);
        CHECK(events <= 1.2 * expected);
        for (int len : lengths) {
            CHECK(len >= at(0.019, sr));
            CHECK(len <= at(0.251, sr));
        }
        for (double dd : depths) {
            CHECK(dd >= 0.5 * (6.0 + 24.0 * u * u) - 0.5);
            CHECK(dd <= 6.0 + 24.0 * u * u + 0.01);
        }
    }

    // Through the effect: dropouts on, everything else off; no clicks.
    for (float sr2 : kRates) {
        CAPTURE(sr2);
        Panel p;
        p.failure = 1.0f, p.model = 1;
        p.extra   = "  - { t: 0, param: snag_on, to: 0 }\n  - { t: 0, param: crinkle_on, to: 0 }\n";
        const auto in  = sine(sr2, 10.0, 500.0, 0.5f);
        const auto out = go(in, p.yaml(), sr2, {1, 512}).output;
        double worst = 0.0;
        for (size_t i = at(0.3, sr2); i < out.size(); ++i)
            worst = std::max(worst, std::fabs(static_cast<double>(out[i]) - out[i - 1]));
        CHECK(worst <= 2.0 * 6.283185307179586 * 500.0 / sr2 * 0.5);
    }
}

TEST_CASE("12. tape stop: silent within 650 ms of the hold; after release, normal pitch and no lag") {
    for (float sr : kRates) {
        CAPTURE(sr);
        // In the effect: hold FS2 at 0.5 s (the hold fires at 1.0 s), release at 2.5 s.
        Panel p;
        p.model = 1;
        const auto   in  = sine(sr, 4.5, 1000.0, 0.5f);
        const auto   r   = go(in, p.yaml() + "  - { t: 0.5, stomp: 2, action: hold, for: 2.0 }\n", sr, {1, 512});
        REQUIRE(r.gestures.size() == 2);
        const double hold = r.gestures[0].t, release = r.gestures[1].t;
        float quiet = 0.0f;
        for (size_t i = at(hold + 0.65, sr); i < at(release, sr); ++i) quiet = std::max(quiet, std::fabs(r.output[i]));
        CHECK(quiet < 1e-4f);
        const std::vector<float> after(r.output.begin() + static_cast<long>(at(release + 0.33, sr)), r.output.end());
        CHECK(peakCents(after, sr, 0.0) < 5.0);

        // Directly: the lag is gone 320 ms after the release, and the read head
        // stays inside the tape however long the stop (6 s at the slowest).
        Heap     heap;
        OnceMore fx;
        fx.prepare({sr, 1, &heap});
        fx.setParam(OnceMore::kPerformanceMs, 1.0f); // 2 s: a 6 s stop
        float x = 0.0f, y = 0.0f, worst = 0.0f;
        auto  run = [&](double seconds) {
            for (size_t i = 0; i < at(seconds, sr); ++i) {
                fx.process({&x, &y, 1});
                worst = std::max(worst, fx.lastLagSamples() + 0.004f * sr + 2.7e-3f * sr);
            }
        };
        run(0.1);
        fx.onGesture({1, GestureType::Hold, 0});
        run(9.0);
        fx.onGesture({1, GestureType::Release, 0});
        run(0.32);
        CHECK(fx.lastLagSamples() == 0.0f);
        CHECK(fx.lastSpeed() == 1.0f);
        CHECK(worst <= fx.maxDelaySamples());
    }
}

TEST_CASE("13. FS2: tap drags the pitch down and it comes back; Fail and the LED") {
    const float sr = 48000.0f;
    const auto  in = sine(sr, 6.0, 1000.0, 0.5f);
    Panel       p;
    p.model     = 1;
    const auto r = go(in, p.yaml() + "  - { t: 0.5, stomp: 2, action: tap }\n", sr);
    REQUIRE(r.gestures.size() == 1);
    const double tap = r.gestures[0].t;
    // The drag: r down to 1 - 0.06 x 0.5 x (0.5-1), i.e. 26-52 cents flat.
    const std::vector<float> drag(r.output.begin() + static_cast<long>(at(tap, sr)),
                                  r.output.begin() + static_cast<long>(at(tap + 0.8, sr)));
    CHECK(peakCents(drag, sr, 0.0) > 20.0);
    const std::vector<float> later(r.output.begin() + static_cast<long>(at(tap + 3.5, sr)), r.output.end());
    CHECK(peakCents(later, sr, 0.0) < 1.0);
    // LED 2 flashed once for it, for about 80 ms.
    int flashes = 0;
    for (size_t i = 1; i < r.leds.size(); ++i)
        if (r.leds[i].led2 > 0.5f && r.leds[i - 1].led2 < 0.5f) ++flashes;
    CHECK(flashes == 1);

    // Fail (Toggle 4 down): holding FS2 makes a quiet knob break things,
    // and LED 2 stays on while held.
    p.fs2Stop = false;
    p.extra   = "  - { t: 0, param: snag_on, to: 0 }\n  - { t: 0, param: crinkle_on, to: 0 }\n";
    const auto f = go(sine(sr, 8.0, 500.0, 0.5f), p.yaml() + "  - { t: 0.5, stomp: 2, action: hold, for: 6.0 }\n", sr);
    REQUIRE(f.gestures.size() == 2);
    double minDb = 0.0;
    for (size_t i = at(f.gestures[0].t + 0.3, sr); i < at(f.gestures[1].t, sr); i += at(0.005, sr)) {
        double peak = 0.0;
        for (size_t k = i; k < i + at(0.005, sr); ++k) peak = std::max(peak, std::fabs(static_cast<double>(f.output[k])));
        minDb = std::min(minDb, db(peak / 0.5));
    }
    CHECK(minDb < -6.0); // at least one dropout while held, with the knob at 0
    bool lit = true;
    for (const LedSample& l : f.leds)
        if (l.t > f.gestures[0].t + 0.01 && l.t < f.gestures[1].t - 0.01) lit = lit && l.led2 == 1.0f;
    CHECK(lit);

    // During a stop LED 2 follows the tape speed down.
    p.fs2Stop = true, p.extra.clear();
    const auto s = go(sine(sr, 3.0, 1000.0, 0.5f), p.yaml() + "  - { t: 0.5, stomp: 2, action: hold, for: 2.0 }\n", sr);
    float mid = -1.0f;
    for (const LedSample& l : s.leds)
        if (l.t > s.gestures[0].t + 0.25 && l.t < s.gestures[0].t + 0.35) mid = l.led2;
    CHECK(mid > 0.2f);
    CHECK(mid < 0.8f);
}

TEST_CASE("13. the event processes keep their rates (Poisson, crinkles)") {
    const float sr = 48000.0f;
    fx::failure::Poisson ev;
    Xorshift32 rng;
    rng.seed(4);
    ev.reset(rng);
    int n = 0;
    for (size_t i = 0; i < at(200.0, sr); ++i) n += ev.next(0.5f, sr, rng) ? 1 : 0;
    CHECK(n >= 0.8 * 100);
    CHECK(n <= 1.2 * 100);

    fx::failure::Crinkles c;
    c.setup(sr);
    c.reset(rng);
    int crinkles = 0;
    float peak = 0.0f;
    for (size_t i = 0; i < at(60.0, sr); ++i) {
        const auto o = c.next(2.0f, rng);
        crinkles += o.started ? 1 : 0;
        peak = std::max(peak, std::fabs(o.noise));
    }
    CHECK(crinkles >= 0.8 * 120);
    CHECK(crinkles <= 1.2 * 120);
    CHECK(std::fabs(db(peak) + 40.0) <= 2.0);
}

TEST_CASE("N4: every failure on, still seeded, block-size free and bounded") {
    for (float sr : kRates) {
        CAPTURE(sr);
        Panel p;
        p.wow = 0.5f, p.flutter = 0.5f, p.failure = 1.0f, p.hiss = true, p.interference = true, p.model = 3;
        const auto in  = sine(sr, 6.0, 440.0, 0.3f);
        const auto ref = go(in, p.yaml(), sr, {48, 48}, 5);
        CHECK(ref.peak <= 2.0f);
        for (BlockPlan blocks : kBlocks) {
            CAPTURE(blocks.minBlock);
            CAPTURE(blocks.maxBlock);
            CHECK(go(in, p.yaml(), sr, blocks, 5).output == ref.output);
        }
        CHECK(go(in, p.yaml(), sr, {48, 48}, 6).output != ref.output);
    }
}


TEST_CASE("13. v0.14: Failure is light in the first half, falling apart only near the end") {
    // Events counted from LED 2's flashes: 2 f^3 dropouts + 0.5 f^2 faults +
    // 2 f^2 crinkles a second (a fault may be skipped while one is running).
    const float sr = 48000.0f;
    for (float f : {0.25f, 0.5f, 1.0f}) {
        CAPTURE(f);
        Panel p;
        p.failure = f, p.model = 1;
        const auto r = go(std::vector<float>(at(120.0, sr), 0.0f), p.yaml(), sr);
        int flashes = 0;
        for (size_t i = 1; i < r.leds.size(); ++i)
            if (r.leds[i].led2 == 1.0f && r.leds[i - 1].led2 < 1.0f) ++flashes;
        const double expected = (2.0 * f * f * f + 0.5 * f * f + 2.0 * f * f) * 120.0;
        CAPTURE(flashes);
        CAPTURE(expected);
        CHECK(flashes >= 0.6 * expected);
        CHECK(flashes <= 1.2 * expected);
    }
}
