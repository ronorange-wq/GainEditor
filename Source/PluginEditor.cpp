#include "PluginEditor.h"

//==============================================================================
// ToolButton
//==============================================================================
ToolButton::ToolButton (const juce::String& text, Icon i, Accent a)
    : juce::Button (text), label (text), icon (i), accent (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int ToolButton::getIdealWidth() const
{
    if (label.isEmpty() || ! showText)
        return 44;
    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (juce::FontOptions (15.0f)), label, 0, 0);
    auto w = (int) ga.getBoundingBox (0, -1, true).getWidth();
    if (hint.isNotEmpty()) w += 26;
    return w + (icon == Icon::none ? 30 : 56);
}

void ToolButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    auto pick = [] (Accent a, juce::Colour& bg, juce::Colour& line, juce::Colour& fg)
    {
        switch (a)
        {
            case Accent::blue:   bg = Theme::active;              line = Theme::activeLine; fg = Theme::text; break;
            case Accent::orange: bg = juce::Colour (0xff2a1f0e); line = Theme::resetLine;  fg = Theme::reset; break;
            case Accent::green:  bg = juce::Colour (0xff123a2a); line = Theme::greenLine;  fg = Theme::green; break;
            case Accent::red:    bg = juce::Colour (0xff3a1416); line = Theme::record;     fg = juce::Colour (0xffff9a9d); break;
            case Accent::normal:
            default:             bg = Theme::button;              line = Theme::buttonLine; fg = Theme::text; break;
        }
    };

    juce::Colour bg, line, fg;
    pick (active ? accent : alwaysAccent, bg, line, fg);
    if (alwaysAccent != Accent::normal && ! active)
        bg = Theme::button;   // outlined look when not active
    if (! isEnabled())
        fg = fg.withAlpha (0.35f), line = line.withAlpha (0.4f);
    if (highlighted && isEnabled()) bg = bg.brighter (0.12f);
    if (down) bg = bg.darker (0.15f);

    g.setColour (bg);
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 9.0f, 1.0f);

    auto content = r.reduced (10.0f, 0.0f);
    const bool hasText = label.isNotEmpty() && showText;

    if (icon != Icon::none)
    {
        auto iconArea = hasText ? content.removeFromLeft (22.0f) : content;
        drawIcon (g, icon, iconArea.withSizeKeepingCentre (20.0f, 20.0f), fg);
        content.removeFromLeft (8.0f);
    }

    if (hasText)
    {
        g.setColour (fg);
        g.setFont (juce::FontOptions (15.0f));
        auto textArea = content;
        if (hint.isNotEmpty())
        {
            auto h = textArea.removeFromRight (20.0f).withSizeKeepingCentre (18.0f, 18.0f);
            g.setColour (Theme::textDim);
            g.drawRoundedRectangle (h, 3.0f, 1.0f);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (hint, h, juce::Justification::centred);
            g.setColour (fg);
            g.setFont (juce::FontOptions (15.0f));
        }
        g.drawText (label, textArea, juce::Justification::centredLeft);
    }
}

