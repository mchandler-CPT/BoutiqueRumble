#pragma once

#include <atomic>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

class BrakePhysics {
public:
    std::atomic<float>* parameter { nullptr };
    juce::LinearSmoothedValue<float> smoother;
    float stallStutterPhase { 0.0f };

    static constexpr float brakeFrequencyIdleFloorHz = 20.0f;
    static constexpr float stutterRateScale = 40.0f;
    static constexpr float stutterRatePower = 3.0f;
    static constexpr float stutterSquareDuty = 0.5f;

    void setParameter(std::atomic<float>* param) noexcept
    {
        parameter = param;
    }

    void prepare(double sampleRate, double smootherSeconds)
    {
        smoother.reset(sampleRate, smootherSeconds);
        smoother.setCurrentAndTargetValue(1.0f);
        stallStutterPhase = 0.0f;
    }

    struct Frame {
        float brakeIn {};
        float pitchMult {};
        float gainMult {};
        float stutterGate {};
        float subHz {};
        float brakeFreqScale {};
    };

    Frame next(double sampleRateHz, float baseSubHz) noexcept
    {
        const float brakeTarget = (parameter != nullptr) ? parameter->load() : 1.0f;
        smoother.setTargetValue(juce::jlimit(0.0f, 1.0f, brakeTarget));
        const float brakeIn = smoother.getNextValue();
        const float brakeWarpX = (brakeIn - 0.5f) * 10.0f;
        const float warpedBrake = 1.0f / (1.0f + std::exp(-brakeWarpX));

        const float pitchMult = warpedBrake * warpedBrake * warpedBrake;
        const float gainMult = brakeIn;

        const float stutterIntensity = 1.0f - brakeIn;
        const float oneMinusBrake = 1.0f - brakeIn;
        const float stutterRateHz = stutterRateScale * std::pow(oneMinusBrake, stutterRatePower);

        float rawStutterSquareWave = 1.0f;
        if (stutterRateHz > 1.0e-9f)
        {
            stallStutterPhase += stutterRateHz / static_cast<float>(sampleRateHz);
            if (stallStutterPhase >= 1.0f)
            {
                stallStutterPhase -= std::floor(stallStutterPhase);
            }

            rawStutterSquareWave = (stallStutterPhase < stutterSquareDuty) ? 1.0f : 0.0f;
        }

        const float brakeStutterGate = 1.0f - (stutterIntensity * (1.0f - rawStutterSquareWave));

        const float rawSubHz = baseSubHz * pitchMult;
        float subHz = rawSubHz;
        if (brakeIn > 0.0f)
        {
            subHz = juce::jmax(brakeFrequencyIdleFloorHz, rawSubHz);
        }

        const float brakeFreqScale = (rawSubHz > 1.0e-6f) ? (subHz / rawSubHz) : 0.0f;

        return { brakeIn, pitchMult, gainMult, brakeStutterGate, subHz, brakeFreqScale };
    }
};
