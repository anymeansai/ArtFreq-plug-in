#pragma once

// The canvas: a picture of the spectrogram the drawing will make, which the user edits
// directly. Includes its own frequency axis (left) and time axis (bottom). See docs/DESIGN.md.

#include "EditorState.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace artfreq::ui
{

class CanvasView final : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    static constexpr int axisWidth = 44;    // frequency labels on the left
    static constexpr int axisHeight = 22;   // bar/beat labels at the bottom

    CanvasView (ArtFreqProcessor& processor, EditorState& state);
    ~CanvasView() override;

    /** The area the drawing occupies (without the axes), in this component's coordinates. */
    juce::Rectangle<float> getDrawingArea() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Ends an arrow-key nudge gesture once the arrows are let go. The editor also passes on
        key changes from wherever the focus is, as it does the arrows themselves. */
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace artfreq::ui
