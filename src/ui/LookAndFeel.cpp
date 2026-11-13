#include "LookAndFeel.h"

#include "ShellControls.h"

#include <cmath>

namespace freqart::ui
{
namespace
{
    const juce::Identifier popupParentProperty { "freqartPopupParent" };
    const juce::Identifier dangerProperty { "freqartDanger" };

    constexpr int menuItemHeight = 28;
    constexpr int menuBorder = 4;
    constexpr float menuCorner = 8.0f;

    juce::Colour pressed (juce::Colour c) { return c.darker (0.3f); }
} // namespace

FreqArtLookAndFeel::FreqArtLookAndFeel()
{
    const auto none = juce::Colours::transparentBlack;

    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::DocumentWindow::backgroundColourId, theme::bg);

    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::Label::textWhenEditingColourId, theme::text);
    setColour (juce::Label::backgroundColourId, none);
    setColour (juce::Label::backgroundWhenEditingColourId, none);
    setColour (juce::Label::outlineColourId, none);
    setColour (juce::Label::outlineWhenEditingColourId, none);

    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::backgroundColourId, theme::raised);
    setColour (juce::TextEditor::outlineColourId, theme::line);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour (juce::TextEditor::shadowColourId, none);
    setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.38f));
    setColour (juce::TextEditor::highlightedTextColourId, theme::text);
    setColour (juce::CaretComponent::caretColourId, theme::accent.brighter (0.3f));

    setColour (juce::TextButton::buttonColourId, theme::raised);
    setColour (juce::TextButton::buttonOnColourId, theme::accent);
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ToggleButton::textColourId, theme::text);
    setColour (juce::ToggleButton::tickColourId, theme::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, theme::textFaint);

    setColour (juce::ComboBox::backgroundColourId, theme::raised);
    setColour (juce::ComboBox::outlineColourId, theme::line);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::arrowColourId, theme::textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, theme::accent);
    setColour (juce::ComboBox::buttonColourId, theme::raised);

    setColour (juce::PopupMenu::backgroundColourId, none);   // translucent: rounded corners
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::headerTextColourId, theme::textFaint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::raisedHover);
    setColour (juce::PopupMenu::highlightedTextColourId, theme::text);

    setColour (juce::Slider::backgroundColourId, theme::line);
    setColour (juce::Slider::trackColourId, theme::accent);
    setColour (juce::Slider::thumbColourId, theme::text);
    setColour (juce::Slider::rotarySliderFillColourId, theme::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, theme::line);
    setColour (juce::Slider::textBoxTextColourId, theme::textDim);
    setColour (juce::Slider::textBoxBackgroundColourId, none);
    setColour (juce::Slider::textBoxOutlineColourId, none);
    setColour (juce::Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.38f));

    setColour (juce::ScrollBar::thumbColourId, theme::textFaint);
    setColour (juce::ScrollBar::trackColourId, none);
    setColour (juce::ScrollBar::backgroundColourId, none);

    setColour (juce::TooltipWindow::backgroundColourId, theme::raised);
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::TooltipWindow::outlineColourId, theme::line);

    setColour (juce::ListBox::backgroundColourId, theme::panel);
    setColour (juce::ListBox::outlineColourId, theme::line);
    setColour (juce::ListBox::textColourId, theme::text);

    setColour (juce::GroupComponent::outlineColourId, theme::line);
    setColour (juce::GroupComponent::textColourId, theme::textDim);
}

FreqArtLookAndFeel::~FreqArtLookAndFeel() = default;

void FreqArtLookAndFeel::setPopupParent (juce::Component& component)
{
    component.getProperties().set (popupParentProperty, true);
}

juce::Component* FreqArtLookAndFeel::findPopupParent (juce::Component& start)
{
    for (auto* c = &start; c != nullptr; c = c->getParentComponent())
        if (c->getProperties().contains (popupParentProperty))
            return c;

    return nullptr;
}

void FreqArtLookAndFeel::setDanger (juce::Component& component, bool isDanger)
{
    component.getProperties().set (dangerProperty, isDanger);
    component.repaint();
}

