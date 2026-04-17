#pragma once
#include "Oscillator.h"

class RumbleEngine {
public:
    RumbleEngine() = default;

    void prepare(double sampleRate)
    {
        sampleRateHz = juce::jmax(1.0, sampleRate);

        subOsc.prepare(sampleRateHz);
        midOscA.prepare(sampleRateHz);
        midOscB.prepare(sampleRateHz);

        updateFrequencies();
        setShape(shape);
    }

    void setShape(float newShape)
    {
        shape = juce::jlimit(0.0f, 1.0f, newShape);

        subOsc.setShape(shape);
        midOscA.setShape(shape);
        midOscB.setShape(shape);
    }

    void setHarmony(float newHarmony)
    {
        harmony = juce::jlimit(0.0f, 1.0f, newHarmony);
        updateFrequencies();
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numOutputChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numOutputChannels <= 0)
        {
            return;
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float sub = subOsc.getNextSample() * 0.6f;
            const float midA = midOscA.getNextSample() * 0.2f;
            const float midB = midOscB.getNextSample() * 0.2f;
            const float mixed = juce::jlimit(-1.0f, 1.0f, sub + midA + midB);

            for (int channel = 0; channel < numOutputChannels; ++channel)
            {
                buffer.setSample(channel, sample, mixed);
            }
        }
    }

private:
    void updateFrequencies()
    {
        // Anchored around A1 while exposing harmonic spread from the HARMONY macro.
        constexpr float baseFrequencyHz = 55.0f;
        const float subFrequency = baseFrequencyHz * 0.5f;
        const float midAFrequency = baseFrequencyHz * (1.0f + harmony);
        const float midBFrequency = baseFrequencyHz * (1.5f + harmony * 1.5f);

        subOsc.setFrequency(subFrequency);
        midOscA.setFrequency(midAFrequency);
        midOscB.setFrequency(midBFrequency);
    }

    double sampleRateHz { 44100.0 };
    float shape { 0.0f };
    float harmony { 0.0f };

    Oscillator subOsc;
    Oscillator midOscA;
    Oscillator midOscB;
};