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
       apvts (*this, nullptr, "RUMBLE_PARAMS", createParameterLayout()) // The Handshake
{
    pulseParam = apvts.getRawParameterValue(IDs::pulse);
    shapeParam = apvts.getRawParameterValue(IDs::shape);
    harmonyParam = apvts.getRawParameterValue(IDs::harmony);
    gritParam = apvts.getRawParameterValue(IDs::grit);
    girthParam = apvts.getRawParameterValue(IDs::girth);
    rateParam = apvts.getRawParameterValue(IDs::rate);
}

BoutiqueRumbleAudioProcessor::~BoutiqueRumbleAudioProcessor() = default;

void BoutiqueRumbleAudioProcessor::setStandaloneClockBpm(double bpm) noexcept
{
    mDefaultBpm.store(juce::jlimit(40.0, 220.0, bpm));
}

double BoutiqueRumbleAudioProcessor::getStandaloneClockBpm() const noexcept
{
    return mDefaultBpm.load();
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
    rumbleEngine.prepare(sampleRate);
}

void BoutiqueRumbleAudioProcessor::releaseResources()
{
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

    double bpm = mDefaultBpm.load();
    double ppqPosition = mInternalPpq;
    bool isPlaying = true;
    bool hasPpqPosition = true;
    bool usingHostClock = false;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            const bool hostIsPlaying = position->getIsPlaying();
            if (hostIsPlaying)
            {
                usingHostClock = true;
                isPlaying = true;

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
        const double pulseAsDouble = static_cast<double>(pulse);
        const double defaultBpm = mDefaultBpm.load();

       #if JucePlugin_Build_Standalone
        const double internalBpm = defaultBpm * juce::jmap(pulseAsDouble, 0.0, 1.0, 0.9, 1.1);
       #else
        const double internalBpm = defaultBpm;
       #endif

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
    rumbleEngine.setTransportInfo(bpm, ppqPosition, isPlaying, hasPpqPosition);
    rumbleEngine.process(buffer);
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
    juce::ignoreUnused (destData);
}

void BoutiqueRumbleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BoutiqueRumbleAudioProcessor();
}
