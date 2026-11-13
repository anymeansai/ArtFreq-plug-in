#pragma once

// A scrolling spectrogram of FreqArt's own output, drawn like the canvas (same scale, range,
// slope and colours). See docs/DESIGN.md, "Live view".

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

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

    /** How many seconds the view shows: one pass of the drawing and a quarter more, in whole
        seconds from 4 to 20 (it follows the length and the tempo). */
    double getVisibleSeconds() const;

    /** For the snapshot tool: fills the history with columns analysed from `seconds` of the
        current plan rendered offline at its real speed, one pass ending now (so a snapshot shows
        what a user sees, without a running host). */
    void fillFromPlanForSnapshot (double seconds);

    /** For the snapshot tool: fills the history with columns analysed from `audio` (one
        channel at `sampleRate`, ending now): FreqArt FX's output, which needs a song. */
    void fillFromAudioForSnapshot (const std::vector<float>& audio, double sampleRate);

private:
    void timerCallback() override;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace freqart::ui
