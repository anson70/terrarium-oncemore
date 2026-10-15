#pragma once
// Every effect the desktop hosts can load, by name.
// Generated from each effect's effect.cmake (effects/registry.cpp.in);
// TFX_EFFECTS chooses which ones are built.

#include <memory>
#include <string>
#include <vector>

#include "tfx/effect.h"

namespace tfx {

std::unique_ptr<Effect>  createEffect(const std::string& name); // nullptr if unknown
std::vector<std::string> effectNames();

} // namespace tfx
