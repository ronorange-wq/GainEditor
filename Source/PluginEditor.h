#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "WaveformView.h"
#include "LoudnessPanel.h"

//==============================================================================
/** Toolbar button drawn in the Gain Editor style (icon + optional text). */
class ToolButton : public juce::Button
{
public:
    enum class Icon { none, play, stop, record, pointer, range, envelope, blob, peak, link, reset, loudness, zoomOut, zoomIn, fit, undo, redo };
    enum class Accent { normal, blue, orange, green, red };

    ToolButton (const juce::String& text, Icon icon, Accent activeAccent = Accent::blue);
    void setActive (bool a) { if (a != active) { active = a; repaint(); } }
    void setAlwaysAccent (Accent a) { alwaysAccent = a; repaint(); }
    void setShortcutHint (const juce::String& s) { hint = s; repaint(); }
    void setShowText (bool s) { if (s != showText) { showText = s; repaint(); } }
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    static void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);
    juce::String label, hint;
    Icon icon;
    Accent accent, alwaysAccent = Accent::normal;
    bool active = false, showText = true;
};

//==============================================================================
/** Threshold / peak count / Select all / Tame, shown over the waveform with the Peak tool. */
class PeakBar : public juce::Component
{
public:
    PeakBar();
    void setInfo (float thresholdDb, int peaksAbove);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::TextButton selectAll { "Select all" }, tame { "Tame" };

private:
    float thr = -6.0f;
    int count = 0;
};

//==============================================================================
class GainEditorEditor : public juce::AudioProcessorEditor,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    explicit GainEditorEditor (GainEditorProcessor&);
    ~GainEditorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshButtons();
    void selectTool (Tool t);
    void togglePlay();
    void updateLoudness();
    void showLoudness (bool show);
    void layoutOverlays();

    GainEditorProcessor& proc;
    PeakCache peaks;
    WaveformView view;
    OverviewStrip overview;
    LoudnessPanel loudness;
    PeakBar peakBar;
    juce::TooltipWindow tooltips { this, 600 };

    ToolButton playBtn { {}, ToolButton::Icon::play }, stopBtn { {}, ToolButton::Icon::stop };
    ToolButton recordBtn { "Record", ToolButton::Icon::record, ToolButton::Accent::red };
    ToolButton selectBtn { "Select", ToolButton::Icon::pointer };
    ToolButton rangeBtn { "Range", ToolButton::Icon::range };
    ToolButton envBtn { "Envelope", ToolButton::Icon::envelope };
    ToolButton blobBtn { "Blob", ToolButton::Icon::blob };
    ToolButton peakBtn { "Peak", ToolButton::Icon::peak };
    ToolButton syncBtn { "Ableton", ToolButton::Icon::link, ToolButton::Accent::green };
    ToolButton resetBtn { "Reset", ToolButton::Icon::reset, ToolButton::Accent::orange };
    ToolButton loudBtn { "Loudness", ToolButton::Icon::loudness, ToolButton::Accent::green };
    ToolButton undoBtn { {}, ToolButton::Icon::undo }, redoBtn { {}, ToolButton::Icon::redo };
    ToolButton zoomOutBtn { {}, ToolButton::Icon::zoomOut }, zoomInBtn { {}, ToolButton::Icon::zoomIn }, fitBtn { {}, ToolButton::Icon::fit };

    bool loudnessHeld = false, loudnessNeedsUpdate = true, didInitialFit = false;
    void cycleTool (int direction);
    void showSyncHelp();
    int64_t lastWrittenEnd = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainEditorEditor)
};
