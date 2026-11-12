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
    peaksDirty = true;
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
    peaksDirty = true;
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
    peaksDirty = true;
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

    auto r = std::round (currentResolution() * 40.0f) / 40.0f;   // re-detect only when the zoom really changed
    const auto visA = (int64_t) viewStart, visB = (int64_t) getViewEnd();
    const auto gen = cap.getGeneration();

    if (std::abs (r - segRes) < 0.001f && visA >= segA && visB <= segB && gen == segGeneration)
        return;

    const auto span = visB - visA;
    segA = juce::jmax (cap.getWrittenStart(), visA - span);
    segB = juce::jmin (cap.getWrittenEnd(), visB + span);
    segRes = r;
    segGeneration = gen;
    segments = segB > segA ? Analysis::detectBlobs (cap, segA, segB, r) : std::vector<Analysis::Segment>();
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
    badge.setVisible (show);
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

    // range selection
    if (hasSelection())
    {
        const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (x0, (float) area.getY(), x1 - x0, (float) area.getHeight()));
    }

    // hovered blob
    if (tool == Tool::blob && hoverPos >= 0 && drag == Drag::none)
        if (auto* sg = segmentAt (hoverPos))
        {
            const auto x0 = posToX ((double) sg->start), x1 = posToX ((double) sg->end);
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
        paintThreshold (g, area);
        if (tool == Tool::blob || tool == Tool::peak || tool == Tool::select)
            paintSpots (g, area);
        if (tool == Tool::envelope || ! proc.getEdits().envelope.empty())
            paintEnvelope (g, area);

        if (hasSelection())
        {
            const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
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
        if (std::abs (db - thrDb) < 0.01f) continue;   // the threshold label goes there instead
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

    // threshold handles (drag them up / down)
    for (auto upper : { true, false })
    {
        const auto y = ampToY (thresholdGain(), upper);
        juce::Rectangle<float> h ((float) r.getX() + 3.0f, y - 10.0f, (float) r.getWidth() - 6.0f, 20.0f);
        g.setColour (Theme::thresh);
        g.fillRoundedRectangle (h, 5.0f);
        g.setColour (juce::Colour (0xff14161a));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label (std::round (thrDb)), h, juce::Justification::centred);
    }
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
        g.setColour (Theme::wave.withAlpha (0.28f));
        g.fillPath (original);
    }

    g.setColour (Theme::wave);
    g.fillPath (edited);

    // everything over the threshold, in orange
    const auto ty = ampToY (thresholdGain(), true), by = ampToY (thresholdGain(), false);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::RectangleList<int> over;
        over.add (area.withBottom ((int) std::floor (ty)));
        over.add (area.withTop ((int) std::ceil (by)));
        g.reduceClipRegion (over);
        g.setColour (Theme::over);
        g.fillPath (edited);
    }
}

void WaveformView::paintThreshold (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto alpha = tool == Tool::peak ? 0.95f : 0.5f;
    float dashes[] = { 7.0f, 5.0f };
    for (auto upper : { true, false })
    {
        const auto y = ampToY (thresholdGain(), upper);
        g.setColour (Theme::thresh.withAlpha (alpha));
        g.drawDashedLine (juce::Line<float> ((float) area.getX(), y, (float) area.getRight(), y), dashes, 2, 1.5f);
    }

    if (tool == Tool::peak)
    {
        refreshVisiblePeaks();
        g.setColour (Theme::over);
        for (auto& p : visiblePeaks)
        {
            const auto x = posToX ((double) p.at);
            const auto y = ampToY (p.level, true) - 6.0f;
            juce::Path tri;
            tri.addTriangle (x - 5.0f, juce::jmax ((float) area.getY() + 2.0f, y - 8.0f), x + 5.0f,
                             juce::jmax ((float) area.getY() + 2.0f, y - 8.0f), x, juce::jmax ((float) area.getY() + 9.0f, y - 1.0f));
            g.fillPath (tri);
        }
    }
}

