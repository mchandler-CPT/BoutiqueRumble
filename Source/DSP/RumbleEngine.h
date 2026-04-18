#pragma once
#include <array>
#include <cmath>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "Oscillator.h"
#include "Utils/SlewLimiter.h"

class RumbleEngine {
public:
    RumbleEngine() = default;

    void prepare(double sampleRate)
    {
        sampleRateHz = juce::jmax(1.0, sampleRate);
        mReleasePerSample = 1.0f / juce::jmax(1.0f, static_cast<float>(sampleRateHz * releaseTimeSeconds));
        mRisePerSample = 1.0f / juce::jmax(1.0f, static_cast<float>(sampleRateHz * releaseTimeSeconds));

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
        applyCurrentBaseFrequency(mCurrentFrequency.getCurrentValue());
        mShapeSmoothed.reset(sampleRateHz, macroSmoothingSeconds);
        mShapeSmoothed.setCurrentAndTargetValue(applyKinkedMacroTaper(shape));
        mGritSmoothed.reset(sampleRateHz, macroSmoothingSeconds);
        mGritSmoothed.setCurrentAndTargetValue(applyKinkedMacroTaper(mGrit));
        mShapedShape = mShapeSmoothed.getCurrentValue();
        mShapedGrit = mGritSmoothed.getCurrentValue();
        mEntropy = mShapedGrit;
        subOsc.setShape(mShapedShape);
        midOscA.setShape(mShapedShape);
        midOscB.setShape(mShapedShape);
        setPulse(mPulse);
        gateSlew.prepare(sampleRateHz);
        prepareCrossover();
        mCurrentStepIsSkipped = false;
        mSubDriftLfoTheta = {};
        noteOff();
    }

    void setShape(float newShape)
    {
        shape = juce::jlimit(0.0f, 1.0f, newShape);
        mShapeSmoothed.setTargetValue(applyKinkedMacroTaper(shape));
    }

    void setHarmony(float newHarmony)
    {
        harmony = juce::jlimit(0.0f, 1.0f, newHarmony);
        updateFrequencyRatios();
        applyCurrentBaseFrequency(mCurrentFrequency.getCurrentValue());
    }

    void setGrit(float newGrit)
    {
        mGrit = juce::jlimit(0.0f, 1.0f, newGrit);
        mGritSmoothed.setTargetValue(applyKinkedMacroTaper(mGrit));
    }

    void setGirth(float newGirth)
    {
        mGirth = juce::jlimit(0.0f, 1.0f, newGirth);
        rightDecorrelator.setDelay(decorrelationDelaySamples(mGirth));
    }

    void setPulse(float newPulse)
    {
        mPulse = juce::jlimit(0.0f, 1.0f, newPulse);
        mGateDutyCycle = juce::jmap(mPulse, 0.0f, 1.0f, 0.15f, 0.95f);

        // Lower pulse values feel clickier; higher values soften gate transitions.
        const float slewMs = juce::jmap(mPulse, 0.0f, 1.0f, 0.2f, 35.0f);
        gateSlew.setRiseAndFallTimesMs(slewMs, slewMs);
    }

    void setSubdivision(float multiplier)
    {
        mSubdivisionMultiplier = juce::jlimit(0.25f, 16.0f, multiplier);
    }

    void setRate(int index)
    {
        mRateIndex = juce::jlimit(0, 9, index);
        mSubdivisionMultiplier = rateIndexToMultiplier(mRateIndex);
    }

