// Every registered effect explains itself to the desktop hosts.

#include "doctest.h"

#include <string>

#include "registry.h"

using namespace tfx;

TEST_CASE("every effect and every parameter has help text for fx-live and fx-run") {
    for (const std::string& name : effectNames()) {
        CAPTURE(name);
        const auto fx = createEffect(name);
        REQUIRE(fx);
        CHECK(fx->help() != nullptr);
        const ParamTable& t = fx->params();
        for (size_t i = 0; i < t.count; ++i) {
            CAPTURE(t.specs[i].key);
            REQUIRE(t.specs[i].help != nullptr);
            CHECK(std::string(t.specs[i].help).size() > 20);
        }
    }
}