void WaveformView::paintSpots (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& sp = proc.getEdits().spots;
    const auto t0 = (int64_t) viewStart, t1 = (int64_t) getViewEnd();
    auto selIdx = selectedSpotIndices();
    float dashes[] = { 6.0f, 4.0f };

    for (int i = 0; i < (int) sp.size(); ++i)
    {
        const auto& s = sp[(size_t) i];
        if (s.end < t0 || s.start > t1) continue;
        const bool selected = std::binary_search (selIdx.begin(), selIdx.end(), i);
        const auto x0 = posToX ((double) s.start), x1 = posToX ((double) s.end);
        const auto inset = s.kind == SpotEdit::peak ? 14.0f : 6.0f;
        juce::Rectangle<float> r (x0, (float) area.getY() + inset, juce::jmax (2.0f, x1 - x0), (float) area.getHeight() - inset * 2.0f);

        juce::Path outline;
        outline.addRoundedRectangle (r, 6.0f);
        if (selected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colours::white);
            g.strokePath (outline, juce::PathStrokeType (1.6f));
        }
        else
        {
            juce::Path dashed;
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour ((s.kind == SpotEdit::peak ? Theme::thresh : Theme::spot).withAlpha (0.85f));
            g.fillPath (dashed);
        }

        // label with a small lock: this edit stays where it is when you zoom
        const auto visibleW = juce::jmin (r.getRight(), (float) area.getRight()) - juce::jmax (r.getX(), (float) area.getX());
        if (visibleW > 84.0f)
        {
            // bottom-left of the visible part (the top corners hold the gain badge and the peak bar)
            juce::Rectangle<float> tag (juce::jmax (r.getX(), (float) area.getX()) + 6.0f, r.getBottom() - 26.0f, 74.0f, 20.0f);
            g.setColour (s.kind == SpotEdit::peak ? Theme::thresh : Theme::spotFill);
            g.fillRoundedRectangle (tag, 5.0f);
            const auto fg = s.kind == SpotEdit::peak ? juce::Colour (0xff14161a) : juce::Colours::white;
            g.setColour (fg);
            juce::Rectangle<float> body (tag.getX() + 7.0f, tag.getY() + 9.0f, 9.0f, 7.0f);
            g.fillRoundedRectangle (body, 1.5f);
            juce::Path shackle;
            shackle.addCentredArc (body.getCentreX(), body.getY(), 3.0f, 3.5f, 0.0f, -juce::MathConstants<float>::halfPi,
                                   juce::MathConstants<float>::halfPi, true);
            g.strokePath (shackle, juce::PathStrokeType (1.5f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            auto t = dbText (s.db, 1);
            g.drawText (t.upToFirstOccurrenceOf (" dB", false, false), tag.withTrimmedLeft (20.0f), juce::Justification::centredLeft);
        }

        if (selected)
            for (auto x : { x0, x1 })
            {
                juce::Rectangle<float> h (x - 5.0f, (float) area.getCentreY() - 5.0f, 10.0f, 10.0f);
                g.setColour (juce::Colours::white);
                g.fillRect (h);
                g.setColour (Theme::spotFill);
                g.drawRect (h, 2.0f);
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
        auto* before = hoverPos >= 0 ? segmentAt (hoverPos) : nullptr;
        auto* after = newHover >= 0 ? segmentAt (newHover) : nullptr;
        hoverPos = newHover;
        if (before != after)
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
            auto* sg = segmentAt (pos);
            if (sg == nullptr)
            {
                if (! e.mods.isShiftDown()) selectedSpots.clear();
                break;
            }
            proc.beginDrag();
            auto s = edits;
            const auto i = EditOps::ensureSpot (s, sg->start, sg->end, SpotEdit::blob);
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
            if (auto* pk = peakNear (pos))
            {
                int64_t a, b;
                Analysis::peakRegion (*pk, sampleRate(), a, b);
                proc.beginDrag();
                auto s = edits;
                const auto i = EditOps::ensureSpot (s, a, b, SpotEdit::peak);
                proc.setEdits (s, false);
                selectSpot (proc.getEdits().spots[(size_t) i], e.mods.isShiftDown() || e.mods.isCommandDown());
                startSpotDrag();
                break;
            }
            // not on a peak: pick an existing peak spot under the mouse, if any
            for (auto& sp : edits.spots)
                if (sp.kind == SpotEdit::peak && pos >= sp.start && pos < sp.end)
                {
                    selectSpot (sp, e.mods.isShiftDown());
                    proc.beginDrag();
                    startSpotDrag();
                    break;
                }
            if (drag == Drag::none && ! e.mods.isShiftDown())
                selectedSpots.clear();
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
            peaksDirty = true;
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
        case Drag::spotGain:
        case Drag::spotStart:
        case Drag::spotEnd:
        {
            // a click that changed nothing leaves no empty 0 dB spot behind
            auto s = proc.getEdits();
            const auto before = s.spots.size();
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
        peaksDirty = true;
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
