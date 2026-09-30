#include "PatternEditor.h"
#include "../PluginProcessor.h"

#include <cmath>

namespace pulselock::ui
{
    namespace
    {
        constexpr float hitRadius = 7.0f;

        bool samePoints (const std::vector<CurvePoint>& a, const std::vector<CurvePoint>& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (! juce::exactlyEqual (a[i].x, b[i].x) || ! juce::exactlyEqual (a[i].y, b[i].y)
                    || ! juce::exactlyEqual (a[i].bend, b[i].bend))
                    return false;
            return true;
        }

        Curve toCurve (const std::vector<CurvePoint>& points)
        {
            Curve c;
            c.count = (int) std::min (points.size(), (size_t) Curve::maxPoints);
            for (int i = 0; i < c.count; ++i)
                c.points[(size_t) i] = points[(size_t) i];
            return c;
        }
    }

    PatternEditor::PatternEditor (PulseLockProcessor& p) : processor (p)
    {
        refresh();
    }

    void PatternEditor::setTarget (int patternIndex, Lane laneKind)
    {
        // Mid-gesture, finish on the lane that was grabbed; the switch happens on the next tick.
        if (drag != Drag::none || (patternIndex == pattern && laneKind == lane))
            return;

        pattern = patternIndex;
        lane = laneKind;
        drag = Drag::none;
        refresh();
    }

    void PatternEditor::setGrid (int divisions, bool snapOn)
    {
        if (divisions != grid || snapOn != snap)
        {
            grid = juce::jmax (1, divisions);
            snap = snapOn;
            repaint();
        }
    }

    void PatternEditor::refresh()
    {
        if (drag != Drag::none)
            return;

        points = processor.getLanePoints (pattern, lane);
        hoverPoint = hoverBend = -1;
        repaint();
    }

    void PatternEditor::setPlayhead (float phase, bool visible)
    {
        if (visible != playVisible || (visible && std::abs (phase - playPhase) > 1.0e-4f))
        {
            playPhase = phase;
            playVisible = visible;
            repaint();
        }
    }

    //==============================================================================
    juce::Rectangle<float> PatternEditor::plotArea() const
    {
        return getLocalBounds().toFloat().reduced (10.0f, 12.0f).withTrimmedLeft (30.0f);
    }

    juce::Point<float> PatternEditor::toScreen (float x, float y) const
    {
        const auto a = plotArea();
        return { a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight() };
    }

    float PatternEditor::xFromScreen (float sx) const
    {
        const auto a = plotArea();
        return juce::jlimit (0.0f, 1.0f, (sx - a.getX()) / a.getWidth());
    }

    float PatternEditor::yFromScreen (float sy) const
    {
        const auto a = plotArea();
        const float y = juce::jlimit (0.0f, 1.0f, (a.getBottom() - sy) / a.getHeight());
        // Land exactly on fully open / fully shut when close.
        if (y < 0.015f) return 0.0f;
        if (y > 0.985f) return 1.0f;
        return y;
    }

    float PatternEditor::snapX (float x, bool free) const
    {
        if (! snap || free)
            return x;
        return juce::jlimit (0.0f, 1.0f, std::round (x * (float) grid) / (float) grid);
    }

    int PatternEditor::pointAt (juce::Point<float> position) const
    {
        int best = -1;
        float bestDistance = hitRadius;
        for (int i = 0; i < (int) points.size(); ++i)
        {
            const float d = toScreen (points[(size_t) i].x, points[(size_t) i].y).getDistanceFrom (position);
            if (d <= bestDistance)
            {
                best = i;
                bestDistance = d;
            }
        }
        return best;
    }

    int PatternEditor::segmentAt (float x) const
    {
        for (int i = (int) points.size() - 2; i >= 0; --i)
            if (points[(size_t) i].x <= x && points[(size_t) (i + 1)].x > points[(size_t) i].x)
                return i;
        return -1;
    }

    bool PatternEditor::hasBendHandle (int segment) const
    {
        if (segment < 0 || segment + 1 >= (int) points.size())
            return false;

        const auto& a = points[(size_t) segment];
        const auto& b = points[(size_t) (segment + 1)];
        const float widthOnScreen = (b.x - a.x) * plotArea().getWidth();
        return widthOnScreen > 18.0f && std::abs (b.y - a.y) > 0.02f;
    }

    juce::Point<float> PatternEditor::bendHandlePosition (int segment) const
    {
        const auto& a = points[(size_t) segment];
        const auto& b = points[(size_t) (segment + 1)];
        const float mid = 0.5f * (a.x + b.x);
        return toScreen (mid, a.y + (b.y - a.y) * bendShape (0.5f, a.bend));
    }

    int PatternEditor::bendHandleAt (juce::Point<float> position) const
    {
        for (int i = 0; i + 1 < (int) points.size(); ++i)
            if (hasBendHandle (i) && bendHandlePosition (i).getDistanceFrom (position) <= hitRadius)
                return i;
        return -1;
    }

