#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <unordered_set>
#include "DSP/RumbleEngine.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kNumSamples = 256;
constexpr int kMidiNote = 33; // A1 ~= 55 Hz
constexpr float kLowTestFrequencyHz = 60.0f;
constexpr float kShape = 0.25f;
constexpr float kHarmony = 0.4f;
constexpr float kEpsilon = 1.0e-4f;
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
    RumbleEngine engineA;
    engineA.prepare(kSampleRate);
    engineA.setShape(kShape);
    engineA.setHarmony(kHarmony);
    engineA.setGrit(0.0f);
    engineA.setGirth(0.35f);
    engineA.setMasterGain(1.0f);
    engineA.noteOn(kMidiNote, 1.0f);

    RumbleEngine engineB;
    engineB.prepare(kSampleRate);
    engineB.setShape(kShape);
    engineB.setHarmony(kHarmony);
    engineB.setGrit(0.0f);
    engineB.setGirth(0.35f);
    engineB.setMasterGain(1.0f);
    engineB.noteOn(kMidiNote, 1.0f);

    juce::AudioBuffer<float> bufferA(2, kNumSamples);
    juce::AudioBuffer<float> bufferB(2, kNumSamples);
    bufferA.clear();
    bufferB.clear();
    engineA.process(bufferA);
    engineB.process(bufferB);

    for (int i = 0; i < kNumSamples; ++i)
    {
        REQUIRE(bufferA.getSample(0, i) == Catch::Approx(bufferB.getSample(0, i)).margin(kEpsilon));
        REQUIRE(bufferA.getSample(1, i) == Catch::Approx(bufferB.getSample(1, i)).margin(kEpsilon));
    }
}

TEST_CASE("RumbleEngine noteOff silences output", "[engine][midi][boundary]")
{
    RumbleEngine engine;
    constexpr double releaseSampleRate = 44100.0;
    engine.prepare(releaseSampleRate);
    engine.noteOn(kMidiNote, 1.0f);

    juce::AudioBuffer<float> preBuffer(2, 64);
    preBuffer.clear();
    engine.process(preBuffer);

    engine.noteOff();

    juce::AudioBuffer<float> buffer(2, 320);
    buffer.clear();
    engine.process(buffer);

    float earlyPeak = 0.0f;
    for (int i = 0; i < 40; ++i)
    {
        earlyPeak = juce::jmax(earlyPeak, std::abs(buffer.getSample(0, i)));
    }
    REQUIRE(earlyPeak > 1.0e-4f);

    int silenceStartSample = -1;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        bool remainingSilent = true;
        for (int j = i; j < buffer.getNumSamples(); ++j)
        {
            if (std::abs(buffer.getSample(0, j)) > 1.0e-6f || std::abs(buffer.getSample(1, j)) > 1.0e-6f)
            {
                remainingSilent = false;
                break;
            }
        }

        if (remainingSilent)
        {
            silenceStartSample = i;
            break;
        }
    }

    REQUIRE(silenceStartSample >= 210);
    REQUIRE(silenceStartSample <= 230);
}

TEST_CASE("Harmony 0 maps to 1:2:4 frequency ratios", "[engine][harmony][ratios]")
{
    RumbleEngine engine;
    engine.prepare(kSampleRate);
    engine.setHarmony(0.0f);
    engine.noteOn(kMidiNote, 1.0f);

    const auto freqs = engine.getCurrentFrequenciesForTests();
    REQUIRE(freqs[0] > 0.0f);

    const float ratioA = freqs[1] / freqs[0];
    const float ratioB = freqs[2] / freqs[0];

    REQUIRE(ratioA == Catch::Approx(2.0f).margin(1.0e-6f));
    REQUIRE(ratioB == Catch::Approx(4.0f).margin(1.0e-6f));
}

