#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs are part of saved Cubase projects. Never rename or remove one once shipped.
namespace pulselock::ParamID
{
    inline constexpr const char* pattern     = "pattern";
    inline constexpr const char* sync        = "sync";
    inline constexpr const char* rate        = "rate";
    inline constexpr const char* trigger     = "trigger";
    inline constexpr const char* switchMode  = "switchMode";
    inline constexpr const char* keyStart    = "keyStart";

    inline constexpr const char* volumeOn    = "volumeOn";
    inline constexpr const char* volumeDepth = "volumeDepth";

    inline constexpr const char* filterOn    = "filterOn";
    inline constexpr const char* filterType  = "filterType";
    inline constexpr const char* filterLow   = "filterLow";
    inline constexpr const char* filterHigh  = "filterHigh";
    inline constexpr const char* filterRes   = "filterRes";

    inline constexpr const char* panOn       = "panOn";
    inline constexpr const char* panDepth    = "panDepth";

    inline constexpr const char* smooth      = "smooth";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* outGain     = "outGain";

    /** The host's bypass switch. Kept out of `all`: presets and resets must never touch it. */
    inline constexpr const char* bypass      = "bypass";

    inline constexpr const char* all[] = {
        pattern, sync, rate, trigger, switchMode, keyStart,
        volumeOn, volumeDepth,
        filterOn, filterType, filterLow, filterHigh, filterRes,
        panOn, panDepth,
        smooth, mix, outGain
    };
}

namespace pulselock
{
    inline constexpr int numPatterns = 8;
    inline constexpr int numLanes = 3;

    enum class Lane       { volume = 0, filter, pan };
    enum class Trigger    { song = 0, retrigger, oneShot };
    enum class SwitchMode { instant = 0, nextBeat, nextBar };
    enum class FilterType { lowPass = 0, highPass, bandPass };

    /** Choice labels for the sync parameter. Index 0 is free-running (Rate, in Hz). */
    const juce::StringArray& syncChoices();

    /** Length of one pattern pass in quarter-note beats for a sync index, or 0 for free-running. */
    double syncBeats (int syncIndex) noexcept;

    /** "1" .. "8" (numbers, so they never read as note names). */
    juce::String patternName (int index);

    /** Note name as Cubase shows it by default (middle C, MIDI 60, is C3). */
    juce::String formatNoteName (int midiNote);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Cached pointers to every parameter's live value, read on the audio thread without locks. */
    struct ParameterRefs
    {
        explicit ParameterRefs (juce::AudioProcessorValueTreeState& state);

        std::atomic<float>* pattern;
        std::atomic<float>* sync;
        std::atomic<float>* rate;
        std::atomic<float>* trigger;
        std::atomic<float>* switchMode;
        std::atomic<float>* keyStart;
        std::atomic<float>* volumeOn;
        std::atomic<float>* volumeDepth;
        std::atomic<float>* filterOn;
        std::atomic<float>* filterType;
        std::atomic<float>* filterLow;
        std::atomic<float>* filterHigh;
        std::atomic<float>* filterRes;
        std::atomic<float>* panOn;
        std::atomic<float>* panDepth;
        std::atomic<float>* smooth;
        std::atomic<float>* mix;
        std::atomic<float>* outGain;
        std::atomic<float>* bypass;
    };
}
