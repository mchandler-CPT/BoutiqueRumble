#include <catch2/catch_test_macros.hpp>
#include "DSP/MotifEngine.h"

TEST_CASE("MotifEngine skip zero bypasses motif and keeps all steps active", "[motif][bypass][boundary]")
{
    MotifEngine motif;
    motif.prepareState();
    motif.rebuild(60, 0.0f, 0.0, false);

    REQUIRE(motif.bypass);
    REQUIRE(motif.stepIndex == 0);
    for (const bool step : motif.pattern)
    {
        REQUIRE(step);
    }
}

TEST_CASE("MotifEngine rebuild is deterministic for same note and skip", "[motif][determinism]")
{
    MotifEngine first;
    first.prepareState();
    first.rebuild(64, 0.42f, 1.5, true);

    MotifEngine second;
    second.prepareState();
    second.rebuild(64, 0.42f, 1.5, true);

    REQUIRE(first.pattern == second.pattern);
    REQUIRE(first.lockedMidiNote == second.lockedMidiNote);
    REQUIRE(first.lockedSkipValue == second.lockedSkipValue);
}

TEST_CASE("MotifEngine resets step index on host bar boundary", "[motif][transport][host]")
{
    MotifEngine motif;
    motif.prepareState();
    motif.rebuild(48, 0.5f, 0.0, true);

    for (int i = 0; i < 5; ++i)
    {
        motif.advanceOnPulseWrap(0.5, true);
    }
    REQUIRE(motif.stepIndex == 5);

    motif.advanceOnPulseWrap(4.0, true);
    REQUIRE(motif.stepIndex == 0);
}
