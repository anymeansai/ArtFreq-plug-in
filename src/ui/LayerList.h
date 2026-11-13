#pragma once

// The inspector's list of layers: one row per object, front first, and the paint layer last.
// See docs/DESIGN.md, "Inspector".

#include "EditorState.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace freqart::ui
{

/** Each object row shows its type, its name, an eye (show / hide), a delete button (while the
    mouse is over the row) and a chevron that opens its settings. The paint layer's row has an
    eye and "Clear".

    Click a row to select its object (the list stays, the canvas shows the handles); double-click
    it, press Return or click the chevron for its settings. Drag a row to reorder: the order is
    which object the canvas picks where objects overlap. Up / Down move the selection while the
    list has the keyboard focus, Delete removes the selected object, Esc deselects. Right-click
    opens the object's menu (Bring to front, Send to back, Hide, Delete). Hiding, reordering and
    deleting are one undo step each; selecting is none. */
class LayerList final : public juce::Component,
                        public juce::TooltipClient,
                        private juce::ScrollBar::Listener
{
public:
    static constexpr int rowHeight = 30;

    LayerList (FreqArtProcessor& processor, EditorState& state);
    ~LayerList() override;

    /** Reads the document and the selection again (the inspector calls this on every change). */
    void refresh();

    /** The height that shows every row without scrolling. */
    int getContentHeight() const;

    /** Rows are counted from the front-most object (0); the paint layer's row comes after them.
        These are in the list's own coordinates, at the current scroll position (for tests). */
    int getNumObjectRows() const noexcept;
    juce::Rectangle<int> getRowBounds (int row) const;
    juce::Rectangle<int> getEyeBounds (int row) const;
    juce::Rectangle<int> getDeleteBounds (int row) const;
    juce::Rectangle<int> getSettingsBounds (int row) const;
    juce::Rectangle<int> getPaintRowBounds() const;
    juce::Rectangle<int> getPaintEyeBounds() const;
    juce::Rectangle<int> getPaintClearBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    juce::String getTooltip() override;

private:
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayerList)
};

} // namespace freqart::ui
