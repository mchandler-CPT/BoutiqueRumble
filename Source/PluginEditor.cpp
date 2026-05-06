#include "PluginEditor.h"
#include "Parameters/ParamConstants.h"

BoutiqueRumbleAudioProcessorEditor::BoutiqueRumbleAudioProcessorEditor (BoutiqueRumbleAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      rumbleLogo(juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize)),
      keyboardComponent (audioProcessor.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
      waveformVisualiser (2)
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
    bpmBox.setTextBoxStyle(juce::Slider::TextBoxRight, false, 86, 20);
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

    // Prime preset cache so first button click always has data.
    audioProcessor.updatePresetList();

    mPrevButton.setButtonText("<");
    mNextButton.setButtonText(">");
    mSaveButton.setButtonText("SAVE");
    mSetFolderButton.setButtonText("SET FOLDER");
    mPrevButton.setTooltip("Previous preset");
    mNextButton.setTooltip("Next preset");
    mSaveButton.setTooltip("Save current preset to file");
    mSetFolderButton.setTooltip("Choose preset folder");
    mPrevButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2622));
    mNextButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2622));
    mPrevButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a322b));
    mNextButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a322b));
    mPrevButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffd7c3a7));
    mNextButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffd7c3a7));
    mPrevButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7c3a7));
    mNextButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7c3a7));
    mSaveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2622));
    mSetFolderButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2622));
    mSaveButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a322b));
    mSetFolderButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a322b));
    mSaveButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffd7c3a7));
    mSetFolderButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffd7c3a7));
    mSaveButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7c3a7));
    mSetFolderButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7c3a7));
    mPrevButton.onClick = [this]
    {
        audioProcessor.loadPreviousPreset();
        refreshPresetUi();
        mPresetLabel.setText(audioProcessor.getCurrentPresetDisplayName(), juce::dontSendNotification);
        mPresetLabel.repaint();
        repaint(mPresetLabel.getBounds());
    };
    mNextButton.onClick = [this]
    {
        audioProcessor.loadNextPreset();
        refreshPresetUi();
        mPresetLabel.setText(audioProcessor.getCurrentPresetDisplayName(), juce::dontSendNotification);
        mPresetLabel.repaint();
        repaint(mPresetLabel.getBounds());
    };
    mSaveButton.onClick = [this] { promptSavePreset(); };
    mSetFolderButton.onClick = [this] { promptSetPresetFolder(); };
    addAndMakeVisible(mPrevButton);
    addAndMakeVisible(mNextButton);
    addAndMakeVisible(mSaveButton);
    addAndMakeVisible(mSetFolderButton);

    mPresetLabel.setText("Init", juce::dontSendNotification);
    mPresetLabel.setJustificationType(juce::Justification::centred);
    mPresetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd5d9e2));
    mPresetLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0x20101010));
    mPresetLabel.setInterceptsMouseClicks(false, false); // route click handling to editor for Shift+click save.
    mPresetLabel.setTooltip("Shift+Click to Quick Save preset");
    addAndMakeVisible(mPresetLabel);

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
    // Scope audio: processor calls pushBuffer (final post-master buffer) each processBlock.
    audioProcessor.setScopeVisualiser(&waveformVisualiser);

    bpmLabel.setVisible(true);
    bpmBox.setVisible(true);
    bpmBox.setEnabled(! syncLightButton.getToggleState());
    syncLightButton.setVisible(true);
    startTimerHz(30);
    refreshPresetUi();

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
    if (rumbleLogo.isValid())
    {
        g.drawImageWithin(rumbleLogo,
                          headerArea.getX(),
                          headerArea.getY() + 4,
                          340,
                          headerArea.getHeight(),
                          juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
    }
}

void BoutiqueRumbleAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    auto presetArea = bounds.removeFromTop(28);
    bounds.removeFromTop(24);

    auto navArea = presetArea.withSizeKeepingCentre(260, presetArea.getHeight());
    auto actionsArea = presetArea.removeFromRight(250);
    mSetFolderButton.setBounds(actionsArea.removeFromRight(120).reduced(2, 0));
    mSaveButton.setBounds(actionsArea.removeFromRight(90).reduced(2, 0));
    presetArea.removeFromRight(8);
    auto prevArea = navArea.removeFromLeft(36).reduced(2, 0);
    auto nextArea = navArea.removeFromRight(36).reduced(2, 0);
    mPrevButton.setBounds(prevArea);
    mNextButton.setBounds(nextArea);
    mPresetLabel.setBounds(navArea.reduced(4, 0));

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
    const int timingInnerWidth = timingInner.getWidth();
    const int bpmPreferredWidth = juce::jlimit(96, juce::jmax(96, timingInnerWidth - 160), timingInnerWidth - 112);
    const int macroAvailableWidth = juce::jmax(80, timingInnerWidth - bpmPreferredWidth);
    const int timingMacroCellWidth = juce::jmax(40, macroAvailableWidth / 2);
    const int timingKnobSize = juce::jlimit(62, 112, juce::jmin(timingMacroCellWidth - 8, timingInner.getHeight() - 24));

    auto pulseCell = timingInner.removeFromLeft(timingMacroCellWidth);
    pulseLabel.setBounds(pulseCell.removeFromTop(20));
    pulseSlider.setBounds(pulseCell.withSizeKeepingCentre(timingKnobSize, timingKnobSize));

    auto rateCell = timingInner.removeFromLeft(timingMacroCellWidth);
    rateLabel.setBounds(rateCell.removeFromTop(20));
    rateSlider.setBounds(rateCell.withSizeKeepingCentre(timingKnobSize, timingKnobSize));

    auto bpmCell = timingInner;
    auto bpmTopRow = bpmCell.removeFromTop(20);
    auto syncArea = bpmTopRow.removeFromRight(58);
    bpmLabel.setBounds(bpmTopRow);
    syncLightButton.setBounds(syncArea.reduced(2, 0));
    bpmBox.setBounds(bpmCell.reduced(4, juce::jmax(8, bpmCell.getHeight() / 3)));

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

void BoutiqueRumbleAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    const auto pointInEditor = event.getEventRelativeTo(this).position.toInt();
    if (mPresetLabel.getBounds().contains(pointInEditor) && event.mods.isShiftDown() && event.mods.isRightButtonDown())
        promptSavePreset();
}

void BoutiqueRumbleAudioProcessorEditor::refreshPresetUi()
{
    audioProcessor.updatePresetList();
    const auto& presets = audioProcessor.getPresetList();
    mSelectedPresetIndex = audioProcessor.getCurrentPresetIndex();

    const bool hasPresets = ! presets.isEmpty();
    mPrevButton.setEnabled(hasPresets);
    mNextButton.setEnabled(hasPresets);
    const auto presetName = juce::File(audioProcessor.getCurrentPresetDisplayName()).getFileName();
    // Force a visible UI refresh even when loaded preset has identical values.
    mPresetLabel.setText({}, juce::dontSendNotification);
    mPresetLabel.setText(presetName, juce::dontSendNotification);
}

void BoutiqueRumbleAudioProcessorEditor::promptSavePreset()
{
    const auto currentName = mPresetLabel.getText().isEmpty() ? juce::String("Rumble") : mPresetLabel.getText();
    mPresetSaveChooser = std::make_unique<juce::FileChooser>(
        "Save Boutique Rumble Preset",
        audioProcessor.getPresetDirectory().getChildFile(currentName),
        "*");

    const int flags = juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting;
    mPresetSaveChooser->launchAsync(flags, [this] (const juce::FileChooser& chooser)
    {
        const juce::File target = chooser.getResult();
        if (target != juce::File{})
        {
            const juce::File outputFile = target;
            audioProcessor.saveCurrentPreset(outputFile);
            if (outputFile.existsAsFile())
            {
                audioProcessor.setPresetDirectory(outputFile.getParentDirectory());
                mPresetLabel.setText(outputFile.getFileName(), juce::dontSendNotification);
                mPresetLabel.repaint();
                repaint(mPresetLabel.getBounds());
            }
        }

        refreshPresetUi();
        mPresetSaveChooser.reset();
    });
}

void BoutiqueRumbleAudioProcessorEditor::promptSetPresetFolder()
{
    mPresetFolderChooser = std::make_unique<juce::FileChooser>(
        "Choose Boutique Rumble Preset Folder",
        audioProcessor.getPresetDirectory(),
        "*");

    const int flags = juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectDirectories;
    mPresetFolderChooser->launchAsync(flags, [this] (const juce::FileChooser& chooser)
    {
        const juce::File chosenFolder = chooser.getResult();
        if (chosenFolder != juce::File{})
        {
            audioProcessor.setPresetDirectory(chosenFolder);
            refreshPresetUi();
        }

        mPresetFolderChooser.reset();
    });
}