bool FreqArtLookAndFeel::isDanger (const juce::Component& component)
{
    return static_cast<bool> (component.getProperties().getWithDefault (dangerProperty, false));
}

juce::Font FreqArtLookAndFeel::controlFont()
{
    return fonts::ui (theme::controlSize);
}

//==============================================================================
void FreqArtLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto enabled = slider.isEnabled();
    const auto hot = enabled && (slider.isMouseOverOrDragging());
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto arcRadius = size * 0.5f - 2.0f;
    const auto arcWidth = 3.0f;
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Track and value arcs.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::line);
    g.strokePath (track, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (enabled ? theme::accent : theme::textFaint);
        g.strokePath (value, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The body: a raised disc with a 1 px outline and a pointer.
    const auto bodyRadius = arcRadius - 6.0f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (slider.isMouseButtonDown() ? theme::raised : hot ? theme::raisedHover : theme::raised);
    g.fillEllipse (body);
    g.setColour (slider.hasKeyboardFocus (false) ? theme::accent : hot ? theme::textFaint : theme::line);
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    juce::Path pointer;
    const auto inner = centre.getPointOnCircumference (bodyRadius * 0.28f, angle);
    const auto outer = centre.getPointOnCircumference (bodyRadius * 0.78f, angle);
    pointer.startNewSubPath (inner);
    pointer.lineTo (outer);
    g.setColour (enabled ? theme::text : theme::textFaint);
    g.strokePath (pointer, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void FreqArtLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto enabled = slider.isEnabled();
    const auto hot = enabled && slider.isMouseOverOrDragging();

    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (theme::raised);
        g.fillRoundedRectangle (bounds, theme::controlCorner);

        auto filled = bounds;

        if (style == juce::Slider::LinearBar)
            filled.setRight (sliderPos);
        else
            filled.setTop (sliderPos);

        g.setColour ((enabled ? theme::accent : theme::textFaint).withAlpha (0.3f));
        g.fillRoundedRectangle (filled, theme::controlCorner);
        g.setColour (hot ? theme::textFaint : theme::line);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::controlCorner, 1.0f);
        return;
    }

    if (style == juce::Slider::TwoValueHorizontal || style == juce::Slider::TwoValueVertical
        || style == juce::Slider::ThreeValueHorizontal || style == juce::Slider::ThreeValueVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto horizontal = slider.isHorizontal();
    const auto trackThickness = 4.0f;
    const auto thumbRadius = static_cast<float> (getSliderThumbRadius (slider)) + (hot ? 1.0f : 0.0f);
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();

    juce::Rectangle<float> track, fill;
    juce::Point<float> thumb;

    if (horizontal)
    {
        const auto cy = std::round (area.getCentreY());
        track = { area.getX(), cy - trackThickness * 0.5f, area.getWidth(), trackThickness };
        fill = track.withRight (sliderPos);
        thumb = { sliderPos, cy };
    }
    else
    {
        const auto cx = std::round (area.getCentreX());
        track = { cx - trackThickness * 0.5f, area.getY(), trackThickness, area.getHeight() };
        fill = track.withTop (sliderPos);
        thumb = { cx, sliderPos };
    }

    g.setColour (theme::line);
    g.fillRoundedRectangle (track, trackThickness * 0.5f);
    g.setColour (enabled ? theme::accent : theme::textFaint);
    g.fillRoundedRectangle (fill, trackThickness * 0.5f);

    // The thumb: a light disc, ringed in the background colour so it stands off the track.
    const auto disc = juce::Rectangle<float> (thumbRadius * 2.0f, thumbRadius * 2.0f).withCentre (thumb);
    g.setColour (theme::panel);
    g.fillEllipse (disc.expanded (1.5f));
    g.setColour (enabled ? (slider.isMouseButtonDown() ? theme::text.darker (0.15f) : theme::text) : theme::textFaint);
    g.fillEllipse (disc);
}

int FreqArtLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.isBar() ? 0 : 6;
}

