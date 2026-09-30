#include "PluginEditor.h"

namespace
{
    constexpr int baseWidth = 780;
    constexpr int baseHeight = 440;
}

PulseLockEditor::PulseLockEditor (PulseLockProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (baseWidth, baseHeight);
}

void PulseLockEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1c1e));
    g.setColour (juce::Colour (0xff2bd4c4));
    g.setFont (juce::FontOptions ((float) getHeight() * 0.08f));
    g.drawText ("PULSELOCK", getLocalBounds(), juce::Justification::centred);
}

void PulseLockEditor::resized() {}
