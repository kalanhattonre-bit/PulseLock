#include "PluginProcessor.h"
#include "PluginEditor.h"

PulseLockProcessor::PulseLockProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PulseLockState", createParameterLayout())
{
    outGainParam = apvts.getRawParameterValue ("outGain");
}

juce::AudioProcessorValueTreeState::ParameterLayout PulseLockProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "outGain", 1 }, "Output Gain",
                                                             juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

void PulseLockProcessor::prepareToPlay (double sampleRate, int)
{
    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outGainParam->load()));
}

void PulseLockProcessor::releaseResources() {}

bool PulseLockProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void PulseLockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIns = getTotalNumInputChannels();

    // Mono in, stereo out: the right output starts as whatever the host left there, so copy the left.
    if (numIns == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (outGainParam->load()));

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        left[i] *= g;
        if (right != nullptr)
            right[i] *= g;
    }
}

juce::AudioProcessorEditor* PulseLockProcessor::createEditor()
{
    return new PulseLockEditor (*this);
}

void PulseLockProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PulseLockProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PulseLockProcessor();
}
