// Offline verification for PulseLock. Runs the shipped processor with no host and exits
// non-zero if any check fails, so CI fails with it.
//
//   PulseLockTests                 run everything
//   PulseLockTests --only alloc    run just the allocation check (used for the Debug-CRT build)
//   PulseLockTests --snapshot DIR  render the editor to PNGs in DIR instead of testing

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "PluginProcessor.h"
#include "Presets.h"
#include "ui/MainPanel.h"
#include "ui/PulseLookAndFeel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include <vector>

//==============================================================================
// Allocation counter. Counting is switched on only for the calling thread, and only
// around processBlock, so it measures exactly what the audio thread would allocate.

namespace alloccount
{
    std::atomic<long long> count { 0 };
    thread_local bool active = false;

    inline void note() noexcept
    {
        if (active)
            count.fetch_add (1, std::memory_order_relaxed);
    }

    struct Scope
    {
        Scope() noexcept  { active = true; }
        ~Scope() noexcept { active = false; }
    };

    const char* coverage = "operator new/new[]";
}

void* operator new (std::size_t size)
{
    alloccount::note();
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    alloccount::note();
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept                 { std::free (p); }
void operator delete[] (void* p) noexcept               { std::free (p); }
void operator delete (void* p, std::size_t) noexcept    { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept  { std::free (p); }

#if defined (__linux__) && defined (__GLIBC__)
// glibc lets the executable interpose the C allocator, which also catches JUCE's HeapBlock
// (it calls malloc directly, never operator new).
extern "C"
{
    void* __libc_malloc (size_t);
    void* __libc_calloc (size_t, size_t);
    void* __libc_realloc (void*, size_t);
    void* __libc_memalign (size_t, size_t);
    void  __libc_free (void*);

    void* malloc (size_t size) noexcept                 { alloccount::note(); return __libc_malloc (size); }
    void* calloc (size_t n, size_t size) noexcept       { alloccount::note(); return __libc_calloc (n, size); }
    void* realloc (void* p, size_t size) noexcept       { alloccount::note(); return __libc_realloc (p, size); }
    void  free (void* p) noexcept                       { __libc_free (p); }
    void* memalign (size_t align, size_t size) noexcept { alloccount::note(); return __libc_memalign (align, size); }
    void* aligned_alloc (size_t align, size_t size) noexcept { alloccount::note(); return __libc_memalign (align, size); }

    int posix_memalign (void** out, size_t align, size_t size) noexcept
    {
        alloccount::note();
        void* p = __libc_memalign (align, size);
        if (p == nullptr)
            return ENOMEM;
        *out = p;
        return 0;
    }
}
 #define PULSELOCK_INSTALL_ALLOC_HOOK() alloccount::coverage = "malloc/calloc/realloc/memalign + operator new (glibc interposition)"

#elif defined (_MSC_VER) && defined (_DEBUG)
 #include <crtdbg.h>
// The debug CRT reports every heap allocation, including malloc calls that bypass operator new.
static int crtAllocHook (int allocType, void*, size_t, int, long, const unsigned char*, int)
{
    if (allocType == _HOOK_ALLOC || allocType == _HOOK_REALLOC)
        alloccount::note();
    return 1;
}
 #define PULSELOCK_INSTALL_ALLOC_HOOK() (_CrtSetAllocHook (crtAllocHook), alloccount::coverage = "every CRT heap allocation (debug CRT hook)")

#else
 #define PULSELOCK_INSTALL_ALLOC_HOOK() (void) 0
#endif

//==============================================================================
namespace
{
    using namespace pulselock;

    int failures = 0;

    /** printf-style formatting into a juce::String (juce::String::formatted is wide-char on Windows). */
    juce::String fmt (const char* format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start (args, format);
        std::vsnprintf (buffer, sizeof (buffer), format, args);
        va_end (args);
        return juce::String (buffer);
    }

    void check (bool ok, const juce::String& what)
    {
        std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        std::fflush (stdout);
        if (! ok)
            ++failures;
    }

    void section (const char* name)
    {
        std::printf ("\n== %s ==\n", name);
        std::fflush (stdout);
    }

    struct FakePlayHead final : public juce::AudioPlayHead
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool playing = true;
        int numerator = 4, denominator = 4;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            info.setTimeSignature (TimeSignature { numerator, denominator });
            const double beatsPerBar = numerator * 4.0 / denominator;
            info.setPpqPositionOfLastBarStart (std::floor (ppq / beatsPerBar + 1.0e-9) * beatsPerBar);
            return info;
        }
    };

    struct MidiEvent
    {
        juce::int64 time;
        juce::uint8 bytes[3];
    };

    MidiEvent noteOnAt (juce::int64 t, int note)  { return { t, { 0x90, (juce::uint8) note, 100 } }; }

    using Points = std::vector<CurvePoint>;
    Points flat (float level) { return { { 0.0f, level, 0.0f }, { 1.0f, level, 0.0f } }; }
    Points halfGate()          { return { { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }; }

    /** A processor with no host around it. The fake play head advances with every block. */
    struct Harness
    {
        Harness (double sampleRate, int maxBlock, int numInputs = 2) : rate (sampleRate), blockSize (maxBlock)
        {
            proc.setPlayConfigDetails (numInputs, 2, sampleRate, maxBlock);
            proc.setPlayHead (&playHead);
            proc.prepareToPlay (sampleRate, maxBlock);
        }

        void set (const char* id, float plainValue)
        {
            auto* p = proc.apvts.getParameter (id);
            jassert (p != nullptr);
            p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        }

        /** Runs numSamples with input from signal (time, channel); appends the output. */
        void run (juce::int64 numSamples, const std::vector<MidiEvent>& events,
                  const std::function<float (juce::int64, int)>& signal,
                  std::vector<float>* outL, std::vector<float>* outR = nullptr)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.ensureSize (4096);
            size_t nextEvent = 0;

            for (juce::int64 pos = 0; pos < numSamples; pos += blockSize)
            {
                const int n = (int) std::min<juce::int64> (blockSize, numSamples - pos);
                buffer.setSize (2, n, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        buffer.setSample (ch, i, signal (pos + i, ch));

                midi.clear();
                while (nextEvent < events.size() && events[nextEvent].time < pos + n)
                {
                    const auto& e = events[nextEvent++];
                    midi.addEvent (e.bytes, 3, (int) std::max<juce::int64> (0, e.time - pos));
                }

                proc.processBlock (buffer, midi);
                if (playHead.playing)
                    playHead.ppq += n / rate * playHead.bpm / 60.0;

                if (outL != nullptr)
                    outL->insert (outL->end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + n);
                if (outR != nullptr)
                    outR->insert (outR->end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + n);
            }
        }

        PulseLockProcessor proc;
        FakePlayHead playHead;
        double rate;
        int blockSize;
    };

    float dc (juce::int64, int) { return 0.5f; }

    /** First index at or after `from` where |x| drops below `level`, or -1. */
    juce::int64 firstBelow (const std::vector<float>& x, juce::int64 from, float level)
    {
        for (size_t i = (size_t) std::max<juce::int64> (0, from); i < x.size(); ++i)
            if (std::abs (x[i]) < level)
                return (juce::int64) i;
        return -1;
    }

    //==========================================================================
    void testSongSync()
    {
        section ("1. Song mode: a drawn gate edge lands on the beat it was drawn on");

        // 120 BPM, 1-bar pattern (96000 samples at 48 kHz); the gate closes halfway = beat 3 = sample 48000.
        Harness h (48000.0, 512);
        h.proc.setLanePoints (0, Lane::volume, halfGate(), {});
        h.set (ParamID::smooth, 0.2f);

        std::vector<float> out;
        h.run (100000, {}, dc, &out);

        const auto edge = firstBelow (out, 1000, 0.25f);
        check (std::abs (edge - 48000) <= 48, fmt ("gate closes at sample %lld (expected 48000, within 1 ms)", (long long) edge));
        check (std::abs (out[20000] - 0.5f) < 1.0e-4f && std::abs (out[70000]) < 1.0e-4f,
               fmt ("open before the edge (%.4f), shut after it (%.6f)", out[20000], out[70000]));

        // Starting the song at bar 3, beat 2 (ppq 9): the pattern is already a quarter in, so it closes after 1 beat.
        Harness late (48000.0, 512);
        late.proc.setLanePoints (0, Lane::volume, halfGate(), {});
        late.set (ParamID::smooth, 0.2f);
        late.playHead.ppq = 9.0;
        std::vector<float> lateOut;
        late.run (60000, {}, dc, &lateOut);
        const auto lateEdge = firstBelow (lateOut, 100, 0.25f);
        check (std::abs (lateEdge - 24000) <= 48, fmt ("started mid-bar: closes after one beat, at sample %lld (expected 24000)", (long long) lateEdge));
    }

    void testNoteTriggers()
    {
        section ("2. Retrigger and one-shot: a note restarts the pattern");

        {
            Harness h (48000.0, 512);
            h.playHead.playing = false;
            h.proc.setLanePoints (0, Lane::volume, halfGate(), {});
            h.set (ParamID::smooth, 0.2f);
            h.set (ParamID::trigger, (float) (int) Trigger::retrigger);

            std::vector<float> out;
            h.run (80000, { noteOnAt (10000, 80) }, dc, &out);
            const auto edge = firstBelow (out, 10100, 0.25f);
            check (std::abs (edge - 58000) <= 48,
                   fmt ("retrigger: note at 10000, gate closes at %lld (expected 58000)", (long long) edge));
        }

        {
            Harness h (48000.0, 512);
            h.playHead.playing = false;
            h.proc.setLanePoints (0, Lane::volume, halfGate(), {});
            h.set (ParamID::smooth, 0.2f);
            h.set (ParamID::trigger, (float) (int) Trigger::oneShot);

            std::vector<float> out;
            h.run (120000, { noteOnAt (10000, 80) }, dc, &out);
            check (std::abs (out[5000]) < 1.0e-4f && std::abs (out[11000] - 0.5f) < 1.0e-3f && std::abs (out[115000]) < 1.0e-4f,
                   fmt ("one-shot: rests at the end value before a note (%.4f), plays after it (%.4f), rests again (%.4f)",
                        out[5000], out[11000], out[115000]));
        }
    }

    void testPatternSwitching()
    {
        section ("3. Pattern keys switch patterns, instantly or on the next beat");

        auto runCase = [] (SwitchMode mode, juce::int64 expected, const char* label)
        {
            Harness h (48000.0, 512);
            h.proc.setLanePoints (0, Lane::volume, flat (1.0f), {});   // A: open
            h.proc.setLanePoints (1, Lane::volume, flat (0.0f), {});   // B: shut
            h.set (ParamID::smooth, 0.2f);
            h.set (ParamID::switchMode, (float) (int) mode);

            std::vector<float> out;
            h.run (40000, { noteOnAt (20000, 37) }, dc, &out);   // keys start at C1 (36): 37 = pattern 2
            const auto edge = firstBelow (out, 100, 0.25f);
            check (std::abs (edge - expected) <= 48, fmt ("%s: key at 20000, switches at %lld (expected %lld)",
                                                         label, (long long) edge, (long long) expected));
        };

        runCase (SwitchMode::instant, 20000, "instant");
        runCase (SwitchMode::nextBeat, 24000, "next beat");   // beats every 24000 samples at 120 BPM

        // The Pattern parameter switches too.
        Harness h (48000.0, 512);
        h.proc.setLanePoints (2, Lane::volume, flat (0.25f), {});
        h.set (ParamID::smooth, 0.2f);
        std::vector<float> out;
        h.run (4096, {}, dc, &out);
        h.set (ParamID::pattern, 2.0f);
        h.run (4096, {}, dc, &out);
        check (std::abs (out.back() - 0.125f) < 1.0e-3f, fmt ("choosing pattern 3 from the parameter: level %.4f (expected 0.125)", out.back()));
    }

    /** First index at or after `from` where |x| rises above `level`, or -1. */
    juce::int64 firstAbove (const std::vector<float>& x, juce::int64 from, float level)
    {
        for (size_t i = (size_t) std::max<juce::int64> (0, from); i < x.size(); ++i)
            if (std::abs (x[i]) > level)
                return (juce::int64) i;
        return -1;
    }

    /** Pattern 1 open, 2 at half (0.5), 3 shut, fast smoothing. */
    void openHalfShut (Harness& h)
    {
        h.proc.setLanePoints (0, Lane::volume, flat (1.0f), {});
        h.proc.setLanePoints (1, Lane::volume, flat (0.5f), {});
        h.proc.setLanePoints (2, Lane::volume, flat (0.0f), {});
        h.set (ParamID::smooth, 0.2f);
    }

    void testTimingEdges()
    {
        section ("3b. Bar lines, other time signatures, relocating, re-clicking, bypass");

        // A key quantised onto a bar line switches on that bar, not one bar later.
        auto onTheLine = [] (double sampleRate, double bpm, juce::int64 noteAt, juce::int64 expected, const char* label)
        {
            Harness h (sampleRate, 512);
            h.playHead.bpm = bpm;
            openHalfShut (h);
            h.set (ParamID::switchMode, (float) (int) SwitchMode::nextBar);
            std::vector<float> out;
            h.run (expected + 30000, { noteOnAt (noteAt, 38) }, dc, &out);   // 38 = pattern 3, shut
            const auto edge = firstBelow (out, 100, 0.25f);
            check (std::abs (edge - expected) <= 48, fmt ("%s: switches at %lld (expected %lld)", label, (long long) edge, (long long) expected));
        };
        onTheLine (48000.0, 120.0, 96000, 96000, "next bar, key exactly on bar 2 at 120 BPM / 48 kHz");
        // At 128 BPM / 44.1 kHz bar 2 falls between samples 82687 and 82688; a key on either is on the line.
        onTheLine (44100.0, 128.0, 82687, 82688, "next bar, key half a sample before bar 2 at 128 BPM / 44.1 kHz");
        onTheLine (44100.0, 128.0, 82688, 82688, "next bar, key half a sample after bar 2 at 128 BPM / 44.1 kHz");
        onTheLine (48000.0, 120.0, 20000, 96000, "next bar, key early in bar 1 at 120 BPM / 48 kHz");

        // 3/4: one bar is three beats, bar lines every 72000 samples at 120 BPM / 48 kHz.
        {
            Harness h (48000.0, 512);
            h.playHead.numerator = 3;
            h.proc.setLanePoints (0, Lane::volume, halfGate(), {});
            h.set (ParamID::smooth, 0.2f);
            std::vector<float> out;
            h.run (120000, {}, dc, &out);
            const auto closes = firstBelow (out, 1000, 0.25f);
            const auto reopens = firstAbove (out, closes + 100, 0.25f);
            check (std::abs (closes - 36000) <= 48 && std::abs (reopens - 72000) <= 48,
                   fmt ("3/4, 1-bar gate: closes at %lld, reopens at %lld (expected 36000, 72000)",
                        (long long) closes, (long long) reopens));
        }
        {
            Harness h (48000.0, 512);
            h.playHead.numerator = 3;
            openHalfShut (h);
            h.set (ParamID::switchMode, (float) (int) SwitchMode::nextBar);
            std::vector<float> out;
            h.run (100000, { noteOnAt (50000, 38) }, dc, &out);
            const auto edge = firstBelow (out, 100, 0.25f);
            check (std::abs (edge - 72000) <= 48, fmt ("3/4, next bar: key at 50000 switches at %lld (expected 72000, the bar-2 line)", (long long) edge));
        }
        {
            // A 2-bar pattern (shut, then open) in 3/4, started at bar 2: it plays its second, open half,
            // and the next pass starts (shut) on bar 3, 3 beats = 72000 samples later.
            Harness h (48000.0, 512);
            h.playHead.numerator = 3;
            h.playHead.ppq = 3.0;
            h.proc.setLanePoints (0, Lane::volume, { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, {});
            h.set (ParamID::smooth, 0.2f);
            h.set (ParamID::sync, 10.0f);   // 2 bars
            std::vector<float> out;
            h.run (100000, {}, dc, &out);
            const auto closes = firstBelow (out, 1000, 0.25f);
            check (out[1000] > 0.45f && std::abs (closes - 72000) <= 48,
                   fmt ("3/4, 2-bar pattern started at bar 2: plays its second half (%.3f), next pass starts on bar 3 at %lld (expected 72000)",
                        out[1000], (long long) closes));
        }

        // Relocating backwards while a switch waits: it moves to the next bar line after the new position.
        {
            Harness h (48000.0, 512);
            openHalfShut (h);
            h.set (ParamID::switchMode, (float) (int) SwitchMode::nextBar);
            h.playHead.ppq = 12.0;
            std::vector<float> out;
            h.run (12000, { noteOnAt (6000, 38) }, dc, &out);   // waiting for ppq 16
            h.playHead.ppq = 1.0;                                // cycle back to bar 1
            h.run (96000, {}, dc, &out);
            const auto edge = firstBelow (out, 100, 0.25f);
            check (std::abs (edge - 84000) <= 48,
                   fmt ("relocated from bar 4 to bar 1 while waiting: switches at %lld (bar 2, expected 84000)", (long long) edge));
        }

        // Clicking the selected pattern again after a key switched away from it switches back.
        {
            Harness h (48000.0, 512);
            openHalfShut (h);
            std::vector<float> out;
            h.set (ParamID::pattern, 1.0f);
            h.run (4096, {}, dc, &out);
            const float onB = out.back();
            h.run (4096, { noteOnAt (100, 38) }, dc, &out);
            const float onC = out.back();
            h.proc.requestPatternFromUi (1);
            h.run (4096, {}, dc, &out);
            const float backOnB = out.back();
            check (std::abs (onB - 0.25f) < 1.0e-3f && std::abs (onC) < 1.0e-3f && std::abs (backOnB - 0.25f) < 1.0e-3f,
                   fmt ("pattern 2 chosen (%.3f), key to 3 (%.3f), click 2 again: 2 plays (%.3f)", onB, onC, backOnB));
        }

        // The host's bypass switch crossfades both ways.
        {
            Harness h (48000.0, 512);
            openHalfShut (h);
            h.set (ParamID::pattern, 2.0f);   // pattern 3: shut
            std::vector<float> out;
            h.run (4096, {}, dc, &out);
            const float active = out.back();
            h.set (ParamID::bypass, 1.0f);
            h.run (4096, {}, dc, &out);
            const float bypassed = out.back();
            h.set (ParamID::bypass, 0.0f);
            h.run (4096, {}, dc, &out);
            const float back = out.back();

            // Measured from the moment bypass is pressed (before that the lane is still settling from
            // its start-up value onto the shut pattern, which is the Smooth time's job, not bypass's).
            float biggest = 0.0f;
            for (size_t i = 4097; i < out.size(); ++i)
                biggest = std::max (biggest, std::abs (out[i] - out[i - 1]));
            check (std::abs (active) < 1.0e-4f && juce::exactlyEqual (bypassed, 0.5f) && std::abs (back) < 1.0e-4f && biggest < 0.01f,
                   fmt ("bypass on: input untouched (%.4f); off: effect back (%.5f); largest one-sample step %.4f",
                        bypassed, back, biggest));
        }
    }

    void testNoClicks()
    {
        section ("4. Gate edges are smoothed (no clicks)");

        Harness h (48000.0, 256);   // factory pattern 1: 16th gate; default Smooth 1.5 ms
        std::vector<float> out;
        h.run (96000, {}, dc, &out);

        float biggest = 0.0f;
        int edges = 0;
        for (size_t i = 1; i < out.size(); ++i)
        {
            const float step = std::abs (out[i] - out[i - 1]);
            biggest = std::max (biggest, step);
            if ((out[i] < 0.25f) != (out[i - 1] < 0.25f))
                ++edges;
        }

        // A 1.5 ms one-pole moves at most 1 - exp(-1 / 72) = 1.4% of a 0.5 jump per sample.
        check (edges >= 30 && biggest < 0.01f, fmt ("%d gate edges in one bar, largest one-sample step %.4f (limit 0.01)", edges, biggest));
    }

    //==========================================================================
    /** Mean power in [loHz, hiHz] of x (Hann-windowed 2^16 FFT on the tail). */
    double bandPower (const std::vector<float>& x, double rate, double loHz, double hiHz)
    {
        constexpr int order = 16;
        const int n = 1 << order;
        std::vector<float> buf ((size_t) (2 * n), 0.0f);
        const size_t start = x.size() - (size_t) n;
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = x[start + (size_t) i] * (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (n - 1)));

        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (buf.data());

        double sum = 0.0;
        int count = 0;
        for (int k = (int) (loHz * n / rate); k <= (int) (hiHz * n / rate); ++k, ++count)
            sum += (double) buf[(size_t) k] * buf[(size_t) k];
        return sum / juce::jmax (1, count);
    }

    void testFilterLane()
    {
        section ("5. Filter lane: the curve sweeps the cutoff between Low and High");

        auto runAt = [] (float laneLevel)
        {
            Harness h (48000.0, 512);
            h.set (ParamID::volumeOn, 0.0f);
            h.set (ParamID::filterOn, 1.0f);
            h.set (ParamID::filterLow, 250.0f);
            h.set (ParamID::filterHigh, 8000.0f);
            h.set (ParamID::filterRes, 0.0f);
            h.proc.setLanePoints (0, Lane::filter, flat (laneLevel), {});

            juce::Random rng (77);
            std::vector<float> out;
            h.run (96000, {}, [&rng] (juce::int64, int) { return rng.nextFloat() - 0.5f; }, &out);
            const double low = bandPower (out, h.rate, 60.0, 120.0);
            const double high = bandPower (out, h.rate, 2000.0, 4000.0);
            return 10.0 * std::log10 (high / low);
        };

        const double closed = runAt (0.0f);   // cutoff 250 Hz: 2-4 kHz is 3+ octaves above
        const double open = runAt (1.0f);     // cutoff 8 kHz: 2-4 kHz passes
        check (closed < -30.0, fmt ("lane at 0 (250 Hz low-pass): 2-4 kHz sits %.1f dB below 60-120 Hz", closed));
        check (open > -3.0, fmt ("lane at 1 (8 kHz low-pass): 2-4 kHz within 3 dB of the lows (%.1f dB)", open));
    }

    void testPanLane()
    {
        section ("6. Pan lane: a balance that moves the sound without boosting it");

        auto runAt = [] (float laneLevel, float& leftGain, float& rightGain)
        {
            Harness h (48000.0, 512);
            h.set (ParamID::volumeOn, 0.0f);
            h.set (ParamID::panOn, 1.0f);
            h.proc.setLanePoints (0, Lane::pan, flat (laneLevel), {});
            std::vector<float> outL, outR;
            h.run (20000, {}, dc, &outL, &outR);
            leftGain = outL.back() / 0.5f;
            rightGain = outR.back() / 0.5f;
        };

        float l = 0, r = 0;
        runAt (1.0f, l, r);
        check (std::abs (l) < 1.0e-5f && std::abs (r - 1.0f) < 1.0e-5f, fmt ("hard right: left %.6f, right %.6f", l, r));
        runAt (0.0f, l, r);
        check (std::abs (l - 1.0f) < 1.0e-5f && std::abs (r) < 1.0e-5f, fmt ("hard left: left %.6f, right %.6f", l, r));
        runAt (0.5f, l, r);
        check (std::abs (l - 1.0f) < 1.0e-6f && std::abs (r - 1.0f) < 1.0e-6f, fmt ("centre: left %.6f, right %.6f", l, r));
    }

    void testTransparency()
    {
        section ("7. An open pattern, filter and pan off: output equals input within 1e-6");

        for (int numInputs : { 2, 1 })
        {
            Harness h (48000.0, 4096, numInputs);
            h.proc.setLanePoints (0, Lane::volume, flat (1.0f), {});
            juce::Random rng (31337);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buffer (2, 4096);

            float maxDiff = 0.0f;
            juce::int64 processed = 0;
            while (processed < (juce::int64) (5.0 * h.rate))
            {
                const int n = 1 + rng.nextInt (4096);
                buffer.setSize (2, n, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        buffer.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.9f);

                const juce::AudioBuffer<float> input (buffer);
                h.proc.processBlock (buffer, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < n; ++i)
                        maxDiff = std::max (maxDiff, std::abs (buffer.getSample (ch, i) - input.getSample (numInputs == 1 ? 0 : ch, i)));
                processed += n;
            }

            check (maxDiff <= 1.0e-6f, fmt ("%s input, blocks of 1..4096 samples: max |out - in| = %.3g",
                                            numInputs == 2 ? "stereo" : "mono", (double) maxDiff));
        }
    }

    //==========================================================================
    void testState()
    {
        section ("8. Save and restore: every parameter and every drawn point round-trips; edits undo");

        PulseLockProcessor a;
        juce::Random rng (2024);
        for (const char* id : ParamID::all)
        {
            auto* p = a.apvts.getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (p->convertFrom0to1 (rng.nextFloat())));
        }

        // A random curve with a vertical edge and bends in every lane of pattern 6.
        for (int l = 0; l < numLanes; ++l)
        {
            Points pts { { 0.0f, rng.nextFloat(), rng.nextFloat() * 2.0f - 1.0f } };
            for (int k = 1; k < 10; ++k)
                pts.push_back ({ (float) k / 10.0f, rng.nextFloat(), rng.nextFloat() * 2.0f - 1.0f });
            pts.push_back ({ 0.9f, rng.nextFloat(), 0.0f });
            pts.push_back ({ 1.0f, rng.nextFloat(), 0.0f });
            a.setLanePoints (5, (Lane) l, pts, {});
        }

        juce::MemoryBlock state;
        a.getStateInformation (state);
        PulseLockProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());

        int mismatches = 0;
        for (const char* id : ParamID::all)
            if (std::abs (a.apvts.getParameter (id)->getValue() - b.apvts.getParameter (id)->getValue()) > 1.0e-6f)
                ++mismatches;

        int pointMismatches = 0;
        for (int p = 0; p < numPatterns; ++p)
            for (int l = 0; l < numLanes; ++l)
            {
                const auto pa = a.getLanePoints (p, (Lane) l);
                const auto pb = b.getLanePoints (p, (Lane) l);
                if (pa.size() != pb.size())
                    ++pointMismatches;
                else
                    for (size_t i = 0; i < pa.size(); ++i)
                        if (std::abs (pa[i].x - pb[i].x) > 1.0e-6f || std::abs (pa[i].y - pb[i].y) > 1.0e-6f
                            || std::abs (pa[i].bend - pb[i].bend) > 1.0e-6f)
                            ++pointMismatches;
            }

        check (mismatches == 0, fmt ("all %d parameters restored (%d mismatches)", (int) std::size (ParamID::all), mismatches));
        check (pointMismatches == 0, fmt ("all 24 lanes' points restored (%d mismatches)", pointMismatches));

        // Undo and redo are heard, not just stored.
        {
            Harness h (48000.0, 512);
            h.set (ParamID::smooth, 0.2f);
            h.proc.setLanePoints (0, Lane::volume, flat (1.0f), {});
            h.proc.setLanePoints (0, Lane::volume, flat (0.0f), "Clear");
            std::vector<float> out;
            h.run (2048, {}, dc, &out);
            const float cleared = out.back();
            h.proc.undoPatterns();
            h.run (2048, {}, dc, &out);
            const float undone = out.back();
            h.proc.redoPatterns();
            h.run (2048, {}, dc, &out);
            const float redone = out.back();
            check (std::abs (cleared) < 1.0e-4f && std::abs (undone - 0.5f) < 1.0e-4f && std::abs (redone) < 1.0e-4f,
                   fmt ("an edit plays at once (%.5f), undo plays the old shape (%.5f), redo the new one (%.5f)",
                        cleared, undone, redone));
        }

        // A drag in the editor: live previews while dragging, then one undo step back to where it began.
        {
            Harness h (48000.0, 512);
            h.set (ParamID::smooth, 0.2f);
            h.proc.setLanePoints (0, Lane::volume, flat (1.0f), {});
            const auto before = h.proc.getLanePoints (0, Lane::volume);
            std::vector<float> out;
            h.proc.setLanePoints (0, Lane::volume, flat (0.8f), {});   // preview mid-drag
            h.run (1024, {}, dc, &out);
            h.proc.setLanePoints (0, Lane::volume, flat (0.6f), {});   // another preview
            h.proc.commitLaneEdit (0, Lane::volume, before, flat (0.25f), "Move point");
            h.run (2048, {}, dc, &out);
            const float committed = out.back();
            h.proc.undoPatterns();
            h.run (2048, {}, dc, &out);
            const float undone = out.back();
            check (std::abs (committed - 0.125f) < 1.0e-4f && std::abs (undone - 0.5f) < 1.0e-4f
                       && ! h.proc.getPatternUndo().canUndo(),
                   fmt ("a finished drag plays (%.5f); one undo returns to before the drag (%.5f), not to a preview",
                        committed, undone));
        }

        // A restored session reaches the audio thread, and brings no stale undo history with it.
        {
            Harness saver (48000.0, 512);
            saver.proc.setLanePoints (0, Lane::volume, flat (0.25f), {});
            juce::MemoryBlock saved;
            saver.proc.getStateInformation (saved);

            Harness h (48000.0, 512);
            h.set (ParamID::smooth, 0.2f);
            h.proc.setLanePoints (0, Lane::volume, flat (1.0f), "Edit");
            std::vector<float> out;
            h.run (2048, {}, dc, &out);
            h.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            h.run (4096, {}, dc, &out);
            check (std::abs (out.back() - 0.125f) < 1.0e-3f && ! h.proc.getPatternUndo().canUndo(),
                   fmt ("restoring a session while playing: the restored drawing plays (%.4f, expected 0.125), undo history cleared",
                        out.back()));
        }
    }
}

