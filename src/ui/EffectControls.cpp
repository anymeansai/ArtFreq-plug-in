#include "EffectControls.h"

#include "Parameters.h"

namespace freqart::ui::shell
{
namespace
{
    constexpr int captionGap = 10;   // between a caption and its control
    constexpr int groupGap = 16;     // between Mode and Depth

    constexpr const char* modeCaptionText = "Mode";
    constexpr const char* depthCaptionText = "Depth";

    juce::RangedAudioParameter& parameter (FreqArtProcessor& processor, const char* id)
    {
        auto* p = processor.getParameters().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }
} // namespace

EffectControls::EffectControls (FreqArtProcessor& p)
    : processor (p)
{
    setComponentID ("effect-controls");

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
    return capsWidth (modeCaptionText) + captionGap + modeWidth + groupGap
         + capsWidth (depthCaptionText) + captionGap + depthWidth;
}

void EffectControls::refresh()
{
    const auto carves = carvesAudio (processor.getEffectMode());

    if (depth->isEnabled() != carves)
        depth->setEnabled (carves);
}

void EffectControls::paint (juce::Graphics& g)
{
    drawCaps (g, modeCaptionText, modeCaption, juce::Justification::centredLeft, theme::textDim);
    drawCaps (g, depthCaptionText, depthCaption, juce::Justification::centredLeft,
              depth->isEnabled() ? theme::textDim : theme::textFaint);
}

void EffectControls::resized()
{
    const auto height = metrics::controlHeight;
    const auto y = (getHeight() - height) / 2;
    auto x = 0;

    modeCaption = { x, 0, capsWidth (modeCaptionText), getHeight() };
    x = modeCaption.getRight() + captionGap;
    mode.setBounds (x, y, modeWidth, height);
    x = mode.getRight() + groupGap;

    depthCaption = { x, 0, capsWidth (depthCaptionText), getHeight() };
    x = depthCaption.getRight() + captionGap;
    depth->setBounds (x, y, depthWidth, height);
}

} // namespace freqart::ui::shell
