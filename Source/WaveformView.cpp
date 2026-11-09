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
    setSize (196, 44);
}

void GainBadge::setDb (float db)
{
    if (std::abs (db - value) > 0.0001f) { value = db; repaint(); }
}

void GainBadge::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 3), 10.0f);
    g.setColour (juce::Colour (0xff121418));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colour (0xff3a3f47));
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    juce::String txt = (value > 0.0f ? "+" : (value < 0.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()))
                       + juce::String (std::abs (value), std::abs (value) < 10.0f ? 2 : 1) + " dB";
    g.drawText (txt, 12, 0, 92, getHeight(), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff3a3f47));
    g.fillRect (106, 8, 1, getHeight() - 16);
}

void GainBadge::resized()
{
    resetButton.setBounds (114, 6, getWidth() - 120, getHeight() - 12);
}

//==============================================================================
// WaveformView
//==============================================================================
WaveformView::WaveformView (GainEditorProcessor& p, PeakCache& pc) : proc (p), peaks (pc)
{
    addChildComponent (badge);
    badge.onReset = [this]
    {
        // Envelope point badge: Reset removes that point. Otherwise: reset the selected part.
        if (tool == Tool::envelope && selectedPoint >= 0)
            deleteSelected();
        else
            resetTarget (EditOps::resetEverything);
    };
    proc.addChangeListener (this);
    setWantsKeyboardFocus (false);
}

WaveformView::~WaveformView()
{
    proc.removeChangeListener (this);
}

void WaveformView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto& r = proc.getEdits().ranges;
    if (activeRange >= (int) r.size())
        activeRange = -1;
    if (selectedPoint >= (int) proc.getEdits().envelope.size())
        selectedPoint = -1;

    // forget selected blobs that no longer exist
    for (auto it = selectedBlobStarts.begin(); it != selectedBlobStarts.end();)
    {
        const auto& bl = proc.getEdits().blobs;
        const bool exists = std::any_of (bl.begin(), bl.end(), [&] (const Blob& b) { return b.start == *it; });
        it = exists ? std::next (it) : selectedBlobStarts.erase (it);
    }

    updateBadge();
    repaint();
}

juce::Rectangle<int> WaveformView::waveArea() const
{
    return getLocalBounds().withTrimmedTop (rulerHeight);
}

void WaveformView::resized()
{
    updateBadge();
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
    updateBadge();
    repaint();
    if (onStateChanged) onStateChanged();
}

void WaveformView::zoomBy (double factor, double anchorX)
{
    if (anchorX < 0.0)
        anchorX = getWidth() * 0.5;
    const auto anchorPos = xToPos ((float) anchorX);
    spp = juce::jlimit (0.05, 200000.0, spp / factor);
    viewStart = anchorPos - anchorX * spp;
    scrollTo (viewStart);
}

void WaveformView::zoomToFit()
{
    const auto& c = proc.getCapture();
    if (! c.hasData() || getWidth() <= 0)
        return;
    const auto a = (double) c.getWrittenStart(), b = (double) c.getWrittenEnd();
    spp = juce::jmax (0.05, (b - a) / (getWidth() * 0.98));
    scrollTo (a - (b - a) * 0.01);
}

void WaveformView::zoomToSelection()
{
    int64_t a, b;
    if (! getTargetRegion (a, b) || getWidth() <= 0)
        return;
    const auto len = (double) (b - a);
    spp = juce::jmax (0.05, len * 1.2 / getWidth());
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
    updateBadge();
    repaint();
    if (onStateChanged) onStateChanged();
}

