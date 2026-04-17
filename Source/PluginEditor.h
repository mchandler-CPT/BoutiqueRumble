#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/BoutiqueLookAndFeel.h"

class BoutiqueRumbleAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BoutiqueRumbleAudioProcessorEditor (BoutiqueRumbleAudioProcessor&);
    ~BoutiqueRumbleAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BoutiqueRumbleAudioProcessor& audioProcessor;
    BoutiqueLookAndFeel boutiqueLookAndFeel;
    juce::MidiKeyboardComponent keyboardComponent;

    juce::Slider pulseSlider;
    juce::Slider shapeSlider;
    juce::Slider gritSlider;
    juce::Slider girthSlider;
    juce::Slider harmonySlider;

    juce::Label pulseLabel;
    juce::Label shapeLabel;
    juce::Label gritLabel;
    juce::Label girthLabel;
    juce::Label harmonyLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pulseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shapeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gritAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> girthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> harmonyAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoutiqueRumbleAudioProcessorEditor)
};
