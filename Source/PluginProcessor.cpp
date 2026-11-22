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
// One virtual MIDI port shared by every Gain Editor in the session.
static constexpr int dawPort = 47447;   // the GainEditor control surface listens here (127.0.0.1 only)

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
    liveMap.store (nullptr);
    liveSibMap.store (nullptr);
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
    sibScratch.assign ((size_t) juce::jmax (1, samplesPerBlock) * 2, 1.0f);
    for (auto& h : highs) h.reset();

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
    const auto* sibMap = liveSibMap.load (std::memory_order_acquire);
    const bool sibActive = sibMap != nullptr && ! sibMap->gains.empty();
    if (sibActive)
        for (auto& h : highs) h.setup (currentSampleRate, sibMap->hpFreq);

    // y = (x - (1 - gHighs) * highpass(x)) * gain : with gHighs = 1 the sound is untouched
    auto lowerHighs = [] (HighPass4& hp, float x, float gHighs) { const auto h = hp.process (x); return x - (1.0f - gHighs) * h; };

    // ---- Audition: play the captured take (with edits) from inside the plugin ----
    if (auditioning.load())
    {
        auto pos = auditionPos.load();
        const auto end = auditionEnd.load();
        const auto n = (int) juce::jlimit ((int64_t) 0, (int64_t) numSamples, end - pos);

        capture.read (pos, monoScratch.data(), n);
        for (int i = 0; i < n; ++i)
        {
            auto v = monoScratch[(size_t) i];
            if (sibActive) v = lowerHighs (highs[2], v, sibMap->gainAt (pos + i));
            monoScratch[(size_t) i] = v * (map != nullptr ? map->gainAt (pos + i) : 1.0f);
        }
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
            if (auto ppq = info->getPpqPosition())
            {
                refPpq = *ppq;
                refSamples = info->getTimeInSamples().orFallback (timelinePos);
            }
            if (auto bpm = info->getBpm())
                refBpm = *bpm;
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
    {
        monoScratch[(size_t) i] = map->gainAt (timelinePos + i);
        sibScratch[(size_t) i] = sibActive ? sibMap->gainAt (timelinePos + i) : 1.0f;
    }

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        auto& hp = highs[juce::jmin (ch, 1)];
        for (int i = 0; i < numSamples; ++i)
        {
            auto v = d[i];
            if (sibActive) v = lowerHighs (hp, v, sibScratch[(size_t) i]);   // the filter keeps running so it never clicks in
            d[i] = v * monoScratch[(size_t) i];
        }
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

    auto* rawSib = new GainMap (*EditOps::buildSibMap (edits, capture.getSampleRate()));
    liveSibMap.exchange (rawSib, std::memory_order_acq_rel);
    if (ownedSibMap != nullptr)
        retiredMaps.push_back ({ std::move (ownedSibMap), juce::Time::getMillisecondCounter() });
    ownedSibMap.reset (rawSib);
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
    }
    sendChangeMessage();
}

void GainEditorProcessor::clearCapture()
{
    capturing = false;
    stopAudition();
    suspendProcessing (true);
    capture.clear();
    capture.setSampleRate (currentSampleRate);
    suspendProcessing (false);

    auto s = edits;
    s.spots.clear();
    setEdits (s, true, "Clear Recording");
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

float GainEditorProcessor::getThresholdDb() const
{
    return (float) uiState.getProperty ("thresholdDb", -6.0f);
}

void GainEditorProcessor::setThresholdDb (float db)
{
    uiState.setProperty ("thresholdDb", juce::jlimit (-40.0f, 0.0f, db), nullptr);
    sendChangeMessage();
}

float GainEditorProcessor::getReductionDb() const
{
    return (float) uiState.getProperty ("reductionDb", 0.0f);
}

void GainEditorProcessor::setReductionDb (float db)
{
    uiState.setProperty ("reductionDb", juce::jlimit (-24.0f, 0.0f, db), nullptr);
}

bool GainEditorProcessor::isAbletonSync() const
{
    return (bool) uiState.getProperty ("abletonSync", true);
}

bool GainEditorProcessor::isDawConnected() const noexcept
{
    const auto t = lastPongMs.load();
    return t != 0 && juce::Time::getMillisecondCounter() - t < 2500;
}

void GainEditorProcessor::sendToDaw (const juce::String& text)
{
    if (link == nullptr)
    {
        link = std::make_unique<juce::DatagramSocket> (false);
        if (! link->bindToPort (0, "127.0.0.1"))
        {
            link.reset();
            return;
        }
    }
    const auto utf8 = text.toStdString();
    link->write ("127.0.0.1", dawPort, utf8.data(), (int) utf8.size());
}

void GainEditorProcessor::serviceLink()
{
    if (! isAbletonSync())
        return;

    if (! scriptChecked)
    {
        scriptChecked = true;   // keep the script in Ableton's User Library up to date
        juce::String where;
        installRemoteScript (where);
    }

    const auto now = juce::Time::getMillisecondCounter();
    if (link == nullptr || now - lastPingMs > 1000)
    {
        lastPingMs = now;
        sendToDaw ("GE PING");
    }
    if (link == nullptr)
        return;

    char buf[256];
    while (link->waitUntilReady (true, 0) == 1)
    {
        const auto n = link->read (buf, (int) sizeof (buf) - 1, false);
        if (n <= 0)
            break;
        buf[n] = 0;
        if (juce::String (buf).startsWith ("GE PONG"))
            lastPongMs = now;
    }
}

void GainEditorProcessor::setAbletonSync (bool on)
{
    uiState.setProperty ("abletonSync", on, nullptr);
    if (on)
    {
        juce::String where;
        installRemoteScript (where);
    }
    sendChangeMessage();
}

void GainEditorProcessor::sendPlayFrom (int64_t pos)
{
    const auto sr = capture.getSampleRate();
    const auto beats = juce::jmax (0.0, refPpq.load() + (double) (pos - refSamples.load()) / sr * refBpm.load() / 60.0);
    sendToDaw ("GE PLAY " + juce::String (beats, 4));
}

void GainEditorProcessor::sendStop()
{
    sendToDaw ("GE STOP");
}

static const char* remoteScriptSource = R"PY(# Gain Editor control surface for Ableton Live (v2).
# Lets the Gain Editor plugin start Ableton from a position and stop it.
# Installed automatically by the plugin. Select it in Settings > Link, Tempo & MIDI > Control Surface
# (Input and Output can stay None). It only listens on this computer (127.0.0.1).
import socket
from _Framework.ControlSurface import ControlSurface

PORT = 47447


def create_instance(c_instance):
    return GainEditor(c_instance)


class GainEditor(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        self._sock = None
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("127.0.0.1", PORT))
            s.setblocking(False)
            self._sock = s
            self.log_message("Gain Editor control surface ready on port %d" % PORT)
        except Exception as e:
            self.log_message("Gain Editor control surface could not open port: %s" % e)

    def update_display(self):
        ControlSurface.update_display(self)
        self._poll()

    def _poll(self):
        if self._sock is None:
            return
        for _ in range(32):
            try:
                data, addr = self._sock.recvfrom(256)
            except Exception:
                return
            try:
                self._handle(data.decode("utf-8", "ignore").split(), addr)
            except Exception as e:
                self.log_message("Gain Editor: %s" % e)

    def _handle(self, words, addr):
        if len(words) < 2 or words[0] != "GE":
            return
        cmd = words[1]
        song = self.song()
        if cmd == "PING":
            self._sock.sendto(b"GE PONG", addr)
        elif cmd == "PLAY" and len(words) >= 3:
            song.current_song_time = max(0.0, float(words[2]))
            if not song.is_playing:
                song.continue_playing()
        elif cmd == "STOP":
            song.stop_playing()

    def disconnect(self):
        if self._sock is not None:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None
        ControlSurface.disconnect(self)
)PY";

