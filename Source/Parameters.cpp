#include "Parameters.h"

namespace pulselock
{
    namespace
    {
        using Range = juce::NormalisableRange<float>;

        juce::String signedString (float v, int decimals)
        {
            const auto text = juce::String (v, decimals);
            return v > 0.0f ? "+" + text : text;
        }

        juce::String formatMs (float ms, int)       { return juce::String (ms, ms < 10.0f ? 1 : 0) + " ms"; }
        juce::String formatPercent (float v, int)   { return juce::String (juce::roundToInt (v)) + "%"; }
        juce::String formatDb (float v, int)        { return signedString (v, 1) + " dB"; }
        juce::String formatHz (float v, int)        { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; }

        juce::String formatCutoff (float hz, int)
        {
            return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz"
                                 : juce::String (juce::roundToInt (hz)) + " Hz";
        }

        float parseNumber (const juce::String& text) { return text.trim().getFloatValue(); }

        float parseCutoff (const juce::String& text)
        {
            const auto t = text.trim().toLowerCase();
            const float v = t.getFloatValue();
            return t.endsWith ("khz") || t.endsWith ("k") ? v * 1000.0f : v;
        }

        Range skewedRange (float lo, float hi, float step, float centre)
        {
            Range r (lo, hi, step);
            r.setSkewForCentre (centre);
            return r;
        }

        auto floatParam (const char* id, const char* name, Range range, float def,
                         juce::AudioParameterFloatAttributes::StringFromValue toText,
                         juce::AudioParameterFloatAttributes::ValueFromString fromText = parseNumber)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, def,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction (std::move (toText))
                    .withValueFromStringFunction (std::move (fromText)));
        }

        auto boolParam (const char* id, const char* name, bool def)
        {
            return std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id, 1 }, name, def,
                juce::AudioParameterBoolAttributes().withStringFromValueFunction (
                    [] (bool v, int) { return juce::String (v ? "On" : "Off"); }));
        }

        auto choiceParam (const char* id, const char* name, const juce::StringArray& choices, int def)
        {
            return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
        }
    }

    const juce::StringArray& syncChoices()
    {
        static const juce::StringArray choices { "Free", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4",
                                                 "1/2", "1 bar", "2 bars", "4 bars" };
        return choices;
    }

    double syncBeats (int syncIndex) noexcept
    {
        static constexpr double beats[] = { 0.0, 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0,
                                            2.0, 4.0, 8.0, 16.0 };
        constexpr int count = (int) (sizeof (beats) / sizeof (beats[0]));
        return beats[juce::jlimit (0, count - 1, syncIndex)];
    }

    juce::String patternName (int index)
    {
        return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, numPatterns - 1, index)));
    }

    juce::String formatNoteName (int midiNote)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        midiNote = juce::jlimit (0, 127, midiNote);
        return juce::String (names[midiNote % 12]) + juce::String (midiNote / 12 - 2);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // TIMING
        juce::StringArray patternNames;
        for (int i = 0; i < numPatterns; ++i)
            patternNames.add (patternName (i));
        layout.add (choiceParam (ParamID::pattern, "Pattern", patternNames, 0));
        layout.add (choiceParam (ParamID::sync, "Length", syncChoices(), 9));   // 1 bar
        layout.add (floatParam (ParamID::rate, "Rate", skewedRange (0.05f, 20.0f, 0.001f, 2.0f), 1.0f, formatHz));
        layout.add (choiceParam (ParamID::trigger, "Trigger", { "Song", "Retrigger", "One-shot" }, (int) Trigger::song));
        layout.add (choiceParam (ParamID::switchMode, "Switch", { "Instant", "Next beat", "Next bar" }, (int) SwitchMode::instant));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { ParamID::keyStart, 1 }, "Pattern Keys", 0, 120, 36,
            juce::AudioParameterIntAttributes().withStringFromValueFunction (
                [] (int v, int) { return formatNoteName (v) + "-" + formatNoteName (v + numPatterns - 1); })));

        // VOLUME
        layout.add (boolParam (ParamID::volumeOn, "Volume On", true));
        layout.add (floatParam (ParamID::volumeDepth, "Volume Depth", Range (0.0f, 100.0f, 0.1f), 100.0f, formatPercent));

        // FILTER
        layout.add (boolParam (ParamID::filterOn, "Filter On", false));
        layout.add (choiceParam (ParamID::filterType, "Filter Type", { "Low-pass", "High-pass", "Band-pass" }, (int) FilterType::lowPass));
        layout.add (floatParam (ParamID::filterLow, "Filter Low", skewedRange (20.0f, 20000.0f, 1.0f, 1000.0f), 250.0f, formatCutoff, parseCutoff));
        layout.add (floatParam (ParamID::filterHigh, "Filter High", skewedRange (20.0f, 20000.0f, 1.0f, 1000.0f), 8000.0f, formatCutoff, parseCutoff));
        layout.add (floatParam (ParamID::filterRes, "Resonance", Range (0.0f, 100.0f, 0.1f), 20.0f, formatPercent));

        // PAN
        layout.add (boolParam (ParamID::panOn, "Pan On", false));
        layout.add (floatParam (ParamID::panDepth, "Pan Depth", Range (0.0f, 100.0f, 0.1f), 100.0f, formatPercent));

        // OUTPUT
        layout.add (floatParam (ParamID::smooth, "Smooth", skewedRange (0.0f, 20.0f, 0.01f, 3.0f), 1.5f, formatMs));
        layout.add (floatParam (ParamID::mix, "Mix", Range (0.0f, 100.0f, 0.1f), 100.0f, formatPercent));
        layout.add (floatParam (ParamID::outGain, "Output Gain", Range (-24.0f, 24.0f, 0.1f), 0.0f, formatDb));

        // Handed to the host as its bypass switch, so bypassing crossfades instead of cutting.
        layout.add (boolParam (ParamID::bypass, "Bypass", false));

        return layout;
    }

    ParameterRefs::ParameterRefs (juce::AudioProcessorValueTreeState& state)
    {
        auto get = [&state] (const char* id)
        {
            auto* p = state.getRawParameterValue (id);
            jassert (p != nullptr);
            return p;
        };

        pattern     = get (ParamID::pattern);
        sync        = get (ParamID::sync);
        rate        = get (ParamID::rate);
        trigger     = get (ParamID::trigger);
        switchMode  = get (ParamID::switchMode);
        keyStart    = get (ParamID::keyStart);
        volumeOn    = get (ParamID::volumeOn);
        volumeDepth = get (ParamID::volumeDepth);
        filterOn    = get (ParamID::filterOn);
        filterType  = get (ParamID::filterType);
        filterLow   = get (ParamID::filterLow);
        filterHigh  = get (ParamID::filterHigh);
        filterRes   = get (ParamID::filterRes);
        panOn       = get (ParamID::panOn);
        panDepth    = get (ParamID::panDepth);
        smooth      = get (ParamID::smooth);
        mix         = get (ParamID::mix);
        outGain     = get (ParamID::outGain);
        bypass      = get (ParamID::bypass);
    }
}
