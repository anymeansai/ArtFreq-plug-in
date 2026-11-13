#pragma once

// The toolbar under the header: the tools (Select, Brush, Erase), the brush settings, adding
// text and pictures, and clearing the drawing. See docs/DESIGN.md, "Layout".

#include "EditorState.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace freqart::ui::shell
{

class Toolbar final : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit Toolbar (EditorState& state);
    ~Toolbar() override;

    /** The tools and brush settings have nothing to act on in the live view: they are disabled
        there, while adding and clearing stay available. */
    void setDrawingToolsEnabled (bool shouldBeEnabled);

    void paint (juce::Graphics&) override;
    void resized() override;

    IconButton selectTool;
    IconButton brushTool;
    IconButton eraseTool;
    BrushSlider brushSize;
    BrushSlider brushSoftness;
    BrushSlider brushStrength;
    IconButton addTextButton;
    IconButton addImageButton;
    IconButton clearButton;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateFromState();

    EditorState& state;
    juce::Rectangle<int> toolGroup;   // the raised box behind the three tools
    int brushDividerX = 0, clearDividerX = 0;
    bool toolsEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toolbar)
};

} // namespace freqart::ui::shell
