#pragma once
#include <JuceHeader.h>
#include "CaptureStore.h"
#include "EditModel.h"

class GainEditorProcessor : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::Timer
{
public:
    GainEditorProcessor();
    ~GainEditorProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Gain Editor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread API used by the editor.
    const EditState& getEdits() const noexcept { return edits; }

    /** Replaces the edits. If undoable, one undo step is recorded (name shows in menus). */
    void setEdits (const EditState& newState, bool undoable, const juce::String& actionName = {});

    /** For live dragging: change edits without an undo step; call commitDrag() at mouse-up. */
    void beginDrag();
    void commitDrag (const juce::String& actionName);

    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    CaptureStore& getCapture() noexcept { return capture; }
    const CaptureStore& getCapture() const noexcept { return capture; }

    void setCapturing (bool shouldCapture);
    bool isCapturing() const noexcept { return capturing.load(); }
    void clearCapture();

    void startAudition (int64_t from, int64_t to);
    void stopAudition();
    bool isAuditioning() const noexcept { return auditioning.load(); }

    int64_t getPlayPosition() const noexcept { return playPos.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }
    double getSampleRateForTimeline() const noexcept { return capture.getSampleRate(); }

    // ---- Peak threshold (dBFS), shown as a line on the waveform ----
    float getThresholdDb() const;
    void setThresholdDb (float db);

    // ---- Reduction for all peaks over the threshold (dB, <= 0) ----
    float getReductionDb() const;
    void setReductionDb (float db);

    // ---- Ableton transport through a virtual MIDI port called "Gain Editor" ----
    bool isAbletonSync() const;
    void setAbletonSync (bool on);
    bool hasMidiPort() const noexcept { return midiOut != nullptr; }
    /** Asks Ableton (through the GainEditor control surface script) to play from a timeline position. */
    void sendPlayFrom (int64_t timelinePos);
    void sendStop();
    /** Copies the GainEditor control surface script into Ableton's User Library. Returns true if it is there. */
    bool installRemoteScript (juce::String& whereOrError);

    // Editor UI state kept with the project.
    juce::ValueTree uiState { "UI" };

private:
    void timerCallback() override;
    void publishGainMap();
    juce::File legacyCaptureFile (const juce::String& id) const;

    CaptureStore capture;
    EditState edits, dragStartState;
    bool dragging = false;
    juce::UndoManager undoManager { 0, 200 };

    std::atomic<GainMap*> liveMap { nullptr };
    std::vector<std::pair<std::unique_ptr<GainMap>, juce::uint32>> retiredMaps;
    std::unique_ptr<GainMap> ownedMap;

    std::atomic<bool> capturing { false }, hostPlaying { false }, auditioning { false };
    std::atomic<int64_t> playPos { 0 }, auditionPos { 0 }, auditionEnd { 0 };
    std::vector<float> monoScratch;
    double currentSampleRate = 48000.0;

    // The recording is stored inside the Ableton project (FLAC), encoded once per change.
    std::shared_ptr<juce::MidiOutput> midiOut;
    void sendSysEx (const juce::uint8* data, int size);

    // last known link between samples and beats (from Ableton's play head)
    std::atomic<double> refPpq { 0.0 }, refBpm { 120.0 };
    std::atomic<int64_t> refSamples { 0 };

    juce::MemoryBlock encodedAudio;
    uint64_t encodedGeneration = std::numeric_limits<uint64_t>::max();
    juce::CriticalSection encodeLock;

    friend struct EditAction;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainEditorProcessor)
};
