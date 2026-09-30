#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"
#include "../Parameters.h"

namespace pulselock::ui
{
    /** Patterns A to H. The filled one is playing; the outlined one is the one being edited
        (the Pattern parameter); a dashed outline waits for the next beat or bar. Each shows the
        MIDI key that switches to it. Clicking one selects it for editing and playing. */
    class PatternButtons final : public juce::Component
    {
    public:
        explicit PatternButtons (juce::RangedAudioParameter& patternParameter);

        /** Called on every click, including on the pattern already selected. */
        std::function<void (int)> onPick;

        void setPlaying (int playingPattern, int pendingPattern);
        void setKeyStart (int firstNote);
        int getEdited() const noexcept { return edited; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        int cellAt (juce::Point<float> position) const;

        int edited = 0, playing = 0, pending = -1, keyStart = 36, hovered = -1;
        juce::ParameterAttachment attachment;
    };

    /** VOLUME / FILTER / PAN. Click a name to edit that lane; click its light to switch it on or off. */
    class LaneTabs final : public juce::Component
    {
    public:
        explicit LaneTabs (juce::AudioProcessorValueTreeState& state);

        std::function<void (int)> onSelect;
        void setSelected (int lane);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> tabBounds (int lane) const;
        juce::Rectangle<float> lightBounds (int lane) const;

        std::array<std::unique_ptr<juce::ParameterAttachment>, numLanes> attachments;
        std::array<bool, numLanes> on {};
        int selected = 0, hovered = -1;
        bool hoveringLight = false;
    };

    /** A row of text choices that are not parameters (the editor's grid). */
    class TextChoice final : public juce::Component
    {
    public:
        TextChoice (juce::StringArray labels, std::function<void (int)> onChange);

        void setSelected (int index);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::StringArray labels;
        std::function<void (int)> changed;
        int selected = 0;
    };

    /** A small flat button showing a word or a drawn icon; optionally a toggle. */
    class SmallButton final : public juce::Button
    {
    public:
        using IconPainter = std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)>;

        SmallButton (const juce::String& label, const juce::String& tooltip, IconPainter icon = nullptr);
        void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    private:
        IconPainter icon;
    };

    /** A thin keyboard across the whole MIDI range. Held keys light up; the eight pattern keys
        are underlined with their pattern letters. Display only. */
    class KeyStrip final : public juce::Component
    {
    public:
        void update (const std::array<juce::uint64, 2>& held, int patternKeyStart);
        void paint (juce::Graphics&) override;

    private:
        std::array<juce::uint64, 2> heldNotes {};
        int keyStart = 36;
    };
}
