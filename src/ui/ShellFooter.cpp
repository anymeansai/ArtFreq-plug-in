#include "ShellFooter.h"

#include "Parameters.h"

namespace freqart::ui::shell
{
namespace
{
    // Rows (the knobs' caption, dial and value rows line up with these).
    constexpr int captionTop = 16;
    constexpr int captionHeight = 12;
    constexpr int controlTop = 40;
    constexpr int hintTop = 80;
    constexpr int hintHeight = 16;

    // Columns
    constexpr int knobGap = 4;
    constexpr int lengthWidth = 104;
    constexpr int triggerWidth = 184;
    constexpr int tempoWidth = 112;
    constexpr int bpmWidth = 68;
    constexpr int columnGap = 16;
    constexpr int previewWidth = 56;
    constexpr int previewSize = 44;
    constexpr int previewGap = 12;

    juce::RangedAudioParameter& parameter (FreqArtProcessor& processor, const char* id)
    {
        auto* p = processor.getParameters().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    int choiceIndex (FreqArtProcessor& processor, const char* id)
    {
        if (auto* value = processor.getParameters().getRawParameterValue (id))
            return juce::roundToInt (value->load());

        return 0;
    }

    void drawHint (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> column,
                   juce::Colour colour, juce::Justification justification = juce::Justification::centredLeft)
    {
        g.setFont (fonts::ui (theme::labelSize));
        g.setColour (colour);
        g.drawText (text, column.getX(), hintTop, column.getWidth(), hintHeight, justification, true);
    }
} // namespace

//==============================================================================
PreviewButton::PreviewButton() : juce::Button ("Preview")
{
    setClickingTogglesState (false);
    setTooltip ("Preview: play the drawing on a loop, even while the DAW is stopped");
    setComponentID ("preview");
}

void PreviewButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto enabled = isEnabled();
    const auto on = getToggleState() && enabled;
    const auto circle = getLocalBounds().toFloat().reduced (0.5f);

    highlighted = highlighted && enabled;
    down = down && enabled;

    if (on)
    {
        g.setColour (down ? theme::accent2.darker (0.25f) : highlighted ? theme::accent2.brighter (0.15f) : theme::accent2);
        g.fillEllipse (circle);
    }
    else
    {
        g.setColour (down ? theme::raised.darker (0.3f) : highlighted ? theme::raisedHover : theme::raised);
        g.fillEllipse (circle);
        g.setColour (highlighted ? theme::textFaint : theme::line);
        g.drawEllipse (circle, 1.0f);
    }

    // The triangle's visual centre is left of its box centre: nudge it right.
    const auto iconSize = 20.0f;
    auto box = juce::Rectangle<float> (iconSize, iconSize).withCentre (circle.getCentre());

    if (! on)
        box.translate (1.0f, 0.0f);

    drawIcon (g, on ? icons::stop() : icons::play(), box,
              ! enabled ? theme::textFaint : on ? theme::bg : theme::text);
}

//==============================================================================
Footer::Footer (FreqArtProcessor& p, int inspectorLeftEdge)
    : exporter (p),
      processor (p),
      inspectorLeft (inspectorLeftEdge)
{
    setOpaque (true);

    auto& state = processor.getParameters();

    // Knobs: the attachments set a value <-> text conversion from the parameter, replaced here by
    // the read-outs the design asks for.
    density.setComponentID ("density");
    contrast.setComponentID ("contrast");
    level.setComponentID ("level");

    densityAttachment = std::make_unique<SliderAttachment> (state, params::id::density, density);
    contrastAttachment = std::make_unique<SliderAttachment> (state, params::id::contrast, contrast);
    levelAttachment = std::make_unique<SliderAttachment> (state, params::id::level, level);

    density.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " lines"; };
    contrast.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " dB"; };
    level.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };

    for (auto* knob : { &density, &contrast, &level })
    {
        knob->valueFromTextFunction = [knob] (const juce::String& text) { return parseNumber (text, knob->getValue()); };
        knob->updateText();
        addAndMakeVisible (*knob);
    }

    density.setTooltip ("Density: how many lines share the height. Few = scan lines, many = solid shapes.");
    contrast.setTooltip ("Contrast: how far the quietest drawn parts sit below the loudest (dB)");
    level.setTooltip ("Level: the output level");

    // Length
    length.addItemList (params::lengthNames(), 1);
    length.setComponentID ("length");
    length.setTitle ("Length");
    length.setTooltip ("How long one pass of the drawing lasts, in beats or bars");
    lengthAttachment = std::make_unique<ComboBoxAttachment> (state, params::id::length, length);
    addAndMakeVisible (length);

    // Trigger and tempo
    trigger.setComponentID ("trigger");
    trigger.setTitle ("Trigger");
    triggerAttachment = std::make_unique<ChoiceSegments> (trigger, parameter (processor, params::id::trigger));
    addAndMakeVisible (trigger);

    tempoSource.setComponentID ("tempo-source");
    tempoSource.setTitle ("Tempo");
    tempoAttachment = std::make_unique<ChoiceSegments> (tempoSource, parameter (processor, params::id::tempoSource));
    addAndMakeVisible (tempoSource);

    NumberField::Options bpmOptions;
    bpmOptions.format = [] (double v) { return formatNumber (v, 2); };
    bpmOptions.dragUnitsPerPixel = 0.5;
    bpmOptions.snap = 1.0;
    bpmOptions.editOnSingleClick = true;
    bpm = std::make_unique<NumberField> (parameter (processor, params::id::bpm), bpmOptions);
    bpm->setComponentID ("bpm");
    bpm->setTitle ("BPM");
    addAndMakeVisible (*bpm);

    // Preview
    preview.onClick = [this]
    {
        processor.setPreview (! processor.isPreviewing());
        refresh();
    };
    addAndMakeVisible (preview);

    addAndMakeVisible (exporter);
    refresh();
}

