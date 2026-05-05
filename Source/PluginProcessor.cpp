#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParamConstants.h"
#include <algorithm>
#include <cmath>

BoutiqueRumbleAudioProcessor::BoutiqueRumbleAudioProcessor()
     : AudioProcessor (BusesProperties()
        #if ! JucePlugin_IsMidiEffect
         #if ! JucePlugin_IsSynth
           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
         #endif
           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        #endif
       ),
      apvts (*this, nullptr, "RUMBLE_PARAMS", createParameterLayout()), // The Handshake
      mPresetDir(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                     .getChildFile("Rumble")
                     .getChildFile("Presets"))
{
    pulseParam = apvts.getRawParameterValue(IDs::pulse);
    shapeParam = apvts.getRawParameterValue(IDs::shape);
    harmonyParam = apvts.getRawParameterValue(IDs::harmony);
    gritParam = apvts.getRawParameterValue(IDs::grit);
    girthParam = apvts.getRawParameterValue(IDs::girth);
    rateParam = apvts.getRawParameterValue(IDs::rate);
    skipParam = apvts.getRawParameterValue(IDs::skip_prob);
    brakeParam = apvts.getRawParameterValue(IDs::brake);
    cutoffParam = apvts.getRawParameterValue(IDs::cutoff);
    resoParam = apvts.getRawParameterValue(IDs::reso);
    rumbleEngine.setBrakeParameter(brakeParam);
    if (! mPresetDir.exists())
        mPresetDir.createDirectory();
    updatePresetList();
}

BoutiqueRumbleAudioProcessor::~BoutiqueRumbleAudioProcessor() = default;

void BoutiqueRumbleAudioProcessor::setStandaloneClockBpm(double bpm) noexcept
{
    mDefaultBpm.store(juce::jlimit(40.0, 250.0, bpm));
}

double BoutiqueRumbleAudioProcessor::getStandaloneClockBpm() const noexcept
{
    return mDefaultBpm.load();
}

void BoutiqueRumbleAudioProcessor::setUseHostSync(bool shouldUseHostSync) noexcept
{
    mUseHostSync.store(shouldUseHostSync);
}

bool BoutiqueRumbleAudioProcessor::getUseHostSync() const noexcept
{
    return mUseHostSync.load();
}

double BoutiqueRumbleAudioProcessor::getCurrentClockBpmForUi() const noexcept
{
    return mCurrentClockBpmForUi.load();
}

void BoutiqueRumbleAudioProcessor::setScopeVisualiser(juce::AudioVisualiserComponent* visualiser) noexcept
{
    mScopeVisualiser = visualiser;
}

void BoutiqueRumbleAudioProcessor::updatePresetList()
{
    if (! mPresetDir.exists())
        mPresetDir.createDirectory();

    mPresetFiles.clear();
    mPresetNames.clear();
    const juce::Array<juce::File> files = mPresetDir.findChildFiles(juce::File::TypesOfFileToFind::findFiles, false, "*");
    for (const auto& file : files)
    {
        const auto fileName = file.getFileName();
        const auto lowerName = fileName.toLowerCase();
        if (file.isHidden() || lowerName == ".ds_store" || lowerName == "desktop.ini")
            continue;
        if (! file.existsAsFile())
            continue;

        mPresetFiles.add(file);
    }

    std::sort(mPresetFiles.begin(), mPresetFiles.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    for (const auto& file : mPresetFiles)
        mPresetNames.add(file.getFileName());
    DBG("Presets found: " + juce::String(mPresetNames.size()));

    if (mPresetNames.isEmpty())
    {
        mCurrentPresetIndex = -1;
        return;
    }

    if (mCurrentPresetIndex < 0)
    {
        mCurrentPresetIndex = 0;
    }
    else
    {
        mCurrentPresetIndex = juce::jlimit(0, mPresetNames.size() - 1, mCurrentPresetIndex);
    }
}

bool BoutiqueRumbleAudioProcessor::loadPreset(int index)
{
    updatePresetList();
    if (index < 0 || index >= mPresetFiles.size())
        return false;

    DBG("Loading Preset: " + mPresetNames[index]);
    const auto presetFile = mPresetFiles[index];
    if (! presetFile.existsAsFile())
        return false;

    juce::MemoryBlock mb;
    presetFile.loadFileAsData(mb);
    std::unique_ptr<juce::XmlElement> xml = getXmlFromBinary(mb.getData(), static_cast<int>(mb.getSize()));
    if (xml == nullptr)
        xml = juce::XmlDocument::parse(presetFile);

    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        mCurrentPresetIndex = index;
        DBG("Successfully loaded preset: " + presetFile.getFileName());
        return true;
    }

    return false;
}

bool BoutiqueRumbleAudioProcessor::loadNextPreset()
{
    updatePresetList();
    if (mPresetNames.isEmpty())
        return false;

    mCurrentPresetIndex = (mCurrentPresetIndex + 1) % mPresetNames.size();
    return loadPreset(mCurrentPresetIndex);
}

