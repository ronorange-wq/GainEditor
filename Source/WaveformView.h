#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analysis.h"

//==============================================================================
/** Min/max of the captured audio per 64 samples, for fast drawing. */
class PeakCache
{
public:
    static constexpr int binSize = 64;

    void update (const CaptureStore& audio, int64_t start, int64_t end);
    void clear() { mn.clear(); mx.clear(); }

    /** Min and max of the original audio over [s0, s1). */
    void getRange (const CaptureStore& audio, int64_t s0, int64_t s1, float& lo, float& hi) const;
    /** Min, max and RMS of the original audio over [s0, s1). */
    void getRange (const CaptureStore& audio, int64_t s0, int64_t s1, float& lo, float& hi, float& rms) const;

private:
    std::vector<float> mn, mx, ms;   // per bin: min, max, mean square
};

//==============================================================================
namespace Theme
{
    const juce::Colour bg        { 0xff171b21 };
    const juce::Colour bar       { 0xff1a1e25 };
    const juce::Colour button    { 0xff20252d };
    const juce::Colour buttonLine{ 0xff353c47 };
    const juce::Colour active    { 0xff23558f };
    const juce::Colour activeLine{ 0xff5aaaff };
    const juce::Colour text      { 0xffe9ecef };
    const juce::Colour textDim   { 0xff9aa0a6 };
    const juce::Colour waveBg    { 0xff1c2129 };
    const juce::Colour grid      { 0xff262c35 };
    const juce::Colour scaleBg   { 0xff1c2129 };
    const juce::Colour scaleText { 0xffc3cad4 };
    const juce::Colour wave      { 0xff5aaaff };
    const juce::Colour selFill   { 0xff2e6fb8 };
    const juce::Colour selLine   { 0xff6fb6ff };
    const juce::Colour spot      { 0xff5b8cff };
    const juce::Colour spotFill  { 0xff2f6bff };
    const juce::Colour over      { 0xffff6a3d };   // audio above the threshold
    const juce::Colour thresh    { 0xffffb020 };
    const juce::Colour envLine   { 0xfff1f3f5 };
    const juce::Colour reset     { 0xffffc46b };
    const juce::Colour resetLine { 0xffe8a13a };
    const juce::Colour green     { 0xff7ff0b5 };
    const juce::Colour greenLine { 0xff3dd68c };
    const juce::Colour record    { 0xffef3b36 };
}

//==============================================================================
enum class Tool { select = 0, range, envelope, blob, peak };

/** "Blob  +3.0 dB  [Reset]" shown in the top-left corner of the waveform (never over the audio you edit). */
class GainBadge : public juce::Component
{
public:
    GainBadge();
    void setInfo (const juce::String& what, float db);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onReset;

private:
    juce::String label;
    float value = 0.0f;
    juce::TextButton resetButton { "Reset" };
};

