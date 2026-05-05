#pragma once

#include <array>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Oscillator.h"

class VoiceBank {
public:
    float harmony { 0.0f };
    float shape { 0.0f };
    float mShapedShape { 0.0f };
    float mBaseFrequencyHz { 55.0f };
    float mVelocity { 0.0f };
    float mActiveMidiNote { 36.0f };
    float mMidARatioTarget { 2.0f };
    float mMidBRatioTarget { 4.0f };
    float mCurrentMidARatio { 2.0f };
    float mCurrentMidBRatio { 4.0f };
    float mSubFrequencyHz { 55.0f };
    float mMidAFrequencyHz { 110.0f };
    float mMidBFrequencyHz { 220.0f };
    float mNoteGainEnvelope { 0.0f };
    float mAttackStepPerSample { 1.0f };
    float mReleaseStepPerSample { 1.0f };
    float mDynamicReleaseSeconds { 0.005f };
    float mGirthDrift { 0.0f };

    juce::LinearSmoothedValue<float> mMidARatioSmoothed;
    juce::LinearSmoothedValue<float> mMidBRatioSmoothed;
    juce::LinearSmoothedValue<float> mShapeSmoothed;
    juce::LinearSmoothedValue<float> mBaseFrequencySmoothed;

    std::array<double, 3> mSubDriftLfoTheta {};

