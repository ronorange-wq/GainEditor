#include "PluginEditor.h"

//==============================================================================
// ToolButton
//==============================================================================
ToolButton::ToolButton (const juce::String& text, Icon i, Accent a, bool smallButton)
    : juce::Button (text), label (text), icon (i), accent (a), small (smallButton)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int ToolButton::getIdealWidth() const
{
    const auto fontSize = small ? 13.0f : 16.0f;
    if (label.isEmpty() || ! showText)
        return small ? 38 : 46;
    juce::GlyphArrangement ga;
    ga.addLineOfText (juce::Font (juce::FontOptions (fontSize)), label, 0, 0);
    auto w = (int) ga.getBoundingBox (0, -1, true).getWidth();
    if (hint.isNotEmpty()) w += 28;
    return w + (icon == Icon::none ? 30 : (small ? 46 : 58));
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
            case Accent::outline: bg = juce::Colour (0xff1c2a3d); line = Theme::activeLine; fg = Theme::activeLine; break;
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
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    auto content = r.reduced (small ? 8.0f : 11.0f, 0.0f);
    const auto fontSize = small ? 13.0f : 16.0f;
    const bool hasText = label.isNotEmpty() && showText;

    if (icon != Icon::none)
    {
        const auto is = small ? 15.0f : 19.0f;
        auto iconArea = hasText ? content.removeFromLeft (is + 2.0f) : content;
        drawIcon (g, icon, iconArea.withSizeKeepingCentre (is, is), fg);
        content.removeFromLeft (8.0f);
    }

    if (hasText)
    {
        g.setColour (fg);
        g.setFont (juce::FontOptions (fontSize));
        auto textArea = content;
        if (hint.isNotEmpty())
        {
            auto h = textArea.removeFromRight (22.0f).withSizeKeepingCentre (20.0f, 20.0f);
            g.setColour (juce::Colour (0x40000000));
            g.fillRoundedRectangle (h, 4.0f);
            g.setColour (fg.withAlpha (0.6f));
            g.drawRoundedRectangle (h, 4.0f, 1.0f);
            g.setColour (fg);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (hint, h, juce::Justification::centred);
            g.setColour (fg);
            g.setFont (juce::FontOptions (fontSize));
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
// StepperBox
//==============================================================================
void StepperBox::setValue (float v, bool notify)
{
    v = juce::jlimit (minValue, maxValue, std::round (v / step) * step);
    if (juce::exactlyEqual (v, value)) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void StepperBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff141920));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (juce::Colour (dragging ? 0xff5aaaff : 0xff3a414c));
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    const auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    auto txt = (value < 0.0f ? minus : juce::String()) + juce::String (std::abs (value), 1) + " dB";
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (24.0f));
    g.drawText (txt, r.withTrimmedLeft (18.0f).withTrimmedRight (36.0f), juce::Justification::centredLeft);

    // up / down chevrons
    const auto cx = r.getRight() - 20.0f, cy = r.getCentreY();
    juce::Path up, dn;
    up.startNewSubPath (cx - 5.0f, cy - 6.0f); up.lineTo (cx, cy - 11.0f); up.lineTo (cx + 5.0f, cy - 6.0f);
    dn.startNewSubPath (cx - 5.0f, cy + 6.0f); dn.lineTo (cx, cy + 11.0f); dn.lineTo (cx + 5.0f, cy + 6.0f);
    g.setColour (Theme::text.withAlpha (0.8f));
    g.strokePath (up, juce::PathStrokeType (1.6f));
    g.strokePath (dn, juce::PathStrokeType (1.6f));
}

void StepperBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.position.x > getWidth() - 36)
    {
        if (onDragStart) onDragStart();
        setValue (value + (e.position.y < getHeight() * 0.5f ? step : -step), true);
        if (onDragEnd) onDragEnd();
        return;
    }
    dragging = true;
    dragStartValue = value;
    if (onDragStart) onDragStart();
    repaint();
}

void StepperBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const auto perPixel = e.mods.isAltDown() ? step * 0.05f : step * 0.25f;
    setValue (dragStartValue - (float) e.getDistanceFromDragStartY() * perPixel, true);
}

void StepperBox::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    repaint();
    if (onDragEnd) onDragEnd();
}

//==============================================================================
// AnalysisPanel
//==============================================================================
AnalysisPanel::AnalysisPanel()
{
    addAndMakeVisible (threshold);
    addAndMakeVisible (reduction);
    reductionSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    reductionSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    reductionSlider.setRange (-24.0, 0.0, 0.1);
    reductionSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff3a414c));
    reductionSlider.setColour (juce::Slider::backgroundColourId, Theme::wave);
    reductionSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    reductionSlider.setTooltip ("Lower every peak over the threshold by this much. Peaks you dragged by hand keep their own value.");
    addAndMakeVisible (reductionSlider);
    threshold.setTooltip ("Peaks over this level get a diamond in the Peak tool");
}

void AnalysisPanel::setAnalysis (const Analysis::LoudnessReport& r, const juce::String& s, const juce::String& e, double secs, bool valid)
{
    report = r; startText = s; endText = e; duration = secs; hasSelection = valid;
    repaint();
}

void AnalysisPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bar);
    g.setColour (juce::Colours::black);
    g.fillRect (0, 0, 1, getHeight());

    auto r = getLocalBounds().reduced (22, 18);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText ("Threshold", r.removeFromTop (30), juce::Justification::centredLeft);
    r.removeFromTop (62);
    g.setColour (Theme::buttonLine);
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (14);
    g.setColour (Theme::text);
    g.drawText ("Reduction", r.removeFromTop (30), juce::Justification::centredLeft);
    r.removeFromTop (100);
    g.setColour (Theme::buttonLine);
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (14);

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText ("Selection Analysis", r.removeFromTop (30), juce::Justification::centredLeft);
    auto legend = r.removeFromTop (26);
    g.setColour (Theme::wave);
    g.fillRoundedRectangle (legend.removeFromLeft (14).withSizeKeepingCentre (14, 14).toFloat(), 3.0f);
    legend.removeFromLeft (10);
    g.setColour (Theme::text.withAlpha (0.85f));
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (hasSelection ? "Selected region" : "Nothing selected", legend, juce::Justification::centredLeft);
    r.removeFromTop (8);

    const auto minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    auto num = [&] (float v) { return v <= -149.0f ? juce::String ("-") : (v < 0.0f ? minus : juce::String()) + juce::String (std::abs (v), 1); };

    auto row = [&] (const juce::String& name, const juce::String& value, const juce::String& unit)
    {
        auto line = r.removeFromTop (30);
        g.setColour (Theme::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (name, line, juce::Justification::centredLeft);
        auto unitArea = line.removeFromRight (40);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (unit, unitArea, juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText (hasSelection ? value : juce::String ("-"), line.withTrimmedRight (6), juce::Justification::centredRight);
    };
    row ("Peak", num (report.now.peakDb), "dBFS");
    row ("True Peak", num (report.now.truePeakDb), "dBTP");
    row ("LUFS-I", num (report.now.lufs), "LUFS");
    row ("RMS", num (report.now.rmsDb), "dBFS");
    row ("Crest Factor", juce::String (report.now.crestDb(), 1), "dB");

    r.removeFromTop (8);
    g.setColour (Theme::buttonLine);
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (8);

    auto row2 = [&] (const juce::String& name, const juce::String& value)
    {
        auto line = r.removeFromTop (30);
        g.setColour (Theme::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (name, line, juce::Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (17.0f));
        g.drawText (hasSelection ? value : juce::String ("-"), line, juce::Justification::centredRight);
    };
    row2 ("Start", startText);
    row2 ("End", endText);
    row2 ("Duration", juce::String (duration, 3) + " s");
}

void AnalysisPanel::resized()
{
    auto r = getLocalBounds().reduced (22, 18);
    r.removeFromTop (34);
    threshold.setBounds (r.removeFromTop (52));
    r.removeFromTop (25);
    r.removeFromTop (34);
    reduction.setBounds (r.removeFromTop (52));
    r.removeFromTop (10);
    reductionSlider.setBounds (r.removeFromTop (28));
}

//==============================================================================
// Editor
//==============================================================================
static juce::String timeText (int64_t samples, double sr)
{
    const auto sec = juce::jmax (0.0, (double) samples / sr);
    const auto m = (int) (sec / 60.0);
    auto rest = juce::String (sec - m * 60.0, 3);
    if (sec - m * 60.0 < 10.0) rest = "0" + rest;
    return juce::String (m) + ":" + rest;
}

GainEditorEditor::GainEditorEditor (GainEditorProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p, peaks), overview (p, peaks, view)
{
    setWantsKeyboardFocus (true);

    for (auto* b : { &playBtn, &stopBtn, &recordBtn, &syncBtn, &rangeBtn, &envBtn, &blobBtn, &peakBtn,
                     &undoBtn, &redoBtn, &resetBtn, &loudBtn })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    rangeBtn.setShortcutHint ("1");
    envBtn.setShortcutHint ("2");
    blobBtn.setShortcutHint ("3");
    peakBtn.setShortcutHint ("4");

    playBtn.setTooltip ("Play from the cursor or the selection  [Space]. With DAW on, Ableton plays too.");
    stopBtn.setTooltip ("Stop  [Space]");
    recordBtn.setTooltip ("Record: while on, press play in Ableton and the track is captured into the plugin");
    syncBtn.setTooltip ("DAW: Play / Stop here start and stop Ableton from the same point. Right-click for setup.");
    rangeBtn.setTooltip ("Range [1]: drag to select, then drag up/down inside it");
    envBtn.setTooltip ("Envelope [2]: click to add points, drag them, double-click to delete");
    blobBtn.setTooltip ("Blob [3]: hover and drag up/down. Zoom in for smaller blobs, out for bigger ones");
    peakBtn.setTooltip ("Peak [4]: drag a diamond down to lower that peak. Shift-click for several");
    undoBtn.setTooltip ("Undo  [Cmd+Z]");
    redoBtn.setTooltip ("Redo  [Shift+Cmd+Z]");
    resetBtn.setTooltip ("Return the selected part to how it was recorded  [Cmd+R]");
    loudBtn.setTooltip ("Show / hide the side panel  [L]");

    playBtn.onClick = [this] { togglePlay(); };
    stopBtn.onClick = [this]
    {
        proc.stopAudition();
        if (proc.isAbletonSync()) proc.sendStop();
        refreshButtons();
    };
    recordBtn.onClick = [this] { proc.setCapturing (! proc.isCapturing()); refreshButtons(); };
    syncBtn.onClick = [this]
    {
        if (juce::ModifierKeys::currentModifiers.isPopupMenu()) { showSyncHelp(); return; }
        const bool on = ! proc.isAbletonSync();
        proc.setAbletonSync (on);
        if (on && ! (bool) proc.uiState.getProperty ("syncHelpShown2", false))
        {
            proc.uiState.setProperty ("syncHelpShown2", true, nullptr);
            showSyncHelp();
        }
        refreshButtons();
    };
    rangeBtn.onClick = [this] { selectTool (Tool::range); };
    envBtn.onClick = [this] { selectTool (Tool::envelope); };
    blobBtn.onClick = [this] { selectTool (Tool::blob); };
    peakBtn.onClick = [this] { selectTool (Tool::peak); };
    undoBtn.onClick = [this] { proc.getUndoManager().undo(); };
    redoBtn.onClick = [this] { proc.getUndoManager().redo(); };
    resetBtn.onClick = [this] { view.resetTarget (EditOps::resetEverything); };
    loudBtn.onClick = [this] { setPanelVisible (! panel.isVisible()); };

    addAndMakeVisible (overview);
    addAndMakeVisible (view);
    addAndMakeVisible (panel);

    // Threshold
    panel.threshold.setValue (proc.getThresholdDb(), false);
    panel.threshold.onChange = [this] (float v)
    {
        proc.setThresholdDb (v);
        if (std::abs (proc.getReductionDb()) > 0.01f)
            applyReduction (false);
        view.repaint();
    };
    panel.threshold.onDragStart = [this] { proc.beginDrag(); };
    panel.threshold.onDragEnd = [this] { proc.commitDrag ("Threshold"); };

    // Reduction (box and slider stay in step)
    panel.reduction.setValue (proc.getReductionDb(), false);
    panel.reductionSlider.setValue (proc.getReductionDb(), juce::dontSendNotification);
    panel.reduction.onDragStart = [this] { proc.beginDrag(); };
    panel.reduction.onDragEnd = [this] { proc.commitDrag ("Peak Reduction"); };
    panel.reduction.onChange = [this] (float v)
    {
        proc.setReductionDb (v);
        panel.reductionSlider.setValue (v, juce::dontSendNotification);
        applyReduction (false);
    };
    panel.reductionSlider.onDragStart = [this] { reductionDragging = true; proc.beginDrag(); };
    panel.reductionSlider.onDragEnd = [this] { reductionDragging = false; proc.commitDrag ("Peak Reduction"); };
    panel.reductionSlider.onValueChange = [this]
    {
        const auto v = (float) panel.reductionSlider.getValue();
        proc.setReductionDb (v);
        panel.reduction.setValue (v, false);
        if (! reductionDragging) proc.beginDrag();
        applyReduction (false);
        if (! reductionDragging) proc.commitDrag ("Peak Reduction");
    };

    view.onStateChanged = [this] { analysisNeedsUpdate = true; refreshButtons(); overview.repaint(); repaint (0, getHeight() - 40, getWidth(), 40); };
    view.onShowLoudness = [this] { setPanelVisible (true); };

    proc.addChangeListener (this);

    const auto w = (int) proc.uiState.getProperty ("w", 1560);
    const auto h = (int) proc.uiState.getProperty ("h", 860);
    setResizable (true, true);
    setResizeLimits (1100, 600, 3200, 2000);
    panel.setVisible ((bool) proc.uiState.getProperty ("panel", true));
    setSize (w, h);

    auto savedTool = (int) proc.uiState.getProperty ("tool", (int) Tool::blob);
    if (savedTool == (int) Tool::select) savedTool = (int) Tool::blob;
    selectTool ((Tool) juce::jlimit (1, 4, savedTool));

    peaks.update (proc.getCapture(), 0, proc.getCapture().getWrittenEnd());
    startTimerHz (30);
}

GainEditorEditor::~GainEditorEditor()
{
    proc.removeChangeListener (this);
    proc.uiState.setProperty ("w", getWidth(), nullptr);
    proc.uiState.setProperty ("h", getHeight(), nullptr);
    proc.uiState.setProperty ("tool", (int) view.getTool(), nullptr);
    proc.uiState.setProperty ("panel", panel.isVisible(), nullptr);
}

void GainEditorEditor::applyReduction (bool undoable)
{
    auto s = proc.getEdits();
    Analysis::applyAutoReduction (s, proc.getCapture(), juce::Decibels::decibelsToGain (proc.getThresholdDb()), proc.getReductionDb());
    proc.setEdits (s, undoable, "Peak Reduction");
}

void GainEditorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto bar = getLocalBounds().removeFromTop (66);
    g.setColour (Theme::bar);
    g.fillRect (bar);
    g.setColour (juce::Colours::black);
    g.fillRect (bar.removeFromBottom (1));

    g.setColour (juce::Colour (0xfff1f3f5));
    g.setFont (juce::FontOptions (27.0f));
    g.drawText ("Gain Editor", 20, 0, 200, 66, juce::Justification::centredLeft);

    g.setColour (Theme::buttonLine);
    g.fillRect (syncBtn.getRight() + 18, 18, 1, 30);
    g.fillRect (peakBtn.getRight() + 12, 18, 1, 30);

    // status bar
    auto status = getLocalBounds().removeFromBottom (40);
    g.setColour (Theme::bar);
    g.fillRect (status);
    g.setColour (juce::Colours::black);
    g.fillRect (status.removeFromTop (1));
    status.reduce (20, 0);

    // link icon
    {
        auto ic = status.removeFromLeft (22).toFloat().withSizeKeepingCentre (18.0f, 18.0f);
        g.setColour (Theme::text.withAlpha (0.8f));
        g.drawRoundedRectangle (ic.withWidth (11.0f).withHeight (7.0f).withY (ic.getCentreY() - 3.5f), 3.5f, 1.5f);
        g.drawRoundedRectangle (ic.withX (ic.getX() + 7.0f).withWidth (11.0f).withHeight (7.0f).withY (ic.getCentreY() - 3.5f), 3.5f, 1.5f);
    }
    status.removeFromLeft (8);
    g.setFont (juce::FontOptions (16.0f));
    g.setColour (Theme::text);
    g.drawText ("DAW sync:", status.removeFromLeft (84), juce::Justification::centredLeft);
    const bool on = proc.isAbletonSync();
    g.setColour (on ? Theme::wave : Theme::textDim);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (on ? "On" : "Off", status.removeFromLeft (40), juce::Justification::centredLeft);
    g.setColour (Theme::buttonLine);
    g.fillRect (status.removeFromLeft (20).withSizeKeepingCentre (1, 22));
    status.removeFromLeft (14);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("Option = fine", status.removeFromLeft (140), juce::Justification::centredLeft);

    const auto sr = proc.getSampleRateForTimeline();
    const auto pos = (proc.isHostPlaying() || proc.isAuditioning()) ? proc.getPlayPosition() : view.getCursor();
    g.drawText (timeText (pos, sr).dropLastCharacters (1), status, juce::Justification::centredRight);
}

void GainEditorEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (66).reduced (12, 12);
    bar.removeFromLeft (196);

    auto place = [&bar] (ToolButton& b, int gap) { b.setBounds (bar.removeFromLeft (b.getIdealWidth())); bar.removeFromLeft (gap); };
    place (playBtn, 8);
    place (stopBtn, 8);
    place (recordBtn, 22);
    place (syncBtn, 36);

    auto placeRight = [&bar] (ToolButton& b, int gap) { b.setBounds (bar.removeFromRight (b.getIdealWidth()).reduced (0, 3)); bar.removeFromRight (gap); };
    placeRight (loudBtn, 8);
    placeRight (resetBtn, 8);
    placeRight (redoBtn, 0);
    placeRight (undoBtn, 24);

    // tools: spread evenly in what is left
    ToolButton* tools[] = { &rangeBtn, &envBtn, &blobBtn, &peakBtn };
    int total = 0;
    for (auto* t : tools) total += t->getIdealWidth();
    const auto gap = juce::jlimit (8, 30, (bar.getWidth() - total) / 4);
    for (auto* t : tools) place (*t, gap);

    overview.setBounds (r.removeFromTop (88).reduced (0, 6));
    r.removeFromBottom (40);
    if (panel.isVisible())
        panel.setBounds (r.removeFromRight (290));
    view.setBounds (r);

    if (! didInitialFit && proc.getCapture().hasData() && view.getWidth() > 0)
    {
        didInitialFit = true;
        view.zoomToFit();
    }
}

