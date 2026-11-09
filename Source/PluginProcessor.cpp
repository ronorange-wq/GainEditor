#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Analysis.h"

//==============================================================================
struct EditAction : public juce::UndoableAction
{
    EditAction (GainEditorProcessor& p, EditState before, EditState after)
        : proc (p), oldState (std::move (before)), newState (std::move (after)) {}

    bool perform() override { proc.setEdits (newState, false); return true; }
    bool undo() override    { proc.setEdits (oldState, false); return true; }
    int getSizeInUnits() override { return 1; }

    GainEditorProcessor& proc;
    EditState oldState, newState;
};

//==============================================================================
GainEditorProcessor::GainEditorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    publishGainMap();
    startTimerHz (20);
}

GainEditorProcessor::~GainEditorProcessor()
{
    stopTimer();
    saver.removeAllJobs (false, 10000);
    liveMap.store (nullptr);
}

bool GainEditorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void GainEditorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock) * 2, 0.0f);

    if (! capture.hasData())
        capture.setSampleRate (sampleRate);

    publishGainMap();
}

//==============================================================================
void GainEditorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    const auto numCh = getTotalNumOutputChannels();

    for (auto ch = getTotalNumInputChannels(); ch < numCh; ++ch)
        buffer.clear (ch, 0, numSamples);

    if ((int) monoScratch.size() < numSamples)
        return; // host broke its promise about block size; pass audio through untouched

    const auto* map = liveMap.load (std::memory_order_acquire);

    // ---- Audition: play the captured take (with edits) from inside the plugin ----
    if (auditioning.load())
    {
        auto pos = auditionPos.load();
        const auto end = auditionEnd.load();
        const auto n = (int) juce::jlimit ((int64_t) 0, (int64_t) numSamples, end - pos);

        capture.read (pos, monoScratch.data(), n);
        for (int i = 0; i < n; ++i)
            monoScratch[(size_t) i] *= map != nullptr ? map->gainAt (pos + i) : 1.0f;
        std::fill (monoScratch.begin() + n, monoScratch.begin() + numSamples, 0.0f);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.copyFrom (ch, 0, monoScratch.data(), numSamples);

        pos += n;
        auditionPos = pos;
        playPos = pos;
        if (pos >= end)
            auditioning = false;
        return;
    }

    // ---- Normal: follow the host timeline ----
    bool playing = false;
    int64_t timelinePos = 0;

    if (auto* ph = getPlayHead())
    {
        if (auto info = ph->getPosition())
        {
            playing = info->getIsPlaying() || info->getIsRecording();
            if (auto t = info->getTimeInSamples())
                timelinePos = *t;
            else if (auto s = info->getTimeInSeconds())
                timelinePos = (int64_t) std::llround (*s * currentSampleRate);
            else
                playing = false;
        }
    }

    hostPlaying = playing;
    if (playing)
        playPos = timelinePos;

    if (! playing)
        return; // stopped: audio passes through unchanged

    if (capturing.load())
    {
        const auto inCh = juce::jmax (1, getTotalNumInputChannels());
        const auto scale = 1.0f / (float) inCh;
        for (int i = 0; i < numSamples; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < inCh; ++ch)
                s += buffer.getSample (ch, i);
            monoScratch[(size_t) i] = s * scale;
        }
        capture.write (timelinePos, monoScratch.data(), numSamples);
    }

    if (map == nullptr)
        return;

    for (int i = 0; i < numSamples; ++i)
        monoScratch[(size_t) i] = map->gainAt (timelinePos + i);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] *= monoScratch[(size_t) i];
    }
}

//==============================================================================
void GainEditorProcessor::publishGainMap()
{
    auto fresh = EditOps::buildGainMap (edits, capture.getSampleRate());
    auto* raw = new GainMap (*fresh);
    [[maybe_unused]] auto* old = liveMap.exchange (raw, std::memory_order_acq_rel);

    // The audio thread may still be reading the old map for a moment: free it later.
    if (ownedMap != nullptr)
    {
        jassert (ownedMap.get() == old);
        retiredMaps.push_back ({ std::move (ownedMap), juce::Time::getMillisecondCounter() });
    }
    ownedMap.reset (raw);
}

void GainEditorProcessor::setEdits (const EditState& newState, bool undoable, const juce::String& actionName)
{
    if (undoable && newState != edits)
    {
        undoManager.beginNewTransaction (actionName);
        undoManager.perform (new EditAction (*this, edits, newState));
        return; // perform() calls back into setEdits(..., false)
    }

    edits = newState;
    publishGainMap();
    sendChangeMessage();
}

