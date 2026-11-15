#pragma once

// What the editor's parts share that is not part of the drawing: the active tool, the brush
// and the selected object. Lives as long as the editor.

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include <functional>

namespace artfreq::ui
{

enum class Tool
{
    select = 0,
    brush = 1,
    erase = 2
};

class EditorState final : public juce::ChangeBroadcaster
{
public:
    Tool getTool() const noexcept { return tool; }
    void setTool (Tool newTool)
    {
        if (tool != newTool) { tool = newTool; sendChangeMessage(); }
    }

    // Brush, in screen pixels / fractions.
    float getBrushSize() const noexcept { return brushSize; }      // diameter, 2..64
    float getBrushSoftness() const noexcept { return softness; }   // 0..1
    float getBrushStrength() const noexcept { return strength; }   // 0..1
    void setBrushSize (float v)     { v = juce::jlimit (2.0f, 64.0f, v); if (! juce::exactlyEqual (brushSize, v)) { brushSize = v; sendChangeMessage(); } }
    void setBrushSoftness (float v) { v = juce::jlimit (0.0f, 1.0f, v);  if (! juce::exactlyEqual (softness, v))  { softness = v;  sendChangeMessage(); } }
    void setBrushStrength (float v) { v = juce::jlimit (0.0f, 1.0f, v);  if (! juce::exactlyEqual (strength, v))  { strength = v;  sendChangeMessage(); } }

    /** The selected object (null Uuid = nothing). Selecting an object this way (on the canvas,
        a new object, the editor's selectObject) shows its settings in the inspector, even if it
        was already selected from the layer list; see selectInLayerList. */
    const juce::Uuid& getSelected() const noexcept { return selected; }
    void setSelected (const juce::Uuid& id)
    {
        if (selected != id || layerListOpen) { selected = id; layerListOpen = false; sendChangeMessage(); }
    }
    bool hasSelection() const noexcept { return ! selected.isNull(); }

    /** Selects an object from the inspector's layer list, which stays open (so the selection
        can be walked through the list, and seen on the canvas, before opening one). */
    void selectInLayerList (const juce::Uuid& id)
    {
        const auto open = ! id.isNull();

        if (selected != id || layerListOpen != open) { selected = id; layerListOpen = open; sendChangeMessage(); }
    }

    /** Whether the inspector shows the layer list: always with nothing selected; with a
        selection, after picking it in the list or going back to the list from its settings. */
    bool isLayerListShown() const noexcept { return layerListOpen || ! hasSelection(); }
    void showLayerList (bool shouldShow)
    {
        shouldShow = shouldShow && hasSelection();   // with nothing selected the list shows anyway

        if (layerListOpen != shouldShow) { layerListOpen = shouldShow; sendChangeMessage(); }
    }

    /** Asks the inspector to put the keyboard focus in the text field (after "Add text"). */
    std::function<void()> focusTextField;

    /** Shows a short message over the canvas for a few seconds (a picture that could not be
        read). Set by the editor. */
    std::function<void (const juce::String&)> showNotice;

private:
    Tool tool = Tool::select;
    float brushSize = 16.0f;
    float softness = 0.5f;
    float strength = 1.0f;
    juce::Uuid selected = juce::Uuid::null();
    bool layerListOpen = false;   // only meaningful with a selection
};

} // namespace artfreq::ui
