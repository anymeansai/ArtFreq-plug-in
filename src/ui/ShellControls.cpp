#include "ShellControls.h"

#include "LookAndFeel.h"

#include <cmath>
#include <utility>

namespace artfreq::ui::shell
{
namespace
{
    constexpr double dragPixelsForFullRange = 240.0;
    constexpr double fineDragScale = 0.1;
    constexpr float fineWheelScale = 0.2f;

    bool isFineAdjust (const juce::ModifierKeys& mods) noexcept
    {
        return mods.isShiftDown();
    }

    juce::Path strokedIcon (const char* svg, float thickness = 2.0f)
    {
        juce::Path stroked;
        juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (stroked, juce::Drawable::parseSVGPath (svg));
        return stroked;
    }
} // namespace

//==============================================================================
void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
               juce::Justification justification, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (theme::labelFont());
    g.drawText (text.toUpperCase(), area, justification, false);
}

int capsWidth (const juce::String& text)
{
    return textWidth (theme::labelFont(), text.toUpperCase());
}

int textWidth (const juce::Font& font, const juce::String& text)
{
    return static_cast<int> (std::ceil (juce::GlyphArrangement::getStringWidth (font, text)));
}

void drawVerticalDivider (juce::Graphics& g, int x, int top, int bottom, juce::Colour colour)
{
    g.setColour (colour);
    g.fillRect (x, top, 1, bottom - top);
}

void drawHorizontalDivider (juce::Graphics& g, int y, int left, int right, juce::Colour colour)
{
    g.setColour (colour);
    g.fillRect (left, y, right - left, 1);
}

int iconButtonWidth (const juce::String& label, int height)
{
    if (label.isEmpty())
        return height;

    // Icon, a gap of 6 and the label, with 10 px of padding either side (see IconButton).
    const auto iconSize = height <= 28 ? 16 : 18;
    return 10 + iconSize + 6 + textWidth (fonts::ui (theme::controlSize, fonts::UiWeight::medium), label) + 12;
}

void drawIcon (juce::Graphics& g, const juce::Path& icon24, juce::Rectangle<float> box, juce::Colour colour)
{
    // Whole-pixel placement keeps the strokes as sharp as the scale allows.
    const auto snapped = juce::Rectangle<float> (std::round (box.getX()), std::round (box.getY()),
                                                 std::round (box.getWidth()), std::round (box.getHeight()));
    const auto scale = juce::jmin (snapped.getWidth(), snapped.getHeight()) / 24.0f;

    g.setColour (colour);
    g.fillPath (icon24, juce::AffineTransform::scale (scale).translated (snapped.getX(), snapped.getY()));
}

namespace shellIcons
{
    juce::Path chevronDown()  { return strokedIcon ("M7 10 L12 15 L17 10"); }
    juce::Path check()        { return strokedIcon ("M5 12.5 L10 17.5 L19 7"); }
    juce::Path plus()         { return strokedIcon ("M12 5.5 L12 18.5 M5.5 12 L18.5 12"); }

    juce::Path repeat()
    {
        // Two arrows chasing each other round: play again from the top.
        return strokedIcon ("M17 2.5 L20.5 6 L17 9.5 M3.5 11.5 L3.5 10 Q3.5 6 7.5 6 L20.5 6 "
                            "M7 21.5 L3.5 18 L7 14.5 M20.5 12.5 L20.5 14 Q20.5 18 16.5 18 L3.5 18");
    }
    juce::Path dragHandle()   { return strokedIcon ("M9 6 L9 6.1 M15 6 L15 6.1 M9 12 L9 12.1 M15 12 L15 12.1 M9 18 L9 18.1 M15 18 L15 18.1", 2.6f); }

    juce::Path waveFile()
    {
        // A sound wave: what the chip hands to the DAW.
        return strokedIcon ("M3 12 L5 12 M7 8 L7 16 M10.5 4.5 L10.5 19.5 M14 7.5 L14 16.5 M17.5 10 L17.5 14 M21 12 L19.5 12");
    }
} // namespace shellIcons

juce::String formatNumber (double value, int maxDecimals)
{
    auto text = juce::String (value, maxDecimals);

    if (text.containsChar ('.'))
    {
        text = text.trimCharactersAtEnd ("0");

        if (text.endsWithChar ('.'))
            text = text.dropLastCharacters (1);
    }

    return text == "-0" ? juce::String ("0") : text;
}

