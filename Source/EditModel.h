#pragma once
#include <JuceHeader.h>
#include <vector>

/** A region of the timeline with a gain change (used by the Range layer). */
struct RangeEdit
{
    int64_t start = 0, end = 0;   // timeline samples, [start, end)
    float db = 0.0f;
};

/** One point of the drawn volume envelope. */
struct EnvPoint
{
    int64_t pos = 0;
    float db = 0.0f;
};

/** A detected (or hand-adjusted) blob and its gain. */
struct Blob
{
    int64_t start = 0, end = 0;
    float db = 0.0f;
};

/** Everything the user has edited. Plain value type: copied for undo snapshots. */
struct EditState
{
    std::vector<RangeEdit> ranges;
    std::vector<EnvPoint> envelope;    // kept sorted by pos
    std::vector<Blob> blobs;           // kept sorted by start, non-overlapping
    float blobResolution = 0.6f;       // 0 = Phrase ... 1 = Peak

    bool operator== (const EditState& o) const;
    bool operator!= (const EditState& o) const { return ! (*this == o); }

    juce::ValueTree toValueTree() const;
    static EditState fromValueTree (const juce::ValueTree& v);
};

/**
    The gain the audio thread applies, sampled every `hop` samples along the timeline.
    Built on the message thread from an EditState and handed to the audio thread whole.
*/
struct GainMap
{
    static constexpr int hop = 32;
    int64_t origin = 0;                // timeline sample of gains[0]
    std::vector<float> gains;          // linear gain per hop
    float before = 1.0f, after = 1.0f; // gain outside the covered region

    float gainAt (int64_t pos) const noexcept;
};

namespace EditOps
{
    constexpr float edgeFadeSeconds = 0.005f;   // soft edges on ranges and blobs (no clicks)

    /** Total dB at a timeline position (ranges + envelope + blobs), with soft edges. */
    float totalDbAt (const EditState& s, int64_t pos, double sampleRate);
    float rangeDbAt (const EditState& s, int64_t pos, double sampleRate);
    float envelopeDbAt (const EditState& s, int64_t pos);
    float blobDbAt (const EditState& s, int64_t pos, double sampleRate);

    std::shared_ptr<GainMap> buildGainMap (const EditState& s, double sampleRate);

    enum ResetWhat { resetEverything = 0, resetRanges, resetEnvelope, resetBlobs };

    /** Returns the state with edits inside [a, b) removed, so that region plays as recorded. */
    EditState resetRegion (const EditState& s, int64_t a, int64_t b, ResetWhat what, double sampleRate);
    EditState resetAll (const EditState& s);

    void sortEnvelope (EditState& s);
    void sortBlobs (EditState& s);
}
