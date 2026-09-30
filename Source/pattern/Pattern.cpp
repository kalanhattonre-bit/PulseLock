#include "Pattern.h"

#include <algorithm>
#include <cmath>

namespace pulselock
{
    float bendShape (float u, float bend) noexcept
    {
        if (std::abs (bend) < 1.0e-3f)
            return u;

        const float k = bend * 8.0f;
        return (std::exp (k * u) - 1.0f) / (std::exp (k) - 1.0f);
    }

    float Curve::valueAt (double position) const noexcept
    {
        if (count <= 0)
            return 0.0f;
        if (count == 1)
            return points[0].y;

        const float x = (float) juce::jlimit (0.0, 1.0, position);

        // Last point with points[i].x <= x. With two points on one x (a vertical edge) this lands on
        // the second, so the value at the edge is the value after it.
        int lo = 0, hi = count;
        while (lo < hi)
        {
            const int mid = (lo + hi) / 2;
            if (points[(size_t) mid].x <= x)
                lo = mid + 1;
            else
                hi = mid;
        }

        const int i = lo - 1;
        if (i < 0)
            return points[0].y;
        if (i >= count - 1)
            return points[(size_t) (count - 1)].y;

        const auto& a = points[(size_t) i];
        const auto& b = points[(size_t) (i + 1)];
        const float span = b.x - a.x;
        if (span <= 1.0e-6f)
            return b.y;

        return a.y + (b.y - a.y) * bendShape ((x - a.x) / span, a.bend);
    }

    //==============================================================================
    namespace Shapes
    {
        Points flat (float level) { return { { 0.0f, level, 0.0f }, { 1.0f, level, 0.0f } }; }

        Points steps (const std::vector<float>& levels, float duty, float floor)
        {
            Points p;
            const float n = (float) levels.size();
            for (size_t s = 0; s < levels.size(); ++s)
            {
                const float x0 = (float) s / n;
                const float x1 = ((float) s + duty) / n;
                const float x2 = ((float) s + 1.0f) / n;
                p.push_back ({ x0, levels[s], 0.0f });
                if (duty < 1.0f)
                {
                    p.push_back ({ x1, levels[s], 0.0f });
                    p.push_back ({ x1, floor, 0.0f });
                    p.push_back ({ x2, floor, 0.0f });
                }
                else
                {
                    p.push_back ({ x2, levels[s], 0.0f });
                }
            }
            return p;
        }

        Points gate (int count, float duty)
        {
            return steps (std::vector<float> ((size_t) juce::jlimit (1, 32, count), 1.0f), duty);
        }

        Points pump (int beats, float low, float recover)
        {
            Points p;
            beats = juce::jlimit (1, 32, beats);
            for (int i = 0; i < beats; ++i)
            {
                const float x0 = (float) i / (float) beats;
                const float width = 1.0f / (float) beats;
                p.push_back ({ x0, low, -0.55f });
                p.push_back ({ x0 + width * recover, 1.0f, 0.0f });
                p.push_back ({ x0 + width, 1.0f, 0.0f });
            }
            return p;
        }

        Points ramp (float from, float to, float bendAmount) { return { { 0.0f, from, bendAmount }, { 1.0f, to, 0.0f } }; }

        Points triangle (int cycles, float low, float high)
        {
            Points p;
            cycles = juce::jlimit (1, 32, cycles);
            for (int c = 0; c < cycles; ++c)
            {
                const float x0 = (float) c / (float) cycles;
                p.push_back ({ x0, low, 0.0f });
                p.push_back ({ x0 + 0.5f / (float) cycles, high, 0.0f });
            }
            p.push_back ({ 1.0f, low, 0.0f });
            return p;
        }

        Points wave (int cycles, float low, float high)
        {
            Points p;
            cycles = juce::jlimit (1, 16, cycles);
            for (int c = 0; c < cycles; ++c)
            {
                const float x0 = (float) c / (float) cycles;
                const float w = 1.0f / (float) cycles;
                p.push_back ({ x0, 0.5f * (low + high), -0.35f });
                p.push_back ({ x0 + w * 0.25f, high, 0.35f });
                p.push_back ({ x0 + w * 0.5f, 0.5f * (low + high), -0.35f });
                p.push_back ({ x0 + w * 0.75f, low, 0.35f });
            }
            p.push_back ({ 1.0f, 0.5f * (low + high), 0.0f });
            return p;
        }