void ToolButton::drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> b, juce::Colour c)
{
    g.setColour (c);
    const auto x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    juce::PathStrokeType stroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;

    switch (icon)
    {
        case Icon::play:
            p.addTriangle (x + w * 0.22f, y + h * 0.12f, x + w * 0.22f, y + h * 0.88f, x + w * 0.9f, y + h * 0.5f);
            g.fillPath (p);
            break;
        case Icon::stop:
            g.fillRoundedRectangle (b.reduced (w * 0.15f), 2.0f);
            break;
        case Icon::record:
            g.setColour (Theme::record);
            g.fillEllipse (b.reduced (w * 0.08f));
            break;
        case Icon::pointer:
            p.startNewSubPath (x + w * 0.25f, y + h * 0.05f);
            p.lineTo (x + w * 0.25f, y + h * 0.8f);
            p.lineTo (x + w * 0.43f, y + h * 0.63f);
            p.lineTo (x + w * 0.56f, y + h * 0.95f);
            p.lineTo (x + w * 0.68f, y + h * 0.9f);
            p.lineTo (x + w * 0.55f, y + h * 0.6f);
            p.lineTo (x + w * 0.8f, y + h * 0.6f);
            p.closeSubPath();
            g.fillPath (p);
            break;
        case Icon::range:
        {
            juce::Path r;
            r.addRectangle (b.reduced (w * 0.08f, h * 0.18f));
            float dashes[] = { 3.0f, 2.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.8f).createDashedStroke (dashed, r, dashes, 2);
            g.fillPath (dashed);
            break;
        }
        case Icon::envelope:
            p.startNewSubPath (x, y + h * 0.75f);
            p.lineTo (x + w * 0.3f, y + h * 0.4f);
            p.lineTo (x + w * 0.5f, y + h * 0.6f);
            p.lineTo (x + w * 0.75f, y + h * 0.25f);
            p.lineTo (x + w, y + h * 0.4f);
            g.strokePath (p, stroke);
            for (auto pt : { juce::Point<float> (x + w * 0.3f, y + h * 0.4f), juce::Point<float> (x + w * 0.75f, y + h * 0.25f) })
                g.fillEllipse (pt.x - 2.2f, pt.y - 2.2f, 4.4f, 4.4f);
            break;
        case Icon::blob:
            p.startNewSubPath (x, y + h * 0.5f);
            p.lineTo (x + w * 0.15f, y + h * 0.5f);
            p.lineTo (x + w * 0.25f, y + h * 0.3f);
            p.lineTo (x + w * 0.35f, y + h * 0.7f);
            p.lineTo (x + w * 0.5f, y + h * 0.05f);
            p.lineTo (x + w * 0.62f, y + h * 0.95f);
            p.lineTo (x + w * 0.74f, y + h * 0.35f);
            p.lineTo (x + w * 0.84f, y + h * 0.6f);
            p.lineTo (x + w, y + h * 0.5f);
            g.strokePath (p, stroke);
            break;
        case Icon::peak:
            p.startNewSubPath (x, y + h * 0.85f);
            p.lineTo (x + w * 0.3f, y + h * 0.85f);
            p.lineTo (x + w * 0.5f, y + h * 0.1f);
            p.lineTo (x + w * 0.7f, y + h * 0.85f);
            p.lineTo (x + w, y + h * 0.85f);
            g.strokePath (p, stroke);
            {
                float dashes[] = { 2.5f, 2.0f };
                g.drawDashedLine (juce::Line<float> (x, y + h * 0.38f, x + w, y + h * 0.38f), dashes, 2, 1.4f);
            }
            break;
        case Icon::link:
        {
            const auto r1 = juce::Rectangle<float> (x + w * 0.05f, y + h * 0.32f, w * 0.52f, h * 0.36f);
            const auto r2 = juce::Rectangle<float> (x + w * 0.43f, y + h * 0.32f, w * 0.52f, h * 0.36f);
            g.drawRoundedRectangle (r1, h * 0.18f, 1.8f);
            g.drawRoundedRectangle (r2, h * 0.18f, 1.8f);
            break;
        }
        case Icon::reset:
        case Icon::undo:
        case Icon::redo:
        {
            const bool mirror = icon == Icon::redo;
            const auto cx = b.getCentreX(), cy = b.getCentreY(), rad = w * 0.38f;
            if (icon == Icon::reset)
            {
                p.addCentredArc (cx, cy, rad, rad, 0.0f, -juce::MathConstants<float>::pi * 0.75f, juce::MathConstants<float>::pi * 0.95f, true);
                g.strokePath (p, stroke);
                juce::Path head;
                const auto hx = cx - rad * 0.71f, hy = cy - rad * 0.71f;
                head.addTriangle (hx - 4.0f, hy - 3.0f, hx + 2.5f, hy - 4.0f, hx - 1.0f, hy + 3.5f);
                g.fillPath (head);
            }
            else
            {
                const auto s = mirror ? -1.0f : 1.0f;
                p.startNewSubPath (cx + s * w * 0.3f, y + h * 0.8f);
                p.lineTo (cx + s * w * 0.3f, y + h * 0.55f);
                p.quadraticTo (cx + s * w * 0.3f, y + h * 0.3f, cx, y + h * 0.3f);
                p.lineTo (cx - s * w * 0.35f, y + h * 0.3f);
                g.strokePath (p, stroke);
                juce::Path head;
                head.addTriangle (cx - s * w * 0.48f, y + h * 0.3f, cx - s * w * 0.25f, y + h * 0.12f, cx - s * w * 0.25f, y + h * 0.48f);
                g.fillPath (head);
            }
            break;
        }
        case Icon::loudness:
            for (int i = 0; i < 4; ++i)
            {
                const float heights[] = { 0.45f, 0.95f, 0.65f, 0.3f };
                const auto bh = h * heights[i];
                g.fillRoundedRectangle (x + w * (0.1f + i * 0.22f), y + h - bh, w * 0.12f, bh, 1.0f);
            }
            break;
        case Icon::zoomOut:
        case Icon::zoomIn:
        {
            const auto cr = w * 0.33f, cx = x + w * 0.42f, cy = y + h * 0.42f;
            g.drawEllipse (cx - cr, cy - cr, cr * 2.0f, cr * 2.0f, 1.8f);
            g.drawLine (cx + cr * 0.72f, cy + cr * 0.72f, x + w * 0.95f, y + h * 0.95f, 2.0f);
            g.drawLine (cx - cr * 0.5f, cy, cx + cr * 0.5f, cy, 1.8f);
            if (icon == Icon::zoomIn)
                g.drawLine (cx, cy - cr * 0.5f, cx, cy + cr * 0.5f, 1.8f);
            break;
        }
        case Icon::fit:
            g.drawLine (x, y + h * 0.5f, x + w, y + h * 0.5f, 1.8f);
            p.startNewSubPath (x + w * 0.25f, y + h * 0.25f); p.lineTo (x, y + h * 0.5f); p.lineTo (x + w * 0.25f, y + h * 0.75f);
            p.startNewSubPath (x + w * 0.75f, y + h * 0.25f); p.lineTo (x + w, y + h * 0.5f); p.lineTo (x + w * 0.75f, y + h * 0.75f);
            g.strokePath (p, stroke);
            break;
        case Icon::none:
        default:
            break;
    }
}

