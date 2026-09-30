#include "PatternBar.h"

namespace pulselock::ui
{
    //==============================================================================
    PatternButtons::PatternButtons (juce::RangedAudioParameter& patternParameter)
        : attachment (patternParameter, [this] (float value)
                      {
                          edited = juce::jlimit (0, numPatterns - 1, juce::roundToInt (value));
                          repaint();
                      })
    {
        attachment.sendInitialUpdate();
    }

    void PatternButtons::setPlaying (int playingPattern, int pendingPattern)
    {
        if (playingPattern != playing || pendingPattern != pending)
        {
            playing = playingPattern;
            pending = pendingPattern;
            repaint();
        }
    }

    void PatternButtons::setKeyStart (int firstNote)
    {
        if (firstNote != keyStart)
        {
            keyStart = firstNote;
            repaint();
        }
    }

    int PatternButtons::cellAt (juce::Point<float> position) const
    {
        if (getWidth() <= 0)
            return -1;
        return juce::jlimit (0, numPatterns - 1, (int) (position.x * (float) numPatterns / (float) getWidth()));
    }

    void PatternButtons::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        const float w = bounds.getWidth() / (float) numPatterns;

        for (int i = 0; i < numPatterns; ++i)
        {
            const auto cell = juce::Rectangle<float> (bounds.getX() + w * (float) i, bounds.getY(), w, bounds.getHeight()).reduced (2.5f, 0.5f);
            const bool isPlaying = i == playing;
            const bool isEdited = i == edited;

            g.setColour (isPlaying ? Theme::accent : (i == hovered ? Theme::panelRaised.brighter (0.08f) : Theme::panelRaised));
            g.fillRoundedRectangle (cell, 4.0f);

            if (isEdited)
            {
                g.setColour (isPlaying ? Theme::text : Theme::accent);
                g.drawRoundedRectangle (cell.reduced (0.75f), 4.0f, 1.5f);
            }
            else if (i == pending)
            {
                juce::Path outline;
                outline.addRoundedRectangle (cell.reduced (0.75f), 4.0f);
                juce::Path dashed;
                const float dashes[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (Theme::accent);
                g.fillPath (dashed);
            }

            const auto ink = isPlaying ? Theme::onAccent : (isEdited ? Theme::text : Theme::textDim);
            const auto inner = cell.reduced (9.0f, 0.0f);
            g.setColour (ink);
            g.setFont (Theme::font (15.0f, true));
            g.drawText (patternName (i), inner, juce::Justification::centredLeft, false);

            g.setColour (ink.withAlpha (0.6f));
            g.setFont (Theme::font (9.5f, false, 0.04f));
            g.drawText (formatNoteName (keyStart + i), inner, juce::Justification::centredRight, false);
        }
    }

    void PatternButtons::mouseDown (const juce::MouseEvent& e)
    {
        const int index = cellAt (e.position);
        if (index < 0)
            return;

        attachment.setValueAsCompleteGesture ((float) index);
        if (onPick)
            onPick (index);
    }

    void PatternButtons::mouseMove (const juce::MouseEvent& e)
    {
        const int index = cellAt (e.position);
        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }

    void PatternButtons::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    //==============================================================================
    LaneTabs::LaneTabs (juce::AudioProcessorValueTreeState& state)
    {
        const char* ids[] = { ParamID::volumeOn, ParamID::filterOn, ParamID::panOn };
        for (int i = 0; i < numLanes; ++i)
        {
            attachments[(size_t) i] = std::make_unique<juce::ParameterAttachment> (
                *state.getParameter (ids[i]), [this, i] (float value)
                {
                    on[(size_t) i] = value >= 0.5f;
                    repaint();
                });
            attachments[(size_t) i]->sendInitialUpdate();
        }
    }

    void LaneTabs::setSelected (int lane)
    {
        lane = juce::jlimit (0, numLanes - 1, lane);
        if (lane != selected)
        {
            selected = lane;
            repaint();
        }
    }

    juce::Rectangle<float> LaneTabs::tabBounds (int lane) const
    {
        const auto b = getLocalBounds().toFloat();
        const float w = b.getWidth() / (float) numLanes;
        return juce::Rectangle<float> (b.getX() + w * (float) lane, b.getY(), w, b.getHeight()).reduced (2.5f, 0.5f);
    }

    juce::Rectangle<float> LaneTabs::lightBounds (int lane) const
    {
        const auto tab = tabBounds (lane);
        return tab.withWidth (tab.getHeight()).reduced (3.0f);
    }

