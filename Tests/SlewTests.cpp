#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "DSP/RumbleEngine.h"
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

TEST_CASE("Subdivision changes gate transition timing", "[slew][subdivision][timing]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int renderSamples = 48000; // one second

    auto countTransitions = [] (const std::vector<float>& envelope)
    {
        int transitions = 0;
        for (size_t i = 1; i < envelope.size(); ++i)
        {
            const bool wasOpen = envelope[i - 1] > 0.5f;
            const bool isOpen = envelope[i] > 0.5f;
            if (wasOpen != isOpen)
            {
                ++transitions;
            }
        }
        return transitions;
    };

    RumbleEngine eighthEngine;
    eighthEngine.prepare(sampleRate);
    eighthEngine.setPulse(0.0f); // sharpest transitions for counting.
    eighthEngine.setSubdivision(2.0f); // 1/8
    eighthEngine.setTransportInfo(120.0, 0.0, true, false);
    eighthEngine.noteOn(36, 1.0f);
    const auto eighthEnvelope = eighthEngine.renderGateEnvelopeForTests(renderSamples);

    RumbleEngine sixteenthEngine;
    sixteenthEngine.prepare(sampleRate);
    sixteenthEngine.setPulse(0.0f);
    sixteenthEngine.setSubdivision(4.0f); // 1/16
    sixteenthEngine.setTransportInfo(120.0, 0.0, true, false);
    sixteenthEngine.noteOn(36, 1.0f);
    const auto sixteenthEnvelope = sixteenthEngine.renderGateEnvelopeForTests(renderSamples);

    const int eighthTransitions = countTransitions(eighthEnvelope);
    const int sixteenthTransitions = countTransitions(sixteenthEnvelope);

    REQUIRE(eighthTransitions > 0);
    REQUIRE(sixteenthTransitions > eighthTransitions);
}

TEST_CASE("Rate index maps to expected multipliers", "[slew][rate][mapping]")
{
    RumbleEngine engine;
    engine.prepare(48000.0);

    engine.setRate(6); // 1/16
    REQUIRE(engine.getRateMultiplierForTests() == Catch::Approx(4.0f).margin(1.0e-6f));

    engine.setRate(4); // 1/8
    REQUIRE(engine.getRateMultiplierForTests() == Catch::Approx(2.0f).margin(1.0e-6f));
}