double parseNumber (const juce::String& text, double fallback)
{
    const auto cleaned = text.replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-")
                             .replaceCharacter (',', '.')
                             .trim();

    if (! cleaned.containsAnyOf ("0123456789"))
        return fallback;

    auto number = cleaned.retainCharacters ("0123456789.-").getDoubleValue();

    // "2.5k" or "2.5 kHz" means thousands.
    if (cleaned.toLowerCase().containsChar ('k'))
        number *= 1000.0;

    return number;
}

//==============================================================================
ValueLabel::ValueLabel()
{
    setJustificationType (juce::Justification::centred);
    setKeyboardType (juce::TextInputTarget::decimalKeyboard);
    setMinimumHorizontalScale (1.0f);
    setMouseClickGrabsKeyboardFocus (false);
    setFont (fonts::mono (12.0f));
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::Label::textWhenEditingColourId, theme::text);
}

void ValueLabel::mouseUp (const juce::MouseEvent&)
{
    // The slider makes its text box editable on a single click; a double-click is wanted.
}

void ValueLabel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isEditable() && isEnabled() && ! e.mods.isPopupMenu())
        showEditor();
}

juce::TextEditor* ValueLabel::createEditorComponent()
{
    auto* textEditor = juce::Label::createEditorComponent();

    textEditor->setJustification (juce::Justification::centred);
    textEditor->setPopupMenuEnabled (false);
    textEditor->setScrollbarsShown (false);
    textEditor->setInputRestrictions (12);
    textEditor->setIndents (0, 0);
    textEditor->setBorder ({});
    textEditor->setFont (fonts::mono (12.0f));

    return textEditor;
}

//==============================================================================
Knob::Knob (const juce::String& captionText)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow),
      caption (captionText)
{
    setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setTextBoxStyle (juce::Slider::TextBoxBelow, false, width, 16);
    setTextBoxIsEditable (true);
    setScrollWheelEnabled (true);
    setPopupMenuEnabled (false);
    setSize (width, height);
}

juce::Rectangle<int> Knob::getDialBounds() const noexcept
{
    return { (getWidth() - dialSize) / 2, 16, dialSize, dialSize };
}

juce::Rectangle<int> Knob::getValueBounds() const noexcept
{
    return { 0, 64, getWidth(), 16 };
}

void Knob::paint (juce::Graphics& g)
{
    juce::Slider::paint (g);
    drawCaps (g, caption, { 0, 0, getWidth(), 12 }, juce::Justification::centred,
              isEnabled() ? theme::textDim : theme::textFaint);
}

bool Knob::hitTest (int x, int y)
{
    const auto point = juce::Point<int> (x, y);
    return getValueBounds().contains (point) || getDialBounds().expanded (4).contains (point);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    lastDragPosition = e.position;
    dragProportion = valueToProportionOfLength (getValue());
    juce::Slider::mouseDown (e);   // starts the gesture (or resets with a double-click)
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;

    // Relative movement, so that pressing or releasing Shift never makes the value jump.
    const auto travel = static_cast<double> ((e.position.x - lastDragPosition.x) + (lastDragPosition.y - e.position.y));
    lastDragPosition = e.position;

    dragProportion = juce::jlimit (0.0, 1.0, dragProportion + travel / dragPixelsForFullRange
                                                                  * (isFineAdjust (e.mods) ? fineDragScale : 1.0));

    setValue (proportionOfLengthToValue (dragProportion), juce::sendNotificationSync);
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto adjusted = wheel;

   #if JUCE_MAC
    // With Shift held, macOS reports a vertical wheel as a horizontal one.
    if (e.mods.isShiftDown() && juce::exactlyEqual (adjusted.deltaY, 0.0f))
        std::swap (adjusted.deltaX, adjusted.deltaY);
   #endif

    if (isFineAdjust (e.mods))
    {
        adjusted.deltaX *= fineWheelScale;
        adjusted.deltaY *= fineWheelScale;
    }

    juce::Slider::mouseWheelMove (e, adjusted);
}

//==============================================================================
NumberField::NumberField (juce::RangedAudioParameter& p, Options o)
    : parameter (p),
      options (std::move (o)),
      attachment (p, [this] (float newValue)
                     {
                         value = static_cast<double> (newValue);
                         repaint();
                     })
{
    if (options.format == nullptr)
        options.format = [] (double v) { return formatNumber (v, 2); };

    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    attachment.sendInitialUpdate();
}

NumberField::~NumberField()
{
    if (dragging && dragMoved)
        attachment.endGesture();

    if (editor != nullptr)
        editor->removeListener (this);
}

bool NumberField::canEdit() const noexcept
{
    return isEnabled() && ! following;
}

