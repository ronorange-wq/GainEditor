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
    enum class Accent { normal, blue, orange, green, red, outline };

    ToolButton (const juce::String& text, Icon icon, Accent activeAccent = Accent::blue, bool small = false);
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
    bool active = false, showText = true, small = false;
};

//==============================================================================
/** A value box with up / down arrows ("-6.0 dB"). Drag up/down on it, or click the arrows. */
class StepperBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    StepperBox (float minV, float maxV, float stepV) : minValue (minV), maxValue (maxV), step (stepV) {}
    void setValue (float v, bool notify);
    float getValue() const noexcept { return value; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (float)> onChange;
    std::function<void()> onDragStart, onDragEnd;

private:
    float value = 0.0f, minValue, maxValue, step, dragStartValue = 0.0f;
    bool dragging = false;
};

//==============================================================================
/** Right-hand panel: Threshold, Reduction and Selection Analysis. */
class AnalysisPanel : public juce::Component
{
public:
    AnalysisPanel();
    void paint (juce::Graphics&) override;
    void resized() override;
    void setAnalysis (const Analysis::LoudnessReport& r, const juce::String& start, const juce::String& end, double seconds, bool valid);

    StepperBox threshold { -40.0f, 0.0f, 0.5f };
    StepperBox reduction { -24.0f, 0.0f, 0.5f };
    juce::Slider reductionSlider;

private:
    Analysis::LoudnessReport report;
    juce::String startText, endText;
    double duration = 0.0;
    bool hasSelection = false;
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

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshButtons();
    void selectTool (Tool t);
    void cycleTool (int direction);
    void togglePlay();
    void updateAnalysis();
    void setPanelVisible (bool show);
    void applyReduction (bool undoable);
    void showSyncHelp();

    GainEditorProcessor& proc;
    PeakCache peaks;
    WaveformView view;
    OverviewStrip overview;
    AnalysisPanel panel;
    juce::TooltipWindow tooltips { this, 600 };

    ToolButton playBtn { {}, ToolButton::Icon::play }, stopBtn { {}, ToolButton::Icon::stop };
    ToolButton recordBtn { {}, ToolButton::Icon::record, ToolButton::Accent::red };
    ToolButton syncBtn { "DAW", ToolButton::Icon::link, ToolButton::Accent::outline };
    ToolButton rangeBtn { "Range", ToolButton::Icon::range };
    ToolButton envBtn { "Envelope", ToolButton::Icon::envelope };
    ToolButton blobBtn { "Blob", ToolButton::Icon::blob };
    ToolButton peakBtn { "Peak", ToolButton::Icon::peak };
    ToolButton undoBtn { "Undo", ToolButton::Icon::undo, ToolButton::Accent::normal, true };
    ToolButton redoBtn { "Redo", ToolButton::Icon::redo, ToolButton::Accent::normal, true };
    ToolButton resetBtn { "Reset", ToolButton::Icon::reset, ToolButton::Accent::normal, true };
    ToolButton loudBtn { "Loudness", ToolButton::Icon::loudness, ToolButton::Accent::outline, true };

    bool analysisNeedsUpdate = true, didInitialFit = false, reductionDragging = false;
    int64_t lastWrittenEnd = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainEditorEditor)
};
