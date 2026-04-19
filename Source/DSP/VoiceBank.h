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
    float mReleaseMultiplierPerSample { 1.0f };
    float mRisePerSample { 1.0f };
    float mThumpSemitones { 0.0f };
    float mThumpDecayMultiplierPerSample { 1.0f };
    float mGirthDrift { 0.0f };

    juce::LinearSmoothedValue<float> mCurrentFrequency;
    juce::LinearSmoothedValue<float> mMidARatioSmoothed;
    juce::LinearSmoothedValue<float> mMidBRatioSmoothed;
    juce::LinearSmoothedValue<float> mShapeSmoothed;

    std::array<double, 3> mSubDriftLfoTheta {};

    Oscillator subOsc;
    Oscillator midOscA;
    Oscillator midOscB;

    static float applyKinkedMacroTaper(float x) noexcept
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, x);
        if (clamped <= 0.5f)
        {
            return 2.0f * clamped * clamped;
        }

        return juce::jlimit(0.0f, 1.0f, 0.5f + (clamped - 0.5f) * 1.35f);
    }

    static constexpr double glideTimeSeconds = 0.025;
    static constexpr double macroSmoothingSeconds = 0.01;
    static constexpr float thumpDurationSeconds = 0.045f;
    static constexpr float thumpSemitoneFresh = 18.0f;
    static constexpr float thumpSemitoneLegato = 18.0f;
    static constexpr float thumpSemitoneFloor = 1.0e-9f;
    static constexpr float subDriftMaxCents = 20.0f;
    static constexpr std::array<float, 3> subDriftLfoHz { 0.31f, 0.73f, 1.13f };
    static constexpr float attackTimeSeconds = 0.005f;
    static constexpr float noteEnvelopeSilenceThreshold = 1.0e-6f;

    void prepare(double sampleRateHz, float shapeIn, float harmonyIn, float girthIn)
    {
        harmony = juce::jlimit(0.0f, 1.0f, harmonyIn);
        shape = juce::jlimit(0.0f, 1.0f, shapeIn);
        mGirthDrift = juce::jlimit(0.0f, 1.0f, girthIn);

        mRisePerSample = 1.0f / juce::jmax(1.0f, static_cast<float>(sampleRateHz * attackTimeSeconds));
        updateThumpDecayCoefficient(sampleRateHz);

        subOsc.prepare(sampleRateHz);
        midOscA.prepare(sampleRateHz);
        midOscB.prepare(sampleRateHz);

        updateFrequencyRatios();
        mCurrentFrequency.reset(sampleRateHz, glideTimeSeconds);
        mCurrentFrequency.setCurrentAndTargetValue(mBaseFrequencyHz);
        mMidARatioSmoothed.reset(sampleRateHz, glideTimeSeconds);
        mMidBRatioSmoothed.reset(sampleRateHz, glideTimeSeconds);
        mMidARatioSmoothed.setCurrentAndTargetValue(mMidARatioTarget);
        mMidBRatioSmoothed.setCurrentAndTargetValue(mMidBRatioTarget);
        mCurrentMidARatio = mMidARatioSmoothed.getCurrentValue();
        mCurrentMidBRatio = mMidBRatioSmoothed.getCurrentValue();
        applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(mCurrentFrequency.getCurrentValue()));

        mShapeSmoothed.reset(sampleRateHz, macroSmoothingSeconds);
        mShapeSmoothed.setCurrentAndTargetValue(applyKinkedMacroTaper(shape));
        mShapedShape = mShapeSmoothed.getCurrentValue();
        subOsc.setShape(mShapedShape);
        midOscA.setShape(mShapedShape);
        midOscB.setShape(mShapedShape);

        mSubDriftLfoTheta = {};
        mThumpSemitones = 0.0f;
    }

    void setHarmony(float newHarmony)
    {
        harmony = juce::jlimit(0.0f, 1.0f, newHarmony);
        updateFrequencyRatios();
        applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(mCurrentFrequency.getCurrentValue()));
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
        const float pulsePeriodMs = computePulsePeriodMs(currentBpm, subdivisionMultiplier);
        const float periodMinusHardSilenceMs = juce::jmax(0.0f, pulsePeriodMs - adaptivePulseHardSilenceMs);
        const float maxAllowableReleaseMs = juce::jmin(
            adaptiveReleaseMaxPeriodFraction * pulsePeriodMs,
            periodMinusHardSilenceMs);

        const float noteClamped = juce::jlimit(12.0f, 127.0f, mActiveMidiNote);
        const float stretch = juce::jmap(noteClamped, 16.0f, 96.0f, 1.55f, 0.88f);
        const float releaseMs = static_cast<float>(releaseTauBaseSeconds * stretch * 1000.0);

        const float effectiveCeiling = juce::jmax(minimumAdaptiveReleaseTauMs, maxAllowableReleaseMs);
        const float effectiveReleaseMs = std::min(releaseMs, effectiveCeiling);

        const double tauSec = static_cast<double>(effectiveReleaseMs) * 0.001;
        mReleaseMultiplierPerSample = static_cast<float>(std::exp(-1.0 / (sampleRateHz * juce::jmax(1.0e-6, tauSec))));
    }

    void noteOn(int midiNoteNumber, float velocity, bool legatoRetrigger, bool voiceAlreadyActive,
                double hostSampleRateHz, double currentBpm, float subdivisionMultiplier)
    {
        if (! legatoRetrigger)
        {
            mNoteGainEnvelope = 0.0f;
        }

        const float noteFrequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
        mBaseFrequencyHz = juce::jlimit(1.0f, 20000.0f, noteFrequency);
        mActiveMidiNote = static_cast<float>(midiNoteNumber);
        mVelocity = juce::jlimit(0.0f, 1.0f, velocity);

        if (voiceAlreadyActive)
        {
            mCurrentFrequency.setTargetValue(mBaseFrequencyHz);
        }
        else
        {
            mCurrentFrequency.setCurrentAndTargetValue(mBaseFrequencyHz);
            if (mNoteGainEnvelope <= 0.0f)
            {
                subOsc.resetPhase();
                midOscA.resetPhase();
                midOscB.resetPhase();
            }
        }

        if (legatoRetrigger)
        {
            mThumpSemitones = juce::jmax(mThumpSemitones, thumpSemitoneLegato);
        }
        else
        {
            mThumpSemitones = thumpSemitoneFresh;
        }

        updateReleaseEnvelopeCoefficient(hostSampleRateHz, currentBpm, subdivisionMultiplier);
        updateFrequencyRatios();
        applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(mCurrentFrequency.getCurrentValue()));

        subOsc.setActive(true);
        midOscA.setActive(true);
        midOscB.setActive(true);
    }

    void advanceSubDriftLfos(double sampleRateHz) noexcept
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        for (size_t i = 0; i < subDriftLfoHz.size(); ++i)
        {
            mSubDriftLfoTheta[i] += twoPi * static_cast<double>(subDriftLfoHz[i]) / sampleRateHz;
            while (mSubDriftLfoTheta[i] >= twoPi)
            {
                mSubDriftLfoTheta[i] -= twoPi;
            }
        }
    }

    void tickAmplitudeEnvelope(bool isNoteSustaining)
    {
        if (isNoteSustaining)
        {
            mNoteGainEnvelope = juce::jmin(1.0f, mNoteGainEnvelope + mRisePerSample);
        }
        else if (mNoteGainEnvelope > 0.0f)
        {
            mNoteGainEnvelope *= mReleaseMultiplierPerSample;
            if (mNoteGainEnvelope < noteEnvelopeSilenceThreshold)
            {
                mNoteGainEnvelope = 0.0f;
                mVelocity = 0.0f;
                subOsc.setActive(false);
                midOscA.setActive(false);
                midOscB.setActive(false);
            }
        }
    }

    void tickPitchGlideAndOrganicFrequencies()
    {
        const float currentBaseFrequency = mCurrentFrequency.getNextValue();
        mCurrentMidARatio = mMidARatioSmoothed.getNextValue();
        mCurrentMidBRatio = mMidBRatioSmoothed.getNextValue();
        applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(currentBaseFrequency));
        advanceThumpSemitoneDecay();
    }

    void advanceShapeSmoothedAndApplyToOscillators() noexcept
    {
        mShapedShape = mShapeSmoothed.getNextValue();
        subOsc.setShape(mShapedShape);
        midOscA.setShape(mShapedShape);
        midOscB.setShape(mShapedShape);
    }

    void sampleOscillators(float& sub, float& midA, float& midB) noexcept
    {
        sub = subOsc.getNextSample() * 0.6f;
        midA = midOscA.getNextSample() * 0.2f;
        midB = midOscB.getNextSample() * 0.2f;
    }

    void advanceSubDriftAndFrequencyTraceStep(double sampleRateHz)
    {
        advanceSubDriftLfos(sampleRateHz);
        tickPitchGlideAndOrganicFrequencies();
    }

    std::array<double, 3> getChildSampleRatesForTests() const noexcept
    {
        return {
            subOsc.getSampleRateForTests(),
            midOscA.getSampleRateForTests(),
            midOscB.getSampleRateForTests()
        };
    }

    std::array<float, 3> getCurrentFrequenciesForTests() const noexcept
    {
        return { mSubFrequencyHz, mMidAFrequencyHz, mMidBFrequencyHz };
    }

    std::array<double, 3> getOscillatorPhasesForTests() const noexcept
    {
        return { subOsc.getPhaseForTests(), midOscA.getPhaseForTests(), midOscB.getPhaseForTests() };
    }

