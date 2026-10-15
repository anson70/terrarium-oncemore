// The Daisy host: runs one effect on the PedalPCB Terrarium.
//
// The only code in the repository that includes libDaisy. It turns the
// Terrarium's pots, toggles and footswitches into a tfx::PanelState, hands it
// to the same PanelDriver every host uses, and reports what it sees (and how
// much CPU the effect takes) over USB serial, so a new board or a new effect
// can be checked without a debugger.
//
// Control timing: the audio callback runs every 48 samples (1 ms at 48 kHz)
// and reads the controls at its start, so knobs, debouncing and gestures all
// tick at exactly 1 kHz, the same block boundaries the offline host uses.

#include <cstdint>
#include <cstring>

#include "daisy_seed.h"
#include "tfx/panel.h"
#include TFX_EFFECT_HEADER

using namespace daisy;

namespace {

constexpr float  kSampleRate = 48000.0f;
constexpr size_t kBlock      = 48;

// --- Terrarium wiring -------------------------------------------------------
// From libDaisy's Petal pin table (daisy_petal.cpp, MIT) combined with the
// index map in PedalPCB's terrarium.h: pots 1-6 are A1-A6 (D16-D21), toggles
// 1-4 are D10, D9, D8, D7, footswitches D25 and D26, LEDs D22 and D23.
// Pots 1-3 are the top row, 4-6 the bottom row.
constexpr Pin kKnobPins[tfx::PanelState::kKnobs]     = {seed::D16, seed::D17, seed::D18,
                                                        seed::D19, seed::D20, seed::D21};
constexpr Pin kTogglePins[tfx::PanelState::kToggles] = {seed::D10, seed::D9, seed::D8, seed::D7};
constexpr Pin kStompPins[tfx::PanelState::kStomps]   = {seed::D25, seed::D26};
constexpr Pin kLedPins[2]                            = {seed::D22, seed::D23};

// To be confirmed with the panel test (hosts/daisy/README.md): whether
// clockwise reads as 1, and whether a closed toggle is "up".
constexpr bool kKnobsInverted   = false;
constexpr bool kClosedToggleIsUp = true;

constexpr int   kDebounceMs   = 5;     // a switch must read the same for this long
constexpr float kKnobSmooth   = 0.08f; // one-pole per 1 ms block: ~12 ms, hides ADC noise
constexpr float kKnobEndZone  = 0.01f; // pots rarely reach the rails exactly

// --- Hardware -----------------------------------------------------------------
DaisySeed    hw;
GPIO         toggleIn[tfx::PanelState::kToggles];
GPIO         stompIn[tfx::PanelState::kStomps];
GPIO         ledOut[2];
CpuLoadMeter load;

// --- Large buffers: SDRAM, handed out once in prepare() -----------------------
// The linker places this in the 64 MB SDRAM; NOLOAD, so it starts as garbage
// and every allocation is cleared here, as the Allocator contract promises.
constexpr size_t kArenaBytes = 48u * 1024u * 1024u;
DSY_SDRAM_BSS alignas(16) uint8_t arena[kArenaBytes];

class SdramAllocator final : public tfx::Allocator {
public:
    void* allocate(size_t bytes) override {
        const size_t rounded = (bytes + 15u) & ~size_t{15};
        if (rounded > kArenaBytes - used_) return nullptr;
        uint8_t* p = arena + used_;
        std::memset(p, 0, rounded);
        used_ += rounded;
        return p;
    }
    size_t used() const { return used_; }

private:
    size_t used_ = 0;
};
SdramAllocator sdram;

// --- Effect and controls --------------------------------------------------------
TFX_EFFECT_CLASS effect;
tfx::PanelDriver driver;
tfx::PanelState  panel;
uint64_t         now = 0; // samples since start

struct Debounced {
    bool state = false;
    int  count = 0;
    bool update(bool raw) {
        if (raw == state) count = 0;
        else if (++count >= kDebounceMs) state = raw, count = 0;
        return state;
    }
};
Debounced toggleDb[tfx::PanelState::kToggles];
Debounced stompDb[tfx::PanelState::kStomps];
float     knobRaw[tfx::PanelState::kKnobs];

// Gestures go from the audio callback to the main loop for printing.
struct GestureRing {
    static constexpr uint32_t kSize = 16;
    tfx::Gesture              items[kSize];
    volatile uint32_t         head = 0, tail = 0;
    void push(const tfx::Gesture& g) {
        if (head - tail < kSize) items[head % kSize] = g, head = head + 1;
    }
    bool pop(tfx::Gesture& g) {
        if (tail == head) return false;
        g = items[tail % kSize], tail = tail + 1;
        return true;
    }
};
GestureRing gestures;

class Observer final : public tfx::PanelObserver {
public:
    void onParam(const tfx::ParamSpec&, float, uint64_t) override {}
    void onGesture(const tfx::Gesture& g, uint64_t) override { gestures.push(g); }
};
Observer observer;

// What the main loop shows; written by the audio callback only.
volatile float ledLevel[2];

float knobValue(size_t k) {
    float v = hw.adc.GetFloat(static_cast<uint8_t>(k));
    if (kKnobsInverted) v = 1.0f - v;
    v = (v - kKnobEndZone) / (1.0f - 2.0f * kKnobEndZone);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void readPanel(bool first) {
    for (size_t k = 0; k < tfx::PanelState::kKnobs; ++k) {
        const float v = knobValue(k);
        knobRaw[k]    = first ? v : knobRaw[k] + kKnobSmooth * (v - knobRaw[k]);
        // Land exactly on the ends, so a knob turned fully reaches min/max.
        panel.knobs[k] = knobRaw[k] < 0.001f ? 0.0f : (knobRaw[k] > 0.999f ? 1.0f : knobRaw[k]);
    }
    // Switches pull up: a closed contact reads low.
    for (size_t t = 0; t < tfx::PanelState::kToggles; ++t) {
        const bool closed = !toggleIn[t].Read();
        if (first) toggleDb[t].state = closed;
        panel.toggles[t] = toggleDb[t].update(closed) == kClosedToggleIsUp;
    }
    for (size_t s = 0; s < tfx::PanelState::kStomps; ++s) {
        const bool closed = !stompIn[s].Read();
        if (first) stompDb[s].state = closed;
        panel.stomps[s] = stompDb[s].update(closed);
    }
}

void audioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    load.OnBlockStart();
    readPanel(false);
    driver.apply(panel, now);
    // The Terrarium is mono: left in, left out; the right output gets a copy.
    effect.process({in[0], out[0], size});
    std::memcpy(out[1], out[0], size * sizeof(float));
    now += size;
    const tfx::LedState l = effect.leds();
    ledLevel[0]           = l.led[0];
    ledLevel[1]           = l.led[1];
    load.OnBlockEnd();
}

const char* boardName() {
    switch (hw.CheckBoardVersion()) {
        case DaisySeed::BoardVersion::DAISY_SEED_1_1:   return "Daisy Seed 1.1 (WM8731)";
        case DaisySeed::BoardVersion::DAISY_SEED_1_2:   return "Daisy Seed 1.2 (PCM3060)";
        case DaisySeed::BoardVersion::DAISY_SEED_2_DFM: return "Daisy Seed 2 DFM (PCM3060)";
        case DaisySeed::BoardVersion::DAISY_SEED_3:     return "Daisy Seed3 (TAC5242)";
        case DaisySeed::BoardVersion::DAISY_SEED:
        default:                                        return "Daisy Seed rev4 (AK4556)";
    }
}

const char* gestureLabel(tfx::GestureType t) {
    switch (t) {
        case tfx::GestureType::Tap:       return "tap";
        case tfx::GestureType::DoubleTap: return "double tap";
        case tfx::GestureType::Hold:      return "hold";
        case tfx::GestureType::Release:   return "release";
    }
    return "?";
}

int percent10(float x) { return static_cast<int>(x * 1000.0f + 0.5f); } // 0.123 -> 123 (12.3%)

} // namespace