//==============================================================================
class WaveformView : public juce::Component,
                     public juce::ChangeListener
{
public:
    WaveformView (GainEditorProcessor&, PeakCache&);
    ~WaveformView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // ---- view ----
    void setTool (Tool t);
    Tool getTool() const noexcept { return tool; }
    void zoomBy (double factor, double anchorX = -1.0);
    void zoomToFit();
    void zoomToSelection();
    void zoomVertical (float factor);
    void scrollTo (double startSample);
    double getViewStart() const noexcept { return viewStart; }
    double getViewEnd() const noexcept { return viewStart + spp * waveArea().getWidth(); }
    double getSamplesPerPixel() const noexcept { return spp; }
    void followPlayhead (int64_t pos);
    juce::Rectangle<int> waveArea() const;

    /** Current blob size (follows the zoom). */
    float currentResolution() const;

    // ---- selection & commands ----
    bool hasSelection() const noexcept { return selB > selA; }
    /** The region Reset / Loudness act on: the range selection, else the selected spots. */
    bool getTargetRegion (int64_t& a, int64_t& b) const;
    void clearSelection();

    void resetTarget (EditOps::ResetWhat what);
    void resetEverythingWithConfirm();
    void nudgeSelected (float db);
    void deleteSelected();
    void showContextMenu();

    // ---- peaks ----
    int countPeaksAbove();                    // whole take, cached
    void selectAllPeaks();
    void tameAllPeaks();

    int64_t getCursor() const noexcept { return cursor; }

    std::function<void()> onStateChanged;   // selection / tool / view changed
    std::function<void()> onShowLoudness;

private:
    // geometry
    static constexpr int rulerHeight = 34;
    static constexpr int scaleWidth = 56;
    double xToPos (float x) const noexcept { return viewStart + (double) (x - (float) waveArea().getX()) * spp; }
    float posToX (double pos) const noexcept { return (float) waveArea().getX() + (float) ((pos - viewStart) / spp); }
    float ampToY (float amp, bool upper) const noexcept;
    float dbToY (float db) const noexcept;     // envelope line
    float yToDb (float y) const noexcept;
    double sampleRate() const noexcept { return proc.getSampleRateForTimeline(); }
    float thresholdGain() const { return juce::Decibels::decibelsToGain (proc.getThresholdDb()); }

    void paintRuler (juce::Graphics&, juce::Rectangle<int>);
    void paintScale (juce::Graphics&, juce::Rectangle<int>, bool leftSide);
    void paintGrid (juce::Graphics&, juce::Rectangle<int>);
    void paintWave (juce::Graphics&, juce::Rectangle<int>);
    void paintSpots (juce::Graphics&, juce::Rectangle<int>);
    void paintEnvelope (juce::Graphics&, juce::Rectangle<int>);
    void paintThreshold (juce::Graphics&, juce::Rectangle<int>);
    void updateBadge();

    // blobs follow the zoom: detected for the visible area and cached
    void refreshSegments();
    const Analysis::Segment* segmentAt (int64_t pos) const;
    /** What the Blob tool grabs at pos: an existing blob edit, or the detected blob. */
    bool blobTargetAt (int64_t pos, int64_t& a, int64_t& b, int& spotIndex) const;
    // peaks over the threshold in the visible area, cached
    void refreshVisiblePeaks();
    const Analysis::Peak* peakNear (int64_t pos) const;

    int rangeEditAt (int64_t pos) const;
    int envPointNear (juce::Point<float> p) const;
    std::vector<int> selectedSpotIndices() const;
    void selectSpot (const SpotEdit& s, bool add);
    float stepFor (const juce::ModifierKeys& m) const noexcept { return m.isAltDown() ? 0.01f : 0.1f; }
    void grabFocusForKeys();
    void changed();

    GainEditorProcessor& proc;
    PeakCache& peaks;
    GainBadge badge;

    Tool tool = Tool::blob;
    double viewStart = 0.0, spp = 400.0;           // samples per pixel
    float vZoom = 1.0f;
    int64_t selA = 0, selB = 0, cursor = 0;
    int activeRange = -1;
    int selectedPoint = -1;

    struct SpotKey { int64_t start, end; int kind; };
    std::vector<SpotKey> selectedSpots;

    std::vector<Analysis::Segment> segments;
    int64_t segA = 0, segB = 0;
    float segRes = -1.0f;
    uint64_t segGeneration = 0;
    int64_t hoverPos = -1;

    std::vector<Analysis::Peak> visiblePeaks;

    struct Diamond
    {
        int64_t a = 0, b = 0;      // region of the peak's spot
        int64_t at = 0;            // highest sample
        float level = 0.0f;        // after edits (linear)
        int spot = -1;             // index in spots, or -1 if the peak has no edit yet
        bool over = true;          // still over the threshold
    };
    std::vector<Diamond> diamonds;
    void refreshDiamonds();
    bool diamondsDirty = true;
    int diamondNear (juce::Point<float> p) const;
    float dragStartLevel = 1.0f;
    bool peaksDirty = true;
    int peakCount = -1;

    enum class Drag { none, newSelection, edgeA, edgeB, rangeGain, envPoint, spotGain, spotStart, spotEnd, ruler, threshold, diamond };
    Drag drag = Drag::none;
    int64_t dragAnchor = 0;
    float dragStartDb = 0.0f;
    std::vector<float> dragStartSpotDb;
    float lastDragY = 0.0f, dragDbAccum = 0.0f;
    bool dragMoved = false;
    juce::Point<float> lastClick;
    int clickCycle = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

//==============================================================================
/** The whole take in miniature, with the visible part outlined. Click or drag to move. */
class OverviewStrip : public juce::Component
{
public:
    OverviewStrip (GainEditorProcessor&, PeakCache&, WaveformView&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void jumpTo (float x);
    double totalLength() const;
    GainEditorProcessor& proc;
    PeakCache& peaks;
    WaveformView& view;
};
