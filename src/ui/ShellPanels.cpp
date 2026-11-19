#include "ShellPanels.h"

#include "KeyFocus.h"
#include "Licensing.h"
#include "Parameters.h"
#include "ShellHeader.h"

namespace artfreq::ui::shell
{
namespace
{
    /** A segmented control that also explains itself in a tooltip. */
    struct TippedChoice final : public SegmentedControl,
                                public juce::SettableTooltipClient
    {
        using SegmentedControl::SegmentedControl;
    };

    constexpr int cardPad = 20;
    constexpr int titleRowHeight = 56;
    constexpr int closeSize = 28;
    constexpr int rowGap = 12;
    constexpr int labelWidth = 76;

    void paintCard (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        const auto r = bounds.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, theme::panelCorner);
        g.setColour (theme::line.brighter (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), theme::panelCorner, 1.0f);
    }

    // ArtFreq FX's panels sit in a smaller window: a lower title row, tighter rows.
    constexpr int effectTitleRowHeight = 46;
    constexpr int effectColumnWidth = 300;
    constexpr int effectColumnGap = 32;
    constexpr int effectRowGap = 8;
    constexpr int effectLicenceGap = 10;   // above and below the hairline over the licence
    constexpr int effectBottomPad = 14;

    void paintTitle (juce::Graphics& g, const juce::String& title, int width, int rowHeight = titleRowHeight)
    {
        g.setColour (theme::text);
        g.setFont (fonts::ui (theme::titleSize, fonts::UiWeight::semiBold));
        g.drawText (title, cardPad, 0, width - 2 * cardPad, rowHeight, juce::Justification::centredLeft, false);
        drawHorizontalDivider (g, rowHeight - 1, 0, width);
    }

    void placeCloseButton (juce::Component& button, int width, int rowHeight = titleRowHeight)
    {
        button.setBounds (width - 14 - closeSize, (rowHeight - closeSize) / 2, closeSize, closeSize);
    }

    int choiceIndex (ArtFreqProcessor& processor, const char* id)
    {
        if (auto* value = processor.getParameters().getRawParameterValue (id))
            return juce::roundToInt (value->load());

        return 0;
    }

    juce::TextLayout wrappedText (const juce::AttributedString& text, float width)
    {
        juce::TextLayout layout;
        layout.createLayout (text, width);
        return layout;
    }

    juce::AttributedString plainText (const juce::String& text, const juce::Font& font, juce::Colour colour, float lineSpacing)
    {
        juce::AttributedString s;
        s.setLineSpacing (lineSpacing);
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (text, font, colour);
        return s;
    }

    juce::String dash()   { return juce::String::fromUTF8 ("\xe2\x80\x93"); }
} // namespace

//==============================================================================
Scrim::Scrim()
{
    setComponentID ("scrim");
}

void Scrim::setDim (float alpha)
{
    if (! juce::exactlyEqual (dim, alpha))
    {
        dim = alpha;
        setOpaque (false);
        repaint();
    }
}

void Scrim::paint (juce::Graphics& g)
{
    if (dim > 0.0f)
        g.fillAll (juce::Colours::black.withAlpha (dim));

    // Each open card (a sibling above this) gets a soft shadow.
    if (auto* parent = getParentComponent())
    {
        for (auto* sibling : parent->getChildren())
        {
            if (sibling->isVisible() && (dynamic_cast<SettingsPanel*> (sibling) != nullptr
                                         || dynamic_cast<HelpPanel*> (sibling) != nullptr))
            {
                juce::Path card;
                card.addRoundedRectangle (getLocalArea (parent, sibling->getBounds()).toFloat(), theme::panelCorner);
                juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 28, { 0, 10 }).drawForPath (g, card);
            }
        }
    }
}

void Scrim::mouseDown (const juce::MouseEvent&)
{
    if (onClick != nullptr)
        onClick();
}