void GainEditorProcessor::beginDrag()
{
    if (! dragging)
    {
        dragging = true;
        dragStartState = edits;
    }
}

void GainEditorProcessor::commitDrag (const juce::String& actionName)
{
    if (! dragging)
        return;
    dragging = false;

    if (dragStartState != edits)
    {
        undoManager.beginNewTransaction (actionName);
        undoManager.perform (new EditAction (*this, dragStartState, edits));
    }
}

//==============================================================================
void GainEditorProcessor::setCapturing (bool shouldCapture)
{
    if (shouldCapture == capturing.load())
        return;

    if (shouldCapture)
    {
        if (! capture.hasData())
            capture.setSampleRate (currentSampleRate);

        const auto pos = playPos.load();
        const auto sr = capture.getSampleRate();
        capture.ensureAllocated (pos - (int64_t) sr, pos + (int64_t) (sr * 30.0));
        capturing = true;
    }
    else
    {
        capturing = false;
        captureDirty = true;
        saveCaptureAsync();
        redetectBlobs (edits.blobResolution);
    }
    sendChangeMessage();
}

void GainEditorProcessor::clearCapture()
{
    capturing = false;
    stopAudition();
    saver.removeAllJobs (false, 10000);
    suspendProcessing (true);
    capture.clear();
    capture.setSampleRate (currentSampleRate);
    suspendProcessing (false);

    auto s = edits;
    s.blobs.clear();
    setEdits (s, true, "Clear Recording");
    captureId = {};
    sendChangeMessage();
}

void GainEditorProcessor::startAudition (int64_t from, int64_t to)
{
    if (to <= from)
        return;
    auditionPos = from;
    auditionEnd = to;
    auditioning = true;
}

void GainEditorProcessor::stopAudition()
{
    auditioning = false;
}

void GainEditorProcessor::redetectBlobs (float resolution, int64_t a, int64_t b)
{
    auto s = edits;
    if (b <= a)
        s.blobResolution = resolution;
    Analysis::redetect (s, capture, resolution, a, b);
    setEdits (s, true, "Blob Resolution");
}

//==============================================================================
void GainEditorProcessor::timerCallback()
{
    // Keep memory ready ahead of the play position while capturing.
    if (capturing.load())
    {
        const auto pos = playPos.load();
        const auto sr = capture.getSampleRate();
        capture.ensureAllocated (pos - (int64_t) sr, pos + (int64_t) (sr * 30.0));
    }

    // Free gain maps the audio thread can no longer be using.
    const auto now = juce::Time::getMillisecondCounter();
    retiredMaps.erase (std::remove_if (retiredMaps.begin(), retiredMaps.end(),
                                       [now] (auto& m) { return now - m.second > 2000; }),
                       retiredMaps.end());
}

juce::File GainEditorProcessor::captureFile() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("GainEditor").getChildFile ("Captures").getChildFile (captureId + ".wav");
}

void GainEditorProcessor::saveCaptureAsync()
{
    if (! capture.hasData())
        return;

    if (captureId.isEmpty())
        captureId = juce::Uuid().toString();

    const auto file = captureFile();
    captureDirty = false;
    saver.addJob ([this, file] { capture.saveToFile (file); });
}

//==============================================================================
void GainEditorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (captureDirty)
    {
        // Make sure the recording is on disk before the host stores the project.
        if (captureId.isEmpty())
            captureId = juce::Uuid().toString();
        capture.saveToFile (captureFile());
        captureDirty = false;
    }

    juce::ValueTree root ("GAINEDITOR");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("captureId", captureId, nullptr);
    root.setProperty ("captureStart", (juce::int64) (capture.hasData() ? capture.getWrittenStart() : 0), nullptr);
    root.setProperty ("captureRate", capture.getSampleRate(), nullptr);
    root.appendChild (edits.toValueTree(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void GainEditorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("GAINEDITOR"))
        return;

    edits = EditState::fromValueTree (root.getChildWithName ("EDITS"));
    if (auto ui = root.getChildWithName ("UI"); ui.isValid())
        uiState = ui.createCopy();

    captureId = root.getProperty ("captureId").toString();
    const auto start = (int64_t) (juce::int64) root.getProperty ("captureStart", 0);

    if (captureId.isNotEmpty())
    {
        auto f = captureFile();
        if (f.existsAsFile())
        {
            saver.removeAllJobs (false, 10000);
            suspendProcessing (true);
            capture.clear();
            capture.loadFromFile (f, start);
            suspendProcessing (false);
        }
    }

    undoManager.clearUndoHistory();
    publishGainMap();
    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* GainEditorProcessor::createEditor()
{
    return new GainEditorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GainEditorProcessor();
}