    void setSkipProbability(float p) noexcept
    {
        mSkipProbability = juce::jlimit(0.0f, 1.0f, p);
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
        const bool hasLingeringSignal = mIsNoteSustaining || mNoteGainEnvelope > 0.0f;
        const float noteFrequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
        mBaseFrequencyHz = juce::jlimit(1.0f, 20000.0f, noteFrequency);
        mVelocity = juce::jlimit(0.0f, 1.0f, velocity);

        if (hasLingeringSignal)
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

        updateFrequencyRatios();
        applyCurrentBaseFrequency(mCurrentFrequency.getCurrentValue());

        subOsc.setActive(true);
        midOscA.setActive(true);
        midOscB.setActive(true);

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

        double gatePhase = getBlockStartGatePhase();
        const double gatePhaseIncrement = getGatePhaseIncrement();

        for (int sample = 0; sample < numSamples; ++sample)
        {
            advanceSubDriftLfos();

            if (mIsNoteSustaining)
            {
                mNoteGainEnvelope = juce::jmin(1.0f, mNoteGainEnvelope + mRisePerSample);
            }
            else if (mNoteGainEnvelope > 0.0f)
            {
                mNoteGainEnvelope = juce::jmax(0.0f, mNoteGainEnvelope - mReleasePerSample);
                if (mNoteGainEnvelope <= 0.0f)
                {
                    mVelocity = 0.0f;
                    subOsc.setActive(false);
                    midOscA.setActive(false);
                    midOscB.setActive(false);
                }
            }

            const float currentBaseFrequency = mCurrentFrequency.getNextValue();
            mCurrentMidARatio = mMidARatioSmoothed.getNextValue();
            mCurrentMidBRatio = mMidBRatioSmoothed.getNextValue();
            applyCurrentBaseFrequency(currentBaseFrequency);
            mShapedShape = mShapeSmoothed.getNextValue();
            mShapedGrit = mGritSmoothed.getNextValue();
            mEntropy = mShapedGrit;
            subOsc.setShape(mShapedShape);
            midOscA.setShape(mShapedShape);
            midOscB.setShape(mShapedShape);

            const float sub = subOsc.getNextSample() * 0.6f;
            const float midA = midOscA.getNextSample() * 0.2f;
            const float midB = midOscB.getNextSample() * 0.2f;
            const float gate = advanceGate(gatePhase);

            const float subMono = sub * mMasterGain * mVelocity * gate;
            const float midMono = (midA + midB) * mMasterGain * mVelocity * gate;

            float leftOut = applyGritManifold(midMono);
            float rightOut = leftOut;
            processSpatialFrame(leftOut, rightOut);
            leftOut = juce::jlimit(-1.0f, 1.0f, leftOut + subMono);
            rightOut = juce::jlimit(-1.0f, 1.0f, rightOut + subMono);
            leftOut *= mNoteGainEnvelope;
            rightOut *= mNoteGainEnvelope;

            buffer.setSample(0, sample, leftOut);
            if (numOutputChannels > 1)
            {
                buffer.setSample(1, sample, rightOut);
            }
            for (int channel = 2; channel < numOutputChannels; ++channel)
            {
                buffer.setSample(channel, sample, 0.0f);
            }

            gatePhase += gatePhaseIncrement;
            if (gatePhase >= 1.0)
            {
                gatePhase -= std::floor(gatePhase);
                mCurrentStepIsSkipped = (entropyRandom.nextFloat() < mSkipProbability);
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

    std::array<float, 2> processSpatialFrameForTests(float inLeft, float inRight)
    {
        processSpatialFrame(inLeft, inRight);
        return { inLeft, inRight };
    }

    std::array<float, 3> getCurrentFrequenciesForTests() const noexcept
    {
        return { mSubFrequencyHz, mMidAFrequencyHz, mMidBFrequencyHz };
    }

    std::array<double, 3> getOscillatorPhasesForTests() const noexcept
    {
        return { subOsc.getPhaseForTests(), midOscA.getPhaseForTests(), midOscB.getPhaseForTests() };
    }

    float getRateMultiplierForTests() const noexcept
    {
        return mSubdivisionMultiplier;
    }

    void setEntropySeedForTests(int seed) noexcept
    {
        entropyRandom.setSeed(seed);
    }

    std::vector<float> renderGateEnvelopeForTests(int numSamples)
    {
        std::vector<float> envelope;
        if (numSamples <= 0)
        {
            return envelope;
        }

        envelope.reserve(static_cast<size_t>(numSamples));
        double gatePhase = getBlockStartGatePhase();
        const double gatePhaseIncrement = getGatePhaseIncrement();

        for (int i = 0; i < numSamples; ++i)
        {
            envelope.push_back(advanceGate(gatePhase));
            gatePhase += gatePhaseIncrement;
            if (gatePhase >= 1.0)
            {
                gatePhase -= std::floor(gatePhase);
                mCurrentStepIsSkipped = (entropyRandom.nextFloat() < mSkipProbability);
            }
        }

        mInternalGatePhase = gatePhase;
        return envelope;
    }

    std::vector<float> renderGritManifoldForTests(int numSamples, float frequencyHz)
    {
        std::vector<float> output;
        if (numSamples <= 0)
        {
            return output;
        }

        output.reserve(static_cast<size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            mShapedGrit = mGritSmoothed.getNextValue();
            mEntropy = mShapedGrit;
            const float phase = static_cast<float>(i) * frequencyHz / static_cast<float>(sampleRateHz);
            const float oscillatorSample = std::sin(juce::MathConstants<float>::twoPi * std::fmod(phase, 1.0f));
            output.push_back(applyGritManifold(oscillatorSample));
        }

        return output;
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
            const float currentBase = mCurrentFrequency.getNextValue();
            mCurrentMidARatio = mMidARatioSmoothed.getNextValue();
            mCurrentMidBRatio = mMidBRatioSmoothed.getNextValue();
            applyCurrentBaseFrequency(currentBase);
            trace.push_back(mSubFrequencyHz);
        }

        return trace;
    }

private:
    static float applyKinkedMacroTaper(float x) noexcept
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, x);
        if (clamped <= 0.5f)
        {
            return 2.0f * clamped * clamped;
        }

        return juce::jlimit(0.0f, 1.0f, 0.5f + (clamped - 0.5f) * 1.35f);
    }