namespace
{
    using namespace pulselock;

    void testNoAllocations (double minutes)
    {
        section ("9. Long run at 48 kHz / 64-sample blocks, patterns edited while playing: no audio-thread allocations");

        const double rate = 48000.0;
        const int block = 64;
        Harness h (rate, block);

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        midi.ensureSize (8192);
        juce::Random rng (99);

        const juce::int64 totalBlocks = (juce::int64) (minutes * 60.0 * rate / block);
        const juce::int64 blocksPerChange = (juce::int64) (2.0 * rate / block);
        bool finite = true;

        alloccount::count = 0;
        const auto startMs = juce::Time::getMillisecondCounterHiRes();

        for (juce::int64 b = 0; b < totalBlocks; ++b)
        {
            // Everything a host or the editor would do between callbacks happens outside the counted scope.
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < block; ++i)
                    d[i] = (rng.nextFloat() * 2.0f - 1.0f) * 0.4f;
            }

            midi.clear();
            if (rng.nextInt (8) == 0)
            {
                const juce::uint8 on[3] = { 0x90, (juce::uint8) (30 + rng.nextInt (20)), 100 };   // pattern keys and others
                midi.addEvent (on, 3, rng.nextInt (block));
            }

            if (b % blocksPerChange == 0)
            {
                h.set (ParamID::pattern, (float) rng.nextInt (numPatterns));
                h.set (ParamID::sync, (float) rng.nextInt (12));
                h.set (ParamID::rate, 0.1f + rng.nextFloat() * 15.0f);
                h.set (ParamID::trigger, (float) rng.nextInt (3));
                h.set (ParamID::switchMode, (float) rng.nextInt (3));
                h.set (ParamID::volumeOn, (float) rng.nextInt (2));
                h.set (ParamID::volumeDepth, rng.nextFloat() * 100.0f);
                h.set (ParamID::filterOn, (float) rng.nextInt (2));
                h.set (ParamID::filterType, (float) rng.nextInt (3));
                h.set (ParamID::filterLow, 20.0f + rng.nextFloat() * 2000.0f);
                h.set (ParamID::filterHigh, 500.0f + rng.nextFloat() * 19000.0f);
                h.set (ParamID::filterRes, rng.nextFloat() * 100.0f);
                h.set (ParamID::panOn, (float) rng.nextInt (2));
                h.set (ParamID::panDepth, rng.nextFloat() * 100.0f);
                h.set (ParamID::smooth, rng.nextFloat() * 20.0f);
                h.set (ParamID::mix, rng.nextFloat() * 100.0f);
                h.set (ParamID::outGain, rng.nextFloat() * 24.0f - 12.0f);
                h.playHead.playing = rng.nextInt (4) != 0;

                // An edit from the editor: a random curve in a random lane (published to the audio thread).
                std::vector<CurvePoint> pts { { 0.0f, rng.nextFloat(), 0.0f } };
                const int extra = rng.nextInt (40);
                for (int k = 1; k <= extra; ++k)
                    pts.push_back ({ rng.nextFloat(), rng.nextFloat(), rng.nextFloat() * 2.0f - 1.0f });
                pts.push_back ({ 1.0f, rng.nextFloat(), 0.0f });
                h.proc.setLanePoints (rng.nextInt (numPatterns), (Lane) rng.nextInt (numLanes), pts, {});
            }

