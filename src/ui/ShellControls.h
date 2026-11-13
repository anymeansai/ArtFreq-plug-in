#pragma once

// Controls and drawing helpers used by the editor shell (header, toolbar, footer, panels).
// Everything here is laid out in logical pixels at 100 %; the editor scales the whole interface.

#include "EditorState.h"
#include "Theme.h"
#include "Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace freqart::ui::shell
{

namespace metrics
{
    constexpr int headerHeight = 52;
    constexpr int toolbarHeight = 44;
    constexpr int footerHeight = 112;
    constexpr int pad = 16;             // outer margin of the header and toolbar
    constexpr int footerPad = 24;       // outer margin of the footer
    constexpr int controlHeight = 28;   // buttons, fields, combo boxes, segmented controls
    constexpr int headerButton = 32;    // the header's icon buttons
} // namespace metrics

//==============================================================================
// Drawing helpers

/** An 11 px uppercase label with the design's tracking. */
void drawCaps (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
               juce::Justification justification, juce::Colour colour);

/** Width of a caps label as drawCaps draws it. */
int capsWidth (const juce::String& text);

/** Width of text in a font, rounded up. */
int textWidth (const juce::Font& font, const juce::String& text);

/** A 1 px divider line: vertical (x, from top to bottom) or horizontal. */
void drawVerticalDivider (juce::Graphics&, int x, int top, int bottom, juce::Colour colour = theme::line);
void drawHorizontalDivider (juce::Graphics&, int y, int left, int right, juce::Colour colour = theme::line);

/** A width that fits an IconButton showing `label` (or only its icon) at the given height. */
int iconButtonWidth (const juce::String& label, int height);

/** Draws a 24-box icon path fitted to `box` (snapped to whole pixels). */
void drawIcon (juce::Graphics&, const juce::Path& icon24, juce::Rectangle<float> box, juce::Colour colour);

/** Icons only the shell uses (the shared ones are in Widgets.h). */
namespace shellIcons
{
    juce::Path chevronDown();
    juce::Path check();
    juce::Path waveFile();
    juce::Path dragHandle();
    juce::Path plus();
    juce::Path repeat();
} // namespace shellIcons

/** Number formatting shared by read-outs and fields. */
juce::String formatNumber (double value, int maxDecimals);  // "120", "93.75", "4.5"
double parseNumber (const juce::String& text, double fallback);

//==============================================================================
/** The value under a knob: double-click to type a value (a single click does nothing). */
class ValueLabel final : public juce::Label
{
public:
    ValueLabel();

    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

protected:
    juce::TextEditor* createEditorComponent() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueLabel)
};

//==============================================================================
/** A rotary knob with its caption above and its value below, as a single juce::Slider (so it
    works with SliderAttachment, automation gestures and screen readers).

      - drag up/down (or left/right); Shift = fine, with no jump when Shift goes down or up;
      - double-click the knob = default value (set with setDoubleClickReturnValue);
      - double-click the value = type a value. */
class Knob final : public juce::Slider
{
public:
    static constexpr int width = 76;
    static constexpr int height = 80;
    static constexpr int dialSize = 44;

    explicit Knob (const juce::String& caption);

    const juce::String& getCaption() const noexcept { return caption; }

    juce::Rectangle<int> getDialBounds() const noexcept;
    juce::Rectangle<int> getValueBounds() const noexcept;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool hitTest (int x, int y) override;

private:
    juce::String caption;
    double dragProportion = 0.0;
    juce::Point<float> lastDragPosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
/** A number field bound to a parameter: drag up/down to change it (Shift = fine), double-click
    (or click, see Options) to type. Shows the parameter's value, or `overrideText` greyed when
    the field is "following" something else (e.g. the host tempo). */
class NumberField final : public juce::Component,
                          private juce::TextEditor::Listener
{
public:
    struct Options
    {
        std::function<juce::String (double)> format;      // value -> text
        double dragUnitsPerPixel = 1.0;                    // linear drag (ignored if logDrag)
        bool logDrag = false;                              // drag multiplies: 100 px = one octave
        double fineFactor = 0.1;                           // with Shift held
        double snap = 0.0;                                 // round dragged values to this step (0 = no)
        bool editOnSingleClick = false;
        juce::String unit;                                 // drawn faint after the number, e.g. "Hz"
        float textSize = theme::controlSize;               // smaller for a field in a line of text
    };

    NumberField (juce::RangedAudioParameter& parameter, Options options);
    ~NumberField() override;

    /** While set, the field shows this text greyed and cannot be dragged or typed into. */
    void setFollowing (bool isFollowing, const juce::String& text = {});
    bool isFollowing() const noexcept { return following; }

    void showEditor();
    bool isEditing() const noexcept { return editor != nullptr; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void enablementChanged() override;

private:
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void closeEditor (bool commit);
    bool canEdit() const noexcept;

    juce::RangedAudioParameter& parameter;
    Options options;
    juce::ParameterAttachment attachment;
    double value = 0.0;

    bool following = false;
    juce::String followingText;
    bool hovered = false;
    bool dragging = false;
    bool dragMoved = false;
    double dragValue = 0.0;
    juce::Point<float> lastDragPosition;
    std::unique_ptr<juce::TextEditor> editor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NumberField)
};

//==============================================================================
/** An on / off switch with an icon and a label, drawn as a one-segment SegmentedControl (lit
    like a chosen segment when on), so it sits with the segmented controls beside it. Bind it to
    a bool parameter with juce::ButtonParameterAttachment. */
class ToggleChip final : public juce::Button
{
public:
    ToggleChip (const juce::String& label, juce::Path icon24);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Path icon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleChip)
};

//==============================================================================
/** A SegmentedControl bound to a choice (or bool) parameter. */
class ChoiceSegments final
{
public:
    ChoiceSegments (SegmentedControl& control, juce::RangedAudioParameter& parameter);

private:
    SegmentedControl& control;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceSegments)
};

//==============================================================================
/** One of the toolbar's brush settings: "SIZE ----o---- 16 px", bound to EditorState. */
class BrushSlider final : public juce::Component,
                          private juce::ChangeListener
{
public:
    enum class Setting
    {
        size,
        softness,
        strength
    };

    static constexpr int trackWidth = 64;

    BrushSlider (EditorState& state, Setting setting);
    ~BrushSlider() override;

    /** The width this needs: caption, track and value. */
    int getIdealWidth() const;

    juce::Slider& getSlider() noexcept { return slider; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateFromState();
    juce::String valueText() const;

    EditorState& state;
    Setting setting;
    juce::String caption;
    juce::Slider slider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrushSlider)
};

//==============================================================================
/** A text button in the shell's style (raised, 1 px outline, optional accent or danger). */
class ShellButton final : public juce::Button
{
public:
    enum class Kind
    {
        normal,
        primary,   // accent fill: the one action that matters here
        danger     // danger colour: destructive confirmation
    };

    ShellButton (const juce::String& buttonText, Kind kind = Kind::normal);

    void setKind (Kind newKind);
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Kind kind;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellButton)
};

} // namespace freqart::ui::shell
