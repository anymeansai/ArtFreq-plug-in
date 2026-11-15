#include "ShellFooter.h"

#include "Parameters.h"

namespace artfreq::ui::shell
{
namespace
{
    // Rows (the knobs' caption, dial and value rows line up with these).
    constexpr int captionTop = 16;
    constexpr int captionHeight = 12;
    constexpr int controlTop = 40;
    constexpr int hintTop = 80;
    constexpr int hintHeight = 16;

    // Columns. The trigger column holds Transport | MIDI and the Repeat switch; the start bar
    // field sits in its hint line, so the timing row keeps its width.
    constexpr int knobGap = 4;
    constexpr int lengthWidth = 96;
    constexpr int triggerWidth = 124;
    constexpr int repeatGap = 6;
    constexpr int repeatWidth = 78;
    constexpr int tempoWidth = 108;
    constexpr int bpmWidth = 56;
    constexpr int columnGap = 16;
    constexpr int startBarWidth = 40;
    constexpr int startBarHeight = 20;
    constexpr int startBarGap = 5;
    constexpr float startBarTextSize = 12.0f;   // as the knobs' values in the same line
    constexpr int previewWidth = 56;
    constexpr int previewSize = 44;
    constexpr int previewGap = 12;

    juce::RangedAudioParameter& parameter (ArtFreqProcessor& processor, const char* id)
    {
        auto* p = processor.getParameters().getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    int choiceIndex (ArtFreqProcessor& processor, const char* id)
    {
        if (auto* value = processor.getParameters().getRawParameterValue (id))
            return juce::roundToInt (value->load());

        return 0;
    }

    juce::Font hintFont()
    {
        return fonts::ui (theme::labelSize);
    }

    void drawHint (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> column,
                   juce::Colour colour, juce::Justification justification = juce::Justification::centredLeft)
    {
        g.setFont (hintFont());
        g.setColour (colour);
        g.drawText (text, column.getX(), hintTop, column.getWidth(), hintHeight, justification, true);
    }

    constexpr const char* startBarLead = "Plays once from bar";
} // namespace

//==============================================================================
PreviewButton::PreviewButton() : juce::Button ("Preview")
{
    setClickingTogglesState (false);
    setTooltip ("Preview: play the drawing from its start, even while the DAW is stopped (once, or on a loop with Repeat)");
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
Footer::Footer (ArtFreqProcessor& p, int inspectorLeftEdge)
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

    // In the effect these three shape the drawing's sines, which only the Draw modes play.
    if (processor.isEffect())
    {
        density.setTooltip ("Density (Carve + Draw and Draw): how many lines share the height. Few = scan lines, many = solid shapes.");
        contrast.setTooltip ("Contrast (Carve + Draw and Draw): how far the quietest drawn parts sit below the loudest (dB)");
        level.setTooltip ("Level (Carve + Draw and Draw): the level of the drawing's sines. The audio passing through keeps its own.");
        preview.setTooltip ("Preview: run the drawing's pass from its start on the audio coming in, even while the DAW is "
                            "stopped (once, or on a loop with Repeat). Most DAWs keep sending audio to effects then.");
    }

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

    repeat.setComponentID ("repeat");
    repeat.setTitle ("Repeat");
    repeat.setTooltip ("Repeat: loop the drawing while the DAW plays (Transport) or while a note is held (MIDI). "
                       "Off: it plays once.");
    repeatAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (processor, params::id::repeat), repeat);
    addAndMakeVisible (repeat);

    NumberField::Options barOptions;
    barOptions.format = [] (double v) { return juce::String (juce::roundToInt (v)); };
    barOptions.dragUnitsPerPixel = 0.25;
    barOptions.snap = 1.0;
    barOptions.editOnSingleClick = true;
    barOptions.textSize = startBarTextSize;
    startBar = std::make_unique<NumberField> (parameter (processor, params::id::startBar), barOptions);
    startBar->setComponentID ("start-bar");
    startBar->setTitle ("Start bar");
    addChildComponent (*startBar);

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

bool Footer::showsStartBar() const
{
    return processor.getSinglePassStartBar() > 0;
}

juce::String Footer::getTriggerHint() const
{
    const auto repeats = choiceIndex (processor, params::id::repeat) != 0;

    if (choiceIndex (processor, params::id::trigger) == 1)
        return repeats ? "Loops while a note is held" : "Each note plays it once";

    if (! repeats)
        return juce::String (startBarLead) + " " + juce::String (processor.getSinglePassStartBar());

    const auto lengths = params::lengthNames();
    return "Repeats every " + lengths[juce::jlimit (0, lengths.size() - 1, choiceIndex (processor, params::id::length))]
         + " from bar 1";
}

juce::String Footer::tempoHint() const
{
    return choiceIndex (processor, params::id::tempoSource) == 0 ? "From the DAW" : "Typed in";
}

void Footer::refresh()
{
    // ArtFreq FX: the sines' settings are greyed in the modes that play no sines.
    if (processor.isEffect())
    {
        const auto sines = playsSines (processor.getEffectMode());

        for (auto* knob : { &density, &contrast, &level })
            if (knob->isEnabled() != sines)
                knob->setEnabled (sines);
    }

    const auto previewing = processor.isPreviewing();

    if (preview.getToggleState() != previewing)
        preview.setToggleState (previewing, juce::dontSendNotification);

    const auto followHost = choiceIndex (processor, params::id::tempoSource) == 0;
    bpm->setFollowing (followHost, followHost ? formatNumber (processor.getEffectiveTempo(), 2) : juce::String());

    // The start bar field is the bar number in "Plays once from bar [N]".
    const auto withStartBar = showsStartBar();

    if (startBar->isVisible() != withStartBar)
        startBar->setVisible (withStartBar);

    const auto hints = lengthHint() + "|" + getTriggerHint() + "|" + tempoHint() + "|" + (previewing ? "1" : "0");

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
    drawHint (g, showsStartBar() ? juce::String (startBarLead) : getTriggerHint(), triggerColumn.withRight (repeat.getRight()),
              theme::textFaint);
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
    x -= columnGap - repeatGap;   // Repeat belongs to Trigger: closer
    const auto repeatColumn = column (repeatWidth);
    tempoColumn = column (tempoWidth);
    x -= columnGap - 8;   // the BPM field belongs to Tempo: closer
    bpmColumn = column (bpmWidth);

    length.setBounds (lengthColumn);
    trigger.setBounds (triggerColumn);
    repeat.setBounds (repeatColumn);
    tempoSource.setBounds (tempoColumn);
    bpm->setBounds (bpmColumn);

    // Right after "Plays once from bar", centred on the hint line.
    startBar->setBounds (triggerColumn.getX() + textWidth (hintFont(), startBarLead) + startBarGap,
                         hintTop + (hintHeight - startBarHeight) / 2, startBarWidth, startBarHeight);

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

} // namespace artfreq::ui::shell