int main() {
    hw.Init(true); // 480 MHz
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetAudioBlockSize(kBlock);
    hw.StartLog(false); // USB serial; never waits for a terminal
    // Detection re-initialises GPIOs, so do it once, before anything runs.
    const char* const board = boardName();

    AdcChannelConfig adc[tfx::PanelState::kKnobs];
    for (size_t k = 0; k < tfx::PanelState::kKnobs; ++k) adc[k].InitSingle(kKnobPins[k]);
    hw.adc.Init(adc, tfx::PanelState::kKnobs);
    hw.adc.Start();
    for (size_t t = 0; t < tfx::PanelState::kToggles; ++t)
        toggleIn[t].Init(kTogglePins[t], GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    for (size_t s = 0; s < tfx::PanelState::kStomps; ++s)
        stompIn[s].Init(kStompPins[s], GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    for (size_t i = 0; i < 2; ++i) ledOut[i].Init(kLedPins[i], GPIO::Mode::OUTPUT);

    System::Delay(20); // let the ADC fill its buffers
    readPanel(true);

    // Same order as every host: bind, prepare, push the starting panel (so
    // parameters snap), then run.
    driver.bind(&effect, &observer);
    driver.prepare(kSampleRate);
    effect.prepare({kSampleRate, kBlock, &sdram});
    driver.sync(panel, 0);

    load.Init(kSampleRate, kBlock);
    hw.StartAudio(audioCallback);

    uint32_t lastReport = 0, lastBanner = 0;
    float    shownKnob[tfx::PanelState::kKnobs];
    bool     shownToggle[tfx::PanelState::kToggles];
    bool     shownStomp[tfx::PanelState::kStomps];
    for (size_t k = 0; k < tfx::PanelState::kKnobs; ++k) shownKnob[k] = -1.0f;
    for (size_t t = 0; t < tfx::PanelState::kToggles; ++t) shownToggle[t] = !panel.toggles[t];
    for (size_t s = 0; s < tfx::PanelState::kStomps; ++s) shownStomp[s] = panel.stomps[s];

    for (;;) {
        // The audio interrupt changes panel and ledLevel behind the loop's
        // back; make the compiler read them again on every pass.
        asm volatile("" ::: "memory");
        // LEDs: software PWM at 500 Hz, squared for a more even brightness ramp.
        const uint32_t us = System::GetUs();
        for (size_t i = 0; i < 2; ++i) {
            const float b = ledLevel[i];
            ledOut[i].Write(static_cast<float>(us % 2000u) < b * b * 2000.0f);
        }

        const uint32_t ms = System::GetNow();
        // The banner repeats every 10 s, so a terminal opened late still sees it.
        if (ms - lastBanner >= 10000 || lastBanner == 0) {
            lastBanner = ms ? ms : 1;
            hw.PrintLine("terrarium-fx %s (%s) on %s, SDRAM used %u KB", TFX_EFFECT_NAME, TFX_VERSION, board,
                         static_cast<unsigned>(sdram.used() / 1024));
        }
        if (ms - lastReport >= 2000) {
            lastReport = ms;
            const int avg = percent10(load.GetAvgCpuLoad()), max = percent10(load.GetMaxCpuLoad());
            hw.PrintLine("cpu avg %d.%d%%  max %d.%d%%", avg / 10, avg % 10, max / 10, max % 10);
            load.Reset();
        }
        // Panel changes, for the panel test.
        for (size_t k = 0; k < tfx::PanelState::kKnobs; ++k) {
            const float v = panel.knobs[k];
            if (v - shownKnob[k] > 0.02f || shownKnob[k] - v > 0.02f) {
                shownKnob[k] = v;
                hw.PrintLine("knob %u: %d", static_cast<unsigned>(k + 1), static_cast<int>(v * 100.0f + 0.5f));
            }
        }
        for (size_t t = 0; t < tfx::PanelState::kToggles; ++t)
            if (panel.toggles[t] != shownToggle[t]) {
                shownToggle[t] = panel.toggles[t];
                hw.PrintLine("toggle %u: %s", static_cast<unsigned>(t + 1), panel.toggles[t] ? "up" : "down");
            }
        for (size_t s = 0; s < tfx::PanelState::kStomps; ++s)
            if (panel.stomps[s] != shownStomp[s]) {
                shownStomp[s] = panel.stomps[s];
                hw.PrintLine("footswitch %u: %s", static_cast<unsigned>(s + 1), panel.stomps[s] ? "down" : "up");
            }
        tfx::Gesture g;
        while (gestures.pop(g))
            hw.PrintLine("footswitch %u: %s", static_cast<unsigned>(g.stomp + 1), gestureLabel(g.type));
    }
}
