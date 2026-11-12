#include "WaveformView.h"
#include <cmath>

//==============================================================================
// PeakCache
//==============================================================================
void PeakCache::update (const CaptureStore& audio, int64_t start, int64_t end)
{
    end = juce::jmin (end, audio.getWrittenEnd());
    if (end <= 0)
        return;

    const auto neededBins = (size_t) ((end + binSize - 1) / binSize);
    if (mn.size() < neededBins)
    {
        mn.resize (neededBins, 0.0f);
        mx.resize (neededBins, 0.0f);
    }

    const auto firstBin = (size_t) juce::jmax ((int64_t) 0, start / binSize);
    float buf[binSize];
    for (auto b = firstBin; b < neededBins; ++b)
    {
        audio.read ((int64_t) b * binSize, buf, binSize);
        float lo = 0.0f, hi = 0.0f;
        for (auto v : buf) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
        mn[b] = lo;
        mx[b] = hi;
    }
}

void PeakCache::getRange (const CaptureStore& audio, int64_t s0, int64_t s1, float& lo, float& hi) const
{
    lo = hi = 0.0f;
    if (s1 <= s0)
        s1 = s0 + 1;

    if (s1 - s0 < binSize * 2)
    {
        for (auto p = s0; p < s1; ++p)
        {
            const auto v = audio.get (p);
            lo = juce::jmin (lo, v);
            hi = juce::jmax (hi, v);
        }
        return;
    }

    const auto b0 = (size_t) juce::jmax ((int64_t) 0, s0 / binSize);
    const auto b1 = (size_t) juce::jmax ((int64_t) 0, (s1 - 1) / binSize);
    for (auto b = b0; b <= b1 && b < mn.size(); ++b)
    {
        lo = juce::jmin (lo, mn[b]);
        hi = juce::jmax (hi, mx[b]);
    }
}

//==============================================================================
// GainBadge
//==============================================================================
GainBadge::GainBadge()
{
    resetButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a1f0e));
    resetButton.setColour (juce::TextButton::textColourOffId, Theme::reset);
    resetButton.setColour (juce::ComboBox::outlineColourId, Theme::resetLine);
    resetButton.onClick = [this] { if (onReset) onReset(); };
    resetButton.setTooltip ("Return this part to how it was recorded (Cmd+R)");
    addAndMakeVisible (resetButton);
    setInterceptsMouseClicks (false, true);   // only the Reset button takes clicks
    setSize (236, 40);
}

void GainBadge::setInfo (const juce::String& what, float db)
{
    if (what != label || std::abs (db - value) > 0.0001f)
    {
        label = what;
        value = db;
        repaint();
    }
}

static juce::String dbText (float v, int decimals = 1)
{
    const auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    if (std::abs (v) < 0.005f) return "0.0 dB";
    return (v > 0.0f ? juce::String ("+") : minus) + juce::String (std::abs (v), decimals) + " dB";
}

void GainBadge::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xeb121418));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (juce::Colour (0xff3a3f47));
    g.drawRoundedRectangle (r, 9.0f, 1.0f);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (label, 12, 0, 70, getHeight(), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (dbText (value, std::abs (value) < 10.0f ? 2 : 1), 70, 0, 90, getHeight(), juce::Justification::centredLeft);
}

void GainBadge::resized()
{
    resetButton.setBounds (getWidth() - 72, 6, 66, getHeight() - 12);
}

//==============================================================================
// WaveformView
//==============================================================================
WaveformView::WaveformView (GainEditorProcessor& p, PeakCache& pc) : proc (p), peaks (pc)
{
    addChildComponent (badge);
    badge.onReset = [this]
    {
        if (tool == Tool::envelope && selectedPoint >= 0)
            deleteSelected();
        else
            resetTarget (EditOps::resetEverything);
    };
    proc.addChangeListener (this);
}

WaveformView::~WaveformView()
{
    proc.removeChangeListener (this);
}

void WaveformView::changed()
{
    updateBadge();
    repaint();
    if (onStateChanged) onStateChanged();
}

void WaveformView::grabFocusForKeys()
{
    // Ableton keeps the keyboard unless the plugin asks for it on every click.
    if (auto* ed = findParentComponentOfClass<juce::AudioProcessorEditor>())
        ed->grabKeyboardFocus();
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (activeRange >= (int) proc.getEdits().ranges.size())
        activeRange = -1;
    if (selectedPoint >= (int) proc.getEdits().envelope.size())
        selectedPoint = -1;
    peaksDirty = true; diamondsDirty = true;
    peakCount = -1;
    updateBadge();
    repaint();
}

juce::Rectangle<int> WaveformView::waveArea() const
{
    return getLocalBounds().withTrimmedTop (rulerHeight).reduced (scaleWidth, 0);
}

void WaveformView::resized()
{
    badge.setTopLeftPosition (waveArea().getX() + 10, waveArea().getY() + 10);
    peaksDirty = true; diamondsDirty = true;
    updateBadge();
}

float WaveformView::ampToY (float amp, bool upper) const noexcept
{
    const auto a = waveArea().toFloat();
    const auto mid = a.getCentreY();
    const auto half = (a.getHeight() * 0.5f - 10.0f) * vZoom;
    return upper ? mid - amp * half : mid + amp * half;
}

float WaveformView::dbToY (float db) const noexcept
{
    const auto a = waveArea().toFloat();
    const auto zero = a.getY() + a.getHeight() * 0.30f;
    db = juce::jlimit (-36.0f, 12.0f, db);
    if (db >= 0.0f)
        return zero - db / 12.0f * (zero - a.getY() - 6.0f);
    return zero + (-db / 36.0f) * (a.getBottom() - 6.0f - zero);
}

float WaveformView::yToDb (float y) const noexcept
{
    const auto a = waveArea().toFloat();
    const auto zero = a.getY() + a.getHeight() * 0.30f;
    if (y <= zero)
        return juce::jlimit (0.0f, 12.0f, (zero - y) / (zero - a.getY() - 6.0f) * 12.0f);
    return juce::jlimit (-36.0f, 0.0f, -(y - zero) / (a.getBottom() - 6.0f - zero) * 36.0f);
}

//==============================================================================
void WaveformView::setTool (Tool t)
{
    tool = t;
    selectedPoint = -1;
    hoverPos = -1;
    changed();
}

void WaveformView::zoomBy (double factor, double anchorX)
{
    if (anchorX < 0.0)
        anchorX = waveArea().getCentreX();
    const auto anchorPos = xToPos ((float) anchorX);
    spp = juce::jlimit (0.05, 200000.0, spp / factor);
    scrollTo (anchorPos - (anchorX - waveArea().getX()) * spp);
}

void WaveformView::zoomToFit()
{
    const auto& c = proc.getCapture();
    const auto w = waveArea().getWidth();
    if (! c.hasData() || w <= 0)
        return;
    const auto a = (double) c.getWrittenStart(), b = (double) c.getWrittenEnd();
    spp = juce::jmax (0.05, (b - a) / (w * 0.98));
    scrollTo (a - (b - a) * 0.01);
}

void WaveformView::zoomToSelection()
{
    int64_t a, b;
    const auto w = waveArea().getWidth();
    if (! getTargetRegion (a, b) || w <= 0)
        return;
    const auto len = (double) (b - a);
    spp = juce::jmax (0.05, len * 1.2 / w);
    scrollTo ((double) a - len * 0.1);
}

void WaveformView::zoomVertical (float factor)
{
    vZoom = juce::jlimit (0.25f, 32.0f, vZoom * factor);
    repaint();
}

void WaveformView::scrollTo (double startSample)
{
    viewStart = juce::jmax (-spp * 20.0, startSample);
    peaksDirty = true; diamondsDirty = true;
    changed();
}

