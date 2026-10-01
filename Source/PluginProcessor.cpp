#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <cmath>

using namespace pulselock;

PulseLockProcessor::PulseLockProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PulseLockState", createParameterLayout()),
      params (apvts)
{
    ensurePatterns();
    apvts.state.addListener (this);
    publishPatterns();
}

PulseLockProcessor::~PulseLockProcessor()
{
    apvts.state.removeListener (this);
}

//==============================================================================
void PulseLockProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

void PulseLockProcessor::releaseResources() {}

void PulseLockProcessor::reset()
{
    engine.reset();
}

bool PulseLockProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

EngineParams PulseLockProcessor::readParameters() const noexcept
{
    // A host can hand over a NaN or infinite value; anything non-finite falls back to the default.
    const EngineParams d {};
    auto value  = [] (const std::atomic<float>* p, float fallback) { const float v = p->load(); return std::isfinite (v) ? v : fallback; };
    auto asInt  = [&value] (const std::atomic<float>* p, int fallback) { return (int) std::lround (value (p, (float) fallback)); };
    auto asBool = [&value] (const std::atomic<float>* p, bool fallback) { return value (p, fallback ? 1.0f : 0.0f) >= 0.5f; };

    EngineParams p;
    p.pattern     = juce::jlimit (0, numPatterns - 1, asInt (params.pattern, d.pattern));
    p.sync        = asInt (params.sync, d.sync);
    p.rateHz      = value (params.rate, d.rateHz);
    p.trigger     = (Trigger) juce::jlimit (0, 2, asInt (params.trigger, (int) d.trigger));
    p.switchMode  = (SwitchMode) juce::jlimit (0, 2, asInt (params.switchMode, (int) d.switchMode));
    p.keyStart    = juce::jlimit (0, 120, asInt (params.keyStart, d.keyStart));
    p.volumeOn    = asBool (params.volumeOn, d.volumeOn);
    p.volumeDepth = value (params.volumeDepth, d.volumeDepth);
    p.filterOn    = asBool (params.filterOn, d.filterOn);
    p.filterType  = (FilterType) juce::jlimit (0, 2, asInt (params.filterType, (int) d.filterType));
    p.filterLow   = value (params.filterLow, d.filterLow);
    p.filterHigh  = value (params.filterHigh, d.filterHigh);
    p.filterRes   = value (params.filterRes, d.filterRes);
    p.panOn       = asBool (params.panOn, d.panOn);
    p.panDepth    = value (params.panDepth, d.panDepth);
    p.smoothMs    = value (params.smooth, d.smoothMs);
    p.mix         = value (params.mix, d.mix);
    p.outGainDb   = value (params.outGain, d.outGainDb);
    p.bypass      = asBool (params.bypass, false);
    return p;
}

HostTiming PulseLockProcessor::readHostTiming() const noexcept
{
    HostTiming timing;
    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            // Ignore tempos and positions no real session has.
            if (const auto bpm = position->getBpm())
                if (std::isfinite (*bpm) && *bpm > 0.0 && *bpm <= 1000.0)
                    timing.bpm = *bpm;

            if (const auto ppq = position->getPpqPosition())
            {
                if (std::isfinite (*ppq) && std::abs (*ppq) < 1.0e12)
                {
                    timing.hasPpq = true;
                    timing.ppq = *ppq;
                }
            }

            if (const auto barStart = position->getPpqPositionOfLastBarStart())
            {
                if (std::isfinite (*barStart) && std::abs (*barStart) < 1.0e12)
                {
                    timing.hasBarStart = true;
                    timing.barStart = *barStart;
                }
            }

            if (const auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    timing.beatsPerBar = signature->numerator * 4.0 / signature->denominator;

            timing.playing = position->getIsPlaying();
        }
    }
    return timing;
}

void PulseLockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto parameters = readParameters();
    parameters.uiPatternRequest = uiPatternRequest.exchange (-1);

    // The main input bus only: a side-chain bus, if one is ever added, is not the right channel.
    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), getMainBusNumInputChannels(),
                    buffer.getNumSamples(), midi, parameters, readHostTiming(), patternBuffer.read());
}

void PulseLockProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Bypassed audio is the input, on both sides even for a mono input.
    if (getMainBusNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    engine.processBypassed (buffer.getNumSamples(), readHostTiming());
}

juce::AudioProcessorEditor* PulseLockProcessor::createEditor()
{
    return new PulseLockEditor (*this);
}

//==============================================================================
juce::ValueTree PulseLockProcessor::patternsTree() const
{
    return apvts.state.getChildWithName (PatternTree::patterns);
}

void PulseLockProcessor::ensurePatterns()
{
    auto tree = patternsTree();
    if (! tree.isValid())
        apvts.state.appendChild (PatternTree::createDefault(), nullptr);
    else
        PatternTree::repair (tree);
}