//==============================================================================
// PeakBar
//==============================================================================
PeakBar::PeakBar()
{
    for (auto* b : { &selectAll, &tame })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a1f0e));
        b->setColour (juce::TextButton::textColourOffId, Theme::reset);
        b->setColour (juce::ComboBox::outlineColourId, Theme::thresh);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    tame.setColour (juce::TextButton::buttonColourId, Theme::thresh);
    tame.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff14161a));
    selectAll.setTooltip ("Select every peak over the threshold, then drag or use the arrow keys");
    tame.setTooltip ("Bring every peak down to the threshold (only inside the Range selection, if there is one)");
    setSize (520, 42);
}

void PeakBar::setInfo (float thresholdDb, int peaksAbove)
{
    if (! juce::exactlyEqual (thresholdDb, thr) || peaksAbove != count)
    {
        thr = thresholdDb;
        count = peaksAbove;
        tame.setButtonText ("Tame to " + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (std::abs (thr), 1) + " dB");
        repaint();
    }
}

void PeakBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xeb121418));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (Theme::thresh);
    g.drawRoundedRectangle (r, 9.0f, 1.2f);

    g.setFont (juce::FontOptions (14.0f));
    g.setColour (Theme::text);
    g.drawText ("Threshold", 14, 0, 70, getHeight(), juce::Justification::centredLeft);
    g.setColour (Theme::thresh);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (std::abs (thr), 1) + " dB", 84, 0, 70, getHeight(), juce::Justification::centredLeft);
    g.setColour (Theme::over);
    g.drawText (juce::String (count) + (count == 1 ? " peak above" : " peaks above"), 158, 0, 120, getHeight(), juce::Justification::centredLeft);
}

