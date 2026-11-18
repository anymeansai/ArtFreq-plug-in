#pragma once

// The right-hand panel: the list of layers (always when nothing is selected) with the drawing's
// actions, or the properties of the selected object. See docs/DESIGN.md, "Inspector".
//
// ArtFreq FX uses it as a drawer over the canvas (the compact layout): narrower, captions beside
// their controls, a close button, and without what its bars and strips already hold (adding,
// the brush, Lows, the keys).

#include "EditorState.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace artfreq::ui
{

class Inspector final : public juce::Component,
                        private juce::ChangeListener
{
public:
    static constexpr int preferredWidth = 280;
    static constexpr int compactWidth = 248;   // ArtFreq FX's drawer

    /** The height the compact layout's tallest content (a text object's settings) needs. */
    static constexpr int compactMinimumHeight = 288;

    Inspector (ArtFreqProcessor& processor, EditorState& state, bool compact = false);
    ~Inspector() override;

    /** The compact layout's close button. */
    std::function<void()> onClose;

    /** The compact layout: which side of the canvas the drawer is on (its hairline faces the
        canvas). */
    void setOnLeft (bool shouldBeOnLeft);

    /** Puts the keyboard focus in the text field (if a text object is selected) and selects
        its contents. */
    void focusTextField();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace artfreq::ui