//==============================================================================
SettingsPanel::SettingsPanel (ArtFreqProcessor& p)
    : processor (p),
      effect (p.isEffect()),
      closeButton ("Close settings", icons::close())
{
    setComponentID ("settings-panel");
    setWantsKeyboardFocus (true);
    setKeepsKeyFocus (*this);   // while it is open: Esc closes it, and the canvas gets no keys

    closeButton.setFlat (true);
    closeButton.setTooltip ("Close (Esc)");
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    auto& state = processor.getParameters();

    scale.setTitle ("Analyser scale");
    scale.setComponentID ("scale");
    scaleAttachment = std::make_unique<ChoiceSegments> (scale, *state.getParameter (params::id::scale));
    addAndMakeVisible (scale);

    NumberField::Options hzOptions;
    hzOptions.format = [] (double v) { return formatNumber (v, 0); };
    hzOptions.logDrag = true;
    hzOptions.snap = 1.0;
    hzOptions.editOnSingleClick = true;
    hzOptions.unit = "Hz";

    lowHz = std::make_unique<NumberField> (*state.getParameter (params::id::rangeLow), hzOptions);
    highHz = std::make_unique<NumberField> (*state.getParameter (params::id::rangeHigh), hzOptions);
    lowHz->setTitle ("Analyser low");
    highHz->setTitle ("Analyser high");
    lowHz->setComponentID ("range-low");
    highHz->setComponentID ("range-high");
    addAndMakeVisible (*lowHz);
    addAndMakeVisible (*highHz);

    slope.setTitle ("Analyser slope");
    slope.setComponentID ("slope");
    slopeAttachment = std::make_unique<ChoiceSegments> (slope, *state.getParameter (params::id::slope));
    addAndMakeVisible (slope);

    // How the analyser draws: the canvas shows the sound as an analyser with this FFT size shows
    // it (the lows' blur, the scan lines), its levels over this range in ArtFreq's palette.
    fftSize.setTitle ("Analyser FFT size");
    fftSize.setComponentID ("analyser-fft");
    fftAttachment = std::make_unique<ChoiceSegments> (fftSize, *state.getParameter (params::id::analyserFft));
    addAndMakeVisible (fftSize);

    NumberField::Options dbOptions;
    dbOptions.format = [] (double v) { return formatNumber (v, 0); };
    dbOptions.dragUnitsPerPixel = 0.5;
    dbOptions.snap = 1.0;
    dbOptions.editOnSingleClick = true;
    dbOptions.unit = "dB";

    floorDb = std::make_unique<NumberField> (*state.getParameter (params::id::analyserFloor), dbOptions);
    ceilingDb = std::make_unique<NumberField> (*state.getParameter (params::id::analyserCeiling), dbOptions);
    floorDb->setTitle ("Analyser floor");
    ceilingDb->setTitle ("Analyser top");
    floorDb->setComponentID ("analyser-floor");
    ceilingDb->setComponentID ("analyser-top");
    floorDb->setTooltip ("The level your analyser shows as its darkest colour (dBFS): drag, or click to type");
    ceilingDb->setTooltip ("The level your analyser shows as its brightest colour (dBFS): drag, or click to type");
    addAndMakeVisible (*floorDb);
    addAndMakeVisible (*ceilingDb);

    windowSize.setTitle ("Window size");
    windowSize.setComponentID ("window-size");
    windowSize.onChange = [this] (int index)
    {
        if (juce::isPositiveAndBelow (index, static_cast<int> (std::size (windowScales))) && onWindowScaleChosen != nullptr)
            onWindowScaleChosen (windowScales[index]);
    };
    addAndMakeVisible (windowSize);

    resetButton.setComponentID ("reset-drawing");
    resetButton.setTooltip ("Replace the drawing with the one a new instance starts with");
    resetButton.onClick = [this] { showResetConfirmation (true); };
    cancelButton.onClick = [this] { showResetConfirmation (false); };
    confirmButton.setComponentID ("reset-confirm");
    confirmButton.onClick = [this]
    {
        showResetConfirmation (false);

        if (onResetDrawing != nullptr)
            onResetDrawing();
    };

    addAndMakeVisible (resetButton);
    addChildComponent (cancelButton);
    addChildComponent (confirmButton);

    // ArtFreq FX: the timing that the instrument keeps in its footer.
    if (effect)
    {
        trigger = std::make_unique<SegmentedControl> (juce::StringArray { "Transport", "MIDI" });
        trigger->setTitle ("Trigger");
        trigger->setComponentID ("trigger");
        triggerAttachment = std::make_unique<ChoiceSegments> (*trigger, *state.getParameter (params::id::trigger));
        addAndMakeVisible (*trigger);

        // (Its tooltip says what On play means for an export, which is when At bar is wanted.)
        auto tipped = std::make_unique<TippedChoice> (juce::StringArray { "On play", "At bar" });
        tipped->setTooltip ("Where a single pass starts. On play: wherever the DAW starts playing (an export or "
                            "freeze starts it where the render starts). At bar: at a bar of the song, which an "
                            "export keeps");
        startAt = std::move (tipped);
        startAt->setTitle ("Start");
        startAt->setComponentID ("start-at");
        startAtAttachment = std::make_unique<ChoiceSegments> (*startAt, *state.getParameter (params::id::startAt));
        addChildComponent (*startAt);

        NumberField::Options barOptions;
        barOptions.format = [] (double v) { return juce::String (juce::roundToInt (v)); };
        barOptions.dragUnitsPerPixel = 0.25;
        barOptions.snap = 1.0;
        barOptions.editOnSingleClick = true;
        startBar = std::make_unique<NumberField> (*state.getParameter (params::id::startBar), barOptions);
        startBar->setTitle ("Start bar");
        startBar->setComponentID ("start-bar");
        startBar->setTooltip ("The bar of the song the drawing starts at: drag, or click to type");
        addChildComponent (*startBar);

        tempoSource = std::make_unique<SegmentedControl> (juce::StringArray { "Host", "Manual" });
        tempoSource->setTitle ("Tempo");
        tempoSource->setComponentID ("tempo-source");
        tempoAttachment = std::make_unique<ChoiceSegments> (*tempoSource, *state.getParameter (params::id::tempoSource));
        addAndMakeVisible (*tempoSource);

        NumberField::Options bpmOptions;
        bpmOptions.format = [] (double v) { return formatNumber (v, 2); };
        bpmOptions.dragUnitsPerPixel = 0.5;
        bpmOptions.snap = 1.0;
        bpmOptions.editOnSingleClick = true;
        bpmOptions.unit = "BPM";
        bpm = std::make_unique<NumberField> (*state.getParameter (params::id::bpm), bpmOptions);
        bpm->setTitle ("BPM");
        bpm->setComponentID ("bpm");
        addChildComponent (*bpm);
    }

    // What is activated on this computer, and Deactivate: ArtFreq FX's two columns have it in a
    // row across the foot of the panel.
    licence = std::make_unique<LicenceCard> (processor, effect);
    licence->onActivateWanted = [this] { if (onActivateWanted != nullptr) onActivateWanted(); };
    licence->onDeactivated = [this] (const anymeans::license::LicenseManager::Result& result)
    {
        if (onDeactivated != nullptr)
            onDeactivated (result);
    };
    licence->onHeightChanged = [this]
    {
        const auto before = idealHeight;
        resized();

        if (idealHeight != before && onIdealHeightChanged != nullptr)
            onIdealHeightChanged();
    };
    addAndMakeVisible (*licence);

    // Lay out once to learn the height the content needs.
    setSize (getPanelWidth(), 600);
    setSize (getPanelWidth(), idealHeight);
    refresh();
}