void WaveformView::followPlayhead (int64_t pos)
{
    const auto x = posToX ((double) pos);
    if (x > getWidth() * 0.92f || x < 0.0f)
        scrollTo ((double) pos - getWidth() * 0.08 * spp);
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

int WaveformView::blobIndexAt (int64_t pos) const
{
    const auto& bl = proc.getEdits().blobs;
    auto it = std::upper_bound (bl.begin(), bl.end(), pos, [] (int64_t p, const Blob& b) { return p < b.start; });
    if (it == bl.begin())
        return -1;
    --it;
    return pos < it->end ? (int) std::distance (bl.begin(), it) : -1;
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

std::vector<int> WaveformView::selectedBlobIndices() const
{
    std::vector<int> out;
    const auto& bl = proc.getEdits().blobs;
    for (int i = 0; i < (int) bl.size(); ++i)
        if (selectedBlobStarts.count (bl[(size_t) i].start) > 0)
            out.push_back (i);
    return out;
}

bool WaveformView::getTargetRegion (int64_t& a, int64_t& b) const
{
    if (hasSelection())
    {
        a = selA; b = selB;
        return true;
    }
    auto idx = selectedBlobIndices();
    if (! idx.empty())
    {
        const auto& bl = proc.getEdits().blobs;
        a = bl[(size_t) idx.front()].start;
        b = bl[(size_t) idx.back()].end;
        return true;
    }
    return false;
}

void WaveformView::clearSelection()
{
    selA = selB = 0;
    activeRange = -1;
    selectedBlobStarts.clear();
    selectedPoint = -1;
    updateBadge();
    repaint();
    if (onStateChanged) onStateChanged();
}

//==============================================================================
// Commands
//==============================================================================
void WaveformView::resetTarget (EditOps::ResetWhat what)
{
    int64_t a, b;
    if (! getTargetRegion (a, b))
        return;

    static const char* names[] = { "Reset Selection", "Reset Range Gain", "Reset Envelope", "Reset Blobs" };
    auto s = EditOps::resetRegion (proc.getEdits(), a, b, what, sampleRate());

    // If the selection was exactly one range edit, that edit is gone now.
    activeRange = -1;
    proc.setEdits (s, true, names[(int) what]);
}

void WaveformView::resetEverythingWithConfirm()
{
    juce::Component::SafePointer<WaveformView> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Reset All Edits")
                                      .withMessage ("Remove every gain change (Range, Envelope and Blob) from the whole take?\nYou can still undo this.")
                                      .withButton ("Reset All")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [safe] (int result)
                                  {
                                      if (safe == nullptr || result != 1)
                                          return;
                                      safe->activeRange = -1;
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

    auto blobIdx = selectedBlobIndices();
    if (! blobIdx.empty() && (tool == Tool::blob || tool == Tool::select))
    {
        for (auto i : blobIdx)
            s.blobs[(size_t) i].db = juce::jlimit (-48.0f, 24.0f, s.blobs[(size_t) i].db + db);
        proc.setEdits (s, true, "Nudge Blob Gain");
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

void WaveformView::mergeSelectedBlobs()
{
    auto idx = selectedBlobIndices();
    if (idx.size() < 2)
        return;

    auto s = proc.getEdits();
    const auto first = idx.front(), last = idx.back();
    Blob m = s.blobs[(size_t) first];
    m.end = s.blobs[(size_t) last].end;
    s.blobs.erase (s.blobs.begin() + first, s.blobs.begin() + last + 1);
    s.blobs.insert (s.blobs.begin() + first, m);
    selectedBlobStarts = { m.start };
    proc.setEdits (s, true, "Merge Blobs");
}

void WaveformView::applyResolutionToSelection (float resolution)
{
    int64_t a, b;
    if (! getTargetRegion (a, b))
        return;
    selectedBlobStarts.clear();
    proc.redetectBlobs (resolution, a, b);
}

void WaveformView::showContextMenu()
{
    const bool target = hasSelection() || ! selectedBlobStarts.empty();
    juce::PopupMenu m;
    m.addSectionHeader ("Reset");
    m.addItem (1, "Reset Selection  (Cmd+R)", target);
    m.addItem (2, "Reset Range Gain Only", target);
    m.addItem (3, "Reset Envelope in Selection", target);
    m.addItem (4, "Reset Blobs in Selection", target);
    m.addSeparator();
    m.addItem (5, "Reset All Edits...  (Shift+Cmd+R)");
    m.addSeparator();
    m.addItem (6, "Show Loudness  (L)", target);
    m.addItem (7, "Zoom to Selection  (Z)", target);
    if (tool == Tool::blob)
    {
        m.addSeparator();
        m.addItem (8, "Merge Selected Blobs  (M)", selectedBlobStarts.size() > 1);
    }
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
                             case 4: safe->resetTarget (EditOps::resetBlobs); break;
                             case 5: safe->resetEverythingWithConfirm(); break;
                             case 6: if (safe->onShowLoudness) safe->onShowLoudness(); break;
                             case 7: safe->zoomToSelection(); break;
                             case 8: safe->mergeSelectedBlobs(); break;
                             case 9: safe->proc.getUndoManager().undo(); break;
                             case 10: safe->proc.getUndoManager().redo(); break;
                             default: break;
                         }
                     });
}

//==============================================================================
// Badge
//==============================================================================
void WaveformView::updateBadge()
{
    const auto& s = proc.getEdits();
    float db = 0.0f;
    float centreX = 0.0f, top = 0.0f;
    bool show = false;
    const auto a = waveArea().toFloat();

    if (tool == Tool::envelope && selectedPoint >= 0 && selectedPoint < (int) s.envelope.size())
    {
        const auto& p = s.envelope[(size_t) selectedPoint];
        db = p.db;
        centreX = posToX ((double) p.pos);
        top = dbToY (p.db) + 18.0f;
        show = true;
    }
    else if ((tool == Tool::blob || tool == Tool::select) && ! selectedBlobStarts.empty())
    {
        auto idx = selectedBlobIndices();
        if (! idx.empty())
        {
            const auto& f = s.blobs[(size_t) idx.front()];
            const auto& l = s.blobs[(size_t) idx.back()];
            db = f.db;
            centreX = (posToX ((double) f.start) + posToX ((double) l.end)) * 0.5f;
            top = a.getCentreY() + a.getHeight() * 0.18f;
            show = true;
        }
    }
    else if (hasSelection())
    {
        db = (activeRange >= 0 && activeRange < (int) s.ranges.size()) ? s.ranges[(size_t) activeRange].db : 0.0f;
        centreX = (posToX ((double) selA) + posToX ((double) selB)) * 0.5f;
        top = a.getY() + a.getHeight() * 0.18f;
        show = true;
    }

    if (show && centreX > -100.0f && centreX < (float) getWidth() + 100.0f)
    {
        badge.setDb (db);
        auto x = juce::jlimit (4, juce::jmax (4, getWidth() - badge.getWidth() - 4), (int) (centreX - badge.getWidth() * 0.5f));
        auto y = juce::jlimit ((int) a.getY() + 4, juce::jmax ((int) a.getY() + 4, (int) a.getBottom() - badge.getHeight() - 4), (int) top);
        badge.setTopLeftPosition (x, y);
        badge.setVisible (true);
    }
    else
    {
        badge.setVisible (false);
    }
}

//==============================================================================
// Painting
//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    paintRuler (g, getLocalBounds().withHeight (rulerHeight));

    const auto area = waveArea();
    g.setColour (Theme::wave);
    g.fillRect (area);

    paintSelection (g, area);
    paintWave (g, area);
    if (tool == Tool::blob)
        paintBlobs (g, area);
    if (tool == Tool::envelope || ! proc.getEdits().envelope.empty())
        paintEnvelope (g, area);

    // selection edges + handles on top of the waveform
    if (hasSelection())
    {
        const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (x0 - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight()));
        g.fillRect (juce::Rectangle<float> (x1 - 1.0f, (float) area.getY(), 2.0f, (float) area.getHeight()));
        for (auto x : { x0, x1 })
            for (auto y : { (float) area.getY() + 4.0f, (float) area.getBottom() - 22.0f })
            {
                juce::Rectangle<float> h (x - 7.0f, y, 14.0f, 18.0f);
                g.setColour (juce::Colours::white);
                g.fillRoundedRectangle (h, 3.0f);
                g.setColour (juce::Colour (0xff555555));
                g.drawRoundedRectangle (h, 3.0f, 1.0f);
                g.drawLine (x - 2.0f, y + 5.0f, x - 2.0f, y + 13.0f, 1.2f);
                g.drawLine (x + 2.0f, y + 5.0f, x + 2.0f, y + 13.0f, 1.2f);
            }
    }

    // play position
    const auto pp = proc.getPlayPosition();
    const auto px = posToX ((double) pp);
    if (px >= 0.0f && px < (float) getWidth() && (proc.isHostPlaying() || proc.isAuditioning()))
    {
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (px, (float) area.getY(), 1.5f, (float) area.getHeight()));
    }

    // cursor (where Play starts when nothing is selected)
    const auto cx = posToX ((double) cursor);
    if (cx >= 0.0f && cx < (float) getWidth())
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (cx, (float) area.getY(), 1.0f, (float) area.getHeight()));
    }

    if (! proc.getCapture().hasData())
    {
        g.setColour (Theme::ink.withAlpha (0.7f));
        g.setFont (juce::FontOptions (20.0f));
        g.drawFittedText (proc.isCapturing() ? "Recording... press play in Ableton"
                                             : "Press Record, then play your song in Ableton to capture the track",
                          area.reduced (40), juce::Justification::centred, 2);
    }
}

