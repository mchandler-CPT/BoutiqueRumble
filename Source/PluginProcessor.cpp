#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParamConstants.h"

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
    shapeParam = apvts.getRawParameterValue(IDs::shape);
    harmonyParam = apvts.getRawParameterValue(IDs::harmony);
    gritParam = apvts.getRawParameterValue(IDs::grit);
}

BoutiqueRumbleAudioProcessor::~BoutiqueRumbleAudioProcessor() = default;

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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(IDs::harmony, "Harmony", 0.0f, 1.0f, 0.0f));

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

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            rumbleEngine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
        }
        else if (message.isNoteOff() || message.isAllNotesOff() || message.isAllSoundOff())
        {
            rumbleEngine.noteOff();
        }
    }

    rumbleEngine.setShape(shape);
    rumbleEngine.setHarmony(harmony);
    rumbleEngine.setGrit(grit);
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
