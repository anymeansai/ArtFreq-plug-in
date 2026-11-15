#pragma once

// The header bar: wordmark, Canvas | Live, undo / redo, help and settings; in ArtFreq FX also
// the effect's Mode and Depth. See docs/DESIGN.md, "Header".

#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace artfreq::ui::shell
{

class Header final : public juce::Component
{
public:
    /** forEffect: ArtFreq FX's header ("ArtFreq FX", room for the effect's controls). */
    explicit Header (bool forEffect = false);

    /** ArtFreq FX: puts the effect's Mode and Depth in the header, between Canvas | Live (which
        then moves next to the wordmark) and undo / redo. */
    void setEffectControls (juce::Component* controls, int idealWidth);

    void paint (juce::Graphics&) override;
    void resized() override;

    SegmentedControl viewSwitch { { "Canvas", "Live" } };   // 0 = canvas, 1 = live view
    IconButton undoButton;
    IconButton redoButton;
    IconButton helpButton;
    IconButton settingsButton;

private:
    int bylineRight() const;

    const bool effect;
    juce::Component* effectControls = nullptr;
    int effectControlsWidth = 0;
    juce::Rectangle<int> logoArea;
    int dividerX = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Header)
};

/** "Cmd+Z" on macOS, "Ctrl+Z" elsewhere: for tooltips. */
juce::String shortcutText (const juce::String& keys);

} // namespace artfreq::ui::shell