Footer::~Footer() = default;

juce::String Footer::lengthHint() const
{
    const auto seconds = processor.getLengthSeconds();
    return formatNumber (seconds, seconds < 10.0 ? 2 : 1) + " s per pass";
}

juce::String Footer::triggerHint() const
{
    switch (choiceIndex (processor, params::id::trigger))
    {
        case 1:  return "Each note plays it once";
        case 2:  return "Loops on its own";
        default: return "Plays with the DAW from bar 1";
    }
}

juce::String Footer::tempoHint() const
{
    return choiceIndex (processor, params::id::tempoSource) == 0 ? "From the DAW" : "Typed in";
}

void Footer::refresh()
{
    const auto previewing = processor.isPreviewing();

    if (preview.getToggleState() != previewing)
        preview.setToggleState (previewing, juce::dontSendNotification);

    const auto followHost = choiceIndex (processor, params::id::tempoSource) == 0;
    bpm->setFollowing (followHost, followHost ? formatNumber (processor.getEffectiveTempo(), 2) : juce::String());

    const auto hints = lengthHint() + "|" + triggerHint() + "|" + tempoHint() + "|" + (previewing ? "1" : "0");

    if (hints != shownHints)
    {
        shownHints = hints;
        repaint (0, hintTop, inspectorLeft, hintHeight);
    }

    exporter.refresh();
}

void Footer::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    drawHorizontalDivider (g, 0, 0, getWidth());

    drawVerticalDivider (g, soundDividerX, captionTop, hintTop + hintHeight);
    drawVerticalDivider (g, outputDividerX, captionTop, hintTop + hintHeight);

    const auto caption = [&] (const juce::String& text, juce::Rectangle<int> column,
                              juce::Justification justification = juce::Justification::centredLeft)
    {
        drawCaps (g, text, column.withY (captionTop).withHeight (captionHeight), justification, theme::textDim);
    };

    caption ("Length", lengthColumn);
    caption ("Trigger", triggerColumn);
    caption ("Tempo", tempoColumn);
    caption ("BPM", bpmColumn);
    caption ("Preview", previewColumn, juce::Justification::centred);

    drawHint (g, lengthHint(), lengthColumn, theme::textFaint);
    drawHint (g, triggerHint(), triggerColumn, theme::textFaint);
    drawHint (g, tempoHint(), tempoColumn.getUnion (bpmColumn), theme::textFaint);

    const auto previewing = processor.isPreviewing();
    drawHint (g, previewing ? "Playing" : "Play", previewColumn, previewing ? theme::accent2 : theme::textFaint,
              juce::Justification::centred);
}

void Footer::resized()
{
    const auto pad = metrics::footerPad;
    auto x = pad;

    for (auto* knob : { &density, &contrast, &level })
    {
        knob->setBounds (x, captionTop, Knob::width, Knob::height);
        x += Knob::width + knobGap;
    }

    x += pad - knobGap;
    soundDividerX = x;
    x += pad;

    const auto column = [&] (int width)
    {
        const auto r = juce::Rectangle<int> (x, controlTop, width, metrics::controlHeight);
        x += width + columnGap;
        return r;
    };

    lengthColumn = column (lengthWidth);
    triggerColumn = column (triggerWidth);
    tempoColumn = column (tempoWidth);
    x -= columnGap - 8;   // the BPM field belongs to Tempo: closer
    bpmColumn = column (bpmWidth);

    length.setBounds (lengthColumn);
    trigger.setBounds (triggerColumn);
    tempoSource.setBounds (tempoColumn);
    bpm->setBounds (bpmColumn);

    // Preview and Export line up with the inspector above them.
    outputDividerX = inspectorLeft;
    x = inspectorLeft + pad;

    previewColumn = { x, controlTop, previewWidth, metrics::controlHeight };
    const auto dialCentreY = controlTop + metrics::controlHeight / 2;
    preview.setBounds (previewColumn.getCentreX() - previewSize / 2, dialCentreY - previewSize / 2, previewSize, previewSize);

    // The export column ends where the header and toolbar controls end, so the right-hand
    // edges line up from top to bottom.
    x = previewColumn.getRight() + previewGap;
    exporter.setBounds (x, captionTop, getWidth() - metrics::pad - x, hintTop + hintHeight - captionTop);
}

} // namespace freqart::ui::shell
