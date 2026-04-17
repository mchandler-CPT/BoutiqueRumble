#include <catch2/catch_test_macros.hpp>
#include "Utils/SlewLimiter.h"

TEST_CASE("Slew limiter takes non-zero time for a step input", "[slew][determinism][boundary]")
{
    SlewLimiter slew;
    slew.prepare(48000.0);
    slew.setRiseAndFallTimesMs(20.0f, 20.0f);
    slew.reset(0.0f);

    const float firstSample = slew.process(1.0f);
    REQUIRE(firstSample > 0.0f);
    REQUIRE(firstSample < 1.0f);

    int samplesToReachTarget = 1;
    float value = firstSample;
    while (value < 0.999f && samplesToReachTarget < 96000)
    {
        value = slew.process(1.0f);
        ++samplesToReachTarget;
    }

    REQUIRE(samplesToReachTarget > 1);
    REQUIRE(samplesToReachTarget < 96000);
}