void GainEditorEditor::setPanelVisible (bool show)
{
    panel.setVisible (show);
    resized();
    refreshButtons();
    if (show) updateAnalysis();
}

//==============================================================================
void GainEditorEditor::selectTool (Tool t)
{
    view.setTool (t);
    refreshButtons();
    grabKeyboardFocus();
}

void GainEditorEditor::cycleTool (int direction)
{
    // Range, Envelope, Blob, Peak  (enum values 1..4)
    const auto n = (int) view.getTool() - 1;
    selectTool ((Tool) (1 + (n + direction + 4) % 4));
}

void GainEditorEditor::showSyncHelp()
{
    juce::String where;
    const bool ok = proc.installRemoteScript (where);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
        .withIconType (juce::MessageBoxIconType::InfoIcon)
        .withTitle ("Play Ableton from Gain Editor - one-time setup")
        .withMessage (juce::String (ok ? "The Gain Editor control surface was installed in:\n" + where + "\n\n"
                                       : "Could not install the control surface: " + where + "\n\n")
                      + "1. Restart Ableton Live.\n"
                        "2. Settings > Link, Tempo & MIDI > Control Surface: choose \"GainEditor\".\n"
                        "   Input: \"Gain Editor\". Output: None.\n"
                        "3. Done. Click anywhere on the waveform and press Space:\n"
                        "   Ableton jumps to that point and plays every track together.")
        .withButton ("OK")
        .withAssociatedComponent (this),
        nullptr);
}