bool BoutiqueRumbleAudioProcessor::loadPreviousPreset()
{
    updatePresetList();
    if (mPresetNames.isEmpty())
        return false;

    mCurrentPresetIndex = (mCurrentPresetIndex - 1 + mPresetNames.size()) % mPresetNames.size();
    return loadPreset(mCurrentPresetIndex);
}

bool BoutiqueRumbleAudioProcessor::savePreset(const juce::String& name)
{
    const juce::String trimmedName = name.trim();
    const juce::String rawFileName = juce::File(trimmedName).getFileName();
    const juce::String safeName = rawFileName.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_. ");
    if (safeName.isEmpty())
        return false;

    if (! mPresetDir.exists())
        mPresetDir.createDirectory();

    auto presetFile = mPresetDir.getChildFile(safeName);
    if (auto xml = apvts.copyState().createXml())
    {
        if (xml->writeTo(presetFile))
        {
            updatePresetList();
            for (int i = 0; i < mPresetFiles.size(); ++i)
            {
                if (mPresetFiles.getReference(i).getFileName() == presetFile.getFileName())
                {
                    mCurrentPresetIndex = i;
                    break;
                }
            }
            return true;
        }
    }
    return false;
}

juce::String BoutiqueRumbleAudioProcessor::getCurrentPresetDisplayName() const
{
    if (mCurrentPresetIndex >= 0 && mCurrentPresetIndex < mPresetNames.size())
        return mPresetNames[mCurrentPresetIndex];
    return "Default Rumble";
}

const juce::String BoutiqueRumbleAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BoutiqueRumbleAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool BoutiqueRumbleAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool BoutiqueRumbleAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double BoutiqueRumbleAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BoutiqueRumbleAudioProcessor::getNumPrograms()
{
    return 1;
}

int BoutiqueRumbleAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BoutiqueRumbleAudioProcessor::setCurrentProgram (int)
{
}

const juce::String BoutiqueRumbleAudioProcessor::getProgramName (int)
{
    return {};
}

void BoutiqueRumbleAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void BoutiqueRumbleAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    mInternalPpq = 0.0;
    mActiveNotes.clear();
    const int numChannels = juce::jmax(1, getTotalNumOutputChannels());
    mOutputDcBlockPrevIn.assign(static_cast<size_t>(numChannels), 0.0f);
    mOutputDcBlockPrevOut.assign(static_cast<size_t>(numChannels), 0.0f);
    rumbleEngine.prepare(sampleRate);
}

void BoutiqueRumbleAudioProcessor::releaseResources()
{
    mOutputDcBlockPrevIn.clear();
    mOutputDcBlockPrevOut.clear();
}

#if ! JucePlugin_PreferredChannelConfigurations
bool BoutiqueRumbleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    {
        return false;
    }

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    {
        return false;
    }
#endif

    return true;
#endif
}
#endif

juce::AudioProcessorValueTreeState::ParameterLayout BoutiqueRumbleAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Mapping our research paper concepts to actual sliders
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::pulse, "Pulse", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::shape, "Shape", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::grit, "Grit", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::girth, "Girth", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::harmony, "Harmony", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        IDs::rate,
        "Rate",
        juce::StringArray { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" },
        6));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::skip_prob, "Fault", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::brake, "Break", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        IDs::cutoff,
        "Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.01f, 0.2f),
        20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::reso, "Reso", 0.1f, 20.0f, 0.707f));

    return { params.begin(), params.end() };
}

void BoutiqueRumbleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
    {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    const float shape = (shapeParam != nullptr) ? shapeParam->load() : 0.0f;
    const float harmony = (harmonyParam != nullptr) ? harmonyParam->load() : 0.0f;
    const float grit = (gritParam != nullptr) ? gritParam->load() : 0.0f;
    const float girth = (girthParam != nullptr) ? girthParam->load() : 0.0f;
    const float pulse = (pulseParam != nullptr) ? pulseParam->load() : 0.5f;
    const int rateIndex = (rateParam != nullptr) ? juce::roundToInt(rateParam->load()) : 6;
    const float skipProbability = (skipParam != nullptr) ? skipParam->load() : 0.0f;
    const float cutoffHz = (cutoffParam != nullptr) ? cutoffParam->load() : 20000.0f;
    const float resonance = (resoParam != nullptr) ? resoParam->load() : 0.707f;

    double bpm = mDefaultBpm.load();
    double ppqPosition = mInternalPpq;
    bool isPlaying = true;
    bool hasPpqPosition = true;
    bool usingHostClock = false;
    if (mUseHostSync.load())
    {
        if (auto* playHead = getPlayHead())
        {
            if (const auto position = playHead->getPosition())
            {
                usingHostClock = true;
                isPlaying = position->getIsPlaying();

                if (const auto hostBpm = position->getBpm())
                {
                    bpm = *hostBpm;
                }

                if (const auto hostPpqPosition = position->getPpqPosition())
                {
                    ppqPosition = *hostPpqPosition;
                    hasPpqPosition = true;
                }
                else
                {
                    hasPpqPosition = false;
                }
            }
        }
    }

    if (! usingHostClock)
    {
        const double sampleRate = juce::jmax(1.0, getSampleRate());
        const double internalBpm = mDefaultBpm.load();

        const double quarterNotesPerSecond = internalBpm / 60.0;
        const double blockIncrement = (static_cast<double>(buffer.getNumSamples()) / sampleRate) * quarterNotesPerSecond;

        bpm = internalBpm;
        ppqPosition = mInternalPpq;
        hasPpqPosition = true;
        isPlaying = true;

        mInternalPpq += blockIncrement;
        if (mInternalPpq > 100000.0)
        {
            mInternalPpq = std::fmod(mInternalPpq, 64.0);
        }
    }
    else
    {
        mInternalPpq = ppqPosition;
    }

    mCurrentClockBpmForUi.store(juce::jlimit(40.0, 250.0, bpm));

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const int noteNumber = message.getNoteNumber();
            mActiveNotes.erase(std::remove(mActiveNotes.begin(), mActiveNotes.end(), noteNumber), mActiveNotes.end());
            mActiveNotes.push_back(noteNumber);
            rumbleEngine.noteOn(noteNumber, message.getFloatVelocity());
        }
        else if (message.isNoteOff())
        {
            const int noteNumber = message.getNoteNumber();
            mActiveNotes.erase(std::remove(mActiveNotes.begin(), mActiveNotes.end(), noteNumber), mActiveNotes.end());

            if (! mActiveNotes.empty())
            {
                rumbleEngine.noteOn(mActiveNotes.back(), 1.0f);
            }
            else
            {
                rumbleEngine.noteOff();
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            mActiveNotes.clear();
            rumbleEngine.noteOff();
        }
    }

    rumbleEngine.setShape(shape);
    rumbleEngine.setHarmony(harmony);
    rumbleEngine.setGrit(grit);
    rumbleEngine.setGirth(girth);
    rumbleEngine.setPulse(pulse);
    rumbleEngine.setRate(rateIndex);
    rumbleEngine.setSkipProbability(skipProbability);
    rumbleEngine.setCutoff(cutoffHz);
    rumbleEngine.setResonance(resonance);
    rumbleEngine.setTransportInfo(bpm, ppqPosition, isPlaying, hasPpqPosition);
    rumbleEngine.process(buffer);

    const float shapeClamped = juce::jlimit(0.0f, 1.0f, shape);
    const float gritClamped = juce::jlimit(0.0f, 1.0f, grit);
    const float preGain = masterOutputLevel;
    const float shapePenalty = juce::jmap(shapeClamped, 0.0f, 1.0f, 1.0f, 0.55f);
    const float gritPenalty = 1.0f - (std::pow(gritClamped, 1.5f) * 0.65f);
    const float finalPostGain = shapePenalty * gritPenalty;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float s = d[i] * preGain;
            s = std::tanh(s * 1.2f);
            d[i] = s * finalPostGain;
        }
    }

    // Final safety rail: remove any post-clipper DC as the last stage in the processor chain.
    const int numChannels = buffer.getNumChannels();
    if (static_cast<int>(mOutputDcBlockPrevIn.size()) < numChannels)
    {
        mOutputDcBlockPrevIn.resize(static_cast<size_t>(numChannels), 0.0f);
        mOutputDcBlockPrevOut.resize(static_cast<size_t>(numChannels), 0.0f);
    }
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* d = buffer.getWritePointer(ch);
        float prevIn = mOutputDcBlockPrevIn[static_cast<size_t>(ch)];
        float prevOut = mOutputDcBlockPrevOut[static_cast<size_t>(ch)];
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float x = d[i];
            const float y = x - prevIn + (outputDcBlockCoeff * prevOut);
            d[i] = y;
            prevIn = x;
            prevOut = y;
        }
        mOutputDcBlockPrevIn[static_cast<size_t>(ch)] = prevIn;
        mOutputDcBlockPrevOut[static_cast<size_t>(ch)] = prevOut;
    }

    if (mScopeVisualiser != nullptr)
        mScopeVisualiser->pushBuffer(buffer);
}

bool BoutiqueRumbleAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* BoutiqueRumbleAudioProcessor::createEditor()
{
    return new BoutiqueRumbleAudioProcessorEditor (*this);
}

void BoutiqueRumbleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        juce::MemoryOutputStream stream(destData, false);
        xml->writeTo(stream); // Save as clean text XML, no binary wrapper.
    }
}

void BoutiqueRumbleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const juce::String xmlText = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    if (auto xmlState = juce::XmlDocument::parse(xmlText))
    {
        if (xmlState->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BoutiqueRumbleAudioProcessor();
}
