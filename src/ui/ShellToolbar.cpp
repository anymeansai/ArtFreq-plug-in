#include "ShellToolbar.h"

#include "Actions.h"
#include "ShellHeader.h"

namespace artfreq::ui::shell
{
namespace
{
    constexpr int toolButtonWidth = 36;
    constexpr int groupInset = 2;
    constexpr int sliderGap = 20;
    constexpr int dividerSpace = 16;   // either side of a divider
    constexpr float idleSliderAlpha = 0.45f;
} // namespace

Toolbar::Toolbar (EditorState& s)
    : selectTool ("Select", icons::select()),
      brushTool ("Brush", icons::brush()),
      eraseTool ("Erase", icons::eraser()),
      brushSize (s, BrushSlider::Setting::size),
      brushSoftness (s, BrushSlider::Setting::softness),
      brushStrength (s, BrushSlider::Setting::strength),
      addTextButton ("Add text", icons::text(), "Add text"),
      addImageButton ("Add image", icons::image(), "Add image"),
      clearButton ("Clear", icons::clear(), "Clear"),
      state (s)
{
    setOpaque (true);

    selectTool.setTooltip ("Select, move and resize text and pictures  (V)");
    brushTool.setTooltip ("Brush: paint loudness onto the canvas  (B)");
    eraseTool.setTooltip ("Erase painted strokes  (E)");
    addTextButton.setTooltip ("Add a text object (in the mid band, or another band if that is taken)");
    addImageButton.setTooltip ("Add a picture (" + actions::pictureFormatNames() + "). You can also drop one onto the canvas.");
    clearButton.setTooltip ("Remove everything from the drawing (" + shortcutText ("Cmd+Z") + " brings it back)");

    selectTool.setComponentID ("tool-select");
    brushTool.setComponentID ("tool-brush");
    eraseTool.setComponentID ("tool-erase");
    addTextButton.setComponentID ("add-text");
    addImageButton.setComponentID ("add-image");
    clearButton.setComponentID ("clear");

    const std::pair<IconButton*, Tool> tools[] = { { &selectTool, Tool::select },
                                                   { &brushTool, Tool::brush },
                                                   { &eraseTool, Tool::erase } };

    for (const auto& [button, tool] : tools)
    {
        button->setFlat (true);
        button->setClickingTogglesState (false);
        button->onClick = [this, t = tool] { state.setTool (t); };
        addAndMakeVisible (*button);
    }

    // Touching a brush setting while the Select tool is active means the user wants to paint.
    for (auto* slider : { &brushSize, &brushSoftness, &brushStrength })
    {
        slider->getSlider().onDragStart = [this]
        {
            if (state.getTool() == Tool::select)
                state.setTool (Tool::brush);
        };

        addAndMakeVisible (*slider);
    }

    for (auto* button : { &addTextButton, &addImageButton, &clearButton })
        addAndMakeVisible (*button);

    updateFromState();
    state.addChangeListener (this);
}

Toolbar::~Toolbar()
{
    state.removeChangeListener (this);
}

void Toolbar::setDrawingToolsEnabled (bool shouldBeEnabled)
{
    if (toolsEnabled == shouldBeEnabled)
        return;

    toolsEnabled = shouldBeEnabled;

    for (auto* c : std::initializer_list<juce::Component*> { &selectTool, &brushTool, &eraseTool,
                                                            &brushSize, &brushSoftness, &brushStrength })
        c->setEnabled (shouldBeEnabled);

    repaint (toolGroup.expanded (1));
}

void Toolbar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    drawHorizontalDivider (g, getHeight() - 1, 0, getWidth());

    // The tools share one raised box; the buttons inside are concentric with it.
    const auto group = toolGroup.toFloat();
    const auto corner = theme::controlCorner + static_cast<float> (groupInset);
    g.setColour (theme::raised.withMultipliedAlpha (toolsEnabled && isEnabled() ? 1.0f : 0.6f));
    g.fillRoundedRectangle (group, corner);
    g.setColour (theme::line);
    g.drawRoundedRectangle (group.reduced (0.5f), corner, 1.0f);

    const auto centreY = getHeight() / 2;
    drawVerticalDivider (g, brushDividerX, centreY - 10, centreY + 10);
    drawVerticalDivider (g, clearDividerX, centreY - 10, centreY + 10);
}

void Toolbar::resized()
{
    const auto height = metrics::controlHeight;
    const auto top = (getHeight() - height) / 2;
    auto x = metrics::pad;

    // Tools
    toolGroup = { x, top, 3 * toolButtonWidth + 2 * groupInset, height };
    auto toolX = x + groupInset;

    for (auto* b : { &selectTool, &brushTool, &eraseTool })
    {
        b->setBounds (toolX, top + groupInset, toolButtonWidth, height - 2 * groupInset);
        toolX += toolButtonWidth;
    }

    x = toolGroup.getRight() + dividerSpace;
    brushDividerX = x;
    x += 1 + dividerSpace;

    // Brush settings
    for (auto* slider : { &brushSize, &brushSoftness, &brushStrength })
    {
        const auto w = slider->getIdealWidth();
        slider->setBounds (x, top, w, height);
        x += w + sliderGap;
    }

    // Actions, from the right
    auto right = getWidth() - metrics::pad;
    const auto place = [&] (IconButton& b, const juce::String& label)
    {
        const auto w = iconButtonWidth (label, height);
        b.setBounds (right - w, top, w, height);
        right -= w;
    };

    place (clearButton, "Clear");
    right -= dividerSpace + 1;
    clearDividerX = right;
    right -= dividerSpace;
    place (addImageButton, "Add image");
    right -= 8;
    place (addTextButton, "Add text");
}

void Toolbar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateFromState();
}

void Toolbar::updateFromState()
{
    const auto tool = state.getTool();

    selectTool.setToggleState (tool == Tool::select, juce::dontSendNotification);
    brushTool.setToggleState (tool == Tool::brush, juce::dontSendNotification);
    eraseTool.setToggleState (tool == Tool::erase, juce::dontSendNotification);

    // The brush settings stay usable with the Select tool, but step back visually.
    const auto alpha = tool == Tool::select ? idleSliderAlpha : 1.0f;

    for (auto* slider : { &brushSize, &brushSoftness, &brushStrength })
        slider->setAlpha (alpha);
}

} // namespace artfreq::ui::shell