SettingsPanel::~SettingsPanel() = default;

int SettingsPanel::getIdealHeight() const
{
    return idealHeight;
}

void SettingsPanel::setCurrentWindowScale (float current)
{
    auto index = -1;

    for (int i = 0; i < static_cast<int> (std::size (windowScales)); ++i)
        if (std::abs (windowScales[i] - current) < 0.01f)
            index = i;

    windowSize.setSelectedIndex (index, juce::dontSendNotification);
}

void SettingsPanel::cancelReset()
{
    showResetConfirmation (false);
    licence->reset();
}

void SettingsPanel::showResetConfirmation (bool shouldShow)
{
    confirmingReset = shouldShow && drawingResettable;
    resetButton.setVisible (drawingResettable && ! confirmingReset);
    cancelButton.setVisible (confirmingReset);
    confirmButton.setVisible (confirmingReset);
    repaint();
}

void SettingsPanel::setDrawingResettable (bool canReset)
{
    if (canReset == drawingResettable)
        return;

    drawingResettable = canReset;
    showResetConfirmation (false);

    const auto before = idealHeight;
    resized();
    repaint();

    if (idealHeight != before && onIdealHeightChanged != nullptr)
        onIdealHeightChanged();
}

void SettingsPanel::refresh()
{
    if (! effect)
        return;

    // Where a single pass starts can be chosen for the one way of playing that has a single
    // pass (Transport without Repeat); for the others the row says where they start instead.
    // The tempo can be typed with Manual; with Host the row shows the DAW's. Nothing is greyed.
    const auto midi = choiceIndex (processor, params::id::trigger) == 1;
    const auto repeats = choiceIndex (processor, params::id::repeat) != 0;
    const auto single = ! midi && ! repeats;
    const auto atBar = params::startAtForIndex (choiceIndex (processor, params::id::startAt)) == StartAt::bar;
    const auto manual = choiceIndex (processor, params::id::tempoSource) != 0;

    startAt->setVisible (single);
    startBar->setVisible (single && atBar);
    bpm->setVisible (manual);

    const juce::String newStartText (single ? "" : midi ? "When a note is played" : "On the DAW's bars (Repeat is on)");
    const auto newTempoText = manual ? juce::String() : "DAW: " + formatNumber (processor.getEffectiveTempo(), 2) + " BPM";

    if (newStartText != startText || newTempoText != tempoText)
    {
        startText = newStartText;
        tempoText = newTempoText;
        repaint (startTextArea.getUnion (tempoTextArea));
    }
}

