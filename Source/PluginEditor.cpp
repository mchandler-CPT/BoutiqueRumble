#include "PluginEditor.h"

BoutiqueRumbleAudioProcessorEditor::BoutiqueRumbleAudioProcessorEditor (BoutiqueRumbleAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      keyboardComponent (audioProcessor.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible(keyboardComponent);
    keyboardComponent.setAvailableRange(36, 96);
    setSize (600, 400);
}

BoutiqueRumbleAudioProcessorEditor::~BoutiqueRumbleAudioProcessorEditor() = default;

void BoutiqueRumbleAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    auto titleArea = getLocalBounds().reduced(12);
    titleArea.removeFromBottom(90);
    g.drawFittedText ("Boutique Rumble", titleArea, juce::Justification::centred, 1);
}

void BoutiqueRumbleAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    keyboardComponent.setBounds(bounds.removeFromBottom(80));
}
