#include <catch2/catch_test_macros.hpp>

#include "dsp/Info.h"

// Test du squelette : vérifie que la chaîne de compilation et de test fonctionne.
TEST_CASE ("Le squelette expose l'identité du plugin", "[skeleton]")
{
    REQUIRE (openfilter::kName == "Open Filter");
    REQUIRE (! openfilter::kVersion.empty());
    REQUIRE (openfilter::kMaxBands == 24);
}
