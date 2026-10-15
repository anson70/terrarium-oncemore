#include "report.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sstream>

namespace tfx::offline {

Comparison compare(const std::vector<float>& output, const std::vector<float>& golden,
                   double thresholdDbfs, bool requireExact) {
    Comparison c;
    c.outputLength  = output.size();
    c.goldenLength  = golden.size();
    c.lengthMatches = output.size() == golden.size();
    c.thresholdDbfs = thresholdDbfs;
    c.requireExact  = requireExact;

    const size_t n     = std::min(output.size(), golden.size());
    double       sumSq = 0.0;
    bool         exact = c.lengthMatches;
    for (size_t i = 0; i < n; ++i) {
        // Compare bit patterns so NaN == NaN counts as identical and -0 != +0
        // is not hidden; a bit-exact check means bit-exact.
        uint32_t a, b;
        std::memcpy(&a, &output[i], 4);
        std::memcpy(&b, &golden[i], 4);
        if (a != b) exact = false;
        const double d = double(output[i]) - double(golden[i]);
        if (std::isfinite(d)) {
            c.maxAbsError = std::max(c.maxAbsError, std::fabs(d));
            sumSq += d * d;
        } else if (a != b) {
            c.maxAbsError = INFINITY;
        }
    }
    c.exact        = exact;
    const double r = n ? std::sqrt(sumSq / n) : 0.0;
    c.residualDbfs = r > 0.0 ? 20.0 * std::log10(r) : -1000.0;
    c.pass         = c.lengthMatches && (requireExact ? exact : c.residualDbfs <= thresholdDbfs &&
                                                                 std::isfinite(c.maxAbsError));
    return c;
}

namespace {

std::string esc(const std::string& s) {
    std::string out;
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", ch);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

// Fixed formatting so identical runs produce identical report text.
std::string num(double v, int digits = 6) {
    if (std::isnan(v)) return "null";
    if (std::isinf(v)) return v > 0 ? "1e999" : "-1e999";
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*g", digits, v);
    return buf;
}

double dbfs(double x) { return x > 0.0 ? 20.0 * std::log10(x) : -1000.0; }

} // namespace

std::string reportJson(const ReportInfo& info, const RunResult& r,
                       const std::optional<Comparison>& cmp) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"effect\": \"" << esc(info.effect) << "\",\n";
    o << "  \"input\": \"" << esc(info.input) << "\",\n";
    o << "  \"script\": " << (info.script.empty() ? "null" : "\"" + esc(info.script) + "\"") << ",\n";
    o << "  \"sampleRate\": " << num(info.sampleRate) << ",\n";
    o << "  \"seed\": " << info.seed << ",\n";
    o << "  \"blocks\": { \"planMin\": " << info.blocks.minBlock << ", \"planMax\": " << info.blocks.maxBlock
      << ", \"count\": " << r.blocks << ", \"minUsed\": " << r.minBlockUsed << ", \"maxUsed\": "
      << r.maxBlockUsed << " },\n";
    o << "  \"frames\": " << r.output.size() << ",\n";
    o << "  \"output\": { \"peak\": " << num(r.peak) << ", \"peakDbfs\": " << num(dbfs(r.peak), 4)
      << ", \"rms\": " << num(r.rms) << ", \"rmsDbfs\": " << num(dbfs(r.rms), 4) << ", \"clipped\": "
      << r.clipped << ", \"nan\": " << r.nans << ", \"inf\": " << r.infs << " },\n";
    o << "  \"allocationsInProcess\": " << r.allocationsInProcess << ",\n";

    o << "  \"params\": [";
    for (size_t i = 0; i < r.params.size(); ++i) {
        const auto& p = r.params[i];
        o << (i ? ",\n" : "\n") << "    { \"t\": " << num(p.t) << ", \"key\": \"" << esc(p.key)
          << "\", \"normalized\": " << num(p.normalized) << ", \"real\": " << num(p.real) << " }";
    }
    o << (r.params.empty() ? "],\n" : "\n  ],\n");

    o << "  \"gestures\": [";
    for (size_t i = 0; i < r.gestures.size(); ++i) {
        const auto& g = r.gestures[i];
        o << (i ? ",\n" : "\n") << "    { \"t\": " << num(g.t) << ", \"stomp\": " << g.stomp
          << ", \"type\": \"" << g.type << "\" }";
    }
    o << (r.gestures.empty() ? "],\n" : "\n  ],\n");

    o << "  \"leds\": [";
    for (size_t i = 0; i < r.leds.size(); ++i) {
        const auto& l = r.leds[i];
        o << (i ? ",\n" : "\n") << "    { \"t\": " << num(l.t) << ", \"led1\": " << num(l.led1, 4)
          << ", \"led2\": " << num(l.led2, 4) << " }";
    }
    o << (r.leds.empty() ? "],\n" : "\n  ],\n");

    o << "  \"warnings\": [";
    for (size_t i = 0; i < r.warnings.size(); ++i)
        o << (i ? ",\n" : "\n") << "    \"" << esc(r.warnings[i]) << "\"";
    o << (r.warnings.empty() ? "]" : "\n  ]");

    if (cmp) {
        o << ",\n  \"compare\": { \"golden\": \"" << esc(cmp->golden) << "\", \"lengthMatches\": "
          << (cmp->lengthMatches ? "true" : "false") << ", \"outputLength\": " << cmp->outputLength
          << ", \"goldenLength\": " << cmp->goldenLength << ", \"exact\": " << (cmp->exact ? "true" : "false")
          << ", \"maxAbsError\": " << num(cmp->maxAbsError) << ", \"residualDbfs\": "
          << num(cmp->residualDbfs, 4) << ", \"requireExact\": " << (cmp->requireExact ? "true" : "false")
          << ", \"thresholdDbfs\": " << num(cmp->thresholdDbfs, 4) << ", \"pass\": "
          << (cmp->pass ? "true" : "false") << " }";
    }

    if (info.includeTiming) {
        const double budgetUs = 1e6 * info.blocks.maxBlock / info.sampleRate;
        o << ",\n  \"desktopTiming\": { \"note\": \"desktop only; not representative of Daisy\", "
          << "\"meanProcessUs\": " << num(r.meanProcessUs, 4) << ", \"maxProcessUs\": "
          << num(r.maxProcessUs, 4) << ", \"budgetUsAtMaxBlock\": " << num(budgetUs, 4) << " }";
    }
    o << "\n}\n";
    return o.str();
}

} // namespace tfx::offline
