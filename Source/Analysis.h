#pragma once
#include <JuceHeader.h>
#include "EditModel.h"
#include "CaptureStore.h"

namespace Analysis
{
    /**
        Finds blobs in [start, end) of the captured audio.
        resolution: 0 = whole phrases ... 1 = every single peak.
    */
    std::vector<Blob> detectBlobs (const CaptureStore& audio, int64_t start, int64_t end, float resolution);

    /** Re-detects blobs in [start, end) (or everywhere if end <= start) and keeps gains the user already set. */
    void redetect (EditState& state, const CaptureStore& audio, float resolution, int64_t start = 0, int64_t end = 0);

    /** Labels for the resolution slider. */
    juce::String resolutionName (float resolution);

    struct LoudnessStats
    {
        float peakDb = -150.0f;       // dBFS
        float truePeakDb = -150.0f;   // dBTP (4x oversampled)
        float rmsDb = -150.0f;        // dBFS
        float lufs = -150.0f;         // BS.1770 K-weighted loudness of the selection (ungated)
    };

    struct LoudnessReport
    {
        LoudnessStats original, now;
        float gainAppliedDb = 0.0f;   // average gain change in the selection
        double seconds = 0.0;
        bool valid = false;
    };

    LoudnessReport measure (const CaptureStore& audio, const EditState& edits, int64_t start, int64_t end);
}
