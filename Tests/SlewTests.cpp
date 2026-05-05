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
    eighthEngine.setSkipProbability(0.0f);
    eighthEngine.setPulse(0.0f); // sharpest transitions for counting.
    eighthEngine.setSubdivision(2.0f); // 1/8
    eighthEngine.setTransportInfo(120.0, 0.0, true, false);
    eighthEngine.noteOn(36, 1.0f);
    const auto eighthEnvelope = eighthEngine.renderGateEnvelopeForTests(renderSamples);

    RumbleEngine sixteenthEngine;
    sixteenthEngine.prepare(sampleRate);
    sixteenthEngine.setSkipProbability(0.0f);
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

TEST_CASE("Girth 0 and Pulse 0.1 produce a sharp percussive ping", "[slew][lfo][pulse]")
{
    constexpr double sampleRate = 48000.0;
    constexpr double bpm = 120.0;

    RumbleEngine engine;
    engine.prepare(sampleRate);
    engine.setRate(6); // 1/16
    engine.setSkipProbability(0.0f);
    engine.setGirth(0.0f);
    engine.setPulse(0.1f);
    engine.setTransportInfo(bpm, 0.0, true, false);
    engine.noteOn(36, 1.0f);

    const int oneSixteenthSamples = static_cast<int>((60.0 / bpm / 4.0) * sampleRate);
    const auto envelope = engine.renderGateEnvelopeForTests(oneSixteenthSamples);
    const int quarterPoint = oneSixteenthSamples / 4;
    const int midpoint = oneSixteenthSamples / 2;

    float earlyPeak = 0.0f;
    for (int i = 0; i < quarterPoint; ++i)
    {
        earlyPeak = juce::jmax(earlyPeak, envelope[i]);
    }

    REQUIRE(earlyPeak > 0.7f);
    REQUIRE(envelope[midpoint] < 0.2f);
}

TEST_CASE("Legato second note preserves active slot phase in shadow crossfade", "[slew][legato][phase]")
{
    RumbleEngine engine;
    engine.prepare(48000.0);
    engine.noteOn(40, 1.0f);

    juce::AudioBuffer<float> warmup(2, 128);
    warmup.clear();
    engine.process(warmup);
    const auto phaseBefore = engine.getOscillatorPhasesForTests();

    engine.noteOn(52, 1.0f); // legato transition
    const auto phaseAfter = engine.getOscillatorPhasesForTests();

    REQUIRE(phaseAfter[0] == Catch::Approx(phaseBefore[0]).margin(1.0e-6));
    REQUIRE(phaseAfter[1] == Catch::Approx(phaseBefore[1]).margin(1.0e-6));
    REQUIRE(phaseAfter[2] == Catch::Approx(phaseBefore[2]).margin(1.0e-6));
}

TEST_CASE("Legato transition avoids zero-value sample discontinuity", "[slew][legato][continuity]")
{
    RumbleEngine engine;
    engine.prepare(48000.0);
    engine.setShape(0.0f); // sine exposes reset-to-zero artifacts.
    engine.setTransportInfo(120.0, 0.0, false, false); // keep gate open.
    engine.noteOn(40, 1.0f);

    juce::AudioBuffer<float> warmup(2, 256);
    warmup.clear();
    engine.process(warmup);

    engine.noteOn(52, 1.0f); // legato transition

    juce::AudioBuffer<float> transition(2, 24);
    transition.clear();
    engine.process(transition);

    for (int i = 0; i < transition.getNumSamples(); ++i)
    {
        REQUIRE(std::abs(transition.getSample(0, i)) > 1.0e-8f);
        REQUIRE(std::abs(transition.getSample(1, i)) > 1.0e-8f);
    }
}