    static float rateIndexToMultiplier(int index) noexcept
    {
        constexpr float multipliers[] { 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 16.0f };
        return multipliers[juce::jlimit(0, 9, index)];
    }

    void prepareCrossover()
    {
        juce::dsp::ProcessSpec spec {};
        spec.sampleRate = sampleRateHz;
        spec.maximumBlockSize = 2048;
        spec.numChannels = 1;

        for (auto& lowPassFilter : lowPassFilters)
        {
            lowPassFilter.reset();
            lowPassFilter.prepare(spec);
            lowPassFilter.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            lowPassFilter.setCutoffFrequency(crossoverFrequencyHz);
        }

        for (auto& highPassFilter : highPassFilters)
        {
            highPassFilter.reset();
            highPassFilter.prepare(spec);
            highPassFilter.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
            highPassFilter.setCutoffFrequency(crossoverFrequencyHz);
        }

        for (auto& upperSplitFilter : upperSplitFilters)
        {
            upperSplitFilter.reset();
            upperSplitFilter.prepare(spec);
            upperSplitFilter.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
            upperSplitFilter.setCutoffFrequency(upperMidBoundaryHz);
        }

        rightDecorrelator.reset();
        rightDecorrelator.prepare(spec);
        rightDecorrelator.setDelay(decorrelationDelaySamples(mGirth));
    }

    void processSpatialFrame(float& inOutLeft, float& inOutRight)
    {
        const float dryLeft = inOutLeft;
        const float dryRight = inOutRight;

        float lowLeft = 0.0f;
        float lowRight = 0.0f;
        float highLeft = 0.0f;
        float highRight = 0.0f;
        lowPassFilters[0].processSample(0, dryLeft, lowLeft, highLeft);
        lowPassFilters[1].processSample(0, dryRight, lowRight, highRight);

        // Keep explicit HP instances active in parallel per architecture request.
        juce::ignoreUnused(highPassFilters[0].processSample(0, dryLeft));
        juce::ignoreUnused(highPassFilters[1].processSample(0, dryRight));

        const float lowMono = 0.5f * (lowLeft + lowRight);

        float upperMidLeft = 0.0f;
        float upperMidRight = 0.0f;
        float airLeft = 0.0f;
        float airRight = 0.0f;
        upperSplitFilters[0].processSample(0, highLeft, upperMidLeft, airLeft);
        upperSplitFilters[1].processSample(0, highRight, upperMidRight, airRight);

        const float upperMidMono = 0.5f * (upperMidLeft + upperMidRight);

        rightDecorrelator.pushSample(0, airRight);
        const float delayedAirRight = rightDecorrelator.popSample(0);
        const float decorrelatedAirRight = juce::jmap(mGirth, airRight, delayedAirRight);

        const float mid = 0.5f * (airLeft + decorrelatedAirRight);
        const float sideEnergy = std::abs(airLeft) + std::abs(decorrelatedAirRight);
        const float sideWeight = juce::jlimit(0.0f, 1.0f, sideEnergy * 2.0f);
        const float side = 0.5f * (airLeft - decorrelatedAirRight) * (1.0f + mGirth) * sideWeight;
        const float widenedLeft = upperMidMono + mid + side;
        const float widenedRight = upperMidMono + mid - side;

        inOutLeft = juce::jlimit(-1.0f, 1.0f, lowMono + widenedLeft);
        inOutRight = juce::jlimit(-1.0f, 1.0f, lowMono + widenedRight);
    }

    double getGatePhaseIncrement() const noexcept
    {
        return ((mCurrentBpm / 60.0) * static_cast<double>(mSubdivisionMultiplier)) / sampleRateHz;
    }

    double getBlockStartGatePhase() const noexcept
    {
        if (mHasHostPpq)
        {
            const double hostPhase = std::fmod(mCurrentPpqPosition * static_cast<double>(mSubdivisionMultiplier), 1.0);
            const double diff = std::abs(hostPhase - mInternalGatePhase);
            const double wrappedDiff = juce::jmin(diff, 1.0 - diff);
            if (wrappedDiff < phaseLockThreshold)
            {
                return mInternalGatePhase;
            }

            return hostPhase;
        }

        return mInternalGatePhase;
    }