void WaveformView::followPlayhead (int64_t pos)
{
    const auto x = posToX ((double) pos);
    const auto a = waveArea();
    if (x > a.getX() + a.getWidth() * 0.92f || x < a.getX())
        scrollTo ((double) pos - a.getWidth() * 0.08 * spp);
}

float WaveformView::currentResolution() const
{
    return Analysis::resolutionForZoom (spp, sampleRate());
}

//==============================================================================
// Cached detection
//==============================================================================
void WaveformView::refreshSegments()
{
    const auto& cap = proc.getCapture();
    if (! cap.hasData())
    {
        segments.clear();
        return;
    }

    // Detect over the whole take (cached per zoom step) so a blob is always the same blob.
    auto r = std::round (currentResolution() * 40.0f) / 40.0f;
    const auto gen = cap.getGeneration();
    if (std::abs (r - segRes) < 0.001f && gen == segGeneration && ! segments.empty())
        return;

    segA = cap.getWrittenStart();
    segB = cap.getWrittenEnd();
    segRes = r;
    segGeneration = gen;
    segments = Analysis::detectBlobs (cap, segA, segB, r);
}

bool WaveformView::blobTargetAt (int64_t pos, int64_t& a, int64_t& b, int& spotIndex) const
{
    spotIndex = -1;
    const auto* sg = segmentAt (pos);
    const auto segLen = sg != nullptr ? sg->end - sg->start : (int64_t) 0;

    // An existing blob edit under the mouse wins, unless you zoomed in far enough that blobs are much smaller now.
    const auto& sp = proc.getEdits().spots;
    int64_t bestLen = std::numeric_limits<int64_t>::max();
    for (int k = 0; k < (int) sp.size(); ++k)
    {
        const auto& s = sp[(size_t) k];
        if (s.kind != SpotEdit::blob || pos < s.start || pos >= s.end) continue;
        const auto len = s.end - s.start;
        if ((sg == nullptr || len <= segLen * 5 / 2) && len < bestLen) { bestLen = len; spotIndex = k; }
    }
    if (spotIndex >= 0)
    {
        a = sp[(size_t) spotIndex].start;
        b = sp[(size_t) spotIndex].end;
        return true;
    }
    if (sg == nullptr)
        return false;
    a = sg->start;
    b = sg->end;
    return true;
}

const Analysis::Segment* WaveformView::segmentAt (int64_t pos) const
{
    auto it = std::upper_bound (segments.begin(), segments.end(), pos,
                                [] (int64_t p, const Analysis::Segment& s) { return p < s.start; });
    if (it == segments.begin())
        return nullptr;
    --it;
    return pos < it->end ? &*it : nullptr;
}

void WaveformView::refreshVisiblePeaks()
{
    if (! peaksDirty)
        return;
    peaksDirty = false;
    diamondsDirty = true;
    const auto margin = (int64_t) (spp * 20.0);
    visiblePeaks = Analysis::findPeaks (proc.getCapture(), proc.getEdits(),
                                        (int64_t) viewStart - margin, (int64_t) getViewEnd() + margin, thresholdGain());
}

const Analysis::Peak* WaveformView::peakNear (int64_t pos) const
{
    const auto slack = (int64_t) (spp * 4.0);
    const Analysis::Peak* best = nullptr;
    int64_t bestDist = std::numeric_limits<int64_t>::max();
    for (auto& p : visiblePeaks)
    {
        if (pos < p.start - slack || pos > p.end + slack)
            continue;
        const auto d = std::abs (pos - p.at);
        if (d < bestDist) { bestDist = d; best = &p; }
    }
    return best;
}

int WaveformView::countPeaksAbove()
{
    if (peakCount < 0)
    {
        const auto& c = proc.getCapture();
        peakCount = c.hasData() ? (int) Analysis::findPeaks (c, proc.getEdits(), c.getWrittenStart(), c.getWrittenEnd(), thresholdGain()).size() : 0;
    }
    return peakCount;
}

void WaveformView::selectAllPeaks()
{
    const auto& c = proc.getCapture();
    if (! c.hasData()) return;
    auto pk = Analysis::findPeaks (c, proc.getEdits(), c.getWrittenStart(), c.getWrittenEnd(), thresholdGain());
    auto s = proc.getEdits();
    selectedSpots.clear();
    for (auto& p : pk)
    {
        int64_t a, b;
        Analysis::peakRegion (p, sampleRate(), a, b);
        EditOps::ensureSpot (s, a, b, SpotEdit::peak);
        selectedSpots.push_back ({ a, b, SpotEdit::peak });
    }
    selA = selB = 0;
    proc.setEdits (s, false);
    changed();
}

void WaveformView::tameAllPeaks()
{
    const auto& c = proc.getCapture();
    if (! c.hasData()) return;
    auto s = proc.getEdits();
    int64_t a = c.getWrittenStart(), b = c.getWrittenEnd();
    if (hasSelection()) { a = selA; b = selB; }   // only inside the selection, if there is one
    Analysis::tamePeaks (s, c, a, b, thresholdGain());
    proc.setEdits (s, true, "Tame Peaks");
}

//==============================================================================
// Hit testing
//==============================================================================
int WaveformView::rangeEditAt (int64_t pos) const
{
    const auto& r = proc.getEdits().ranges;
    int best = -1;
    int64_t bestLen = std::numeric_limits<int64_t>::max();
    for (int i = 0; i < (int) r.size(); ++i)
        if (pos >= r[(size_t) i].start && pos < r[(size_t) i].end && r[(size_t) i].end - r[(size_t) i].start < bestLen)
        {
            best = i;
            bestLen = r[(size_t) i].end - r[(size_t) i].start;
        }
    return best;
}

int WaveformView::envPointNear (juce::Point<float> p) const
{
    const auto& e = proc.getEdits().envelope;
    for (int i = 0; i < (int) e.size(); ++i)
    {
        const juce::Point<float> q (posToX ((double) e[(size_t) i].pos), dbToY (e[(size_t) i].db));
        if (q.getDistanceFrom (p) < 8.0f)
            return i;
    }
    return -1;
}

