#include "engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>

#include "alloc_guard.h"
#include "heap_allocator.h"
#include "tfx/panel.h"

namespace tfx::offline {

using namespace tfx::desktop;

namespace {

class Recorder final : public PanelObserver {
public:
    Recorder(RunResult& r, float sr) : r_(r), sr_(sr) {}
    void onParam(const ParamSpec& spec, float normalized, uint64_t now) override {
        r_.params.push_back({now / sr_, spec.key, normalized, toReal(spec, normalized)});
    }
    void onGesture(const Gesture& g, uint64_t now) override {
        r_.gestures.push_back({now / sr_, g.stomp + 1, gestureName(g.type)});
    }

private:
    RunResult& r_;
    double     sr_;
};

} // namespace

RunResult run(Effect& effect, const std::vector<float>& input, const ControlScript& script,
              const RunOptions& opt) {
    if (!(opt.sampleRate >= 8000.0f && opt.sampleRate <= 384000.0f))
        throw std::runtime_error("sample rate must be within 8 kHz..384 kHz");
    if (opt.blocks.minBlock < 1 || opt.blocks.minBlock > opt.blocks.maxBlock)
        throw std::runtime_error("block sizes must satisfy 1 <= min <= max");

    const double sr = opt.sampleRate;
    RunResult    result;
    result.output.assign(input.size(), 0.0f);

    for (const auto& ev : script.paramEvents())
        if (!effect.params().find(ev.key.c_str()))
            throw std::runtime_error("control script: unknown param '" + ev.key + "' for effect " +
                                     effect.name());

    // Warn about stomp presses too short to be seen at this block size.
    const double maxBlockSec = opt.blocks.maxBlock / sr;
    for (uint8_t s = 0; s < PanelState::kStomps; ++s) {
        const auto times = script.stompTransitionTimes(s);
        for (size_t i = 1; i < times.size(); ++i)
            if (times[i] - times[i - 1] < maxBlockSec) {
                std::ostringstream w;
                w << "stomp " << int(s + 1) << " changes twice within " << (times[i] - times[i - 1]) * 1000.0
                  << " ms near t=" << times[i - 1] << " s; with blocks up to " << maxBlockSec * 1000.0
                  << " ms the change may be missed";
                result.warnings.push_back(w.str());
            }
    }
    if (script.endTime() > input.size() / sr) {
        std::ostringstream w;
        w << "control script runs to t=" << script.endTime() << " s but the input is only "
          << input.size() / sr << " s long";
        result.warnings.push_back(w.str());
    }

    HeapAllocator heap;
    effect.prepare({opt.sampleRate, opt.blocks.maxBlock, &heap});
    effect.setSeed(opt.seed);

    Recorder    recorder(result, opt.sampleRate);
    PanelDriver driver;
    driver.bind(&effect, &recorder);
    driver.prepare(opt.sampleRate);
    driver.sync(script.panelAt(0.0), 0);

    // Block sizes come from their own generator, derived from the seed, so the
    // effect's randomness and the block pattern never influence each other.
    std::mt19937 blockRng(opt.seed ^ 0x9E3779B9u);
    std::uniform_int_distribution<size_t> blockDist(opt.blocks.minBlock, opt.blocks.maxBlock);

    const auto& paramEvents = script.paramEvents();
    size_t      nextParam   = 0;
    LedState    lastLed{-1.0f, -1.0f};
    double      sumUs = 0.0, sumSq = 0.0;
    result.minBlockUsed = opt.blocks.maxBlock;
    allocGuardReset();

    size_t pos = 0;
    while (pos < input.size()) {
        const size_t want = opt.blocks.minBlock == opt.blocks.maxBlock ? opt.blocks.minBlock
                                                                      : blockDist(blockRng);
        const size_t n  = std::min(want, input.size() - pos);
        const double t0 = pos / sr;

        driver.apply(script.panelAt(t0), pos);
        while (nextParam < paramEvents.size() && paramEvents[nextParam].t <= t0) {
            driver.setParamByKey(paramEvents[nextParam].key.c_str(), paramEvents[nextParam].normalized, pos);
            ++nextParam;
        }

        const ProcessContext ctx{input.data() + pos, result.output.data() + pos, n};
        const auto start = std::chrono::steady_clock::now();
        allocGuardArm();
        effect.process(ctx);
        allocGuardDisarm();
        const double us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        sumUs += us;
        result.maxProcessUs = std::max(result.maxProcessUs, us);

        const LedState led = effect.leds();
        if (std::fabs(led.led[0] - lastLed.led[0]) >= 1.0f / 255.0f ||
            std::fabs(led.led[1] - lastLed.led[1]) >= 1.0f / 255.0f) {
            result.leds.push_back({(pos + n) / sr, led.led[0], led.led[1]});
            lastLed = led;
        }

        ++result.blocks;
        result.minBlockUsed = std::min(result.minBlockUsed, n);
        result.maxBlockUsed = std::max(result.maxBlockUsed, n);
        pos += n;
    }

    result.allocationsInProcess = allocGuardCount();
    result.meanProcessUs        = result.blocks ? sumUs / result.blocks : 0.0;
    if (result.blocks == 0) result.minBlockUsed = 0;

    for (float x : result.output) {
        if (std::isnan(x)) { ++result.nans; continue; }
        if (std::isinf(x)) { ++result.infs; continue; }
        const float a = std::fabs(x);
        result.peak = std::max(result.peak, a);
        if (a > 1.0f) ++result.clipped;
        sumSq += double(x) * x;
    }
    result.rms = result.output.empty() ? 0.0 : std::sqrt(sumSq / result.output.size());
    return result;
}

} // namespace tfx::offline
