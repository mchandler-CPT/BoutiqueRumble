#pragma once

#include <atomic>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

// Smoothed BREAK macro [0,1] where 0 = clean/off and 1 = full break/shred.
// Full-pulse silence / rhythmic gaps are owned by SKIP (MotifEngine) in
// RumbleEngine::getGateTarget — BREAK only adds non-zero gate fragmentation.
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
        smoother.setCurrentAndTargetValue(0.0f);
    }

    float next(double /*sampleRateHz*/) noexcept
    {
        const float breakTarget = (parameter != nullptr) ? parameter->load() : 0.0f;
        smoother.setTargetValue(juce::jlimit(0.0f, 1.0f, breakTarget));
        return smoother.getNextValue();
    }
};