    float getGateTarget(double gatePhase) const noexcept
    {
        if (! mIsTransportPlaying)
        {
            return 1.0f;
        }

        if (mCurrentStepIsSkipped)
        {
            return 0.0f;
        }

        const float squareGate = gatePhase < static_cast<double>(mGateDutyCycle) ? 1.0f : 0.0f;
        const float phase = static_cast<float>(gatePhase);
        const float sineSwell = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);
        const float girthMorph = juce::jlimit(0.0f, 1.0f, mGirth);
        return juce::jmap(girthMorph, squareGate, sineSwell);
    }

    float advanceGate(double gatePhase)
    {
        return gateSlew.process(getGateTarget(gatePhase));
    }

    float applyGritManifold(float oscillatorSample)
    {
        float processed = oscillatorSample;

        const float tear = (entropyRandom.nextFloat() * 2.0f - 1.0f) * std::abs(oscillatorSample) * mShapedGrit * 0.1f;
        processed += tear;

        if (mShapedGrit > 0.5f)
        {
            const float bits = juce::jmap(mShapedGrit, 0.5f, 1.0f, 12.0f, 3.0f);
            const float quantizationLevels = std::pow(2.0f, bits);
            processed = std::round(processed * quantizationLevels) / quantizationLevels;
        }

        if (mShapedGrit > 0.0001f)
        {
            const float drive = 1.0f + mShapedGrit * 8.0f;
            const float clipped = std::tanh(processed * drive);
            const float makeUpGain = 1.0f / juce::jmax(0.05f, std::tanh(drive));
            processed = clipped * makeUpGain;
        }

        return processed;
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

    void advanceSubDriftLfos() noexcept
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

    float getSubDriftFrequencyRatio() const noexcept
    {
        const float s0 = static_cast<float>(std::sin(mSubDriftLfoTheta[0]));
        const float s1 = static_cast<float>(std::sin(mSubDriftLfoTheta[1]));
        const float s2 = static_cast<float>(std::sin(mSubDriftLfoTheta[2]));
        const float sumNorm = (s0 + s1 + s2) * (1.0f / 3.0f);
        const float cents = mGirth * subDriftMaxCents * sumNorm;
        return std::pow(2.0f, cents / 1200.0f);
    }

    float decorrelationDelaySamples(float girthAmount) const noexcept
    {
        const float delayMs = juce::jmap(juce::jlimit(0.0f, 1.0f, girthAmount), 2.0f, 5.0f);
        return static_cast<float>(sampleRateHz * (delayMs * 0.001f));
    }

    double sampleRateHz { 44100.0 };
    float shape { 0.0f };
    float harmony { 0.0f };
    float mBaseFrequencyHz { 55.0f };
    float mVelocity { 0.0f };
    float mMasterGain { 0.5f };
    float mGrit { 0.0f };
    float mShapedGrit { 0.0f };
    float mEntropy { 0.0f };
    float mShapedShape { 0.0f };
    float mGirth { 0.0f };
    float mPulse { 0.5f };
    float mGateDutyCycle { 0.55f };
    float mMidARatioTarget { 2.0f };
    float mMidBRatioTarget { 4.0f };
    float mCurrentMidARatio { 2.0f };
    float mCurrentMidBRatio { 4.0f };
    float mSubdivisionMultiplier { 4.0f };
    int mRateIndex { 6 };
    float mSubFrequencyHz { 55.0f };
    float mMidAFrequencyHz { 110.0f };
    float mMidBFrequencyHz { 220.0f };
    float mNoteGainEnvelope { 0.0f };
    float mReleasePerSample { 1.0f };
    float mRisePerSample { 1.0f };
    bool mIsNoteSustaining { false };

    double mCurrentBpm { 120.0 };
    double mCurrentPpqPosition { 0.0 };
    double mInternalGatePhase { 0.0 };
    bool mIsTransportPlaying { false };
    bool mHasHostPpq { false };
    bool mCurrentStepIsSkipped { false };
    float mSkipProbability { 0.2f };

    SlewLimiter gateSlew;
    juce::LinearSmoothedValue<float> mCurrentFrequency;
    juce::LinearSmoothedValue<float> mMidARatioSmoothed;
    juce::LinearSmoothedValue<float> mMidBRatioSmoothed;
    juce::LinearSmoothedValue<float> mShapeSmoothed;
    juce::LinearSmoothedValue<float> mGritSmoothed;
    static constexpr double glideTimeSeconds = 0.025;
    static constexpr double macroSmoothingSeconds = 0.01;
    static constexpr double phaseLockThreshold = 0.01;
    static constexpr float releaseTimeSeconds = 0.005f;
    static constexpr float crossoverFrequencyHz = 150.0f;
    static constexpr float upperMidBoundaryHz = 400.0f;
    static constexpr float subDriftMaxCents = 20.0f;
    static constexpr std::array<float, 3> subDriftLfoHz { 0.31f, 0.73f, 1.13f };
    std::array<double, 3> mSubDriftLfoTheta {};
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> lowPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> highPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> upperSplitFilters;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> rightDecorrelator { 512 };
    juce::Random entropyRandom;

    Oscillator subOsc;
    Oscillator midOscA;
    Oscillator midOscB;
};