void PeakBar::resized()
{
    auto r = getLocalBounds().reduced (7);
    tame.setBounds (r.removeFromRight (128));
    r.removeFromRight (8);
    selectAll.setBounds (r.removeFromRight (92));
}

//==============================================================================
// Editor
//==============================================================================
GainEditorEditor::GainEditorEditor (GainEditorProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p, peaks), overview (p, peaks, view)
{
    setWantsKeyboardFocus (true);

    for (auto* b : { &playBtn, &stopBtn, &syncBtn, &recordBtn, &selectBtn, &rangeBtn, &envBtn, &blobBtn, &peakBtn, &resetBtn, &loudBtn,
                     &undoBtn, &redoBtn, &zoomOutBtn, &zoomInBtn, &fitBtn })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    resetBtn.setAlwaysAccent (ToolButton::Accent::orange);
    syncBtn.setAlwaysAccent (ToolButton::Accent::normal);
    selectBtn.setShortcutHint ("1");
    rangeBtn.setShortcutHint ("2");
    envBtn.setShortcutHint ("3");
    blobBtn.setShortcutHint ("4");
    peakBtn.setShortcutHint ("5");
    loudBtn.setShortcutHint ("L");

    playBtn.setTooltip ("Play  [Space]. With Ableton on, this starts Ableton (all tracks together).");
    stopBtn.setTooltip ("Stop");
    syncBtn.setTooltip ("Ableton: Play / Stop here also start / stop Ableton. Right-click for setup.");
    recordBtn.setTooltip ("Capture: while on, press play in Ableton and the track is recorded into the plugin");
    selectBtn.setTooltip ("Select [1]: click an edit to pick it (click again for the one underneath), drag up/down");
    rangeBtn.setTooltip ("Range [2]: drag to select, then drag up/down inside it");
    envBtn.setTooltip ("Envelope [3]: click to add points, drag them, double-click to delete");
    blobBtn.setTooltip ("Blob [4]: hover and drag up/down. Zoom in for smaller blobs, out for bigger ones");
    peakBtn.setTooltip ("Peak [5]: peaks over the orange line are marked; click one and drag, or Tame them all");
    resetBtn.setTooltip ("Return the selected part to how it was recorded  [Cmd+R]");
    loudBtn.setTooltip ("Loudness of the selection: hold L, Shift+L to pin");
    undoBtn.setTooltip ("Undo  [Cmd+Z]");
    redoBtn.setTooltip ("Redo  [Shift+Cmd+Z]");
    zoomOutBtn.setTooltip ("Zoom out  [-]");
    zoomInBtn.setTooltip ("Zoom in  [+]");
    fitBtn.setTooltip ("Show the whole recording  [F]");

    playBtn.onClick = [this] { togglePlay(); };
    stopBtn.onClick = [this]
    {
        proc.stopAudition();
        if (proc.isAbletonSync()) proc.sendTransport (false);
        refreshButtons();
    };
    syncBtn.onClick = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) { showSyncHelp(); return; }
        const bool on = ! proc.isAbletonSync();
        proc.setAbletonSync (on);
        if (on && ! (bool) proc.uiState.getProperty ("syncHelpShown", false))
        {
            proc.uiState.setProperty ("syncHelpShown", true, nullptr);
            showSyncHelp();
        }
        refreshButtons();
    };
    recordBtn.onClick = [this] { proc.setCapturing (! proc.isCapturing()); refreshButtons(); };
    selectBtn.onClick = [this] { selectTool (Tool::select); };
    rangeBtn.onClick = [this] { selectTool (Tool::range); };
    envBtn.onClick = [this] { selectTool (Tool::envelope); };
    blobBtn.onClick = [this] { selectTool (Tool::blob); };
    peakBtn.onClick = [this] { selectTool (Tool::peak); };
    resetBtn.onClick = [this] { view.resetTarget (EditOps::resetEverything); };
    loudBtn.onClick = [this] { loudness.setPinned (! loudness.isPinned()); showLoudness (loudness.isPinned()); };
    undoBtn.onClick = [this] { proc.getUndoManager().undo(); };
    redoBtn.onClick = [this] { proc.getUndoManager().redo(); };
    zoomOutBtn.onClick = [this] { view.zoomBy (1.0 / 1.6); };
    zoomInBtn.onClick = [this] { view.zoomBy (1.6); };
    fitBtn.onClick = [this] { view.zoomToFit(); };

    addAndMakeVisible (overview);
    addAndMakeVisible (view);
    addChildComponent (peakBar);
    addChildComponent (loudness);

    peakBar.selectAll.onClick = [this] { view.selectAllPeaks(); };
    peakBar.tame.onClick = [this] { view.tameAllPeaks(); };

    view.onStateChanged = [this] { loudnessNeedsUpdate = true; refreshButtons(); overview.repaint(); repaint (0, getHeight() - 30, getWidth(), 30); };
    view.onShowLoudness = [this] { loudness.setPinned (true); showLoudness (true); };

    proc.addChangeListener (this);

    const auto w = (int) proc.uiState.getProperty ("w", 1560);
    const auto h = (int) proc.uiState.getProperty ("h", 800);
    setResizable (true, true);
    setResizeLimits (1100, 540, 3200, 2000);
    setSize (w, h);
    selectTool ((Tool) juce::jlimit (0, 4, (int) proc.uiState.getProperty ("tool", (int) Tool::blob)));

    peaks.update (proc.getCapture(), 0, proc.getCapture().getWrittenEnd());
    startTimerHz (30);
}

