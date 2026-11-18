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

namespace artfreq::ui::shell
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
    static constexpr int effectWidth = 672;   // ArtFreq FX: two columns

    /** The window sizes offered (fractions of the window at 100 %). */
    static constexpr float windowScales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };

    /** In ArtFreq FX the panel also holds what its small window has no bar for: the trigger,
        where a single pass starts, and the tempo. */
    explicit SettingsPanel (ArtFreqProcessor& processor);
    ~SettingsPanel() override;

    /** The size the content needs. */
    int getPanelWidth() const noexcept { return effect ? effectWidth : width; }
    int getIdealHeight() const;

    /** ArtFreq FX: follows Trigger, Repeat and the tempo (what the Start and Tempo rows show).
        The editor calls this from its timer while the panel is open. */
    void refresh();

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
    void layoutEffect();
    static juce::AttributedString noteText();
    static juce::AttributedString midiNoteText();

    ArtFreqProcessor& processor;
    const bool effect;
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

    // ArtFreq FX only: the timing its window has no bar for.
    std::unique_ptr<SegmentedControl> trigger, startAt, tempoSource;
    std::unique_ptr<NumberField> startBar, bpm;
    std::unique_ptr<ChoiceSegments> triggerAttachment, startAtAttachment, tempoAttachment;
    juce::Rectangle<int> midiNoteArea, startTextArea, tempoTextArea;
    juce::String startText, tempoText;
    int columnDividerX = 0;

    // Layout, filled by resized() and used by paint().
    struct Row
    {
        juce::String label;
        int y = 0;
        int x = 0;
    };
    std::vector<Row> rows;
    std::vector<Row> sectionCaptions;
    std::vector<int> dividers;
    juce::Rectangle<int> noteArea, slopeUnitArea, rangeDashArea, confirmTextArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

//==============================================================================
class HelpPanel final : public juce::Component
{
public:
    static constexpr int width = 920;
    static constexpr int effectWidth = 688;   // ArtFreq FX's window is smaller

    /** forEffect: ArtFreq FX's help, short and its own (where it goes, drawing, the modes,
        playing, bouncing in the DAW). */
    explicit HelpPanel (bool forEffect = false);
    ~HelpPanel() override;

    /** The size the content needs. */
    int getPanelWidth() const noexcept { return effect ? effectWidth : width; }
    int getIdealHeight() const;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Block;
    std::vector<Block> layoutColumn (int column, int width) const;
    int getColumnWidth() const;

    const bool effect;
    IconButton closeButton;
    std::vector<Block> blocks;   // both columns, laid out by resized()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpPanel)
};

} // namespace artfreq::ui::shell
