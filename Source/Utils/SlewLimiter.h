#pragma once

#include <juce_core/juce_core.h>

class SlewLimiter
{
public:
    void prepare(double sampleRate)
    {
        sampleRateHz = juce::jmax(1.0, sampleRate);
        updateCoefficients();
    }

    void reset(float value = 0.0f) noexcept
    {
        currentValue = value;
    }

    void setRiseAndFallTimesMs(float riseTimeMs, float fallTimeMs)
    {
        riseMs = juce::jmax(0.0f, riseTimeMs);
        fallMs = juce::jmax(0.0f, fallTimeMs);
        updateCoefficients();
    }

    float process(float target)
    {
        if (target > currentValue)
        {
            currentValue = juce::jmin(target, currentValue + riseStep);
        }
        else if (target < currentValue)
        {
            currentValue = juce::jmax(target, currentValue - fallStep);
        }

        return currentValue;
    }

private:
    void updateCoefficients()
    {
        const auto toStep = [this] (float timeMs)
        {
            if (timeMs <= 0.0f)
            {
                return 1.0f;
            }

            const float samples = static_cast<float>(sampleRateHz * (timeMs * 0.001f));
            return 1.0f / juce::jmax(1.0f, samples);
        };

        riseStep = toStep(riseMs);
        fallStep = toStep(fallMs);
    }

    double sampleRateHz { 44100.0 };
    float riseMs { 0.0f };
    float fallMs { 0.0f };
    float riseStep { 1.0f };
    float fallStep { 1.0f };
    float currentValue { 0.0f };
};
