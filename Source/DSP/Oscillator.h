#pragma once
#include <JuceHeader.h>

class Oscillator {
public:
    Oscillator() = default;

    void prepare(double sampleRate)
    {
        sampleRateHz = juce::jmax(1.0, sampleRate);
        phase = 0.0;
    }

    void setFrequency(float newFrequencyHz)
    {
        frequencyHz = juce::jlimit(1.0f, 20000.0f, newFrequencyHz);
        updatePhaseIncrement();
    }

    // 0.0 -> sine, 0.5 -> square, 1.0 -> saw
    void setShape(float newShape)
    {
        shape = juce::jlimit(0.0f, 1.0f, newShape);
    }

    float getNextSample()
    {
        const float t = static_cast<float>(phase);
        const float dt = static_cast<float>(phaseIncrement);

        const float sineSample = std::sin(juce::MathConstants<float>::twoPi * t);
        const float sawSample = generatePolyBlepsaw(t, dt);
        const float squareSample = generatePolyBlepSquare(t, dt);

        const float firstMorph = juce::jmap(shape, 0.0f, 0.5f, 0.0f, 1.0f);
        const float secondMorph = juce::jmap(shape, 0.5f, 1.0f, 0.0f, 1.0f);

        const float sineToSquare = juce::jmap(juce::jlimit(0.0f, 1.0f, firstMorph), sineSample, squareSample);
        const float squareToSaw = juce::jmap(juce::jlimit(0.0f, 1.0f, secondMorph), squareSample, sawSample);

        const float result = (shape < 0.5f) ? sineToSquare : squareToSaw;

        advancePhase();
        return result;
    }

private:
    static float polyBlep(float t, float dt)
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }

        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }

        return 0.0f;
    }

    float generatePolyBlepsaw(float t, float dt) const
    {
        float value = 2.0f * t - 1.0f;
        value -= polyBlep(t, dt);
        return value;
    }

    float generatePolyBlepSquare(float t, float dt) const
    {
        float value = (t < 0.5f) ? 1.0f : -1.0f;
        value += polyBlep(t, dt);

        float t2 = t + 0.5f;
        if (t2 >= 1.0f)
        {
            t2 -= 1.0f;
        }

        value -= polyBlep(t2, dt);
        return value;
    }

    void updatePhaseIncrement()
    {
        phaseIncrement = static_cast<double>(frequencyHz) / sampleRateHz;
    }

    void advancePhase()
    {
        phase += phaseIncrement;
        if (phase >= 1.0)
        {
            phase -= std::floor(phase);
        }
    }

    double sampleRateHz { 44100.0 };
    double phase { 0.0 };
    double phaseIncrement { 440.0 / 44100.0 };
    float frequencyHz { 440.0f };
    float shape { 0.0f };
};