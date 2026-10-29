/*
  ==============================================================================

    IRON DISTORTION - distortion pedal

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
    // Engine control ids, in the order the engine expects them.
    const char* const parameterIds[] = { "mode", "gain", "level", "tone", "bass", "mid", "treble", "contour", "presence" };
}

//==============================================================================
NewProjectAudioProcessor::NewProjectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#else
    :
#endif
    parameters (*this, nullptr, juce::Identifier ("PARAMETERS"), createParameterLayout())
{
    for (int i = 0; i < numEngineParams; ++i)
        paramPointers[i] = parameters.getRawParameterValue (parameterIds[i]);

    bypassParam = parameters.getRawParameterValue ("bypass");
}

NewProjectAudioProcessor::~NewProjectAudioProcessor()
{
}

//==============================================================================
const juce::String NewProjectAudioProcessor::getName() const { return JucePlugin_Name; }

bool NewProjectAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool NewProjectAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool NewProjectAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double NewProjectAudioProcessor::getTailLengthSeconds() const  { return 0.2; }

int NewProjectAudioProcessor::getNumPrograms()                 { return 1; }
int NewProjectAudioProcessor::getCurrentProgram()              { return 0; }
void NewProjectAudioProcessor::setCurrentProgram (int)         {}
const juce::String NewProjectAudioProcessor::getProgramName (int) { return {}; }
void NewProjectAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void NewProjectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    engine.prepare (sampleRate);
    engine.updateParameters();
}

void NewProjectAudioProcessor::releaseResources()
{
    engine.clear();
}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool NewProjectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void NewProjectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    if (numSamples == 0 || totalNumOutputChannels == 0)
        return;

    // ---- Footswitch: leave the signal completely untouched when off ----
    if (bypassParam != nullptr && bypassParam->load() > 0.5f)
        return;

    // ---- Push control values into the engine once per block ----
    for (int i = 0; i < numEngineParams; ++i)
        if (paramPointers[i] != nullptr)
            engine.setParam (i, paramPointers[i]->load());

    engine.updateParameters();

    // ---- Host tempo, for sync where the pedal supports it ----
    if (auto* playHead = getPlayHead())
    {
       #if defined (JUCE_MAJOR_VERSION) && JUCE_MAJOR_VERSION >= 7
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm())
                engine.setHostTempo (*bpm);
       #else
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (playHead->getCurrentPosition (info))
            engine.setHostTempo (info.bpm);
       #endif
    }

    const bool stereo = (totalNumInputChannels >= 2 && totalNumOutputChannels >= 2);
    auto* left  = buffer.getWritePointer (0);
    auto* right = stereo ? buffer.getWritePointer (1) : nullptr;

    for (int n = 0; n < numSamples; ++n)
    {
        const float inL = left[n];
        const float inR = stereo ? right[n] : inL;

        float outL = 0.0f, outR = 0.0f;
        engine.process (inL, inR, outL, outR);

        if (! std::isfinite (outL)) outL = 0.0f;
        if (! std::isfinite (outR)) outR = 0.0f;

        left[n] = juce::jlimit (-1.0f, 1.0f, outL);

        if (stereo)
            right[n] = juce::jlimit (-1.0f, 1.0f, outR);
    }
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor (*this);
}

//==============================================================================
void NewProjectAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void NewProjectAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
NewProjectAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 }, "Mode",
        juce::StringArray { "Hard Clip", "Diode", "Scooped", "Modern" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 }, "Gain",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 6.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "level", 1 }, "Level",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tone", 1 }, "Tone",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 6.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bass", 1 }, "Bass",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mid", 1 }, "Mid",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "treble", 1 }, "Treble",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "contour", 1 }, "Contour",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "presence", 1 }, "Presence",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));

    // The footswitch. True means the pedal is off and passing signal through.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}