GainEditorEditor::~GainEditorEditor()
{
    proc.removeChangeListener (this);
    proc.uiState.setProperty ("w", getWidth(), nullptr);
    proc.uiState.setProperty ("h", getHeight(), nullptr);
    proc.uiState.setProperty ("tool", (int) view.getTool(), nullptr);
}

void GainEditorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto bar = getLocalBounds().removeFromTop (60);
    g.setColour (Theme::bar);
    g.fillRect (bar);
    g.setColour (juce::Colours::black);
    g.fillRect (bar.removeFromBottom (1));

    g.setColour (juce::Colour (0xfff1f3f5));
    g.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    g.drawText ("Gain Editor", 16, 0, 170, 60, juce::Justification::centredLeft);

    g.setColour (Theme::buttonLine);
    for (auto* b : { &recordBtn, &peakBtn, &loudBtn })
        g.fillRect (b->getRight() + 7, 16, 1, 28);

    // status bar
    auto status = getLocalBounds().removeFromBottom (30);
    g.setColour (Theme::bar);
    g.fillRect (status);
    g.setColour (juce::Colours::black);
    g.fillRect (status.removeFromTop (1));
    status.reduce (14, 0);

    g.setFont (juce::FontOptions (13.0f));
    auto item = [&] (const juce::String& k, const juce::String& v, juce::Colour c)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions (13.0f)), k, 0, 0);
        const auto kw = (int) ga.getBoundingBox (0, -1, true).getWidth() + 4;
        g.setColour (Theme::textDim);
        g.drawText (k, status.removeFromLeft (kw), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        ga.clear();
        ga.addLineOfText (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)), v, 0, 0);
        g.drawText (v, status.removeFromLeft ((int) ga.getBoundingBox (0, -1, true).getWidth() + 26), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (13.0f));
    };
    item ("Blob size follows zoom: ", Analysis::resolutionName (view.currentResolution()), Theme::text);
    item ("Threshold: ", juce::String (proc.getThresholdDb(), 1) + " dB", Theme::thresh);
    item ("Option: ", "fine", Theme::text);
    item ("Ableton sync: ", proc.isAbletonSync() ? (proc.hasMidiPort() ? "On" : "On (no MIDI port)") : "Off",
          proc.isAbletonSync() ? Theme::green : Theme::textDim);

    const auto sr = proc.getSampleRateForTimeline();
    const auto sec = (double) proc.getPlayPosition() / sr;
    const auto m = (int) (sec / 60.0);
    auto rest = juce::String (sec - m * 60.0, 2);
    if (sec - m * 60.0 < 10.0) rest = "0" + rest;
    g.setColour (Theme::textDim);
    g.drawText (juce::String (m) + ":" + rest, status, juce::Justification::centredRight);
}

void GainEditorEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (60).reduced (10, 9);
    bar.removeFromLeft (178);

    // Show text on the buttons when there is room; drop it (least important first) when there isn't.
    ToolButton* all[] = { &playBtn, &stopBtn, &syncBtn, &recordBtn, &selectBtn, &rangeBtn, &envBtn, &blobBtn, &peakBtn,
                          &resetBtn, &loudBtn, &undoBtn, &redoBtn, &zoomOutBtn, &zoomInBtn, &fitBtn };
    ToolButton* dropOrder[] = { &recordBtn, &syncBtn, &loudBtn, &resetBtn, &selectBtn, &envBtn, &rangeBtn, &blobBtn, &peakBtn };
    for (auto* b : dropOrder) b->setShowText (true);
    auto needed = [&]
    {
        int w = 15 * 3 + 8;
        for (auto* b : all) w += b->getIdealWidth() + 6;
        return w;
    };
    for (auto* b : dropOrder)
        if (needed() > bar.getWidth()) b->setShowText (false);

    auto place = [&bar] (ToolButton& b, int gap = 6) { b.setBounds (bar.removeFromLeft (b.getIdealWidth())); bar.removeFromLeft (gap); };

    place (playBtn, 4);
    place (stopBtn, 6);
    place (syncBtn, 6);
    place (recordBtn, 15);
    place (selectBtn);
    place (rangeBtn);
    place (envBtn);
    place (blobBtn);
    place (peakBtn, 15);
    place (resetBtn);
    place (loudBtn, 15);

    auto placeRight = [&bar] (ToolButton& b) { b.setBounds (bar.removeFromRight (b.getIdealWidth())); bar.removeFromRight (6); };
    placeRight (fitBtn);
    placeRight (zoomInBtn);
    placeRight (zoomOutBtn);
    bar.removeFromRight (8);
    placeRight (redoBtn);
    placeRight (undoBtn);

    overview.setBounds (r.removeFromTop (62));
    r.removeFromBottom (30);
    view.setBounds (r);

    layoutOverlays();

    if (! didInitialFit && proc.getCapture().hasData() && view.getWidth() > 0)
    {
        didInitialFit = true;
        view.zoomToFit();
    }
}

void GainEditorEditor::layoutOverlays()
{
    const auto wa = view.waveArea().translated (view.getX(), view.getY());
    peakBar.setTopRightPosition (wa.getRight() - 10, wa.getY() + 10);
    loudness.setTopRightPosition (wa.getRight() - 10, wa.getY() + (peakBar.isVisible() ? 60 : 10));
}

//==============================================================================
void GainEditorEditor::selectTool (Tool t)
{
    view.setTool (t);
    peakBar.setVisible (t == Tool::peak);
    layoutOverlays();
    refreshButtons();
    grabKeyboardFocus();
}

