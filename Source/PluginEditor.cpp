#include "PluginEditor.h"
#include "Parameters/ParamConstants.h"

BoutiqueRumbleAudioProcessorEditor::BoutiqueRumbleAudioProcessorEditor (BoutiqueRumbleAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      keyboardComponent (audioProcessor.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&boutiqueLookAndFeel);

    auto configureSlider = [this] (juce::Slider& slider, const juce::String& name)
    {
        slider.setName(name);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd5d9e2));
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1a1e28));
        addAndMakeVisible(slider);
    };

    configureSlider(pulseSlider, "Pulse");
    configureSlider(shapeSlider, "Shape");
    configureSlider(gritSlider, "Grit");
    configureSlider(girthSlider, "Girth");
    configureSlider(harmonySlider, "Harmony");

    auto configureLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffd5d9e2));
        addAndMakeVisible(label);
    };

    configureLabel(pulseLabel, "PULSE");
    configureLabel(shapeLabel, "SHAPE");
    configureLabel(gritLabel, "GRIT");
    configureLabel(girthLabel, "GIRTH");
    configureLabel(harmonyLabel, "HARMONY");

    auto& apvts = audioProcessor.getAPVTS();
    pulseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::pulse, pulseSlider);
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::shape, shapeSlider);
    gritAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::grit, gritSlider);
    girthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::girth, girthSlider);
    harmonyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::harmony, harmonySlider);

    addAndMakeVisible(keyboardComponent);
    keyboardComponent.setAvailableRange(36, 96);
    setSize (700, 420);
}

BoutiqueRumbleAudioProcessorEditor::~BoutiqueRumbleAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void BoutiqueRumbleAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto headerArea = getLocalBounds().removeFromTop(52).reduced(12, 8);
    g.setColour(juce::Colour(0xffd5d9e2));
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawFittedText("Boutique Rumble", headerArea.removeFromLeft(280), juce::Justification::centredLeft, 1);

    g.setColour(juce::Colour(0xff7f8797));
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText("Code-driven -> GUI-driven control", headerArea, juce::Justification::centredRight, 1);
}

void BoutiqueRumbleAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    bounds.removeFromTop(52);

    auto keyboardArea = bounds.removeFromBottom(88);
    keyboardComponent.setBounds(keyboardArea);

    auto knobArea = bounds.reduced(6);
    constexpr int knobCount = 5;
    const int knobWidth = knobArea.getWidth() / knobCount;
    const int knobSize = juce::jmin(knobWidth - 10, knobArea.getHeight());

    juce::Slider* sliders[knobCount] = { &pulseSlider, &shapeSlider, &gritSlider, &girthSlider, &harmonySlider };
    juce::Label* labels[knobCount] = { &pulseLabel, &shapeLabel, &gritLabel, &girthLabel, &harmonyLabel };
    for (int i = 0; i < knobCount; ++i)
    {
        auto cell = knobArea.removeFromLeft(knobWidth);
        auto labelArea = cell.removeFromTop(20);
        labels[i]->setBounds(labelArea);
        sliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize));
    }
}
