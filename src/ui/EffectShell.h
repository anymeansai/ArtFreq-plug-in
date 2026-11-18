#pragma once

// ArtFreq FX's window: a small plug-in of its own. One top bar (the emblem and name, Mode, Depth,
// undo / redo, Live, help, settings), the canvas, and one slim bottom bar (the tools, + Text,
// + Image, Layers; then what will happen in plain words, Length and Repeat). Two strips appear
// only while they have something to act on: the sines' settings under the top bar in the modes
// that play them, the brush's settings over the bottom bar while the brush or the eraser is the
// tool. Nothing is greyed: what does not apply is not shown. See docs/DESIGN.md, "ArtFreq FX".

#include "EditorState.h"
#include "EffectControls.h"
#include "PluginProcessor.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace artfreq::ui::shell
{

namespace effectMetrics
{
    constexpr int topBarHeight = 44;
    constexpr int bottomBarHeight = 40;
    constexpr int stripHeight = 32;
    constexpr int pad = 12;             // outer margin of the bars and strips
    constexpr int stripControlHeight = 24;
} // namespace effectMetrics

/** What ArtFreq FX will do, as the line in the bottom bar says it: "Carves once when you press
    play", "Draws once from bar 9", "Repeats every 4 bars", "Each note stencils it once", "Loops
    while a note is held". The verb is the mode's (Carve + Draw draws, in a hole it carves). While
    the DAW plays and a pass on play waits for the track's audio: "Waiting for audio to carve".
    Under a DAW loop shorter than the drawing (on play or with Repeat), what each round shows:
    "Your loop shows 2 of 4 bars". With `shorter`, the same in fewer words, for a bar that has no
    room for the sentence. */
juce::String effectHint (ArtFreqProcessor& processor, bool shorter = false);

/** A segmented control that also explains itself in a tooltip. */
class TippedSegments final : public SegmentedControl,
                             public juce::SettableTooltipClient
{
public:
    using SegmentedControl::SegmentedControl;
};

/** "Live": the canvas's other face, the spectrogram of what comes out. A chip with a dot, lit in
    the playing colour while the live view is shown. */
class LiveToggle final : public juce::Button
{
public:
    LiveToggle();

    static int getIdealWidth();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
class EffectTopBar final : public juce::Component
{
public:
    explicit EffectTopBar (ArtFreqProcessor& processor);

    /** Follows the mode (Depth) and what can be undone: undo and redo are shown only while
        they have something to do. */
    void refresh();
    void setUndoState (bool canUndo, bool canRedo);

    /** Locked (not activated on this computer): only the name, help and settings are shown; the
        activation screen covers the rest of the window. */
    void setLocked (bool shouldBeLocked);

    void paint (juce::Graphics&) override;
    void resized() override;

    EffectControls controls;   // Mode and Depth
    IconButton undoButton;
    IconButton redoButton;
    LiveToggle liveButton;
    IconButton helpButton;
    IconButton settingsButton;

private:
    juce::Rectangle<int> logoArea;
    int dividerX = 0;
    bool locked = false;
    bool undoWanted = false, redoWanted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectTopBar)
};

//==============================================================================
class EffectBottomBar final : public juce::Component,
                              private juce::ChangeListener
{
public:
    EffectBottomBar (ArtFreqProcessor& processor, EditorState& state);
    ~EffectBottomBar() override;

    /** The tools have nothing to act on in the live view: they are not shown there. */
    void setToolsShown (bool shouldShow);

    /** Follows what the processor does on its own (the mode, automation). The editor calls
        this from its timer. */
    void refresh();

    /** The line as it is drawn (the sentence, or its shorter form where the bar is too narrow). */
    juce::String getShownHint() const;
    juce::Rectangle<int> getHintBounds() const noexcept { return hintArea; }

    void paint (juce::Graphics&) override;
    void resized() override;

    IconButton selectTool;
    IconButton brushTool;
    IconButton eraseTool;
    IconButton addTextButton;
    IconButton addImageButton;
    IconButton layersButton;   // lit while the layer list is open
    juce::ComboBox length;
    ToggleChip repeat { "Repeat", shellIcons::repeat() };

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateFromState();

    ArtFreqProcessor& processor;
    EditorState& state;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lengthAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> repeatAttachment;

    juce::Rectangle<int> toolGroup, hintArea;
    bool toolsShown = true;
    juce::String shownHint;
    bool shownSounding = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectBottomBar)
};

//==============================================================================
/** The sines' settings (Level, Density, Contrast, Lows), under the top bar in Carve + Draw and
    Draw: the modes that play the drawing. The editor shows it only there. */
class SoundStrip final : public juce::Component
{
public:
    explicit SoundStrip (ArtFreqProcessor& processor);
    ~SoundStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::unique_ptr<NumberField> level, density, contrast;
    TippedSegments lows;

private:
    std::unique_ptr<ChoiceSegments> lowsAttachment;
    juce::Rectangle<int> titleArea, levelCaption, densityCaption, contrastCaption, lowsCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundStrip)
};

//==============================================================================
/** The brush's settings (Size, Soft, Strength), over the bottom bar while the brush or the
    eraser is the tool. */
class BrushStrip final : public juce::Component,
                         private juce::ChangeListener
{
public:
    explicit BrushStrip (EditorState& state);
    ~BrushStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    BrushSlider brushSize;
    BrushSlider brushSoftness;
    BrushSlider brushStrength;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    EditorState& state;
    Tool shownTool = Tool::brush;
    juce::Rectangle<int> titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrushStrip)
};

} // namespace artfreq::ui::shell