void WaveformView::paintRuler (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (Theme::bar);
    g.fillRect (r);

    const auto sr = sampleRate();
    const auto secondsPerPixel = spp / sr;
    static const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
    double major = 300.0;
    for (auto s : steps)
        if (s / secondsPerPixel >= 110.0) { major = s; break; }
    const auto minor = major / 10.0;

    const auto t0 = viewStart / sr;
    const auto t1 = (viewStart + spp * r.getWidth()) / sr;

    g.setFont (juce::FontOptions (14.0f));
    for (auto t = std::floor (t0 / minor) * minor; t <= t1; t += minor)
    {
        const auto x = posToX (t * sr);
        const bool isMajor = std::abs (std::remainder (t, major)) < minor * 0.5;
        g.setColour (juce::Colour (0xff8a919c));
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
            g.setColour (juce::Colour (0xffd7dbe0));
            g.drawText (juce::String (mins) + ":" + secStr, (int) x - 50, r.getY() + 4, 100, 20, juce::Justification::centred);
        }
    }
}

void WaveformView::paintSelection (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (! hasSelection())
        return;
    const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
    g.setColour (Theme::waveSel);
    g.fillRect (juce::Rectangle<float> (x0, (float) area.getY(), x1 - x0, (float) area.getHeight()));
}

