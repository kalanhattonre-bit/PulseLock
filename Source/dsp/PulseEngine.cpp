#include "PulseEngine.h"

#include <cmath>

namespace pulselock
{
    namespace
    {
        constexpr double halfPi = juce::MathConstants<double>::halfPi;

        /** Resonance 0..100% to filter Q: 0.707 (no peak) up to 12 (a sharp, singing peak). */
        float resonanceToQ (float percent) noexcept
        {
            return 0.7071f * std::pow (12.0f / 0.7071f, juce::jlimit (0.0f, 100.0f, percent) / 100.0f);
        }

        StateVariableFilter::Mode filterModeFor (FilterType type) noexcept
        {
            switch (type)
            {
                case FilterType::highPass: return StateVariableFilter::Mode::highPass;
                case FilterType::bandPass: return StateVariableFilter::Mode::bandPass;
                case FilterType::lowPass:  break;
            }
            return StateVariableFilter::Mode::lowPass;
        }
    }

    void PulseEngine::prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate;

        for (auto* s : { &volumeAmount, &panAmount, &filterMix, &mix, &logLow, &logHigh, &resonance, &bypassFade })
            s->reset (sampleRate, 0.01);
        outGain.reset (sampleRate, 0.02);

        scopeInterval = juce::jmax (1, (int) (sampleRate / 30.0));
        prepared = true;
        firstBlock = true;
        reset();
    }

    void PulseEngine::reset()
    {
        beatPos = 0.0;
        phase = 0.0;
        oneShotDone = true;
        pendingPattern = -1;
        wasPlaying = false;
        barAnchor = 0.0;
        filter.reset();
        heldNotes = {};
        scopeCountdown = 0;
        firstBlock = true;
    }

    void PulseEngine::applyBlockParams (const EngineParams& params, const HostTiming& timing) noexcept
    {
        block = params;

        const double bpm = timing.bpm > 0.0 ? timing.bpm : 120.0;
        beatsPerSample = bpm / 60.0 / sampleRate;
        beatsPerBar = timing.beatsPerBar > 0.0 ? timing.beatsPerBar : 4.0;
        if (timing.hasBarStart && std::isfinite (timing.barStart))
            barAnchor = timing.barStart;

        // Bar lengths follow the time signature (1 bar of 3/4 is 3 beats); note lengths are fixed.
        // Only these three entries are bars; anything added to the list after them is a note length.
        constexpr int firstBarSync = 9, numBarSyncs = 3;   // "1 bar", "2 bars", "4 bars"
        const bool isBars = params.sync >= firstBarSync && params.sync < firstBarSync + numBarSyncs;
        const int bars = isBars ? (1 << (params.sync - firstBarSync)) : 0;
        lengthBeats = bars > 0 ? beatsPerBar * bars : syncBeats (params.sync);
        phaseIncrement = lengthBeats > 0.0 ? beatsPerSample / lengthBeats
                                           : (double) juce::jmax (0.0f, params.rateHz) / sampleRate;

        // Song-mode passes start on bar lines: a 2-bar pattern starts on every other bar line.
        if (bars > 0)
        {
            const auto barIndex = (juce::int64) std::llround (barAnchor / beatsPerBar);
            const auto offset = ((barIndex % bars) + bars) % bars;
            patternOrigin = barAnchor - (double) offset * beatsPerBar;
        }
        else
        {
            patternOrigin = 0.0;
        }

        // Follow the host's position while it plays. A jump (locate, cycle, starting play) moves any
        // pattern waiting for its beat or bar onto the grid around the new position.
        const bool following = timing.playing && timing.hasPpq;
        if (following)
        {
            const double tolerance = juce::jmax (4.0 * beatsPerSample, 1.0 / 256.0);
            const bool jumped = ! wasPlaying || std::abs (timing.ppq - beatPos) > tolerance;
            beatPos = timing.ppq;
            if (jumped && pendingPattern >= 0)
                reschedulePending();
        }
        wasPlaying = following;

        if (pendingPattern >= 0)
        {
            const double unit = block.switchMode == SwitchMode::nextBar ? beatsPerBar : 1.0;
            if (pendingAtBeat - beatPos > unit + 1.0e-6)
                reschedulePending();   // never wait longer than one beat or bar
        }

        laneCoeff = params.smoothMs <= 0.0f ? 1.0f
                                            : (float) (1.0 - std::exp (-1.0 / ((double) params.smoothMs * 0.001 * sampleRate)));

        const float volumeTarget = params.volumeOn ? params.volumeDepth / 100.0f : 0.0f;
        const float panTarget = params.panOn ? params.panDepth / 100.0f : 0.0f;
        const float filterTarget = params.filterOn ? 1.0f : 0.0f;
        const float lowTarget = std::log (juce::jlimit (20.0f, 20000.0f, params.filterLow));
        const float highTarget = std::log (juce::jlimit (20.0f, 20000.0f, params.filterHigh));
        const float qTarget = resonanceToQ (params.filterRes);
        const float gainTarget = juce::Decibels::decibelsToGain (params.outGainDb);

        if (firstBlock)
        {
            volumeAmount.setCurrentAndTargetValue (volumeTarget);
            panAmount.setCurrentAndTargetValue (panTarget);
            filterMix.setCurrentAndTargetValue (filterTarget);
            mix.setCurrentAndTargetValue (params.mix / 100.0f);
            logLow.setCurrentAndTargetValue (lowTarget);
            logHigh.setCurrentAndTargetValue (highTarget);
            resonance.setCurrentAndTargetValue (qTarget);
            outGain.setCurrentAndTargetValue (gainTarget);

            bypassFade.setCurrentAndTargetValue (params.bypass ? 1.0f : 0.0f);
            activePattern = juce::jlimit (0, numPatterns - 1, params.pattern);
            lastParamPattern = params.pattern;
            laneValue = { 1.0f, 1.0f, 0.5f };
        }
        else
        {
            volumeAmount.setTargetValue (volumeTarget);
            panAmount.setTargetValue (panTarget);
            filterMix.setTargetValue (filterTarget);
            mix.setTargetValue (params.mix / 100.0f);
            logLow.setTargetValue (lowTarget);
            logHigh.setTargetValue (highTarget);
            resonance.setTargetValue (qTarget);
            outGain.setTargetValue (gainTarget);
            bypassFade.setTargetValue (params.bypass ? 1.0f : 0.0f);

            // The Pattern parameter (automation, or a click) switches like a key does, measured from
            // this block's position. A click on the pattern already selected still counts.
            if (params.pattern != lastParamPattern)
            {
                lastParamPattern = params.pattern;
                requestPattern (params.pattern, beatPos, false);
            }
            else if (params.uiPatternRequest >= 0)
            {
                requestPattern (params.uiPatternRequest, beatPos, false);
            }
        }
    }

    double PulseEngine::switchLineFor (double position) const noexcept
    {
        // The line a request at this position belongs to. Within a few samples of a line (either side)
        // it is that line, so a key quantised onto a bar line switches on it, never a bar late.
        const bool byBar = block.switchMode == SwitchMode::nextBar;
        const double unit = byBar ? beatsPerBar : 1.0;
        const double origin = byBar ? barAnchor : 0.0;
        const double tolerance = juce::jmax (4.0 * beatsPerSample, 1.0 / 128.0);

        const double k = (position - origin) / unit;
        const double nearest = origin + std::round (k) * unit;
        if (std::abs (position - nearest) <= tolerance)
            return nearest;
        return origin + (std::floor (k) + 1.0) * unit;
    }

    void PulseEngine::reschedulePending() noexcept
    {
        const double line = switchLineFor (beatPos);
        if (line <= beatPos)
        {
            activePattern = pendingPattern;
            pendingPattern = -1;
        }
        else
        {
            pendingAtBeat = line;
        }
    }

    void PulseEngine::requestPattern (int index, double beatPosition, bool immediate) noexcept
    {
        index = juce::jlimit (0, numPatterns - 1, index);

        if (immediate || block.switchMode == SwitchMode::instant)
        {
            activePattern = index;
            pendingPattern = -1;
            return;
        }

        // Wait for the next beat or bar line (or switch now if this is on one).
        const double line = switchLineFor (beatPosition);
        if (line <= beatPosition)
        {
            activePattern = index;
            pendingPattern = -1;
            return;
        }
        pendingAtBeat = line;
        pendingPattern = index;
    }

    void PulseEngine::retrigger() noexcept
    {
        phase = 0.0;
        oneShotDone = false;
    }

    void PulseEngine::handleMidiEvent (const juce::uint8* data, int numBytes, double beatPosition) noexcept
    {
        if (data == nullptr || numBytes < 3)
            return;

        const int status = data[0] & 0xf0;
        const int note = data[1] & 0x7f;
        const int velocity = data[2] & 0x7f;

        if (status == 0x90 && velocity > 0)
        {
            heldNotes[(size_t) (note >> 6)] |= (juce::uint64) 1 << (note & 63);

            // Pattern keys pick a pattern; in the note-triggered modes every key also restarts it.
            const bool noteTriggered = block.trigger != Trigger::song;
            if (note >= block.keyStart && note < block.keyStart + numPatterns)
                requestPattern (note - block.keyStart, beatPosition, noteTriggered);

            if (noteTriggered)
                retrigger();
        }
        else if (status == 0x80 || status == 0x90)
        {
            heldNotes[(size_t) (note >> 6)] &= ~((juce::uint64) 1 << (note & 63));
        }
        else if (status == 0xb0 && (data[1] == 120 || data[1] == 123))
        {
            heldNotes = {};
        }
    }

    void PulseEngine::process (float* const* channels, int numChannels, int numInputChannels, int numSamples,
                               const juce::MidiBuffer& midi, const EngineParams& params, const HostTiming& timing,
                               const PatternSet& patterns) noexcept
    {
        if (! prepared || numChannels <= 0 || channels == nullptr)
            return;

        applyBlockParams (params, timing);
        firstBlock = false;

        float* left = channels[0];
        float* right = numChannels > 1 ? channels[1] : nullptr;
        const auto mode = filterModeFor (block.filterType);

        auto midiIt = midi.cbegin();
        const auto midiEnd = midi.cend();

        for (int i = 0; i < numSamples; ++i)
        {
            while (midiIt != midiEnd && (*midiIt).samplePosition <= i)
            {
                const auto event = *midiIt;
                handleMidiEvent (event.data, event.numBytes, beatPos);
                ++midiIt;
            }

            // Where in the pattern this sample is.
            switch (block.trigger)
            {
                case Trigger::song:
                    if (lengthBeats > 0.0)
                    {
                        const double passes = (beatPos - patternOrigin) / lengthBeats;
                        phase = passes - std::floor (passes);
                    }
                    else
                    {
                        phase += phaseIncrement;
                        phase -= std::floor (phase);
                    }
                    break;

                case Trigger::retrigger:
                    phase += phaseIncrement;
                    phase -= std::floor (phase);
                    break;

                case Trigger::oneShot:
                    if (! oneShotDone)
                    {
                        phase += phaseIncrement;
                        if (phase >= 1.0)
                        {
                            phase = 1.0;
                            oneShotDone = true;
                        }
                    }
                    else
                    {
                        phase = 1.0;
                    }
                    break;
            }

            if (! std::isfinite (phase))
                phase = 0.0;

            if (pendingPattern >= 0 && beatPos >= pendingAtBeat)
            {
                activePattern = pendingPattern;
                pendingPattern = -1;
            }

            for (int l = 0; l < numLanes; ++l)
            {
                const float target = patterns.curve (activePattern, (Lane) l).valueAt (phase);
                laneValue[(size_t) l] += laneCoeff * (target - laneValue[(size_t) l]);
            }

            const float inL = numInputChannels > 0 ? left[i] : 0.0f;
            const float inR = (numInputChannels > 1 && right != nullptr) ? right[i] : inL;

            // Volume
            const float volume = 1.0f - volumeAmount.getNextValue() * (1.0f - laneValue[(size_t) Lane::volume]);
            float wetL = inL * volume;
            float wetR = inR * volume;

            // Filter: always running, so switching it on never starts from a cold state.
            const float fm = filterMix.getNextValue();
            const float lo = logLow.getNextValue();
            const float hi = logHigh.getNextValue();
            const float q = resonance.getNextValue();
            const float cutoff = std::exp (lo + laneValue[(size_t) Lane::filter] * (hi - lo));
            filter.setCoefficients (sampleRate, cutoff, q);
            const float filteredL = filter.process (0, wetL, mode);
            const float filteredR = filter.process (1, wetR, mode);
            if (fm > 0.0f)
            {
                wetL += (filteredL - wetL) * fm;
                wetR += (filteredR - wetR) * fm;
            }

            // Pan: a balance control, so the far side fades out and the near side is never boosted.
            const float position = juce::jlimit (-1.0f, 1.0f, (laneValue[(size_t) Lane::pan] - 0.5f) * 2.0f * panAmount.getNextValue());
            if (position > 0.0f)
                wetL *= (float) std::cos ((double) position * halfPi);
            else if (position < 0.0f)
                wetR *= (float) std::cos ((double) -position * halfPi);

            // Mix and output
            const float m = mix.getNextValue();
            float outL, outR;
            if (m >= 1.0f)      { outL = wetL; outR = wetR; }
            else if (m <= 0.0f) { outL = inL;  outR = inR; }
            else
            {
                outL = inL * (1.0f - m) + wetL * m;
                outR = inR * (1.0f - m) + wetR * m;
            }

            const float g = outGain.getNextValue();
            outL *= g;
            outR *= g;

            // Host bypass: fade to the untouched input (everything above keeps running underneath).
            const float b = bypassFade.getNextValue();
            if (b >= 1.0f)     { outL = inL; outR = inR; }
            else if (b > 0.0f) { outL += (inL - outL) * b; outR += (inR - outR) * b; }

            left[i] = outL;
            if (right != nullptr)
                right[i] = outR;

            beatPos += beatsPerSample;

            if (--scopeCountdown <= 0)
            {
                scopeCountdown = scopeInterval;
                publishScope();
            }
        }

        for (; midiIt != midiEnd; ++midiIt)
        {
            const auto event = *midiIt;
            handleMidiEvent (event.data, event.numBytes, beatPos);
        }
    }

    void PulseEngine::processBypassed (int numSamples, const HostTiming& timing) noexcept
    {
        if (! prepared)
            return;

        const double bpm = timing.bpm > 0.0 ? timing.bpm : 120.0;
        beatPos = (timing.playing && timing.hasPpq) ? timing.ppq : beatPos;
        beatPos += bpm / 60.0 / sampleRate * (double) numSamples;

        // Keep the pattern moving so nothing jumps when the bypass is lifted.
        if (block.trigger == Trigger::song && lengthBeats > 0.0)
        {
            const double passes = (beatPos - patternOrigin) / lengthBeats;
            phase = passes - std::floor (passes);
        }
        else if (block.trigger != Trigger::oneShot)
        {
            phase += phaseIncrement * (double) numSamples;
            phase -= std::floor (phase);
        }
        else if (! oneShotDone)
        {
            phase += phaseIncrement * (double) numSamples;
            if (phase >= 1.0)
            {
                phase = 1.0;
                oneShotDone = true;
            }
        }
        if (pendingPattern >= 0 && beatPos >= pendingAtBeat)
        {
            activePattern = pendingPattern;
            pendingPattern = -1;
        }

        scopeCountdown -= numSamples;
        if (scopeCountdown <= 0)
        {
            scopeCountdown = scopeInterval;
            publishScope();
        }
    }

    void PulseEngine::publishScope() noexcept
    {
        ScopeFrame frame;
        frame.phase = (float) phase;
        frame.pattern = activePattern;
        frame.pendingPattern = pendingPattern;
        frame.values = laneValue;
        frame.moving = ! (block.trigger == Trigger::oneShot && oneShotDone);
        frame.heldNotes = heldNotes;
        scopeFifo.push (frame);
    }
}