void SettingsPanel::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds());
    paintTitle (g, "Settings", getWidth(), effect ? effectTitleRowHeight : titleRowHeight);

    for (const auto& caption : sectionCaptions)
        drawCaps (g, caption.label, { caption.x, caption.y, getWidth() - cardPad - caption.x, 12 },
                  juce::Justification::centredLeft, theme::textDim);

    for (auto y : dividers)
        drawHorizontalDivider (g, y, cardPad, getWidth() - cardPad);

    g.setFont (fonts::ui (theme::controlSize));

    for (const auto& row : rows)
    {
        g.setColour (theme::textDim);
        g.drawText (row.label, row.x, row.y, labelWidth, metrics::controlHeight, juce::Justification::centredLeft, false);
    }

    // The hairline between the two columns.
    drawVerticalDivider (g, columnDividerX, (effect ? effectTitleRowHeight + 14 : titleRowHeight + 18), columnsBottom);

    if (! windowCaptionArea.isEmpty())
        drawCaps (g, "Window", windowCaptionArea, juce::Justification::centredRight, theme::textDim);

    if (effect)
    {
        // What the timing rows say in words.
        wrappedText (midiNoteText(), static_cast<float> (midiNoteArea.getWidth())).draw (g, midiNoteArea.toFloat());

        g.setFont (fonts::ui (12.0f));
        g.setColour (theme::textDim);
        g.drawText (startText, startTextArea, juce::Justification::centredLeft, false);
        g.drawText (tempoText, tempoTextArea, juce::Justification::centredLeft, false);
        g.setFont (fonts::ui (theme::controlSize));
    }

    g.setColour (theme::textFaint);
    g.drawText (dash(), rangeDashArea, juce::Justification::centred, false);
    g.drawText (dash(), levelsDashArea, juce::Justification::centred, false);

    g.setFont (fonts::ui (theme::labelSize, fonts::UiWeight::medium));
    g.drawText ("dB/oct", slopeUnitArea, juce::Justification::centredLeft, false);

    wrappedText (noteText(), static_cast<float> (noteArea.getWidth())).draw (g, noteArea.toFloat());

    if (! drawingResettable)
        return;

    juce::AttributedString confirmText;
    confirmText.setWordWrap (juce::AttributedString::byWord);

    if (confirmingReset)
    {
        confirmText.append ("Replace the whole drawing?\n", fonts::ui (12.0f, fonts::UiWeight::medium), theme::text);
        confirmText.append ("You can undo this.", fonts::ui (12.0f), theme::textDim);
    }
    else
    {
        confirmText.append ("Back to the default drawing.", fonts::ui (12.0f), theme::textDim);
    }

    // (The narrow column: the line has the room up to the button it describes.)
    auto confirmArea = confirmTextArea;

    if (! confirmingReset)
        confirmArea.setRight (resetButton.getX() - 12);

    const auto layout = wrappedText (confirmText, static_cast<float> (confirmArea.getWidth()));
    layout.draw (g, confirmArea.toFloat().withSizeKeepingCentre (static_cast<float> (confirmArea.getWidth()),
                                                                  layout.getHeight()));
}

juce::AttributedString SettingsPanel::noteText()
{
    // Short: in ArtFreq FX's window it has two lines. The help says what the settings do.
    return plainText ("As your analyser is set. MiniMeters: Log, 20 Hz " + dash() + " 20 kHz.",
                      fonts::ui (12.0f), theme::textFaint, 2.0f);
}

/** The analyser's rows (Scale, Range, Slope, FFT, Levels) from `y`, in a column at `x`; returns the
    y under them. */
int SettingsPanel::layoutAnalyser (int x, int y, int controlWidth, int gap)
{
    const auto h = metrics::controlHeight;
    const auto controlX = x + labelWidth;

    rows.push_back ({ "Scale", y, x });
    scale.setBounds (controlX, y, controlWidth, h);
    y += h + gap;

    rows.push_back ({ "Range", y, x });
    const auto fieldWidth = (controlWidth - 24) / 2;
    lowHz->setBounds (controlX, y, fieldWidth, h);
    highHz->setBounds (controlX + controlWidth - fieldWidth, y, fieldWidth, h);
    rangeDashArea = { lowHz->getRight(), y, highHz->getX() - lowHz->getRight(), h };
    y += h + gap;

    rows.push_back ({ "Slope", y, x });
    const auto unitWidth = 48;
    slope.setBounds (controlX, y, controlWidth - unitWidth, h);
    slopeUnitArea = { slope.getRight() + 10, y, unitWidth - 10, h };
    y += h + gap;

    rows.push_back ({ "FFT", y, x });
    fftSize.setBounds (controlX, y, controlWidth, h);
    y += h + gap;

    rows.push_back ({ "Levels", y, x });
    floorDb->setBounds (controlX, y, fieldWidth, h);
    ceilingDb->setBounds (controlX + controlWidth - fieldWidth, y, fieldWidth, h);
    levelsDashArea = { floorDb->getRight(), y, ceilingDb->getX() - floorDb->getRight(), h };
    return y + h;
}

juce::AttributedString SettingsPanel::midiNoteText()
{
    return plainText ("MIDI notes reach an effect in Reaper, Bitwig, Cubase, Studio One and FL Studio; "
                      "not in Ableton Live.",
                      fonts::ui (12.0f), theme::textFaint, 2.0f);
}

/** Two columns. Left, the analyser match; right, ArtFreq FX's timing (which its small window has
    no bar for) or the instrument's window size, then the drawing; across the foot, the licence.
    ArtFreq FX's window size sits in the title row: its window is too small for a third row of
    sections. */