    static float applyKinkedMacroTaper(float x) noexcept
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, x);
        if (clamped <= 0.5f)
            return 2.0f * clamped * clamped;
        return juce::jlimit(0.0f, 1.0f, 0.5f + (clamped - 0.5f) * 1.35f);
    }

    static constexpr double glideTimeSeconds = 0.025;
    static constexpr double macroSmoothingSeconds = 0.01;
    static constexpr float subDriftMaxCents = 20.0f;
    static constexpr std::array<float, 3> subDriftLfoHz { 0.31f, 0.73f, 1.13f };
    static constexpr float attackTimeSeconds = 0.002f;
    static constexpr float minReleaseTimeSeconds = 0.002f;
    static constexpr float noteEnvelopeSilenceThreshold = 1.0e-6f;

    void prepare(double sampleRateHz, float shapeIn, float harmonyIn, float girthIn)
    {
        mPreparedSampleRateHz = juce::jmax(1.0, sampleRateHz);
        harmony = juce::jlimit(0.0f, 1.0f, harmonyIn);
        shape = juce::jlimit(0.0f, 1.0f, shapeIn);
        mGirthDrift = juce::jlimit(0.0f, 1.0f, girthIn);

        mAttackStepPerSample = 1.0f / juce::jmax(1.0f, static_cast<float>(mPreparedSampleRateHz * attackTimeSeconds));
        updateReleaseEnvelopeCoefficient(mPreparedSampleRateHz, 120.0, 4.0f);

        activeVoice.sub.prepare(mPreparedSampleRateHz);
        activeVoice.midA.prepare(mPreparedSampleRateHz);
        activeVoice.midB.prepare(mPreparedSampleRateHz);
        shadowVoice.sub.prepare(mPreparedSampleRateHz);
        shadowVoice.midA.prepare(mPreparedSampleRateHz);
        shadowVoice.midB.prepare(mPreparedSampleRateHz);

        updateFrequencyRatios();
        mMidARatioSmoothed.reset(mPreparedSampleRateHz, glideTimeSeconds);
        mMidBRatioSmoothed.reset(mPreparedSampleRateHz, glideTimeSeconds);
        mBaseFrequencySmoothed.reset(mPreparedSampleRateHz, glideTimeSeconds);
        mMidARatioSmoothed.setCurrentAndTargetValue(mMidARatioTarget);
        mMidBRatioSmoothed.setCurrentAndTargetValue(mMidBRatioTarget);
        mBaseFrequencySmoothed.setCurrentAndTargetValue(mBaseFrequencyHz);
        mCurrentMidARatio = mMidARatioSmoothed.getCurrentValue();
        mCurrentMidBRatio = mMidBRatioSmoothed.getCurrentValue();

        initVoice(activeVoice, mBaseFrequencyHz, 0.0f);
        initVoice(shadowVoice, mBaseFrequencyHz, 0.0f);
        activeVoice.active = false;
        shadowVoice.active = false;

        mShapeSmoothed.reset(mPreparedSampleRateHz, macroSmoothingSeconds);
        mShapeSmoothed.setCurrentAndTargetValue(applyKinkedMacroTaper(shape));
        mShapedShape = mShapeSmoothed.getCurrentValue();
        applyShapeToVoice(activeVoice);
        applyShapeToVoice(shadowVoice);

        mSubDriftLfoTheta = {};
        mVelocity = 0.0f;
        mNoteGainEnvelope = 0.0f;
    }

    void setHarmony(float newHarmony)
    {
        harmony = juce::jlimit(0.0f, 1.0f, newHarmony);
        updateFrequencyRatios();
        updateVoiceFrequencies(activeVoice);
        updateVoiceFrequencies(shadowVoice);
    }

    void setShape(float newShape)
    {
        shape = juce::jlimit(0.0f, 1.0f, newShape);
        mShapeSmoothed.setTargetValue(applyKinkedMacroTaper(shape));
    }

    void setGirthForDrift(float newGirth) noexcept
    {
        mGirthDrift = juce::jlimit(0.0f, 1.0f, newGirth);
    }

    float getSubFrequencyHz() const noexcept
    {
        return mSubFrequencyHz;
    }

    void updateReleaseEnvelopeCoefficient(double sampleRateHz, double currentBpm, float subdivisionMultiplier)
    {
        juce::ignoreUnused(currentBpm, subdivisionMultiplier);
        mPreparedSampleRateHz = juce::jmax(1.0, sampleRateHz);
        updateDynamicReleaseFromFrequency(static_cast<float>(mPreparedSampleRateHz),
                                          activeVoice.active ? activeVoice.baseFrequencyHz : mBaseFrequencyHz);
    }

    void noteOn(int midiNoteNumber, float velocity, bool legatoRetrigger, bool voiceAlreadyActive,
                double hostSampleRateHz, double currentBpm, float subdivisionMultiplier)
    {
        juce::ignoreUnused(legatoRetrigger, voiceAlreadyActive, currentBpm, subdivisionMultiplier);

        if (activeVoice.active && activeVoice.envelope > noteEnvelopeSilenceThreshold)
        {
            shadowVoice = activeVoice;
            shadowVoice.releasing = true;
            shadowVoice.attacking = false;
            shadowVoice.releaseProgress = 0.0f;
            shadowVoice.releaseStartLevel = juce::jlimit(0.0f, 1.0f, shadowVoice.envelope);
            shadowVoice.active = true;
        }

        const float noteFrequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
        mBaseFrequencyHz = juce::jlimit(1.0f, 20000.0f, noteFrequency);
        if (activeVoice.active || shadowVoice.active)
            mBaseFrequencySmoothed.setTargetValue(mBaseFrequencyHz);
        else
            mBaseFrequencySmoothed.setCurrentAndTargetValue(mBaseFrequencyHz);
        mPreparedSampleRateHz = juce::jmax(1.0, hostSampleRateHz);
        updateDynamicReleaseFromFrequency(static_cast<float>(mPreparedSampleRateHz), mBaseFrequencyHz);
        mActiveMidiNote = static_cast<float>(midiNoteNumber);

        initVoice(activeVoice, mBaseFrequencySmoothed.getCurrentValue(), juce::jlimit(0.0f, 1.0f, velocity));
        activeVoice.active = true;
        activeVoice.attacking = true;
        activeVoice.releasing = false;

        updateVoiceFrequencies(activeVoice);
        activeVoice.sub.setActive(true);
        activeVoice.midA.setActive(true);
        activeVoice.midB.setActive(true);

        mVelocity = 1.0f;
        mNoteGainEnvelope = 1.0f;
    }

    void advanceSubDriftLfos(double sampleRateHz) noexcept
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        for (size_t i = 0; i < subDriftLfoHz.size(); ++i)
        {
            mSubDriftLfoTheta[i] += twoPi * static_cast<double>(subDriftLfoHz[i]) / sampleRateHz;
            while (mSubDriftLfoTheta[i] >= twoPi)
                mSubDriftLfoTheta[i] -= twoPi;
        }
    }

    void tickAmplitudeEnvelope(bool isNoteSustaining)
    {
        if (activeVoice.active && ! isNoteSustaining && ! activeVoice.releasing)
        {
            activeVoice.releasing = true;
            activeVoice.attacking = false;
            activeVoice.releaseProgress = 0.0f;
            activeVoice.releaseStartLevel = juce::jlimit(0.0f, 1.0f, activeVoice.envelope);
        }

        updateVoiceEnvelope(activeVoice);
        updateVoiceEnvelope(shadowVoice);

        const float activeEnv = activeVoice.active ? juce::jmax(0.0f, activeVoice.envelope) : 0.0f;
        const float shadowEnv = shadowVoice.active ? juce::jmax(0.0f, shadowVoice.envelope) : 0.0f;
        mNoteGainEnvelope = juce::jmax(activeEnv, shadowEnv);

        if (mNoteGainEnvelope <= noteEnvelopeSilenceThreshold)
        {
            mNoteGainEnvelope = 0.0f;
            forceDeactivateVoice(activeVoice);
            forceDeactivateVoice(shadowVoice);
            mVelocity = 0.0f;
        }
        else
        {
            mVelocity = 1.0f;
        }
    }

    void tickPitchGlideAndOrganicFrequencies()
    {
        const float baseHz = mBaseFrequencySmoothed.getNextValue();
        if (activeVoice.active)
            activeVoice.baseFrequencyHz = baseHz;
        mCurrentMidARatio = mMidARatioSmoothed.getNextValue();
        mCurrentMidBRatio = mMidBRatioSmoothed.getNextValue();
        updateVoiceFrequencies(activeVoice);
        updateVoiceFrequencies(shadowVoice);
    }

    void advanceShapeSmoothedAndApplyToOscillators() noexcept
    {
        mShapedShape = mShapeSmoothed.getNextValue();
        applyShapeToVoice(activeVoice);
        applyShapeToVoice(shadowVoice);
    }

    void sampleOscillators(float& sub, float& midA, float& midB) noexcept
    {
        sub = 0.0f;
        midA = 0.0f;
        midB = 0.0f;

        if (mNoteGainEnvelope <= noteEnvelopeSilenceThreshold)
            return;

        sampleVoice(activeVoice, sub, midA, midB);
        sampleVoice(shadowVoice, sub, midA, midB);
    }

    void advanceSubDriftAndFrequencyTraceStep(double sampleRateHz)
    {
        advanceSubDriftLfos(sampleRateHz);
        tickPitchGlideAndOrganicFrequencies();
    }

    std::array<double, 3> getChildSampleRatesForTests() const noexcept
    {
        return {
            activeVoice.sub.getSampleRateForTests(),
            activeVoice.midA.getSampleRateForTests(),
            activeVoice.midB.getSampleRateForTests()
        };
    }

    std::array<float, 3> getCurrentFrequenciesForTests() const noexcept
    {
        return { mSubFrequencyHz, mMidAFrequencyHz, mMidBFrequencyHz };
    }

    std::array<double, 3> getOscillatorPhasesForTests() const noexcept
    {
        return {
            activeVoice.sub.getPhaseForTests(),
            activeVoice.midA.getPhaseForTests(),
            activeVoice.midB.getPhaseForTests()
        };
    }