void WaveformView::paintWave (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& cap = proc.getCapture();
    if (! cap.hasData())
        return;

    const auto& s = proc.getEdits();
    const auto sr = sampleRate();
    const auto mid = (float) area.getCentreY();
    const auto halfH = area.getHeight() * 0.46f * vZoom;
    const auto clipTop = (float) area.getY(), clipBottom = (float) area.getBottom();

    // which columns belong to a selected blob (drawn light)
    std::vector<std::pair<int64_t, int64_t>> lightRegions;
    if (tool == Tool::blob || tool == Tool::select)
        for (auto i : selectedBlobIndices())
            lightRegions.push_back ({ s.blobs[(size_t) i].start, s.blobs[(size_t) i].end });

    const auto wStart = cap.getWrittenStart(), wEnd = cap.getWrittenEnd();

    for (int x = 0; x < area.getWidth(); ++x)
    {
        const auto s0 = (int64_t) std::floor (xToPos ((float) x));
        const auto s1 = (int64_t) std::floor (xToPos ((float) x + 1.0f));
        if (s1 < wStart || s0 >= wEnd)
            continue;

        float lo, hi;
        peaks.getRange (cap, s0, juce::jmax (s1, s0 + 1), lo, hi);
        const auto centre = (s0 + s1) / 2;
        const auto gain = juce::Decibels::decibelsToGain (EditOps::totalDbAt (s, centre, sr), -120.0f);

        auto yTop = [&] (float v) { return juce::jlimit (clipTop, clipBottom, mid - v * halfH); };

        const bool changed = std::abs (gain - 1.0f) > 0.006f;
        if (changed)
        {
            g.setColour (Theme::ink.withAlpha (0.18f));
            g.drawVerticalLine (x, yTop (hi), juce::jmax (yTop (hi) + 1.0f, yTop (lo)));
        }

        bool light = false;
        for (auto& lr : lightRegions)
            if (centre >= lr.first && centre < lr.second) { light = true; break; }

        g.setColour (light ? Theme::blobSel : Theme::ink);
        const auto top = yTop (hi * gain), bottom = yTop (lo * gain);
        g.drawVerticalLine (x, top, juce::jmax (top + 1.0f, bottom));
    }
}