    void LaneTabs::paint (juce::Graphics& g)
    {
        static const char* names[] = { "VOLUME", "FILTER", "PAN" };
        for (int i = 0; i < numLanes; ++i)
        {
            const auto tab = tabBounds (i);
            const bool isSelected = i == selected;
            const bool isOn = on[(size_t) i];

            g.setColour (isSelected ? Theme::panelRaised.brighter (0.06f)
                                    : (i == hovered ? Theme::panelRaised : Theme::panel));
            g.fillRoundedRectangle (tab, 4.0f);
            if (isSelected)
            {
                g.setColour (Theme::accent);
                g.fillRect (juce::Rectangle<float> (tab.getX() + 8.0f, tab.getBottom() - 2.0f, tab.getWidth() - 16.0f, 2.0f));
            }

            // The power light: a small switch of its own.
            const auto light = lightBounds (i);
            const auto dot = light.withSizeKeepingCentre (9.0f, 9.0f);
            if (i == hovered && hoveringLight)
            {
                g.setColour (Theme::text.withAlpha (0.12f));
                g.fillEllipse (light);
            }
            g.setColour (isOn ? Theme::accent : Theme::track);
            g.fillEllipse (dot);
            if (isOn)
            {
                g.setColour (Theme::accentAlpha (0.25f));
                g.fillEllipse (dot.expanded (3.0f));
            }

            g.setColour (isSelected ? Theme::text : (isOn ? Theme::textDim : Theme::textFaint));
            g.setFont (Theme::font (11.5f, isSelected, 0.12f));
            g.drawText (names[i], tab.withTrimmedLeft (light.getRight() - tab.getX() + 4.0f), juce::Justification::centredLeft, false);
        }
    }

    void LaneTabs::mouseDown (const juce::MouseEvent& e)
    {
        for (int i = 0; i < numLanes; ++i)
        {
            if (lightBounds (i).expanded (2.0f).contains (e.position))
            {
                attachments[(size_t) i]->setValueAsCompleteGesture (on[(size_t) i] ? 0.0f : 1.0f);
                return;
            }
            if (tabBounds (i).contains (e.position))
            {
                setSelected (i);
                if (onSelect)
                    onSelect (i);
                return;
            }
        }
    }

    void LaneTabs::mouseMove (const juce::MouseEvent& e)
    {
        int tab = -1;
        bool light = false;
        for (int i = 0; i < numLanes; ++i)
            if (tabBounds (i).contains (e.position))
            {
                tab = i;
                light = lightBounds (i).expanded (2.0f).contains (e.position);
            }

        if (tab != hovered || light != hoveringLight)
        {
            hovered = tab;
            hoveringLight = light;
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            repaint();
        }
    }

