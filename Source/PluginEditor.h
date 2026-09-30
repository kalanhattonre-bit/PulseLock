#pragma once

#include "PluginProcessor.h"
#include "ui/MainPanel.h"
#include "ui/PulseLookAndFeel.h"

class PulseLockEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PulseLockEditor (PulseLockProcessor&);
    ~PulseLockEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    PulseLockProcessor& audioProcessor;
    pulselock::ui::PulseLookAndFeel lookAndFeel;   // declared before the panel so it outlives it
    pulselock::ui::MainPanel panel;
    juce::TooltipWindow tooltips { this, 700 };
    bool constructed = false;   // resized() only saves the size once the constructor has set it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseLockEditor)
};
