// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace oto;

OtoProcessor::OtoProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OtoSamplerState", createLayout())
{
    pOscCount = apvts.getRawParameterValue (ids::oscCount);
    for (int i = 0; i < kMaxOsc; ++i)
        units[(size_t) i] = std::make_unique<OscUnit> (apvts, i, pool);
    startTimerHz (20);
}

OtoProcessor::~OtoProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

bool OtoProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (! l.getMainInputChannelSet().isDisabled())
        return false;

    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void OtoProcessor::prepareToPlay (double sr, int)
{
    for (auto& u : units) u->prepare (sr);
}

void OtoProcessor::timerCallback()
{
    for (auto& u : units) u->timerTick();
}

// ======================= OSC management =======================
void OtoProcessor::setOscCount (int n)
{
    n = juce::jlimit (1, kMaxOsc, n);
    auto* p = apvts.getParameter (ids::oscCount);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) n));
    p->endChangeGesture();
}

OtoProcessor::OscClip OtoProcessor::makeClip (int index) const
{
    OscClip c;
    c.valid = true;
    c.norm.reserve (kOscParamIds.size());
    for (auto* id : kOscParamIds)
        c.norm.push_back (apvts.getParameter (oscParamId (id, index))->getValue());
    c.sample = units[(size_t) index]->getSampleRef();
    return c;
}

void OtoProcessor::applyClip (int index, const OscClip& c)
{
    if (! c.valid || c.norm.size() != kOscParamIds.size()) return;
    for (size_t k = 0; k < kOscParamIds.size(); ++k)
    {
        auto* p = apvts.getParameter (oscParamId (kOscParamIds[k], index));
        p->beginChangeGesture();
        p->setValueNotifyingHost (c.norm[k]);
        p->endChangeGesture();
    }
    if (c.sample.source != nullptr) units[(size_t) index]->loadSampleShared (c.sample);
    else                            units[(size_t) index]->clearSample();
    units[(size_t) index]->markFilterDirty();
}

void OtoProcessor::resetOsc (int index)
{
    for (auto* id : kOscParamIds)
    {
        auto* p = apvts.getParameter (oscParamId (id, index));
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
    units[(size_t) index]->clearSample();
}

int OtoProcessor::addOsc (int copyFrom)
{
    const int n = getOscCount();
    if (n >= kMaxOsc) return -1;
    if (copyFrom >= 0 && copyFrom < n) applyClip (n, makeClip (copyFrom));
    else                               resetOsc (n);
    setOscCount (n + 1);
    return n;
}

bool OtoProcessor::removeOsc (int index)
{
    const int n = getOscCount();
    if (n <= 1 || index < 0 || index >= n) return false;
    for (int i = index; i < n - 1; ++i)
        applyClip (i, makeClip (i + 1));
    setOscCount (n - 1);
    resetOsc (n - 1);
    return true;
}

void OtoProcessor::copyOsc (int index)
{
    if (index >= 0 && index < kMaxOsc) clip = makeClip (index);
}

bool OtoProcessor::pasteOsc (int index)
{
    if (! clip.valid || index < 0 || index >= getOscCount()) return false;
    applyClip (index, clip);
    return true;
}

// ======================= audio =======================
void OtoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    const int count = getOscCount();
    for (int o = 0; o < kMaxOsc; ++o) units[(size_t) o]->setEnabled (o < count);
    for (int o = 0; o < count; ++o) units[(size_t) o]->beginBlock();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);   // on-screen keyboard -> MIDI

    auto renderAll = [&] (int start, int num)
    {
        for (int o = 0; o < count; ++o) units[(size_t) o]->render (buffer, start, num);
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, numSamples, meta.samplePosition);
        if (t > pos) { renderAll (pos, t - pos); pos = t; }
        const auto msg = meta.getMessage();
        for (int o = 0; o < count; ++o) units[(size_t) o]->handleMidi (msg);
    }
    renderAll (pos, numSamples - pos);

    for (int o = 0; o < count; ++o) units[(size_t) o]->endBlock();
}

// ======================= state =======================
void OtoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    for (int o = 0; o < kMaxOsc; ++o)
    {
        const auto ref = units[(size_t) o]->getSampleRef();
        state.setProperty (juce::Identifier (oscParamId ("samplePath", o)), ref.path, nullptr);
        state.setProperty (juce::Identifier (oscParamId ("sampleName", o)), ref.name, nullptr);
        if (ref.raw != nullptr && ref.raw->getSize() <= 12u * 1024u * 1024u)   // embed the file so projects are portable
            state.setProperty (juce::Identifier (oscParamId ("sampleData", o)), ref.raw->toBase64Encoding(), nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void OtoProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);

    struct Saved { juce::String b64, path, name; };
    std::array<Saved, kMaxOsc> saved;
    for (int o = 0; o < kMaxOsc; ++o)
    {
        const juce::Identifier kData (oscParamId ("sampleData", o)), kPath (oscParamId ("samplePath", o)),
                               kName (oscParamId ("sampleName", o));
        saved[(size_t) o] = { vt.getProperty (kData).toString(), vt.getProperty (kPath).toString(), vt.getProperty (kName).toString() };
        vt.removeProperty (kData, nullptr);
        vt.removeProperty (kPath, nullptr);
        vt.removeProperty (kName, nullptr);
    }
    apvts.replaceState (vt);

    for (int o = 0; o < kMaxOsc; ++o)
    {
        auto& u = *units[(size_t) o];
        const auto& s = saved[(size_t) o];
        juce::MemoryBlock mb;
        if (s.b64.isNotEmpty() && mb.fromBase64Encoding (s.b64))
            u.loadSampleFromMemory (std::make_shared<juce::MemoryBlock> (std::move (mb)), s.path, s.name);
        else if (s.path.isNotEmpty() && juce::File (s.path).existsAsFile())
            u.loadSampleFromFile (juce::File (s.path));
        else
            u.clearSample();
        u.markFilterDirty();
    }
}

juce::AudioProcessorEditor* OtoProcessor::createEditor() { return new OtoEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OtoProcessor(); }
