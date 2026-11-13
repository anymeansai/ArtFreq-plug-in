#pragma once

// The right-hand panel: the list of layers (always when nothing is selected) with the drawing's
// actions, or the properties of the selected object. See docs/DESIGN.md, "Inspector".

#include "EditorState.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace freqart::ui
{

class Inspector final : public juce::Component,
                        private juce::ChangeListener
{
public:
    static constexpr int preferredWidth = 280;

    Inspector (FreqArtProcessor& processor, EditorState& state);
    ~Inspector() override;

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

} // namespace freqart::ui
