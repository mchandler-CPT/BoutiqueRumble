#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "DSP/RumbleEngine.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kNumSamples = 256;
constexpr float kShape = 0.25f;
constexpr float kHarmony = 0.4f;
constexpr float kEpsilon = 1.0e-4f;

void configureReferenceOscillators(Oscillator& subOsc, Oscillator& midOscA, Oscillator& midOscB)
{
    constexpr float baseFrequencyHz = 55.0f;
    const float subFrequency = baseFrequencyHz * 0.5f;
    const float midAFrequency = baseFrequencyHz * (1.0f + kHarmony);
    const float midBFrequency = baseFrequencyHz * (1.5f + kHarmony * 1.5f);

    subOsc.prepare(kSampleRate);
    midOscA.prepare(kSampleRate);
    midOscB.prepare(kSampleRate);

    subOsc.setShape(kShape);
    midOscA.setShape(kShape);
    midOscB.setShape(kShape);

    subOsc.setFrequency(subFrequency);
    midOscA.setFrequency(midAFrequency);
    midOscB.setFrequency(midBFrequency);
}
}

TEST_CASE("RumbleEngine prepare propagates sample rate to children", "[engine][prepare]")
{
    RumbleEngine engine;
    engine.prepare(kSampleRate);

    const auto sampleRates = engine.getChildSampleRatesForTests();
    REQUIRE(sampleRates[0] == Catch::Approx(kSampleRate));
    REQUIRE(sampleRates[1] == Catch::Approx(kSampleRate));
    REQUIRE(sampleRates[2] == Catch::Approx(kSampleRate));
}

TEST_CASE("RumbleEngine sums sub and mids deterministically", "[engine][sum][determinism]")
{
    RumbleEngine engine;
    engine.prepare(kSampleRate);
    engine.setShape(kShape);
    engine.setHarmony(kHarmony);

    Oscillator refSub;
    Oscillator refMidA;
    Oscillator refMidB;
    configureReferenceOscillators(refSub, refMidA, refMidB);

    juce::AudioBuffer<float> buffer(2, kNumSamples);
    buffer.clear();
    engine.process(buffer);

    for (int i = 0; i < kNumSamples; ++i)
    {
        const float expected = juce::jlimit(
            -1.0f,
            1.0f,
            refSub.getNextSample() * 0.6f + refMidA.getNextSample() * 0.2f + refMidB.getNextSample() * 0.2f);

        REQUIRE(buffer.getSample(0, i) == Catch::Approx(expected).margin(kEpsilon));
        REQUIRE(buffer.getSample(1, i) == Catch::Approx(expected).margin(kEpsilon));
    }
}
