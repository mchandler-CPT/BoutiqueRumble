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
        juce::dsp::ProcessSpec spec {};
        spec.sampleRate = sampleRateHz;
        spec.maximumBlockSize = 2048;
        spec.numChannels = 1;
        for (auto& filt : mSculptFilter)
        {
            filt.reset();
            filt.prepare(spec);
            filt.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            filt.setCutoffFrequency(mCutoffHz);
            filt.setResonance(mResonanceQ);
        }
        mCutoffSmoothed.reset(sampleRateHz, sculptSmoothingSeconds);
        mResoSmoothed.reset(sampleRateHz, sculptSmoothingSeconds);
        mCutoffSmoothed.setCurrentAndTargetValue(mCutoffHz);
        mResoSmoothed.setCurrentAndTargetValue(mResonanceQ);
        mCurrentCutoffSmoothed = mCutoffHz;
        mCurrentResoSmoothed = mResonanceQ;
        mMotif.prepareState();
        mGateRetriggerDipSamplesRemaining = 0;
        mBrake.prepare(sampleRateHz, brakeSmootherSeconds);
        mBrakeFreeRunningPulseIndex = -1;
        mBrakeHiccupThisPulse = false;
        mSculptTailHoldoffSamples = juce::jmax(1, juce::roundToInt(sampleRateHz * sculptTailHoldoffSeconds));
        mSculptTailHoldoffCounter = 0;
        noteOff();
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

        const float slewMs = (mPulse <= 1.0e-5f)
            ? minGateSlewMs
            : juce::jmap(mPulse, 0.0f, 1.0f, minGateSlewMs, 35.0f);
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

    void setCutoff(float newCutoffHz) noexcept
    {
        mCutoffHz = juce::jlimit(20.0f, 20000.0f, newCutoffHz);
        mCutoffSmoothed.setTargetValue(mCutoffHz);
    }

    void setResonance(float newQ) noexcept
    {
        mResonanceQ = juce::jlimit(0.1f, 20.0f, newQ);
        mResoSmoothed.setTargetValue(mResonanceQ);
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
            mCurrentCutoffSmoothed = mCutoffSmoothed.getNextValue();
            mCurrentResoSmoothed = mResoSmoothed.getNextValue();
            if ((sample & (sculptCoeffUpdateStride - 1)) == 0)
            {
                for (auto& filt : mSculptFilter)
                {
                    filt.setCutoffFrequency(mCurrentCutoffSmoothed);
                    filt.setResonance(mCurrentResoSmoothed);
                }
            }
            mVoice.advanceSubDriftLfos(sampleRateHz);

            const float breakAmount = mBrake.next(sampleRateHz);

            mVoice.tickAmplitudeEnvelope(mIsNoteSustaining);

            mVoice.tickPitchGlideAndOrganicFrequencies();
            mVoice.advanceShapeSmoothedAndApplyToOscillators();
            mSignal.advanceGritSmoothed();

            float sub = 0.0f;
            float midA = 0.0f;
            float midB = 0.0f;
            mVoice.sampleOscillators(sub, midA, midB);

            const float pulseGateLfo = advanceGate(pulsePhase, breakAmount);
            const float pulseGate = pulseGateLfo;

            const float subMono = sub * mMasterGain * mVoice.mVelocity * pulseGate;
            const float midMono = (midA + midB) * mMasterGain * mVoice.mVelocity * pulseGate;

            const float shredT = juce::jlimit(0.0f, 1.0f, breakAmount);
            const float torqueFault = breakAmount * 0.55f + shredT * shredT * 0.15f;
            const float baseFaultAmount = juce::jlimit(0.0f, 1.0f, mSignal.mShapedGrit); // Smoothed via LinearSmoothedValue.
            const float faultAmount = juce::jmin(1.0f, baseFaultAmount + torqueFault);

            float leftOut = midMono;
            if (faultAmount <= 1.0e-5f) [[likely]]
            {
                // BOUTIQUE RULE: Zero means Zero. Bit-perfect bypass active.
                if (mFaultWasActive)
                {
                    mSignal.resetFaultEngine();
                    mFaultWasActive = false;
                }
            }
            else
            {
                const float savedShapedGrit = mSignal.mShapedGrit;
                mSignal.mShapedGrit = faultAmount;
                leftOut = mSignal.applyGritManifold(midMono);
                mSignal.mShapedGrit = savedShapedGrit;
                mFaultWasActive = true;
            }
            float rightOut = leftOut;
            mSignal.processMidHighSpatial(leftOut, rightOut, mGirth);
            leftOut = juce::jlimit(-1.0f, 1.0f, leftOut + subMono);
            rightOut = juce::jlimit(-1.0f, 1.0f, rightOut + subMono);
            leftOut *= mVoice.mNoteGainEnvelope;
            rightOut *= mVoice.mNoteGainEnvelope;

            leftOut = mSculptFilter[0].processSample(0, leftOut);
            rightOut = mSculptFilter[1].processSample(0, rightOut);
            const float resonanceNorm = juce::jlimit(0.0f, 1.0f, (mCurrentResoSmoothed - 0.1f) / 19.9f);
            const float sculptMakeup = 1.0f + (resonanceNorm * 0.05f);
            leftOut *= sculptMakeup;
            rightOut *= sculptMakeup;

            if (mVoice.mNoteGainEnvelope > 0.0f || mIsNoteSustaining)
            {
                mSculptTailHoldoffCounter = mSculptTailHoldoffSamples;
            }

            leftOut = mSignal.processSafety(0, leftOut);
            if (numOutputChannels > 1)
            {
                rightOut = mSignal.processSafety(1, rightOut);
            }

            if (mVoice.mNoteGainEnvelope <= 0.0f && ! mIsNoteSustaining)
            {
                if (mSculptTailHoldoffCounter > 0)
                {
                    --mSculptTailHoldoffCounter;
                }
                else
                {
                    const float tailMagnitude = juce::jmax(std::abs(leftOut), std::abs(rightOut));
                    if (tailMagnitude < sculptTailSilenceThreshold)
                    {
                        leftOut = 0.0f;
                        rightOut = 0.0f;
                        mSignal.resetSafety();
                        for (auto& filt : mSculptFilter)
                            filt.reset();
                    }
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
                pulsePhase -= std::floor(pulsePhase);
                refreshBrakeStumbleOnPulseWrap(breakAmount);
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
            float breakAmount = 0.0f;
            while (pulsePhase < 1.0)
            {
                breakAmount = mBrake.next(sampleRateHz);
                juce::ignoreUnused(advanceGate(pulsePhase, breakAmount));
                pulsePhase += gatePhaseIncrement;
            }

            pulsePhase -= std::floor(pulsePhase);
            refreshBrakeStumbleOnPulseWrap(breakAmount);
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
            const float breakAmount = mBrake.next(sampleRateHz);
            envelope.push_back(advanceGate(pulsePhase, breakAmount));
            pulsePhase += gatePhaseIncrement;
            if (pulsePhase >= 1.0)
            {
                pulsePhase -= std::floor(pulsePhase);
                refreshBrakeStumbleOnPulseWrap(breakAmount);
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

    static uint32_t stumbleHash(int64_t barKey, uint32_t pulseInBar, uint32_t midiNote) noexcept
    {
        uint64_t h = (uint64_t)barKey;
        h ^= (uint64_t)pulseInBar * 0xd6e8feb866afd25full;
        h ^= (uint64_t)midiNote * 0x9e3779b97f4a7c15ull;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ull;
        h ^= h >> 33;
        return (uint32_t)h;
    }

    void refreshBrakeStumbleOnPulseWrap(float breakAmount) noexcept
    {
        const int pulsesPerBar = juce::jmax(1, juce::roundToInt(4.0f * mSubdivisionMultiplier));
        int64_t globalPulse;
        if (mHasHostPpq && std::isfinite(mCurrentPpqPosition))
        {
            globalPulse = (int64_t)std::llround(mCurrentPpqPosition * static_cast<double>(mSubdivisionMultiplier));
        }
        else
        {
            ++mBrakeFreeRunningPulseIndex;
            globalPulse = mBrakeFreeRunningPulseIndex;
        }

        const uint32_t pulseInBar = (uint32_t)(globalPulse % pulsesPerBar);
        const int64_t barKey = globalPulse / pulsesPerBar;
        const int note = juce::jlimit(0, 127, juce::roundToInt(mVoice.mActiveMidiNote));

        const uint32_t seed = stumbleHash(barKey, pulseInBar, (uint32_t)note);
        const float chaos = juce::jlimit(0.0f, 1.0f, breakAmount);
        const float pHiccup = chaos * 0.21f;
        const float u = (float)(seed & 0xffffffu) * (1.0f / 16777216.0f);

        mBrakeHiccupThisPulse = (u < pHiccup);
    }

    uint32_t getFractalStutterHash() const noexcept
    {
        const int pulsesPerBar = juce::jmax(1, juce::roundToInt(4.0f * mSubdivisionMultiplier));
        int64_t globalPulse = 0;
        if (mHasHostPpq && std::isfinite(mCurrentPpqPosition))
        {
            globalPulse = (int64_t)std::llround(mCurrentPpqPosition * static_cast<double>(mSubdivisionMultiplier));
        }
        else
        {
            globalPulse = juce::jmax((int64_t)0, mBrakeFreeRunningPulseIndex);
        }

        const uint32_t pulseInBar = (uint32_t)(globalPulse % pulsesPerBar);
        const int64_t barKey = (globalPulse >= 0) ? (globalPulse / pulsesPerBar) : 0;
        const int note = juce::jlimit(0, 127, juce::roundToInt(mVoice.mActiveMidiNote));
        return stumbleHash(barKey, pulseInBar, (uint32_t)note);
    }

    // Additive thresher: phase-locked 16..128 chops per pulse (1/16..1/128 of pulse period),
    // Lerp toward a two-level ratchet that never hits 0.0 — SKIP alone silences motif-off steps.
    float fractalThresherGate(double pulsePhase, float breakAmount) const noexcept
    {
        const float t = juce::jlimit(0.0f, 1.0f, breakAmount);
        if (t <= 1.0e-5f)
        {
            return 1.0f;
        }

        const float blend = t * t;
        const uint32_t h = getFractalStutterHash();
        const int nCore = (int)std::lround(juce::jmap(t, 0.0f, 1.0f, 16.0f, 128.0f));
        const int nChops = juce::jlimit(16, 128, nCore + (int)(h & 3u));

        const double subPhase = std::fmod((double)pulsePhase * static_cast<double>(nChops), 1.0);
        static constexpr float kThresherHi = 0.985f;
        static constexpr float kThresherFloor = 0.54f;
        const float shredValley = juce::jmap(t, 0.0f, 1.0f, kThresherHi, kThresherFloor);
        const float pat = subPhase < 0.5 ? 1.0f : shredValley;

        return 1.0f + (pat - 1.0f) * blend;
    }

    float getGateTarget(double pulsePhase, float breakAmount) const noexcept
    {
        if (! mIsTransportPlaying)
        {
            return 1.0f;
        }

        if (! mMotif.bypass)
        {
            if (! mMotif.pattern[static_cast<size_t>(mMotif.stepIndex & 15)])
            {
                return 0.0f;
            }
        }

        const double onsetPhaseWidth = juce::jmax(1.0e-9, 4.0 * getGatePhaseIncrement());
        if (pulsePhase < onsetPhaseWidth)
        {
            const float squareGate = pulsePhase < static_cast<double>(mGateDutyCycle) ? 1.0f : 0.0f;
            return squareGate * fractalThresherGate(pulsePhase, breakAmount);
        }

        double phaseForDuty = pulsePhase;
        if (breakAmount > 0.0f)
        {
            const float warpExponent = 1.0f + breakAmount * 2.0f;
            phaseForDuty = std::pow((float)pulsePhase, warpExponent);
        }

        const float squareGate = phaseForDuty < static_cast<double>(mGateDutyCycle) ? 1.0f : 0.0f;
        const float phase = static_cast<float>(phaseForDuty);
        const float sineSwell = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);
        const float girthMorph = juce::jlimit(0.0f, 1.0f, mGirth);
        float out = juce::jmap(girthMorph, squareGate, sineSwell);

        if (mBrakeHiccupThisPulse)
        {
            constexpr double hiccupPhaseWidth = 1.0 / 64.0;
            if (pulsePhase < hiccupPhaseWidth)
            {
                out = juce::jmax(out, 1.0f);
            }
        }

        return out * fractalThresherGate(pulsePhase, breakAmount);
    }

    float advanceGate(double pulsePhase, float breakAmount)
    {
        float target = getGateTarget(pulsePhase, breakAmount);
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
    float mSkipProbability { 0.0f };

    SlewLimiter gateSlew;
    static constexpr double macroSmoothingSeconds = 0.01;
    static constexpr double brakeSmootherSeconds = 0.05;
    static constexpr double phaseLockThreshold = 0.01;
    static constexpr float minGateSlewMs = 0.1f;
    static constexpr float gateRetriggerDipSeconds = 0.001f;
    int mGateRetriggerDipSamplesRemaining { 0 };

    MotifEngine mMotif;
    VoiceBank mVoice;
    BrakePhysics mBrake;
    SignalProcessor mSignal;

    int64_t mBrakeFreeRunningPulseIndex { -1 };
    bool mBrakeHiccupThisPulse { false };
    bool mFaultWasActive { false };
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> mSculptFilter;
    float mCutoffHz { 20000.0f };
    float mResonanceQ { 0.707f };
    juce::LinearSmoothedValue<float> mCutoffSmoothed;
    juce::LinearSmoothedValue<float> mResoSmoothed;
    float mCurrentCutoffSmoothed { 20000.0f };
    float mCurrentResoSmoothed { 0.707f };
    static constexpr double sculptSmoothingSeconds = 0.03;
    static constexpr int sculptCoeffUpdateStride = 8;
    static constexpr double sculptTailHoldoffSeconds = 0.02;
    static constexpr float sculptTailSilenceThreshold = 1.0e-4f;
    int mSculptTailHoldoffSamples { 0 };
    int mSculptTailHoldoffCounter { 0 };

};