TEST_CASE("Low band remains mono regardless of GIRTH", "[engine][girth][crossover]")
{
    constexpr int warmupSamples = 1024;
    constexpr int measuredSamples = 512;
    constexpr float monoEpsilon = 0.025f;
    constexpr float rmsEpsilon = 0.06f;
    constexpr float expectedRms = 0.5f / juce::MathConstants<float>::sqrt2;

    for (const float girth : { 0.0f, 1.0f })
    {
        RumbleEngine engine;
        engine.prepare(kSampleRate);
        engine.setGirth(girth);

        float sumSquaresLeft = 0.0f;
        float sumSquaresRight = 0.0f;

        for (int i = 0; i < warmupSamples + measuredSamples; ++i)
        {
            const float inputLeft = std::sin(juce::MathConstants<float>::twoPi * kLowTestFrequencyHz * static_cast<float>(i) / static_cast<float>(kSampleRate));
            const auto stereoOut = engine.processSpatialFrameForTests(inputLeft, 0.0f);

            if (i >= warmupSamples)
            {
                sumSquaresLeft += stereoOut[0] * stereoOut[0];
                sumSquaresRight += stereoOut[1] * stereoOut[1];
            }
        }

        const float rmsLeft = std::sqrt(sumSquaresLeft / static_cast<float>(measuredSamples));
        const float rmsRight = std::sqrt(sumSquaresRight / static_cast<float>(measuredSamples));
        REQUIRE(rmsLeft == Catch::Approx(rmsRight).margin(monoEpsilon));
        REQUIRE(rmsLeft == Catch::Approx(expectedRms).margin(rmsEpsilon));
    }
}

TEST_CASE("Max GRIT introduces entropy-driven non-periodic differences", "[engine][grit][entropy]")
{
    RumbleEngine engineA;
    engineA.prepare(kSampleRate);
    engineA.setGrit(1.0f);
    engineA.setEntropySeedForTests(12345);
    engineA.noteOn(kMidiNote, 1.0f);

    RumbleEngine engineB;
    engineB.prepare(kSampleRate);
    engineB.setGrit(1.0f);
    engineB.setEntropySeedForTests(98765);
    engineB.noteOn(kMidiNote, 1.0f);

    juce::AudioBuffer<float> bufferA(2, kNumSamples);
    juce::AudioBuffer<float> bufferB(2, kNumSamples);
    bufferA.clear();
    bufferB.clear();
    engineA.process(bufferA);
    engineB.process(bufferB);

    float differenceEnergy = 0.0f;
    for (int i = 0; i < kNumSamples; ++i)
    {
        const float diff = bufferA.getSample(0, i) - bufferB.getSample(0, i);
        differenceEnergy += diff * diff;
    }

    REQUIRE(differenceEnergy > 1.0e-4f);
}

TEST_CASE("Max GRIT reduces effective sample value resolution", "[engine][grit][bitcrush]")
{
    constexpr int oneSecondSamples = static_cast<int>(kSampleRate);
    constexpr double quantizeScale = 1000000.0;
    constexpr float testFrequencyHz = 110.0f;

    auto countUniqueQuantized = [quantizeScale] (const std::vector<float>& signal)
    {
        std::unordered_set<int> values;
        values.reserve(signal.size());
        for (const float sample : signal)
        {
            values.insert(juce::roundToInt(sample * quantizeScale));
        }
        return static_cast<int>(values.size());
    };

    RumbleEngine cleanEngine;
    cleanEngine.prepare(kSampleRate);
    cleanEngine.setShape(0.0f); // sine-like source.
    cleanEngine.setGrit(0.0f);
    cleanEngine.setEntropySeedForTests(1111);

    RumbleEngine crushedEngine;
    crushedEngine.prepare(kSampleRate);
    crushedEngine.setShape(0.0f);
    crushedEngine.setGrit(1.0f);
    crushedEngine.setEntropySeedForTests(1111);

    const auto clean = cleanEngine.renderGritManifoldForTests(oneSecondSamples, testFrequencyHz);
    const auto crushed = crushedEngine.renderGritManifoldForTests(oneSecondSamples, testFrequencyHz);

    const int cleanUnique = countUniqueQuantized(clean);
    const int crushedUnique = countUniqueQuantized(crushed);

    REQUIRE(cleanUnique > 1000);
    REQUIRE(crushedUnique < cleanUnique / 3);
}
