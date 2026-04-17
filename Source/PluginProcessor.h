#pragma once

#include <JuceHeader.h>
#include "DSP/RumbleEngine.h"

class BoutiqueRumbleAudioProcessor final : public juce::AudioProcessor
{
public:
    BoutiqueRumbleAudioProcessor();
    ~BoutiqueRumbleAudioProcessor() override;

    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#if ! JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoutiqueRumbleAudioProcessor)
    juce::AudioProcessorValueTreeState apvts;
    RumbleEngine rumbleEngine;
    juce::MidiKeyboardState keyboardState;
    std::atomic<float>* pulseParam { nullptr };
    std::atomic<float>* shapeParam { nullptr };
    std::atomic<float>* harmonyParam { nullptr };
    std::atomic<float>* gritParam { nullptr };
    double mInternalPpq { 0.0 };
    double mDefaultBpm { 120.0 };
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
};
