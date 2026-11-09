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
        return 50;
    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (juce::FontOptions (17.0f)), label, 0, 0);
    auto w = (int) ga.getBoundingBox (0, -1, true).getWidth();
    if (hint.isNotEmpty()) w += 28;
    return w + (icon == Icon::none ? 30 : 60);
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

    auto content = r.reduced (12.0f, 0.0f);
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
        g.setFont (juce::FontOptions (17.0f));
        auto textArea = content;
        if (hint.isNotEmpty())
        {
            auto h = textArea.removeFromRight (20.0f).withSizeKeepingCentre (18.0f, 18.0f);
            g.setColour (Theme::textDim);
            g.drawRoundedRectangle (h, 3.0f, 1.0f);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (hint, h, juce::Justification::centred);
            g.setColour (fg);
            g.setFont (juce::FontOptions (17.0f));
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
// BlobBar
//==============================================================================
BlobBar::BlobBar()
{
    resolution.setSliderStyle (juce::Slider::LinearHorizontal);
    resolution.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    resolution.setRange (0.0, 1.0, 0.0);
    resolution.setColour (juce::Slider::trackColourId, Theme::wave);
    resolution.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2a2f36));
    resolution.setColour (juce::Slider::thumbColourId, Theme::text);
    resolution.setTooltip ("Blob size: drag right for smaller blobs, all the way to single peaks");
    addAndMakeVisible (resolution);

    valueLabel.setColour (juce::Label::textColourId, Theme::wave);
    valueLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    valueLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (valueLabel);

    applyToSelection.setColour (juce::TextButton::buttonColourId, Theme::button);
    applyToSelection.setColour (juce::TextButton::textColourOffId, Theme::text);
    applyToSelection.setTooltip ("Use this resolution only inside the selected region");
    addAndMakeVisible (applyToSelection);
}

void BlobBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xee121418));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colour (0xff3a3f47));
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("BLOB RESOLUTION", 14, 8, 150, 16, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (11.0f));
    const auto sl = resolution.getBounds();
    const char* names[] = { "Phrase", "Word", "Syllable", "Peak" };
    for (int i = 0; i < 4; ++i)
    {
        const auto fx = sl.getX() + 10 + (int) ((sl.getWidth() - 20) * (i / 3.0f));
        g.drawText (names[i], fx - 30, sl.getBottom() - 2, 60, 14, juce::Justification::centred);
    }

    g.setColour (Theme::textDim.withAlpha (0.8f));
    g.drawText ("Double-click: split   M: merge   Drag edges: widen / narrow   Option: fine",
                getLocalBounds().removeFromBottom (20).reduced (14, 0), juce::Justification::centredLeft);
}

void BlobBar::resized()
{
    auto r = getLocalBounds().reduced (12, 6);
    r.removeFromBottom (16);
    r.removeFromTop (18);
    auto right = r.removeFromRight (160);
    applyToSelection.setBounds (right.withSizeKeepingCentre (156, 28));
    valueLabel.setBounds (r.removeFromRight (80));
    resolution.setBounds (r.withTrimmedBottom (10));
}

