#pragma once

// The overlays: Settings (a card under the gear) and Help (a card over the window). Both close
// with Esc, their close button or a click outside them. See docs/DESIGN.md, "Settings panel" and
// "Help panel".

#include "PluginProcessor.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace freqart::ui::shell
{

/** Covers the window under an open panel: dims it (optionally) and closes the panel when clicked. */
class Scrim final : public juce::Component
{
public:
    Scrim();

    void setDim (float alpha);
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    float dim = 0.0f;
};

//==============================================================================
class SettingsPanel final : public juce::Component
{
public:
    static constexpr int width = 376;

    /** The window sizes offered (fractions of 1120 x 720). */
    static constexpr float windowScales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };

    explicit SettingsPanel (FreqArtProcessor& processor);
    ~SettingsPanel() override;

    /** The height the content needs. */
    int getIdealHeight() const;

    /** Shows which window size is in use (none if it is not one of the offered ones). */
    void setCurrentWindowScale (float scale);

    /** Back to the "Reset drawing" button (when the panel is opened again). */
    void cancelReset();

    std::function<void()> onClose;
    std::function<void (float)> onWindowScaleChosen;
    std::function<void()> onResetDrawing;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showResetConfirmation (bool shouldShow);
    static juce::AttributedString noteText();

    IconButton closeButton;
    SegmentedControl scale { { "Log", "Linear", "Mel" } };
    std::unique_ptr<NumberField> lowHz, highHz;
    SegmentedControl slope { { "0", "3", "4.5", "6" } };
    SegmentedControl windowSize { { "75%", "100%", "125%", "150%", "200%" } };
    ShellButton resetButton { "Reset drawing" };
    ShellButton cancelButton { "Cancel" };
    ShellButton confirmButton { "Reset", ShellButton::Kind::danger };
    std::unique_ptr<ChoiceSegments> scaleAttachment, slopeAttachment;
    bool confirmingReset = false;
    int idealHeight = 456;

    // Layout, filled by resized() and used by paint().
    struct Row
    {
        juce::String label;
        int y = 0;
    };
    std::vector<Row> rows;
    std::vector<std::pair<juce::String, int>> sectionCaptions;
    std::vector<int> dividers;
    juce::Rectangle<int> noteArea, slopeUnitArea, rangeDashArea, confirmTextArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

//==============================================================================
class HelpPanel final : public juce::Component
{
public:
    static constexpr int width = 920;

    /** forEffect: FreqArt FX's help (where it goes, its modes, the latency, MIDI in effects). */
    explicit HelpPanel (bool forEffect = false);
    ~HelpPanel() override;

    /** The height the content needs at the panel's width. */
    int getIdealHeight() const;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Block;
    std::vector<Block> layoutColumn (int column, int width) const;

    const bool effect;
    IconButton closeButton;
    std::vector<Block> blocks;   // both columns, laid out by resized()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpPanel)
};

} // namespace freqart::ui::shell
