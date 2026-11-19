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

    /** Where an object lies on the canvas with its selection handles, in this component's
        coordinates (empty if there is no such object). */
    juce::Rectangle<float> getObjectBounds (const juce::Uuid& id) const;

    /** How far the selection handles reach outside an object's rectangle. */
    static constexpr float handleReach = 4.5f;

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

    /** What the canvas shows of the sound: the measured preview (the real sound, analysed as the
        user's analyser does: see MeasuredPreview.h) once it is in, else the fast preview. */
    struct PreviewStatus
    {
        bool measuredShown = false;   // the measured preview is on screen (fading in or not)
        bool current = false;         // ... and it is of what is drawn and set now
        bool fading = false;          // a cross-fade is running
        bool fastCurrent = false;     // the fast preview last painted is of what is drawn now
        juce::String text;            // the quiet line under the canvas
    };

    PreviewStatus getPreviewStatus() const;

    /** For the snapshot tool and tests: renders the measured preview of what is drawn now, at
        `pixelScale` physical pixels per logical one, on this thread, and shows it at once. */
    void settleMeasuredPreview (float pixelScale);

    /** For the snapshot tool and tests: the fast preview is made while painting rather than on
        its own thread (see FastPreview.h), so a picture taken at once shows what is drawn now. */
    void setFastPreviewMadeWhilePainting (bool shouldMakeWhilePainting);

    /** How many fast previews were made on their own thread (tests). */
    int getFastPreviewsMadeInBackground() const noexcept;

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
