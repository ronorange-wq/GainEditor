#pragma once
#include <JuceHeader.h>
#include "EditModel.h"
#include "CaptureStore.h"

namespace Analysis
{
    /** A detected blob (word, syllable, peak...). Just a region: gains live in SpotEdit. */
    struct Segment
    {
        int64_t start = 0, end = 0;
    };

    /**
        Finds blobs in [start, end) of the captured audio.
        resolution: 0 = whole phrases ... 1 = every single peak.
    */
    std::vector<Segment> detectBlobs (const CaptureStore& audio, int64_t start, int64_t end, float resolution);

    /** Blob size follows zoom: the closer you zoom in, the smaller the blobs (down to single peaks). */
    float resolutionForZoom (double samplesPerPixel, double sampleRate);

    /** Name of a resolution: Phrase, Word, Syllable, Fine, Peak. */
    juce::String resolutionName (float resolution);

    /** A peak of the edited audio that goes over the threshold. */
    struct Peak
    {
        int64_t start = 0, end = 0;   // samples over the threshold
        int64_t at = 0;               // position of the highest sample
        float level = 0.0f;           // highest |sample| after the current edits (linear)
    };

    /** Peaks over thresholdGain in [start, end), measured on the audio with the current edits applied. */
    std::vector<Peak> findPeaks (const CaptureStore& audio, const EditState& edits, int64_t start, int64_t end, float thresholdGain);

    /** The region a peak spot covers (a little wider than the peak, for soft edges). */
    void peakRegion (const Peak& p, double sampleRate, int64_t& a, int64_t& b);

    /** Adds Peak spots that bring every peak in [start, end) down to the threshold. */
    int tamePeaks (EditState& edits, const CaptureStore& audio, int64_t start, int64_t end, float thresholdGain);

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