void GainEditorEditor::togglePlay()
{
    int64_t a, b;
    const bool target = view.getTargetRegion (a, b);
    const auto from = target ? a : view.getCursor();

    if (proc.isAbletonSync())
    {
        proc.stopAudition();
        if (proc.isHostPlaying()) proc.sendStop();
        else proc.sendPlayFrom (from);
        refreshButtons();
        return;
    }

    if (proc.isAuditioning())
    {
        proc.stopAudition();
    }
    else
    {
        auto start = from, end = target ? b : proc.getCapture().getWrittenEnd();
        if (start >= end) { start = proc.getCapture().getWrittenStart(); end = proc.getCapture().getWrittenEnd(); }
        proc.startAudition (start, end);
    }
    refreshButtons();
}

void GainEditorEditor::refreshButtons()
{
    const auto tool = view.getTool();
    rangeBtn.setActive (tool == Tool::range);
    envBtn.setActive (tool == Tool::envelope);
    blobBtn.setActive (tool == Tool::blob);
    peakBtn.setActive (tool == Tool::peak);
    recordBtn.setActive (proc.isCapturing());
    syncBtn.setActive (proc.isAbletonSync());
    playBtn.setActive (proc.isAuditioning() || (proc.isAbletonSync() && proc.isHostPlaying()));
    loudBtn.setActive (panel.isVisible());

    int64_t a, b;
    resetBtn.setEnabled (view.getTargetRegion (a, b));
    undoBtn.setEnabled (proc.getUndoManager().canUndo());
    redoBtn.setEnabled (proc.getUndoManager().canRedo());
}

void GainEditorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    analysisNeedsUpdate = true;
    panel.threshold.setValue (proc.getThresholdDb(), false);
    refreshButtons();
    overview.repaint();
    repaint (0, getHeight() - 40, getWidth(), 40);
}

void GainEditorEditor::updateAnalysis()
{
    analysisNeedsUpdate = false;
    int64_t a, b;
    if (! view.getTargetRegion (a, b))
    {
        panel.setAnalysis ({}, {}, {}, 0.0, false);
        return;
    }
    const auto sr = proc.getSampleRateForTimeline();
    auto rep = Analysis::measure (proc.getCapture(), proc.getEdits(), a, b);
    panel.setAnalysis (rep, timeText (a, sr), timeText (b, sr), rep.seconds, rep.valid);
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
        repaint (0, getHeight() - 40, getWidth(), 40);
    }

    if (panel.isVisible() && analysisNeedsUpdate)
        updateAnalysis();

    recordBtn.setActive (proc.isCapturing());
    playBtn.setActive (proc.isAuditioning() || (proc.isAbletonSync() && proc.isHostPlaying()));
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
        case '1': selectTool (Tool::range); return true;
        case '2': selectTool (Tool::envelope); return true;
        case '3': selectTool (Tool::blob); return true;
        case '4': selectTool (Tool::peak); return true;
        case 'l': setPanelVisible (! panel.isVisible()); return true;
        case '+': case '=': view.zoomBy (1.6); return true;
        case '-': case '_': view.zoomBy (1.0 / 1.6); return true;
        case 'z': view.zoomToSelection(); return true;
        case 'f': view.zoomToFit(); return true;
        default: break;
    }
    return false;
}
