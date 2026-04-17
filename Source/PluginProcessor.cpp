#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    // Any initialization logic that doesn't belong in the header 
    // initializer list goes here.
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

void BoutiqueRumbleAudioProcessor::prepareToPlay (double, int)
{
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
    params.push_back(std::make_unique<juce::AudioParameterFloat>("PULSE", "Pulse", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SHAPE", "Shape", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("GRIT",  "Grit",  0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("GIRTH", "Girth", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("HARMONY", "Harmony", 0.0f, 1.0f, 0.0f));

    return { params.begin(), params.end() };
}

void BoutiqueRumbleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    for (int channel = 0; channel < getTotalNumOutputChannels(); ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        juce::ignoreUnused (channelData);
    }
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
