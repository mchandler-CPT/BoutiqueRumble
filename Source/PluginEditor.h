#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class BoutiqueRumbleAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BoutiqueRumbleAudioProcessorEditor (BoutiqueRumbleAudioProcessor&);
    ~BoutiqueRumbleAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BoutiqueRumbleAudioProcessor& audioProcessor;
    juce::MidiKeyboardComponent keyboardComponent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoutiqueRumbleAudioProcessorEditor)
};