void SettingsPanel::resized()
{
    rows.clear();
    sectionCaptions.clear();
    dividers.clear();

    const auto titleHeight = effect ? effectTitleRowHeight : titleRowHeight;
    const auto gap = effect ? effectRowGap - 2 : rowGap - 2;
    placeCloseButton (closeButton, getWidth(), titleHeight);

    const auto h = metrics::controlHeight;
    const auto controlWidth = effectColumnWidth - labelWidth;
    const auto top = titleHeight + (effect ? 14 : 18);
    const auto caption = [&] (const juce::String& text, int x, int& y)
    {
        sectionCaptions.push_back ({ text, y, x });
        y += 12 + gap + 2;
    };

    // ---- left: match your analyser ----
    auto x = cardPad;
    auto controlX = x + labelWidth;
    auto y = top;

    caption ("Match your analyser", x, y);
    y = layoutAnalyser (x, y, controlWidth, gap) + 6;

    const auto noteHeight = static_cast<int> (std::ceil (wrappedText (noteText(), static_cast<float> (controlWidth)).getHeight()));
    noteArea = { controlX, y, controlWidth, noteHeight };
    const auto leftBottom = noteArea.getBottom();

    // ---- right ----
    x = cardPad + effectColumnWidth + effectColumnGap;
    controlX = x + labelWidth;
    columnDividerX = x - effectColumnGap / 2;
    y = top;

    if (effect)
    {
        // The window's size, in the title row.
        constexpr int sizeWidth = 220, sizeHeight = 24;
        windowSize.setBounds (closeButton.getX() - 16 - sizeWidth, (titleHeight - sizeHeight) / 2, sizeWidth, sizeHeight);
        windowCaptionArea = { windowSize.getX() - 72, windowSize.getY(), 64, sizeHeight };

        caption ("Playing", x, y);

        rows.push_back ({ "Trigger", y, x });
        trigger->setBounds (controlX, y, 168, h);
        y += h + 6;

        const auto midiHeight = static_cast<int> (std::ceil (wrappedText (midiNoteText(), static_cast<float> (controlWidth)).getHeight()));
        midiNoteArea = { controlX, y, controlWidth, midiHeight };
        y = midiNoteArea.getBottom() + gap + 2;

        rows.push_back ({ "Start", y, x });
        startAt->setBounds (controlX, y, 148, h);
        startBar->setBounds (startAt->getRight() + 8, y, 56, h);
        startTextArea = { controlX, y, controlWidth, h };
        y += h + gap;

        rows.push_back ({ "Tempo", y, x });
        tempoSource->setBounds (controlX, y, 116, h);
        bpm->setBounds (tempoSource->getRight() + 8, y, controlWidth - 116 - 8, h);
        tempoTextArea = { bpm->getX() + 2, y, bpm->getWidth() - 2, h };
        y += h;
    }
    else
    {
        windowCaptionArea = {};
        caption ("Window", x, y);
        rows.push_back ({ "Size", y, x });
        windowSize.setBounds (controlX, y, controlWidth, h);
        y += h;
    }

    auto rightBottom = y;

    if (drawingResettable)
    {
        y += 12;
        caption ("Drawing", x, y);

        const auto right = x + effectColumnWidth;
        resetButton.setBounds (right - resetButton.getIdealWidth(), y, resetButton.getIdealWidth(), h);
        confirmButton.setBounds (right - confirmButton.getIdealWidth(), y, confirmButton.getIdealWidth(), h);
        cancelButton.setBounds (confirmButton.getX() - 8 - cancelButton.getIdealWidth(), y, cancelButton.getIdealWidth(), h);
        confirmTextArea = { x, y, cancelButton.getX() - 12 - x, h };
        rightBottom = y + h;
    }

    // ---- across the foot: the licence ----
    columnsBottom = juce::jmax (leftBottom, rightBottom);
    y = columnsBottom + effectLicenceGap;
    dividers.push_back (y);
    y += effectLicenceGap;

    const auto cardWidth = getWidth() - 2 * cardPad;
    licence->setBounds (cardPad, y, cardWidth, licence->getIdealHeight (cardWidth));
    idealHeight = licence->getBottom() + effectBottomPad;
}

//==============================================================================
struct HelpPanel::Block
{
    enum class Kind
    {
        heading,
        step,
        bullet,
        shortcut,
        note        // text alone (the activation note at the foot)
    };

    Kind kind = Kind::step;
    juce::String marker;       // the step's number, or the shortcut's keys
    juce::TextLayout layout;
    juce::Rectangle<float> area;   // the text, in the panel's coordinates
};

namespace
{
    constexpr int helpPad = 32;
    constexpr int helpColumnGap = 48;
    constexpr int helpTop = titleRowHeight + 24;
    constexpr float textIndent = 32.0f;
    constexpr float helpLineSpacing = 2.0f;
    constexpr float shortcutIndent = 136.0f;

    struct Paragraph
    {
        juce::String lead;   // drawn in the text colour, semi-bold
        juce::String rest;   // drawn dimmer
    };

    const std::vector<std::pair<juce::String, Paragraph>>& helpSteps()
    {
        static const std::vector<std::pair<juce::String, Paragraph>> steps {
            { "1", { "Put ArtFreq on an instrument track.", "It makes the sound itself; MIDI notes can trigger it." } },
            { "2", { "Open a spectrogram on its output.", "MiniMeters, or any analyser with a scrolling spectrogram." } },
            { "3", { "Match your analyser.", "In Settings (the gear), choose the scale, range, slope, FFT size and "
                                             "levels your analyser uses. MiniMeters: Log, 20 Hz to 20 kHz. Colours "
                                             "depend on your analyser." } },
            { "4", { "Draw.", "Add text, drop in a picture, or paint. A moment after each change the canvas shows "
                              "the real sound, as your analyser will." } },
            { "5", { "Choose a band and the density.", "Put each text or picture in the Low, Mid, High or Full band "
                                                       "in the panel on the right. Density sets how many lines draw it." } },
            { "6", { "Press play in your DAW.", "The drawing plays once from wherever you start. A DAW's export or "
                                                "freeze starts it where the render starts: to keep it at a bar, "
                                                "choose \"from bar\" under Trigger first." } },
            { "7", { "Bounce to WAV.", "Export renders one pass, and a new file after each change. Drag it into "
                                       "your DAW, or save it." } },
        };
        return steps;
    }

