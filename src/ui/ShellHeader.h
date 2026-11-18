#pragma once

// The instrument's header bar: wordmark, Canvas | Live, undo / redo, help and settings. See
// docs/DESIGN.md, "Header". (ArtFreq FX has a top bar of its own: EffectShell.h.)

#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace artfreq::ui::shell
{

class Header final : public juce::Component
{
public:
    Header();

    /** Locked (not activated on this computer): only the name, help and settings are shown; the
        activation screen covers the rest of the window. */
    void setLocked (bool shouldBeLocked);

    void paint (juce::Graphics&) override;
    void resized() override;

    SegmentedControl viewSwitch { { "Canvas", "Live" } };   // 0 = canvas, 1 = live view
    IconButton undoButton;
    IconButton redoButton;
    IconButton helpButton;
    IconButton settingsButton;

private:
    juce::Rectangle<int> logoArea;
    int dividerX = 0;
    bool locked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Header)
};

/** "Cmd+Z" on macOS, "Ctrl+Z" elsewhere: for tooltips. */
juce::String shortcutText (const juce::String& keys);

} // namespace artfreq::ui::shell
