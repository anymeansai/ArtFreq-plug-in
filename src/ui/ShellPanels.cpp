#include "ShellPanels.h"

#include "KeyFocus.h"
#include "Parameters.h"
#include "ShellHeader.h"

namespace artfreq::ui::shell
{
namespace
{
    constexpr int cardPad = 20;
    constexpr int titleRowHeight = 56;
    constexpr int closeSize = 28;
    constexpr int rowGap = 12;
    constexpr int sectionGap = 20;
    constexpr int labelWidth = 76;

    void paintCard (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        const auto r = bounds.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, theme::panelCorner);
        g.setColour (theme::line.brighter (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), theme::panelCorner, 1.0f);
    }

    void paintTitle (juce::Graphics& g, const juce::String& title, int width)
    {
        g.setColour (theme::text);
        g.setFont (fonts::ui (theme::titleSize, fonts::UiWeight::semiBold));
        g.drawText (title, cardPad, 0, width - 2 * cardPad, titleRowHeight, juce::Justification::centredLeft, false);
        drawHorizontalDivider (g, titleRowHeight - 1, 0, width);
    }

    void placeCloseButton (juce::Component& button, int width)
    {
        button.setBounds (width - 14 - closeSize, (titleRowHeight - closeSize) / 2, closeSize, closeSize);
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
SettingsPanel::SettingsPanel (ArtFreqProcessor& processor)
    : closeButton ("Close settings", icons::close())
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

    // Lay out once to learn the height the content needs.
    setSize (width, 600);
    setSize (width, idealHeight);
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
}

void SettingsPanel::showResetConfirmation (bool shouldShow)
{
    confirmingReset = shouldShow;
    resetButton.setVisible (! shouldShow);
    cancelButton.setVisible (shouldShow);
    confirmButton.setVisible (shouldShow);
    repaint();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds());
    paintTitle (g, "Settings", getWidth());

    for (const auto& [text, y] : sectionCaptions)
        drawCaps (g, text, { cardPad, y, getWidth() - 2 * cardPad, 12 }, juce::Justification::centredLeft, theme::textDim);

    for (auto y : dividers)
        drawHorizontalDivider (g, y, cardPad, getWidth() - cardPad);

    g.setFont (fonts::ui (theme::controlSize));

    for (const auto& row : rows)
    {
        g.setColour (theme::textDim);
        g.drawText (row.label, cardPad, row.y, labelWidth, metrics::controlHeight, juce::Justification::centredLeft, false);
    }

    g.setColour (theme::textFaint);
    g.drawText (dash(), rangeDashArea, juce::Justification::centred, false);

    g.setFont (fonts::ui (theme::labelSize, fonts::UiWeight::medium));
    g.drawText ("dB/oct", slopeUnitArea, juce::Justification::centredLeft, false);

    wrappedText (noteText(), static_cast<float> (noteArea.getWidth())).draw (g, noteArea.toFloat());

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

    const auto layout = wrappedText (confirmText, static_cast<float> (confirmTextArea.getWidth()));
    layout.draw (g, confirmTextArea.toFloat().withSizeKeepingCentre (static_cast<float> (confirmTextArea.getWidth()),
                                                                      layout.getHeight()));
}

juce::AttributedString SettingsPanel::noteText()
{
    return plainText ("MiniMeters: Log, 20 Hz " + dash() + " 20 kHz. Set Slope to match the Slope setting "
                      "of your analyser.",
                      fonts::ui (12.0f), theme::textFaint, 2.0f);
}

