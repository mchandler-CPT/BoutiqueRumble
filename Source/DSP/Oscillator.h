#pragma once
#include <JuceHeader.h>

class Oscillator {
public:
    Oscillator() = default;
    void prepare(double sampleRate) { juce::ignoreUnused(sampleRate); }
    float getNextSample() { return 0.0f; } // Silence for now
};