void NumberField::setFollowing (bool isFollowing, const juce::String& text)
{
    if (following == isFollowing && followingText == text)
        return;

    following = isFollowing;
    followingText = text;

    if (following && editor != nullptr)
        closeEditor (false);

    setMouseCursor (following ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
    repaint();
}

void NumberField::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto active = canEdit();

    g.setColour (active && (hovered || dragging) ? theme::raisedHover : theme::raised);
    g.fillRoundedRectangle (bounds, theme::controlCorner);

    g.setColour (dragging || editor != nullptr ? theme::accent : theme::line);
    g.drawRoundedRectangle (bounds, theme::controlCorner, 1.0f);

    if (editor != nullptr)
        return;

    const auto text = following ? followingText : options.format (value);
    const auto font = fonts::mono (options.textSize);
    const auto unitFont = fonts::ui (theme::labelSize, fonts::UiWeight::medium);
    const auto unitGap = options.unit.isEmpty() ? 0 : 4;
    const auto total = textWidth (font, text) + unitGap + (options.unit.isEmpty() ? 0 : textWidth (unitFont, options.unit));
    auto x = (getWidth() - total) / 2;

    g.setFont (font);
    g.setColour (! isEnabled() ? theme::textFaint : following ? theme::textDim : theme::text);
    g.drawText (text, x, 0, textWidth (font, text), getHeight(), juce::Justification::centredLeft, false);

    if (options.unit.isNotEmpty())
    {
        x += textWidth (font, text) + unitGap;
        g.setFont (unitFont);
        g.setColour (theme::textFaint);
        g.drawText (options.unit, x, 1, getWidth() - x, getHeight(), juce::Justification::centredLeft, false);
    }
}

void NumberField::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds().reduced (2));
}

void NumberField::mouseEnter (const juce::MouseEvent&)   { hovered = true; repaint(); }
void NumberField::mouseExit (const juce::MouseEvent&)    { hovered = false; repaint(); }

void NumberField::mouseDown (const juce::MouseEvent& e)
{
    if (! canEdit() || e.mods.isPopupMenu())
        return;

    dragging = true;
    dragMoved = false;
    dragValue = value;
    lastDragPosition = e.position;
    repaint();
}

void NumberField::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto travel = static_cast<double> ((lastDragPosition.y - e.position.y) + (e.position.x - lastDragPosition.x));
    lastDragPosition = e.position;

    if (std::abs (travel) <= 0.0)
        return;

    // The gesture starts with the first movement: a click that only opens the editor is not
    // an automation touch.
    if (! dragMoved)
    {
        dragMoved = true;
        attachment.beginGesture();
    }

    const auto fine = isFineAdjust (e.mods) ? options.fineFactor : 1.0;
    const auto range = parameter.getNormalisableRange();

    if (options.logDrag)
        dragValue *= std::pow (2.0, travel / 100.0 * fine);
    else
        dragValue += travel * options.dragUnitsPerPixel * fine;

    dragValue = juce::jlimit (static_cast<double> (range.start), static_cast<double> (range.end), dragValue);

    auto snapped = dragValue;

    if (options.snap > 0.0 && ! isFineAdjust (e.mods))
        snapped = std::round (dragValue / options.snap) * options.snap;

    attachment.setValueAsPartOfGesture (static_cast<float> (snapped));
}

void NumberField::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    dragging = false;

    if (dragMoved)
        attachment.endGesture();

    repaint();

    if (options.editOnSingleClick && ! dragMoved && e.getNumberOfClicks() == 1 && ! e.mouseWasDraggedSinceMouseDown())
        showEditor();
}

void NumberField::mouseDoubleClick (const juce::MouseEvent&)
{
    if (canEdit())
        showEditor();
}

void NumberField::enablementChanged()
{
    if (! isEnabled() && editor != nullptr)
        closeEditor (false);

    repaint();
}

void NumberField::showEditor()
{
    if (! canEdit() || editor != nullptr)
        return;

    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (fonts::mono (options.textSize));
    editor->setJustification (juce::Justification::centred);
    editor->setIndents (0, 0);
    editor->setBorder ({});
    editor->setPopupMenuEnabled (false);
    editor->setScrollbarsShown (false);
    editor->setInputRestrictions (12);
    editor->setSelectAllWhenFocused (true);
    editor->setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor->setText (options.format (value), false);
    editor->addListener (this);
    addAndMakeVisible (*editor);
    resized();
    repaint();

    if (isShowing())
        editor->grabKeyboardFocus();

    editor->selectAll();
}

