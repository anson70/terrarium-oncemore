// fx-run: the offline host command line.
//
//   fx-run <effect> (--in FILE.wav | --signal DESC [--duration S]) [options]
//
// Exit codes: 0 ok, 1 usage or I/O error, 2 golden comparison failed,
//             3 real-time rule broken (allocation in process(), NaN or Inf output).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "audio_io.h"
#include "engine.h"
#include "registry.h"
#include "report.h"
#include "script.h"

using namespace tfx;
using namespace tfx::offline;
using namespace tfx::desktop;

namespace {

void usage() {
    std::cerr <<
R"(usage: fx-run <effect> (--in FILE.wav | --signal DESC) [options]

input
  --in FILE.wav           channel 0 of a WAV file; its rate is used unless --sr is given
  --signal DESC           impulse | silence | sine:F[:dB] | sweep:F0:F1[:dB] | noise[:dB]
  --duration S            length of a --signal, seconds (default 2)
  --tail S                append S seconds of silence after the input (default 0)

run
  --sr HZ                 sample rate (default 48000, or the input file's rate)
  --block N | MIN-MAX     fixed block size, or random sizes in a range (default 48)
  --seed N                seed for the effect and for random block sizes (default 1)
  --script FILE.yaml      control script (default: panel at rest, no events)

output
  --out FILE.wav          32-bit float mono
  --report FILE.json      report; "-" for stdout
  --no-timing             leave desktop timing out of the report (for golden reports)

checks
  --compare GOLDEN.wav    compare the output with a reference
  --exact                 require bit-identical output (default: residual below threshold)
  --threshold DB          residual limit in dBFS (default -100)

info
  --list                  list effects
  --params                print the effect's parameter table
)";
}

[[noreturn]] void die(const std::string& msg) {
    std::cerr << "fx-run: " << msg << "\n";
    std::exit(1);
}

const char* controlName(Control c) {
    static const char* names[] = {"-",       "knob1",   "knob2",   "knob3",   "knob4",  "knob5",
                                  "knob6",   "toggle1", "toggle2", "toggle3", "toggle4"};
    return names[static_cast<int>(c)];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    std::string effectName, inPath, signal, scriptPath, outPath, reportPath, goldenPath;
    double      duration = 2.0, tail = 0.0, threshold = -100.0;
    float       sr = 0.0f;
    BlockPlan   blocks;
    uint32_t    seed = 1;
    bool        exact = false, timing = true, list = false, showParams = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) die("missing value after " + a);
            return argv[++i];
        };
        try {
            if      (a == "--in")        inPath     = value();
            else if (a == "--signal")    signal     = value();
            else if (a == "--duration")  duration   = std::stod(value());
            else if (a == "--tail")      tail       = std::stod(value());
            else if (a == "--sr")        sr         = std::stof(value());
            else if (a == "--seed")      seed       = static_cast<uint32_t>(std::stoul(value()));
            else if (a == "--script")    scriptPath = value();
            else if (a == "--out")       outPath    = value();
            else if (a == "--report")    reportPath = value();
            else if (a == "--compare")   goldenPath = value();
            else if (a == "--threshold") threshold  = std::stod(value());
            else if (a == "--exact")     exact      = true;
            else if (a == "--no-timing") timing     = false;
            else if (a == "--list")      list       = true;
            else if (a == "--params")    showParams = true;
            else if (a == "--block") {
                const std::string v    = value();
                const auto        dash = v.find('-');
                if (dash == std::string::npos) {
                    blocks.minBlock = blocks.maxBlock = std::stoul(v);
                } else {
                    blocks.minBlock = std::stoul(v.substr(0, dash));
                    blocks.maxBlock = std::stoul(v.substr(dash + 1));
                }
            }
            else if (a == "-h" || a == "--help") { usage(); return 0; }
            else if (!a.empty() && a[0] == '-') die("unknown option " + a);
            else if (effectName.empty()) effectName = a;
            else die("unexpected argument " + a);
        } catch (const std::invalid_argument&) {
            die("bad number for " + a);
        } catch (const std::out_of_range&) {
            die("number out of range for " + a);
        }
    }

    if (list) {
        for (const auto& n : effectNames()) std::cout << n << "\n";
        return 0;
    }
    if (effectName.empty()) { usage(); return 1; }
    auto effect = createEffect(effectName);
    if (!effect) die("unknown effect '" + effectName + "' (try --list)");

    if (showParams) {
        const ParamTable& t = effect->params();
        std::printf("%-14s %-16s %10s %10s %-6s %-8s %8s %7s %s\n", "key", "name", "min", "max", "unit",
                    "curve", "default", "smooth", "control");
        for (size_t i = 0; i < t.count; ++i) {
            const ParamSpec& s = t.specs[i];
            const char* curve = s.curve == Curve::Exp ? "exp" : s.curve == Curve::Stepped ? "stepped" : "linear";
            std::printf("%-14s %-16s %10g %10g %-6s %-8s %8g %5gms %s\n", s.key, s.name, s.min, s.max, s.unit,
                        curve, toReal(s, s.defaultNorm), s.smoothingMs, controlName(s.control));
            if (s.help) std::printf("%14s %s\n", "", s.help);
        }
        if (effect->help()) std::printf("\n%s\n", effect->help());
        return 0;
    }

    if (inPath.empty() == signal.empty()) die("give exactly one of --in or --signal");

    try {
        Audio input;
        if (!inPath.empty()) {
            input = readWav(inPath);
            if (sr > 0.0f && std::fabs(sr - input.sampleRate) > 0.5f)
                die("input is " + std::to_string(int(input.sampleRate)) + " Hz but --sr is " +
                    std::to_string(int(sr)) + " Hz; resampling is not supported yet (M2)");
        } else {
            input = makeSignal(signal, duration, sr > 0.0f ? sr : 48000.0f);
        }
        if (tail > 0.0) input.samples.resize(input.samples.size() + size_t(tail * input.sampleRate), 0.0f);
        if (input.samples.empty()) die("input is empty");

        const ControlScript script =
            scriptPath.empty() ? ControlScript::empty() : ControlScript::fromYamlFile(scriptPath);

        RunOptions opt;
        opt.sampleRate = input.sampleRate;
        opt.blocks     = blocks;
        opt.seed       = seed;
        const RunResult result = run(*effect, input.samples, script, opt);

        if (!outPath.empty()) writeWav(outPath, result.output, input.sampleRate);

        std::optional<Comparison> cmp;
        if (!goldenPath.empty()) {
            const Audio golden = readWav(goldenPath);
            if (std::fabs(golden.sampleRate - input.sampleRate) > 0.5f)
                die("golden file sample rate differs from the run");
            cmp = compare(result.output, golden.samples, threshold, exact);
            cmp->golden = goldenPath;
        }

        if (!reportPath.empty()) {
            ReportInfo info;
            info.effect        = effectName;
            info.input         = inPath.empty() ? signal : inPath;
            info.script        = scriptPath;
            info.sampleRate    = input.sampleRate;
            info.blocks        = blocks;
            info.seed          = seed;
            info.includeTiming = timing;
            const std::string json = reportJson(info, result, cmp);
            if (reportPath == "-") {
                std::cout << json;
            } else {
                std::ofstream f(reportPath, std::ios::binary);
                if (!f) die("cannot write report: " + reportPath);
                f << json;
            }
        }

        for (const auto& w : result.warnings) std::cerr << "fx-run: warning: " << w << "\n";

        int status = 0;
        if (result.allocationsInProcess > 0) {
            std::cerr << "fx-run: RULE BROKEN: " << result.allocationsInProcess
                      << " heap allocation(s) inside process()\n";
            status = 3;
        }
        if (result.nans || result.infs) {
            std::cerr << "fx-run: RULE BROKEN: output contains " << result.nans << " NaN and " << result.infs
                      << " Inf sample(s)\n";
            status = 3;
        }
        if (status) return status;
        if (cmp && !cmp->pass) {
            std::cerr << "fx-run: comparison FAILED: "
                      << (cmp->lengthMatches ? "" : "length differs; ")
                      << "max error " << cmp->maxAbsError << ", residual " << cmp->residualDbfs << " dBFS\n";
            return 2;
        }
        return 0;
    } catch (const std::exception& e) {
        die(e.what());
    }
}
