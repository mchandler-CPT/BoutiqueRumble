#pragma once
#include "Oscillator.h"

class RumbleEngine {
public:
    RumbleEngine() = default;
    void prepare(double sampleRate) { juce::ignoreUnused(sampleRate); }
    void process(juce::AudioBuffer<float>& buffer) { juce::ignoreUnused(buffer); }
};