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
    configureSlider(rateSlider, "Rate");
    configureSlider(skipSlider, "Skip");
    configureSlider(brakeSlider, "Brake");
    rateSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    rateSlider.setRange(0.0, 9.0, 1.0);
    rateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
    rateSlider.setDoubleClickReturnValue(true, 6.0);
    rateSlider.textFromValueFunction = [] (double value)
    {
        static const juce::String labels[] { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
        return labels[juce::jlimit(0, 9, juce::roundToInt(value))];
    };
    rateSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        static const juce::StringArray labels { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
        const int idx = labels.indexOf(text.trim());
        return static_cast<double>(idx >= 0 ? idx : 6);
    };

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
    configureLabel(rateLabel, "RATE");
    configureLabel(skipLabel, "SKIP");
    configureLabel(brakeLabel, "BRAKE");

    configureLabel(bpmLabel, "BPM");
    bpmBox.setSliderStyle(juce::Slider::IncDecButtons);
    bpmBox.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_AutoDirection);
    bpmBox.setRange(40.0, 220.0, 1.0);
    bpmBox.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 18);
    bpmBox.setValue(audioProcessor.getStandaloneClockBpm(), juce::dontSendNotification);
    bpmBox.onValueChange = [this]
    {
        audioProcessor.setStandaloneClockBpm(bpmBox.getValue());
    };
    addAndMakeVisible(bpmBox);

    auto& apvts = audioProcessor.getAPVTS();
    pulseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::pulse, pulseSlider);
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::shape, shapeSlider);
    gritAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::grit, gritSlider);
    girthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::girth, girthSlider);
    harmonyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::harmony, harmonySlider);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::rate, rateSlider);
    skipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::skip_prob, skipSlider);
    brakeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::brake, brakeSlider);

    addAndMakeVisible(keyboardComponent);
    keyboardComponent.setAvailableRange(36, 96);

#if JucePlugin_Build_Standalone
    bpmLabel.setVisible(true);
    bpmBox.setVisible(true);
    bpmBox.setEnabled(true);
#else
    bpmLabel.setVisible(false);
    bpmBox.setVisible(false);
    bpmBox.setEnabled(false);
#endif

    setSize (940, 420);
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
    auto bottomControlArea = bounds.removeFromBottom(24);

    auto knobArea = bounds.reduced(6);
    constexpr int knobCount = 8;
    const int maxKnobRowWidth = 8 * 112;
    if (knobArea.getWidth() > maxKnobRowWidth)
    {
        knobArea = knobArea.withSizeKeepingCentre(maxKnobRowWidth, knobArea.getHeight());
    }

    const int knobWidth = knobArea.getWidth() / knobCount;
    const int knobSize = juce::jmin(knobWidth - 10, knobArea.getHeight());

    juce::Slider* sliders[knobCount] = { &pulseSlider, &shapeSlider, &gritSlider, &girthSlider, &harmonySlider, &rateSlider, &skipSlider, &brakeSlider };
    juce::Label* labels[knobCount] = { &pulseLabel, &shapeLabel, &gritLabel, &girthLabel, &harmonyLabel, &rateLabel, &skipLabel, &brakeLabel };
    for (int i = 0; i < knobCount; ++i)
    {
        auto cell = knobArea.removeFromLeft(knobWidth);
        auto labelArea = cell.removeFromTop(20);
        labels[i]->setBounds(labelArea);

        sliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize));
    }

#if JucePlugin_Build_Standalone
    auto bpmArea = bottomControlArea.removeFromRight(140);
    bpmLabel.setBounds(bpmArea.removeFromLeft(40));
    bpmBox.setBounds(bpmArea.reduced(2, 2));
#else
    juce::ignoreUnused(bottomControlArea);
#endif
}
