#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/PulseEngine.h"
#include "pattern/Pattern.h"

class PulseLockProcessor final : public juce::AudioProcessor, private juce::ValueTree::Listener
{
public:
    PulseLockProcessor();
    ~PulseLockProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    /** Cubase's bypass button lands here: audio passes untouched, the clock keeps running. */
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PulseLock"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    pulselock::ScopeFifo& getScopeFifo() noexcept { return engine.getScopeFifo(); }

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (pulselock::ParamID::bypass); }

    /** A click on a pattern button: switches even when it is the pattern already selected
        (after a key had switched away from it). */
    void requestPatternFromUi (int index) noexcept { uiPatternRequest.store (index); }

    /** Undo / redo of shape edits, published to the audio thread once when complete. */
    void undoPatterns();
    void redoPatterns();

    /** One finished editor gesture: before is what the lane held when it started (the editor has been
        pushing live previews since), after is the result. One undo step, one publish. */
    void commitLaneEdit (int pattern, pulselock::Lane lane, const std::vector<pulselock::CurvePoint>& before,
                         const std::vector<pulselock::CurvePoint>& after, const juce::String& undoName);

    // Pattern editing (message thread). Every edit is undoable through getPatternUndo().
    std::vector<pulselock::CurvePoint> getLanePoints (int pattern, pulselock::Lane lane) const;
    void setLanePoints (int pattern, pulselock::Lane lane, const std::vector<pulselock::CurvePoint>& points,
                        const juce::String& undoName = "Edit shape");
    /** All three lanes of one pattern as a single undo step (paste). */
    void setPatternPoints (int pattern, const std::array<std::vector<pulselock::CurvePoint>, pulselock::numLanes>& lanes,
                           const juce::String& undoName);
    void resetPatternsToFactory();
    juce::UndoManager& getPatternUndo() noexcept { return patternUndo; }

    /** Goes up every time the patterns change (edit, undo, preset, session load); the editor polls it. */
    int getPatternRevision() const noexcept { return patternRevision.load(); }

    // Factory presets (message thread). Presets set parameters only; your drawn patterns stay.
    int getNumPresets() const;
    juce::String getPresetName (int index) const;
    int getPresetIndex (const juce::String& name) const;
    void loadPreset (int index);
    juce::String getCurrentPresetName() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    pulselock::EngineParams readParameters() const noexcept;
    pulselock::HostTiming readHostTiming() const noexcept;

    juce::ValueTree patternsTree() const;
    void ensurePatterns();
    void publishPatterns();

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    static bool isPatternData (const juce::ValueTree& tree);

    pulselock::ParameterRefs params;
    pulselock::PulseEngine engine;

    pulselock::TripleBuffer<pulselock::PatternSet> patternBuffer;
    juce::CriticalSection patternWriteLock;   // writers only (UI edits, state restore); never the audio thread
    juce::UndoManager patternUndo;
    std::atomic<int> patternRevision { 0 };
    std::atomic<int> uiPatternRequest { -1 };
    bool batchingEdits = false;   // a whole-lane write publishes once at the end, not once per point

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulseLockProcessor)
};
