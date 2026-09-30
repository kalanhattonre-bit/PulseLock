#include "MainPanel.h"
#include "../PluginProcessor.h"

namespace pulselock::ui
{
    namespace
    {
        constexpr int gridDivisions[] = { 4, 8, 16, 32 };

        void paintLogo (juce::Graphics& g, juce::Rectangle<float> area)
        {
            // A rounded frame holding one pulse: a drawn gate, locked to the grid.
            const float d = juce::jmin (area.getWidth(), area.getHeight());
            const auto frame = juce::Rectangle<float> (d, d).withCentre (area.getCentre()).reduced (1.5f);
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (frame, d * 0.22f, 2.0f);

            const auto inner = frame.reduced (d * 0.2f);
            juce::Path pulse;
            pulse.startNewSubPath (inner.getX(), inner.getBottom());
            pulse.lineTo (inner.getX() + inner.getWidth() * 0.3f, inner.getBottom());
            pulse.lineTo (inner.getX() + inner.getWidth() * 0.3f, inner.getY());
            pulse.lineTo (inner.getX() + inner.getWidth() * 0.7f, inner.getY());
            pulse.lineTo (inner.getX() + inner.getWidth() * 0.7f, inner.getBottom());
            pulse.lineTo (inner.getRight(), inner.getBottom());
            g.setColour (Theme::text);
            g.strokePath (pulse, juce::PathStrokeType (1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }

        using Painter = SmallButton::IconPainter;

        Painter strokeIcon (std::function<void (juce::Path&, juce::Rectangle<float>)> build)
        {
            return [build] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
            {
                juce::Path p;
                build (p, r);
                g.setColour (c);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            };
        }

        const Painter gateIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            const float w = r.getWidth() / 4.0f;
            p.startNewSubPath (r.getX(), r.getBottom());
            for (int i = 0; i < 2; ++i)
            {
                const float x = r.getX() + w * 2.0f * (float) i;
                p.lineTo (x, r.getY());
                p.lineTo (x + w, r.getY());
                p.lineTo (x + w, r.getBottom());
                p.lineTo (x + 2.0f * w, r.getBottom());
            }
        });

