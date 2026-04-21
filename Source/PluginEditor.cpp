#include "PluginEditor.h"
#include "Parameters/ParamConstants.h"

BoutiqueRumbleAudioProcessorEditor::BoutiqueRumbleAudioProcessorEditor (BoutiqueRumbleAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      keyboardComponent (audioProcessor.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
      waveformVisualiser (1)
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
    configureSlider(cutoffSlider, "Cutoff");
    configureSlider(resoSlider, "Reso");
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
    cutoffSlider.setRange(20.0, 20000.0);
    cutoffSlider.setSkewFactor(0.2);
    cutoffSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 18);
    cutoffSlider.textFromValueFunction = [] (double value)
    {
        if (value >= 1000.0)
            return juce::String(value / 1000.0, 1) + " kHz";
        return juce::String(juce::roundToInt(value)) + " Hz";
    };
    cutoffSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        auto trimmed = text.trim().toLowerCase();
        if (trimmed.contains("khz"))
            return trimmed.upToFirstOccurrenceOf("k", false, false).getDoubleValue() * 1000.0;
        return trimmed.upToFirstOccurrenceOf("h", false, false).getDoubleValue();
    };
    resoSlider.setRange(0.1, 20.0, 0.01);
    resoSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);

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
    configureLabel(skipLabel, "FAULT");
    configureLabel(brakeLabel, "BREAK");
    configureLabel(cutoffLabel, "CUTOFF");
    configureLabel(resoLabel, "RESO");

    configureLabel(bpmLabel, "BPM");
    bpmBox.setSliderStyle(juce::Slider::IncDecButtons);
    bpmBox.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
    bpmBox.setRange(40.0, 250.0, 1.0);
    bpmBox.setNumDecimalPlacesToDisplay(0);
    bpmBox.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 20);
    bpmBox.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff22201d));
    bpmBox.setColour(juce::Slider::thumbColourId, juce::Colour(0xffc7bb3f));
    bpmBox.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c1a18));
    bpmBox.setValue(audioProcessor.getStandaloneClockBpm(), juce::dontSendNotification);
    bpmBox.onValueChange = [this]
    {
        audioProcessor.setStandaloneClockBpm(bpmBox.getValue());
    };
    addAndMakeVisible(bpmBox);

    syncLightButton.setButtonText("SYNC");
    syncLightButton.setClickingTogglesState(true);
    syncLightButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffc7bb3f));
    syncLightButton.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff4e473d));
    syncLightButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffd5d9e2));
    syncLightButton.setToggleState(audioProcessor.getUseHostSync(), juce::dontSendNotification);
    syncLightButton.onClick = [this]
    {
        const bool syncOn = syncLightButton.getToggleState();
        audioProcessor.setUseHostSync(syncOn);
        bpmBox.setEnabled(! syncOn);
        bpmBox.setAlpha(syncOn ? 0.55f : 1.0f);
    };
    addAndMakeVisible(syncLightButton);
    bpmBox.setEnabled(! syncLightButton.getToggleState());
    bpmBox.setAlpha(syncLightButton.getToggleState() ? 0.55f : 1.0f);

    auto configureGroup = [this] (juce::GroupComponent& group, const juce::String& title)
    {
        group.setText(title);
        group.setColour(juce::GroupComponent::outlineColourId, juce::Colour(0xff3a322b));
        group.setColour(juce::GroupComponent::textColourId, juce::Colour(0xffd7c3a7));
        addAndMakeVisible(group);
    };

    configureGroup(timingGroup, "TIMING");
    configureGroup(disorderGroup, "DISORDER");
    configureGroup(toneGroup, "TONE");
    configureGroup(sculptGroup, "SCULPT");

    auto& apvts = audioProcessor.getAPVTS();
    pulseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::pulse, pulseSlider);
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::shape, shapeSlider);
    gritAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::grit, gritSlider);
    girthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::girth, girthSlider);
    harmonyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::harmony, harmonySlider);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::rate, rateSlider);
    skipAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::skip_prob, skipSlider);
    brakeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::brake, brakeSlider);
    cutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::cutoff, cutoffSlider);
    resoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, IDs::reso, resoSlider);

    addAndMakeVisible(keyboardComponent);
    keyboardComponent.setAvailableRange(0, 96); // C-2..C6 extended performance range
    keyboardComponent.setLowestVisibleKey(0);

    waveformVisualiser.setBufferSize(512);
    waveformVisualiser.setSamplesPerBlock(16);
    waveformVisualiser.setRepaintRate(30);
    waveformVisualiser.setColours(juce::Colour(0x22101010), juce::Colour(0xffc87f2f));
    addAndMakeVisible(waveformVisualiser);
    audioProcessor.setScopeVisualiser(&waveformVisualiser);

    bpmLabel.setVisible(true);
    bpmBox.setVisible(true);
    bpmBox.setEnabled(! syncLightButton.getToggleState());
    syncLightButton.setVisible(true);
    startTimerHz(30);

    setSize (940, 420);
}

