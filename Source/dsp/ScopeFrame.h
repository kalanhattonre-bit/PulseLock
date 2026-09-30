#pragma once

#include "../Parameters.h"

#include <array>

namespace pulselock
{
    /** One display snapshot, built on the audio thread about 30 times a second. */
    struct ScopeFrame
    {
        float phase = 0.0f;                     // position through the pattern, 0..1
        int pattern = 0;                        // the pattern playing
        int pendingPattern = -1;                // a pattern waiting for the next beat or bar, or -1
        std::array<float, numLanes> values {};  // each lane's value right now (after Smooth)
        bool moving = false;                    // the playhead is advancing
        std::array<juce::uint64, 2> heldNotes {};

        bool isHeld (int note) const noexcept
        {
            return note >= 0 && note < 128 && ((heldNotes[(size_t) (note >> 6)] >> (note & 63)) & 1u) != 0;
        }
    };

    /** Lock-free single-producer, single-consumer hand-off from the audio thread to the UI. */
    class ScopeFifo
    {
    public:
        void push (const ScopeFrame& frame) noexcept
        {
            const auto scope = fifo.write (1);
            if (scope.blockSize1 > 0)
                frames[(size_t) scope.startIndex1] = frame;
        }

        bool pullLatest (ScopeFrame& dest) noexcept
        {
            bool got = false;
            while (fifo.getNumReady() > 0)
            {
                const auto scope = fifo.read (1);
                if (scope.blockSize1 > 0)
                {
                    dest = frames[(size_t) scope.startIndex1];
                    got = true;
                }
            }
            return got;
        }

    private:
        static constexpr int capacity = 8;
        juce::AbstractFifo fifo { capacity };
        std::array<ScopeFrame, capacity> frames {};
    };
}
