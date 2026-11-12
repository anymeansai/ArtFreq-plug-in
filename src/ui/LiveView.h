#pragma once

// A scrolling spectrogram of FreqArt's own output, drawn like the canvas (same scale, range,
// slope and colours). See docs/DESIGN.md, "Live view".

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace freqart::ui
{

class LiveView final : public juce::Component,
                       private juce::Timer
{
public:
    explicit LiveView (FreqArtProcessor& processor);
    ~LiveView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Turns the processor's analyser on while visible and on screen, off otherwise. */
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

    /** For the snapshot tool: fills the history with columns analysed from `seconds` of the
        current plan rendered offline (so a snapshot shows something without a running host). */
    void fillFromPlanForSnapshot (double seconds);

private:
    void timerCallback() override;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace freqart::ui