void SettingsPanel::resized()
{
    placeCloseButton (closeButton, getWidth());

    rows.clear();
    sectionCaptions.clear();
    dividers.clear();

    const auto controlX = cardPad + labelWidth;
    const auto controlWidth = getWidth() - cardPad - controlX;
    const auto h = metrics::controlHeight;
    auto y = titleRowHeight + sectionGap;

    // Match your analyser
    sectionCaptions.push_back ({ "Match your analyser", y });
    y += 12 + rowGap + 4;

    rows.push_back ({ "Scale", y });
    scale.setBounds (controlX, y, controlWidth, h);
    y += h + rowGap;

    rows.push_back ({ "Range", y });
    const auto fieldWidth = (controlWidth - 24) / 2;
    lowHz->setBounds (controlX, y, fieldWidth, h);
    highHz->setBounds (controlX + controlWidth - fieldWidth, y, fieldWidth, h);
    rangeDashArea = { lowHz->getRight(), y, highHz->getX() - lowHz->getRight(), h };
    y += h + rowGap;

    rows.push_back ({ "Slope", y });
    const auto unitWidth = 48;
    slope.setBounds (controlX, y, controlWidth - unitWidth, h);
    slopeUnitArea = { slope.getRight() + 10, y, unitWidth - 10, h };
    y += h + 10;

    const auto noteHeight = static_cast<int> (std::ceil (wrappedText (noteText(), static_cast<float> (controlWidth)).getHeight()));
    noteArea = { controlX, y, controlWidth, noteHeight };
    y = noteArea.getBottom() + sectionGap - 4;

    dividers.push_back (y);
    y += sectionGap;

    // Window
    sectionCaptions.push_back ({ "Window", y });
    y += 12 + rowGap + 4;

    rows.push_back ({ "Size", y });
    windowSize.setBounds (controlX, y, controlWidth, h);
    y += h + sectionGap;

    dividers.push_back (y);
    y += sectionGap;

    // Drawing
    sectionCaptions.push_back ({ "Drawing", y });
    y += 12 + rowGap + 4;

    const auto right = getWidth() - cardPad;
    resetButton.setBounds (right - resetButton.getIdealWidth(), y, resetButton.getIdealWidth(), h);
    confirmButton.setBounds (right - confirmButton.getIdealWidth(), y, confirmButton.getIdealWidth(), h);
    cancelButton.setBounds (confirmButton.getX() - 8 - cancelButton.getIdealWidth(), y, cancelButton.getIdealWidth(), h);
    confirmTextArea = { cardPad, y, cancelButton.getX() - 12 - cardPad, h };
    y += h + sectionGap;

    idealHeight = y;
}

//==============================================================================
struct HelpPanel::Block
{
    enum class Kind
    {
        heading,
        step,
        bullet,
        shortcut
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
    constexpr float helpLineSpacing = 3.0f;
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
            { "3", { "Match your analyser.", "In Settings (the gear), choose the same scale, range and slope as your "
                                             "analyser. MiniMeters: Log, 20 Hz to 20 kHz." } },
            { "4", { "Draw.", "Add text, drop in a picture, or paint with the brush. The canvas shows what your "
                              "analyser will show." } },
            { "5", { "Choose a band and the density.", "Put each text or picture in the Low, Mid, High or Full band "
                                                       "in the panel on the right. Density sets how many lines draw it." } },
            { "6", { "Press play in your DAW.", "The drawing plays once from wherever you start. Preview plays it "
                                                "while the DAW is stopped." } },
            { "7", { "Bounce to WAV.", "Export renders one pass, and a new file after each change. Drag it into "
                                       "your DAW, or save it." } },
        };
        return steps;
    }

    // ArtFreq FX: the same window on the audio passing through.
    const std::vector<std::pair<juce::String, Paragraph>>& effectSteps()
    {
        static const std::vector<std::pair<juce::String, Paragraph>> steps {
            { "1", { "Put ArtFreq FX on an audio track, a bus or the master.", "It works on the audio passing "
                                                                               "through." } },
            { "2", { "Open a spectrogram after it.", "MiniMeters on the master, or any scrolling spectrogram." } },
            { "3", { "Match your analyser.", "In Settings (the gear): the same scale, range and slope. MiniMeters: "
                                             "Log, 20 Hz to 20 kHz." } },
            { "4", { "Draw.", "Add text, a picture or brush strokes. The canvas shows them over a stand-in for "
                              "your song." } },
            { "5", { "Choose a mode and the depth", "at the top. Depth is how far the drawing's frequencies drop "
                                                    "(-80 dB looks black)." } },
            { "6", { "Press play in your DAW.", "It carves once from wherever you start (or from a bar: under "
                                                "Trigger). Preview runs the pass on the audio coming in." } },
            { "7", { "Bounce the track in your DAW.", "Carving needs the song. In the Draw modes, Export still "
                                                      "bounces the sines." } },
        };
        return steps;
    }

    const std::vector<Paragraph>& effectModes()
    {
        static const std::vector<Paragraph> modes {
            { "Carve.", "The drawing's frequencies are muted, so it shows as dark shapes in the song. The default." },
            { "Carve + Draw.", "The drawing played in a hole carved a little wider: bright with a dark edge, "
                               "even in a dense mix." },
            { "Stencil.", "The opposite of Carve: only the drawing's frequencies pass." },
            { "Draw.", "The drawing's sines on top of the audio. Density, Contrast, Level and Lows shape the "
                       "sines of both Draw modes." },
        };
        return modes;
    }

    const std::vector<Paragraph>& effectTiming()
    {
        static const std::vector<Paragraph> list {
            { "As in ArtFreq.", "Length, Trigger, Repeat and Tempo work as in the instrument; outside the pass the "
                                "audio passes untouched." },
            { "MIDI note", "works where the DAW sends MIDI to effects (Reaper, Bitwig, Cubase, Studio One, FL Studio). Ableton "
                           "Live does not: use Transport there." },
            { "Latency:", "85 ms, the same in every mode and in bypass. Your DAW makes up for it." },
        };
        return list;
    }

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
            { "Transport.", "Plays once when you press play, wherever the playhead is. Under Trigger you can "
                            "start it at a bar of the song instead. With Repeat it loops every Length." },
            { "MIDI note.", "Each note plays the drawing once from the start. With Repeat it loops while a "
                            "note is held." },
            { "Nothing plays by itself.", "Stop the DAW and it stops; Preview plays it without the DAW." },
        };
        return modes;
    }

    const std::vector<Paragraph>& tips()
    {
        static const std::vector<Paragraph> list {
            { "The highs show the sharpest detail.", "Small text reads best in the High band." },
            { "The lowest octaves are coarse in every analyser.", "Keep shapes there big and bold." },
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
            { "Space", "Plays your DAW, also from this window (typing in a text box, it is a space)" },
            { "Shift-drag", "Fine adjustment of knobs and fields" },
            { "Double-click", "Reset a knob; on a value, type one" },
        };
        return list;
    }

    juce::AttributedString leadText (const Paragraph& p)
    {
        juce::AttributedString s;
        s.setLineSpacing (helpLineSpacing);
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (p.lead, fonts::ui (theme::controlSize, fonts::UiWeight::semiBold), theme::text);
        s.append (" " + p.rest, fonts::ui (theme::controlSize), theme::textDim);
        return s;
    }
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

    setSize (width, getIdealHeight());
}

