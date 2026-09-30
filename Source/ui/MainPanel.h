#pragma once

#include "Controls.h"
#include "PatternBar.h"
#include "PatternEditor.h"
#include "../dsp/ScopeFrame.h"

class PulseLockProcessor;

namespace pulselock::ui
{
    /** The whole interface at its base size (780 x 440). The editor scales it to the window. */
    class MainPanel final : public juce::Component
    {
    public:
        explicit MainPanel (PulseLockProcessor& processor);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Called about 30 times a second. frame is null when the audio thread sent nothing new. */
        void tick (const ScopeFrame* frame);

    private:
        struct Section
        {
            juce::String title;
            juce::Rectangle<int> bounds;
        };

        void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> cells);
        void refreshPresetBox();
        void stepPreset (int delta);
        float plainValue (const char* id) const;
        int editedPattern() const;

        void selectLane (int lane);
        void setGrid (int index);
        void stamp (const juce::String& name, std::vector<CurvePoint> points);
        void copyPattern();
        void pastePattern();

        PulseLockProcessor& processor;
        juce::AudioProcessorValueTreeState& state;

        // Top bar
        ChevronButton previousPreset { false }, nextPreset { true };
        PresetButton presetBox;

        // Pattern bar and editor
        PatternButtons patterns;
        LaneTabs lanes;
        PatternEditor editor;
        int editLane = 0;

        // Tool strip
        TextChoice gridChoice;
        SmallButton snapButton, gateButton, pumpButton, rampUpButton, rampDownButton, triangleButton,
                    randomButton, flatButton, invertButton, copyButton, pasteButton, undoButton, redoButton;
        std::array<std::vector<CurvePoint>, numLanes> clipboard;
        bool hasClipboard = false;
        juce::Random random;

        // Knob row
        juce::ComboBox length, trigger, switchMode, filterType;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
        Knob rate, keys, volumeDepth, filterLow, filterHigh, filterRes, panDepth, smooth, mix, gain;
        juce::Rectangle<int> lengthCaption, triggerCaption, switchCaption, typeCaption, statusArea;
        juce::String statusText;

        KeyStrip keyStrip;

        std::array<Section, 4> sections;
        ScopeFrame lastFrame;
        juce::String shownPresetName;
        int shownRevision = -1;
    };
}
