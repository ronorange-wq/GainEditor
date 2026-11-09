#include "CaptureStore.h"

CaptureStore::CaptureStore()
{
    for (auto& c : chunks)
        c.store (nullptr);
}

CaptureStore::~CaptureStore()
{
    for (auto& c : chunks)
        delete[] c.exchange (nullptr);
}

void CaptureStore::atomicMin (std::atomic<int64_t>& a, int64_t v) noexcept
{
    auto cur = a.load();
    while (v < cur && ! a.compare_exchange_weak (cur, v)) {}
}

void CaptureStore::atomicMax (std::atomic<int64_t>& a, int64_t v) noexcept
{
    auto cur = a.load();
    while (v > cur && ! a.compare_exchange_weak (cur, v)) {}
}

void CaptureStore::ensureAllocated (int64_t start, int64_t end)
{
    start = juce::jmax ((int64_t) 0, start);
    if (end <= start)
        return;

    const auto first = (int) juce::jlimit ((int64_t) 0, (int64_t) maxChunks - 1, start >> chunkBits);
    const auto last  = (int) juce::jlimit ((int64_t) 0, (int64_t) maxChunks - 1, (end - 1) >> chunkBits);

    for (int i = first; i <= last; ++i)
    {
        if (chunks[(size_t) i].load() == nullptr)
        {
            auto* mem = new float[(size_t) chunkSize];
            std::fill (mem, mem + chunkSize, 0.0f);
            float* expected = nullptr;
            if (! chunks[(size_t) i].compare_exchange_strong (expected, mem))
                delete[] mem;
        }
    }
}

void CaptureStore::clear()
{
    // Caller must make sure the audio thread is not writing (processor suspends processing).
    for (auto& c : chunks)
        delete[] c.exchange (nullptr);

    writtenStart = std::numeric_limits<int64_t>::max();
    writtenEnd = 0;
    dirtyStart = 0;
    dirtyEnd = std::numeric_limits<int32_t>::max();   // force a full UI refresh
}

float* CaptureStore::chunkFor (int64_t pos) const noexcept
{
    if (pos < 0)
        return nullptr;
    const auto idx = pos >> chunkBits;
    if (idx >= maxChunks)
        return nullptr;
    return chunks[(size_t) idx].load (std::memory_order_acquire);
}

bool CaptureStore::write (int64_t pos, const float* data, int n) noexcept
{
    bool ok = true;
    int done = 0;

    while (done < n)
    {
        const auto p = pos + done;
        auto* chunk = chunkFor (p);
        const auto offset = (int) (p & (chunkSize - 1));
        const auto count = (int) juce::jmin ((int64_t) (n - done), chunkSize - offset);

        if (chunk != nullptr)
            std::copy (data + done, data + done + count, chunk + offset);
        else
            ok = false;

        done += count;
    }

    if (pos >= 0)
    {
        atomicMin (writtenStart, pos);
        atomicMax (writtenEnd, pos + n);
        atomicMin (dirtyStart, pos);
        atomicMax (dirtyEnd, pos + n);
    }
    return ok;
}

float CaptureStore::get (int64_t pos) const noexcept
{
    if (auto* chunk = chunkFor (pos))
        return chunk[pos & (chunkSize - 1)];
    return 0.0f;
}

void CaptureStore::read (int64_t start, float* dest, int n) const noexcept
{
    int done = 0;
    while (done < n)
    {
        const auto p = start + done;
        if (p < 0)
        {
            const auto count = (int) juce::jmin ((int64_t) (n - done), -p);
            std::fill (dest + done, dest + done + count, 0.0f);
            done += count;
            continue;
        }

        const auto offset = (int) (p & (chunkSize - 1));
        const auto count = (int) juce::jmin ((int64_t) (n - done), chunkSize - offset);

        if (auto* chunk = chunkFor (p))
            std::copy (chunk + offset, chunk + offset + count, dest + done);
        else
            std::fill (dest + done, dest + done + count, 0.0f);

        done += count;
    }
}

bool CaptureStore::takeDirtyRange (int64_t& start, int64_t& end) noexcept
{
    start = dirtyStart.exchange (std::numeric_limits<int64_t>::max());
    end = dirtyEnd.exchange (0);
    return end > start;
}

bool CaptureStore::saveToFile (const juce::File& f) const
{
    if (! hasData())
        return false;

    f.getParentDirectory().createDirectory();
    f.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), getSampleRate(), 1, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // the writer owns the stream now

    const auto start = getWrittenStart();
    const auto end = getWrittenEnd();
    constexpr int block = 65536;
    juce::AudioBuffer<float> buf (1, block);

    for (auto p = start; p < end; p += block)
    {
        const auto n = (int) juce::jmin ((int64_t) block, end - p);
        read (p, buf.getWritePointer (0), n);
        if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
            return false;
    }
    return true;
}

bool CaptureStore::loadFromFile (const juce::File& f, int64_t timelineStart)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
    if (reader == nullptr)
        return false;

    const auto len = (int64_t) reader->lengthInSamples;
    setSampleRate (reader->sampleRate);
    ensureAllocated (timelineStart, timelineStart + len);

    constexpr int block = 65536;
    juce::AudioBuffer<float> buf (1, block);
    for (int64_t p = 0; p < len; p += block)
    {
        const auto n = (int) juce::jmin ((int64_t) block, len - p);
        reader->read (&buf, 0, n, p, true, false);
        write (timelineStart + p, buf.getReadPointer (0), n);
    }
    return true;
}
