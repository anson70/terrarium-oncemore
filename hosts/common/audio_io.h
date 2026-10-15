#pragma once
// WAV files and built-in test signals for the offline host.

#include <string>
#include <vector>

namespace tfx::desktop {

struct Audio {
    std::vector<float> samples; // mono
    float              sampleRate = 48000.0f;
};

// Reads channel 0 of any PCM or float WAV (the Terrarium input is mono).
// Throws std::runtime_error on failure.
Audio readWav(const std::string& path);

// Writes 32-bit float mono. Throws std::runtime_error on failure.
void writeWav(const std::string& path, const std::vector<float>& samples, float sampleRate);

// Built-in signals, described as text so they can be given on the command line:
//   impulse            one sample at full scale, then silence
//   silence
//   sine:FREQ[:DBFS]   default -12 dBFS
//   sweep:F0:F1[:DBFS] exponential sweep over the whole duration, default -12 dBFS
//   noise[:DBFS]       white noise from a fixed seed, default -24 dBFS
// Throws std::runtime_error on an unknown description.
Audio makeSignal(const std::string& description, double seconds, float sampleRate);

} // namespace tfx::desktop
