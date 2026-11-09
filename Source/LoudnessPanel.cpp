#include "LoudnessPanel.h"
#include "WaveformView.h"

LoudnessPanel::LoudnessPanel()
{
    setSize (400, 330);
    setInterceptsMouseClicks (false, false);
}

void LoudnessPanel::setReport (const Analysis::LoudnessReport& r, const juce::String& rangeText)
{
    report = r;
    range = rangeText;
    repaint();
}

static juce::String fmt (float v, const juce::String& unit)
{
    if (v <= -149.0f) return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e ")) + unit;
    auto s = juce::String (std::abs (v), 1);
    return (v < 0.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : (v > 0.0f ? juce::String ("+") : juce::String())) + s + (unit.isEmpty() ? "" : " " + unit);
}

void LoudnessPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (r.translated (0, 4), 12.0f);
    g.setColour (juce::Colour (0xff15181c));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Theme::greenLine);
    g.drawRoundedRectangle (r, 12.0f, pinned ? 2.0f : 1.0f);

    auto area = getLocalBounds().reduced (18, 14);

    auto header = area.removeFromTop (28);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("Selection Loudness", header, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (range, header, juce::Justification::centredRight);

    if (! report.valid)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Select a part of the recording (Range or Blob), then hold L.", area, juce::Justification::centred, 3);
        return;
    }

    area.removeFromTop (8);
    const auto colW = area.getWidth();
    auto col = [colW] (juce::Rectangle<int> row, int i)
    {
        const int w0 = (int) (colW * 0.42f), w = (colW - w0) / 2;
        if (i == 0) return row.withWidth (w0);
        return row.withX (row.getX() + w0 + (i - 1) * w).withWidth (w);
    };

    auto head = area.removeFromTop (22);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Original", col (head, 1), juce::Justification::centredRight);
    g.drawText ("Now", col (head, 2), juce::Justification::centredRight);
    g.setColour (juce::Colour (0xff2a2f36));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);

    struct Row { const char* name; float a, b; const char* unit; };
    const Row rows[] = {
        { "Peak",         report.original.peakDb,     report.now.peakDb,     "dBFS" },
        { "True Peak",    report.original.truePeakDb, report.now.truePeakDb, "dBTP" },
        { "RMS",          report.original.rmsDb,      report.now.rmsDb,      "dB" },
        { "LUFS",         report.original.lufs,       report.now.lufs,       "" },
        { "Gain applied", 0.0f,                        report.gainAppliedDb,  "dB" },
    };

    for (auto& row : rows)
    {
        auto line = area.removeFromTop (38);
        g.setFont (juce::FontOptions (16.0f));
        g.setColour (juce::Colour (0xffc9ced6));
        g.drawText (row.name, col (line, 0), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff8a919c));
        g.drawText (fmt (row.a, row.unit), col (line, 1), juce::Justification::centredRight);
        g.setColour (Theme::green);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (fmt (row.b, row.unit), col (line, 2), juce::Justification::centredRight);
        g.setColour (juce::Colour (0xff22262c));
        g.fillRect (line.getX(), line.getBottom() - 1, line.getWidth(), 1);
    }

    // meter towards the target
    area.removeFromTop (12);
    auto meter = area.removeFromTop (12);
    auto label = meter.removeFromRight (130);
    auto bar = meter.toFloat().withTrimmedRight (10.0f);
    g.setColour (juce::Colour (0xff22262c));
    g.fillRoundedRectangle (bar, 5.0f);
    auto toFrac = [] (float lufs) { return juce::jlimit (0.0f, 1.0f, (lufs + 48.0f) / 48.0f); };
    g.setColour (Theme::greenLine);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * toFrac (report.now.lufs)), 5.0f);
    g.setColour (Theme::reset);
    g.fillRect (bar.getX() + bar.getWidth() * toFrac (targetLufs) - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Target " + fmt (targetLufs, "LUFS"), label.withY (label.getY() - 4).withHeight (20), juce::Justification::centredRight);

    area.removeFromTop (10);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (pinned ? "Pinned  -  Shift+L to unpin" : "Hold L to show  -  Shift+L to pin", area.removeFromTop (20), juce::Justification::centredLeft);
}
