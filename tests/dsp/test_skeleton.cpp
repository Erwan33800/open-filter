#include <catch2/catch_test_macros.hpp>

#include "dsp/Info.h"

// Test du squelette : vérifie que la chaîne de compilation et de test fonctionne.
// Les noms de TEST_CASE restent en ASCII (sans accent ni apostrophe) : ctest les passe à Catch2 en ligne
// de commande, et la console Windows les déforme, ce qui fait échouer le test (« No test cases matched »).
TEST_CASE ("Skeleton exposes plugin identity", "[skeleton]")
{
    REQUIRE (openfilter::kName == "Open Filter");
    REQUIRE (! openfilter::kVersion.empty());
    REQUIRE (openfilter::kMaxBands == 24);
}
