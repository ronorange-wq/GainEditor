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
    enum class Icon { none, play, stop, record, pointer, range, envelope, blob, reset, loudness, zoomOut, zoomIn, fit, undo, redo };
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
/** Resolution controls shown over the waveform while the Blob tool is active. */
class BlobBar : public juce::Component
{
public:
    BlobBar();
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider resolution;
    juce::TextButton applyToSelection { "Apply to selection" };
    juce::Label valueLabel;
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
    BlobBar blobBar;
    juce::TooltipWindow tooltips { this, 600 };

    ToolButton playBtn { {}, ToolButton::Icon::play }, stopBtn { {}, ToolButton::Icon::stop };
    ToolButton recordBtn { "Record", ToolButton::Icon::record, ToolButton::Accent::red };
    ToolButton selectBtn { {}, ToolButton::Icon::pointer };
    ToolButton rangeBtn { "Range", ToolButton::Icon::range };
    ToolButton envBtn { "Envelope", ToolButton::Icon::envelope };
    ToolButton blobBtn { "Blob", ToolButton::Icon::blob };
    ToolButton resetBtn { "Reset", ToolButton::Icon::reset, ToolButton::Accent::orange };
    ToolButton loudBtn { "Loudness", ToolButton::Icon::loudness, ToolButton::Accent::green };
    ToolButton undoBtn { {}, ToolButton::Icon::undo }, redoBtn { {}, ToolButton::Icon::redo };
    ToolButton zoomOutBtn { {}, ToolButton::Icon::zoomOut }, zoomInBtn { {}, ToolButton::Icon::zoomIn }, fitBtn { {}, ToolButton::Icon::fit };

    bool loudnessHeld = false, loudnessNeedsUpdate = true, didInitialFit = false;
    bool resolutionDragging = false, resolutionPending = false;
    juce::uint32 lastResolutionChange = 0;
    int64_t lastWrittenEnd = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainEditorEditor)
};