void GainEditorEditor::cycleTool (int direction)
{
    const auto n = (int) view.getTool();
    selectTool ((Tool) ((n + direction + 5) % 5));
}

void GainEditorEditor::showSyncHelp()
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::InfoIcon)
        .withTitle ("Play Ableton from Gain Editor - one-time setup")
        .withMessage ("1. Ableton > Settings > Link, Tempo & MIDI: next to the input \"Gain Editor\" turn on Remote.\n"
                      "2. Press Cmd+M in Ableton (MIDI Map mode).\n"
                      "3. Click Ableton's Play button, then click Play in Gain Editor.\n"
                      "4. Click Ableton's Stop button, then click Stop in Gain Editor.\n"
                      "5. Press Cmd+M again to leave MIDI Map mode.\n\n"
                      "Now Play / Stop here start and stop Ableton, and every track plays together.\n"
                      "Ableton starts from its own position marker (Ableton does not let plugins move it).")
        .withButton ("OK")
        .withAssociatedComponent (this),
        nullptr);
}

void GainEditorEditor::togglePlay()
{
    if (proc.isAbletonSync())
    {
        // Ableton plays every track (with our edits on this one). Toggle like Ableton's space bar.
        proc.stopAudition();
        proc.sendTransport (! proc.isHostPlaying());
        refreshButtons();
        return;
    }

    if (proc.isAuditioning())
    {
        proc.stopAudition();
    }
    else
    {
        int64_t a, b;
        if (! view.getTargetRegion (a, b))
        {
            a = view.getCursor();
            b = proc.getCapture().getWrittenEnd();
            if (a >= b) a = proc.getCapture().getWrittenStart();
        }
        proc.startAudition (a, b);
    }
    refreshButtons();
}

void GainEditorEditor::refreshButtons()
{
    const auto tool = view.getTool();
    selectBtn.setActive (tool == Tool::select);
    rangeBtn.setActive (tool == Tool::range);
    envBtn.setActive (tool == Tool::envelope);
    blobBtn.setActive (tool == Tool::blob);
    peakBtn.setActive (tool == Tool::peak);
    recordBtn.setActive (proc.isCapturing());
    syncBtn.setActive (proc.isAbletonSync());
    playBtn.setActive (proc.isAuditioning() || (proc.isAbletonSync() && proc.isHostPlaying()));

    int64_t a, b;
    const bool target = view.getTargetRegion (a, b);
    resetBtn.setEnabled (target);
    loudBtn.setActive (loudness.isVisible());
    undoBtn.setEnabled (proc.getUndoManager().canUndo());
    redoBtn.setEnabled (proc.getUndoManager().canRedo());
    playBtn.setEnabled (proc.getCapture().hasData() || proc.isAbletonSync());
}

void GainEditorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    loudnessNeedsUpdate = true;
    refreshButtons();
    overview.repaint();
    repaint (0, getHeight() - 30, getWidth(), 30);
}

void GainEditorEditor::showLoudness (bool show)
{
    loudness.setVisible (show);
    if (show)
    {
        layoutOverlays();
        loudness.toFront (false);
        updateLoudness();
    }
    refreshButtons();
}

void GainEditorEditor::updateLoudness()
{
    loudnessNeedsUpdate = false;
    int64_t a, b;
    if (! view.getTargetRegion (a, b))
    {
        loudness.setReport ({}, {});
        return;
    }

    auto rep = Analysis::measure (proc.getCapture(), proc.getEdits(), a, b);
    const auto sr = proc.getSampleRateForTimeline();
    auto t = [sr] (int64_t s)
    {
        const auto sec = (double) s / sr;
        const auto m = (int) (sec / 60.0);
        auto rest = juce::String (sec - m * 60.0, 1);
        if (sec - m * 60.0 < 10.0) rest = "0" + rest;
        return juce::String (m) + ":" + rest;
    };
    loudness.setTarget (-18.0f);
    loudness.setReport (rep, t (a) + " - " + t (b) + "  |  " + juce::String (rep.seconds, 2) + " s");
}