        Points randomSteps (int count, juce::Random& random)
        {
            std::vector<float> levels;
            for (int i = 0; i < juce::jlimit (1, 32, count); ++i)
                levels.push_back (0.1f + 0.9f * random.nextFloat());
            return steps (levels, 1.0f);
        }

        Points inverted (Points points)
        {
            // 1 - (y0 + (y1 - y0) * shape) = (1 - y0) + ((1 - y1) - (1 - y0)) * shape: the bend carries over unchanged.
            for (auto& p : points)
                p.y = 1.0f - p.y;
            return points;
        }
    }

    //==============================================================================
    namespace PatternTree
    {
        const juce::Identifier patterns ("PATTERNS"), pattern ("PATTERN"), lane ("LANE"), point ("PT"),
                               index ("index"), x ("x"), y ("y"), bend ("b");

        namespace
        {
            using namespace Shapes;

            juce::ValueTree makeLane (int laneIndex, const Points& pts)
            {
                juce::ValueTree laneTree (lane);
                laneTree.setProperty (index, laneIndex, nullptr);
                for (const auto& pt : pts)
                {
                    juce::ValueTree node (point);
                    node.setProperty (x, pt.x, nullptr);
                    node.setProperty (y, pt.y, nullptr);
                    node.setProperty (bend, pt.bend, nullptr);
                    laneTree.appendChild (node, nullptr);
                }
                return laneTree;
            }

            /** Factory pattern i: its volume, filter and pan curves. */
            std::array<Points, numLanes> factoryPattern (int i)
            {
                const std::vector<float> all16 (16, 1.0f);
                const std::vector<float> threeThreeTwo { 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
                const std::vector<float> randomLevels { 0.9f, 0.35f, 0.7f, 0.15f, 1.0f, 0.5f, 0.25f, 0.8f,
                                                        0.4f, 0.95f, 0.2f, 0.6f, 0.75f, 0.1f, 0.55f, 0.85f };
                switch (i)
                {
                    case 0:   // 1: 16th gate
                        return { steps (all16, 0.55f), wave (1, 0.25f, 0.85f), wave (1, 0.2f, 0.8f) };
                    case 1:   // 2: pump on every beat
                        return { pump (4, 0.08f, 0.7f), ramp (1.0f, 0.35f), flat (0.5f) };
                    case 2:   // 3: 3-3-2 gate
                        return { steps (threeThreeTwo, 0.9f), steps (threeThreeTwo, 1.0f, 0.3f), steps ({ 0.15f, 0.85f, 0.15f, 0.85f, 0.15f, 0.85f, 0.15f, 0.85f }, 1.0f) };
                    case 3:   // 4: 8th chop
                        return { steps (std::vector<float> (8, 1.0f), 0.7f), wave (2, 0.3f, 0.9f), flat (0.5f) };
                    case 4:   // 5: riser
                        return { ramp (0.1f, 1.0f, 0.4f), ramp (0.0f, 1.0f, 0.3f), flat (0.5f) };
                    case 5:   // 6: random steps
                        return { steps (randomLevels, 1.0f), steps (randomLevels, 1.0f), steps ({ 0.2f, 0.8f, 0.5f, 0.1f, 0.9f, 0.35f, 0.65f, 0.5f }, 1.0f) };
                    case 6:   // 7: straight first half, 32nd stutter in the second
                    {
                        Points vol { { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f } };
                        for (int s = 0; s < 16; ++s)
                        {
                            const float x0 = 0.5f + (float) s / 32.0f;
                            vol.push_back ({ x0, 1.0f, 0.0f });
                            vol.push_back ({ x0 + 0.5f / 32.0f, 1.0f, 0.0f });
                            vol.push_back ({ x0 + 0.5f / 32.0f, 0.0f, 0.0f });
                            vol.push_back ({ x0 + 1.0f / 32.0f, 0.0f, 0.0f });
                        }
                        return { vol, ramp (1.0f, 0.4f), flat (0.5f) };
                    }
                    default:  // 8: open (no movement)
                        return { flat (1.0f), flat (1.0f), flat (0.5f) };
                }
            }

            juce::ValueTree makePattern (int i)
            {
                juce::ValueTree patternTree (pattern);
                patternTree.setProperty (index, i, nullptr);
                const auto lanes = factoryPattern (i);
                for (int l = 0; l < numLanes; ++l)
                    patternTree.appendChild (makeLane (l, lanes[(size_t) l]), nullptr);
                return patternTree;
            }

            juce::ValueTree findChild (const juce::ValueTree& parent, const juce::Identifier& type, int wantedIndex)
            {
                for (const auto& child : parent)
                    if (child.hasType (type) && (int) child.getProperty (index, -1) == wantedIndex)
                        return child;
                return {};
            }
        }