std::vector<int> WaveformView::selectedSpotIndices() const
{
    std::vector<int> out;
    const auto& sp = proc.getEdits().spots;
    for (auto& k : selectedSpots)
        for (int i = 0; i < (int) sp.size(); ++i)
            if (sp[(size_t) i].sameRegion (k.start, k.end) && sp[(size_t) i].kind == k.kind)
            {
                out.push_back (i);
                break;
            }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

void WaveformView::selectSpot (const SpotEdit& s, bool add)
{
    SpotKey k { s.start, s.end, s.kind };
    auto it = std::find_if (selectedSpots.begin(), selectedSpots.end(), [&] (const SpotKey& o)
                            { return o.start == k.start && o.end == k.end && o.kind == k.kind; });
    if (add)
    {
        if (it != selectedSpots.end()) selectedSpots.erase (it);
        else selectedSpots.push_back (k);
    }
    else if (it == selectedSpots.end())
    {
        selectedSpots = { k };
    }
}

bool WaveformView::getTargetRegion (int64_t& a, int64_t& b) const
{
    if (hasSelection())
    {
        a = selA; b = selB;
        return true;
    }
    auto idx = selectedSpotIndices();
    if (! idx.empty())
    {
        const auto& sp = proc.getEdits().spots;
        a = std::numeric_limits<int64_t>::max(); b = 0;
        for (auto i : idx) { a = juce::jmin (a, sp[(size_t) i].start); b = juce::jmax (b, sp[(size_t) i].end); }
        return true;
    }
    return false;
}

void WaveformView::clearSelection()
{
    selA = selB = 0;
    activeRange = -1;
    selectedSpots.clear();
    selectedPoint = -1;
    changed();
}

//==============================================================================
// Commands
//==============================================================================
void WaveformView::resetTarget (EditOps::ResetWhat what)
{
    static const char* names[] = { "Reset Selection", "Reset Range Gain", "Reset Envelope", "Reset Blobs & Peaks" };

    // Selected spots (no range selection): remove exactly those spots.
    if (! hasSelection() && ! selectedSpots.empty() && (what == EditOps::resetEverything || what == EditOps::resetSpots))
    {
        auto s = proc.getEdits();
        auto idx = selectedSpotIndices();
        for (auto it = idx.rbegin(); it != idx.rend(); ++it)
            s.spots.erase (s.spots.begin() + *it);
        selectedSpots.clear();
        proc.setEdits (s, true, names[(int) what]);
        return;
    }

    int64_t a, b;
    if (! getTargetRegion (a, b))
        return;
    activeRange = -1;
    proc.setEdits (EditOps::resetRegion (proc.getEdits(), a, b, what, sampleRate()), true, names[(int) what]);
}

void WaveformView::resetEverythingWithConfirm()
{
    juce::Component::SafePointer<WaveformView> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Reset All Edits")
                                      .withMessage ("Remove every gain change (Range, Envelope, Blob and Peak) from the whole take?\nYou can still undo this.")
                                      .withButton ("Reset All")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [safe] (int result)
                                  {
                                      if (safe == nullptr || result != 1)
                                          return;
                                      safe->activeRange = -1;
                                      safe->selectedSpots.clear();
                                      safe->proc.setEdits (EditOps::resetAll (safe->proc.getEdits()), true, "Reset All Edits");
                                  });
}

void WaveformView::nudgeSelected (float db)
{
    auto s = proc.getEdits();

    if (tool == Tool::envelope && selectedPoint >= 0 && selectedPoint < (int) s.envelope.size())
    {
        auto& p = s.envelope[(size_t) selectedPoint];
        p.db = juce::jlimit (-36.0f, 12.0f, p.db + db);
        proc.setEdits (s, true, "Nudge Envelope Point");
        return;
    }

    auto idx = selectedSpotIndices();
    if (! idx.empty())
    {
        for (auto i : idx)
            s.spots[(size_t) i].db = juce::jlimit (-48.0f, 24.0f, s.spots[(size_t) i].db + db);
        proc.setEdits (s, true, "Nudge Gain");
        return;
    }

    if (hasSelection())
    {
        if (activeRange < 0 || activeRange >= (int) s.ranges.size())
        {
            s.ranges.push_back ({ selA, selB, 0.0f });
            activeRange = (int) s.ranges.size() - 1;
        }
        auto& r = s.ranges[(size_t) activeRange];
        r.db = juce::jlimit (-48.0f, 24.0f, r.db + db);
        proc.setEdits (s, true, "Nudge Range Gain");
    }
}

void WaveformView::deleteSelected()
{
    auto s = proc.getEdits();
    if (tool == Tool::envelope && selectedPoint >= 0 && selectedPoint < (int) s.envelope.size())
    {
        s.envelope.erase (s.envelope.begin() + selectedPoint);
        selectedPoint = -1;
        proc.setEdits (s, true, "Delete Envelope Point");
        return;
    }
    resetTarget (EditOps::resetEverything);
}

void WaveformView::showContextMenu()
{
    int64_t a, b;
    const bool target = getTargetRegion (a, b);
    juce::PopupMenu m;
    m.addSectionHeader ("Reset");
    m.addItem (1, "Reset Selection  (Cmd+R)", target);
    m.addItem (2, "Reset Range Gain Only", target);
    m.addItem (3, "Reset Envelope in Selection", target);
    m.addItem (4, "Reset Blobs & Peaks in Selection", target);
    m.addSeparator();
    m.addItem (5, "Reset All Edits...  (Shift+Cmd+R)");
    m.addSeparator();
    m.addItem (6, "Show Loudness  (L)", target);
    m.addItem (7, "Zoom to Selection  (Z)", target);
    m.addSeparator();
    m.addItem (8, "Tame Peaks Above Threshold");
    m.addSeparator();
    m.addItem (9, "Undo", proc.getUndoManager().canUndo());
    m.addItem (10, "Redo", proc.getUndoManager().canRedo());

    juce::Component::SafePointer<WaveformView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe] (int r)
                     {
                         if (safe == nullptr) return;
                         switch (r)
                         {
                             case 1: safe->resetTarget (EditOps::resetEverything); break;
                             case 2: safe->resetTarget (EditOps::resetRanges); break;
                             case 3: safe->resetTarget (EditOps::resetEnvelope); break;
                             case 4: safe->resetTarget (EditOps::resetSpots); break;
                             case 5: safe->resetEverythingWithConfirm(); break;
                             case 6: if (safe->onShowLoudness) safe->onShowLoudness(); break;
                             case 7: safe->zoomToSelection(); break;
                             case 8: safe->tameAllPeaks(); break;
                             case 9: safe->proc.getUndoManager().undo(); break;
                             case 10: safe->proc.getUndoManager().redo(); break;
                             default: break;
                         }
                     });
}

//==============================================================================
// Badge (fixed in the corner)
//==============================================================================
void WaveformView::updateBadge()
{
    const auto& s = proc.getEdits();
    juce::String what;
    float db = 0.0f;
    bool show = false;

    if (tool == Tool::envelope && selectedPoint >= 0 && selectedPoint < (int) s.envelope.size())
    {
        what = "Point"; db = s.envelope[(size_t) selectedPoint].db; show = true;
    }
    else if (! selectedSpots.empty() && ! hasSelection())
    {
        auto idx = selectedSpotIndices();
        if (! idx.empty())
        {
            const auto& sp = s.spots[(size_t) idx.front()];
            what = idx.size() > 1 ? juce::String ((int) idx.size()) + " sel." : (sp.kind == SpotEdit::peak ? "Peak" : "Blob");
            db = sp.db;
            show = true;
        }
    }
    else if (hasSelection())
    {
        what = "Range";
        db = (activeRange >= 0 && activeRange < (int) s.ranges.size()) ? s.ranges[(size_t) activeRange].db : 0.0f;
        show = true;
    }

    badge.setInfo (what, db);
    badge.setVisible (false);
    juce::ignoreUnused (show);
}