void NumberField::closeEditor (bool commit)
{
    if (editor == nullptr)
        return;

    // Take the editor out first: deleting it moves the focus, which calls back in here.
    auto outgoing = std::move (editor);
    outgoing->removeListener (this);

    if (commit)
    {
        const auto range = parameter.getNormalisableRange();
        auto typed = parseNumber (outgoing->getText(), value);

        // A range that only goes below zero (Depth) takes "60" to mean -60.
        if (range.end <= 0.0f)
            typed = -std::abs (typed);

        attachment.setValueAsCompleteGesture (juce::jlimit (range.start, range.end, static_cast<float> (typed)));
    }

    removeChildComponent (outgoing.get());
    outgoing.reset();
    repaint();
}

void NumberField::textEditorReturnKeyPressed (juce::TextEditor&)   { closeEditor (true); }
void NumberField::textEditorEscapeKeyPressed (juce::TextEditor&)   { closeEditor (false); }
void NumberField::textEditorFocusLost (juce::TextEditor&)          { closeEditor (true); }

//==============================================================================
namespace
{
    constexpr float chipIconSize = 14.0f;
    constexpr int chipIconGap = 5;

    juce::Font chipFont (bool on)
    {
        // As a segment's label: semi-bold when chosen.
        return fonts::ui (theme::controlSize, on ? fonts::UiWeight::semiBold : fonts::UiWeight::medium);
    }
} // namespace

ToggleChip::ToggleChip (const juce::String& label, juce::Path icon24)
    : juce::Button (label), icon (std::move (icon24))
{
    setButtonText (label);
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);
}

void ToggleChip::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto enabled = isEnabled();
    const auto on = getToggleState();
    const auto outer = getLocalBounds().toFloat();

    highlighted = highlighted && enabled;
    down = down && enabled;

    // The container of a segmented control, with its one segment chosen or hovered.
    g.setColour (theme::raised);
    g.fillRoundedRectangle (outer, theme::controlCorner);
    g.setColour (theme::line);
    g.drawRoundedRectangle (outer.reduced (0.5f), theme::controlCorner, 1.0f);

    const auto inner = outer.reduced (2.0f);
    const auto innerCorner = theme::controlCorner - 2.0f;

    if (on)
    {
        const auto colour = enabled ? theme::accent : theme::textFaint;
        g.setColour (colour.withAlpha (down ? 0.32f : highlighted ? 0.28f : 0.22f));
        g.fillRoundedRectangle (inner, innerCorner);
        g.setColour (colour.withAlpha (0.75f));
        g.drawRoundedRectangle (inner.reduced (0.5f), innerCorner, 1.0f);
    }
    else if (highlighted || down)
    {
        g.setColour (down ? theme::raised.darker (0.25f) : theme::raisedHover);
        g.fillRoundedRectangle (inner, innerCorner);
    }

    // Icon and label centred together.
    const auto font = chipFont (on);
    const auto labelWidth = textWidth (font, getButtonText());
    const auto contentWidth = chipIconSize + static_cast<float> (chipIconGap + labelWidth);
    const auto left = std::round ((outer.getWidth() - contentWidth) * 0.5f);
    const auto colour = ! enabled ? theme::textFaint : on || highlighted ? theme::text : theme::textDim;

    drawIcon (g, icon, juce::Rectangle<float> (left, outer.getCentreY() - chipIconSize * 0.5f, chipIconSize, chipIconSize), colour);

    g.setColour (colour);
    g.setFont (font);
    g.drawText (getButtonText(), juce::roundToInt (left + chipIconSize) + chipIconGap, 0, labelWidth + 2, getHeight(),
                juce::Justification::centredLeft, false);
}

//==============================================================================
ChoiceSegments::ChoiceSegments (SegmentedControl& c, juce::RangedAudioParameter& parameter)
    : control (c),
      attachment (parameter, [this] (float newValue)
                             {
                                 control.setSelectedIndex (juce::roundToInt (newValue), juce::dontSendNotification);
                             })
{
    control.onChange = [this] (int index) { attachment.setValueAsCompleteGesture (static_cast<float> (index)); };
    attachment.sendInitialUpdate();
}