private:
    static float cosineSCurve(float progress) noexcept
    {
        const float p = juce::jlimit(0.0f, 1.0f, progress);
        return 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * p));
    }

    struct VoiceSlot
    {
        Oscillator sub;
        Oscillator midA;
        Oscillator midB;
        float baseFrequencyHz { 55.0f };
        float velocity { 0.0f };
        float envelope { 0.0f };
        float attackProgress { 0.0f };
        float releaseProgress { 0.0f };
        float releaseStartLevel { 0.0f };
        bool active { false };
        bool attacking { false };
        bool releasing { false };
    };

    VoiceSlot activeVoice;
    VoiceSlot shadowVoice;

    void initVoice(VoiceSlot& voice, float baseFrequencyHz, float velocity) noexcept
    {
        voice.baseFrequencyHz = baseFrequencyHz;
        voice.velocity = velocity;
        voice.envelope = 0.0f;
        voice.attackProgress = 0.0f;
        voice.releaseProgress = 0.0f;
        voice.releaseStartLevel = 0.0f;
    }

    void applyShapeToVoice(VoiceSlot& voice) noexcept
    {
        voice.sub.setShape(mShapedShape);
        voice.midA.setShape(mShapedShape);
        voice.midB.setShape(mShapedShape);
    }

    void updateVoiceEnvelope(VoiceSlot& voice) noexcept
    {
        if (! voice.active)
            return;

        if (voice.attacking)
        {
            voice.attackProgress = juce::jmin(1.0f, voice.attackProgress + mAttackStepPerSample);
            voice.envelope = cosineSCurve(voice.attackProgress);
            if (voice.attackProgress >= 1.0f)
            {
                voice.envelope = 1.0f;
                voice.attacking = false;
            }
        }
        else if (voice.releasing)
        {
            voice.releaseProgress = juce::jmin(1.0f, voice.releaseProgress + mReleaseStepPerSample);
            voice.envelope = voice.releaseStartLevel * (1.0f - cosineSCurve(voice.releaseProgress));
            if (voice.releaseProgress >= 1.0f || voice.envelope <= noteEnvelopeSilenceThreshold)
            {
                voice.envelope = 0.0f;
                voice.active = false;
                voice.releasing = false;
                voice.attacking = false;
                voice.sub.setActive(false);
                voice.midA.setActive(false);
                voice.midB.setActive(false);
            }
        }
    }

    void updateFrequencyRatios()
    {
        mMidARatioTarget = (harmony <= 0.5f)
            ? juce::jmap(harmony, 0.0f, 0.5f, 2.0f, 3.0f)
            : juce::jmap(harmony, 0.5f, 1.0f, 3.0f, 2.137f);
        mMidBRatioTarget = (harmony <= 0.5f)
            ? 4.0f
            : juce::jmap(harmony, 0.5f, 1.0f, 4.0f, 3.1415f);
        mMidARatioSmoothed.setTargetValue(mMidARatioTarget);
        mMidBRatioSmoothed.setTargetValue(mMidBRatioTarget);
    }

    void updateVoiceFrequencies(VoiceSlot& voice) noexcept
    {
        if (! voice.active)
            return;

        const float driftRatio = getSubDriftFrequencyRatio();
        const float subHz = voice.baseFrequencyHz;
        const float midAHz = voice.baseFrequencyHz * mCurrentMidARatio * driftRatio;
        const float midBHz = voice.baseFrequencyHz * mCurrentMidBRatio * driftRatio;

        voice.sub.setFrequency(subHz);
        voice.midA.setFrequency(midAHz);
        voice.midB.setFrequency(midBHz);

        if (&voice == &activeVoice)
        {
            mSubFrequencyHz = subHz;
            mMidAFrequencyHz = midAHz;
            mMidBFrequencyHz = midBHz;
            updateDynamicReleaseFromFrequency(static_cast<float>(mPreparedSampleRateHz), voice.baseFrequencyHz);
        }
    }

    static void sampleVoice(VoiceSlot& voice, float& sub, float& midA, float& midB) noexcept
    {
        if (! voice.active)
            return;

        const float gain = voice.envelope * voice.velocity;
        if (gain <= noteEnvelopeSilenceThreshold)
            return;

        sub += voice.sub.getNextSample() * 0.6f * gain;
        midA += voice.midA.getNextSample() * 0.2f * gain;
        midB += voice.midB.getNextSample() * 0.2f * gain;
    }

    float getSubDriftFrequencyRatio() const noexcept
    {
        const float s0 = static_cast<float>(std::sin(mSubDriftLfoTheta[0]));
        const float s1 = static_cast<float>(std::sin(mSubDriftLfoTheta[1]));
        const float s2 = static_cast<float>(std::sin(mSubDriftLfoTheta[2]));
        const float sumNorm = (s0 + s1 + s2) * (1.0f / 3.0f);
        const float cents = mGirthDrift * subDriftMaxCents * sumNorm;
        return std::pow(2.0f, cents / 1200.0f);
    }

    void updateDynamicReleaseFromFrequency(float sampleRateHz, float currentFrequencyHz) noexcept
    {
        const float safeHz = juce::jmax(1.0f, currentFrequencyHz);
        const float minReleaseFromPeriod = 1.5f / safeHz;
        mDynamicReleaseSeconds = juce::jmax(minReleaseTimeSeconds, minReleaseFromPeriod);
        mReleaseStepPerSample = 1.0f / juce::jmax(1.0f, sampleRateHz * mDynamicReleaseSeconds);
    }

    static void forceDeactivateVoice(VoiceSlot& voice) noexcept
    {
        voice.envelope = 0.0f;
        voice.attackProgress = 0.0f;
        voice.releaseProgress = 0.0f;
        voice.releaseStartLevel = 0.0f;
        voice.active = false;
        voice.releasing = false;
        voice.attacking = false;
        voice.sub.setActive(false);
        voice.midA.setActive(false);
        voice.midB.setActive(false);
    }

    double mPreparedSampleRateHz { 44100.0 };
};