    // ArtFreq FX: its own, short. (It has no preview and no export: carving needs the song.)
    const std::vector<std::pair<juce::String, Paragraph>>& effectSteps()
    {
        static const std::vector<std::pair<juce::String, Paragraph>> steps {
            { "1", { "Put ArtFreq FX on a track, a bus or the master,", "before the final limiter." } },
            { "2", { "Open a spectrogram after it", "(MiniMeters) and match it in Settings, the gear." } },
            { "3", { "Draw:", "add text or a picture, or paint. Click one to change it." } },
            { "4", { "Choose a Mode.", "Depth is how far carved frequencies drop: -80 dB looks black." } },
            { "5", { "Press play in your DAW.", "It runs once from there; Carve and Stencil wait for the "
                                                "track's audio. Repeat loops it." } },
            { "6", { "To keep it, bounce the track in your DAW,", "after choosing At bar in Settings: a bounce "
                                                                  "starts it where the bounce starts." } },
        };
        return steps;
    }

    const std::vector<Paragraph>& effectModes()
    {
        static const std::vector<Paragraph> modes {
            { "Carve", "mutes the drawing: dark shapes." },
            { "Carve + Draw", "plays it in a hole it carves." },
            { "Stencil", "lets only the drawing through." },
            { "Draw", "plays it on top of the song." },
            { "Sound", "(Draw modes): the sines' level, density, contrast, lows." },
        };
        return modes;
    }

    // Activating (see ui/Licensing.h), across the foot of both panels: where the key is, what it
    // activates, how many computers and moving to a new one, offline, whom to write to.
    Paragraph activateNote (bool effect)
    {
        const juce::String support (anymeans::license::SUPPORT_EMAIL);

        if (effect)
            return { "Your ArtFreq Pro key",
                     "(purchase email) activates ArtFreq FX and ArtFreq on two computers; deactivate in Settings to "
                     "move. Offline once activated. Help: " + support };

        return { "Your key is in your purchase email.",
                 "Enter it once per computer; an ArtFreq Pro key activates ArtFreq FX too. Two computers per key: to "
                 "move to a new one, deactivate in Settings (Licence) on the old one first. Once activated, ArtFreq "
                 "works offline. Help: " + support };
    }

    const std::vector<Paragraph>& effectNotes()
    {
        static const std::vector<Paragraph> list {
            { "The canvas:", "the real output, on your track once it plays." },
            { "Settings:", "the trigger, where a pass starts, the tempo." },
            { "Latency:", "85 ms; your DAW makes up for it." },
            { "Keys:", "Space plays your DAW. V, B and E pick a tool." },
        };
        return list;
    }

    // ArtFreq FX's panel, in its smaller window.
    constexpr int effectHelpPad = 24;
    constexpr int effectHelpColumnGap = 32;
    constexpr int effectHelpTop = effectTitleRowHeight + 12;
    constexpr int effectHelpBottom = 14;
    constexpr float effectTextIndent = 28.0f;

    const std::vector<Paragraph>& layerTips()
    {
       #if JUCE_MAC
        static const juce::String altClick ("Option-click");
       #else
        static const juce::String altClick ("Alt-click");
       #endif

        static const std::vector<Paragraph> list {
            { "The Layers list", "on the right shows every text and picture, front first. Click to select, "
                                 "double-click for settings, drag to reorder; the eye hides one (silent, but kept)." },
            { "Overlapping objects.", altClick + ", or click again on the same spot, to pick the one underneath. "
                                      "Right-click for Bring to front, Send to back, Hide and Delete." },
        };
        return list;
    }

    const std::vector<Paragraph>& triggerModes()
    {
        static const std::vector<Paragraph> modes {
            { "Transport.", "Plays once when you press play, or from a bar you set under Trigger. With Repeat "
                            "it loops every Length." },
            { "MIDI note.", "Each note plays the drawing once from the start. With Repeat it loops while a "
                            "note is held." },
            { "Stop the DAW and it stops.", "Preview plays it without the DAW." },
        };
        return modes;
    }

    const std::vector<Paragraph>& tips()
    {
        static const std::vector<Paragraph> list {
            { "The highs show the sharpest detail.", "Small text reads best in the High band." },
            { "The lows are blurry in every analyser.", "Below about 250 Hz shapes thicken (the canvas shows it): "
                                                        "keep them big." },
            { "Lows", "(at the foot of the Layers list): Smooth keeps shapes below about 2 kHz filled at "
                      "every FFT size; Stripes plays separate lines there, which long FFTs show as stripes." },
            { "Density shapes the look.", "Raise it for solid shapes; lower it for a scan-line look." },
            { "Contrast", "sets how far the quiet parts of a picture sit below the loudest." },
        };
        return list;
    }