    juce::String PatternEditor::getTooltip()
    {
        return "Click: add a point.  Drag: move it (Shift: off the grid).  Double-click or right-click: delete.  "
               "Drag a diamond, or Alt-drag a slope: bend it.";
    }

    //==============================================================================
    void PatternEditor::pushLive()
    {
        processor.setLanePoints (pattern, lane, points, {});
    }

    void PatternEditor::commit (const juce::String& undoName)
    {
        // One undo step from the shape before the gesture to the shape after it, published once.
        processor.commitLaneEdit (pattern, lane, beforeGesture, points, undoName);
        points = processor.getLanePoints (pattern, lane);
        repaint();
    }

    void PatternEditor::deletePoint (int index)
    {
        if (index <= 0 || index >= (int) points.size() - 1)
            return;   // the first and last points always stay

        beforeGesture = points;
        points.erase (points.begin() + index);
        hoverPoint = -1;
        commit ("Delete point");
    }

    void PatternEditor::mouseDown (const juce::MouseEvent& e)
    {
        if (drag != Drag::none)
            return;

        const auto position = e.position;
        const int hit = pointAt (position);

        if (e.mods.isPopupMenu())
        {
            deletePoint (hit);
            return;
        }

        // A double-click deletes a point that was there before the first click. On empty space the
        // first click already added a point, and the second click leaves it alone.
        if (e.getNumberOfClicks() >= 2)
        {
            if (lastClickGrabbedPoint)
                deletePoint (hit);
            return;
        }

        lastClickGrabbedPoint = hit >= 0;
        beforeGesture = points;

        if (hit >= 0)
        {
            const auto& p = points[(size_t) hit];
            drag = Drag::point;
            dragIndex = hit;
            grabOffset = toScreen (p.x, p.y) - position;
            grabX = p.x;
            xUnlocked = false;
            gestureName = "Move point";
            return;
        }

        const int handle = bendHandleAt (position);
        const int segment = handle >= 0 ? handle : (e.mods.isAltDown() ? segmentAt (xFromScreen (position.x)) : -1);
        if (segment >= 0)
        {
            drag = Drag::bend;
            dragIndex = segment;
            bendAtStart = points[(size_t) segment].bend;
            dragOrigin = position;
            gestureName = "Bend";
            return;
        }

        if ((int) points.size() >= Curve::maxPoints)
            return;

        // A new point, kept in x order, and dragged from here on.
        const float x = snapX (xFromScreen (position.x), e.mods.isShiftDown());
        const float y = yFromScreen (position.y);
        grabOffset = {};
        xUnlocked = true;

        // On either edge there already is a point (the start or the end): take hold of that one
        // instead of stacking a second point on top of it.
        if (x <= points.front().x || x >= points.back().x)
        {
            dragIndex = x <= points.front().x ? 0 : (int) points.size() - 1;
            points[(size_t) dragIndex].y = y;
            drag = Drag::point;
            gestureName = "Move point";
            pushLive();
            repaint();
            return;
        }

        int insertAt = 1;
        while (insertAt < (int) points.size() - 1 && points[(size_t) insertAt].x <= x)
            ++insertAt;

        points.insert (points.begin() + insertAt, CurvePoint { x, y, 0.0f });
        drag = Drag::point;
        dragIndex = insertAt;
        gestureName = "Add point";
        pushLive();
        repaint();
    }