void WaveformView::paintBlobs (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& bl = proc.getEdits().blobs;
    const auto mid = (float) area.getCentreY();
    const auto t0 = (int64_t) viewStart, t1 = (int64_t) getViewEnd();

    auto it = std::upper_bound (bl.begin(), bl.end(), t0, [] (int64_t p, const Blob& b) { return p < b.start; });
    if (it != bl.begin()) --it;

    float lastX = -1000.0f;
    for (; it != bl.end() && it->start < t1; ++it)
    {
        const auto x0 = posToX ((double) it->start);
        const auto x1 = posToX ((double) it->end);

        // boundary ticks so the segmentation is visible
        if (x0 - lastX > 3.0f)
        {
            g.setColour (Theme::ink.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (x0, mid - 10.0f, 1.0f, 20.0f));
        }
        lastX = x0;

        // small dB label on blobs that have a gain change (when there is room)
        if (it->db != 0.0f && x1 - x0 > 46.0f)
        {
            g.setColour (Theme::ink);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ((it->db > 0 ? "+" : "") + juce::String (it->db, 1), (int) x0, area.getBottom() - 26, (int) (x1 - x0), 18,
                        juce::Justification::centred);
        }

        // handles on selected blobs
        if (selectedBlobStarts.count (it->start) > 0)
        {
            for (auto x : { x0, x1 })
            {
                juce::Rectangle<float> h (x - 6.0f, mid - 6.0f, 12.0f, 12.0f);
                g.setColour (juce::Colours::white);
                g.fillRect (h);
                g.setColour (Theme::env);
                g.drawRect (h, 2.0f);
            }
        }
    }
}

void WaveformView::paintEnvelope (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& e = proc.getEdits().envelope;
    juce::Path line;
    const auto w = (float) getWidth();

    if (e.empty())
    {
        line.startNewSubPath (0.0f, dbToY (0.0f));
        line.lineTo (w, dbToY (0.0f));
    }
    else
    {
        line.startNewSubPath (0.0f, dbToY (EditOps::envelopeDbAt (proc.getEdits(), (int64_t) viewStart)));
        for (auto& p : e)
        {
            const auto x = posToX ((double) p.pos);
            if (x < -2000.0f || x > w + 2000.0f) continue;
            line.lineTo (x, dbToY (p.db));
        }
        line.lineTo (w, dbToY (EditOps::envelopeDbAt (proc.getEdits(), (int64_t) getViewEnd())));
    }

    g.saveState();
    g.reduceClipRegion (area);
    g.setColour (Theme::env.withAlpha (tool == Tool::envelope ? 1.0f : 0.55f));
    g.strokePath (line, juce::PathStrokeType (2.2f));

    if (tool == Tool::envelope)
    {
        for (int i = 0; i < (int) e.size(); ++i)
        {
            const auto x = posToX ((double) e[(size_t) i].pos);
            if (x < -10.0f || x > w + 10.0f) continue;
            juce::Rectangle<float> h (x - 5.0f, dbToY (e[(size_t) i].db) - 5.0f, 10.0f, 10.0f);
            g.setColour (i == selectedPoint ? Theme::env : juce::Colours::white);
            g.fillRect (h);
            g.setColour (Theme::env);
            g.drawRect (h, 2.0f);
        }
    }
    g.restoreState();
}

