#include "Presets.h"
#include "Parameters.h"

namespace pulselock
{
    const std::vector<Preset>& factoryPresets()
    {
        using namespace ParamID;

        // Pattern and sync values are choice indexes: pattern 1 = 0 .. 8 = 7; sync "1/2" = 8,
        // "1 bar" = 9, "4 bars" = 11 (see syncChoices()).
        constexpr float halfBar = 8.0f, oneBar = 9.0f, fourBars = 11.0f;
        constexpr float lowPass = (float) (int) FilterType::lowPass;
        constexpr float bandPass = (float) (int) FilterType::bandPass;

        static const std::vector<Preset> presets {
            { "Init", {} },

            // Pattern 1's 16th gate at full depth: the classic trance chop.
            { "Trance Gate", {
                { pattern, 0.0f }, { sync, oneBar }, { smooth, 1.2f } } },

            // Pattern 2 dips on every beat and swells back, like a kick ducking the track.
            { "Sidechain Pump", {
                { pattern, 1.0f }, { sync, oneBar }, { volumeDepth, 85.0f }, { smooth, 2.5f } } },

            // Pattern 5's rising filter over four bars, volume left alone.
            { "Filter Riser", {
                { pattern, 4.0f }, { sync, fourBars }, { volumeOn, 0.0f },
                { filterOn, 1.0f }, { filterType, lowPass }, { filterLow, 180.0f }, { filterHigh, 12000.0f }, { filterRes, 35.0f } } },

            // Pattern 1's pan wave every half bar; nothing else moves.
            { "Auto-Pan", {
                { pattern, 0.0f }, { sync, halfBar }, { volumeOn, 0.0f }, { panOn, 1.0f }, { panDepth, 100.0f } } },

            // Every key restarts pattern 7 (straight, then a 32nd stutter). Play it like a sampler.
            { "Stutter Keys", {
                { pattern, 6.0f }, { sync, halfBar }, { trigger, (float) (int) Trigger::retrigger }, { smooth, 0.8f } } },

            // Pattern 3's 3-3-2 rhythm through a moving low-pass, with a little pan.
            { "3-3-2 Groove", {
                { pattern, 2.0f }, { sync, oneBar }, { filterOn, 1.0f }, { filterType, lowPass },
                { filterLow, 300.0f }, { filterHigh, 9000.0f }, { filterRes, 25.0f }, { panOn, 1.0f }, { panDepth, 60.0f } } },

            // Pattern 6's random steps on all three lanes, band-passed for a telephone shimmer.
            { "Random Steps", {
                { pattern, 5.0f }, { sync, oneBar }, { volumeDepth, 70.0f }, { filterOn, 1.0f }, { filterType, bandPass },
                { filterLow, 400.0f }, { filterHigh, 6000.0f }, { filterRes, 40.0f }, { panOn, 1.0f }, { panDepth, 50.0f } } },
        };
        return presets;
    }

    void applyPreset (juce::AudioProcessorValueTreeState& state, const Preset& preset)
    {
        auto setNormalised = [] (juce::RangedAudioParameter& p, float normalised)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost (normalised);
            p.endChangeGesture();
        };

        for (const char* id : ParamID::all)
            if (auto* p = state.getParameter (id))
                setNormalised (*p, p->getDefaultValue());

        for (const auto& v : preset.values)
            if (auto* p = state.getParameter (v.id))
                setNormalised (*p, p->convertTo0to1 (v.value));

        state.state.setProperty ("presetName", juce::String (preset.name), nullptr);
    }
}