//==============================================================================
// Painting
//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    const auto area = waveArea();
    paintRuler (g, getLocalBounds().withHeight (rulerHeight));

    g.setColour (Theme::waveBg);
    g.fillRect (area);
    paintGrid (g, area);

    // hovered blob
    int64_t ha = 0, hb = 0;
    int hs = -1;
    if (tool == Tool::blob && hoverPos >= 0 && drag == Drag::none && blobTargetAt (hoverPos, ha, hb, hs))
        {
            const auto x0 = posToX ((double) ha), x1 = posToX ((double) hb);
            juce::Rectangle<float> r (x0, (float) area.getY(), x1 - x0, (float) area.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (r);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawRect (r, 1.2f);
        }

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area);
        paintWave (g, area);
        if (tool == Tool::blob || tool == Tool::peak || tool == Tool::select)
            paintSpots (g, area);
        paintThreshold (g, area);   // diamonds on top
        if (tool == Tool::envelope || ! proc.getEdits().envelope.empty())
            paintEnvelope (g, area);

        if (hasSelection())
        {
            const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
            juce::Rectangle<float> r (x0, (float) area.getY() + 14.0f, juce::jmax (2.0f, x1 - x0), (float) area.getHeight() - 28.0f);
            g.setColour (Theme::selFill.withAlpha (0.28f));
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (Theme::selLine);
            g.drawRoundedRectangle (r, 7.0f, 1.6f);
            const auto db = (activeRange >= 0 && activeRange < (int) proc.getEdits().ranges.size()) ? proc.getEdits().ranges[(size_t) activeRange].db : 0.0f;
            const auto visible = r.getIntersection (area.toFloat());
            if (visible.getWidth() > 46.0f)
            {
                const auto w = juce::jmin (96.0f, visible.getWidth() - 10.0f);
                juce::Rectangle<float> pill (visible.getCentreX() - w * 0.5f, r.getY() + 10.0f, w, 26.0f);
                g.setColour (juce::Colour (0xf0213a5c));
                g.fillRoundedRectangle (pill, 6.0f);
                g.setColour (Theme::selLine);
                g.drawRoundedRectangle (pill, 6.0f, 1.0f);
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
                g.drawText (dbText (db, 1), pill, juce::Justification::centred);
            }
            g.setColour (juce::Colours::white.withAlpha (0.0f));
            g.fillRect (juce::Rectangle<float> (x0 - 0.75f, (float) area.getY(), 1.5f, (float) area.getHeight()));
            g.fillRect (juce::Rectangle<float> (x1 - 0.75f, (float) area.getY(), 1.5f, (float) area.getHeight()));
            for (auto x : { x0, x1 })
            {
                juce::Rectangle<float> h (x - 6.0f, (float) area.getY() + 4.0f, 12.0f, 16.0f);
                g.setColour (juce::Colours::white);
                g.fillRoundedRectangle (h, 3.0f);
            }
        }

        const auto pp = proc.getPlayPosition();
        const auto px = posToX ((double) pp);
        if (proc.isHostPlaying() || proc.isAuditioning())
        {
            g.setColour (juce::Colours::white);
            g.fillRect (juce::Rectangle<float> (px, (float) area.getY(), 1.5f, (float) area.getHeight()));
        }

        const auto cx = posToX ((double) cursor);
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.fillRect (juce::Rectangle<float> (cx, (float) area.getY(), 1.0f, (float) area.getHeight()));
    }

    paintScale (g, getLocalBounds().withTrimmedTop (rulerHeight).removeFromLeft (scaleWidth), true);
    paintScale (g, getLocalBounds().withTrimmedTop (rulerHeight).removeFromRight (scaleWidth), false);

    if (! proc.getCapture().hasData())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (19.0f));
        g.drawFittedText (proc.isCapturing() ? "Recording... press play in Ableton"
                                             : "Press Rec, then play your song in Ableton to capture the track",
                          area.reduced (40), juce::Justification::centred, 2);
    }
}

void WaveformView::paintRuler (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (Theme::bar);
    g.fillRect (r);
    g.setColour (juce::Colours::black);
    g.fillRect (r.removeFromBottom (1));

    g.setColour (juce::Colour (0xff8a919c));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("dBFS", r.withWidth (scaleWidth), juce::Justification::centred);
    g.drawText ("dBFS", r.withX (r.getRight() - scaleWidth).withWidth (scaleWidth), juce::Justification::centred);

    const auto area = waveArea();
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.withX (area.getX()).withWidth (area.getWidth()));

    const auto sr = sampleRate();
    const auto secondsPerPixel = spp / sr;
    static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
    double major = 300.0;
    for (auto s : steps)
        if (s / secondsPerPixel >= 110.0) { major = s; break; }
    const auto minor = major / 10.0;

    const auto t0 = viewStart / sr;
    const auto t1 = getViewEnd() / sr;

    g.setFont (juce::FontOptions (13.0f));
    for (auto t = std::floor (t0 / minor) * minor; t <= t1; t += minor)
    {
        const auto x = posToX (t * sr);
        const bool isMajor = std::abs (std::remainder (t, major)) < minor * 0.5;
        g.setColour (juce::Colour (isMajor ? 0xff8a919c : 0xff4a5260));
        g.fillRect (juce::Rectangle<float> (x, (float) r.getBottom() - (isMajor ? 12.0f : 6.0f), 1.0f, isMajor ? 12.0f : 6.0f));

        if (isMajor && t >= 0.0)
        {
            const int decimals = major < 0.01 ? 3 : (major < 0.1 ? 2 : 1);
            const auto scale = std::pow (10.0, decimals);
            const auto totalSec = std::round (t * scale) / scale;
            const auto mins = (int) std::floor (totalSec / 60.0 + 1e-9);
            const auto secs = std::round ((totalSec - mins * 60.0) * scale) / scale;
            auto secStr = juce::String (secs, decimals);
            if (secs < 10.0) secStr = "0" + secStr;
            g.setColour (juce::Colour (0xffc9ced6));
            g.drawText (juce::String (mins) + ":" + secStr, (int) x + 4, r.getY() + 3, 90, 18, juce::Justification::centredLeft);
        }
    }
}

static const float scaleDbs[] = { 0.0f, -1.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f, -24.0f, -30.0f, -40.0f, -50.0f };

void WaveformView::paintGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (Theme::grid);
    float lastY = -100.0f;
    for (auto db : scaleDbs)
    {
        const auto y = ampToY (juce::Decibels::decibelsToGain (db), true);
        if (y < area.getY() || y - lastY < 22.0f || area.getCentreY() - y < 24.0f) continue;
        lastY = y;
        g.fillRect ((float) area.getX(), y, (float) area.getWidth(), 1.0f);
        g.fillRect ((float) area.getX(), ampToY (juce::Decibels::decibelsToGain (db), false), (float) area.getWidth(), 1.0f);
    }
    g.setColour (juce::Colour (0xff1c2636));
    g.fillRect ((float) area.getX(), (float) area.getCentreY(), (float) area.getWidth(), 1.0f);
}

