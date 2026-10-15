#include "oncemore.h"

#include <algorithm>
#include <cmath>

namespace tfx::fx {

namespace {

constexpr float kPi    = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
constexpr float kLn2   = 0.693147181f;

// Section names refer to effects/oncemore/spec.md.
constexpr ParamSpec kSpecs[] = {
    {OnceMore::kWow, "wow", "Wow", "", 0.0f, 1.0f, Curve::Linear, 0.25f, 20.0f, Control::Knob1,
     TFX_HELP("Slow pitch drift (0.3-2 Hz), like a worn capstan or a stretched tape. Peak deviation about 6 cents at 0.1, 14 at 1/4 (default), 27 at half, 50 at full. 0 = none, on every machine.")},
    {OnceMore::kFlutter, "flutter", "Flutter", "", 0.0f, 1.0f, Curve::Linear, 0.15f, 20.0f, Control::Knob2,
     TFX_HELP("Fast pitch flutter (6-20 Hz) that also moves the level (see Flutter AM). Peak deviation about 5 cents at the default, 16 at half, 30 at full.")},
    {OnceMore::kMix, "mix", "Mix", "%", 0.0f, 100.0f, Curve::Linear, 1.0f, 20.0f, Control::Knob6,
     TFX_HELP("Dry/wet balance. With Dry align on (default) the two line up, so partial settings blend cleanly instead of comb-filtering.")},
    {OnceMore::kFlutterAm, "flutter_am", "Flutter AM", "", 0.0f, 1.0f, Curve::Linear, 0.5f, 20.0f, Control::None,
     TFX_HELP("How much flutter also moves the volume: up to +-15% (about +-1.2 dB) with Flutter and this both at full.")},
    // ln(200 / 100) / ln(2000 / 100): 200 ms by default
    {OnceMore::kPerformanceMs, "performance_ms", "Performance speed", "ms", 100.0f, 2000.0f, Curve::Exp, 0.231378f,
     0.0f, Control::None,
     TFX_HELP("How fast Warp (hold FS1) reaches full wow and flutter, and how fast it comes back on release. The tape stop (N4) takes three times as long.")},
    // On by default: an unaligned dry is 4 ms early, a comb filter at partial Mix
    {OnceMore::kDryAlign, "dry_align", "Dry align", "", 0.0f, 1.0f, Curve::Stepped, 1.0f, 0.0f, Control::None,
     TFX_HELP("Delays the dry by the wet's latency (about 5 ms) so partial Mix does not comb-filter. Off: dry is instant but partial Mix sounds hollow.")},
    // ln(3) / ln(12): drive 3 by default
    {OnceMore::kSaturate, "saturate", "Saturate", "", 1.0f, 12.0f, Curve::Exp, 0.442114f, 20.0f, Control::Knob3,
     TFX_HELP("Tape saturation drive, 1-12. The level stays about the same as you turn it: only the colour changes, from clean warmth to dense compression.")},
    // Eight machines, 2 (cassette) by default; hysteresis in setParam()
    {OnceMore::kModel, "model", "Model", "", 1.0f, 8.0f, Curve::Stepped, 1.0f / 7.0f, 0.0f, Control::Knob4,
     TFX_HELP("The machine: 1 reel, 2 cassette, 3 walkman, 4 VHS SP, 5 VHS EP, 6 dictaphone, 7 digital answering machine, 8 toy recorder. Sets the bandwidth, the hiss level and, on 7 and 8, the digital grit (sample rate and bits).")},
    {OnceMore::kModelOverride, "model_override", "Model override", "", 0.0f, 10.0f, Curve::Stepped, 0.0f, 0.0f,
     Control::None,
     TFX_HELP("0 = follow the Model knob. 1-10 picks a machine directly, including the hidden 9 (chrome cassette, brighter) and 10 (cassette at half speed, darker).")},
    {OnceMore::kBias, "bias", "Bias", "", 0.0f, 0.1f, Curve::Linear, 0.3f, 20.0f, Control::None,
     TFX_HELP("Asymmetry in the saturation: adds even harmonics, a slightly fuller tape tone. The DC it creates is filtered out.")},
    // N3: the noise floor and the digital section
    {OnceMore::kHissOn, "hiss_on", "Hiss", "", 0.0f, 1.0f, Curve::Stepped, 1.0f, 0.0f, Control::Toggle1,
     TFX_HELP("Tape hiss, at the level of the machine: from almost silent on the reel (-85 dBFS) to plainly audible on VHS EP (-62 dBFS), about a real machine's distance below a guitar at -30 dBFS. Added before the head and bandwidth, so it dulls with the machine. Digital machines have none.")},
    {OnceMore::kHumOn, "hum_on", "Hum", "", 0.0f, 1.0f, Curve::Stepped, 0.0f, 0.0f, Control::Toggle2,
     TFX_HELP("Mains hum from the machine's power supply: 60 Hz (or 50, see Hum freq) with its 2nd and 3rd harmonics, at Hum level.")},
    {OnceMore::kInterferenceOn, "interference_on", "Static", "", 0.0f, 1.0f, Curve::Stepped, 0.0f, 0.0f,
     Control::Toggle3,
     TFX_HELP("The machine's own noises, coming and going: a 59.94 Hz head-switching buzz that drifts in and out, a 1.2 kHz motor whine that bends with wow and the tape stop, and crackles (about 1.5 a second).")},
    // -24..+18 dB around the machine's hiss: 0 dB is 24/42 of the way
    {OnceMore::kHissLevel, "hiss_level", "Hiss level", "dB", -24.0f, 18.0f, Curve::Linear, 0.571429f, 30.0f,
     Control::None,
     TFX_HELP("Hiss relative to the machine's own level, -24 to +18 dB. Push it up for a tape buried in noise.")},
    // -66 dBFS: 24/40 of the way from -90
    {OnceMore::kHumLevel, "hum_level", "Hum level", "dBFS", -90.0f, -50.0f, Curve::Linear, 0.6f, 30.0f,
     Control::None, TFX_HELP("Hum level (RMS), -90 dBFS (silent) to -50 dBFS. The default -66 is plainly there but well under a guitar.")},
    {OnceMore::kHumFreq, "hum_freq", "Hum freq", "Hz", 50.0f, 60.0f, Curve::Stepped, 1.0f, 0.0f, Control::None,
     TFX_HELP("Mains frequency: 60 Hz (Taiwan, the Americas) or 50 Hz (Europe, most of Asia). Below the middle is 50.")},
    {OnceMore::kInterferenceLevel, "interference_level", "Static level", "dB", -24.0f, 18.0f, Curve::Linear,
     0.571429f, 30.0f, Control::None,
     TFX_HELP("Interference relative to its design levels (buzz -54, whine -72, crackles -42 dBFS peak), -24 to +18 dB.")},
    {OnceMore::kDigitalDepth, "digital_depth", "Digital depth", "", 0.0f, 1.0f, Curve::Linear, 1.0f, 30.0f, Control::None,
     TFX_HELP("How far the digital machines (7, 8) go: 0 = clean, 1 = all the way to their 8 kHz / 8 bit or 4 kHz / 4 bit. Sample rate and bits move together, smoothly. An AGC keeps the playing near the converter's full scale, so the grit follows your level instead of burying it.")},
    {OnceMore::kDigitalFilter, "digital_filter", "Digital filter", "", 0.0f, 1.0f, Curve::Stepped, 1.0f, 0.0f,
     Control::None,
     TFX_HELP("On: the digital machines band-limit before dropping the sample rate (their 3.4 kHz / 2 kHz edge). Off: no filter, so the aliasing comes through raw and much harsher.")},
    // 0 dB: 12/18 of the way from -12
    {OnceMore::kLevel, "level", "Output level", "dB", -12.0f, 6.0f, Curve::Linear, 0.666667f, 20.0f, Control::None,
     TFX_HELP("Level of the wet signal, -12 to +6 dB, to match the narrow machines to the bypassed sound.")},
    // N4: failures
    {OnceMore::kFailure, "failure", "Failure", "", 0.0f, 1.0f, Curve::Linear, 0.0f, 20.0f, Control::Knob5,
     TFX_HELP("How broken the transport and the tape are. Light in the first half (at 0.5: a dropout every 4 s, a drag every 8 s, a crinkle every 2 s), falling apart towards the end (at 1: 2 dropouts, 0.5 transport faults and 2 crinkles a second, tape stops above 0.85).")},
    {OnceMore::kFs2Action, "fs2_action", "FS2 hold", "", 0.0f, 1.0f, Curve::Stepped, 1.0f, 0.0f, Control::Toggle4,
     TFX_HELP("What holding FS2 does. Up: tape stop (the pitch slides down to silence, and spins back up on release). Down: Fail (Failure pushed to the max while held).")},
    {OnceMore::kDropOn, "drop_on", "Dropouts", "", 0.0f, 1.0f, Curve::Stepped, 1.0f, 0.0f, Control::None,
     TFX_HELP("Lets Failure cause oxide dropouts.")},
    {OnceMore::kSnagOn, "snag_on", "Snags", "", 0.0f, 1.0f, Curve::Stepped, 1.0f, 0.0f, Control::None,
     TFX_HELP("Lets Failure cause transport faults: drags, wobbles and tape stops.")},
    {OnceMore::kCrinkleOn, "crinkle_on", "Crinkles", "", 0.0f, 1.0f, Curve::Stepped, 1.0f, 0.0f, Control::None,
     TFX_HELP("Lets Failure cause crinkles: short clicks of creased tape.")},
};
constexpr ParamTable kTable{kSpecs, sizeof(kSpecs) / sizeof(kSpecs[0])};
static_assert(sizeof(kSpecs) / sizeof(kSpecs[0]) == OnceMore::kParamCount, "one spec per parameter id");

// Wow and Flutter
constexpr float kD0Ms          = 4.0f;
constexpr float kTapeSeconds   = 3.2f;  // a 6 s tape stop (3 x performance_ms) lags 3 s
// Spec v0.9: the knobs set the pitch deviation heard, the depth follows.
constexpr float kWowCents      = 50.0f;  // peak deviation at full, x k^0.9
constexpr float kWowMinHz      = 0.3f;   // + 1.7 k: 0.3 Hz to 2 Hz
constexpr float kWowSpanHz     = 1.7f;
constexpr float kWowMaxMs      = 2.5f;   // depth limit; the curve stays under it
constexpr float kFlutterCents  = 30.0f;
constexpr float kFlutterMaxMs  = 0.15f;
constexpr float kCurve         = 0.9f;   // nearly linear in the knob
constexpr float kFlutterMinHz  = 6.0f;
constexpr float kLnFlutterSpan = 1.20397280f; // ln(20 / 6): 6 Hz to 20 Hz
constexpr float kWowSineShare  = 0.15f; // the rest is the random swing
constexpr float kFlutterAmMax  = 0.15f; // gain swing at flutter = flutter_am = 1
constexpr float kSwitchMs      = 10.0f; // bypass, dry align

// Model: the machine library, edges from the spec's table; 9 and 10 (hidden)
// are our own extension of it. Since v0.7 a machine is a sound only: wow and
// flutter come from their knobs alone, as on Generation Loss. Hiss and the
// digital section come in N3.
struct Machine {
    float lowHz;   // high-pass, -3 dB
    float highHz;  // head loss (or the digital low-pass), -3 dB
    bool  digital;
    float hissDb;  // dBFS RMS where hiss is added; digital machines have none
    float ftHz;    // digital: target sample rate
    float bits;    // digital: bits
};
constexpr float   kNoHiss = -200.0f;
constexpr Machine kMachine[OnceMore::kMachines] = {
    // Hiss (v0.12): about a real machine's signal-to-noise below a guitar
    // playing at -30 dBFS RMS.
    {30.0f, 18000.0f, false, -85.0f, 0, 0},       // 1 reel to reel
    {40.0f, 12000.0f, false, -74.0f, 0, 0},       // 2 cassette
    {60.0f, 9000.0f, false, -70.0f, 0, 0},        // 3 walkman
    {100.0f, 10000.0f, false, -68.0f, 0, 0},      // 4 VHS SP
    {100.0f, 5000.0f, false, -62.0f, 0, 0},       // 5 VHS EP
    {300.0f, 5000.0f, false, -64.0f, 0, 0},       // 6 dictaphone
    {300.0f, 3400.0f, true, kNoHiss, 8000, 8},    // 7 digital answering machine
    {400.0f, 2000.0f, true, kNoHiss, 4000, 4},    // 8 toy recorder
    {40.0f, 15000.0f, false, -78.0f, 0, 0},       // 9 chrome cassette (hidden)
    {60.0f, 7000.0f, false, -70.0f, 0, 0},        // 10 cassette at half speed (hidden)
};
// A slot's key: the machine, + 100 for a digital machine without its filter.
constexpr int kUnfiltered = 100;
const Machine& machineOf(int key) { return kMachine[key % kUnfiltered - 1]; }
constexpr float kNoiseFadeMs = 30.0f;
constexpr float kModelFadeMs  = 30.0f;
constexpr float kModelHyst    = 0.02f; // of the knob's travel, around each boundary

// Per-module seed offsets, so the generators never share a sequence.
constexpr uint32_t kWowSeed     = 0x0001F00Du;
constexpr uint32_t kFlutterSeed = 0x0002F00Du;
constexpr uint32_t kHissSeed    = 0x0003F00Du;
constexpr uint32_t kInterfSeed  = 0x0004F00Du;
constexpr uint32_t kDigitalSeed = 0x0005F00Du;
constexpr uint32_t kDropSeed    = 0x0006F00Du;
constexpr uint32_t kSnagSeed    = 0x0007F00Du;
constexpr uint32_t kCrinkleSeed = 0x0008F00Du;
constexpr float    kFlashMs     = 80.0f;

float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

} // namespace

// --- Swing -----------------------------------------------------------------

void OnceMore::Swing::newTarget(Xorshift32& rng) {
    // Two draws per half cycle, whatever the block size (spec: platform).
    sign    = -sign;
    to      = sign * (0.75f + 0.25f * rng.uniform());
    // Half cycles 100..125% as long as the rate says, never shorter: the
    // rate is the fastest the swing goes, which caps the pitch deviation.
    stretch = 1.0f / (1.0f + 0.25f * rng.uniform());
}

void OnceMore::Swing::restart(Xorshift32& rng) {
    from = 0.0f, phase = 0.0f, sign = -1.0f;
    newTarget(rng);
}

float OnceMore::Swing::next(float halfCyclesPerSample, Xorshift32& rng) {
    phase += halfCyclesPerSample * stretch;
    if (phase >= 1.0f) {
        phase = std::min(phase - 1.0f, 0.999f);
        from  = to;
        newTarget(rng);
    }
    return from + (to - from) * (0.5f - 0.5f * std::cos(kPi * phase));
}

float OnceMore::Swing::slope() const {
    // d/dphase of the ease is (to - from) * pi/2 * sin(pi phase); a unit sine
    // crossing the same half cycle peaks at 2 * pi/2.
    return 0.5f * (to - from) * std::sin(kPi * phase) * stretch;
}

// --- OnceMore --------------------------------------------------------------

// Spec, Model: which machine is wanted, from the knob or the override.
void OnceMore::chooseMachine() {
    const int m = override_ > 0 ? override_ : knobMachine_;
    wanted_     = kMachine[m - 1].digital && !digitalFilter_ ? m + kUnfiltered : m;
    if (!started_) {
        // Platform rule 7: the starting machine is simply there.
        slotMachine_[active_] = wanted_;
        fading_               = false;
        hp_[active_].setup(machineOf(wanted_).lowHz, fs_);
    }
    updateNoise();
}

// Spec, noise and Digital: the gains and digital settings the wanted machine
// and the switches ask for, reached over 30 ms.
void OnceMore::updateNoise() {
    const Machine& mc = machineOf(wanted_);
    setSmoothed(hissGain_, hissOn_ && mc.hissDb > kNoHiss ? dbToGain(mc.hissDb + hissLevelDb_) : 0.0f);
    // Spec: -90 dB means off, not merely quiet.
    setSmoothed(humGain_, humOn_ && humLevelDb_ > -89.99f ? dbToGain(humLevelDb_) : 0.0f);
    setSmoothed(interfGain_, interfOn_ ? dbToGain(interfLevelDb_) : 0.0f);
    setSmoothed(digitalAmount_, mc.digital ? digitalDepth_ : 0.0f);
    if (mc.digital) {
        setSmoothed(digitalLnFt_, std::log(mc.ftHz));
        setSmoothed(digitalBits_, mc.bits);
    }
}

// Crossfade to the wanted machine: the idle slot takes over the high-pass's
// state (same input, new corner) and fades in over 30 ms. A change during a
// fade waits for it to end.
void OnceMore::startFade() {
    const int next     = 1 - active_;
    slotMachine_[next] = wanted_;
    hp_[next].setup(machineOf(wanted_).lowHz, fs_);
    hp_[next].copyState(hp_[active_]);
    fade_.snap(0.0f);
    fade_.setTarget(1.0f);
    fading_ = true;
}

OnceMore::OnceMore() {
    // Start the digital settings at the first digital machine's, so a fade
    // from an analog machine never passes through nonsense rates.
    digitalLnFt_.snap(std::log(kMachine[6].ftHz));
    digitalBits_.snap(kMachine[6].bits);
    for (const ParamSpec& s : kSpecs) setParam(s.id, s.defaultNorm);
    setSeed(1);
}

const ParamTable& OnceMore::params() const { return kTable; }

void OnceMore::setSmoothed(Smoother& s, float value) {
    // Platform rule 7: snap until the first block, ramp after.
    if (started_) s.setTarget(value);
    else          s.snap(value);
}

void OnceMore::setRamp(Ramp& r, float value) {
    if (started_) r.setTarget(value);
    else          r.snap(value);
}

// Spec v0.9, Wow and Flutter: the knob sets the peak pitch deviation (cents
// = max x k^0.9) and the rate; the delay swing that gives it follows from
// both, since a swing of A seconds at f Hz bends the pitch by up to 2 pi f A.
float OnceMore::swingSamples(float cents, float hz, float maxMs) const {
    const float ratio = std::exp(cents * (kLn2 / 1200.0f)) - 1.0f;
    return std::min(ratio / (kTwoPi * hz), maxMs * 0.001f) * fs_;
}

void OnceMore::updateWow(float k) {
    k = std::min(std::max(k, 0.0f), 1.0f);
    if (k == wowK_) return;
    wowK_ = k;
    const float hz = kWowMinHz + kWowSpanHz * k;
    wowAmp_  = k > 0.0f ? swingSamples(kWowCents * std::pow(k, kCurve), hz, kWowMaxMs) : 0.0f;
    wowStep_ = 2.0f * hz / fs_;
}

void OnceMore::updateFlutter(float k) {
    k = std::min(std::max(k, 0.0f), 1.0f);
    if (k == flutterK_) return;
    flutterK_     = k;
    flutterDepth_ = k > 0.0f ? std::pow(k, kCurve) : 0.0f; // also scales the level swing
    const float hz = kFlutterMinHz * std::exp(kLnFlutterSpan * k);
    flutterAmp_   = k > 0.0f ? swingSamples(kFlutterCents * flutterDepth_, hz, kFlutterMaxMs) : 0.0f;
    flutterStep_  = 2.0f * hz / fs_;
}

void OnceMore::prepare(const PrepareInfo& info) {
    fs_   = info.sampleRate;
    d0_   = static_cast<int>(std::lround(kD0Ms * 0.001f * fs_));
    size_ = static_cast<size_t>(std::ceil(kTapeSeconds * fs_)) + 8;
    buf_  = info.allocator ? static_cast<float*>(info.allocator->allocate(size_ * sizeof(float))) : nullptr;
    // Head-loss FIRs for every machine, and their shared history.
    firTaps_ = tape::firTaps(fs_);
    const size_t firFloats = static_cast<size_t>(kMachines + 2) * static_cast<size_t>(firTaps_);
    fir_ = buf_ ? static_cast<float*>(info.allocator->allocate(firFloats * sizeof(float))) : nullptr;
    if (!fir_) buf_ = nullptr;
    if (!buf_) size_ = 0; // out of memory: process() passes the input through
    if (fir_) {
        firHist_ = fir_ + static_cast<size_t>(kMachines) * static_cast<size_t>(firTaps_);
        for (int m = 0; m < kMachines; ++m) {
            float*           h = fir_ + static_cast<size_t>(m) * static_cast<size_t>(firTaps_);
            const tape::Edge edge{kMachine[m].highHz, kMachine[m].digital};
            tape::designFir(h, firTaps_, fs_, [&edge](float hz) { return tape::edgeGain(edge, hz); });
            std::reverse(h, h + firTaps_); // symmetric anyway; kept for clarity of the dot product
        }
    }

    auto real = [this](ParamId id) { return toReal(kSpecs[id], norm_[id]); };
    wowKnob_.prepare(fs_, kSpecs[kWow].smoothingMs, real(kWow));
    flutterKnob_.prepare(fs_, kSpecs[kFlutter].smoothingMs, real(kFlutter));
    mix_.prepare(fs_, kSpecs[kMix].smoothingMs, real(kMix) * 0.01f);
    flutterAm_.prepare(fs_, kSpecs[kFlutterAm].smoothingMs, real(kFlutterAm));
    drive_.prepare(fs_, kSpecs[kSaturate].smoothingMs, real(kSaturate));
    bias_.prepare(fs_, kSpecs[kBias].smoothingMs, real(kBias));
    fade_.prepare(fs_, kModelFadeMs);
    warp_.prepare(fs_, real(kPerformanceMs));
    bypass_.prepare(fs_, kSwitchMs);
    align_.prepare(fs_, kSwitchMs);
    hissGain_.prepare(fs_, kNoiseFadeMs, hissGain_.target());
    humGain_.prepare(fs_, kNoiseFadeMs, humGain_.target());
    interfGain_.prepare(fs_, kNoiseFadeMs, interfGain_.target());
    digitalAmount_.prepare(fs_, kNoiseFadeMs, digitalAmount_.target());
    digitalLnFt_.prepare(fs_, kNoiseFadeMs, digitalLnFt_.target());
    digitalBits_.prepare(fs_, kNoiseFadeMs, digitalBits_.target());
    level_.prepare(fs_, kSpecs[kLevel].smoothingMs, level_.target());
    hiss_.setup(fs_);
    hum_.setup(fs_);
    interference_.setup(fs_);
    digital_.setup(fs_);
    transport_.setup(fs_, static_cast<float>(size_ > 0 ? size_ : 1) - static_cast<float>(d0_) - 0.004f * fs_ - 8.0f);
    dropouts_.setup(fs_);
    crinkles_.setup(fs_);
    failureKnob_.prepare(fs_, kSpecs[kFailure].smoothingMs, failureKnob_.target());
    fail_.prepare(fs_, perfMs_);
    flashLen_ = static_cast<int>(kFlashMs * 0.001f * fs_);
    wowK_ = flutterK_ = -1.0f; // rates depend on fs
    reset();
}

void OnceMore::reset() {
    if (buf_) std::fill(buf_, buf_ + size_, 0.0f);
    write_   = 0;
    started_ = false;
    for (Smoother* s : {&wowKnob_, &flutterKnob_, &mix_, &flutterAm_, &drive_, &bias_, &hissGain_, &humGain_,
                        &interfGain_, &digitalAmount_, &digitalLnFt_, &digitalBits_, &level_})
        s->snap(s->target());
    hiss_.reset();
    hum_.reset();
    interference_.reset();
    digital_.reset();
    lastDelay_ = static_cast<float>(d0_);
    failureKnob_.snap(failureKnob_.target());
    fail_.snap(failHeld_ ? 1.0f : 0.0f);
    transport_.reset();
    dropLp_ = 0.0f;
    flash_  = 0;
    if (firHist_) std::fill(firHist_, firHist_ + 2 * firTaps_, 0.0f);
    firPos_ = 0;
    sat_.reset();
    hp_[0].reset();
    hp_[1].reset();
    active_ = 0;
    chooseMachine(); // started_ is false: snaps to the wanted machine
    warp_.snap(warping_ ? 1.0f : 0.0f);
    bypass_.snap(bypassed_ ? 1.0f : 0.0f);
    align_.snap(dryAlign_ ? 1.0f : 0.0f);
    setSeed(seed_);
    lastOffset_ = 0.0f;
}

void OnceMore::setSeed(uint32_t seed) {
    seed_ = seed;
    wowRng_.seed(seed + kWowSeed);
    flutterRng_.seed(seed + kFlutterSeed);
    hissRng_.seed(seed + kHissSeed);
    interfRng_.seed(seed + kInterfSeed);
    digitalRng_.seed(seed + kDigitalSeed);
    dropRng_.seed(seed + kDropSeed);
    snagRng_.seed(seed + kSnagSeed);
    crinkleRng_.seed(seed + kCrinkleSeed);
    dropouts_.reset(dropRng_);
    crinkles_.reset(crinkleRng_);
    snagEvents_.reset(snagRng_);
    wowSwing_.restart(wowRng_);
    flutterSwing_.restart(flutterRng_);
    wowSine_ = 0.0f;
}

void OnceMore::setParam(ParamId id, float normalized) {
    if (id >= kParamCount) return;
    const float n = std::min(std::max(normalized, 0.0f), 1.0f);
    norm_[id]     = n;
    const float r = toReal(kSpecs[id], n);
    switch (id) {
        case kWow:           setSmoothed(wowKnob_, r); break;
        case kFlutter:       setSmoothed(flutterKnob_, r); break;
        case kMix:           setSmoothed(mix_, r * 0.01f); break;
        case kFlutterAm:     setSmoothed(flutterAm_, r); break;
        case kPerformanceMs: // the next push or release uses it
            perfMs_ = r;
            warp_.prepare(fs_, r);
            fail_.prepare(fs_, r);
            break;
        case kDryAlign:
            dryAlign_ = r > 0.5f;
            setRamp(align_, dryAlign_ ? 1.0f : 0.0f);
            break;
        case kSaturate: setSmoothed(drive_, r); break;
        case kBias:     setSmoothed(bias_, r); break;
        case kModel: {
            // Spec, Model: eight cells with 2% of hysteresis around each
            // boundary, so a knob resting on one does not flicker.
            const float pos  = n * 7.0f;  // 0..7
            const float keep = 0.5f + kModelHyst * 7.0f;
            if (std::fabs(pos - static_cast<float>(knobMachine_ - 1)) > keep || !started_)
                knobMachine_ = static_cast<int>(std::lround(pos)) + 1;
            chooseMachine();
            break;
        }
        case kModelOverride:
            override_ = static_cast<int>(r);
            chooseMachine();
            break;
        case kHissOn:            hissOn_ = r > 0.5f; updateNoise(); break;
        case kHumOn:             humOn_ = r > 0.5f; updateNoise(); break;
        case kInterferenceOn:    interfOn_ = r > 0.5f; updateNoise(); break;
        case kHissLevel:         hissLevelDb_ = r; updateNoise(); break;
        case kHumLevel:          humLevelDb_ = r; updateNoise(); break;
        case kHumFreq:           hum_.setFrequency(n > 0.5f ? 60.0f : 50.0f); break;
        case kInterferenceLevel: interfLevelDb_ = r; updateNoise(); break;
        case kDigitalDepth:      digitalDepth_ = r; updateNoise(); break;
        case kDigitalFilter:     digitalFilter_ = r > 0.5f; chooseMachine(); break;
        case kLevel:             setSmoothed(level_, dbToGain(r)); break;
        case kFailure:           setSmoothed(failureKnob_, r); break;
        case kFs2Action:         fs2Stop_ = r > 0.5f; break;
        case kDropOn:            dropOn_ = r > 0.5f; break;
        case kSnagOn:            snagOn_ = r > 0.5f; break;
        case kCrinkleOn:         crinkleOn_ = r > 0.5f; break;
        default: break;
    }
}

void OnceMore::onGesture(const Gesture& g) {
    const bool isTap = g.type == GestureType::Tap || g.type == GestureType::DoubleTap;
    if (g.stomp == 1) {
        // Spec, Terrarium controls: FS2 tap = one drag; hold = tape stop or
        // Fail, as Toggle 4 says when the hold begins.
        if (isTap) {
            if (!transport_.busy()) {
                const float u1 = snagRng_.uniform(), u2 = snagRng_.uniform();
                const float f = failureKnob_.target();
                transport_.drag(std::max(f * std::sqrt(f), 0.5f), u1, u2); // audible even with the knob at 0
                flash_ = flashLen_;
            }
        } else if (g.type == GestureType::Hold) {
            heldStop_ = fs2Stop_;
            if (heldStop_) transport_.stop(3.0f * perfMs_);
            else failHeld_ = true, setRamp(fail_, 1.0f);
        } else { // Release
            if (heldStop_) transport_.start();
            else failHeld_ = false, setRamp(fail_, 0.0f);
        }
        return;
    }
    if (g.stomp != 0) return;
    if (isTap) {
        bypassed_ = !bypassed_;
        setRamp(bypass_, bypassed_ ? 1.0f : 0.0f);
    } else {
        warping_ = g.type == GestureType::Hold;
        setRamp(warp_, warping_ ? 1.0f : 0.0f);
    }
}

void OnceMore::process(const ProcessContext& ctx) {
    started_ = true;
    if (!buf_) {
        for (size_t i = 0; i < ctx.frames; ++i) ctx.out[i] = ctx.in[i];
        return;
    }
    const float d0       = static_cast<float>(d0_);
    const float maxDelay = static_cast<float>(size_ - 4);

    for (size_t i = 0; i < ctx.frames; ++i) {
        const float x  = ctx.in[i];
        const float kw = wowKnob_.next();
        const float kf = flutterKnob_.next();
        const float m  = mix_.next();
        const float am = flutterAm_.next();
        const float wp = warp_.next();
        const float b  = bypass_.next();
        const float al = align_.next();
        const float fk = failureKnob_.next(), fh = fail_.next();
        const float fe = fk + (1.0f - fk) * fh; // Fail pushes Failure to 1

        // Spec v0.14: transport faults at 0.5 f^2 a second, drags 6 f^1.5 %
        // deep, tape stops only above 0.85.
        if (snagEvents_.next(snagOn_ ? 0.5f * fe * fe : 0.0f, fs_, snagRng_) && !transport_.busy()) {
            const float u = snagRng_.uniform();
            if (fe > 0.85f && u < 0.1f) transport_.autoStop(3.0f * perfMs_);
            else if (u < 0.3f) transport_.wobble();
            else {
                const float u1 = snagRng_.uniform(), u2 = snagRng_.uniform();
                transport_.drag(fe * std::sqrt(fe), u1, u2);
            }
            flash_ = flashLen_;
        }
        const failure::Transport::Out tr = transport_.next();
        sat_.set(drive_.next(), bias_.next());

        // Warp pushes both knobs to the end of their travel.
        updateWow(kw + (1.0f - kw) * wp);
        updateFlutter(kf + (1.0f - kf) * wp);

        // Spec, Wow and Flutter: |wow| <= 2.5 ms and |flutter| <= 0.15 ms by
        // construction, so the read never passes the write head.
        const float swing = wowSwing_.next(wowStep_, wowRng_);
        wowSine_ += 0.5f * wowStep_;
        if (wowSine_ >= 1.0f) wowSine_ -= 1.0f;
        const float wow = wowAmp_ * ((1.0f - kWowSineShare) * swing + kWowSineShare * std::sin(kTwoPi * wowSine_));
        const float fl  = flutterSwing_.next(flutterStep_, flutterRng_);
        lastOffset_     = wow + flutterAmp_ * fl;

        const float head  = d0 + lastOffset_ + tr.wobble; // where the read head would be with no lag
        const float delay = std::min(std::max(head + tr.lag, 2.0f), maxDelay);
        lastLag_          = tr.lag;
        lastSpeed_        = tr.speed;
        // Flutter moves the level with the pitch (spec: 其他已決定).
        const float gain = 1.0f + kFlutterAmMax * am * flutterDepth_ * fl;
        float tape = readHermite(buf_, size_, write_, delay);
        if (tr.skip > 0.0f) // the restart skips its lag in one 20 ms crossfade
            tape += tr.skip * (readHermite(buf_, size_, write_, std::min(std::max(head, 2.0f), maxDelay)) - tape);
        tape *= gain;
        const failure::Crinkles::Out cr = crinkles_.next(crinkleOn_ ? 2.0f * fe * fe : 0.0f, crinkleRng_);
        if (cr.started) flash_ = flashLen_;
        tape *= cr.gain;
        // Exactly x(n - latency): the dry, aligned with the wet's filters.
        const float late = readHermite(buf_, size_, write_, static_cast<float>(latencySamples()));

        // Spec, signal flow 2, 4, 5: saturation, head loss, low edge.
        float s = sat_.process(tape);
        // Spec, noise: hiss goes onto the tape, before the head and bandwidth.
        const float hg = hissGain_.next();
        if (hg > 0.0f) s += hg * hiss_.next(hissRng_);
        s = (s + cr.noise) * tr.mute; // a stopped tape carries no sound, hiss included
        firPos_ = firPos_ == 0 ? firTaps_ - 1 : firPos_ - 1;
        firHist_[firPos_] = firHist_[firPos_ + firTaps_] = s;
        if (!fading_ && wanted_ != slotMachine_[active_]) startFade();
        float wet = hp_[active_].process(firDot(slotMachine_[active_]));
        if (fading_) {
            const float f    = fade_.next();
            const float next = hp_[1 - active_].process(firDot(slotMachine_[1 - active_]));
            wet += f * (next - wet);
            if (f >= 1.0f) {
                active_ = 1 - active_;
                fading_ = false;
            }
        }
        buf_[write_]     = x;
        if (++write_ == size_) write_ = 0;

        // Spec, Digital: sample-rate and bit reduction, bypassed (bit-exact)
        // when the amount is zero.
        const float g = digitalAmount_.next();
        const float lnFt = digitalLnFt_.next(), bits = digitalBits_.next();
        if (g > 0.0f) {
            const float lnFs = std::log(fs_);
            wet = digital_.process(wet, std::exp(lnFs + g * (lnFt - lnFs)), 16.0f - g * (16.0f - bits), digitalRng_);
        }

        // Spec, Dropout: a gain envelope on the whole wet, the top end first.
        const failure::Dropouts::Out dr = dropouts_.next(fe, dropOn_, dropRng_);
        if (dr.started) flash_ = flashLen_;
        dropLp_ = flushDenormal(dropLp_ + dr.lowpass * (wet - dropLp_));
        wet     = dropLp_ * dr.gain * tr.mute; // and silent after the filters' tails, too
        if (flash_ > 0) --flash_;

        // Spec, noise: hum and interference belong to the machine's circuits,
        // after everything the tape does.
        const float speed = 1.0f - (delay - lastDelay_); // pitch ratio of the read head
        lastDelay_        = delay;
        const float hu    = humGain_.next();
        if (hu > 0.0f) wet += hu * hum_.next();
        const float ig = interfGain_.next();
        if (ig > 0.0f) wet += ig * interference_.next(speed, interfRng_).sum();
        wet *= level_.next();

        // Spec, signal flow: dry_align delays the dry by the wet's latency.
        const float dry = al <= 0.0f ? x : al >= 1.0f ? late : x + al * (late - x);
        const float fx  = dry * (1.0f - m) + wet * m;
        ctx.out[i]      = b >= 1.0f ? x : fx + b * (x - fx);
    }

    // LED 1 swings 60-100% with the wow's pitch drift (pitch rises while the
    // delay shrinks).
    led1_ = wowK_ > 0.0f ? 0.8f - 0.2f * std::min(std::max(wowSwing_.slope(), -1.0f), 1.0f) : 1.0f;

    // LED 2 (spec): the tape speed while stopping or starting, on while Fail
    // is held, otherwise an 80 ms flash for every dropout, fault or crinkle.
    using S = failure::Transport::State;
    const S s = transport_.state();
    if (s == S::Stopping || s == S::Stopped || s == S::Starting) led2_ = std::min(std::max(lastSpeed_, 0.0f), 1.0f);
    else if (failHeld_) led2_ = 1.0f;
    else led2_ = flash_ > 0 ? 1.0f : 0.0f;
}

float OnceMore::firDot(int key) const {
    const float* hist = firHist_ + firPos_;
    if (key > kUnfiltered) return hist[firTaps_ / 2]; // same delay, no filter
    const float* h    = fir_ + static_cast<size_t>(key - 1) * static_cast<size_t>(firTaps_);
    float        acc  = 0.0f;
    for (int i = 0; i < firTaps_; ++i) acc += h[i] * hist[i];
    return acc;
}

LedState OnceMore::leds() const {
    LedState l;
    l.led[0] = bypassed_ ? 0.0f : led1_;
    l.led[1] = led2_;
    return l;
}

} // namespace tfx::fx
