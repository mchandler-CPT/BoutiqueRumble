#pragma once

#include <atomic>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

// Smoothed macro [0,1] only (1 = clean). Full-pulse silence / rhythmic gaps are owned by SKIP
// (MotifEngine) in RumbleEngine::getGateTarget — gate "shred" is applied there as non-zero
// additive ratcheting, never as a subtractive pulse skip from this value alone.
class BrakePhysics {
public:
    std::atomic<float>* parameter { nullptr };
    juce::LinearSmoothedValue<float> smoother;

    void setParameter(std::atomic<float>* param) noexcept
    {
        parameter = param;
    }

    void prepare(double sampleRate, double smootherSeconds)
    {
        smoother.reset(sampleRate, smootherSeconds);
        smoother.setCurrentAndTargetValue(1.0f);
    }

    float next(double /*sampleRateHz*/) noexcept
    {
        const float brakeTarget = (parameter != nullptr) ? parameter->load() : 1.0f;
        smoother.setTargetValue(juce::jlimit(0.0f, 1.0f, brakeTarget));
        return smoother.getNextValue();
    }
};
