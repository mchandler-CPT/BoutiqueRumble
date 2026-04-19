#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "MotifEngine.h"
#include "VoiceBank.h"
#include "BrakePhysics.h"
#include "SignalProcessor.h"
#include "Utils/SlewLimiter.h"

class RumbleEngine {
public:
    RumbleEngine() = default;

    void prepare(double sampleRate)
    {
        sampleRateHz = juce::jmax(1.0, sampleRate);

        mVoice.prepare(sampleRateHz, mVoice.shape, mVoice.harmony, mGirth);
        mSignal.prepareGritSmoother(sampleRateHz, macroSmoothingSeconds, SignalProcessor::applyKinkedMacroTaper(mSignal.mGrit));
        mVoice.updateReleaseEnvelopeCoefficient(sampleRateHz, mCurrentBpm, mSubdivisionMultiplier);

        setPulse(mPulse);
        gateSlew.prepare(sampleRateHz);
        mSignal.prepareCrossover(sampleRateHz, mGirth);
        mSignal.prepareSafety(sampleRateHz);
        mMotif.prepareState();
        mGateRetriggerDipSamplesRemaining = 0;
        mBrake.prepare(sampleRateHz, brakeSmootherSeconds);
        noteOff();
#if JUCE_DEBUG
        mDbgHadPreviousOnset = false;
        mDbgLastOnsetSampleIndex = 0;
        mGlobalAudioSampleCounterForDbg = 0;
#endif
    }

    void setBrakeParameter(std::atomic<float>* param) noexcept
    {
        mBrake.setParameter(param);
    }

    void setShape(float newShape)
    {
        mVoice.setShape(newShape);
    }

    void setHarmony(float newHarmony)
    {
        mVoice.setHarmony(newHarmony);
    }

    void setGrit(float newGrit)
    {
        mSignal.setGrit(newGrit);
    }

    void setGirth(float newGirth)
    {
        mGirth = juce::jlimit(0.0f, 1.0f, newGirth);
        mSignal.setGirth(mGirth, sampleRateHz);
        mVoice.setGirthForDrift(mGirth);
    }

    void setPulse(float newPulse)
    {
        mPulse = juce::jlimit(0.0f, 1.0f, newPulse);
        mGateDutyCycle = juce::jmap(mPulse, 0.0f, 1.0f, 0.15f, 0.95f);

        const float slewMs = juce::jmax(minGateSlewMs, juce::jmap(mPulse, 0.0f, 1.0f, 0.2f, 35.0f));
        gateSlew.setRiseAndFallTimesMs(slewMs, slewMs);
    }

    void setSubdivision(float multiplier)
    {
        mSubdivisionMultiplier = juce::jlimit(0.25f, 16.0f, multiplier);
        mVoice.updateReleaseEnvelopeCoefficient(sampleRateHz, mCurrentBpm, mSubdivisionMultiplier);
    }

    void setRate(int index)
    {
        mRateIndex = juce::jlimit(0, 9, index);
        mSubdivisionMultiplier = rateIndexToMultiplier(mRateIndex);
        mVoice.updateReleaseEnvelopeCoefficient(sampleRateHz, mCurrentBpm, mSubdivisionMultiplier);
    }

