#pragma once

#include "ScopeFrame.h"
#include "StateVariableFilter.h"
#include "../pattern/Pattern.h"

namespace pulselock
{
    /** Every parameter's value for one block, read once from the atomics. */
    struct EngineParams
    {
        int pattern = 0;
        int sync = 9;
        float rateHz = 1.0f;
        Trigger trigger = Trigger::song;
        SwitchMode switchMode = SwitchMode::instant;
        int keyStart = 36;

        bool volumeOn = true;
        float volumeDepth = 100.0f;

        bool filterOn = false;
        FilterType filterType = FilterType::lowPass;
        float filterLow = 250.0f;
        float filterHigh = 8000.0f;
        float filterRes = 20.0f;

        bool panOn = false;
        float panDepth = 100.0f;

        float smoothMs = 1.5f;
        float mix = 100.0f;
        float outGainDb = 0.0f;
        bool bypass = false;

        /** A pattern the user clicked this block (even the one already selected), or -1. */
        int uiPatternRequest = -1;
    };

    /** What the host said about tempo and position for this block. */
    struct HostTiming
    {
        double bpm = 120.0;
        double beatsPerBar = 4.0;   // in quarter notes
        bool hasPpq = false;
        double ppq = 0.0;
        bool hasBarStart = false;
        double barStart = 0.0;      // ppq of the bar line at or before the block start
        bool playing = false;
    };

    /** Plays the drawn lanes against the song position (or MIDI triggers) and applies them to
        volume, a state-variable filter and a stereo balance. */
    class PulseEngine
    {
    public:
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        /** Processes in place. Channel 0 (and 1 when numInputChannels > 1) hold the input. */
        void process (float* const* channels, int numChannels, int numInputChannels, int numSamples,
                      const juce::MidiBuffer& midi, const EngineParams& params, const HostTiming& timing,
                      const PatternSet& patterns) noexcept;

        /** The host bypassed the plugin: keep the clock and the display moving, leave audio alone. */
        void processBypassed (int numSamples, const HostTiming& timing) noexcept;

        ScopeFifo& getScopeFifo() noexcept { return scopeFifo; }

    private:
        void applyBlockParams (const EngineParams& params, const HostTiming& timing) noexcept;
        void handleMidiEvent (const juce::uint8* data, int numBytes, double beatPosition) noexcept;
        void requestPattern (int index, double beatPosition, bool immediate) noexcept;
        double switchLineFor (double position) const noexcept;
        void reschedulePending() noexcept;
        void retrigger() noexcept;
        void publishScope() noexcept;

        double sampleRate = 48000.0;
        bool prepared = false;

        EngineParams block;
        double beatsPerSample = 0.0;
        double beatsPerBar = 4.0;
        double lengthBeats = 4.0;       // 0 when free-running in Hz
        double barAnchor = 0.0;         // a known bar line (ppq); bar lines are barAnchor + k * beatsPerBar
        double patternOrigin = 0.0;     // where Song-mode passes of the pattern start (ppq)
        double phaseIncrement = 0.0;    // pattern passes per sample
        bool firstBlock = true;
        bool wasPlaying = false;

        // Clock
        double beatPos = 0.0;           // quarter notes; follows the song position while the host plays
        double phase = 0.0;             // 0..1 through the pattern
        bool oneShotDone = true;        // one-shot mode rests at the end of the pattern until a note

        // Pattern switching
        int activePattern = 0;
        int pendingPattern = -1;
        double pendingAtBeat = 0.0;
        int lastParamPattern = -1;

        // Per-lane smoothing and the ramps that fade lanes in and out.
        std::array<float, numLanes> laneValue {};
        float laneCoeff = 1.0f;
        juce::SmoothedValue<float> volumeAmount, panAmount, filterMix, mix, logLow, logHigh, resonance, bypassFade;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain { 1.0f };

        StateVariableFilter filter;
        ScopeFifo scopeFifo;
        int scopeInterval = 1600;
        int scopeCountdown = 0;
        std::array<juce::uint64, 2> heldNotes {};
    };
}