bool GainEditorProcessor::installRemoteScript (juce::String& result)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("Ableton").getChildFile ("User Library").getChildFile ("Remote Scripts").getChildFile ("GainEditor");
    auto file = dir.getChildFile ("__init__.py");
    if (file.existsAsFile() && file.loadFileAsString() == juce::String (remoteScriptSource))
    {
        result = dir.getFullPathName();
        return true;
    }
    if (! dir.createDirectory() || ! file.replaceWithText (remoteScriptSource))
    {
        result = "Could not write " + file.getFullPathName();
        return false;
    }
    result = dir.getFullPathName();
    return true;
}

//==============================================================================
void GainEditorProcessor::timerCallback()
{
    serviceLink();

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

juce::File GainEditorProcessor::legacyCaptureFile (const juce::String& id) const
{
    // Version 1.0 kept recordings as separate files. Still read them so old projects open.
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("GainEditor").getChildFile ("Captures").getChildFile (id + ".wav");
}

//==============================================================================
void GainEditorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("GAINEDITOR");
    root.setProperty ("version", 2, nullptr);
    root.appendChild (edits.toValueTree(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);

    if (capture.hasData())
    {
        const juce::ScopedLock sl (encodeLock);
        const auto gen = capture.getGeneration();
        if (gen != encodedGeneration || encodedAudio.isEmpty())
        {
            capture.encodeToFlac (encodedAudio);
            encodedGeneration = gen;
        }

        root.setProperty ("captureStart", (juce::int64) capture.getWrittenStart(), nullptr);
        root.setProperty ("captureRate", capture.getSampleRate(), nullptr);
        root.setProperty ("audio", juce::var (encodedAudio), nullptr);
    }

    juce::MemoryOutputStream out (destData, false);
    out.writeInt (0x47456432); // "GEd2"
    root.writeToStream (out);
}

void GainEditorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ValueTree root;

    if (sizeInBytes > 4 && juce::ByteOrder::littleEndianInt (data) == 0x47456432)
    {
        root = juce::ValueTree::readFromData (static_cast<const char*> (data) + 4, (size_t) sizeInBytes - 4);
    }
    else if (auto xml = getXmlFromBinary (data, sizeInBytes))   // version 1 projects
    {
        root = juce::ValueTree::fromXml (*xml);
    }

    if (! root.hasType ("GAINEDITOR"))
        return;

    edits = EditState::fromValueTree (root.getChildWithName ("EDITS"));
    if (auto ui = root.getChildWithName ("UI"); ui.isValid())
        uiState = ui.createCopy();

    const auto start = (int64_t) (juce::int64) root.getProperty ("captureStart", 0);

    suspendProcessing (true);
    capturing = false;
    auditioning = false;
    capture.clear();

    bool loaded = false;
    if (auto* mb = root.getProperty ("audio").getBinaryData())
    {
        loaded = capture.decodeFromFlac (*mb, start);
        if (loaded)
        {
            const juce::ScopedLock sl (encodeLock);
            encodedAudio = *mb;
            encodedGeneration = capture.getGeneration();
        }
    }

    if (! loaded)
    {
        const auto id = root.getProperty ("captureId").toString();
        if (id.isNotEmpty() && legacyCaptureFile (id).existsAsFile())
            loaded = capture.loadFromFile (legacyCaptureFile (id), start);
    }

    if (! capture.hasData())
        capture.setSampleRate (currentSampleRate);
    suspendProcessing (false);

    if (isAbletonSync())
    {
        juce::String where;
        installRemoteScript (where);
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
