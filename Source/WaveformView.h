#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Analysis.h"
#include <set>

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

private:
    std::vector<float> mn, mx;
};

//==============================================================================
namespace Theme
{
    const juce::Colour bg        { 0xff0f1114 };
    const juce::Colour bar       { 0xff14161a };
    const juce::Colour button    { 0xff1c1f24 };
    const juce::Colour buttonLine{ 0xff30353c };
    const juce::Colour active    { 0xff1e4e8c };
    const juce::Colour activeLine{ 0xff3b82f6 };
    const juce::Colour text      { 0xffe9ecef };
    const juce::Colour textDim   { 0xff9aa0a6 };
    const juce::Colour wave      { 0xffffd000 };
    const juce::Colour waveSel   { 0xffffe57a };
    const juce::Colour ink       { 0xff0b0b0b };
    const juce::Colour blobSel   { 0xfffff1b8 };
    const juce::Colour env       { 0xff2f6bff };
    const juce::Colour reset     { 0xffffc46b };
    const juce::Colour resetLine { 0xffe8a13a };
    const juce::Colour green     { 0xff7ff0b5 };
    const juce::Colour greenLine { 0xff3dd68c };
    const juce::Colour record    { 0xffef3b36 };
}

//==============================================================================
enum class Tool { select, range, envelope, blob };

/** Floating "-5.0 dB  [Reset]" badge shown above the thing being edited. */
class GainBadge : public juce::Component
{
public:
    GainBadge();
    void setDb (float db);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onReset;

private:
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
    double getViewEnd() const noexcept { return viewStart + spp * getWidth(); }
    void followPlayhead (int64_t pos);

    // ---- selection & commands ----
    bool hasSelection() const noexcept { return selB > selA; }
    /** The region a Reset / Loudness acts on: the range selection, else the selected blobs. */
    bool getTargetRegion (int64_t& a, int64_t& b) const;
    void clearSelection();

    void resetTarget (EditOps::ResetWhat what);
    void resetEverythingWithConfirm();
    void nudgeSelected (float db);
    void deleteSelected();
    void mergeSelectedBlobs();
    void applyResolutionToSelection (float resolution);
    void showContextMenu();

    int64_t getCursor() const noexcept { return cursor; }

    std::function<void()> onStateChanged;   // selection / tool changed: editor refreshes buttons & loudness
    std::function<void()> onShowLoudness;

private:
    // geometry
    static constexpr int rulerHeight = 40;
    juce::Rectangle<int> waveArea() const;
    double xToPos (float x) const noexcept { return viewStart + (double) x * spp; }
    float posToX (double pos) const noexcept { return (float) ((pos - viewStart) / spp); }
    float dbToY (float db) const noexcept;
    float yToDb (float y) const noexcept;
    double sampleRate() const noexcept { return proc.getSampleRateForTimeline(); }

    void paintRuler (juce::Graphics&, juce::Rectangle<int>);
    void paintWave (juce::Graphics&, juce::Rectangle<int>);
    void paintEnvelope (juce::Graphics&, juce::Rectangle<int>);
    void paintSelection (juce::Graphics&, juce::Rectangle<int>);
    void paintBlobs (juce::Graphics&, juce::Rectangle<int>);
    void updateBadge();

    int rangeEditAt (int64_t pos) const;
    int blobIndexAt (int64_t pos) const;
    int envPointNear (juce::Point<float> p) const;
    std::vector<int> selectedBlobIndices() const;
    float stepFor (const juce::ModifierKeys& m) const noexcept { return m.isAltDown() ? 0.01f : 0.1f; }

    GainEditorProcessor& proc;
    PeakCache& peaks;
    GainBadge badge;

    Tool tool = Tool::range;
    double viewStart = 0.0, spp = 400.0;           // samples per pixel
    float vZoom = 1.0f;                            // waveform height zoom
    int64_t selA = 0, selB = 0, cursor = 0;
    int activeRange = -1;
    std::set<int64_t> selectedBlobStarts;
    int selectedPoint = -1;

    enum class Drag { none, newSelection, edgeA, edgeB, rangeGain, envPoint, blobGain, blobStart, blobEnd, ruler };
    Drag drag = Drag::none;
    int64_t dragAnchor = 0;
    float dragStartDb = 0.0f;
    std::vector<float> dragStartBlobDb;
    float lastDragY = 0.0f;
    float dragDbAccum = 0.0f;
    bool dragMoved = false;

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