//==============================================================================
// Editor
//==============================================================================
GainEditorEditor::GainEditorEditor (GainEditorProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p, peaks), overview (p, peaks, view)
{
    setWantsKeyboardFocus (true);

    for (auto* b : { &playBtn, &stopBtn, &recordBtn, &selectBtn, &rangeBtn, &envBtn, &blobBtn, &resetBtn, &loudBtn,
                     &undoBtn, &redoBtn, &zoomOutBtn, &zoomInBtn, &fitBtn })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    resetBtn.setAlwaysAccent (ToolButton::Accent::orange);
    loudBtn.setShortcutHint ("L");

    playBtn.setTooltip ("Play the selection (or from the cursor) inside the plugin  [Space]");
    stopBtn.setTooltip ("Stop");
    recordBtn.setTooltip ("Capture: while on, press play in Ableton and the track is recorded into the plugin");
    selectBtn.setTooltip ("Select: click a blob or range to pick it, drag up/down for gain  [1]");
    rangeBtn.setTooltip ("Range: drag to select, then drag up/down inside it  [2]");
    envBtn.setTooltip ("Envelope: click to add points, drag them, double-click to delete  [3]");
    blobBtn.setTooltip ("Blob: click a blob, drag up/down; Shift-click for several  [4]");
    resetBtn.setTooltip ("Return the selected part to how it was recorded  [Cmd+R]");
    loudBtn.setTooltip ("Loudness of the selection: hold L, Shift+L to pin");
    undoBtn.setTooltip ("Undo  [Cmd+Z]");
    redoBtn.setTooltip ("Redo  [Shift+Cmd+Z]");
    zoomOutBtn.setTooltip ("Zoom out  [-]");
    zoomInBtn.setTooltip ("Zoom in  [+]");
    fitBtn.setTooltip ("Show the whole recording");

    playBtn.onClick = [this] { togglePlay(); };
    stopBtn.onClick = [this] { proc.stopAudition(); refreshButtons(); };
    recordBtn.onClick = [this] { proc.setCapturing (! proc.isCapturing()); refreshButtons(); };
    selectBtn.onClick = [this] { selectTool (Tool::select); };
    rangeBtn.onClick = [this] { selectTool (Tool::range); };
    envBtn.onClick = [this] { selectTool (Tool::envelope); };
    blobBtn.onClick = [this] { selectTool (Tool::blob); };
    resetBtn.onClick = [this] { view.resetTarget (EditOps::resetEverything); };
    loudBtn.onClick = [this] { loudness.setPinned (! loudness.isPinned()); showLoudness (loudness.isPinned()); };
    undoBtn.onClick = [this] { proc.getUndoManager().undo(); };
    redoBtn.onClick = [this] { proc.getUndoManager().redo(); };
    zoomOutBtn.onClick = [this] { view.zoomBy (1.0 / 1.6); };
    zoomInBtn.onClick = [this] { view.zoomBy (1.6); };
    fitBtn.onClick = [this] { view.zoomToFit(); };

    addAndMakeVisible (overview);
    addAndMakeVisible (view);
    addChildComponent (blobBar);
    addChildComponent (loudness);

    view.onStateChanged = [this] { loudnessNeedsUpdate = true; refreshButtons(); overview.repaint(); };
    view.onShowLoudness = [this] { loudness.setPinned (true); showLoudness (true); };

    blobBar.resolution.setValue (proc.getEdits().blobResolution, juce::dontSendNotification);
    blobBar.valueLabel.setText (Analysis::resolutionName (proc.getEdits().blobResolution), juce::dontSendNotification);
    blobBar.resolution.onDragStart = [this] { resolutionDragging = true; proc.beginDrag(); };
    blobBar.resolution.onValueChange = [this]
    {
        blobBar.valueLabel.setText (Analysis::resolutionName ((float) blobBar.resolution.getValue()), juce::dontSendNotification);
        resolutionPending = true;
        lastResolutionChange = juce::Time::getMillisecondCounter();
    };
    blobBar.resolution.onDragEnd = [this]
    {
        auto s = proc.getEdits();
        s.blobResolution = (float) blobBar.resolution.getValue();
        Analysis::redetect (s, proc.getCapture(), s.blobResolution);
        proc.setEdits (s, false);
        resolutionPending = false;
        resolutionDragging = false;
        proc.commitDrag ("Blob Resolution");
    };
    blobBar.applyToSelection.onClick = [this] { view.applyResolutionToSelection ((float) blobBar.resolution.getValue()); };

    proc.addChangeListener (this);

    // restore UI
    const auto w = (int) proc.uiState.getProperty ("w", 1400);
    const auto h = (int) proc.uiState.getProperty ("h", 780);
    setResizable (true, true);
    setResizeLimits (1000, 520, 3000, 2000);
    setSize (w, h);
    selectTool ((Tool) (int) proc.uiState.getProperty ("tool", (int) Tool::range));

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
    auto bar = getLocalBounds().removeFromTop (64);
    g.setColour (Theme::bar);
    g.fillRect (bar);
    g.setColour (juce::Colours::black);
    g.fillRect (bar.removeFromBottom (1));

    g.setColour (juce::Colour (0xfff1f3f5));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("Gain Editor", 18, 0, 190, 64, juce::Justification::centredLeft);

    // thin separators between button groups
    g.setColour (Theme::buttonLine);
    for (auto* b : { &recordBtn, &blobBtn })
        g.fillRect (b->getRight() + 8, 16, 1, 32);

    g.setColour (juce::Colour (0xff1c1f24));
    g.fillRect (getLocalBounds().removeFromBottom (12));
}

void GainEditorEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (64).reduced (10, 9);
    bar.removeFromLeft (200);

    const bool compact = getWidth() < 1380;
    for (auto* b : { &rangeBtn, &envBtn, &blobBtn, &resetBtn, &loudBtn, &recordBtn })
        b->setShowText (! compact);

    auto place = [&bar] (ToolButton& b, int gap = 6) { b.setBounds (bar.removeFromLeft (b.getIdealWidth())); bar.removeFromLeft (gap); };

    place (playBtn, 0);
    place (stopBtn, 10);
    place (recordBtn, 17);
    place (selectBtn);
    place (rangeBtn);
    place (envBtn);
    place (blobBtn, 17);
    place (resetBtn);
    place (loudBtn);

    auto placeRight = [&bar] (ToolButton& b) { b.setBounds (bar.removeFromRight (b.getIdealWidth())); bar.removeFromRight (6); };
    placeRight (fitBtn);
    placeRight (zoomInBtn);
    placeRight (zoomOutBtn);
    bar.removeFromRight (10);
    placeRight (redoBtn);
    placeRight (undoBtn);

    overview.setBounds (r.removeFromTop (76));
    r.removeFromBottom (12);
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
    const auto v = view.getBounds();
    blobBar.setBounds (v.getX() + 12, v.getY() + 48, juce::jmin (720, v.getWidth() - 24), 84);
    loudness.setTopRightPosition (v.getRight() - 16, v.getY() + 56);
}