    void LaneTabs::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        hoveringLight = false;
        repaint();
    }

    //==============================================================================
    TextChoice::TextChoice (juce::StringArray choiceLabels, std::function<void (int)> onChange)
        : labels (std::move (choiceLabels)), changed (std::move (onChange))
    {
    }

    void TextChoice::setSelected (int index)
    {
        index = juce::jlimit (0, juce::jmax (0, labels.size() - 1), index);
        if (index != selected)
        {
            selected = index;
            repaint();
        }
    }

    void TextChoice::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const int n = juce::jmax (1, labels.size());
        const float w = bounds.getWidth() / (float) n;

        g.setColour (Theme::panelRaised);
        g.fillRoundedRectangle (bounds, 4.0f);

        for (int i = 0; i < n; ++i)
        {
            const auto seg = juce::Rectangle<float> (bounds.getX() + w * (float) i, bounds.getY(), w, bounds.getHeight());
            if (i == selected)
            {
                g.setColour (Theme::accent);
                g.fillRoundedRectangle (seg.reduced (2.0f), 3.0f);
            }
            g.setColour (i == selected ? Theme::onAccent : Theme::textDim);
            g.setFont (Theme::font (11.0f, i == selected));
            g.drawText (labels[i], seg, juce::Justification::centred, false);
        }

        g.setColour (Theme::outline);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    void TextChoice::mouseDown (const juce::MouseEvent& e)
    {
        const int n = juce::jmax (1, labels.size());
        const int index = juce::jlimit (0, n - 1, (int) (e.position.x * (float) n / (float) juce::jmax (1, getWidth())));
        if (index != selected)
        {
            selected = index;
            repaint();
            if (changed)
                changed (index);
        }
    }

    //==============================================================================
    SmallButton::SmallButton (const juce::String& label, const juce::String& tooltip, IconPainter iconPainter)
        : juce::Button (label), icon (std::move (iconPainter))
    {
        setTooltip (tooltip);
    }

    void SmallButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool lit = getToggleState();

        g.setColour (lit ? Theme::accent : (isButtonDown ? Theme::accentAlpha (0.25f) : Theme::panelRaised));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (isMouseOver ? Theme::accentAlpha (0.6f) : Theme::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        const auto ink = lit ? Theme::onAccent : (isMouseOver ? Theme::text : Theme::textDim);
        if (icon)
        {
            icon (g, r.reduced (r.getWidth() * 0.2f, r.getHeight() * 0.25f), ink);
        }
        else
        {
            g.setColour (ink);
            g.setFont (Theme::font (10.5f, lit, 0.08f));
            g.drawFittedText (getButtonText(), r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }

    //==============================================================================
    void KeyStrip::update (const std::array<juce::uint64, 2>& held, int patternKeyStart)
    {
        if (held != heldNotes || patternKeyStart != keyStart)
        {
            heldNotes = held;
            keyStart = patternKeyStart;
            repaint();
        }
    }

    void KeyStrip::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRoundedRectangle (bounds, 3.0f);

        auto isHeld = [this] (int note) { return ((heldNotes[(size_t) (note >> 6)] >> (note & 63)) & 1u) != 0; };
        auto isBlack = [] (int note) { const int k = note % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; };
        auto isPatternKey = [this] (int note) { return note >= keyStart && note < keyStart + numPatterns; };

        int whiteCount = 0;
        for (int note = 0; note < 128; ++note)
            if (! isBlack (note))
                ++whiteCount;

        const auto keys = bounds.reduced (1.0f);
        const float whiteWidth = keys.getWidth() / (float) whiteCount;
        std::array<float, 128> keyCentre {};

        int whiteIndex = 0;
        for (int note = 0; note < 128; ++note)
        {
            if (isBlack (note))
                continue;
            const auto key = juce::Rectangle<float> (keys.getX() + whiteWidth * (float) whiteIndex, keys.getY(),
                                                     whiteWidth, keys.getHeight()).reduced (0.5f, 0.0f);
            keyCentre[(size_t) note] = key.getCentreX();
            g.setColour (isHeld (note) ? Theme::accent : (isPatternKey (note) ? Theme::panelRaised.brighter (0.12f) : Theme::panelRaised));
            g.fillRect (key);
            ++whiteIndex;
        }

        whiteIndex = 0;
        for (int note = 0; note < 128; ++note)
        {
            if (! isBlack (note))
            {
                ++whiteIndex;
                continue;
            }
            const float x = keys.getX() + whiteWidth * (float) whiteIndex - whiteWidth * 0.32f;
            const auto key = juce::Rectangle<float> (x, keys.getY(), whiteWidth * 0.64f, keys.getHeight() * 0.58f);
            keyCentre[(size_t) note] = key.getCentreX();
            g.setColour (isHeld (note) ? Theme::accent : Theme::background);
            g.fillRect (key);
        }

        // Underline the eight pattern keys and number them 1-8: white keys low down, black keys on the
        // black key itself, so every pattern key is labelled and none is skipped.
        g.setFont (Theme::font (8.5f, true));
        float rangeLeft = keys.getRight(), rangeRight = keys.getX();
        for (int i = 0; i < numPatterns; ++i)
        {
            const int note = keyStart + i;
            if (note < 0 || note > 127)
                continue;
            const float cx = keyCentre[(size_t) note];
            rangeLeft = juce::jmin (rangeLeft, cx);
            rangeRight = juce::jmax (rangeRight, cx);

            const auto label = isBlack (note) ? juce::Rectangle<float> (cx - 6.0f, keys.getY() + 1.0f, 12.0f, 9.0f)
                                              : juce::Rectangle<float> (cx - 6.0f, keys.getBottom() - 11.0f, 12.0f, 9.0f);
            g.setColour (isHeld (note) ? Theme::onAccent : Theme::accent);
            g.drawText (patternName (i), label, juce::Justification::centred, false);
        }

        // One unbroken underline under the whole range, so the eight keys read as one group.
        if (rangeLeft <= rangeRight)
        {
            g.setColour (Theme::accent);
            g.fillRect (juce::Rectangle<float> (rangeLeft - whiteWidth * 0.3f, keys.getBottom() - 2.0f,
                                                rangeRight - rangeLeft + whiteWidth * 0.6f, 2.0f));
        }
    }
}
