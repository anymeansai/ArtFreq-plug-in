#pragma once

// ArtFreq FX's own controls, in its top bar: Mode (Carve | Carve + Draw | Stencil | Draw) and
// Depth. See docs/DESIGN.md, "ArtFreq FX".

#include "PluginProcessor.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace artfreq::ui::shell
{

class EffectControls final : public juce::Component
{
public:
    static constexpr int modeWidth = 252;
    static constexpr int depthWidth = 62;

    explicit EffectControls (ArtFreqProcessor& processor);
    ~EffectControls() override;

    /** The width Mode and Depth (with its caption) take. */
    static int getIdealWidth();

    /** Follows the mode: Depth is shown in the modes that carve, and not in Draw, where it does
        nothing. The editor calls this from its timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Mode, with a tooltip that says what each one does. */
    class ModeControl final : public SegmentedControl,
                              public juce::SettableTooltipClient
    {
    public:
        ModeControl() : SegmentedControl (params::modeNames()) {}
    };

    ModeControl mode;
    std::unique_ptr<NumberField> depth;

private:
    ArtFreqProcessor& processor;
    std::unique_ptr<ChoiceSegments> modeAttachment;
    juce::Rectangle<int> depthCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectControls)
};

} // namespace artfreq::ui::shell
