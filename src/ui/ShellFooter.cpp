#include "ShellFooter.h"

#include "LookAndFeel.h"
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

    // Columns. The trigger column holds Transport | MIDI and the Repeat switch; where a single
    // pass starts (the start choice, and the start bar field) sits in its hint line, so the timing
    // row keeps its width.
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
    constexpr int startBarGap = 3;
    constexpr float startBarTextSize = 12.0f;   // as the knobs' values in the same line

    // The start choice: its words continue the hint's ("Plays once"), on the same baseline.
    constexpr int startChoiceGap = 0;           // after the hint's words (the pad is the word space)
    constexpr int startChoicePad = 3;           // left of its words
    constexpr int startChoiceChevron = 10;
    constexpr int startChoiceTail = 3;          // right of the chevron
    constexpr int startMenuItemHeight = 28;
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

    // The single pass's hint: these words, then the start choice's, then (At bar) the bar.
    constexpr const char* startLead = "Plays once";
    constexpr const char* onPlayWords = "when you press play";
    constexpr const char* atBarWords = "from bar";
} // namespace

//==============================================================================
StartChoice::StartChoice() : juce::Button ("Start at")
{
    setTitle ("Start at");
    setTooltip ("Where the drawing starts: when you press play in your DAW (wherever the playhead is; a DAW's "
                "export or freeze starts it where the render starts), or at a bar of the song, which an export keeps");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StartChoice::setWords (const juce::String& newWords)
{
    if (words == newWords)
        return;

    words = newWords;
    repaint();
}

int StartChoice::getIdealWidth() const
{
    return startChoicePad + textWidth (hintFont(), words) + startChoiceChevron + startChoiceTail;
}

void StartChoice::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();

    if (highlighted || down)
    {
        g.setColour (down ? theme::raised : theme::raisedHover);
        g.fillRoundedRectangle (bounds, 4.0f);
    }

    // A shade brighter than the hint's words, so it reads as the part that can be changed.
    const auto colour = highlighted || down ? theme::text : theme::textDim;
    const auto wordsWidth = textWidth (hintFont(), words);

    g.setFont (hintFont());
    g.setColour (colour);
    g.drawText (words, startChoicePad, 0, wordsWidth, getHeight(), juce::Justification::centredLeft, false);

    const auto chevron = juce::Rectangle<float> (static_cast<float> (startChoicePad + wordsWidth), 0.0f,
                                                 static_cast<float> (startChoiceChevron), bounds.getHeight());
    drawIcon (g, shellIcons::chevronDown(), chevron.withSizeKeepingCentre (10.0f, 10.0f).translated (0.0f, 0.5f),
              highlighted || down ? theme::text : theme::textFaint.brighter (0.25f));
}

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

    startChoice.setComponentID ("start-choice");
    startChoice.onClick = [this] { showStartMenu(); };
    addChildComponent (startChoice);

    NumberField::Options barOptions;
    barOptions.format = [] (double v) { return juce::String (juce::roundToInt (v)); };
    barOptions.dragUnitsPerPixel = 0.25;
    barOptions.snap = 1.0;
    barOptions.editOnSingleClick = true;
    barOptions.textSize = startBarTextSize;
    startBar = std::make_unique<NumberField> (parameter (processor, params::id::startBar), barOptions);
    startBar->setComponentID ("start-bar");
    startBar->setTitle ("Start bar");
    startBar->setTooltip ("The bar of the song the drawing starts at: drag, or click to type");
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

bool Footer::showsStartChoice() const
{
    // Transport without Repeat: the single pass, which starts on play or at a bar.
    return choiceIndex (processor, params::id::trigger) == 0 && choiceIndex (processor, params::id::repeat) == 0;
}

bool Footer::showsStartBar() const
{
    return processor.getSinglePassStartBar() > 0;
}

bool Footer::startsOnPlay() const
{
    return params::startAtForIndex (choiceIndex (processor, params::id::startAt)) == StartAt::play;
}

juce::String Footer::getTriggerHint() const
{
    const auto repeats = choiceIndex (processor, params::id::repeat) != 0;

    if (choiceIndex (processor, params::id::trigger) == 1)
        return repeats ? "Loops while a note is held" : "Each note plays it once";

    if (! repeats)
        return juce::String (startLead) + " "
             + (startsOnPlay() ? juce::String (onPlayWords)
                               : juce::String (atBarWords) + " " + juce::String (processor.getSinglePassStartBar()));

    const auto lengths = params::lengthNames();
    return "Repeats every " + lengths[juce::jlimit (0, lengths.size() - 1, choiceIndex (processor, params::id::length))];
}

void Footer::setStartAt (StartAt startAt)
{
    // Not an automation target, but the host still hears of it (the project has changed).
    auto& choice = parameter (processor, params::id::startAt);
    const auto index = startAt == StartAt::bar ? params::atBarStartIndex : params::onPlayStartIndex;

    choice.beginChangeGesture();
    choice.setValueNotifyingHost (choice.convertTo0to1 (static_cast<float> (index)));
    choice.endChangeGesture();
    refresh();
}

juce::PopupMenu Footer::startMenu() const
{
    juce::PopupMenu menu;
    menu.addItem (static_cast<int> (StartAt::play) + 1, "When you press play", true, startsOnPlay());
    menu.addItem (static_cast<int> (StartAt::bar) + 1, "From a bar of the song", true, ! startsOnPlay());
    return menu;
}

void Footer::showStartMenu()
{
    auto options = juce::PopupMenu::Options()
                       .withTargetComponent (startChoice)
                       .withStandardItemHeight (startMenuItemHeight);

    // Inside the window: scaled like the rest, never a separate desktop window.
    if (auto* parent = ArtFreqLookAndFeel::findPopupParent (*this))
        options = options.withParentComponent (parent);

    // The window's look, from the start: a menu looks its look-and-feel up before it joins its
    // parent, and would otherwise be opaque, with square corners left unpainted.
    auto menu = startMenu();
    menu.setLookAndFeel (&getLookAndFeel());

    menu.showMenuAsync (options, [safe = juce::Component::SafePointer<Footer> (this)] (int result)
    {
        if (safe != nullptr && result != 0)
            safe->setStartAt (static_cast<StartAt> (result - 1));
    });
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

    // A single Transport pass: "Plays once [when you press play v]", or "Plays once [from bar v]
    // [N]" with the start bar field as its number.
    const auto withStartChoice = showsStartChoice();
    const auto withStartBar = showsStartBar();
    const juce::String words (startsOnPlay() ? onPlayWords : atBarWords);

    if (startChoice.getWords() != words)
    {
        startChoice.setWords (words);
        layoutStart();
    }

    if (startChoice.isVisible() != withStartChoice)
        startChoice.setVisible (withStartChoice);

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
    drawHint (g, showsStartChoice() ? juce::String (startLead) : getTriggerHint(), triggerColumn.withRight (repeat.getRight()),
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

    layoutStart();

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

void Footer::layoutStart()
{
    // Right after "Plays once", centred on the hint line (so its words sit on the hint's
    // baseline); the start bar field right after it.
    const auto top = hintTop + (hintHeight - startBarHeight) / 2;

    startChoice.setBounds (triggerColumn.getX() + textWidth (hintFont(), startLead) + startChoiceGap, top,
                           startChoice.getIdealWidth(), startBarHeight);
    startBar->setBounds (startChoice.getRight() + startBarGap, top, startBarWidth, startBarHeight);
}

} // namespace artfreq::ui::shell
