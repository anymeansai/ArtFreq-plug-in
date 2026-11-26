#pragma once

// Small controls shared across the editor.

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace artfreq::ui
{

/** A row of mutually exclusive options (e.g. Low | Mid | High | Full). Nothing is selected if
    the selected index is -1. */
class SegmentedControl : public juce::Component
{
public:
    explicit SegmentedControl (juce::StringArray options = {});

    void setOptions (const juce::StringArray& options);
    const juce::StringArray& getOptions() const noexcept { return options; }

    /** Changes the label of one option without changing the selection (e.g. "Auto (Negative)"). */
    void setOptionLabel (int index, const juce::String& label);

    /** Shorter labels, one per option, drawn instead when the options would touch each other
        even at the smallest type size (a narrow panel). An empty array: always the options. */
    void setShortLabels (const juce::StringArray& labels);

    /** The labels as drawn at this width: the options, or the short labels. */
    const juce::StringArray& getShownLabels() const;

    /** True if every label drawn has its padding at the type size used: none is squeezed. */
    bool labelsFit() const;

    void setSelectedIndex (int index, juce::NotificationType notification = juce::sendNotificationSync);
    int getSelectedIndex() const noexcept { return selected; }

    /** Called when the user picks an option. */
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int indexAt (juce::Point<float> position) const;

    juce::StringArray options, shortLabels;
    int selected = -1;
    int hovered = -1;
};

/** The icons drawn with paths (no image assets). Each is designed in a 0..24 box. */
namespace icons
{
    juce::Path select();
    juce::Path brush();
    juce::Path eraser();
    juce::Path text();
    juce::Path image();
    juce::Path undo();
    juce::Path redo();
    juce::Path help();
    juce::Path gear();
    juce::Path trash();
    juce::Path play();
    juce::Path stop();
    juce::Path close();
    juce::Path download();
    juce::Path clear();
    juce::Path eye();
    juce::Path eyeOff();
    juce::Path chevronLeft();
    juce::Path chevronRight();
    juce::Path layers();
} // namespace icons

/** A button showing a path icon (optionally with a text label after it), with hover, pressed,
    toggled (accent) and disabled states. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, juce::Path icon, const juce::String& label = {});

    void setIcon (juce::Path newIcon);
    void setLabel (const juce::String& newLabel);

    /** Colour of the icon and outline when toggled on (default theme::accent). */
    void setOnColour (juce::Colour colour);

    /** Draw without the raised background when not hovered/toggled (header-style). */
    void setFlat (bool shouldBeFlat);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Path icon;
    juce::String label;
    juce::Colour onColour = theme::accent;
    bool flat = false;
};

} // namespace artfreq::ui