            {
                const alloccount::Scope counting;
                h.proc.processBlock (buffer, midi);
            }
            if (h.playHead.playing)
                h.playHead.ppq += block / rate * h.playHead.bpm / 60.0;

            const float* l = buffer.getReadPointer (0);
            const float* r = buffer.getReadPointer (1);
            for (int i = 0; i < block; ++i)
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
        }

        const double seconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
        std::printf ("    %.1f minutes of audio in %.1f s; counting %s\n", minutes, seconds, alloccount::coverage);

        check (finite, "long run: no NaN or inf");
        check (alloccount::count.load() == 0, fmt ("allocations inside processBlock: %lld", alloccount::count.load()));
    }

    void testFactoryPresets()
    {
        section ("Presets: each one sets exactly its values (defaults elsewhere), keeps the drawn patterns, and plays");

        const auto& presets = factoryPresets();
        check (presets.size() >= 8 && juce::String (presets[0].name) == "Init",
               fmt ("%d factory presets, starting with Init", (int) presets.size()));

        for (int index = 0; index < (int) presets.size(); ++index)
        {
            const auto& preset = presets[(size_t) index];
            Harness h (48000.0, 512);

            juce::Random scramble (100 + index);
            for (const char* id : ParamID::all)
            {
                auto* p = h.proc.apvts.getParameter (id);
                p->setValueNotifyingHost (p->convertTo0to1 (p->convertFrom0to1 (scramble.nextFloat())));
            }
            h.proc.setLanePoints (7, Lane::volume, flat (0.3f), {});   // a user's drawing

            h.proc.loadPreset (index);

            int wrong = 0;
            for (const char* id : ParamID::all)
            {
                auto* p = h.proc.apvts.getParameter (id);
                float want = p->convertFrom0to1 (p->getDefaultValue());
                for (const auto& v : preset.values)
                    if (juce::String (v.id) == id)
                        want = p->convertFrom0to1 (p->convertTo0to1 (v.value));
                const float got = h.proc.apvts.getRawParameterValue (id)->load();
                if (std::abs (got - want) > 1.0e-3f * std::max (1.0f, std::abs (want)))
                {
                    ++wrong;
                    std::printf ("    %s: %s is %.4f, expected %.4f\n", preset.name, id, got, want);
                }
            }
            const bool drawingKept = std::abs (h.proc.getLanePoints (7, Lane::volume).front().y - 0.3f) < 1.0e-6f;
            check (wrong == 0 && drawingKept && h.proc.getCurrentPresetName() == preset.name,
                   fmt ("%s: all %d parameters as specified, drawings untouched", preset.name, (int) std::size (ParamID::all)));

            juce::Random noise (40 + index);
            std::vector<float> out;
            h.run (96000, { noteOnAt (24000, 37), noteOnAt (48000, 60) },
                   [&noise] (juce::int64, int) { return noise.nextFloat() - 0.5f; }, &out);
            float peak = 0.0f;
            bool finite = true;
            for (float v : out)
            {
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }
            check (finite && peak > 0.01f, fmt ("%s: plays (peak %.3f)", preset.name, peak));
        }
    }

    void testExtras()
    {
        section ("Extra: sample rates, block sizes, bypass, host garbage, layouts, latency");

        PulseLockProcessor proc;
        FakePlayHead playHead;
        proc.setPlayHead (&playHead);
        juce::Random rng (5);
        bool finite = true;

        for (const auto& [rate, block] : { std::pair<double, int> { 44100.0, 512 }, { 96000.0, 1 },
                                           { 192000.0, 4096 }, { 22050.0, 33 }, { 48000.0, 4096 } })
        {
            proc.setPlayConfigDetails (2, 2, rate, block);
            proc.prepareToPlay (rate, block);
            proc.apvts.getParameter (ParamID::filterOn)->setValueNotifyingHost (1.0f);
            proc.apvts.getParameter (ParamID::panOn)->setValueNotifyingHost (1.0f);

            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            for (juce::int64 pos = 0; pos < (juce::int64) (0.5 * rate); pos += block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        buffer.setSample (ch, i, rng.nextFloat() - 0.5f);
                proc.processBlock (buffer, midi);
                playHead.ppq += block / rate * 2.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
            }
        }
        check (finite, "44.1/96/192/22.05/48 kHz with 512/1/4096/33/4096-sample blocks, filter and pan on: all output finite");

        // Bypass: audio passes untouched, including a mono input copied to both sides.
        {
            Harness h (48000.0, 512, 1);
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample (0, i, 0.3f);
                buffer.setSample (1, i, -7.0f);   // garbage a host may leave in the unused channel
            }
            h.proc.processBlockBypassed (buffer, midi);
            check (std::abs (buffer.getSample (0, 100) - 0.3f) < 1.0e-7f && std::abs (buffer.getSample (1, 100) - 0.3f) < 1.0e-7f,
                   "bypass with a mono input: both outputs carry the input");
        }

        // A host sending an infinite tempo, a NaN position and NaN parameters.
        {
            Harness h (48000.0, 256);
            h.playHead.bpm = std::numeric_limits<double>::infinity();
            h.playHead.ppq = std::numeric_limits<double>::quiet_NaN();
            for (const char* id : { ParamID::filterLow, ParamID::smooth, ParamID::rate, ParamID::mix })
                h.proc.apvts.getParameter (id)->setValueNotifyingHost (std::numeric_limits<float>::quiet_NaN());
            h.set (ParamID::filterOn, 1.0f);

            juce::Random noise (12);
            std::vector<float> out;
            h.run (48000, { noteOnAt (1000, 37) }, [&noise] (juce::int64, int) { return noise.nextFloat() - 0.5f; }, &out);
            bool ok = true;
            for (float v : out)
                ok = ok && std::isfinite (v);
            check (ok, "infinite tempo, NaN position and NaN parameters: output stays finite");
        }

        check (proc.getLatencySamples() == 0, "reports zero latency");

        using Set = juce::AudioChannelSet;
        auto layout = [] (Set in, Set out)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (in);
            l.outputBuses.add (out);
            return l;
        };
        check (proc.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())), "stereo in / stereo out supported");
        check (proc.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())), "mono in / stereo out supported");
        check (! proc.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono())), "stereo in / mono out rejected");
    }
}

