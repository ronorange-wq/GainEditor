#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>

/**
    Mono copy of the audio that passed through the plugin, indexed by the host's
    timeline position in samples (sample 0 = start of the song).

    Memory is held in fixed-size chunks. Chunks are allocated on the message thread
    (see ensureAllocated) a little ahead of the play position, so the audio thread
    never allocates; it only writes into chunks that already exist.
*/
class CaptureStore
{
public:
    static constexpr int chunkBits = 18;                       // 262144 samples per chunk
    static constexpr int64_t chunkSize = (int64_t) 1 << chunkBits;
    static constexpr int maxChunks = 8192;                     // ~ 12 hours at 48 kHz, ~ 3 h at 192 kHz

    CaptureStore();
    ~CaptureStore();

    // ---- message thread ----
    void ensureAllocated (int64_t start, int64_t end);         // [start, end)
    void clear();
    double getSampleRate() const noexcept { return sampleRate.load(); }
    void setSampleRate (double sr) noexcept { sampleRate.store (sr); }

    // ---- audio thread (real-time safe) ----
    /** Writes n samples starting at timeline position pos. Returns false if a chunk was missing. */
    bool write (int64_t pos, const float* data, int n) noexcept;

    // ---- any thread ----
    float get (int64_t pos) const noexcept;
    bool hasData() const noexcept { return writtenEnd.load() > writtenStart.load(); }
    int64_t getWrittenStart() const noexcept { return writtenStart.load(); }
    int64_t getWrittenEnd() const noexcept   { return writtenEnd.load(); }

    /** Region written since the last call (used by the UI to refresh its peak cache). */
    bool takeDirtyRange (int64_t& start, int64_t& end) noexcept;

    /** Copies [start, start+n) into dest (zeros where nothing was captured). */
    void read (int64_t start, float* dest, int n) const noexcept;

    // ---- persistence (message / background thread) ----
    bool saveToFile (const juce::File& f) const;
    bool loadFromFile (const juce::File& f, int64_t timelineStart);
    bool loadFromReader (juce::AudioFormatReader& reader, int64_t timelineStart);

    /** Encodes the captured audio (FLAC, 24-bit) so it can be stored inside the project. */
    bool encodeToFlac (juce::MemoryBlock& dest) const;
    bool decodeFromFlac (const juce::MemoryBlock& src, int64_t timelineStart);

    /** Changes every time audio is written or cleared (used to know when to re-encode). */
    uint64_t getGeneration() const noexcept { return generation.load(); }

private:
    float* chunkFor (int64_t pos) const noexcept;

    std::array<std::atomic<float*>, maxChunks> chunks;
    std::atomic<int64_t> writtenStart { std::numeric_limits<int64_t>::max() };
    std::atomic<int64_t> writtenEnd { 0 };
    std::atomic<int64_t> dirtyStart { std::numeric_limits<int64_t>::max() };
    std::atomic<int64_t> dirtyEnd { 0 };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<uint64_t> generation { 0 };

    static void atomicMin (std::atomic<int64_t>& a, int64_t v) noexcept;
    static void atomicMax (std::atomic<int64_t>& a, int64_t v) noexcept;
};
