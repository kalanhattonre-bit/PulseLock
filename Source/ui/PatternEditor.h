#pragma once

#include "Theme.h"
#include "../pattern/Pattern.h"

class PulseLockProcessor;

namespace pulselock::ui
{
    /** Draws and edits one lane of one pattern.

        Click empty space to add a point (and keep dragging to place it), drag a point to move it,
        double-click or right-click a point to delete it. Drag the small diamond in the middle of a
        sloped segment (or Alt-drag anywhere on it) to bend it. Points snap to the grid unless Shift
        is held. Every finished gesture is one undo step. */
    class PatternEditor final : public juce::Component, public juce::TooltipClient
    {
    public:
        explicit PatternEditor (PulseLockProcessor& processor);

        void setTarget (int patternIndex, Lane laneKind);
        void setGrid (int divisions, bool snapOn);

        /** Re-reads the lane from the processor (skipped while a gesture is in progress). */
        void refresh();

        /** phase 0..1; shown only when this pattern is the one playing. */
        void setPlayhead (float phase, bool visible);

        bool isEditing() const noexcept { return drag != Drag::none; }

        juce::String getTooltip() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        enum class Drag { none, point, bend };

        juce::Rectangle<float> plotArea() const;
        juce::Point<float> toScreen (float x, float y) const;
        float xFromScreen (float sx) const;
        float yFromScreen (float sy) const;
        float snapX (float x, bool free) const;

        int pointAt (juce::Point<float> position) const;
        int bendHandleAt (juce::Point<float> position) const;
        int segmentAt (float x) const;
        bool hasBendHandle (int segment) const;
        juce::Point<float> bendHandlePosition (int segment) const;

        void pushLive();
        void commit (const juce::String& undoName);
        void deletePoint (int index);

        void paintGrid (juce::Graphics&, juce::Rectangle<float> area) const;
        void paintCurve (juce::Graphics&, juce::Rectangle<float> area) const;

        PulseLockProcessor& processor;
        int pattern = 0;
        Lane lane = Lane::volume;
        int grid = 16;
        bool snap = true;

        std::vector<CurvePoint> points, beforeGesture;
        Drag drag = Drag::none;
        int dragIndex = -1;
        float bendAtStart = 0.0f;
        juce::Point<float> dragOrigin;
        juce::Point<float> grabOffset;      // point centre minus where it was grabbed, so it never jumps
        float grabX = 0.0f;                 // x holds still until the drag clearly moves sideways
        bool xUnlocked = false;
        bool lastClickGrabbedPoint = false; // a double-click deletes only a point that was already there
        juce::String gestureName;

        int hoverPoint = -1, hoverBend = -1;
        float playPhase = 0.0f;
        bool playVisible = false;
    };
}