//==============================================================================
void GainEditorEditor::selectTool (Tool t)
{
    view.setTool (t);
    blobBar.setVisible (t == Tool::blob);

    if (t == Tool::blob && proc.getEdits().blobs.empty() && proc.getCapture().hasData())
        proc.redetectBlobs ((float) blobBar.resolution.getValue());

    refreshButtons();
}

void GainEditorEditor::togglePlay()
{
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
    recordBtn.setActive (proc.isCapturing());
    playBtn.setActive (proc.isAuditioning());

    int64_t a, b;
    const bool target = view.getTargetRegion (a, b);
    resetBtn.setEnabled (target);
    loudBtn.setActive (loudness.isVisible());
    undoBtn.setEnabled (proc.getUndoManager().canUndo());
    redoBtn.setEnabled (proc.getUndoManager().canRedo());
    playBtn.setEnabled (proc.getCapture().hasData());
    blobBar.applyToSelection.setEnabled (target);
}

void GainEditorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    loudnessNeedsUpdate = true;
    if (! resolutionDragging)
    {
        blobBar.resolution.setValue (proc.getEdits().blobResolution, juce::dontSendNotification);
        blobBar.valueLabel.setText (Analysis::resolutionName (proc.getEdits().blobResolution), juce::dontSendNotification);
    }
    refreshButtons();
    overview.repaint();
}

void GainEditorEditor::showLoudness (bool show)
{
    loudness.setVisible (show);
    if (show)
    {
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
    loudness.setReport (rep, t (a) + " - " + t (b) + "  |  " + juce::String (rep.seconds, 2) + " s");
}

//==============================================================================
void GainEditorEditor::timerCallback()
{
    auto& cap = proc.getCapture();

    // keep the waveform picture in step with what is being recorded
    int64_t d0, d1;
    bool changed = false;
    if (cap.takeDirtyRange (d0, d1))
    {
        if (cap.getWrittenEnd() < lastWrittenEnd)
            peaks.clear();
        peaks.update (cap, d0, d1);
        changed = true;
    }
    lastWrittenEnd = cap.getWrittenEnd();

    if (! didInitialFit && cap.hasData() && ! proc.isCapturing())
    {
        didInitialFit = true;
        view.zoomToFit();
    }

    // live blob resolution while the slider is dragged
    if (resolutionPending && resolutionDragging && juce::Time::getMillisecondCounter() - lastResolutionChange > 60)
    {
        resolutionPending = false;
        auto s = proc.getEdits();
        s.blobResolution = (float) blobBar.resolution.getValue();
        Analysis::redetect (s, cap, s.blobResolution);
        proc.setEdits (s, false);
    }
    else if (resolutionPending && ! resolutionDragging)
    {
        resolutionPending = false;
        proc.redetectBlobs ((float) blobBar.resolution.getValue());
    }

    const bool moving = proc.isHostPlaying() || proc.isAuditioning();
    if (moving)
        view.followPlayhead (proc.getPlayPosition());

    if (changed || moving || proc.isCapturing())
    {
        view.repaint();
        overview.repaint();
    }

    if (loudness.isVisible() && loudnessNeedsUpdate)
        updateLoudness();

    // L released?
    if (loudnessHeld && ! juce::KeyPress::isKeyCurrentlyDown ('L') && ! juce::KeyPress::isKeyCurrentlyDown ('l'))
    {
        loudnessHeld = false;
        if (! loudness.isPinned())
            showLoudness (false);
    }

    if (playBtn.getToggleState() != proc.isAuditioning())
        refreshButtons();
    recordBtn.setActive (proc.isCapturing());
    playBtn.setActive (proc.isAuditioning());
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
        return false; // leave other shortcuts to the host

    if (code == 'L' || ch == 'l')
    {
        if (mods.isShiftDown())
        {
            loudness.setPinned (! loudness.isPinned());
            showLoudness (loudness.isPinned());
        }
        else if (! loudness.isVisible())
        {
            loudnessHeld = true;
            showLoudness (true);
        }
        else
        {
            loudnessHeld = true;
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
        case '+': case '=': view.zoomBy (1.6); return true;
        case '-': case '_': view.zoomBy (1.0 / 1.6); return true;
        case 'z': view.zoomToSelection(); return true;
        case 'f': view.zoomToFit(); return true;
        case 'm': view.mergeSelectedBlobs(); return true;
        default: break;
    }
    return false;
}