//==============================================================================
void GainEditorEditor::timerCallback()
{
    auto& cap = proc.getCapture();

    int64_t d0, d1;
    bool changedAudio = false;
    if (cap.takeDirtyRange (d0, d1))
    {
        if (cap.getWrittenEnd() < lastWrittenEnd)
            peaks.clear();
        peaks.update (cap, d0, d1);
        changedAudio = true;
    }
    lastWrittenEnd = cap.getWrittenEnd();

    if (! didInitialFit && cap.hasData() && ! proc.isCapturing())
    {
        didInitialFit = true;
        view.zoomToFit();
    }

    const bool moving = proc.isHostPlaying() || proc.isAuditioning();
    if (moving)
        view.followPlayhead (proc.getPlayPosition());

    if (changedAudio || moving || proc.isCapturing())
    {
        view.repaint();
        overview.repaint();
        repaint (0, getHeight() - 30, getWidth(), 30);
    }

    if (peakBar.isVisible())
        peakBar.setInfo (proc.getThresholdDb(), view.countPeaksAbove());

    if (loudness.isVisible() && loudnessNeedsUpdate)
        updateLoudness();

    if (loudnessHeld && ! juce::KeyPress::isKeyCurrentlyDown ('L') && ! juce::KeyPress::isKeyCurrentlyDown ('l'))
    {
        loudnessHeld = false;
        if (! loudness.isPinned())
            showLoudness (false);
    }

    recordBtn.setActive (proc.isCapturing());
    playBtn.setActive (proc.isAuditioning() || (proc.isAbletonSync() && proc.isHostPlaying()));
}

bool GainEditorEditor::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown && loudnessHeld && ! juce::KeyPress::isKeyCurrentlyDown ('L') && ! juce::KeyPress::isKeyCurrentlyDown ('l'))
    {
        loudnessHeld = false;
        if (! loudness.isPinned())
            showLoudness (false);
        return true;
    }
    return false;
}

bool GainEditorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const bool cmd = mods.isCommandDown();

    if (cmd && (code == 'Z' || ch == 'z'))
    {
        if (mods.isShiftDown()) proc.getUndoManager().redo(); else proc.getUndoManager().undo();
        return true;
    }
    if (cmd && (code == 'R' || ch == 'r'))
    {
        if (mods.isShiftDown()) view.resetEverythingWithConfirm(); else view.resetTarget (EditOps::resetEverything);
        return true;
    }
    if (cmd)
        return false;

    if (key == juce::KeyPress::tabKey) { cycleTool (mods.isShiftDown() ? -1 : 1); return true; }

    if (code == 'L' || ch == 'l')
    {
        if (mods.isShiftDown())
        {
            loudness.setPinned (! loudness.isPinned());
            showLoudness (loudness.isPinned());
        }
        else
        {
            loudnessHeld = true;
            if (! loudness.isVisible())
                showLoudness (true);
        }
        return true;
    }

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        const auto step = mods.isAltDown() ? 0.1f : 0.5f;
        view.nudgeSelected (key == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { view.deleteSelected(); return true; }
    if (key == juce::KeyPress::escapeKey) { view.clearSelection(); return true; }
    if (key == juce::KeyPress::spaceKey) { togglePlay(); return true; }

    switch (ch)
    {
        case '1': selectTool (Tool::select); return true;
        case '2': selectTool (Tool::range); return true;
        case '3': selectTool (Tool::envelope); return true;
        case '4': selectTool (Tool::blob); return true;
        case '5': selectTool (Tool::peak); return true;
        case '+': case '=': view.zoomBy (1.6); return true;
        case '-': case '_': view.zoomBy (1.0 / 1.6); return true;
        case 'z': view.zoomToSelection(); return true;
        case 'f': view.zoomToFit(); return true;
        default: break;
    }
    return false;
}
