#pragma once
#include <JuceHeader.h>
#include <vector>

/** A region of the timeline with a gain change (Range tool). */
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

/**
    A gain change made with the Blob or Peak tool. It is locked to its place in time:
    zooming (which changes the blob size) never moves or resizes it.
    Spots may overlap; their gains add up (a word +3 dB, a peak inside it -6 dB).
*/
struct SpotEdit
{
    enum Kind : int { blob = 0, peak = 1, peakAuto = 2, sib = 3, sibAuto = 4 };   // *Auto: made by a Reduction control
    static bool isSibilance (int k) noexcept { return k == sib || k == sibAuto; }
    static bool isPeak (int k) noexcept { return k == peak || k == peakAuto; }
    int64_t start = 0, end = 0;
    float db = 0.0f;
    int kind = blob;

    bool sameRegion (int64_t s, int64_t e) const noexcept { return start == s && end == e; }
};

/** Everything the user has edited. Plain value type: copied for undo snapshots. */
struct EditState
{
    std::vector<RangeEdit> ranges;
    std::vector<EnvPoint> envelope;    // kept sorted by pos
    std::vector<SpotEdit> spots;       // kept sorted by start

    // Sibilance edits: lower the whole sound (volume) or only the highs above sibFreq (filter)
    bool sibFilter = false;
    float sibFreq = 5000.0f;

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
    float hpFreq = 5000.0f;            // for the sibilance map: where "the highs" start

    float gainAt (int64_t pos) const noexcept;
};

namespace EditOps
{
    constexpr float edgeFadeSeconds = 0.005f;   // soft edges on ranges and spots (no clicks)

    /** Total dB at a timeline position (ranges + envelope + spots), with soft edges. */
    float totalDbAt (const EditState& s, int64_t pos, double sampleRate);
    float rangeDbAt (const EditState& s, int64_t pos, double sampleRate);
    float envelopeDbAt (const EditState& s, int64_t pos);
    float spotDbAt (const EditState& s, int64_t pos, double sampleRate);

    std::shared_ptr<GainMap> buildGainMap (const EditState& s, double sampleRate);
    /** Gain for the highs only (sibilance in Filter mode). Empty when there is nothing to do. */
    std::shared_ptr<GainMap> buildSibMap (const EditState& s, double sampleRate);
    /** dB of the sibilance edits alone at pos (used for drawing in Filter mode). */
    float sibOnlyDbAt (const EditState& s, int64_t pos, double sampleRate);
    /** What the waveform shows: the full change, including Filter-mode sibilance as if it were volume. */
    float displayDbAt (const EditState& s, int64_t pos, double sampleRate);

    enum ResetWhat { resetEverything = 0, resetRanges, resetEnvelope, resetSpots };

    /** Returns the state with edits inside [a, b) removed, so that region plays as recorded. */
    EditState resetRegion (const EditState& s, int64_t a, int64_t b, ResetWhat what, double sampleRate);
    EditState resetAll (const EditState& s);

    /** Index of the spot with exactly this region (and kind), or -1. */
    int findSpot (const EditState& s, int64_t start, int64_t end, int kind);
    /** Index of the spot with this region, adding a 0 dB one if missing. */
    int ensureSpot (EditState& s, int64_t start, int64_t end, int kind);
    /** Drops spots whose gain is 0 dB (they do nothing). */
    void pruneSpots (EditState& s);

    void sortEnvelope (EditState& s);
    void sortSpots (EditState& s);
}
