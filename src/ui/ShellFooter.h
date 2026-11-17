#pragma once

// The instrument's footer: the sound (Density, Contrast, Level), the timing (Length, Trigger with
// Repeat and where a single pass starts, Tempo, BPM), Preview and Export. See docs/DESIGN.md,
// "Footer". (ArtFreq FX has a bottom bar of its own: EffectShell.h.)

#include "PluginProcessor.h"
#include "ShellControls.h"
#include "ShellExport.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace artfreq::ui::shell
{

/** The round play / stop button (accent2 while previewing). */
class PreviewButton final : public juce::Button
{
public:
    PreviewButton();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** The words in the Trigger hint that say where a single pass starts ("when you press play" or
    "from bar"), as a quiet button with a small chevron: a click offers the two. */
class StartChoice final : public juce::Button
{
public:
    StartChoice();

    void setWords (const juce::String& words);
    const juce::String& getWords() const noexcept { return words; }

    /** The width its words and chevron need. */
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String words;
};

class Footer final : public juce::Component
{
public:
    /** `inspectorLeft` is where the inspector starts, in the footer's coordinates: the Preview
        and Export columns line up with it. */
    Footer (ArtFreqProcessor& processor, int inspectorLeft);
    ~Footer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Follows what the processor does on its own (host tempo, preview, bounces). The editor
        calls this from its timer. */
    void refresh();

    Knob density { "Density" };
    Knob contrast { "Contrast" };
    Knob level { "Level" };
    juce::ComboBox length;
    SegmentedControl trigger { { "Transport", "MIDI" } };
    ToggleChip repeat { "Repeat", shellIcons::repeat() };
    StartChoice startChoice;                 // in the hint line: "Plays once [when you press play v]"
    std::unique_ptr<NumberField> startBar;   // ... or "Plays once [from bar v] [N]"
    SegmentedControl tempoSource { { "Host", "Manual" } };
    std::unique_ptr<NumberField> bpm;
    PreviewButton preview;
    ExportPanel exporter;

    /** What the trigger will do, in words, as the line under it says: "Plays once when you press
        play", "Plays once from bar 9", "Repeats every 4 bars", "Each note plays it once", "Loops
        while a note is held". With Transport and no Repeat the words after "Plays once" are the
        start choice, and the bar number is the start bar field. */
    juce::String getTriggerHint() const;

    /** Where a single Transport pass starts: when the DAW starts playing, or at the start bar.
        What the start choice's menu does. */
    void setStartAt (StartAt startAt);

    /** The start choice's menu ("When you press play", "From a bar of the song", the current one
        ticked): its item ids are the StartAt values plus one. */
    juce::PopupMenu startMenu() const;

private:
    juce::String lengthHint() const;
    juce::String tempoHint() const;
    bool showsStartChoice() const;
    bool showsStartBar() const;
    bool startsOnPlay() const;
    void layoutStart();
    void showStartMenu();

    ArtFreqProcessor& processor;
    const int inspectorLeft;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttachment> densityAttachment, contrastAttachment, levelAttachment;
    std::unique_ptr<ComboBoxAttachment> lengthAttachment;
    std::unique_ptr<ChoiceSegments> triggerAttachment, tempoAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> repeatAttachment;

    // Layout, in the footer's coordinates.
    int soundDividerX = 0, outputDividerX = 0;
    juce::Rectangle<int> lengthColumn, triggerColumn, tempoColumn, bpmColumn, previewColumn;
    juce::String shownHints;   // what the hint row last showed, to repaint only on change

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footer)
};

} // namespace artfreq::ui::shell