namespace
{
    using namespace pulselock;

    /** Renders the real interface (2x) to a PNG after playing some of the song, exactly as the
        editor draws it, including the playhead fed through the audio thread's FIFO. */
    bool renderSnapshot (const juce::File& file, const std::function<void (Harness&)>& setUp,
                         const std::vector<MidiEvent>& events, juce::int64 playSamples)
    {
        Harness h (48000.0, 1600);   // one display frame per block
        setUp (h);

        ui::PulseLookAndFeel lookAndFeel;
        ui::MainPanel panel (h.proc);
        panel.setLookAndFeel (&lookAndFeel);
        panel.setSize (ui::Theme::baseWidth, ui::Theme::baseHeight);

        juce::Random noise (3);
        for (juce::int64 pos = 0; pos < playSamples; pos += 1600)
        {
            std::vector<MidiEvent> blockEvents;
            for (const auto& e : events)
                if (e.time >= pos && e.time < pos + 1600)
                    blockEvents.push_back ({ e.time - pos, { e.bytes[0], e.bytes[1], e.bytes[2] } });

            h.run (1600, blockEvents, [&noise] (juce::int64, int) { return noise.nextFloat() - 0.5f; }, nullptr);

            ScopeFrame frame;
            const bool fresh = h.proc.getScopeFifo().pullLatest (frame);
            panel.tick (fresh ? &frame : nullptr);
        }

        const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 2.0f);
        file.deleteFile();
        bool ok = false;
        {
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;
            ok = out.openedOk() && image.isValid() && png.writeImageToStream (image, out);
        }
        panel.setLookAndFeel (nullptr);