    void setSkipProbability(float p) noexcept
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, p);
        const bool changed = (clamped != mSkipProbability);
        mSkipProbability = clamped;

        if (mIsNoteSustaining && changed)
        {
            mMotif.rebuild(juce::jlimit(0, 127, juce::roundToInt(mVoice.mActiveMidiNote)), clamped, mCurrentPpqPosition, mHasHostPpq);
        }
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
        mVoice.updateReleaseEnvelopeCoefficient(sampleRateHz, mCurrentBpm, mSubdivisionMultiplier);
    }

    void noteOn(int midiNoteNumber, float velocity)
    {
        const bool voiceAlreadyActive = mIsNoteSustaining || mVoice.mNoteGainEnvelope > 0.0f;
        const bool legatoRetrigger = mIsNoteSustaining;

        if (legatoRetrigger)
        {
            mGateRetriggerDipSamplesRemaining = juce::jmax(1, juce::roundToInt(sampleRateHz * gateRetriggerDipSeconds));
        }

        mVoice.noteOn(midiNoteNumber, velocity, legatoRetrigger, voiceAlreadyActive, sampleRateHz, mCurrentBpm, mSubdivisionMultiplier);

        const int activeNote = juce::jlimit(0, 127, midiNoteNumber);
        const bool shouldRebuildMotif = (! legatoRetrigger) || (activeNote != mMotif.lockedMidiNote);
        if (shouldRebuildMotif)
        {
            mMotif.rebuild(activeNote, mSkipProbability, mCurrentPpqPosition, mHasHostPpq);
        }

        mIsNoteSustaining = true;
    }

    void noteOff()
    {
        mIsNoteSustaining = false;
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numOutputChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numOutputChannels <= 0)
        {
            return;
        }

        mVoice.updateReleaseEnvelopeCoefficient(sampleRateHz, mCurrentBpm, mSubdivisionMultiplier);

        double pulsePhase = getBlockStartPulsePhase();
        const double gatePhaseIncrement = getGatePhaseIncrement();

        for (int sample = 0; sample < numSamples; ++sample)
        {
#if JUCE_DEBUG
            ++mGlobalAudioSampleCounterForDbg;
#endif
            mVoice.advanceSubDriftLfos(sampleRateHz);

            const BrakePhysics::Frame brake = mBrake.next(sampleRateHz, mVoice.getSubFrequencyHz());

            mVoice.tickAmplitudeEnvelope(mIsNoteSustaining);

            mVoice.tickPitchGlideAndOrganicFrequencies();
            mVoice.applyBrakeToOscFrequencies(brake);
            mVoice.advanceShapeSmoothedAndApplyToOscillators();
            mSignal.advanceGritSmoothed();

            float sub = 0.0f;
            float midA = 0.0f;
            float midB = 0.0f;
            mVoice.sampleOscillators(sub, midA, midB);

            const float pulseGateLfo = advanceGate(pulsePhase);
            const float pulseGate = pulseGateLfo * brake.stutterGate;

            const float subMono = sub * mMasterGain * mVoice.mVelocity * pulseGate;
            const float midMono = (midA + midB) * mMasterGain * mVoice.mVelocity * pulseGate;

            float leftOut = mSignal.applyGritManifold(midMono);
            float rightOut = leftOut;
            mSignal.processMidHighSpatial(leftOut, rightOut, mGirth);
            leftOut = juce::jlimit(-1.0f, 1.0f, leftOut + subMono);
            rightOut = juce::jlimit(-1.0f, 1.0f, rightOut + subMono);
            leftOut *= mVoice.mNoteGainEnvelope;
            rightOut *= mVoice.mNoteGainEnvelope;
            leftOut *= brake.gainMult;
            rightOut *= brake.gainMult;

            if (mVoice.mNoteGainEnvelope <= 0.0f && ! mIsNoteSustaining)
            {
                leftOut = 0.0f;
                rightOut = 0.0f;
                mSignal.resetSafety();
            }
            else
            {
                leftOut = mSignal.processSafety(0, leftOut);
                if (numOutputChannels > 1)
                {
                    rightOut = mSignal.processSafety(1, rightOut);
                }
            }

            buffer.setSample(0, sample, leftOut);
            if (numOutputChannels > 1)
            {
                buffer.setSample(1, sample, rightOut);
            }
            for (int channel = 2; channel < numOutputChannels; ++channel)
            {
                buffer.setSample(channel, sample, 0.0f);
            }

            pulsePhase += gatePhaseIncrement;
            if (pulsePhase >= 1.0)
            {
#if JUCE_DEBUG
                if (mIsTransportPlaying)
                {
                    if (mDbgHadPreviousOnset)
                    {
                        const double dtMs = static_cast<double>(mGlobalAudioSampleCounterForDbg - mDbgLastOnsetSampleIndex)
                            * (1000.0 / sampleRateHz);
                        ++mDbgOnsetLogCounter;
                        if ((mDbgOnsetLogCounter % 8u) == 0u)
                        {
                            DBG("[RumbleEngine] timeBetweenOnsets = " << dtMs << " ms @ Girth=" << mGirth);
                        }
                    }

                    mDbgHadPreviousOnset = true;
                    mDbgLastOnsetSampleIndex = mGlobalAudioSampleCounterForDbg;
                }
#endif
                pulsePhase -= std::floor(pulsePhase);
                mMotif.advanceOnPulseWrap(mCurrentPpqPosition, mHasHostPpq);
            }
        }

        mPulsePhase = pulsePhase;
    }

    std::array<double, 3> getChildSampleRatesForTests() const noexcept
    {
        return mVoice.getChildSampleRatesForTests();
    }

    std::array<float, 2> processSpatialFrameForTests(float inLeft, float inRight)
    {
        mSignal.processMidHighSpatial(inLeft, inRight, mGirth);
        return { inLeft, inRight };
    }

    std::array<float, 3> getCurrentFrequenciesForTests() const noexcept
    {
        return mVoice.getCurrentFrequenciesForTests();
    }

    std::array<double, 3> getOscillatorPhasesForTests() const noexcept
    {
        return mVoice.getOscillatorPhasesForTests();
    }

    float getRateMultiplierForTests() const noexcept
    {
        return mSubdivisionMultiplier;
    }

    std::array<bool, 16> getMotifPatternForTests() const noexcept
    {
        return mMotif.pattern;
    }

    uint8_t getMotifStepIndexForTests() const noexcept
    {
        return mMotif.stepIndex;
    }

    double getPulsePhaseForTests() const noexcept
    {
        return mPulsePhase;
    }

    double getGatePhaseIncrementForTests() const noexcept
    {
        return getGatePhaseIncrement();
    }

    void advancePulseWrappedRhythmForTests(int numFullPulseWraps)
    {
        if (numFullPulseWraps <= 0)
        {
            return;
        }

        double pulsePhase = getBlockStartPulsePhase();
        const double gatePhaseIncrement = getGatePhaseIncrement();

        for (int w = 0; w < numFullPulseWraps; ++w)
        {
            while (pulsePhase < 1.0)
            {
                juce::ignoreUnused(advanceGate(pulsePhase));
                pulsePhase += gatePhaseIncrement;
            }

            pulsePhase -= std::floor(pulsePhase);
            mMotif.advanceOnPulseWrap(mCurrentPpqPosition, mHasHostPpq);
        }

        mPulsePhase = pulsePhase;
    }

    void setEntropySeedForTests(int seed) noexcept
    {
        mSignal.setEntropySeedForTests(seed);
    }

    std::vector<float> renderGateEnvelopeForTests(int numSamples)
    {
        std::vector<float> envelope;
        if (numSamples <= 0)
        {
            return envelope;
        }

        envelope.reserve(static_cast<size_t>(numSamples));
        double pulsePhase = getBlockStartPulsePhase();
        const double gatePhaseIncrement = getGatePhaseIncrement();

        for (int i = 0; i < numSamples; ++i)
        {
            envelope.push_back(advanceGate(pulsePhase));
            pulsePhase += gatePhaseIncrement;
            if (pulsePhase >= 1.0)
            {
                pulsePhase -= std::floor(pulsePhase);
                mMotif.advanceOnPulseWrap(mCurrentPpqPosition, mHasHostPpq);
            }
        }

        mPulsePhase = pulsePhase;
        return envelope;
    }

    std::vector<float> renderGritManifoldForTests(int numSamples, float frequencyHz)
    {
        return mSignal.renderGritManifoldForTests(sampleRateHz, numSamples, frequencyHz);
    }

    std::vector<float> renderFrequencyTraceForTests(int numSamples)
    {
        std::vector<float> trace;
        if (numSamples <= 0)
        {
            return trace;
        }

        trace.reserve(static_cast<size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            mVoice.advanceSubDriftAndFrequencyTraceStep(sampleRateHz);
            trace.push_back(mVoice.getSubFrequencyHz());
        }

        return trace;
    }

