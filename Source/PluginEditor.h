#pragma once

#include "PluginProcessor.h"

class PulseLockEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PulseLockEditor (PulseLockProcessor&);
    ~PulseLockEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PulseLockProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseLockEditor)
};