        std::printf ("%s %s (%d x %d)\n", ok ? "wrote" : "FAILED to write", file.getFullPathName().toRawUTF8(),
                     image.getWidth(), image.getHeight());
        return ok;
    }

    int renderSnapshots (const juce::String& directory)
    {
        const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (directory);
        dir.createDirectory();

        // The default: pattern 1's 16th gate on Volume, playing along with the song.
        bool ok = renderSnapshot (dir.getChildFile ("pulselock-gate.png"), [] (Harness&) {}, {}, 60000);

        // Pattern 5 (riser) on the Filter lane with filter and pan on, while key C#1 asks for
        // pattern 2 at the next bar, so 2 shows as waiting.
        ok = renderSnapshot (dir.getChildFile ("pulselock-filter.png"), [] (Harness& h)
        {
            h.set (ParamID::pattern, 4.0f);
            h.set (ParamID::filterOn, 1.0f);
            h.set (ParamID::panOn, 1.0f);
            h.set (ParamID::switchMode, (float) (int) SwitchMode::nextBar);
            h.proc.apvts.state.setProperty ("editLane", (int) Lane::filter, nullptr);
        }, { noteOnAt (60000, 37) }, 72000) && ok;

        return ok ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    PULSELOCK_INSTALL_ALLOC_HOOK();
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String only;
    double allocMinutes = 10.0;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--only" && i + 1 < argc)
            only = argv[++i];
        else if (arg == "--alloc-minutes" && i + 1 < argc)
            allocMinutes = juce::String (argv[++i]).getDoubleValue();
        else if (arg == "--snapshot" && i + 1 < argc)
            return renderSnapshots (argv[++i]);
    }

    auto wants = [&only] (const char* name) { return only.isEmpty() || only == name; };

    if (wants ("sync"))     testSongSync();
    if (wants ("triggers")) testNoteTriggers();
    if (wants ("switch"))   testPatternSwitching();
    if (wants ("timing"))   testTimingEdges();
    if (wants ("clicks"))   testNoClicks();
    if (wants ("filter"))   testFilterLane();
    if (wants ("pan"))      testPanLane();
    if (wants ("dry"))      testTransparency();
    if (wants ("state"))    testState();
    if (wants ("alloc"))    testNoAllocations (allocMinutes);
    if (wants ("presets"))  testFactoryPresets();
    if (wants ("extra"))    testExtras();

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