void WaveformView::paintScale (juce::Graphics& g, juce::Rectangle<int> r, bool left)
{
    g.setColour (Theme::scaleBg);
    g.fillRect (r);

    const auto thrDb = proc.getThresholdDb();
    const auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    auto label = [&] (float db) { return db == 0.0f ? juce::String ("0") : minus + juce::String ((int) -db); };

    g.setFont (juce::FontOptions (12.5f));
    float lastY = -100.0f;
    for (auto db : scaleDbs)
    {
        const auto yUp = ampToY (juce::Decibels::decibelsToGain (db), true);
        if (yUp < r.getY() + 4 || yUp - lastY < 22.0f || waveArea().getCentreY() - yUp < 24.0f) continue;
        lastY = yUp;
        for (auto y : { yUp, ampToY (juce::Decibels::decibelsToGain (db), false) })
        {
            g.setColour (Theme::scaleText);
            const auto tickX = left ? (float) r.getRight() - 7.0f : (float) r.getX();
            g.fillRect (tickX, y - 0.5f, 7.0f, 1.2f);
            auto tr = juce::Rectangle<int> (r.getX() + 4, (int) y - 9, r.getWidth() - 14, 18);
            if (! left) tr = tr.withX (r.getX() + 10);
            g.drawText (label (db), tr, left ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }
    }
    g.setColour (juce::Colour (0xff6b7a8f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e")), r.withY ((int) waveArea().getCentreY() - 9).withHeight (18).reduced (8, 0),
                left ? juce::Justification::centredRight : juce::Justification::centredLeft);

    juce::ignoreUnused (thrDb);
}

void WaveformView::paintWave (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& cap = proc.getCapture();
    if (! cap.hasData())
        return;

    const auto& s = proc.getEdits();
    const auto sr = sampleRate();
    const auto scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto step = 1.0f / scale;                      // draw at the screen's real resolution
    const auto wStart = cap.getWrittenStart(), wEnd = cap.getWrittenEnd();

    juce::Path edited, original;
    std::vector<juce::Point<float>> eTop, eBot, oTop, oBot;
    bool anyChange = false;

    auto flush = [&]
    {
        if (eTop.size() > 1)
        {
            edited.startNewSubPath (eTop.front());
            for (size_t i = 1; i < eTop.size(); ++i) edited.lineTo (eTop[i]);
            for (auto it = eBot.rbegin(); it != eBot.rend(); ++it) edited.lineTo (*it);
            edited.closeSubPath();
            original.startNewSubPath (oTop.front());
            for (size_t i = 1; i < oTop.size(); ++i) original.lineTo (oTop[i]);
            for (auto it = oBot.rbegin(); it != oBot.rend(); ++it) original.lineTo (*it);
            original.closeSubPath();
        }
        eTop.clear(); eBot.clear(); oTop.clear(); oBot.clear();
    };

    for (float x = (float) area.getX(); x < (float) area.getRight(); x += step)
    {
        const auto s0 = (int64_t) std::floor (xToPos (x));
        const auto s1 = juce::jmax (s0 + 1, (int64_t) std::floor (xToPos (x + step)));
        if (s1 < wStart || s0 >= wEnd) { flush(); continue; }

        float lo, hi;
        peaks.getRange (cap, s0, s1, lo, hi);
        const auto gain = juce::Decibels::decibelsToGain (EditOps::totalDbAt (s, (s0 + s1) / 2, sr), -120.0f);
        if (std::abs (gain - 1.0f) > 0.006f) anyChange = true;

        eTop.push_back ({ x, ampToY (hi * gain, true) });
        eBot.push_back ({ x, ampToY (-lo * gain, false) });
        oTop.push_back ({ x, ampToY (hi, true) });
        oBot.push_back ({ x, ampToY (-lo, false) });
    }
    flush();

    if (anyChange)
    {
        g.setColour (Theme::wave.withAlpha (0.25f));
        g.fillPath (original);
    }

    g.setColour (Theme::wave);
    g.fillPath (edited);

    juce::ignoreUnused (area);
}

void WaveformView::paintThreshold (juce::Graphics& g, juce::Rectangle<int> area)
{
    float dashes[] = { 6.0f, 5.0f };
    for (auto upper : { true, false })
    {
        const auto y = ampToY (thresholdGain(), upper);
        g.setColour (juce::Colours::white.withAlpha (tool == Tool::peak ? 0.85f : 0.6f));
        g.drawDashedLine (juce::Line<float> ((float) area.getX(), y, (float) area.getRight(), y), dashes, 2, 1.3f);
    }

    refreshVisiblePeaks();

    if (tool != Tool::peak)
    {
        // small white markers over every peak above the line
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        for (auto& p : visiblePeaks)
        {
            const auto x = posToX ((double) p.at);
            const auto y = juce::jmax ((float) area.getY() + 10.0f, ampToY (p.level, true) - 10.0f);
            juce::Path tri;
            tri.addTriangle (x - 5.0f, y - 8.0f, x + 5.0f, y - 8.0f, x, y);
            g.fillPath (tri);
        }
        return;
    }

    // Peak tool: a diamond on top of every peak; drag it down to lower that peak
    refreshDiamonds();
    auto selIdx = selectedSpotIndices();
    for (auto& d : diamonds)
    {
        const auto x = posToX ((double) d.at);
        const auto y = juce::jmax ((float) area.getY() + 8.0f, ampToY (d.level, true));
        const bool selected = d.spot >= 0 && std::binary_search (selIdx.begin(), selIdx.end(), d.spot);

        juce::Path dia;
        dia.addQuadrilateral (x, y - 7.0f, x + 7.0f, y, x, y + 7.0f, x - 7.0f, y);
        g.setColour (selected ? Theme::wave : juce::Colours::white.withAlpha (d.over ? 1.0f : 0.45f));
        g.fillPath (dia);
        g.setColour (selected ? juce::Colours::white : juce::Colour (0xff1c2129));
        g.strokePath (dia, juce::PathStrokeType (1.4f));

        if (selected || (d.spot >= 0 && drag == Drag::diamond))
        {
            const auto db = proc.getEdits().spots[(size_t) d.spot].db;
            juce::Rectangle<float> pill (x - 34.0f, y - 34.0f, 68.0f, 22.0f);
            g.setColour (juce::Colour (0xf0151a21));
            g.fillRoundedRectangle (pill, 6.0f);
            g.setColour (Theme::selLine);
            g.drawRoundedRectangle (pill, 6.0f, 1.0f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (dbText (db, 1), pill, juce::Justification::centred);
        }
    }
}

void WaveformView::refreshDiamonds()
{
    if (! diamondsDirty)
        return;
    diamondsDirty = false;
    diamonds.clear();

    const auto& cap = proc.getCapture();
    if (! cap.hasData())
        return;

    const auto& edits = proc.getEdits();
    const auto sr = sampleRate();
    const auto margin = (int64_t) (spp * 20.0);
    const auto v0 = (int64_t) viewStart - margin, v1 = (int64_t) getViewEnd() + margin;
    const auto thr = thresholdGain();

    // peaks that already have an edit
    for (int i = 0; i < (int) edits.spots.size(); ++i)
    {
        const auto& sp = edits.spots[(size_t) i];
        if (sp.kind == SpotEdit::blob || sp.end < v0 || sp.start > v1)
            continue;
        Diamond d;
        d.a = sp.start; d.b = sp.end; d.spot = i;
        d.level = 0.0f; d.at = (sp.start + sp.end) / 2;
        for (auto p = sp.start; p < sp.end; ++p)
        {
            const auto v = std::abs (cap.get (p)) * juce::Decibels::decibelsToGain (EditOps::totalDbAt (edits, p, sr), -120.0f);
            if (v > d.level) { d.level = v; d.at = p; }
        }
        d.over = d.level > thr;
        diamonds.push_back (d);
    }

    // peaks over the line that nobody touched yet
    for (auto& pk : visiblePeaks)
    {
        int64_t a, b;
        Analysis::peakRegion (pk, sr, a, b);
        const bool covered = std::any_of (diamonds.begin(), diamonds.end(), [&] (const Diamond& d) { return d.a < b && d.b > a; });
        if (covered)
            continue;
        Diamond d;
        d.a = a; d.b = b; d.at = pk.at; d.level = pk.level; d.spot = -1; d.over = true;
        diamonds.push_back (d);
    }
}

int WaveformView::diamondNear (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 14.0f;
    for (int i = 0; i < (int) diamonds.size(); ++i)
    {
        const auto& d = diamonds[(size_t) i];
        const juce::Point<float> c (posToX ((double) d.at), juce::jmax ((float) waveArea().getY() + 8.0f, ampToY (d.level, true)));
        const auto dist = c.getDistanceFrom (p);
        if (dist < bestDist) { bestDist = dist; best = i; }
    }
    return best;
}

void WaveformView::paintSpots (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& sp = proc.getEdits().spots;
    const auto t0 = (int64_t) viewStart, t1 = (int64_t) getViewEnd();
    auto selIdx = selectedSpotIndices();

    for (int i = 0; i < (int) sp.size(); ++i)
    {
        const auto& s = sp[(size_t) i];
        if (s.kind != SpotEdit::blob || s.end < t0 || s.start > t1) continue;   // peaks are shown as diamonds
        const bool selected = std::binary_search (selIdx.begin(), selIdx.end(), i);
        const auto x0 = posToX ((double) s.start), x1 = posToX ((double) s.end);
        juce::Rectangle<float> r (x0, (float) area.getY() + 14.0f, juce::jmax (2.0f, x1 - x0), (float) area.getHeight() - 28.0f);

        g.setColour (Theme::selFill.withAlpha (selected ? 0.32f : 0.12f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (Theme::selLine.withAlpha (selected ? 1.0f : 0.4f));
        g.drawRoundedRectangle (r, 7.0f, selected ? 1.6f : 1.0f);

        // "+3.0 dB" pill at the top of the blob
        const auto visible = r.getIntersection (area.toFloat());
        if (visible.getWidth() > 46.0f)
        {
            const auto w = juce::jmin (96.0f, visible.getWidth() - 10.0f);
            juce::Rectangle<float> pill (visible.getCentreX() - w * 0.5f, r.getY() + 10.0f, w, 26.0f);
            g.setColour (juce::Colour (selected ? 0xf0213a5c : 0xc8213a5c));
            g.fillRoundedRectangle (pill, 6.0f);
            g.setColour (Theme::selLine.withAlpha (selected ? 1.0f : 0.5f));
            g.drawRoundedRectangle (pill, 6.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (selected ? 1.0f : 0.8f));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (dbText (s.db, 1), pill, juce::Justification::centred);
        }

        if (selected)
            for (auto x : { x0, x1 })
            {
                juce::Rectangle<float> h (x - 4.0f, (float) area.getCentreY() - 9.0f, 8.0f, 18.0f);
                g.setColour (juce::Colours::white);
                g.fillRoundedRectangle (h, 3.0f);
            }
    }
}

void WaveformView::paintEnvelope (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& e = proc.getEdits().envelope;
    juce::Path line;
    const auto x0 = (float) area.getX(), x1 = (float) area.getRight();

    if (e.empty())
    {
        line.startNewSubPath (x0, dbToY (0.0f));
        line.lineTo (x1, dbToY (0.0f));
    }
    else
    {
        line.startNewSubPath (x0, dbToY (EditOps::envelopeDbAt (proc.getEdits(), (int64_t) viewStart)));
        for (auto& p : e)
        {
            const auto x = posToX ((double) p.pos);
            if (x < x0 - 2000.0f || x > x1 + 2000.0f) continue;
            line.lineTo (x, dbToY (p.db));
        }
        line.lineTo (x1, dbToY (EditOps::envelopeDbAt (proc.getEdits(), (int64_t) getViewEnd())));
    }

    g.setColour (Theme::envLine.withAlpha (tool == Tool::envelope ? 0.95f : 0.45f));
    g.strokePath (line, juce::PathStrokeType (2.0f));

    if (tool == Tool::envelope)
        for (int i = 0; i < (int) e.size(); ++i)
        {
            const auto x = posToX ((double) e[(size_t) i].pos);
            if (x < x0 - 10.0f || x > x1 + 10.0f) continue;
            juce::Rectangle<float> h (x - 5.0f, dbToY (e[(size_t) i].db) - 5.0f, 10.0f, 10.0f);
            g.setColour (i == selectedPoint ? Theme::wave : Theme::waveBg);
            g.fillRect (h);
            g.setColour (Theme::envLine);
            g.drawRect (h, 2.0f);
        }
}

//==============================================================================
// Mouse
//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    auto cursorType = juce::MouseCursor::NormalCursor;
    const auto x = e.position.x;
    const auto area = waveArea();

    if (e.position.y < rulerHeight)
        cursorType = juce::MouseCursor::PointingHandCursor;
    else if (x < area.getX() || x >= area.getRight())
        cursorType = juce::MouseCursor::UpDownResizeCursor;     // threshold
    else if (tool == Tool::range)
    {
        cursorType = juce::MouseCursor::IBeamCursor;
        if (hasSelection())
        {
            const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
            if (std::abs (x - x0) < 7.0f || std::abs (x - x1) < 7.0f) cursorType = juce::MouseCursor::LeftRightResizeCursor;
            else if (x > x0 && x < x1) cursorType = juce::MouseCursor::UpDownResizeCursor;
        }
    }
    else if (tool == Tool::envelope)
        cursorType = envPointNear (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor;
    else if (tool == Tool::blob || tool == Tool::peak || tool == Tool::select)
    {
        cursorType = juce::MouseCursor::UpDownResizeCursor;
        for (auto i : selectedSpotIndices())
        {
            const auto& b = proc.getEdits().spots[(size_t) i];
            if (std::abs (x - posToX ((double) b.start)) < 6.0f || std::abs (x - posToX ((double) b.end)) < 6.0f)
                cursorType = juce::MouseCursor::LeftRightResizeCursor;
        }
    }
    setMouseCursor (cursorType);

    if (tool == Tool::blob)
    {
        refreshSegments();
        const auto newHover = (x >= area.getX() && x < area.getRight() && e.position.y >= rulerHeight) ? (int64_t) xToPos (x) : -1;
        int64_t a0 = -1, b0 = -1, a1 = -1, b1 = -1;
        int s0, s1;
        if (hoverPos >= 0) blobTargetAt (hoverPos, a0, b0, s0);
        if (newHover >= 0) blobTargetAt (newHover, a1, b1, s1);
        hoverPos = newHover;
        if (a0 != a1 || b0 != b1)
            repaint();
    }
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    if (hoverPos >= 0) { hoverPos = -1; repaint(); }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    grabFocusForKeys();
    dragMoved = false;
    lastDragY = e.position.y;
    dragDbAccum = 0.0f;
    drag = Drag::none;

    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    const auto area = waveArea();
    const auto x = e.position.x;
    const auto pos = (int64_t) xToPos (x);

    if (e.position.y < rulerHeight)
    {
        cursor = juce::jmax ((int64_t) 0, pos);
        drag = Drag::ruler;
        repaint();
        return;
    }

    if (x < area.getX() || x >= area.getRight())
    {
        drag = Drag::threshold;
        mouseDrag (e);
        return;
    }

    const auto& edits = proc.getEdits();

    // edge handles of selected spots (all spot tools)
    if (tool == Tool::blob || tool == Tool::peak || tool == Tool::select)
        for (auto i : selectedSpotIndices())
        {
            const auto& b = edits.spots[(size_t) i];
            if (std::abs (x - posToX ((double) b.start)) < 6.0f || std::abs (x - posToX ((double) b.end)) < 6.0f)
            {
                drag = std::abs (x - posToX ((double) b.start)) < 6.0f ? Drag::spotStart : Drag::spotEnd;
                selectedSpots = { { b.start, b.end, b.kind } };
                proc.beginDrag();
                return;
            }
        }

    auto startSpotDrag = [this]
    {
        dragStartSpotDb.clear();
        for (auto i : selectedSpotIndices())
            dragStartSpotDb.push_back (proc.getEdits().spots[(size_t) i].db);
        drag = Drag::spotGain;
    };

    switch (tool)
    {
        case Tool::range:
        {
            selectedSpots.clear();
            if (hasSelection())
            {
                const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
                if (std::abs (x - x0) < 7.0f) { drag = Drag::edgeA; proc.beginDrag(); return; }
                if (std::abs (x - x1) < 7.0f) { drag = Drag::edgeB; proc.beginDrag(); return; }
                if (x > x0 && x < x1)
                {
                    proc.beginDrag();
                    auto s = edits;
                    if (activeRange < 0 || activeRange >= (int) s.ranges.size())
                    {
                        s.ranges.push_back ({ selA, selB, 0.0f });
                        activeRange = (int) s.ranges.size() - 1;
                        proc.setEdits (s, false);
                    }
                    dragStartDb = proc.getEdits().ranges[(size_t) activeRange].db;
                    drag = Drag::rangeGain;
                    return;
                }
            }
            drag = Drag::newSelection;
            dragAnchor = pos;
            selA = selB = pos;
            activeRange = -1;
            cursor = juce::jmax ((int64_t) 0, pos);
            break;
        }

        case Tool::envelope:
        {
            auto idx = envPointNear (e.position);
            proc.beginDrag();
            if (idx < 0)
            {
                auto s = edits;
                const auto db = yToDb (e.position.y);
                if (s.envelope.empty())
                {
                    const auto anchor = (int64_t) (sampleRate() * 0.15);
                    s.envelope.push_back ({ pos - anchor, 0.0f });
                    s.envelope.push_back ({ pos + anchor, 0.0f });
                }
                s.envelope.push_back ({ pos, db });
                EditOps::sortEnvelope (s);
                for (int i = 0; i < (int) s.envelope.size(); ++i)
                    if (s.envelope[(size_t) i].pos == pos) { idx = i; break; }
                proc.setEdits (s, false);
            }
            selectedPoint = idx;
            drag = Drag::envPoint;
            break;
        }

        case Tool::blob:
        {
            refreshSegments();
            selA = selB = 0;
            activeRange = -1;
            int64_t ba = 0, bb = 0;
            int existing = -1;
            if (! blobTargetAt (pos, ba, bb, existing))
            {
                // not on a blob: move the play cursor here
                if (! e.mods.isShiftDown()) selectedSpots.clear();
                cursor = juce::jmax ((int64_t) 0, pos);
                break;
            }
            proc.beginDrag();
            auto s = edits;
            const auto i = existing >= 0 ? existing : EditOps::ensureSpot (s, ba, bb, SpotEdit::blob);
            if (existing < 0)
                proc.setEdits (s, false);
            selectSpot (proc.getEdits().spots[(size_t) i], e.mods.isShiftDown() || e.mods.isCommandDown());
            startSpotDrag();
            break;
        }

        case Tool::peak:
        {
            selA = selB = 0;
            activeRange = -1;
            refreshVisiblePeaks();
            refreshDiamonds();
            const auto di = diamondNear (e.position);
            if (di < 0)
            {
                // empty spot: move the play cursor here
                if (! e.mods.isShiftDown()) selectedSpots.clear();
                cursor = juce::jmax ((int64_t) 0, pos);
                break;
            }

            const auto d = diamonds[(size_t) di];
            proc.beginDrag();
            auto s = edits;
            int idx;
            if (d.spot >= 0 && d.spot < (int) s.spots.size())
            {
                idx = d.spot;
                s.spots[(size_t) idx].kind = SpotEdit::peak;     // moved by hand: Reduction leaves it alone now
            }
            else
            {
                idx = EditOps::ensureSpot (s, d.a, d.b, SpotEdit::peak);
            }
            const auto key = s.spots[(size_t) idx];
            proc.setEdits (s, false);
            selectSpot (key, e.mods.isShiftDown() || e.mods.isCommandDown());
            dragStartSpotDb.clear();
            for (auto i : selectedSpotIndices())
                dragStartSpotDb.push_back (proc.getEdits().spots[(size_t) i].db);
            dragStartLevel = juce::jmax (1.0e-4f, d.level);
            dragStartDb = key.db;
            drag = Drag::diamond;
            break;
        }

        case Tool::select:
        default:
        {
            // all spots under the mouse, smallest first; clicking again in the same place picks the next one
            std::vector<const SpotEdit*> under;
            for (auto& sp : edits.spots)
                if (pos >= sp.start && pos < sp.end)
                    under.push_back (&sp);
            std::sort (under.begin(), under.end(), [] (auto* a, auto* b) { return a->end - a->start < b->end - b->start; });

            clickCycle = (e.position.getDistanceFrom (lastClick) < 4.0f) ? clickCycle + 1 : 0;
            lastClick = e.position;

            if (! under.empty())
            {
                selA = selB = 0;
                activeRange = -1;
                selectSpot (*under[(size_t) clickCycle % under.size()], false);
                selectedSpots = { { under[(size_t) clickCycle % under.size()]->start, under[(size_t) clickCycle % under.size()]->end,
                                    under[(size_t) clickCycle % under.size()]->kind } };
                proc.beginDrag();
                startSpotDrag();
                break;
            }

            selectedSpots.clear();
            const auto ri = rangeEditAt (pos);
            if (ri >= 0)
            {
                activeRange = ri;
                selA = edits.ranges[(size_t) ri].start;
                selB = edits.ranges[(size_t) ri].end;
                dragStartDb = edits.ranges[(size_t) ri].db;
                drag = Drag::rangeGain;
                proc.beginDrag();
            }
            else
            {
                selA = selB = 0;
                activeRange = -1;
            }
            break;
        }
    }

    changed();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 2)
        dragMoved = true;

    const auto pos = (int64_t) xToPos (e.position.x);
    const auto dy = lastDragY - e.position.y;    // up = louder
    lastDragY = e.position.y;
    dragDbAccum += dy * stepFor (e.mods);         // Option = 10x finer

    auto s = proc.getEdits();
    const auto minLen = (int64_t) (sampleRate() * 0.001);

    switch (drag)
    {
        case Drag::ruler:
            cursor = juce::jmax ((int64_t) 0, pos);
            repaint();
            return;

        case Drag::threshold:
        {
            const auto a = waveArea().toFloat();
            const auto half = (a.getHeight() * 0.5f - 10.0f) * vZoom;
            const auto amp = juce::jlimit (0.01f, 1.0f, std::abs (e.position.y - a.getCentreY()) / half);
            auto db = juce::Decibels::gainToDecibels (amp);
            db = e.mods.isAltDown() ? std::round (db * 10.0f) / 10.0f : std::round (db * 2.0f) / 2.0f;
            proc.setThresholdDb (db);
            peaksDirty = true; diamondsDirty = true;
            peakCount = -1;
            repaint();
            if (onStateChanged) onStateChanged();
            return;
        }

        case Drag::newSelection:
            selA = juce::jmin (dragAnchor, pos);
            selB = juce::jmax (dragAnchor, pos);
            break;

        case Drag::edgeA:
        case Drag::edgeB:
            if (drag == Drag::edgeA) selA = pos; else selB = pos;
            if (selA > selB) { std::swap (selA, selB); drag = (drag == Drag::edgeA ? Drag::edgeB : Drag::edgeA); }
            if (activeRange >= 0 && activeRange < (int) s.ranges.size())
            {
                s.ranges[(size_t) activeRange].start = selA;
                s.ranges[(size_t) activeRange].end = selB;
                proc.setEdits (s, false);
            }
            break;

        case Drag::rangeGain:
            if (activeRange >= 0 && activeRange < (int) s.ranges.size())
            {
                s.ranges[(size_t) activeRange].db = juce::jlimit (-48.0f, 24.0f, dragStartDb + dragDbAccum);
                proc.setEdits (s, false);
            }
            break;

        case Drag::envPoint:
            if (selectedPoint >= 0 && selectedPoint < (int) s.envelope.size())
            {
                auto& p = s.envelope[(size_t) selectedPoint];
                const auto lo = selectedPoint > 0 ? s.envelope[(size_t) selectedPoint - 1].pos + 1 : std::numeric_limits<int64_t>::min() / 2;
                const auto hi = selectedPoint + 1 < (int) s.envelope.size() ? s.envelope[(size_t) selectedPoint + 1].pos - 1 : std::numeric_limits<int64_t>::max() / 2;
                if (e.mods.isAltDown())
                    p.db = juce::jlimit (-36.0f, 12.0f, p.db + dy * 0.01f);   // fine: vertical only
                else
                {
                    p.pos = juce::jlimit (lo, hi, pos);
                    p.db = yToDb (e.position.y);
                }
                proc.setEdits (s, false);
            }
            break;

        case Drag::spotGain:
        {
            auto idx = selectedSpotIndices();
            for (size_t k = 0; k < idx.size() && k < dragStartSpotDb.size(); ++k)
                s.spots[(size_t) idx[k]].db = juce::jlimit (-48.0f, 24.0f, dragStartSpotDb[k] + dragDbAccum);
            proc.setEdits (s, false);
            break;
        }

        case Drag::diamond:
        {
            // the diamond follows the mouse: work out how many dB that is for the dragged peak
            float delta;
            if (e.mods.isAltDown())
                delta = dragDbAccum;                              // Option: 0.01 dB per pixel
            else
            {
                const auto a = waveArea().toFloat();
                const auto half = (a.getHeight() * 0.5f - 10.0f) * vZoom;
                auto amp = juce::jlimit (0.0005f, 2.0f, (a.getCentreY() - e.position.y) / half);
                if (std::abs (e.position.y - ampToY (thresholdGain(), true)) < 6.0f)
                    amp = thresholdGain() * 0.999f;               // snap to the line
                delta = juce::Decibels::gainToDecibels (amp / dragStartLevel);
            }
            auto idx = selectedSpotIndices();
            for (size_t k = 0; k < idx.size() && k < dragStartSpotDb.size(); ++k)
                s.spots[(size_t) idx[k]].db = juce::jlimit (-48.0f, 24.0f, dragStartSpotDb[k] + delta);
            proc.setEdits (s, false);
            diamondsDirty = true;
            break;
        }

        case Drag::spotStart:
        case Drag::spotEnd:
        {
            auto idx = selectedSpotIndices();
            if (idx.empty()) break;
            auto& b = s.spots[(size_t) idx.front()];
            if (drag == Drag::spotStart) b.start = juce::jmin (pos, b.end - minLen);
            else                         b.end = juce::jmax (pos, b.start + minLen);
            selectedSpots = { { b.start, b.end, b.kind } };
            EditOps::sortSpots (s);
            proc.setEdits (s, false);
            break;
        }

        case Drag::none:
        default:
            break;
    }

    changed();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    switch (drag)
    {
        case Drag::newSelection:
            if (! dragMoved || selB - selA < (int64_t) (spp * 3.0))
            {
                const auto ri = rangeEditAt ((int64_t) xToPos (e.position.x));
                if (ri >= 0)
                {
                    activeRange = ri;
                    selA = proc.getEdits().ranges[(size_t) ri].start;
                    selB = proc.getEdits().ranges[(size_t) ri].end;
                }
                else
                    selA = selB = 0;
            }
            break;
        case Drag::edgeA:
        case Drag::edgeB:     proc.commitDrag ("Resize Range"); break;
        case Drag::rangeGain: proc.commitDrag ("Range Gain"); break;
        case Drag::envPoint:  proc.commitDrag ("Envelope"); break;
        case Drag::diamond:
        case Drag::spotGain:
        case Drag::spotStart:
        case Drag::spotEnd:
        {
            // a click that changed nothing leaves no empty 0 dB spot behind
            auto s = proc.getEdits();
            const auto before = s.spots.size();
            if (drag != Drag::diamond)   // a clicked peak stays selectable (arrow keys) even at 0 dB
                EditOps::pruneSpots (s);
            if (s.spots.size() != before)
                proc.setEdits (s, false);
            proc.commitDrag (tool == Tool::peak ? "Peak Gain" : "Blob Gain");
            break;
        }
        case Drag::ruler:
        case Drag::threshold:
        case Drag::none:
        default: break;
    }

    drag = Drag::none;
    changed();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto area = waveArea();
    if (e.position.x < area.getX() || e.position.x >= area.getRight())
    {
        proc.setThresholdDb (-6.0f);   // double-click the scale: back to -6 dB
        peaksDirty = true; diamondsDirty = true;
        peakCount = -1;
        changed();
        return;
    }
    if (e.position.y < rulerHeight)
        return;

    if (tool == Tool::envelope)
    {
        const auto idx = envPointNear (e.position);
        if (idx >= 0)
        {
            auto s = proc.getEdits();
            s.envelope.erase (s.envelope.begin() + idx);
            selectedPoint = -1;
            proc.setEdits (s, true, "Delete Envelope Point");
        }
        return;
    }

    if (tool == Tool::range)
    {
        refreshSegments();
        if (auto* sg = segmentAt ((int64_t) xToPos (e.position.x)))
        {
            selA = sg->start;
            selB = sg->end;
            activeRange = -1;
            changed();
        }
    }
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        zoomBy (std::pow (2.0, (double) w.deltaY * (w.isReversed ? -2.0 : 2.0)), e.position.x);
        return;
    }
    if (e.mods.isCtrlDown())
    {
        zoomVertical (std::pow (2.0f, w.deltaY * 2.0f));
        return;
    }

    const auto delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY;
    scrollTo (viewStart + (double) delta * waveArea().getWidth() * spp * 0.5);
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomBy ((double) scaleFactor, e.position.x);
}

//==============================================================================
// OverviewStrip
//==============================================================================
OverviewStrip::OverviewStrip (GainEditorProcessor& p, PeakCache& pc, WaveformView& v)
    : proc (p), peaks (pc), view (v) {}

double OverviewStrip::totalLength() const
{
    const auto& c = proc.getCapture();
    return c.hasData() ? (double) juce::jmax (c.getWrittenEnd(), (int64_t) (c.getSampleRate() * 10.0))
                       : c.getSampleRate() * 60.0;
}

void OverviewStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (10.0f, 7.0f);
    g.setColour (Theme::waveBg);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (0xff262c35));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    const auto& cap = proc.getCapture();
    const auto total = totalLength();
    const auto spp = total / r.getWidth();
    const auto mid = r.getCentreY();
    const auto h = r.getHeight() * 0.45f;

    if (cap.hasData())
    {
        g.setColour (Theme::wave);
        for (int x = 0; x < (int) r.getWidth(); ++x)
        {
            const auto s0 = (int64_t) (x * spp), s1 = (int64_t) ((x + 1) * spp);
            if (s1 < cap.getWrittenStart() || s0 >= cap.getWrittenEnd()) continue;
            float lo, hi;
            peaks.getRange (cap, s0, s1, lo, hi);
            const auto a = juce::jmax (std::abs (lo), std::abs (hi));
            g.drawVerticalLine ((int) r.getX() + x, mid - a * h, mid + juce::jmax (1.0f, a * h));
        }
    }

    const auto vx0 = r.getX() + (float) (view.getViewStart() / total) * r.getWidth();
    const auto vx1 = r.getX() + (float) (view.getViewEnd() / total) * r.getWidth();
    juce::Rectangle<float> box (vx0, r.getY() + 2.0f, juce::jmax (6.0f, vx1 - vx0), r.getHeight() - 4.0f);
    g.setColour (juce::Colours::white);
    g.drawRoundedRectangle (box, 3.0f, 2.0f);
}

void OverviewStrip::jumpTo (float x)
{
    auto r = getLocalBounds().toFloat().reduced (10.0f, 7.0f);
    const auto centre = (double) ((x - r.getX()) / r.getWidth()) * totalLength();
    const auto width = view.getViewEnd() - view.getViewStart();
    view.scrollTo (centre - width * 0.5);
    repaint();
}

void OverviewStrip::mouseDown (const juce::MouseEvent& e) { jumpTo (e.position.x); }
void OverviewStrip::mouseDrag (const juce::MouseEvent& e) { jumpTo (e.position.x); }
