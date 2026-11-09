#pragma once
#include <JuceHeader.h>
#include "Analysis.h"

/** "Selection Loudness" card: Original vs Now for the selected part. */
class LoudnessPanel : public juce::Component
{
public:
    LoudnessPanel();
    void setReport (const Analysis::LoudnessReport& r, const juce::String& rangeText);
    void setPinned (bool p) { pinned = p; repaint(); }
    bool isPinned() const noexcept { return pinned; }
    void setTarget (float lufs) { targetLufs = lufs; repaint(); }
    void paint (juce::Graphics&) override;

private:
    Analysis::LoudnessReport report;
    juce::String range;
    bool pinned = false;
    float targetLufs = -18.0f;
};
