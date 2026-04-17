#pragma once
#include <array>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Oscillator.h"
#include "Utils/SlewLimiter.h"

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
        setPulse(mPulse);
        gateSlew.prepare(sampleRateHz);
        gateSlew.reset(1.0f);
        noteOff();
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

    void setGrit(float newGrit)
    {
        mGrit = juce::jlimit(0.0f, 1.0f, newGrit);
    }

    void setPulse(float newPulse)
    {
        mPulse = juce::jlimit(0.0f, 1.0f, newPulse);
        mGateDutyCycle = juce::jmap(mPulse, 0.0f, 1.0f, 0.15f, 0.95f);

        // Lower pulse values feel clickier; higher values soften gate transitions.
        const float slewMs = juce::jmap(mPulse, 0.0f, 1.0f, 0.2f, 35.0f);
        gateSlew.setRiseAndFallTimesMs(slewMs, slewMs);
    }

    void setMasterGain(float newMasterGain)
    {
        mMasterGain = juce::jmax(0.0f, newMasterGain);
    }

    void setTransportInfo(double bpm, double ppqPosition, bool isPlaying, bool hasPpqPosition)
    {
        mCurrentBpm = juce::jlimit(20.0, 300.0, bpm);
        mCurrentPpqPosition = ppqPosition;
        mIsTransportPlaying = isPlaying;
        mHasHostPpq = hasPpqPosition;
    }

    void noteOn(int midiNoteNumber, float velocity)
    {
        const float noteFrequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
        mBaseFrequencyHz = juce::jlimit(1.0f, 20000.0f, noteFrequency);
        mVelocity = juce::jlimit(0.0f, 1.0f, velocity);

        updateFrequencies();

        subOsc.resetPhase();
        midOscA.resetPhase();
        midOscB.resetPhase();

        subOsc.setActive(true);
        midOscA.setActive(true);
        midOscB.setActive(true);

        gateSlew.reset(mIsTransportPlaying ? 0.0f : 1.0f);
    }

    void noteOff()
    {
        mVelocity = 0.0f;
        subOsc.setActive(false);
        midOscA.setActive(false);
        midOscB.setActive(false);
        gateSlew.reset(0.0f);
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numOutputChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numOutputChannels <= 0)
        {
            return;
        }

        double gatePhase = getBlockStartGatePhase();
        const double gatePhaseIncrement = getGatePhaseIncrement();

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float sub = subOsc.getNextSample() * 0.6f;
            const float midA = midOscA.getNextSample() * 0.2f;
            const float midB = midOscB.getNextSample() * 0.2f;
            const float gateTarget = getGateTarget(gatePhase);
            const float gate = gateSlew.process(gateTarget);

            const float dry = (sub + midA + midB) * mMasterGain * mVelocity * gate;

            float mixed = dry;
            if (mGrit > 0.0001f)
            {
                const float drive = 1.0f + mGrit * 8.0f;
                const float clipped = std::tanh(dry * drive);
                const float makeUpGain = 1.0f / juce::jmax(0.05f, std::tanh(drive));
                mixed = clipped * makeUpGain;
            }

            mixed = juce::jlimit(-1.0f, 1.0f, mixed);

            for (int channel = 0; channel < numOutputChannels; ++channel)
            {
                buffer.setSample(channel, sample, mixed);
            }

            gatePhase += gatePhaseIncrement;
            if (gatePhase >= 1.0)
            {
                gatePhase -= std::floor(gatePhase);
            }
        }

        mInternalGatePhase = gatePhase;
    }

    std::array<double, 3> getChildSampleRatesForTests() const noexcept
    {
        return {
            subOsc.getSampleRateForTests(),
            midOscA.getSampleRateForTests(),
            midOscB.getSampleRateForTests()
        };
    }

private:
    double getGatePhaseIncrement() const noexcept
    {
        return ((mCurrentBpm / 60.0) * 4.0) / sampleRateHz;
    }

    double getBlockStartGatePhase() const noexcept
    {
        if (mHasHostPpq)
        {
            return std::fmod(mCurrentPpqPosition * 4.0, 1.0);
        }

        return mInternalGatePhase;
    }

    float getGateTarget(double gatePhase) const noexcept
    {
        if (! mIsTransportPlaying)
        {
            return 1.0f;
        }

        return gatePhase < static_cast<double>(mGateDutyCycle) ? 1.0f : 0.0f;
    }

    void updateFrequencies()
    {
        const float subFrequency = mBaseFrequencyHz * 0.5f;
        const float midAFrequency = mBaseFrequencyHz * (1.0f + harmony);
        const float midBFrequency = mBaseFrequencyHz * (1.5f + harmony * 1.5f);

        subOsc.setFrequency(subFrequency);
        midOscA.setFrequency(midAFrequency);
        midOscB.setFrequency(midBFrequency);
    }

    double sampleRateHz { 44100.0 };
    float shape { 0.0f };
    float harmony { 0.0f };
    float mBaseFrequencyHz { 55.0f };
    float mVelocity { 0.0f };
    float mMasterGain { 0.5f };
    float mGrit { 0.0f };
    float mPulse { 0.5f };
    float mGateDutyCycle { 0.55f };

    double mCurrentBpm { 120.0 };
    double mCurrentPpqPosition { 0.0 };
    double mInternalGatePhase { 0.0 };
    bool mIsTransportPlaying { false };
    bool mHasHostPpq { false };

    SlewLimiter gateSlew;

    Oscillator subOsc;
    Oscillator midOscA;
    Oscillator midOscB;
};