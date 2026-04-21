#pragma once

#include <array>
#include <cmath>
#include <vector>

#include <juce_dsp/juce_dsp.h>

class SignalProcessor {
public:
    static constexpr float crossoverFrequencyHz = 150.0f;
    static constexpr float upperMidBoundaryHz = 400.0f;
    static constexpr float dcBlockerHz = 5.0f;
    static constexpr float safetyHighPassHz = 25.0f;

    std::array<juce::dsp::IIR::Filter<float>, 2> dcBlockers;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> safetyOutputHp;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> lowPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> highPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> upperSplitFilters;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> rightDecorrelator { 512 };

    float mGrit { 0.0f };
    float mShapedGrit { 0.0f };
    float mEntropy { 0.0f };
    juce::LinearSmoothedValue<float> mGritSmoothed;
    juce::Random entropyRandom;

    static float applyKinkedMacroTaper(float x) noexcept
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, x);
        if (clamped <= 0.5f)
        {
            return 2.0f * clamped * clamped;
        }

        return juce::jlimit(0.0f, 1.0f, 0.5f + (clamped - 0.5f) * 1.35f);
    }

    void setGirth(float girth, double sampleRateHz) noexcept
    {
        const float delayMs = juce::jmap(juce::jlimit(0.0f, 1.0f, girth), 2.0f, 5.0f);
        rightDecorrelator.setDelay(static_cast<float>(sampleRateHz * (delayMs * 0.001f)));
    }

    void prepareGritSmoother(double sampleRateHz, double macroSmoothingSeconds, float initialGritTapered)
    {
        mGritSmoothed.reset(sampleRateHz, macroSmoothingSeconds);
        mGritSmoothed.setCurrentAndTargetValue(initialGritTapered);
        mShapedGrit = mGritSmoothed.getCurrentValue();
        mEntropy = mShapedGrit;
    }

    void setGrit(float newGrit)
    {
        mGrit = juce::jlimit(0.0f, 1.0f, newGrit);
        mGritSmoothed.setTargetValue(applyKinkedMacroTaper(mGrit));
    }

    void prepareCrossover(double sampleRateHz, float girth)
    {
        juce::dsp::ProcessSpec spec {};
        spec.sampleRate = sampleRateHz;
        spec.maximumBlockSize = 2048;
        spec.numChannels = 1;

        for (auto& lowPassFilter : lowPassFilters)
        {
            lowPassFilter.reset();
            lowPassFilter.prepare(spec);
            lowPassFilter.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            lowPassFilter.setCutoffFrequency(crossoverFrequencyHz);
        }

        for (auto& highPassFilter : highPassFilters)
        {
            highPassFilter.reset();
            highPassFilter.prepare(spec);
            highPassFilter.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
            highPassFilter.setCutoffFrequency(crossoverFrequencyHz);
        }

        for (auto& upperSplitFilter : upperSplitFilters)
        {
            upperSplitFilter.reset();
            upperSplitFilter.prepare(spec);
            upperSplitFilter.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            upperSplitFilter.setCutoffFrequency(upperMidBoundaryHz);
        }

        rightDecorrelator.reset();
        rightDecorrelator.prepare(spec);
        setGirth(girth, sampleRateHz);
    }

    void prepareSafety(double sampleRateHz)
    {
        juce::dsp::ProcessSpec spec {};
        spec.sampleRate = sampleRateHz;
        spec.maximumBlockSize = 2048;
        spec.numChannels = 1;

        const auto dcCoeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(
            sampleRateHz,
            static_cast<float>(dcBlockerHz));

        for (size_t i = 0; i < dcBlockers.size(); ++i)
        {
            dcBlockers[i].reset();
            dcBlockers[i].prepare(spec);
            dcBlockers[i].coefficients = dcCoeffs;
        }

        for (size_t i = 0; i < safetyOutputHp.size(); ++i)
        {
            safetyOutputHp[i].reset();
            safetyOutputHp[i].prepare(spec);
            safetyOutputHp[i].setType(juce::dsp::LinkwitzRileyFilterType::highpass);
            safetyOutputHp[i].setCutoffFrequency(safetyHighPassHz);
        }
    }

    float processSafety(int channelIndex, float x) noexcept
    {
        float low = 0.0f;
        float high = 0.0f;
        safetyOutputHp[static_cast<size_t>(channelIndex)].processSample(0, x, low, high);
        return dcBlockers[static_cast<size_t>(channelIndex)].processSample(high);
    }

    void resetSafety() noexcept
    {
        for (auto& dc : dcBlockers)
        {
            dc.reset();
        }

        for (auto& hp : safetyOutputHp)
        {
            hp.reset();
        }
    }

    void processMidHighSpatial(float& inOutLeft, float& inOutRight, float girth) noexcept
    {
        const float dryLeft = inOutLeft;
        const float dryRight = inOutRight;

        float lowLeft = 0.0f;
        float lowRight = 0.0f;
        float highLeft = 0.0f;
        float highRight = 0.0f;
        lowPassFilters[0].processSample(0, dryLeft, lowLeft, highLeft);
        lowPassFilters[1].processSample(0, dryRight, lowRight, highRight);

        juce::ignoreUnused(highPassFilters[0].processSample(0, dryLeft));
        juce::ignoreUnused(highPassFilters[1].processSample(0, dryRight));

        const float lowMono = 0.5f * (lowLeft + lowRight);

        float upperMidLeft = 0.0f;
        float upperMidRight = 0.0f;
        float airLeft = 0.0f;
        float airRight = 0.0f;
        upperSplitFilters[0].processSample(0, highLeft, upperMidLeft, airLeft);
        upperSplitFilters[1].processSample(0, highRight, upperMidRight, airRight);

        const float upperMidMono = 0.5f * (upperMidLeft + upperMidRight);

        rightDecorrelator.pushSample(0, airRight);
        const float delayedAirRight = rightDecorrelator.popSample(0);
        const float decorrelatedAirRight = juce::jmap(girth, airRight, delayedAirRight);

        const float mid = 0.5f * (airLeft + decorrelatedAirRight);
        const float sideEnergy = std::abs(airLeft) + std::abs(decorrelatedAirRight);
        const float sideWeight = juce::jlimit(0.0f, 1.0f, sideEnergy * 2.0f);
        const float side = 0.5f * (airLeft - decorrelatedAirRight) * (1.0f + girth) * sideWeight;
        const float widenedLeft = upperMidMono + mid + side;
        const float widenedRight = upperMidMono + mid - side;

        inOutLeft = juce::jlimit(-1.0f, 1.0f, lowMono + widenedLeft);
        inOutRight = juce::jlimit(-1.0f, 1.0f, lowMono + widenedRight);
    }

    void advanceGritSmoothed() noexcept
    {
        mShapedGrit = mGritSmoothed.getNextValue();
        mEntropy = mShapedGrit;
    }

    void resetFaultEngine() noexcept
    {
        mShapedGrit = 0.0f;
        mEntropy = 0.0f;
    }

    float applyGritManifold(float oscillatorSample)
    {
        float processed = oscillatorSample;

        const float tear = (entropyRandom.nextFloat() * 2.0f - 1.0f) * std::abs(oscillatorSample) * mShapedGrit * 0.1f;
        processed += tear;

        if (mShapedGrit > 0.5f)
        {
            const float bits = juce::jmap(mShapedGrit, 0.5f, 1.0f, 12.0f, 3.0f);
            const float quantizationLevels = std::pow(2.0f, bits);
            processed = std::round(processed * quantizationLevels) / quantizationLevels;
        }

        if (mShapedGrit > 0.0001f)
        {
            const float drive = 1.0f + mShapedGrit * 8.0f;
            const float clipped = std::tanh(processed * drive);
            const float makeUpGain = 1.0f / juce::jmax(0.05f, std::tanh(drive));
            processed = clipped * makeUpGain;
        }

        return processed;
    }

    void setEntropySeedForTests(int seed) noexcept
    {
        entropyRandom.setSeed(seed);
    }

    std::vector<float> renderGritManifoldForTests(double sampleRateHz, int numSamples, float frequencyHz)
    {
        std::vector<float> output;
        if (numSamples <= 0)
        {
            return output;
        }

        output.reserve(static_cast<size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            advanceGritSmoothed();
            const float phase = static_cast<float>(i) * frequencyHz / static_cast<float>(sampleRateHz);
            const float oscillatorSample = std::sin(juce::MathConstants<float>::twoPi * std::fmod(phase, 1.0f));
            output.push_back(applyGritManifold(oscillatorSample));
        }

        return output;
    }
};
