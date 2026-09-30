#pragma once

#include "../Parameters.h"

#include <array>
#include <atomic>
#include <vector>

namespace pulselock
{
    /** A point on a lane's curve. x is the position through the pattern (0..1), y the lane value (0..1),
        bend shapes the segment from this point to the next (-1..1, 0 = straight). */
    struct CurvePoint
    {
        float x = 0.0f;
        float y = 0.0f;
        float bend = 0.0f;
    };

    /** Bent interpolation for u in 0..1. bend > 0 starts slow and finishes fast; bend < 0 the reverse. */
    float bendShape (float u, float bend) noexcept;

    /** One lane's shape over one pass of the pattern. Points are sorted by x, run from x = 0 to x = 1,
        and two points may share an x to make a vertical edge (a hard gate). */
    struct Curve
    {
        static constexpr int maxPoints = 128;

        std::array<CurvePoint, maxPoints> points {};
        int count = 0;

        float valueAt (double x) const noexcept;
    };

    /** Every curve of every pattern: what the audio thread plays. */
    struct PatternSet
    {
        std::array<std::array<Curve, numLanes>, numPatterns> curves {};

        const Curve& curve (int pattern, Lane lane) const noexcept
        {
            return curves[(size_t) juce::jlimit (0, numPatterns - 1, pattern)][(size_t) lane];
        }
    };

    /** Shape builders used by the factory patterns and by the editor's stamp buttons. */
    namespace Shapes
    {
        using Points = std::vector<CurvePoint>;

        Points flat (float level);

        /** Hard-edged steps: each step holds its level for `duty` of the step, then drops to `floor`. */
        Points steps (const std::vector<float>& levels, float duty, float floor = 0.0f);

        /** A trance gate: `count` equal steps, each open for `duty` of its length. */
        Points gate (int count, float duty);

        /** A sidechain-style pump per beat: drop to `low` on the beat, curve back up over `recover` of it. */
        Points pump (int beats, float low, float recover);

        Points ramp (float from, float to, float bend = 0.0f);

        /** Up and back down, `cycles` times. */
        Points triangle (int cycles, float low, float high);

        /** One or more smooth up-down cycles (bent segments approximate a sine). */
        Points wave (int cycles, float low, float high);

        /** `count` steps at random levels. */
        Points randomSteps (int count, juce::Random& random);

        /** Mirrors every level (y -> 1 - y); bends carry over unchanged. */
        Points inverted (Points points);
    }

    /** The patterns as they are saved: a ValueTree inside the plugin state, edited by the UI
        (with undo) and turned into a PatternSet for the audio thread.

        PATTERNS / PATTERN (index) / LANE (index) / PT (x, y, b) */
    namespace PatternTree
    {
        extern const juce::Identifier patterns, pattern, lane, point, index, x, y, bend;

        /** The eight factory patterns. */
        juce::ValueTree createDefault();

        /** The LANE tree for one pattern and lane, or an invalid tree. */
        juce::ValueTree findLane (const juce::ValueTree& patterns, int patternIndex, Lane lane);

        /** A lane's points, cleaned up: clamped, sorted, capped, and running from x = 0 to x = 1. */
        std::vector<CurvePoint> readLane (const juce::ValueTree& laneTree);

        /** Replaces a lane's points (undoable when an UndoManager is given). */
        void writeLane (juce::ValueTree laneTree, const std::vector<CurvePoint>& points, juce::UndoManager* undo);

        /** Fills dest from the tree. Missing patterns or lanes become flat curves. */
        void read (const juce::ValueTree& patterns, PatternSet& dest);

        /** Makes sure all 8 patterns and 24 lanes exist, filling any gap with a factory pattern. */
        void repair (juce::ValueTree& patterns);
    }

    /** Hands a value from one writer thread to the audio thread without locks: the writer fills a
        spare copy and publishes it; the reader always gets the newest complete copy. */
    template <typename T>
    class TripleBuffer
    {
    public:
        T& backBuffer() noexcept { return slots[(size_t) back]; }

        void publish() noexcept
        {
            back = middle.exchange (back | dirty, std::memory_order_acq_rel) & indexMask;
        }

        /** Audio thread. The returned value stays untouched until the next call. */
        const T& read() noexcept
        {
            if ((middle.load (std::memory_order_acquire) & dirty) != 0)
                front = middle.exchange (front, std::memory_order_acq_rel) & indexMask;
            return slots[(size_t) front];
        }

    private:
        static constexpr int dirty = 4;
        static constexpr int indexMask = 3;

        std::array<T, 3> slots {};
        std::atomic<int> middle { 1 };
        int front = 0;   // reader only
        int back = 2;    // writer only
    };
}