    const std::vector<std::pair<juce::String, juce::String>>& shortcuts()
    {
        static const std::vector<std::pair<juce::String, juce::String>> list {
            { "V|B|E", "Select, Brush, Erase" },
            { shortcutText ("Cmd+Z") + "|" + shortcutText ("Cmd+Shift+Z"), "Undo, redo" },
            { "Delete", "Remove the selected text or picture" },
            { "Space", "Plays your DAW (a space while you type)" },
            { "Shift-drag", "Fine adjustment of knobs and fields" },
            { "Double-click", "Reset a knob; on a value, type one" },
        };
        return list;
    }

    juce::AttributedString leadText (const Paragraph& p, float size = theme::controlSize)
    {
        juce::AttributedString s;
        s.setLineSpacing (helpLineSpacing);
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (p.lead, fonts::ui (size, fonts::UiWeight::semiBold), theme::text);
        s.append (" " + p.rest, fonts::ui (size), theme::textDim);
        return s;
    }

    // The activation note across the foot: under a hairline, its caption on the left.
    constexpr float footerGap = 12.0f;
    constexpr float effectFooterGap = 8.0f;
    constexpr float footerCaptionWidth = 96.0f;
    constexpr float effectFooterCaptionWidth = 72.0f;
    constexpr int helpBottom = 24;
} // namespace

HelpPanel::HelpPanel (bool forEffect)
    : effect (forEffect),
      closeButton ("Close help", icons::close())
{
    setComponentID ("help-panel");
    setWantsKeyboardFocus (true);
    setKeepsKeyFocus (*this);   // as the settings panel

    closeButton.setFlat (true);
    closeButton.setTooltip ("Close (Esc)");
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (closeButton);

    setSize (getPanelWidth(), getIdealHeight());
}

HelpPanel::~HelpPanel() = default;

int HelpPanel::getColumnWidth() const
{
    return effect ? (effectWidth - 2 * effectHelpPad - effectHelpColumnGap) / 2
                  : (width - 2 * helpPad - helpColumnGap) / 2;
}

std::vector<HelpPanel::Block> HelpPanel::layoutColumn (int column, int columnWidth) const
{
    std::vector<Block> result;
    const auto x = static_cast<float> (effect ? effectHelpPad + column * (columnWidth + effectHelpColumnGap)
                                              : helpPad + column * (columnWidth + helpColumnGap));
    const auto w = static_cast<float> (columnWidth);
    auto y = static_cast<float> (effect ? effectHelpTop : helpTop);

    const auto heading = [&] (const juce::String& text)
    {
        Block b;
        b.kind = Block::Kind::heading;
        b.marker = text;
        b.area = { x, y, w, 12.0f };
        result.push_back (std::move (b));
        y += 12.0f + (effect ? 12.0f : 14.0f);
    };

    const auto paragraph = [&] (Block::Kind kind, const juce::String& marker, const juce::AttributedString& text,
                                float indent, float gapAfter)
    {
        Block b;
        b.kind = kind;
        b.marker = marker;
        b.layout.createLayout (text, w - indent);
        b.area = { x + indent, y, w - indent, b.layout.getHeight() };
        y += b.layout.getHeight() + gapAfter;
        result.push_back (std::move (b));
    };

    if (effect)
    {
        // Short, and its own: six steps on the left; the modes and four notes on the right.
        if (column == 0)
        {
            heading ("Getting started");

            for (const auto& [number, p] : effectSteps())
                paragraph (Block::Kind::step, number, leadText (p), effectTextIndent, 6.0f);
        }
        else
        {
            heading ("Modes");

            for (const auto& p : effectModes())
                paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 4.0f);

            y += 2.0f;
            heading ("Good to know");

            for (const auto& p : effectNotes())
                paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 4.0f);
        }

        return result;
    }

    if (column == 0)
    {
        heading ("Getting started");

        for (const auto& [number, p] : helpSteps())
            paragraph (Block::Kind::step, number, leadText (p), textIndent, 10.0f);

        y += 4.0f;
        heading ("Layers");

        for (const auto& p : layerTips())
            paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 6.0f);
    }
    else
    {
        heading ("Trigger modes");

        for (const auto& p : triggerModes())
            paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 6.0f);

        y += 10.0f;
        heading ("Tips");

        for (const auto& p : tips())
            paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 6.0f);

        y += 10.0f;
        heading ("Shortcuts");

        for (const auto& [keys, what] : shortcuts())
        {
            juce::AttributedString s;
            s.append (what, fonts::ui (theme::controlSize), theme::textDim);
            paragraph (Block::Kind::shortcut, keys, s, shortcutIndent, 8.0f);
        }
    }

    return result;
}

