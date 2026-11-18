#include "EffectControls.h"

#include "Parameters.h"

namespace artfreq::ui::shell
{
namespace
{
    constexpr int captionGap = 6;   // between Depth's caption and its field
    constexpr int groupGap = 10;    // between Mode and Depth

    constexpr const char* depthCaptionText = "Depth";

    juce::RangedAudioParameter& parameter (ArtFreqProcessor& processor, const char* id)
    {
        auto* p = processor.getParameters().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }
} // namespace

EffectControls::EffectControls (ArtFreqProcessor& p)
    : processor (p)
{
    setComponentID ("effect-controls");

    // Mode names itself: its four choices are its label.
    mode.setComponentID ("mode");
    mode.setTitle ("Mode");
    mode.setTooltip ("Carve mutes the drawing's frequencies in the audio. Carve + Draw plays the drawing in a hole "
                     "carved a little wider. Stencil lets only the drawing through. Draw plays it on top of the audio.");
    modeAttachment = std::make_unique<ChoiceSegments> (mode, parameter (processor, params::id::mode));
    addAndMakeVisible (mode);

    NumberField::Options depthOptions;
    depthOptions.format = [] (double v) { return formatNumber (std::round (v), 0); };
    depthOptions.dragUnitsPerPixel = 0.5;
    depthOptions.snap = 1.0;
    depthOptions.editOnSingleClick = true;
    depthOptions.unit = "dB";
    depth = std::make_unique<NumberField> (parameter (processor, params::id::depth), depthOptions);
    depth->setComponentID ("depth");
    depth->setTitle ("Depth");
    depth->setTooltip ("Depth: how far carved frequencies drop. -80 dB looks black in MiniMeters, even over a "
                       "master's bass. Drag, or click to type.");
    addAndMakeVisible (*depth);

    refresh();
}

EffectControls::~EffectControls() = default;

int EffectControls::getIdealWidth()
{
    return modeWidth + groupGap + capsWidth (depthCaptionText) + captionGap + depthWidth;
}

void EffectControls::refresh()
{
    // Nothing is greyed in ArtFreq FX: what a mode has no use for is not there.
    const auto carves = carvesAudio (processor.getEffectMode());

    if (depth->isVisible() != carves)
    {
        depth->setVisible (carves);
        repaint();
    }
}

void EffectControls::paint (juce::Graphics& g)
{
    if (depth->isVisible())
        drawCaps (g, depthCaptionText, depthCaption, juce::Justification::centredLeft, theme::textDim);
}

void EffectControls::resized()
{
    const auto height = metrics::controlHeight;
    const auto y = (getHeight() - height) / 2;

    mode.setBounds (0, y, modeWidth, height);

    auto x = mode.getRight() + groupGap;
    depthCaption = { x, 0, capsWidth (depthCaptionText), getHeight() };
    x = depthCaption.getRight() + captionGap;
    depth->setBounds (x, y, depthWidth, height);
}

} // namespace artfreq::ui::shell