void PulseLockProcessor::publishPatterns()
{
    const juce::ScopedLock lock (patternWriteLock);
    PatternTree::read (patternsTree(), patternBuffer.backBuffer());
    patternBuffer.publish();
    ++patternRevision;
}

bool PulseLockProcessor::isPatternData (const juce::ValueTree& tree)
{
    return tree.hasType (PatternTree::patterns) || tree.hasType (PatternTree::pattern)
        || tree.hasType (PatternTree::lane) || tree.hasType (PatternTree::point);
}

void PulseLockProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (! batchingEdits && isPatternData (tree))
        publishPatterns();
}

void PulseLockProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (! batchingEdits && (isPatternData (parent) || isPatternData (child)))
        publishPatterns();
}

void PulseLockProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (! batchingEdits && (isPatternData (parent) || isPatternData (child)))
        publishPatterns();
}

void PulseLockProcessor::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (! batchingEdits && isPatternData (parent))
        publishPatterns();
}

void PulseLockProcessor::valueTreeRedirected (juce::ValueTree&)
{
    ensurePatterns();
    publishPatterns();
}

std::vector<CurvePoint> PulseLockProcessor::getLanePoints (int pattern, Lane lane) const
{
    return PatternTree::readLane (PatternTree::findLane (patternsTree(), pattern, lane));
}

void PulseLockProcessor::setLanePoints (int pattern, Lane lane, const std::vector<CurvePoint>& points,
                                        const juce::String& undoName)
{
    auto laneTree = PatternTree::findLane (patternsTree(), pattern, lane);
    if (! laneTree.isValid())
        return;

    if (undoName.isNotEmpty())
        patternUndo.beginNewTransaction (undoName);

    batchingEdits = true;
    PatternTree::writeLane (laneTree, points, undoName.isNotEmpty() ? &patternUndo : nullptr);
    batchingEdits = false;
    publishPatterns();
}

void PulseLockProcessor::setPatternPoints (int pattern, const std::array<std::vector<CurvePoint>, numLanes>& lanes,
                                           const juce::String& undoName)
{
    patternUndo.beginNewTransaction (undoName);
    batchingEdits = true;
    for (int l = 0; l < numLanes; ++l)
        PatternTree::writeLane (PatternTree::findLane (patternsTree(), pattern, (Lane) l), lanes[(size_t) l], &patternUndo);
    batchingEdits = false;
    publishPatterns();
}

void PulseLockProcessor::undoPatterns()
{
    batchingEdits = true;
    patternUndo.undo();
    batchingEdits = false;
    publishPatterns();
}

void PulseLockProcessor::redoPatterns()
{
    batchingEdits = true;
    patternUndo.redo();
    batchingEdits = false;
    publishPatterns();
}

void PulseLockProcessor::commitLaneEdit (int pattern, Lane lane, const std::vector<CurvePoint>& before,
                                         const std::vector<CurvePoint>& after, const juce::String& undoName)
{
    auto laneTree = PatternTree::findLane (patternsTree(), pattern, lane);
    if (! laneTree.isValid())
        return;

    // Put the lane back to where the gesture began without recording it, then record the whole
    // change as one step. The audio thread only ever sees the finished shape.
    batchingEdits = true;
    PatternTree::writeLane (laneTree, before, nullptr);
    patternUndo.beginNewTransaction (undoName);
    PatternTree::writeLane (laneTree, after, &patternUndo);
    batchingEdits = false;
    publishPatterns();
}

void PulseLockProcessor::resetPatternsToFactory()
{
    patternUndo.beginNewTransaction ("Reset patterns");
    const auto factory = PatternTree::createDefault();
    batchingEdits = true;
    for (int p = 0; p < numPatterns; ++p)
        for (int l = 0; l < numLanes; ++l)
            PatternTree::writeLane (PatternTree::findLane (patternsTree(), p, (Lane) l),
                                    PatternTree::readLane (PatternTree::findLane (factory, p, (Lane) l)), &patternUndo);
    batchingEdits = false;
    publishPatterns();
}

//==============================================================================
int PulseLockProcessor::getNumPresets() const
{
    return (int) factoryPresets().size();
}

juce::String PulseLockProcessor::getPresetName (int index) const
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? juce::String (presets[(size_t) index].name) : juce::String();
}

int PulseLockProcessor::getPresetIndex (const juce::String& name) const
{
    const auto& presets = factoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (name == presets[i].name)
            return (int) i;
    return -1;
}

void PulseLockProcessor::loadPreset (int index)
{
    const auto& presets = factoryPresets();
    if (juce::isPositiveAndBelow (index, (int) presets.size()))
        applyPreset (apvts, presets[(size_t) index]);
}

juce::String PulseLockProcessor::getCurrentPresetName() const
{
    return apvts.state.getProperty ("presetName", "Init").toString();
}

//==============================================================================
void PulseLockProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PulseLockProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));   // valueTreeRedirected repairs and republishes
            patternUndo.clearUndoHistory();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PulseLockProcessor();
}
