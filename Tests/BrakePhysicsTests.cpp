#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <atomic>
#include "DSP/BrakePhysics.h"

TEST_CASE("BrakePhysics defaults to zero when parameter is null", "[brake][boundary]")
{
    BrakePhysics brake;
    brake.prepare(48000.0, 0.05);

    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(brake.next(48000.0) == Catch::Approx(0.0f).margin(1.0e-7f));
    }
}

TEST_CASE("BrakePhysics clamps input and smooths toward target", "[brake][smoothing][clamp]")
{
    std::atomic<float> param { 2.0f };
    BrakePhysics brake;
    brake.setParameter(&param);
    brake.prepare(48000.0, 0.02);

    const float first = brake.next(48000.0);
    REQUIRE(first > 0.0f);
    REQUIRE(first < 1.0f);

    float value = first;
    for (int i = 0; i < 4000; ++i)
    {
        value = brake.next(48000.0);
    }
    REQUIRE(value == Catch::Approx(1.0f).margin(1.0e-3f));

    param.store(-1.0f);
    const float down = brake.next(48000.0);
    REQUIRE(down < value);
}