    void PatternEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag == Drag::point && juce::isPositiveAndBelow (dragIndex, (int) points.size()))
        {
            auto& p = points[(size_t) dragIndex];
            const auto target = e.position + grabOffset;
            const bool endpoint = dragIndex == 0 || dragIndex == (int) points.size() - 1;
            if (! endpoint)
            {
                // Moving a point up or down leaves it where it was sideways (an off-grid gate edge stays
                // put) until the drag clearly heads left or right.
                if (! xUnlocked && std::abs (e.getDistanceFromDragStartX()) > 4)
                    xUnlocked = true;

                const float lo = points[(size_t) (dragIndex - 1)].x;
                const float hi = points[(size_t) (dragIndex + 1)].x;
                p.x = xUnlocked ? juce::jlimit (lo, hi, snapX (xFromScreen (target.x), e.mods.isShiftDown()))
                                : grabX;
            }
            p.y = yFromScreen (target.y);
            pushLive();
            repaint();
        }
        else if (drag == Drag::bend && dragIndex + 1 < (int) points.size())
        {
            // Dragging up always bulges the curve up, whichever way the segment slopes.
            const auto& a = points[(size_t) dragIndex];
            const auto& b = points[(size_t) (dragIndex + 1)];
            const float dy = (dragOrigin.y - e.position.y) / 120.0f;
            const bool rising = b.y > a.y;
            points[(size_t) dragIndex].bend = juce::jlimit (-1.0f, 1.0f, bendAtStart + (rising ? -dy : dy));
            pushLive();
            repaint();
        }
    }

    void PatternEditor::mouseUp (const juce::MouseEvent&)
    {
        if (drag == Drag::none)
            return;

        drag = Drag::none;
        if (! samePoints (points, beforeGesture))
            commit (gestureName);
    }

    void PatternEditor::mouseMove (const juce::MouseEvent& e)
    {
        const int p = pointAt (e.position);
        const int b = p < 0 ? bendHandleAt (e.position) : -1;
        if (p != hoverPoint || b != hoverBend)
        {
            hoverPoint = p;
            hoverBend = b;
            setMouseCursor (p >= 0 ? juce::MouseCursor::DraggingHandCursor
                                   : (b >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor));
            repaint();
        }
    }

    void PatternEditor::mouseExit (const juce::MouseEvent&)
    {
        hoverPoint = hoverBend = -1;
        repaint();
    }

    //==============================================================================
    void PatternEditor::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (Theme::display);
        g.fillRoundedRectangle (bounds, Theme::corner);
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), Theme::corner, 1.0f);

        const auto area = plotArea();
        paintGrid (g, area);
        paintCurve (g, area);
    }

    void PatternEditor::paintGrid (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        // Every grid step, with each quarter of the pattern stronger.
        for (int k = 0; k <= grid; ++k)
        {
            const float x = area.getX() + area.getWidth() * (float) k / (float) grid;
            const bool strong = k == 0 || k == grid || (grid >= 4 && k % (grid / 4) == 0);
            g.setColour (Theme::outline.withAlpha (strong ? 0.9f : 0.45f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        }

        for (int k = 0; k <= 4; ++k)
        {
            const float y = area.getBottom() - area.getHeight() * (float) k / 4.0f;
            const bool centre = lane == Lane::pan && k == 2;
            g.setColour (Theme::outline.withAlpha (centre ? 0.9f : 0.35f));
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        }

        // What the top and bottom of this lane mean.
        const char* top = lane == Lane::volume ? "100%" : (lane == Lane::filter ? "HIGH" : "R");
        const char* bottom = lane == Lane::volume ? "0" : (lane == Lane::filter ? "LOW" : "L");
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (10.0f, true, 0.08f));
        const auto labels = juce::Rectangle<float> (area.getX() - 34.0f, area.getY(), 30.0f, area.getHeight());
        g.drawText (top, labels.withHeight (12.0f).translated (0.0f, -5.0f), juce::Justification::centredRight, false);
        g.drawText (bottom, labels.withTop (labels.getBottom() - 12.0f).translated (0.0f, 5.0f), juce::Justification::centredRight, false);
        if (lane == Lane::pan)
            g.drawText ("C", labels.withSizeKeepingCentre (labels.getWidth(), 12.0f), juce::Justification::centredRight, false);
    }

    void PatternEditor::paintCurve (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        if (points.empty())
            return;

        const auto curve = toCurve (points);

        // The curve, sampled finely enough to show vertical edges as vertical.
        juce::Path line, fill;
        const int steps = juce::jmax (2, (int) (area.getWidth() / 1.5f));
        fill.startNewSubPath (area.getX(), area.getBottom());
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            const auto p = toScreen (x, curve.valueAt (x));
            if (i == 0) line.startNewSubPath (p);
            else        line.lineTo (p);
            fill.lineTo (p);
        }
        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();

        g.setColour (Theme::accentAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (Theme::accent);
        g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bend handles on sloped segments.
        for (int i = 0; i + 1 < (int) points.size(); ++i)
        {
            if (! hasBendHandle (i))
                continue;
            const auto c = bendHandlePosition (i);
            juce::Path diamond;
            diamond.addQuadrilateral (c.x, c.y - 4.5f, c.x + 4.5f, c.y, c.x, c.y + 4.5f, c.x - 4.5f, c.y);
            g.setColour (i == hoverBend ? Theme::text : Theme::accentAlpha (0.55f));
            g.fillPath (diamond);
        }

        // Points.
        for (int i = 0; i < (int) points.size(); ++i)
        {
            const auto c = toScreen (points[(size_t) i].x, points[(size_t) i].y);
            const bool hot = i == hoverPoint || (drag == Drag::point && i == dragIndex);
            const float r = hot ? 5.0f : 3.5f;
            g.setColour (Theme::display);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 3.0f, r * 2.0f + 3.0f).withCentre (c));
            g.setColour (hot ? Theme::text : Theme::accent);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        }

        // Playhead.
        if (playVisible)
        {
            const float x = area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, playPhase);
            g.setColour (Theme::text.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
            const auto dot = toScreen (juce::jlimit (0.0f, 1.0f, playPhase), curve.valueAt (playPhase));
            g.setColour (Theme::text);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
        }
    }
}