private:
    static float rateIndexToMultiplier(int index) noexcept
    {
        constexpr float multipliers[] { 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 16.0f };
        return multipliers[juce::jlimit(0, 9, index)];
    }

    double getGatePhaseIncrement() const noexcept
    {
        return ((mCurrentBpm / 60.0) * static_cast<double>(mSubdivisionMultiplier)) / sampleRateHz;
    }

    double getBlockStartPulsePhase() const noexcept
    {
        if (mHasHostPpq)
        {
            const double hostPhase = std::fmod(mCurrentPpqPosition * static_cast<double>(mSubdivisionMultiplier), 1.0);
            const double diff = std::abs(hostPhase - mPulsePhase);
            const double wrappedDiff = juce::jmin(diff, 1.0 - diff);
            if (wrappedDiff < phaseLockThreshold)
            {
                return mPulsePhase;
            }

            return hostPhase;
        }

        return mPulsePhase;
    }

    float getGateTarget(double pulsePhase) const noexcept
    {
        if (! mIsTransportPlaying)
        {
            return 1.0f;
        }

        if (mMotif.currentStepSkipped)
        {
            return 0.0f;
        }

        const float squareGate = pulsePhase < static_cast<double>(mGateDutyCycle) ? 1.0f : 0.0f;
        const double onsetPhaseWidth = juce::jmax(1.0e-9, 4.0 * getGatePhaseIncrement());
        if (pulsePhase < onsetPhaseWidth)
        {
            return squareGate;
        }

        const float phase = static_cast<float>(pulsePhase);
        const float sineSwell = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);
        const float girthMorph = juce::jlimit(0.0f, 1.0f, mGirth);
        return juce::jmap(girthMorph, squareGate, sineSwell);
    }

    float advanceGate(double pulsePhase)
    {
        float target = getGateTarget(pulsePhase);
        if (mGateRetriggerDipSamplesRemaining > 0)
        {
            target = 0.0f;
            --mGateRetriggerDipSamplesRemaining;
        }

        return gateSlew.process(target);
    }

    double sampleRateHz { 44100.0 };
    float mMasterGain { 0.5f };
    float mGirth { 0.0f };
    float mPulse { 0.5f };
    float mGateDutyCycle { 0.55f };
    float mSubdivisionMultiplier { 4.0f };
    int mRateIndex { 6 };
    bool mIsNoteSustaining { false };

    double mCurrentBpm { 120.0 };
    double mCurrentPpqPosition { 0.0 };
    double mPulsePhase { 0.0 };
    bool mIsTransportPlaying { false };
    bool mHasHostPpq { false };
    float mSkipProbability { 0.2f };

    SlewLimiter gateSlew;
    static constexpr double macroSmoothingSeconds = 0.01;
    static constexpr double brakeSmootherSeconds = 0.05;
    static constexpr double phaseLockThreshold = 0.01;
    static constexpr float minGateSlewMs = 1.0f;
    static constexpr float gateRetriggerDipSeconds = 0.001f;
    int mGateRetriggerDipSamplesRemaining { 0 };

    MotifEngine mMotif;
    VoiceBank mVoice;
    BrakePhysics mBrake;
    SignalProcessor mSignal;

#if JUCE_DEBUG
    uint64_t mGlobalAudioSampleCounterForDbg { 0 };
    uint64_t mDbgLastOnsetSampleIndex { 0 };
    uint32_t mDbgOnsetLogCounter { 0 };
    bool mDbgHadPreviousOnset { false };
#endif
};
