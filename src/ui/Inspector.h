#pragma once

// The right-hand panel: the list of layers (always when nothing is selected) with the drawing's
// actions, or the properties of the selected object. See docs/DESIGN.md, "Inspector".

#include "EditorState.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace artfreq::ui
{

class Inspector final : public juce::Component,
                        private juce::ChangeListener
{
public:
    static constexpr int preferredWidth = 280;

    Inspector (ArtFreqProcessor& processor, EditorState& state);
    ~Inspector() override;

    /** Puts the keyboard focus in the text field (if a text object is selected) and selects
        its contents. */
    void focusTextField();

    /** ArtFreq FX: greys Lows in the modes that play no sines (it shapes only the sines). Called
        from the editor's timer, as the footer's knobs are. */
    void refreshEffectMode();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace artfreq::ui