        juce::ValueTree createDefault()
        {
            juce::ValueTree tree (patterns);
            for (int i = 0; i < numPatterns; ++i)
                tree.appendChild (makePattern (i), nullptr);
            return tree;
        }

        juce::ValueTree findLane (const juce::ValueTree& patternsTree, int patternIndex, Lane laneKind)
        {
            const auto patternTree = findChild (patternsTree, pattern, patternIndex);
            return patternTree.isValid() ? findChild (patternTree, lane, (int) laneKind) : juce::ValueTree();
        }

        std::vector<CurvePoint> readLane (const juce::ValueTree& laneTree)
        {
            std::vector<CurvePoint> pts;
            for (const auto& node : laneTree)
            {
                if (! node.hasType (point))
                    continue;

                auto finite = [] (const juce::var& v, float fallback)
                {
                    const float f = (float) (double) v;
                    return std::isfinite (f) ? f : fallback;
                };

                pts.push_back ({ juce::jlimit (0.0f, 1.0f, finite (node.getProperty (x), 0.0f)),
                                 juce::jlimit (0.0f, 1.0f, finite (node.getProperty (y), 0.0f)),
                                 juce::jlimit (-1.0f, 1.0f, finite (node.getProperty (bend), 0.0f)) });
            }

            std::stable_sort (pts.begin(), pts.end(), [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

            if (pts.empty())
                return { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };

            // The curve always spans the whole pattern.
            if (pts.front().x > 0.0f)
                pts.insert (pts.begin(), CurvePoint { 0.0f, pts.front().y, 0.0f });
            if (pts.back().x < 1.0f)
                pts.push_back (CurvePoint { 1.0f, pts.back().y, 0.0f });

            if ((int) pts.size() > Curve::maxPoints)
            {
                const auto last = pts.back();
                pts.resize ((size_t) Curve::maxPoints - 1);
                pts.push_back (last);
            }
            return pts;
        }

        void writeLane (juce::ValueTree laneTree, const std::vector<CurvePoint>& pts, juce::UndoManager* undo)
        {
            if (! laneTree.isValid())
                return;

            laneTree.removeAllChildren (undo);
            for (const auto& pt : pts)
            {
                juce::ValueTree node (point);
                node.setProperty (x, pt.x, nullptr);
                node.setProperty (y, pt.y, nullptr);
                node.setProperty (bend, pt.bend, nullptr);
                laneTree.appendChild (node, undo);
            }
        }

        void read (const juce::ValueTree& patternsTree, PatternSet& dest)
        {
            for (int p = 0; p < numPatterns; ++p)
            {
                for (int l = 0; l < numLanes; ++l)
                {
                    const auto pts = readLane (findLane (patternsTree, p, (Lane) l));
                    auto& curve = dest.curves[(size_t) p][(size_t) l];
                    curve.count = (int) std::min (pts.size(), (size_t) Curve::maxPoints);
                    for (int i = 0; i < curve.count; ++i)
                        curve.points[(size_t) i] = pts[(size_t) i];
                }
            }
        }

        void repair (juce::ValueTree& patternsTree)
        {
            for (int p = 0; p < numPatterns; ++p)
            {
                auto patternTree = findChild (patternsTree, pattern, p);
                if (! patternTree.isValid())
                {
                    patternsTree.appendChild (makePattern (p), nullptr);
                    continue;
                }

                const auto lanes = factoryPattern (p);
                for (int l = 0; l < numLanes; ++l)
                    if (! findChild (patternTree, lane, l).isValid())
                        patternTree.appendChild (makeLane (l, lanes[(size_t) l]), nullptr);
            }
        }
    }
}