        const Painter pumpIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            const float w = r.getWidth() / 2.0f;
            p.startNewSubPath (r.getX(), r.getY());
            for (int i = 0; i < 2; ++i)
            {
                const float x = r.getX() + w * (float) i;
                p.lineTo (x, r.getBottom());
                p.quadraticTo (x + w * 0.15f, r.getY(), x + w, r.getY());
            }
        });

        const Painter rampUpIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            p.startNewSubPath (r.getBottomLeft());
            p.lineTo (r.getTopRight());
        });

        const Painter rampDownIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            p.startNewSubPath (r.getTopLeft());
            p.lineTo (r.getBottomRight());
        });

        const Painter triangleIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            p.startNewSubPath (r.getBottomLeft());
            p.lineTo (r.getX() + r.getWidth() * 0.25f, r.getY());
            p.lineTo (r.getCentreX(), r.getBottom());
            p.lineTo (r.getX() + r.getWidth() * 0.75f, r.getY());
            p.lineTo (r.getBottomRight());
        });

        const Painter randomIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            const float levels[] = { 0.3f, 0.9f, 0.1f, 0.6f };
            const float w = r.getWidth() / 4.0f;
            p.startNewSubPath (r.getX(), r.getBottom() - levels[0] * r.getHeight());
            for (int i = 0; i < 4; ++i)
            {
                const float y = r.getBottom() - levels[i] * r.getHeight();
                p.lineTo (r.getX() + w * (float) i, y);
                p.lineTo (r.getX() + w * (float) (i + 1), y);
            }
        });

        const Painter flatIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            p.startNewSubPath (r.getX(), r.getCentreY());
            p.lineTo (r.getRight(), r.getCentreY());
        });

        const Painter invertIcon = strokeIcon ([] (juce::Path& p, juce::Rectangle<float> r)
        {
            const float a = r.getWidth() * 0.18f;
            const float x1 = r.getX() + r.getWidth() * 0.3f, x2 = r.getX() + r.getWidth() * 0.7f;
            p.startNewSubPath (x1, r.getBottom());
            p.lineTo (x1, r.getY());
            p.lineTo (x1 - a, r.getY() + a);
            p.startNewSubPath (x1, r.getY());
            p.lineTo (x1 + a, r.getY() + a);
            p.startNewSubPath (x2, r.getY());
            p.lineTo (x2, r.getBottom());
            p.lineTo (x2 - a, r.getBottom() - a);
            p.startNewSubPath (x2, r.getBottom());
            p.lineTo (x2 + a, r.getBottom() - a);
        });
    }

    MainPanel::MainPanel (PulseLockProcessor& p)
        : processor (p),
          state (p.apvts),
          patterns (*state.getParameter (ParamID::pattern)),
          lanes (state),
          editor (p),
          gridChoice ({ "4", "8", "16", "32" }, [this] (int index) { setGrid (index); }),
          snapButton ("SNAP", "Points snap to the grid (hold Shift while dragging to place freely)"),
          gateButton ("Gate", "Stamp a gate: one step per grid division", gateIcon),
          pumpButton ("Pump", "Stamp a sidechain pump on every quarter of the pattern", pumpIcon),
          rampUpButton ("Ramp up", "Stamp a ramp up", rampUpIcon),
          rampDownButton ("Ramp down", "Stamp a ramp down", rampDownIcon),
          triangleButton ("Triangle", "Stamp a triangle", triangleIcon),
          randomButton ("Random", "Stamp random steps, one per grid division", randomIcon),
          flatButton ("Flat", "Clear the lane to a flat line", flatIcon),
          invertButton ("Invert", "Flip the lane upside down", invertIcon),
          copyButton ("COPY", "Copy this pattern (all three lanes)"),
          pasteButton ("PASTE", "Paste over this pattern (all three lanes)"),
          undoButton ("UNDO", "Undo the last shape edit"),
          redoButton ("REDO", "Redo"),
          rate        (state, ParamID::rate, "Rate"),
          keys        (state, ParamID::keyStart, "Keys"),
          volumeDepth (state, ParamID::volumeDepth, "Volume"),
          filterLow   (state, ParamID::filterLow, "Low"),
          filterHigh  (state, ParamID::filterHigh, "High"),
          filterRes   (state, ParamID::filterRes, "Res"),
          panDepth    (state, ParamID::panDepth, "Pan"),
          smooth      (state, ParamID::smooth, "Smooth"),
          mix         (state, ParamID::mix, "Mix"),
          gain        (state, ParamID::outGain, "Gain", true)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &previousPreset, &nextPreset, &presetBox, &patterns, &lanes, &editor,
                 &gridChoice, &snapButton, &gateButton, &pumpButton, &rampUpButton, &rampDownButton, &triangleButton,
                 &randomButton, &flatButton, &invertButton, &copyButton, &pasteButton, &undoButton, &redoButton,
                 &length, &trigger, &switchMode, &filterType,
                 &rate, &keys, &volumeDepth, &filterLow, &filterHigh, &filterRes, &panDepth, &smooth, &mix, &gain,
                 &keyStrip })
            addAndMakeVisible (c);

        // Choice parameters as combo boxes, filled from the parameters' own labels.
        const std::pair<juce::ComboBox*, const char*> combos[] = {
            { &length, ParamID::sync }, { &trigger, ParamID::trigger },
            { &switchMode, ParamID::switchMode }, { &filterType, ParamID::filterType } };
        for (const auto& [box, id] : combos)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
                box->addItemList (choice->choices, 1);
            comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, *box));
        }

        // Editor settings are part of the saved session, like the window size.
        lanes.onSelect = [this] (int lane) { selectLane (lane); };
        selectLane (juce::jlimit (0, numLanes - 1, (int) state.state.getProperty ("editLane", 0)));

        const int gridIndex = juce::jlimit (0, 3, (int) state.state.getProperty ("gridIndex", 2));
        gridChoice.setSelected (gridIndex);
        snapButton.setClickingTogglesState (true);
        snapButton.setToggleState ((bool) state.state.getProperty ("snap", true), juce::dontSendNotification);
        snapButton.onClick = [this] { state.state.setProperty ("snap", snapButton.getToggleState(), nullptr); setGrid (-1); };
        setGrid (gridIndex);

        auto shapeOfGrid = [this] { return gridDivisions[juce::jlimit (0, 3, (int) state.state.getProperty ("gridIndex", 2))]; };
        gateButton.onClick      = [this, shapeOfGrid] { stamp ("gate", Shapes::gate (shapeOfGrid(), 0.5f)); };
        pumpButton.onClick      = [this, shapeOfGrid] { stamp ("pump", Shapes::pump (juce::jmax (1, shapeOfGrid() / 4), 0.08f, 0.7f)); };
        rampUpButton.onClick    = [this] { stamp ("ramp up", Shapes::ramp (0.0f, 1.0f)); };
        rampDownButton.onClick  = [this] { stamp ("ramp down", Shapes::ramp (1.0f, 0.0f)); };
        triangleButton.onClick  = [this, shapeOfGrid] { stamp ("triangle", Shapes::triangle (juce::jmax (1, shapeOfGrid() / 8), 0.0f, 1.0f)); };
        randomButton.onClick    = [this, shapeOfGrid] { stamp ("random", Shapes::randomSteps (shapeOfGrid(), random)); };
        flatButton.onClick      = [this] { stamp ("flat", Shapes::flat (editLane == (int) Lane::pan ? 0.5f : 1.0f)); };
        invertButton.onClick    = [this] { stamp ("invert", Shapes::inverted (processor.getLanePoints (editedPattern(), (Lane) editLane))); };
        copyButton.onClick      = [this] { copyPattern(); };
        pasteButton.onClick     = [this] { pastePattern(); };
        undoButton.onClick      = [this] { processor.undoPatterns(); };
        redoButton.onClick      = [this] { processor.redoPatterns(); };
        patterns.onPick         = [this] (int index) { processor.requestPatternFromUi (index); };

        presetBox.getNames = [this]
        {
            juce::StringArray names;
            for (int i = 0; i < processor.getNumPresets(); ++i)
                names.add (processor.getPresetName (i));
            return names;
        };
        presetBox.getCurrentIndex = [this] { return processor.getPresetIndex (processor.getCurrentPresetName()); };
        presetBox.onPick = [this] (int index) { processor.loadPreset (index); };
        previousPreset.onClick = [this] { stepPreset (-1); };
        nextPreset.onClick = [this] { stepPreset (1); };
        refreshPresetBox();

        setSize (Theme::baseWidth, Theme::baseHeight);
    }

    //==============================================================================
    void MainPanel::layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> cells)
    {
        const int n = (int) cells.size();
        const float w = (float) area.getWidth() / (float) n;
        int i = 0;
        for (auto* c : cells)
        {
            const int x0 = area.getX() + juce::roundToInt (w * (float) i);
            const int x1 = area.getX() + juce::roundToInt (w * (float) (i + 1));
            c->setBounds (juce::Rectangle<int> (x0, area.getY(), x1 - x0, area.getHeight()).reduced (1, 0));
            ++i;
        }
    }

    void MainPanel::resized()
    {
        // Top bar
        previousPreset.setBounds (262, 10, 24, 24);
        presetBox.setBounds (290, 10, 200, 24);
        nextPreset.setBounds (494, 10, 24, 24);
        statusArea = { 540, 10, 228, 24 };

        // Pattern bar and editor
        patterns.setBounds (12, 50, 420, 28);
        lanes.setBounds (440, 50, 328, 28);
        editor.setBounds (12, 84, 756, 196);

        // Tool strip
        gridChoice.setBounds (12, 286, 132, 22);
        snapButton.setBounds (150, 286, 50, 22);
        int x = 214;
        for (auto* b : { &gateButton, &pumpButton, &rampUpButton, &rampDownButton, &triangleButton,
                         &randomButton, &flatButton, &invertButton })
        {
            b->setBounds (x, 286, 30, 22);
            x += 34;
        }
        copyButton.setBounds (500, 286, 54, 22);
        pasteButton.setBounds (558, 286, 54, 22);
        undoButton.setBounds (662, 286, 51, 22);
        redoButton.setBounds (717, 286, 51, 22);

        // Knob row
        sections = { Section { "TIMING", { 12, 314, 250, 94 } },
                     Section { "DEPTH",  { 268, 314, 124, 94 } },
                     Section { "FILTER", { 398, 314, 224, 94 } },
                     Section { "OUTPUT", { 628, 314, 140, 94 } } };

        auto body = [] (const Section& s) { return s.bounds.reduced (6, 4).withTrimmedTop (18); };

        auto timing = body (sections[0]);
        auto combosColumn = timing.removeFromLeft (148);
        for (auto [caption, box] : { std::pair<juce::Rectangle<int>*, juce::ComboBox*> { &lengthCaption, &length },
                                     { &triggerCaption, &trigger }, { &switchCaption, &switchMode } })
        {
            auto row = combosColumn.removeFromTop (20);
            combosColumn.removeFromTop (3);
            *caption = row.removeFromLeft (52);
            box->setBounds (row);
        }
        timing.removeFromLeft (4);
        layoutRow (timing, { &rate, &keys });

        layoutRow (body (sections[1]), { &volumeDepth, &panDepth });

        auto filter = body (sections[2]);
        auto typeColumn = filter.removeFromLeft (70);
        typeCaption = typeColumn.removeFromTop (14);
        filterType.setBounds (typeColumn.removeFromTop (22));
        filter.removeFromLeft (4);
        layoutRow (filter, { &filterLow, &filterHigh, &filterRes });

        layoutRow (body (sections[3]), { &smooth, &mix, &gain });

        keyStrip.setBounds (12, 414, 738, 20);   // stops short of the window's resize corner
    }

    void MainPanel::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);

        paintLogo (g, { 16.0f, 11.0f, 22.0f, 22.0f });
        g.setColour (Theme::text);
        g.setFont (Theme::font (17.0f, true, 0.2f));
        g.drawText ("PULSELOCK", juce::Rectangle<int> (46, 10, 200, 24), juce::Justification::centredLeft, false);

        // What is playing, and what is waiting for its beat or bar.
        g.setColour (Theme::accent);
        g.setFont (Theme::font (11.5f, true, 0.1f));
        g.drawText (statusText, statusArea, juce::Justification::centredRight, false);

        for (const auto& s : sections)
        {
            const auto r = s.bounds.toFloat();
            g.setColour (Theme::panel);
            g.fillRoundedRectangle (r, Theme::corner);

            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.getX() + 10.0f, r.getY() + 9.0f, 3.0f, 10.0f, 1.5f);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (11.5f, true, 0.2f));
            g.drawText (s.title, juce::Rectangle<float> (r.getX() + 18.0f, r.getY() + 6.0f, r.getWidth() - 24.0f, 16.0f),
                        juce::Justification::centredLeft, false);
        }

        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (10.5f, true, 0.1f));
        g.drawText ("LENGTH", lengthCaption, juce::Justification::centredLeft, false);
        g.drawText ("TRIGGER", triggerCaption, juce::Justification::centredLeft, false);
        g.drawText ("SWITCH", switchCaption, juce::Justification::centredLeft, false);
        g.drawText ("TYPE", typeCaption, juce::Justification::centredLeft, false);
    }

    //==============================================================================
    float MainPanel::plainValue (const char* id) const
    {
        return state.getRawParameterValue (id)->load();
    }

    int MainPanel::editedPattern() const
    {
        return juce::jlimit (0, numPatterns - 1, juce::roundToInt (plainValue (ParamID::pattern)));
    }

    void MainPanel::selectLane (int lane)
    {
        editLane = juce::jlimit (0, numLanes - 1, lane);
        state.state.setProperty ("editLane", editLane, nullptr);
        lanes.setSelected (editLane);
        editor.setTarget (editedPattern(), (Lane) editLane);
    }

    void MainPanel::setGrid (int index)
    {
        if (index >= 0)
            state.state.setProperty ("gridIndex", index, nullptr);
        const int stored = juce::jlimit (0, 3, (int) state.state.getProperty ("gridIndex", 2));
        editor.setGrid (gridDivisions[stored], snapButton.getToggleState());
    }

    void MainPanel::stamp (const juce::String& name, std::vector<CurvePoint> points)
    {
        processor.setLanePoints (editedPattern(), (Lane) editLane, points, "Stamp " + name);
        editor.refresh();
    }

    void MainPanel::copyPattern()
    {
        for (int l = 0; l < numLanes; ++l)
            clipboard[(size_t) l] = processor.getLanePoints (editedPattern(), (Lane) l);
        hasClipboard = true;
    }

    void MainPanel::pastePattern()
    {
        if (hasClipboard)
            processor.setPatternPoints (editedPattern(), clipboard, "Paste pattern");
    }

    void MainPanel::tick (const ScopeFrame* frame)
    {
        if (frame != nullptr)
            lastFrame = *frame;

        const int edited = editedPattern();
        const int keyStart = juce::roundToInt (plainValue (ParamID::keyStart));

        // The host can restore a whole session while the window is open: follow its editor settings.
        const int storedLane = juce::jlimit (0, numLanes - 1, (int) state.state.getProperty ("editLane", 0));
        if (storedLane != editLane)
            selectLane (storedLane);
        const int storedGrid = juce::jlimit (0, 3, (int) state.state.getProperty ("gridIndex", 2));
        const bool storedSnap = (bool) state.state.getProperty ("snap", true);
        gridChoice.setSelected (storedGrid);
        if (storedSnap != snapButton.getToggleState())
            snapButton.setToggleState (storedSnap, juce::dontSendNotification);
        editor.setGrid (gridDivisions[storedGrid], storedSnap);

        patterns.setPlaying (lastFrame.pattern, lastFrame.pendingPattern);
        patterns.setKeyStart (keyStart);
        editor.setTarget (edited, (Lane) editLane);
        editor.setPlayhead (lastFrame.phase, lastFrame.moving && lastFrame.pattern == edited);

        const int revision = processor.getPatternRevision();
        if (revision != shownRevision)
        {
            shownRevision = revision;
            editor.refresh();
        }

        keyStrip.update (lastFrame.heldNotes, keyStart);

        juce::String status = patternName (lastFrame.pattern) + " PLAYING";
        if (lastFrame.pendingPattern >= 0)
        {
            const int mode = juce::roundToInt (plainValue (ParamID::switchMode));
            status << "   " << juce::String::fromUTF8 ("\xe2\x86\x92") << "   " << patternName (lastFrame.pendingPattern)
                   << (mode == (int) SwitchMode::nextBar ? " AT NEXT BAR" : " ON NEXT BEAT");
        }
        if (status != statusText)
        {
            statusText = status;
            repaint (statusArea);
        }

        // Controls that do nothing right now fade back (they still work).
        auto dim = [] (juce::Component& c, bool active) { c.setAlpha (active ? 1.0f : 0.45f); };
        dim (rate, juce::roundToInt (plainValue (ParamID::sync)) == 0);
        dim (volumeDepth, plainValue (ParamID::volumeOn) >= 0.5f);
        const bool filterActive = plainValue (ParamID::filterOn) >= 0.5f;
        for (auto* c : std::initializer_list<juce::Component*> { &filterType, &filterLow, &filterHigh, &filterRes })
            dim (*c, filterActive);
        dim (panDepth, plainValue (ParamID::panOn) >= 0.5f);

        undoButton.setEnabled (processor.getPatternUndo().canUndo());
        redoButton.setEnabled (processor.getPatternUndo().canRedo());
        pasteButton.setEnabled (hasClipboard);

        if (processor.getCurrentPresetName() != shownPresetName)
            refreshPresetBox();
    }

    //==============================================================================
    void MainPanel::refreshPresetBox()
    {
        shownPresetName = processor.getCurrentPresetName();
        presetBox.setDisplayedName (shownPresetName);
    }

    void MainPanel::stepPreset (int delta)
    {
        const int count = processor.getNumPresets();
        if (count <= 0)
            return;

        const int current = juce::jmax (0, processor.getPresetIndex (processor.getCurrentPresetName()));
        processor.loadPreset ((current + delta + count) % count);
    }
}