//==============================================================================
// Mouse
//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    auto cursorType = juce::MouseCursor::NormalCursor;
    const auto x = e.position.x;

    if (e.position.y < rulerHeight)
        cursorType = juce::MouseCursor::PointingHandCursor;
    else if (tool == Tool::range && hasSelection())
    {
        const auto x0 = posToX ((double) selA), x1 = posToX ((double) selB);
        if (std::abs (x - x0) < 7.0f || std::abs (x - x1) < 7.0f)
            cursorType = juce::MouseCursor::LeftRightResizeCursor;
        else if (x > x0 && x < x1)
            cursorType = juce::MouseCursor::UpDownResizeCursor;
        else
            cursorType = juce::MouseCursor::IBeamCursor;
    }
    else if (tool == Tool::range)
        cursorType = juce::MouseCursor::IBeamCursor;
    else if (tool == Tool::envelope)
        cursorType = envPointNear (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor;
    else if (tool == Tool::blob)
    {
        cursorType = juce::MouseCursor::UpDownResizeCursor;
        for (auto i : selectedBlobIndices())
        {
            const auto& b = proc.getEdits().blobs[(size_t) i];
            if (std::abs (x - posToX ((double) b.start)) < 7.0f || std::abs (x - posToX ((double) b.end)) < 7.0f)
                cursorType = juce::MouseCursor::LeftRightResizeCursor;
        }
    }
    setMouseCursor (cursorType);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    dragMoved = false;
    lastDragY = e.position.y;
    dragDbAccum = 0.0f;
    drag = Drag::none;

    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    const auto pos = (int64_t) xToPos (e.position.x);
    const auto x = e.position.x;

    // Ruler: set the cursor (where Play starts)
    if (e.position.y < rulerHeight)
    {
        cursor = juce::jmax ((int64_t) 0, pos);
        drag = Drag::ruler;
        repaint();
        return;
    }

    auto& edits = proc.getEdits();

    switch (tool)
    {
        case Tool::range:
        {
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
            selectedBlobStarts.clear();
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
                    // first point: add 0 dB anchors around it so only this spot changes
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
        case Tool::select:
        {
            // blob edge handles
            if (tool == Tool::blob)
            {
                for (auto i : selectedBlobIndices())
                {
                    const auto& b = edits.blobs[(size_t) i];
                    if (std::abs (x - posToX ((double) b.start)) < 7.0f) { drag = Drag::blobStart; dragAnchor = b.start; proc.beginDrag(); return; }
                    if (std::abs (x - posToX ((double) b.end)) < 7.0f)   { drag = Drag::blobEnd;   dragAnchor = b.start; proc.beginDrag(); return; }
                }
            }

            const auto bi = blobIndexAt (pos);
            if (tool == Tool::select && bi < 0)
            {
                const auto ri = rangeEditAt (pos);
                selectedBlobStarts.clear();
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

            if (bi < 0)
            {
                if (! e.mods.isShiftDown())
                    selectedBlobStarts.clear();
                selA = selB = 0;
                activeRange = -1;
                break;
            }

            const auto start = edits.blobs[(size_t) bi].start;
            if (e.mods.isShiftDown() || e.mods.isCommandDown())
            {
                if (selectedBlobStarts.count (start) > 0) selectedBlobStarts.erase (start);
                else selectedBlobStarts.insert (start);
            }
            else if (selectedBlobStarts.count (start) == 0)
            {
                selectedBlobStarts = { start };
            }
            selA = selB = 0;
            activeRange = -1;

            dragStartBlobDb.clear();
            for (auto i : selectedBlobIndices())
                dragStartBlobDb.push_back (edits.blobs[(size_t) i].db);
            drag = Drag::blobGain;
            proc.beginDrag();
            break;
        }
    }

    updateBadge();
    repaint();
    if (onStateChanged) onStateChanged();
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

        case Drag::newSelection:
            selA = juce::jmin (dragAnchor, pos);
            selB = juce::jmax (dragAnchor, pos);
            break;

        case Drag::edgeA:
        case Drag::edgeB:
        {
            if (drag == Drag::edgeA) selA = pos; else selB = pos;
            if (selA > selB) { std::swap (selA, selB); drag = (drag == Drag::edgeA ? Drag::edgeB : Drag::edgeA); }
            if (activeRange >= 0 && activeRange < (int) s.ranges.size())
            {
                s.ranges[(size_t) activeRange].start = selA;
                s.ranges[(size_t) activeRange].end = selB;
                proc.setEdits (s, false);
            }
            break;
        }

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
                p.pos = juce::jlimit (lo, hi, pos);
                if (e.mods.isAltDown())
                    p.db = juce::jlimit (-36.0f, 12.0f, p.db + dy * 0.01f);   // fine vertical only
                else
                    p.db = yToDb (e.position.y);
                proc.setEdits (s, false);
            }
            break;

        case Drag::blobGain:
        {
            auto idx = selectedBlobIndices();
            for (size_t k = 0; k < idx.size() && k < dragStartBlobDb.size(); ++k)
                s.blobs[(size_t) idx[k]].db = juce::jlimit (-48.0f, 24.0f, dragStartBlobDb[k] + dragDbAccum);
            proc.setEdits (s, false);
            break;
        }

        case Drag::blobStart:
        case Drag::blobEnd:
        {
            int bi = -1;
            for (int i = 0; i < (int) s.blobs.size(); ++i)
                if (s.blobs[(size_t) i].start == dragAnchor) { bi = i; break; }
            if (bi < 0) break;

            auto& b = s.blobs[(size_t) bi];
            if (drag == Drag::blobStart)
            {
                const auto lo = bi > 0 ? s.blobs[(size_t) bi - 1].start + minLen : (int64_t) 0;
                b.start = juce::jlimit (lo, b.end - minLen, pos);
                if (bi > 0 && s.blobs[(size_t) bi - 1].end > b.start)
                    s.blobs[(size_t) bi - 1].end = b.start;        // neighbour shrinks
                selectedBlobStarts.erase (dragAnchor);
                selectedBlobStarts.insert (b.start);
                dragAnchor = b.start;
            }
            else
            {
                const auto hi = bi + 1 < (int) s.blobs.size() ? s.blobs[(size_t) bi + 1].end - minLen : std::numeric_limits<int64_t>::max() / 2;
                b.end = juce::jlimit (b.start + minLen, hi, pos);
                if (bi + 1 < (int) s.blobs.size() && s.blobs[(size_t) bi + 1].start < b.end)
                {
                    auto& n = s.blobs[(size_t) bi + 1];
                    const bool wasSel = selectedBlobStarts.count (n.start) > 0;
                    selectedBlobStarts.erase (n.start);
                    n.start = b.end;
                    if (wasSel) selectedBlobStarts.insert (n.start);
                }
            }
            proc.setEdits (s, false);
            break;
        }

        case Drag::none:
        default:
            break;
    }

    updateBadge();
    repaint();
    if (onStateChanged) onStateChanged();
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    switch (drag)
    {
        case Drag::newSelection:
            if (! dragMoved || selB - selA < (int64_t) (spp * 3.0))
            {
                // a click: select the range edit under the mouse, if any
                const auto pos = (int64_t) xToPos (e.position.x);
                const auto ri = rangeEditAt (pos);
                if (ri >= 0)
                {
                    activeRange = ri;
                    selA = proc.getEdits().ranges[(size_t) ri].start;
                    selB = proc.getEdits().ranges[(size_t) ri].end;
                }
                else
                {
                    selA = selB = 0;
                }
            }
            break;
        case Drag::edgeA:
        case Drag::edgeB:    proc.commitDrag ("Resize Range"); break;
        case Drag::rangeGain: proc.commitDrag ("Range Gain"); break;
        case Drag::envPoint:  proc.commitDrag ("Envelope"); break;
        case Drag::blobGain:  proc.commitDrag ("Blob Gain"); break;
        case Drag::blobStart:
        case Drag::blobEnd:   proc.commitDrag ("Resize Blob"); break;
        case Drag::ruler:
        case Drag::none:
        default: break;
    }

    drag = Drag::none;
    updateBadge();
    repaint();
    if (onStateChanged) onStateChanged();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.y < rulerHeight)
        return;

    const auto pos = (int64_t) xToPos (e.position.x);
    auto s = proc.getEdits();

    if (tool == Tool::envelope)
    {
        const auto idx = envPointNear (e.position);
        if (idx >= 0)
        {
            s.envelope.erase (s.envelope.begin() + idx);
            selectedPoint = -1;
            proc.setEdits (s, true, "Delete Envelope Point");
        }
        return;
    }

    if (tool == Tool::blob)
    {
        const auto bi = blobIndexAt (pos);
        if (bi < 0) return;
        auto b = s.blobs[(size_t) bi];
        const auto minLen = (int64_t) (sampleRate() * 0.001);
        if (pos - b.start < minLen || b.end - pos < minLen)
            return;
        s.blobs[(size_t) bi].end = pos;
        s.blobs.insert (s.blobs.begin() + bi + 1, Blob { pos, b.end, b.db });
        selectedBlobStarts = { pos };
        proc.setEdits (s, true, "Split Blob");
        return;
    }

    if (tool == Tool::range)
    {
        // double-click selects the blob-sized phrase under the mouse, handy for quick fixes
        const auto bi = blobIndexAt (pos);
        if (bi >= 0)
        {
            selA = s.blobs[(size_t) bi].start;
            selB = s.blobs[(size_t) bi].end;
            activeRange = -1;
            updateBadge();
            repaint();
            if (onStateChanged) onStateChanged();
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

    auto delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY;
    if (e.mods.isShiftDown() && std::abs (w.deltaX) < 1e-6f)
        delta = -w.deltaY;
    scrollTo (viewStart + (double) delta * getWidth() * spp * 0.5);
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
    auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
    g.setColour (juce::Colour (0xff121418));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (0xff3a3f47));
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
    auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
    const auto total = totalLength();
    const auto centre = (double) ((x - r.getX()) / r.getWidth()) * total;
    const auto width = view.getViewEnd() - view.getViewStart();
    view.scrollTo (centre - width * 0.5);
    repaint();
}

void OverviewStrip::mouseDown (const juce::MouseEvent& e) { jumpTo (e.position.x); }
void OverviewStrip::mouseDrag (const juce::MouseEvent& e) { jumpTo (e.position.x); }
