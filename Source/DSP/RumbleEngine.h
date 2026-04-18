#pragma once
#include <array>
#include <atomic>
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
        mRisePerSample = 1.0f / juce::jmax(1.0f, static_cast<float>(sampleRateHz * attackTimeSeconds));
        updateReleaseEnvelopeCoefficient();
        updateThumpDecayCoefficient();

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
        prepareSafetyOutputStage();
        mCurrentStepIsSkipped = false;
        mSubDriftLfoTheta = {};
        mGateRetriggerDipSamplesRemaining = 0;
        mThumpSemitones = 0.0f;
        mBrakeSmoother.reset(sampleRateHz, brakeSmootherSeconds);
        mBrakeSmoother.setCurrentAndTargetValue(1.0f);
        mStallStutterPhase = 0.0f;
        noteOff();
    }

    void setBrakeParameter(std::atomic<float>* param) noexcept
    {
        mBrakeParameter = param;
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
        applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(mCurrentFrequency.getCurrentValue()));
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
        // Never go below ~1 ms edges: a true 0 ms slew clicks on square LFO flanks.
        const float slewMs = juce::jmax(minGateSlewMs, juce::jmap(mPulse, 0.0f, 1.0f, 0.2f, 35.0f));
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
        const bool voiceAlreadyActive = mIsNoteSustaining || mNoteGainEnvelope > 0.0f;
        const bool legatoRetrigger = mIsNoteSustaining;

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
            mGateRetriggerDipSamplesRemaining = juce::jmax(1, juce::roundToInt(sampleRateHz * gateRetriggerDipSeconds));
            mThumpSemitones = juce::jmax(mThumpSemitones, thumpSemitoneLegato);
        }
        else
        {
            mThumpSemitones = thumpSemitoneFresh;
        }

        updateReleaseEnvelopeCoefficient();
        updateFrequencyRatios();
        applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(mCurrentFrequency.getCurrentValue()));

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

            const float brakeTarget = (mBrakeParameter != nullptr) ? mBrakeParameter->load() : 1.0f;
            mBrakeSmoother.setTargetValue(juce::jlimit(0.0f, 1.0f, brakeTarget));
            const float brakeIn = mBrakeSmoother.getNextValue();
            const float brakeWarpX = (brakeIn - 0.5f) * 10.0f;
            const float warpedBrake = 1.0f / (1.0f + std::exp(-brakeWarpX));

            const float pitchMult = warpedBrake * warpedBrake * warpedBrake;
            const float gainMult = brakeIn;

            const float stutterIntensity = 1.0f - brakeIn;
            const float oneMinusBrake = 1.0f - brakeIn;
            const float stutterRateHz = stutterRateScale * std::pow(oneMinusBrake, stutterRatePower);

            float rawStutterSquareWave = 1.0f;
            if (stutterRateHz > 1.0e-9f)
            {
                mStallStutterPhase += stutterRateHz / static_cast<float>(sampleRateHz);
                if (mStallStutterPhase >= 1.0f)
                {
                    mStallStutterPhase -= std::floor(mStallStutterPhase);
                }

                rawStutterSquareWave = (mStallStutterPhase < stutterSquareDuty) ? 1.0f : 0.0f;
            }

            const float brakeStutterGate = 1.0f - (stutterIntensity * (1.0f - rawStutterSquareWave));

            if (mIsNoteSustaining)
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

            const float currentBaseFrequency = mCurrentFrequency.getNextValue();
            mCurrentMidARatio = mMidARatioSmoothed.getNextValue();
            mCurrentMidBRatio = mMidBRatioSmoothed.getNextValue();
            applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(currentBaseFrequency));
            advanceThumpSemitoneDecay();

            const float rawSubHz = mSubFrequencyHz * pitchMult;
            float subHz = rawSubHz;
            if (brakeIn > 0.0f)
            {
                subHz = juce::jmax(brakeFrequencyIdleFloorHz, rawSubHz);
            }

            const float brakeFreqScale = (rawSubHz > 1.0e-6f) ? (subHz / rawSubHz) : 0.0f;
            subOsc.setFrequency(subHz);
            midOscA.setFrequency(mMidAFrequencyHz * pitchMult * brakeFreqScale);
            midOscB.setFrequency(mMidBFrequencyHz * pitchMult * brakeFreqScale);
            mShapedShape = mShapeSmoothed.getNextValue();
            mShapedGrit = mGritSmoothed.getNextValue();
            mEntropy = mShapedGrit;
            subOsc.setShape(mShapedShape);
            midOscA.setShape(mShapedShape);
            midOscB.setShape(mShapedShape);

            const float sub = subOsc.getNextSample() * 0.6f;
            const float midA = midOscA.getNextSample() * 0.2f;
            const float midB = midOscB.getNextSample() * 0.2f;
            const float pulseGateLfo = advanceGate(gatePhase);
            const float pulseGate = pulseGateLfo * brakeStutterGate;

            const float subMono = sub * mMasterGain * mVelocity * pulseGate;
            const float midMono = (midA + midB) * mMasterGain * mVelocity * pulseGate;

            float leftOut = applyGritManifold(midMono);
            float rightOut = leftOut;
            processSpatialFrame(leftOut, rightOut);
            leftOut = juce::jlimit(-1.0f, 1.0f, leftOut + subMono);
            rightOut = juce::jlimit(-1.0f, 1.0f, rightOut + subMono);
            leftOut *= mNoteGainEnvelope;
            rightOut *= mNoteGainEnvelope;
            leftOut *= gainMult;
            rightOut *= gainMult;

            if (mNoteGainEnvelope <= 0.0f && ! mIsNoteSustaining)
            {
                leftOut = 0.0f;
                rightOut = 0.0f;
                resetSafetyOutputStage();
            }
            else
            {
                leftOut = applySafetyOutputStage(0, leftOut);
                if (numOutputChannels > 1)
                {
                    rightOut = applySafetyOutputStage(1, rightOut);
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
            advanceSubDriftLfos();
            const float currentBase = mCurrentFrequency.getNextValue();
            mCurrentMidARatio = mMidARatioSmoothed.getNextValue();
            mCurrentMidBRatio = mMidBRatioSmoothed.getNextValue();
            applyCurrentBaseFrequency(baseFrequencyWithThumpOffset(currentBase));
            advanceThumpSemitoneDecay();
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

    void prepareSafetyOutputStage()
    {
        juce::dsp::ProcessSpec spec {};
        spec.sampleRate = sampleRateHz;
        spec.maximumBlockSize = 2048;
        spec.numChannels = 1;

        const auto dcCoeffs = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass(
            sampleRateHz,
            static_cast<float>(dcBlockerHz));

        for (size_t i = 0; i < dcBlockers.size(); ++i)
        {
            dcBlockers[i].reset();
            dcBlockers[i].prepare(spec);
            dcBlockers[i].coefficients = dcCoeffs;
        }

        for (size_t i = 0; i < safetyOutputHp.size(); ++i)
        {
            safetyOutputHp[i].reset();
            safetyOutputHp[i].prepare(spec);
            safetyOutputHp[i].setType(juce::dsp::LinkwitzRileyFilterType::highpass);
            safetyOutputHp[i].setCutoffFrequency(safetyHighPassHz);
        }
    }

    float applySafetyOutputStage(int channelIndex, float x) noexcept
    {
        float low = 0.0f;
        float high = 0.0f;
        safetyOutputHp[static_cast<size_t>(channelIndex)].processSample(0, x, low, high);
        return dcBlockers[static_cast<size_t>(channelIndex)].processSample(high);
    }

    void resetSafetyOutputStage() noexcept
    {
        for (auto& dc : dcBlockers)
        {
            dc.reset();
        }

        for (auto& hp : safetyOutputHp)
        {
            hp.reset();
        }
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
        float target = getGateTarget(gatePhase);
        if (mGateRetriggerDipSamplesRemaining > 0)
        {
            target = 0.0f;
            --mGateRetriggerDipSamplesRemaining;
        }

        return gateSlew.process(target);
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

    void updateThumpDecayCoefficient() noexcept
    {
        const double n = static_cast<double>(thumpDurationSeconds) * sampleRateHz;
        mThumpDecayMultiplierPerSample = static_cast<float>(std::exp(std::log(static_cast<double>(thumpSemitoneFloor)) / n));
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

    void updateReleaseEnvelopeCoefficient() noexcept
    {
        const float noteClamped = juce::jlimit(12.0f, 127.0f, mActiveMidiNote);
        const float stretch = juce::jmap(noteClamped, 16.0f, 96.0f, 1.55f, 0.88f);
        const double tauSec = static_cast<double>(releaseTauBaseSeconds) * static_cast<double>(stretch);
        mReleaseMultiplierPerSample = static_cast<float>(std::exp(-1.0 / (sampleRateHz * tauSec)));
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
    float mReleaseMultiplierPerSample { 1.0f };
    float mRisePerSample { 1.0f };
    float mThumpSemitones { 0.0f };
    float mThumpDecayMultiplierPerSample { 1.0f };
    float mActiveMidiNote { 36.0f };
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
    std::atomic<float>* mBrakeParameter { nullptr };
    juce::LinearSmoothedValue<float> mBrakeSmoother;
    static constexpr double glideTimeSeconds = 0.025;
    static constexpr double macroSmoothingSeconds = 0.01;
    static constexpr double brakeSmootherSeconds = 0.05;
    static constexpr float brakeFrequencyIdleFloorHz = 20.0f;
    static constexpr float stutterRateScale = 40.0f;
    static constexpr float stutterRatePower = 3.0f;
    static constexpr float stutterSquareDuty = 0.5f;
    float mStallStutterPhase { 0.0f };
    static constexpr double phaseLockThreshold = 0.01;
    static constexpr float attackTimeSeconds = 0.005f;
    static constexpr float releaseTauBaseSeconds = 0.035f;
    static constexpr float noteEnvelopeSilenceThreshold = 1.0e-6f;
    static constexpr float thumpDurationSeconds = 0.045f;
    static constexpr float thumpSemitoneFresh = 36.0f;
    static constexpr float thumpSemitoneLegato = 18.0f;
    static constexpr float thumpSemitoneFloor = 1.0e-9f;
    static constexpr float crossoverFrequencyHz = 150.0f;
    static constexpr float upperMidBoundaryHz = 400.0f;
    static constexpr float subDriftMaxCents = 20.0f;
    static constexpr std::array<float, 3> subDriftLfoHz { 0.31f, 0.73f, 1.13f };
    std::array<double, 3> mSubDriftLfoTheta {};
    static constexpr float minGateSlewMs = 1.0f;
    static constexpr float gateRetriggerDipSeconds = 0.001f;
    int mGateRetriggerDipSamplesRemaining { 0 };
    static constexpr float dcBlockerHz = 5.0f;
    static constexpr float safetyHighPassHz = 25.0f;
    std::array<juce::dsp::IIR::Filter<float>, 2> dcBlockers;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> safetyOutputHp;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> lowPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> highPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> upperSplitFilters;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> rightDecorrelator { 512 };
    juce::Random entropyRandom;

    Oscillator subOsc;
    Oscillator midOscA;
    Oscillator midOscB;
};