BoutiqueRumbleAudioProcessorEditor::~BoutiqueRumbleAudioProcessorEditor()
{
    audioProcessor.setScopeVisualiser(nullptr);
    stopTimer();
    setLookAndFeel(nullptr);
}

void BoutiqueRumbleAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    auto headerArea = getLocalBounds().removeFromTop(52).reduced(12, 8);
    g.setColour(juce::Colour(0xffd5d9e2));
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawFittedText("Boutique Rumble", headerArea.removeFromLeft(280), juce::Justification::centredLeft, 1);
}

void BoutiqueRumbleAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    bounds.removeFromTop(52);

    auto keyboardArea = bounds.removeFromBottom(84);
    const int keyboardStart = 0;
    const int keyboardEnd = 96;
    int whiteKeyCount = 0;
    for (int midi = keyboardStart; midi <= keyboardEnd; ++midi)
    {
        if (! juce::MidiMessage::isMidiNoteBlack(midi))
            ++whiteKeyCount;
    }
    if (whiteKeyCount > 0)
    {
        const float targetKeyWidth = static_cast<float>(keyboardArea.getWidth()) / static_cast<float>(whiteKeyCount);
        keyboardComponent.setKeyWidth(targetKeyWidth);
    }
    keyboardComponent.setBounds(keyboardArea);

    auto visualizerArea = bounds.removeFromBottom(54).reduced(6, 4);
    waveformVisualiser.setBounds(visualizerArea);

    auto groupedArea = bounds.reduced(6);
    const int gap = 10;
    const int totalGap = gap * 3;
    const int availableWidth = juce::jmax(0, groupedArea.getWidth() - totalGap);
    const int timingWidth = availableWidth * 3 / 12;
    const int toneWidth = availableWidth * 4 / 12;
    const int disorderWidth = availableWidth * 2 / 12;
    const int sculptWidth = availableWidth - timingWidth - toneWidth - disorderWidth;

    auto timingArea = groupedArea.removeFromLeft(timingWidth);
    groupedArea.removeFromLeft(gap);
    auto toneArea = groupedArea.removeFromLeft(toneWidth);
    groupedArea.removeFromLeft(gap);
    auto disorderArea = groupedArea.removeFromLeft(disorderWidth);
    groupedArea.removeFromLeft(gap);
    auto sculptArea = groupedArea;

    timingGroup.setBounds(timingArea);
    disorderGroup.setBounds(disorderArea);
    toneGroup.setBounds(toneArea);
    sculptGroup.setBounds(sculptArea);

    auto layoutKnobGroup = [] (juce::Rectangle<int> area,
                               juce::Slider* const* sliders,
                               juce::Label* const* labels,
                               int count)
    {
        auto inner = area.reduced(10, 24);
        const int cellWidth = count > 0 ? inner.getWidth() / count : inner.getWidth();
        const int knobSize = juce::jlimit(66, 112, juce::jmin(cellWidth - 10, inner.getHeight() - 24));

        for (int i = 0; i < count; ++i)
        {
            auto cell = inner.removeFromLeft(cellWidth);
            auto labelArea = cell.removeFromTop(20);
            labels[i]->setBounds(labelArea);
            sliders[i]->setBounds(cell.withSizeKeepingCentre(knobSize, knobSize));
        }
    };

    auto timingInner = timingArea.reduced(10, 24);
    const int timingCellWidth = timingInner.getWidth() / 3;
    const int timingKnobSize = juce::jlimit(66, 112, juce::jmin(timingCellWidth - 10, timingInner.getHeight() - 24));

    auto pulseCell = timingInner.removeFromLeft(timingCellWidth);
    pulseLabel.setBounds(pulseCell.removeFromTop(20));
    pulseSlider.setBounds(pulseCell.withSizeKeepingCentre(timingKnobSize, timingKnobSize));

    auto rateCell = timingInner.removeFromLeft(timingCellWidth);
    rateLabel.setBounds(rateCell.removeFromTop(20));
    rateSlider.setBounds(rateCell.withSizeKeepingCentre(timingKnobSize, timingKnobSize));

    auto bpmCell = timingInner;
    auto bpmTopRow = bpmCell.removeFromTop(20);
    auto syncArea = bpmTopRow.removeFromRight(58);
    bpmLabel.setBounds(bpmTopRow);
    syncLightButton.setBounds(syncArea.reduced(2, 0));
    bpmBox.setBounds(bpmCell.reduced(8, juce::jmax(8, bpmCell.getHeight() / 3)));

    juce::Slider* toneSliders[] = { &shapeSlider, &gritSlider, &harmonySlider, &girthSlider };
    juce::Label* toneLabels[] = { &shapeLabel, &gritLabel, &harmonyLabel, &girthLabel };
    layoutKnobGroup(toneArea, toneSliders, toneLabels, 4);

    juce::Slider* disorderSliders[] = { &skipSlider, &brakeSlider };
    juce::Label* disorderLabels[] = { &skipLabel, &brakeLabel };
    layoutKnobGroup(disorderArea, disorderSliders, disorderLabels, 2);

    auto sculptInner = sculptArea.reduced(10, 24);
    auto cutoffCell = sculptInner.removeFromLeft(juce::jmax(130, sculptInner.getWidth() * 3 / 4));
    cutoffLabel.setBounds(cutoffCell.removeFromTop(20));
    cutoffSlider.setBounds(cutoffCell.withSizeKeepingCentre(120, 120));

    auto resoCell = sculptInner;
    resoLabel.setBounds(resoCell.removeFromTop(20));
    const int resoSize = juce::jmin(90, juce::jmax(70, juce::jmin(resoCell.getWidth() - 8, resoCell.getHeight() - 10)));
    resoSlider.setBounds(resoCell.withSizeKeepingCentre(resoSize, resoSize));

    bpmLabel.setVisible(true);
    bpmBox.setVisible(true);
    syncLightButton.setVisible(true);
}

void BoutiqueRumbleAudioProcessorEditor::timerCallback()
{
    const float bpm = static_cast<float>(juce::jlimit(40.0, 250.0, audioProcessor.getCurrentClockBpmForUi()));
    const float beatsPerSecond = bpm / 60.0f;
    const float dt = 1.0f / 30.0f;
    mSyncPulsePhase = std::fmod(mSyncPulsePhase + beatsPerSecond * dt, 1.0f);

    const float pulseShape = std::exp(-12.0f * mSyncPulsePhase);
    const float glow = 0.28f + 0.72f * pulseShape;
    const auto lit = juce::Colour(0xfff2d33c).withMultipliedBrightness(glow);
    const auto dim = juce::Colour(0xff4e473d);

    syncLightButton.setColour(juce::ToggleButton::tickColourId, lit);
    syncLightButton.setColour(juce::ToggleButton::tickDisabledColourId, dim);
    repaint(syncLightButton.getBounds());
}