juce::Slider::SliderLayout FreqArtLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (auto* knob = dynamic_cast<shell::Knob*> (&slider))
    {
        juce::Slider::SliderLayout layout;
        layout.sliderBounds = knob->getDialBounds();
        layout.textBoxBounds = knob->getValueBounds();
        return layout;
    }

    return juce::LookAndFeel_V4::getSliderLayout (slider);
}

juce::Label* FreqArtLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    if (dynamic_cast<shell::Knob*> (&slider) != nullptr)
        return new shell::ValueLabel();

    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (fonts::mono (12.0f));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return label;
}

//==============================================================================
void FreqArtLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto fill = editor.findColour (juce::TextEditor::backgroundColourId);

    if (fill.isTransparent())
        return;

    // A field inside a value label fills the label: no rounded box of its own.
    if (dynamic_cast<juce::Label*> (editor.getParentComponent()) != nullptr)
    {
        g.setColour (theme::raised);
        g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), 4.0f);
        return;
    }

    g.setColour (editor.isEnabled() ? fill : fill.withMultipliedAlpha (0.5f));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), theme::controlCorner);
}

void FreqArtLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);

    if (dynamic_cast<juce::Label*> (editor.getParentComponent()) != nullptr)
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
        return;
    }

    const auto outline = editor.findColour (juce::TextEditor::outlineColourId);
    const auto focused = editor.findColour (juce::TextEditor::focusedOutlineColourId);

    if (outline.isTransparent() && focused.isTransparent())
        return;

    if (editor.isEnabled() && editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
        g.setColour (focused);
    else if (editor.isEnabled() && editor.isMouseOver (true))
        g.setColour (theme::textFaint);
    else
        g.setColour (outline);

    g.drawRoundedRectangle (bounds, theme::controlCorner, 1.0f);
}

//==============================================================================
void FreqArtLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto enabled = button.isEnabled();
    const auto danger = isDanger (button);
    const auto on = button.getToggleState();

    auto fill = theme::raised;
    auto outline = theme::line;

    if (! enabled)
    {
        fill = theme::raised.withMultipliedAlpha (0.6f);
    }
    else if (on)
    {
        fill = theme::accent.withAlpha (isDown ? 0.32f : isHighlighted ? 0.26f : 0.2f);
        outline = theme::accent.withAlpha (0.75f);
    }
    else if (isDown)
    {
        fill = danger ? theme::danger.darker (0.2f) : pressed (theme::raised);
        outline = danger ? theme::danger : theme::line;
    }
    else if (isHighlighted)
    {
        fill = danger ? theme::danger : theme::raisedHover;
        outline = danger ? theme::danger : theme::textFaint.withAlpha (0.6f);
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, theme::controlCorner);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds, theme::controlCorner, 1.0f);
}

void FreqArtLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool isHighlighted, bool isDown)
{
    const auto enabled = button.isEnabled();
    const auto dangerHot = isDanger (button) && enabled && (isHighlighted || isDown);

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (! enabled ? theme::textFaint : dangerHot ? juce::Colours::white : theme::text);
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (8, 0), juce::Justification::centred, true);
}

juce::Font FreqArtLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts::ui (buttonHeight < 24 ? 12.0f : theme::controlSize, fonts::UiWeight::medium);
}

void FreqArtLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool isDown)
{
    const auto boxSize = 16.0f;
    const auto top = (static_cast<float> (button.getHeight()) - boxSize) * 0.5f;

    drawTickBox (g, button, 0.0f, std::round (top), boxSize, boxSize, button.getToggleState(),
                 button.isEnabled(), isHighlighted, isDown);

    g.setFont (fonts::ui (theme::controlSize));
    g.setColour (button.isEnabled() ? theme::text : theme::textFaint);
    g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (24), juce::Justification::centredLeft, true);
}

void FreqArtLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h,
                                      bool ticked, bool isEnabled, bool isHighlighted, bool isDown)
{
    const auto box = juce::Rectangle<float> (x, y, w, h).reduced (0.5f);

    g.setColour (ticked ? (isEnabled ? theme::accent : theme::textFaint)
                        : isDown ? pressed (theme::raised) : isHighlighted ? theme::raisedHover : theme::raised);
    g.fillRoundedRectangle (box, 4.0f);

    if (! ticked)
    {
        g.setColour (isHighlighted ? theme::textFaint : theme::line);
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        return;
    }

    shell::drawIcon (g, shell::shellIcons::check(), box.reduced (2.0f), juce::Colours::white);
}

//==============================================================================
void FreqArtLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                       int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    const auto enabled = box.isEnabled();
    const auto hot = enabled && box.isMouseOver (true);
    const auto open = box.isPopupActive();

    g.setColour (isButtonDown ? pressed (theme::raised) : hot ? theme::raisedHover : theme::raised);
    g.fillRoundedRectangle (bounds, theme::controlCorner);
    g.setColour (open || box.hasKeyboardFocus (false) ? theme::accent : hot ? theme::textFaint.withAlpha (0.6f) : theme::line);
    g.drawRoundedRectangle (bounds, theme::controlCorner, 1.0f);

    const auto arrowBox = juce::Rectangle<float> (static_cast<float> (width - 24), (static_cast<float> (height) - 16.0f) * 0.5f, 16.0f, 16.0f);
    shell::drawIcon (g, shell::shellIcons::chevronDown(), arrowBox,
                     ! enabled ? theme::textFaint : hot || open ? theme::text : theme::textDim);
}

juce::Font FreqArtLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return fonts::ui (box.getHeight() < 24 ? 12.0f : theme::controlSize);
}

void FreqArtLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setBorderSize ({ 0, 8, 0, 0 });
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, box.isEnabled() ? theme::text : theme::textFaint);
}

void FreqArtLookAndFeel::drawComboBoxTextWhenNothingSelected (juce::Graphics& g, juce::ComboBox& box, juce::Label& label)
{
    g.setColour (theme::textFaint);
    g.setFont (getComboBoxFont (box));
    g.drawText (box.getTextWhenNothingSelected(), label.getBounds().withTrimmedLeft (8),
                juce::Justification::centredLeft, true);
}

juce::PopupMenu::Options FreqArtLookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label)
{
    // The menu opens beside the box (below, or above when there is no room), never over it with
    // the ticked item scrolled onto the box: the tick shows the current choice.
    auto options = juce::LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label)
                       .withStandardItemHeight (menuItemHeight)
                       .withItemThatMustBeVisible (0);

    // Inside the window: scaled like the rest and never a separate desktop window.
    if (auto* parent = findPopupParent (box))
        options = options.withParentComponent (parent);

    return options;
}

//==============================================================================
juce::Font FreqArtLookAndFeel::getPopupMenuFont()
{
    return fonts::ui (theme::controlSize);
}

void FreqArtLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                             const juce::PopupMenu::Options&)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, menuCorner);
    g.setColour (theme::line.brighter (0.15f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), menuCorner, 1.0f);
}

void FreqArtLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                       bool isHighlighted, const juce::PopupMenu::Item& item,
                                                       const juce::PopupMenu::Options&)
{
    if (item.isSeparator)
    {
        g.setColour (theme::line);
        g.fillRect (area.getX() + 8, area.getCentreY(), area.getWidth() - 16, 1);
        return;
    }

    const auto row = area.reduced (4, 1).toFloat();

    if (isHighlighted && item.isEnabled)
    {
        g.setColour (theme::raisedHover);
        g.fillRoundedRectangle (row, 4.0f);
    }

    if (item.isTicked)
        shell::drawIcon (g, shell::shellIcons::check(),
                         { row.getX() + 6.0f, row.getCentreY() - 7.0f, 14.0f, 14.0f }, theme::accent.brighter (0.3f));

    g.setFont (getPopupMenuFont());
    g.setColour (! item.isEnabled ? theme::textFaint : item.isTicked || isHighlighted ? theme::text : theme::textDim);
    g.drawText (item.text, row.withTrimmedLeft (28.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);

    if (item.subMenu != nullptr)
    {
        juce::Path arrow;
        arrow.startNewSubPath (row.getRight() - 14.0f, row.getCentreY() - 4.0f);
        arrow.lineTo (row.getRight() - 10.0f, row.getCentreY());
        arrow.lineTo (row.getRight() - 14.0f, row.getCentreY() + 4.0f);
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }
}

void FreqArtLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                                const juce::String& sectionName,
                                                                const juce::PopupMenu::Options&)
{
    shell::drawCaps (g, sectionName, area.withTrimmedLeft (10), juce::Justification::bottomLeft, theme::textFaint);
}

void FreqArtLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                                               int& idealWidth, int& idealHeight,
                                                               const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 40;
        idealHeight = 9;
        return;
    }

    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : menuItemHeight;
    idealWidth = shell::textWidth (getPopupMenuFont(), text) + 28 + 24;
}

int FreqArtLookAndFeel::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&)
{
    return menuBorder;
}

void FreqArtLookAndFeel::preparePopupMenuWindow (juce::Component& window)
{
    // Inside the editor the menu is a child component, so it can be translucent (rounded corners).
    if (window.getParentComponent() != nullptr)
        window.setOpaque (false);
}

void FreqArtLookAndFeel::drawResizableFrame (juce::Graphics&, int, int, const juce::BorderSize<int>&)
{
    // JUCE draws this frame over a menu shown inside a component; the menu has its own edge.
}

//==============================================================================
void FreqArtLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();

    g.setColour (theme::raised);
    g.fillRoundedRectangle (bounds, theme::controlCorner);
    g.setColour (theme::line.brighter (0.2f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), theme::controlCorner, 1.0f);

    g.setColour (theme::text);
    g.setFont (fonts::ui (12.0f));
    g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (10, 6), juce::Justification::centredLeft, 4, 1.0f);
}

juce::Rectangle<int> FreqArtLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    const auto font = fonts::ui (12.0f);
    const auto maxWidth = 280;
    const auto lineHeight = 16;
    const auto width = juce::jmin (maxWidth, shell::textWidth (font, tipText)) + 20;
    const auto lines = juce::jmax (1, static_cast<int> (std::ceil (static_cast<float> (shell::textWidth (font, tipText))
                                                                    / static_cast<float> (maxWidth))));
    const auto height = lines * lineHeight + 12;

    auto x = screenPos.x - width / 2;
    auto y = screenPos.y + 18;

    if (y + height > parentArea.getBottom())
        y = screenPos.y - height - 10;

    x = juce::jlimit (parentArea.getX() + 4, juce::jmax (parentArea.getX() + 4, parentArea.getRight() - width - 4), x);
    return { x, y, width, height };
}

//==============================================================================
int FreqArtLookAndFeel::getDefaultScrollbarWidth()
{
    return 10;
}

void FreqArtLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                        bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (bar);

    auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize)
                                     : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height);
    const auto thickness = isMouseOver || isMouseDown ? 6.0f : 4.0f;
    auto r = thumb.toFloat();

    r = isScrollbarVertical ? r.withSizeKeepingCentre (thickness, juce::jmax (0.0f, r.getHeight() - 4.0f))
                            : r.withSizeKeepingCentre (juce::jmax (0.0f, r.getWidth() - 4.0f), thickness);

    g.setColour (isMouseDown ? theme::textDim : isMouseOver ? theme::textFaint.brighter (0.2f) : theme::textFaint);
    g.fillRoundedRectangle (r, thickness * 0.5f);
}

//==============================================================================
void FreqArtLookAndFeel::drawCornerResizer (juce::Graphics& g, int width, int height, bool isMouseOver, bool isMouseDragging)
{
    // Three short diagonal strokes in the corner.
    const auto w = static_cast<float> (width), h = static_cast<float> (height);
    g.setColour (isMouseOver || isMouseDragging ? theme::textDim : theme::textFaint);

    for (int i = 0; i < 3; ++i)
    {
        const auto inset = 3.0f + static_cast<float> (i) * 4.0f;
        g.drawLine (w - inset, h - 2.0f, w - 2.0f, h - inset, 1.0f);
    }
}

} // namespace freqart::ui
