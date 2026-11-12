#pragma once

// What the editor's parts share that is not part of the drawing: the active tool, the brush
// and the selected object. Lives as long as the editor.

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include <functional>

namespace freqart::ui
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

    /** The selected object (null Uuid = nothing). */
    const juce::Uuid& getSelected() const noexcept { return selected; }
    void setSelected (const juce::Uuid& id)
    {
        if (selected != id) { selected = id; sendChangeMessage(); }
    }
    bool hasSelection() const noexcept { return ! selected.isNull(); }

    /** Asks the inspector to put the keyboard focus in the text field (after "Add text"). */
    std::function<void()> focusTextField;

private:
    Tool tool = Tool::select;
    float brushSize = 16.0f;
    float softness = 0.5f;
    float strength = 1.0f;
    juce::Uuid selected = juce::Uuid::null();
};

} // namespace freqart::ui
