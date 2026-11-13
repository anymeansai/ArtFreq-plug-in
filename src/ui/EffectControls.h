#pragma once

// FreqArt FX's own controls, in the header: Mode (Carve | Carve + Draw | Stencil | Draw) and
// Depth. See docs/DESIGN.md, "FreqArt FX".

#include "PluginProcessor.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace freqart::ui::shell
{

class EffectControls final : public juce::Component
{
public:
    static constexpr int modeWidth = 284;
    static constexpr int depthWidth = 72;

    explicit EffectControls (FreqArtProcessor& processor);
    ~EffectControls() override;

    /** The width the controls and their captions take. */
    static int getIdealWidth();

    /** Follows the mode: Depth is greyed in Draw, which carves nothing. The editor calls this
        from its timer. */
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
    FreqArtProcessor& processor;
    std::unique_ptr<ChoiceSegments> modeAttachment;
    juce::Rectangle<int> modeCaption, depthCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectControls)
};

} // namespace freqart::ui::shell
