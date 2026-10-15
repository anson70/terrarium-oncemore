#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include "audio_io.h"

#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace tfx::desktop {

Audio readWav(const std::string& path) {
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), nullptr))
        throw std::runtime_error("cannot open WAV file: " + path);
    const drwav_uint64 frames   = wav.totalPCMFrameCount;
    const unsigned     channels = wav.channels;
    std::vector<float> interleaved(static_cast<size_t>(frames) * channels);
    const drwav_uint64 got = drwav_read_pcm_frames_f32(&wav, frames, interleaved.data());
    Audio a;
    a.sampleRate = static_cast<float>(wav.sampleRate);
    drwav_uninit(&wav);
    a.samples.resize(static_cast<size_t>(got));
    for (size_t i = 0; i < a.samples.size(); ++i) a.samples[i] = interleaved[i * channels];
    return a;
}

void writeWav(const std::string& path, const std::vector<float>& samples, float sampleRate) {
    drwav_data_format fmt{};
    fmt.container     = drwav_container_riff;
    fmt.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels      = 1;
    fmt.sampleRate    = static_cast<drwav_uint32>(std::lround(sampleRate));
    fmt.bitsPerSample = 32;
    drwav wav;
    if (!drwav_init_file_write(&wav, path.c_str(), &fmt, nullptr))
        throw std::runtime_error("cannot write WAV file: " + path);
    const drwav_uint64 written = drwav_write_pcm_frames(&wav, samples.size(), samples.data());
    drwav_uninit(&wav);
    if (written != samples.size()) throw std::runtime_error("short write to WAV file: " + path);
}

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::stringstream        ss(s);
    std::string              item;
    while (std::getline(ss, item, sep)) parts.push_back(item);
    return parts;
}

double number(const std::string& text, const std::string& whole) {
    try {
        size_t used = 0;
        const double v = std::stod(text, &used);
        if (used != text.size()) throw std::invalid_argument("trailing");
        return v;
    } catch (...) {
        throw std::runtime_error("bad number '" + text + "' in signal '" + whole + "'");
    }
}

float dbToGain(double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); }

} // namespace

Audio makeSignal(const std::string& description, double seconds, float sampleRate) {
    if (!(seconds > 0.0)) throw std::runtime_error("signal duration must be > 0");
    Audio a;
    a.sampleRate = sampleRate;
    const size_t n = static_cast<size_t>(std::llround(seconds * sampleRate));
    a.samples.assign(n, 0.0f);

    const auto   parts = split(description, ':');
    const auto&  kind  = parts.empty() ? description : parts[0];
    const double twoPi = 6.283185307179586;

    if (kind == "silence" && parts.size() == 1) return a;
    if (kind == "impulse" && parts.size() == 1) {
        if (n) a.samples[0] = 1.0f;
        return a;
    }
    if (kind == "sine" && (parts.size() == 2 || parts.size() == 3)) {
        const double f = number(parts[1], description);
        const float  g = dbToGain(parts.size() == 3 ? number(parts[2], description) : -12.0);
        for (size_t i = 0; i < n; ++i)
            a.samples[i] = g * static_cast<float>(std::sin(twoPi * f * i / sampleRate));
        return a;
    }
    if (kind == "sweep" && (parts.size() == 3 || parts.size() == 4)) {
        const double f0 = number(parts[1], description), f1 = number(parts[2], description);
        if (!(f0 > 0.0 && f1 > 0.0)) throw std::runtime_error("sweep frequencies must be > 0");
        const float  g = dbToGain(parts.size() == 4 ? number(parts[3], description) : -12.0);
        const double k = std::log(f1 / f0);
        for (size_t i = 0; i < n; ++i) {
            const double t     = i / double(sampleRate);
            const double phase = twoPi * f0 * seconds / k * (std::exp(t / seconds * k) - 1.0);
            a.samples[i]       = g * static_cast<float>(std::sin(phase));
        }
        return a;
    }
    if (kind == "noise" && parts.size() <= 2) {
        const float g = dbToGain(parts.size() == 2 ? number(parts[1], description) : -24.0);
        // xorshift32 with a fixed seed: identical on every platform and compiler.
        uint32_t s = 0x12345678u;
        for (size_t i = 0; i < n; ++i) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            a.samples[i] = g * (static_cast<float>(s) / 2147483648.0f - 1.0f);
        }
        return a;
    }
    throw std::runtime_error("unknown signal '" + description +
                             "' (impulse, silence, sine:F[:dB], sweep:F0:F1[:dB], noise[:dB])");
}

} // namespace tfx::desktop