HelpPanel::~HelpPanel() = default;

std::vector<HelpPanel::Block> HelpPanel::layoutColumn (int column, int columnWidth) const
{
    std::vector<Block> result;
    const auto x = static_cast<float> (helpPad + column * (columnWidth + helpColumnGap));
    const auto w = static_cast<float> (columnWidth);
    auto y = static_cast<float> (helpTop);

    const auto heading = [&] (const juce::String& text)
    {
        Block b;
        b.kind = Block::Kind::heading;
        b.marker = text;
        b.area = { x, y, w, 12.0f };
        result.push_back (std::move (b));
        y += 12.0f + 14.0f;
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

    if (column == 0)
    {
        heading ("Getting started");

        for (const auto& [number, p] : effect ? effectSteps() : helpSteps())
            paragraph (Block::Kind::step, number, leadText (p), textIndent, 14.0f);

        y += 4.0f;
        heading ("Layers");

        for (const auto& p : layerTips())
            paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 8.0f);
    }
    else
    {
        heading (effect ? "Modes" : "Trigger modes");

        for (const auto& p : effect ? effectModes() : triggerModes())
            paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 8.0f);

        y += 14.0f;
        heading (effect ? "Timing, MIDI and latency" : "Tips");

        for (const auto& p : effect ? effectTiming() : tips())
            paragraph (Block::Kind::bullet, {}, leadText (p), 16.0f, 8.0f);

        y += 14.0f;
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

int HelpPanel::getIdealHeight() const
{
    const auto columnWidth = (width - 2 * helpPad - helpColumnGap) / 2;
    auto bottom = 0.0f;

    for (int column = 0; column < 2; ++column)
        for (const auto& b : layoutColumn (column, columnWidth))
            bottom = juce::jmax (bottom, b.area.getBottom());

    return static_cast<int> (std::ceil (bottom)) + 28;
}

void HelpPanel::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds());
    paintTitle (g, effect ? "How to use ArtFreq FX" : "How to use ArtFreq", getWidth());

    const auto columnWidth = (getWidth() - 2 * helpPad - helpColumnGap) / 2;

    // A hairline between the two columns.
    const auto middle = helpPad + columnWidth + helpColumnGap / 2;
    drawVerticalDivider (g, middle, helpTop, getHeight() - 28);

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
                    const auto disc = juce::Rectangle<float> (b.area.getX() - textIndent, b.area.getY() - 1.0f, 20.0f, 20.0f);
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
    placeCloseButton (closeButton, getWidth());

    const auto columnWidth = (getWidth() - 2 * helpPad - helpColumnGap) / 2;
    blocks = layoutColumn (0, columnWidth);

    for (auto& b : layoutColumn (1, columnWidth))
        blocks.push_back (std::move (b));
}

} // namespace artfreq::ui::shell