private:
    static constexpr float releaseTauBaseSeconds = 0.035f;
    static constexpr float adaptiveReleaseMaxPeriodFraction = 0.8f;
    static constexpr float adaptivePulseHardSilenceMs = 5.0f;
    static constexpr float minimumAdaptiveReleaseTauMs = 0.05f;

    static float computePulsePeriodMs(double currentBpm, float subdivisionMultiplier) noexcept
    {
        const double pulsesPerSecond = (currentBpm / 60.0) * static_cast<double>(subdivisionMultiplier);
        if (pulsesPerSecond <= 1.0e-9)
        {
            return 60000.0f;
        }

        return static_cast<float>(1000.0 / pulsesPerSecond);
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

    void applyCurrentBaseFrequency(float baseFrequency)
    {
        mSubFrequencyHz = baseFrequency;
        const float driftRatio = getSubDriftFrequencyRatio();
        mMidAFrequencyHz = baseFrequency * mCurrentMidARatio * driftRatio;
        mMidBFrequencyHz = baseFrequency * mCurrentMidBRatio * driftRatio;

        subOsc.setFrequency(mSubFrequencyHz);
        midOscA.setFrequency(mMidAFrequencyHz);
        midOscB.setFrequency(mMidBFrequencyHz);
    }

    float baseFrequencyWithThumpOffset(float baseFrequencyHzIn) const noexcept
    {
        return baseFrequencyHzIn * std::pow(2.0f, mThumpSemitones / 12.0f);
    }

    void advanceThumpSemitoneDecay() noexcept
    {
        if (mThumpSemitones <= 0.0f)
        {
            return;
        }

        mThumpSemitones *= mThumpDecayMultiplierPerSample;
        if (mThumpSemitones < thumpSemitoneFloor)
        {
            mThumpSemitones = 0.0f;
        }
    }

    void updateThumpDecayCoefficient(double sampleRateHz) noexcept
    {
        const double n = static_cast<double>(thumpDurationSeconds) * sampleRateHz;
        mThumpDecayMultiplierPerSample = static_cast<float>(std::exp(std::log(static_cast<double>(thumpSemitoneFloor)) / n));
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
};
