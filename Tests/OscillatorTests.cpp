#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "DSP/Oscillator.h"

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr float kFrequencyHz = 220.0f;
constexpr int kNumSamples = 512;
constexpr float kSineEpsilon = 1.0e-4f;
constexpr float kSquareEpsilon = 5.0e-2f;

float computeIdealSine(int sampleIndex)
{
    const double phase = std::fmod((static_cast<double>(sampleIndex) * kFrequencyHz) / kSampleRate, 1.0);
    return std::sin(juce::MathConstants<double>::twoPi * phase);
}
}

TEST_CASE("Oscillator shape 0.0 matches sine wave", "[oscillator][shape][determinism]")
{
    Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(kFrequencyHz);
    osc.setShape(0.0f);

    for (int i = 0; i < kNumSamples; ++i)
    {
        const float actual = osc.getNextSample();
        const float expected = computeIdealSine(i);
        REQUIRE(actual == Catch::Approx(expected).margin(kSineEpsilon));
    }
}

TEST_CASE("Oscillator shape 0.5 approaches square wave away from edges", "[oscillator][shape][boundary]")
{
    Oscillator osc;
    osc.prepare(kSampleRate);
    osc.setFrequency(kFrequencyHz);
    osc.setShape(0.5f);

    const float dt = kFrequencyHz / static_cast<float>(kSampleRate);
    int checkedSamples = 0;

    for (int i = 0; i < kNumSamples; ++i)
    {
        const float actual = osc.getNextSample();
        const float phase = std::fmod((static_cast<float>(i) * kFrequencyHz) / static_cast<float>(kSampleRate), 1.0f);

        const bool nearRisingEdge = phase < (2.0f * dt) || phase > (1.0f - 2.0f * dt);
        const bool nearFallingEdge = std::abs(phase - 0.5f) < (2.0f * dt);
        if (nearRisingEdge || nearFallingEdge)
        {
            continue;
        }

        const float expected = phase < 0.5f ? 1.0f : -1.0f;
        REQUIRE(actual == Catch::Approx(expected).margin(kSquareEpsilon));
        ++checkedSamples;
    }

    REQUIRE(checkedSamples > (kNumSamples / 2));
}
