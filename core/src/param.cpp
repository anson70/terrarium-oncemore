#include "tfx/param.h"

#include <algorithm>
#include <cstring>

namespace tfx {

const ParamSpec* ParamTable::find(const char* key) const {
    for (size_t i = 0; i < count; ++i)
        if (std::strcmp(specs[i].key, key) == 0) return &specs[i];
    return nullptr;
}

const ParamSpec* ParamTable::byId(ParamId id) const {
    for (size_t i = 0; i < count; ++i)
        if (specs[i].id == id) return &specs[i];
    return nullptr;
}

float toReal(const ParamSpec& spec, float normalized) {
    const float n = std::clamp(normalized, 0.0f, 1.0f);
    switch (spec.curve) {
        case Curve::Exp:
            return spec.min * std::pow(spec.max / spec.min, n);
        case Curve::Stepped:
            return std::round(spec.min + n * (spec.max - spec.min));
        case Curve::Linear:
        default:
            return spec.min + n * (spec.max - spec.min);
    }
}

float toNormalized(const ParamSpec& spec, float real) {
    float n = 0.0f;
    switch (spec.curve) {
        case Curve::Exp:
            n = std::log(real / spec.min) / std::log(spec.max / spec.min);
            break;
        case Curve::Stepped:
        case Curve::Linear:
        default:
            n = (real - spec.min) / (spec.max - spec.min);
            break;
    }
    return std::clamp(n, 0.0f, 1.0f);
}

void Smoother::prepare(float sampleRate, float timeMs, float initial) {
    // Time constant: reaches ~63% of a step after timeMs.
    coeff_   = timeMs <= 0.0f ? 1.0f : 1.0f - std::exp(-1000.0f / (timeMs * sampleRate));
    current_ = target_ = initial;
}

} // namespace tfx
