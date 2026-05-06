#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/BoutiqueLookAndFeel.h"

class BoutiqueRumbleAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit BoutiqueRumbleAudioProcessorEditor (BoutiqueRumbleAudioProcessor&);
    ~BoutiqueRumbleAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void refreshPresetUi();
    void promptSavePreset();
    void promptSetPresetFolder();

    BoutiqueRumbleAudioProcessor& audioProcessor;
    BoutiqueLookAndFeel boutiqueLookAndFeel;
    juce::Image rumbleLogo;
    juce::MidiKeyboardComponent keyboardComponent;
    juce::AudioVisualiserComponent waveformVisualiser;

    juce::Slider pulseSlider;
    juce::Slider shapeSlider;
    juce::Slider gritSlider;
    juce::Slider girthSlider;
    juce::Slider harmonySlider;
    juce::Slider rateSlider;
    juce::Slider skipSlider;
    juce::Slider brakeSlider;
    juce::Slider cutoffSlider;
    juce::Slider resoSlider;

    juce::Label pulseLabel;
    juce::Label shapeLabel;
    juce::Label gritLabel;
    juce::Label girthLabel;
    juce::Label harmonyLabel;
    juce::Label rateLabel;
    juce::Label skipLabel;
    juce::Label brakeLabel;
    juce::Label cutoffLabel;
    juce::Label resoLabel;
    juce::Label bpmLabel;

    juce::GroupComponent timingGroup;
    juce::GroupComponent disorderGroup;
    juce::GroupComponent toneGroup;
    juce::GroupComponent sculptGroup;

    juce::Slider bpmBox;
    juce::ToggleButton syncLightButton;
    juce::TextButton mPrevButton;
    juce::TextButton mNextButton;
    juce::TextButton mSaveButton;
    juce::TextButton mSetFolderButton;
    juce::Label mPresetLabel;
    std::unique_ptr<juce::FileChooser> mPresetSaveChooser;
    std::unique_ptr<juce::FileChooser> mPresetFolderChooser;
    int mSelectedPresetIndex { -1 };
    float mSyncPulsePhase { 0.0f };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pulseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shapeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gritAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> girthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> harmonyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> skipAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> brakeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resoAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoutiqueRumbleAudioProcessorEditor)
};
