#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "DSP/SignalProcessor.h"

TEST_CASE("SignalProcessor kinked macro taper endpoints and monotonicity", "[signal][grit][mapping]")
{
    REQUIRE(SignalProcessor::applyKinkedMacroTaper(0.0f) == Catch::Approx(0.0f));
    REQUIRE(SignalProcessor::applyKinkedMacroTaper(1.0f) == Catch::Approx(1.0f));
    REQUIRE(SignalProcessor::applyKinkedMacroTaper(0.5f) == Catch::Approx(0.5f));

    const float a = SignalProcessor::applyKinkedMacroTaper(0.2f);
    const float b = SignalProcessor::applyKinkedMacroTaper(0.6f);
    const float c = SignalProcessor::applyKinkedMacroTaper(0.9f);
    REQUIRE(a < b);
    REQUIRE(b < c);
}

TEST_CASE("SignalProcessor grit zero is transparent", "[signal][grit][bypass]")
{
    SignalProcessor signal;
    signal.prepareGritSmoother(48000.0, 0.01, SignalProcessor::applyKinkedMacroTaper(0.0f));
    signal.setEntropySeedForTests(42);
    signal.setGrit(0.0f);
    signal.advanceGritSmoothed();

    for (float x : { -0.9f, -0.3f, 0.0f, 0.2f, 0.75f })
    {
        REQUIRE(signal.applyGritManifold(x) == Catch::Approx(x).margin(1.0e-7f));
    }
}

TEST_CASE("SignalProcessor safety rejects DC energy over time", "[signal][safety][dc]")
{
    SignalProcessor signal;
    signal.prepareSafety(48000.0);

    float last = 0.0f;
    for (int i = 0; i < 4096; ++i)
    {
        last = signal.processSafety(0, 0.5f);
    }

    REQUIRE(std::abs(last) < 1.0e-2f);
}