//==============================================================================
BrushSlider::BrushSlider (EditorState& s, Setting which)
    : state (s), setting (which)
{
    switch (setting)
    {
        case Setting::size:
            caption = "Size";
            slider.setRange (2.0, 64.0, 1.0);
            slider.setDoubleClickReturnValue (true, 16.0);
            slider.setTooltip ("Brush size");
            break;

        case Setting::softness:
            caption = "Soft";
            slider.setRange (0.0, 100.0, 1.0);
            slider.setDoubleClickReturnValue (true, 50.0);
            slider.setTooltip ("Brush softness: 0 % is a hard edge, 100 % fades out from the centre");
            break;

        case Setting::strength:
            caption = "Strength";
            slider.setRange (0.0, 100.0, 1.0);
            slider.setDoubleClickReturnValue (true, 100.0);
            slider.setTooltip ("How loud the brush paints (and how much Erase removes)");
            break;
    }

    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setPopupMenuEnabled (false);
    slider.setScrollWheelEnabled (true);
    slider.setTitle (caption);
    slider.onValueChange = [this]
    {
        const auto v = static_cast<float> (slider.getValue());

        switch (setting)
        {
            case Setting::size:     state.setBrushSize (v); break;
            case Setting::softness: state.setBrushSoftness (v / 100.0f); break;
            case Setting::strength: state.setBrushStrength (v / 100.0f); break;
        }

        repaint();
    };

    addAndMakeVisible (slider);
    updateFromState();
    state.addChangeListener (this);
}

BrushSlider::~BrushSlider()
{
    state.removeChangeListener (this);
}

int BrushSlider::getIdealWidth() const
{
    return capsWidth (caption) + 8 + trackWidth + 8 + 40;
}

juce::String BrushSlider::valueText() const
{
    const auto v = juce::roundToInt (slider.getValue());
    return setting == Setting::size ? juce::String (v) + " px" : juce::String (v) + "%";
}

void BrushSlider::paint (juce::Graphics& g)
{
    const auto captionWidth = capsWidth (caption);
    const auto enabled = isEnabled();

    drawCaps (g, caption, { 0, 0, captionWidth, getHeight() }, juce::Justification::centredLeft,
              enabled ? theme::textDim : theme::textFaint);

    g.setFont (fonts::mono (12.0f));
    g.setColour (enabled ? theme::text : theme::textFaint);
    g.drawText (valueText(), slider.getRight() + 8, 0, getWidth() - slider.getRight() - 8, getHeight(),
                juce::Justification::centredLeft, false);
}

void BrushSlider::resized()
{
    slider.setBounds (capsWidth (caption) + 8, (getHeight() - 20) / 2, trackWidth, 20);
}

void BrushSlider::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateFromState();
}

void BrushSlider::updateFromState()
{
    double v = 0.0;

    switch (setting)
    {
        case Setting::size:     v = state.getBrushSize(); break;
        case Setting::softness: v = state.getBrushSoftness() * 100.0; break;
        case Setting::strength: v = state.getBrushStrength() * 100.0; break;
    }

    if (! juce::approximatelyEqual (slider.getValue(), v))
    {
        slider.setValue (v, juce::dontSendNotification);
        repaint();
    }
}

//==============================================================================
ShellButton::ShellButton (const juce::String& buttonText, Kind k)
    : juce::Button (buttonText), kind (k)
{
    setButtonText (buttonText);
}

void ShellButton::setKind (Kind newKind)
{
    if (kind != newKind)
    {
        kind = newKind;
        repaint();
    }
}

int ShellButton::getIdealWidth() const
{
    return textWidth (fonts::ui (theme::controlSize, fonts::UiWeight::medium), getButtonText()) + 28;
}

void ShellButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto enabled = isEnabled();

    auto fill = theme::raised;
    auto outline = theme::line;
    auto textColour = theme::text;

    switch (kind)
    {
        case Kind::normal:
            fill = down ? theme::raised.darker (0.25f) : highlighted ? theme::raisedHover : theme::raised;
            break;

        case Kind::primary:
            fill = down ? theme::accent.darker (0.2f) : highlighted ? theme::accent.brighter (0.12f) : theme::accent;
            outline = fill;
            textColour = juce::Colours::white;
            break;

        case Kind::danger:
            fill = down ? theme::danger.darker (0.25f) : highlighted ? theme::danger.brighter (0.1f) : theme::danger.withAlpha (0.88f);
            outline = fill;
            textColour = juce::Colours::white;
            break;
    }

    if (! enabled)
    {
        fill = theme::raised;
        outline = theme::line;
        textColour = theme::textFaint;
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, theme::controlCorner);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds, theme::controlCorner, 1.0f);

    g.setColour (textColour);
    g.setFont (fonts::ui (theme::controlSize, fonts::UiWeight::medium));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

} // namespace artfreq::ui::shell