std::vector<HelpPanel::Block> HelpPanel::layoutAll (float& dividerY) const
{
    auto all = layoutColumn (0, getColumnWidth());

    for (auto& b : layoutColumn (1, getColumnWidth()))
        all.push_back (std::move (b));

    auto columnsBottom = 0.0f;

    for (const auto& b : all)
        columnsBottom = juce::jmax (columnsBottom, b.area.getBottom());

    // Activating, across the foot of the panel: the caption on the left, the note beside it.
    const auto pad = static_cast<float> (effect ? effectHelpPad : helpPad);
    const auto gap = effect ? effectFooterGap : footerGap;
    const auto captionWidth = effect ? effectFooterCaptionWidth : footerCaptionWidth;
    const auto noteWidth = static_cast<float> (getPanelWidth()) - 2.0f * pad - captionWidth;

    dividerY = std::round (columnsBottom + gap);
    const auto y = dividerY + gap + 1.0f;

    Block caption;
    caption.kind = Block::Kind::heading;
    caption.marker = "Activate";
    caption.area = { pad, y + 2.0f, captionWidth, 12.0f };
    all.push_back (std::move (caption));

    Block note;
    note.kind = Block::Kind::note;
    note.layout.createLayout (leadText (activateNote (effect), effect ? 12.0f : theme::controlSize), noteWidth);
    note.area = { pad + captionWidth, y, noteWidth, note.layout.getHeight() };
    all.push_back (std::move (note));

    return all;
}

int HelpPanel::getIdealHeight() const
{
    auto dividerY = 0.0f;
    auto bottom = 0.0f;

    for (const auto& b : layoutAll (dividerY))
        bottom = juce::jmax (bottom, b.area.getBottom());

    return static_cast<int> (std::ceil (bottom)) + (effect ? effectHelpBottom : helpBottom);
}

void HelpPanel::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds());
    paintTitle (g, effect ? "How to use ArtFreq FX" : "How to use ArtFreq", getWidth(),
                effect ? effectTitleRowHeight : titleRowHeight);

    // A hairline between the two columns, and one over the activation note across the foot.
    const auto footer = juce::roundToInt (footerDividerY);
    const auto pad = effect ? effectHelpPad : helpPad;

    if (effect)
        drawVerticalDivider (g, effectHelpPad + getColumnWidth() + effectHelpColumnGap / 2, effectHelpTop,
                             footer - juce::roundToInt (effectFooterGap));
    else
        drawVerticalDivider (g, helpPad + getColumnWidth() + helpColumnGap / 2, helpTop, footer - juce::roundToInt (footerGap));

    drawHorizontalDivider (g, footer, pad, getWidth() - pad);

    {
        for (const auto& b : blocks)
        {
            switch (b.kind)
            {
                case Block::Kind::heading:
                    drawCaps (g, b.marker, b.area.toNearestInt(), juce::Justification::centredLeft, theme::textDim);
                    break;

                case Block::Kind::step:
                {
                    // The number in a small accent disc, centred on the first line.
                    const auto disc = juce::Rectangle<float> (b.area.getX() - (effect ? effectTextIndent : textIndent),
                                                              b.area.getY() - 1.0f, 20.0f, 20.0f);
                    g.setColour (theme::accent.withAlpha (0.18f));
                    g.fillEllipse (disc);
                    g.setColour (theme::accent.brighter (0.35f));
                    g.setFont (fonts::mono (11.0f, true));
                    g.drawText (b.marker, disc, juce::Justification::centred, false);
                    b.layout.draw (g, b.area);
                    break;
                }

                case Block::Kind::bullet:
                {
                    g.setColour (theme::accent);
                    g.fillEllipse (b.area.getX() - 14.0f, b.area.getY() + 7.0f, 4.0f, 4.0f);
                    b.layout.draw (g, b.area);
                    break;
                }

                case Block::Kind::note:
                    b.layout.draw (g, b.area);
                    break;

                case Block::Kind::shortcut:
                {
                    // Keys as small raised caps.
                    const auto font = fonts::ui (theme::labelSize, fonts::UiWeight::semiBold);
                    auto keyX = b.area.getX() - shortcutIndent;

                    for (const auto& key : juce::StringArray::fromTokens (b.marker, "|", {}))
                    {
                        const auto keyWidth = static_cast<float> (textWidth (font, key)) + 12.0f;
                        const auto cap = juce::Rectangle<float> (keyX, b.area.getY() - 1.0f, keyWidth, 20.0f);
                        g.setColour (theme::raised);
                        g.fillRoundedRectangle (cap, 4.0f);
                        g.setColour (theme::line);
                        g.drawRoundedRectangle (cap.reduced (0.5f), 4.0f, 1.0f);
                        g.setColour (theme::text);
                        g.setFont (font);
                        g.drawText (key, cap, juce::Justification::centred, false);
                        keyX += keyWidth + 4.0f;
                    }

                    b.layout.draw (g, b.area);
                    break;
                }
            }
        }
    }
}

void HelpPanel::resized()
{
    placeCloseButton (closeButton, getWidth(), effect ? effectTitleRowHeight : titleRowHeight);

    blocks = layoutAll (footerDividerY);
}

} // namespace artfreq::ui::shell
