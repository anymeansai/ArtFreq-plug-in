#include "EffectShell.h"

#include "Actions.h"
#include "Emblem.h"
#include "Parameters.h"
#include "ShellHeader.h"

namespace artfreq::ui::shell
{
namespace
{
    using namespace effectMetrics;

    constexpr int buttonSize = 28;       // the top bar's icon buttons
    constexpr int logoSize = 22;
    constexpr float wordmarkSize = 16.0f;
    constexpr const char* wordmark = "ArtFreq";
    constexpr const char* effectMark = "FX";

    constexpr int toolButtonWidth = 30;
    constexpr int groupInset = 2;
    constexpr int labelledPad = 6;       // either side of a labelled button's icon and label
    constexpr int lengthWidth = 82;
    constexpr int repeatWidth = 76;
    constexpr int resizerRoom = 16;      // the window's corner resizer sits in the bar's bottom right corner
    constexpr float hintSize = 11.0f;

    constexpr int captionGap = 6;        // a strip's caption to its control
    constexpr int stripGroupGap = 18;    // between a strip's settings

    juce::Font wordmarkFont()   { return fonts::ui (wordmarkSize, fonts::UiWeight::semiBold); }
    juce::Font hintFont()       { return fonts::ui (hintSize); }

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

    /** A labelled button's width: its icon, a gap of 6, its label, and a little air. */
    int labelledWidth (const juce::String& label)
    {
        return 2 * labelledPad + 16 + 6 + textWidth (fonts::ui (theme::controlSize, fonts::UiWeight::medium), label);
    }

    /** What a mode does with the drawing, as a verb: "carves", "draws", "stencils". */
    juce::String modeVerb (EffectMode mode)
    {
        switch (mode)
        {
            case EffectMode::carve:     return "carves";
            case EffectMode::stencil:   return "stencils";
            case EffectMode::carveDraw:
            case EffectMode::draw:      break;
        }

        return "draws";   // Carve + Draw draws too, in a hole it carves
    }

    juce::String capitalised (const juce::String& word)
    {
        return word.substring (0, 1).toUpperCase() + word.substring (1);
    }

    void paintStripBackground (juce::Graphics& g, juce::Component& strip, bool lineAtBottom)
    {
        g.fillAll (theme::panel);
        drawHorizontalDivider (g, lineAtBottom ? strip.getHeight() - 1 : 0, 0, strip.getWidth());
    }
} // namespace

//==============================================================================
juce::String effectHint (ArtFreqProcessor& processor, bool shorter)
{
    const auto verb = modeVerb (processor.getEffectMode());
    const auto repeats = choiceIndex (processor, params::id::repeat) != 0;

    if (choiceIndex (processor, params::id::trigger) == 1)
    {
        if (repeats)
            return shorter ? "Loops with a held note" : "Loops while a note is held";

        return shorter ? "Once per note" : "Each note " + verb + " it once";
    }

    if (repeats)
    {
        const auto lengths = params::lengthNames();
        return "Repeats every " + lengths[juce::jlimit (0, lengths.size() - 1, choiceIndex (processor, params::id::length))];
    }

    if (params::startAtForIndex (choiceIndex (processor, params::id::startAt)) == StartAt::bar)
        return capitalised (verb) + " once from bar " + juce::String (processor.getSinglePassStartBar());

    return capitalised (verb) + (shorter ? " once on play" : " once when you press play");
}

//==============================================================================
LiveToggle::LiveToggle() : juce::Button ("Live")
{
    setButtonText ("Live");
    setTitle ("Live");
    setClickingTogglesState (false);   // the editor sets it, with the view
    setWantsKeyboardFocus (false);
}

int LiveToggle::getIdealWidth()
{
    return 8 + 6 + 5 + textWidth (fonts::ui (12.5f, fonts::UiWeight::semiBold), "Live") + 8;
}

void LiveToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = getToggleState();
    const auto bounds = getLocalBounds().toFloat();
    const auto corner = bounds.getHeight() * 0.5f;

    if (on)
    {
        g.setColour (theme::accent2.withAlpha (down ? 0.26f : highlighted ? 0.2f : 0.14f));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (theme::accent2.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
    }
    else
    {
        g.setColour (down ? theme::raised.darker (0.3f) : highlighted ? theme::raisedHover : theme::raised);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (theme::line);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
    }

    // A dot (the live view's badge has one too), then the word.
    const auto font = fonts::ui (12.5f, on ? fonts::UiWeight::semiBold : fonts::UiWeight::medium);
    const auto colour = on ? theme::accent2.brighter (0.2f) : highlighted || down ? theme::text : theme::textDim;

    g.setColour (on ? theme::accent2 : colour.withAlpha (0.8f));
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ 11.0f, bounds.getCentreY() }));

    g.setColour (colour);
    g.setFont (font);
    g.drawText (getButtonText(), juce::Rectangle<float> (19.0f, 0.0f, bounds.getWidth() - 19.0f, bounds.getHeight()),
                juce::Justification::centredLeft, false);
}

//==============================================================================
EffectTopBar::EffectTopBar (ArtFreqProcessor& processor)
    : controls (processor),
      undoButton ("Undo", icons::undo()),
      redoButton ("Redo", icons::redo()),
      helpButton ("Help", icons::help()),
      settingsButton ("Settings", icons::gear())
{
    setOpaque (true);
    setComponentID ("header");

    undoButton.setTooltip ("Undo  " + shortcutText ("Cmd+Z"));
    redoButton.setTooltip ("Redo  " + shortcutText ("Cmd+Shift+Z"));
    liveButton.setTooltip ("Live: a spectrogram of what comes out of ArtFreq FX, in place of the canvas");
    helpButton.setTooltip ("How ArtFreq FX works");
    settingsButton.setTooltip ("Settings: match your analyser, trigger, start and tempo, window size");

    undoButton.setComponentID ("undo");
    redoButton.setComponentID ("redo");
    liveButton.setComponentID ("live");
    helpButton.setComponentID ("help");
    settingsButton.setComponentID ("settings");

    for (auto* button : { &undoButton, &redoButton, &helpButton, &settingsButton })
    {
        button->setFlat (true);
        addAndMakeVisible (*button);
    }

    addAndMakeVisible (liveButton);
    addAndMakeVisible (controls);

    // Nothing to undo yet.
    setUndoState (false, false);
}

void EffectTopBar::refresh()
{
    controls.refresh();
}

void EffectTopBar::setUndoState (bool canUndo, bool canRedo)
{
    // Shown only while they have something to do, each in its own place (nothing else moves).
    undoButton.setVisible (canUndo);
    redoButton.setVisible (canRedo);
}

void EffectTopBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    drawHorizontalDivider (g, getHeight() - 1, 0, getWidth());

    drawEmblem (g, logoArea.toFloat());

    // "ArtFreq FX", centred on the bar by its cap height; FX in the playing colour, as in the
    // instrument's sibling.
    const auto baseline = getHeight() / 2 + 6;
    auto x = logoArea.getRight() + 8;

    g.setFont (wordmarkFont());
    g.setColour (theme::text);
    g.drawSingleLineText (wordmark, x, baseline);
    x += textWidth (wordmarkFont(), wordmark) + 4;
    g.setColour (theme::accent2);
    g.drawSingleLineText (effectMark, x, baseline);

    drawVerticalDivider (g, dividerX, getHeight() / 2 - 9, getHeight() / 2 + 9);
}

void EffectTopBar::resized()
{
    const auto centreY = getHeight() / 2;
    const auto buttonY = centreY - buttonSize / 2;

    logoArea = { pad, centreY - logoSize / 2, logoSize, logoSize };

    const auto markRight = logoArea.getRight() + 8 + textWidth (wordmarkFont(), wordmark) + 4
                         + textWidth (wordmarkFont(), effectMark);

    // From the right: settings, help, a divider, Live, redo, undo. Undo and redo come and go at
    // the open end of the row, so nothing else moves.
    auto right = getWidth() - 8;
    settingsButton.setBounds (right - buttonSize, buttonY, buttonSize, buttonSize);
    right = settingsButton.getX() - 2;
    helpButton.setBounds (right - buttonSize, buttonY, buttonSize, buttonSize);
    dividerX = helpButton.getX() - 7;
    right = dividerX - 7;

    const auto liveWidth = LiveToggle::getIdealWidth();
    liveButton.setBounds (right - liveWidth, centreY - 12, liveWidth, 24);
    right = liveButton.getX() - 4;
    redoButton.setBounds (right - buttonSize, buttonY, buttonSize, buttonSize);
    right = redoButton.getX() - 2;
    undoButton.setBounds (right - buttonSize, buttonY, buttonSize, buttonSize);

    // Mode and Depth follow the name.
    const auto x = markRight + 12;
    controls.setBounds (x, 0, EffectControls::getIdealWidth(), getHeight());
    jassert (controls.getRight() + 6 <= undoButton.getX());
}

//==============================================================================
EffectBottomBar::EffectBottomBar (ArtFreqProcessor& p, EditorState& s)
    : selectTool ("Select", icons::select()),
      brushTool ("Brush", icons::brush()),
      eraseTool ("Erase", icons::eraser()),
      addTextButton ("Add text", shellIcons::plus(), "Text"),
      addImageButton ("Add image", shellIcons::plus(), "Image"),
      layersButton ("Layers", icons::layers(), "Layers"),
      processor (p),
      state (s)
{
    setOpaque (true);
    setComponentID ("footer");

    selectTool.setTooltip ("Select, move and resize text and pictures  (V)");
    brushTool.setTooltip ("Brush: paint onto the canvas  (B)");
    eraseTool.setTooltip ("Erase painted strokes  (E)");
    addTextButton.setTooltip ("Add a text object (in the mid band, or another band if that is taken)");
    addImageButton.setTooltip ("Add a picture (" + actions::pictureFormatNames() + "). You can also drop one onto the canvas.");
    layersButton.setTooltip ("Layers: every text and picture, and the paint. Select, hide, reorder or delete them.");

    selectTool.setComponentID ("tool-select");
    brushTool.setComponentID ("tool-brush");
    eraseTool.setComponentID ("tool-erase");
    addTextButton.setComponentID ("add-text");
    addImageButton.setComponentID ("add-image");
    layersButton.setComponentID ("layers");

    const std::pair<IconButton*, Tool> tools[] = { { &selectTool, Tool::select },
                                                   { &brushTool, Tool::brush },
                                                   { &eraseTool, Tool::erase } };

    for (const auto& [button, tool] : tools)
    {
        button->setFlat (true);
        button->setClickingTogglesState (false);
        button->onClick = [this, t = tool] { state.setTool (t); };
        addAndMakeVisible (*button);
    }

    for (auto* button : { &addTextButton, &addImageButton, &layersButton })
        addAndMakeVisible (*button);

    length.addItemList (params::lengthNames(), 1);
    length.setComponentID ("length");
    length.setTitle ("Length");
    length.setTooltip ("Length: how long one pass of the drawing lasts, in beats or bars");
    lengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getParameters(),
                                                                                                  params::id::length, length);
    addAndMakeVisible (length);

    repeat.setComponentID ("repeat");
    repeat.setTitle ("Repeat");
    repeat.setTooltip ("Repeat: loop the drawing while the DAW plays. Off: once, each time you press play.");
    repeatAttachment = std::make_unique<juce::ButtonParameterAttachment> (parameter (processor, params::id::repeat), repeat);
    addAndMakeVisible (repeat);

    updateFromState();
    state.addChangeListener (this);
    refresh();
}

EffectBottomBar::~EffectBottomBar()
{
    state.removeChangeListener (this);
}

void EffectBottomBar::setToolsShown (bool shouldShow)
{
    if (toolsShown == shouldShow)
        return;

    toolsShown = shouldShow;

    for (auto* tool : { &selectTool, &brushTool, &eraseTool })
        tool->setVisible (shouldShow);

    resized();
    repaint();
}

juce::String EffectBottomBar::getShownHint() const
{
    // The sentence if the bar has room for it, else the same in fewer words: never clipped.
    const auto full = effectHint (processor);
    return textWidth (hintFont(), full) <= hintArea.getWidth() ? full : effectHint (processor, true);
}

void EffectBottomBar::refresh()
{
    if (const auto hint = getShownHint(); hint != shownHint)
    {
        shownHint = hint;
        repaint (hintArea);
    }
}

void EffectBottomBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    drawHorizontalDivider (g, 0, 0, getWidth());

    if (toolsShown)
    {
        // The tools share one raised box; the buttons inside are concentric with it.
        const auto group = toolGroup.toFloat();
        const auto corner = theme::controlCorner + static_cast<float> (groupInset);
        g.setColour (theme::raised);
        g.fillRoundedRectangle (group, corner);
        g.setColour (theme::line);
        g.drawRoundedRectangle (group.reduced (0.5f), corner, 1.0f);
    }

    g.setFont (hintFont());
    g.setColour (theme::textDim);
    g.drawText (getShownHint(), hintArea, juce::Justification::centredRight, false);
}

void EffectBottomBar::resized()
{
    const auto height = metrics::controlHeight;
    const auto top = (getHeight() - height) / 2;
    auto x = pad - 2;

    if (toolsShown)
    {
        toolGroup = { x, top, 3 * toolButtonWidth + 2 * groupInset, height };
        auto toolX = x + groupInset;

        for (auto* b : { &selectTool, &brushTool, &eraseTool })
        {
            b->setBounds (toolX, top + groupInset, toolButtonWidth, height - 2 * groupInset);
            toolX += toolButtonWidth;
        }

        x = toolGroup.getRight() + 8;
    }

    const auto place = [&] (IconButton& b, const juce::String& label, int gapAfter)
    {
        const auto w = labelledWidth (label);
        b.setBounds (x, top, w, height);
        x += w + gapAfter;
    };

    place (addTextButton, "Text", 4);
    place (addImageButton, "Image", 4);
    place (layersButton, "Layers", 0);

    // From the right: Repeat (clear of the window's corner resizer), Length, and the line that
    // says what they add up to.
    auto right = getWidth() - resizerRoom;
    repeat.setBounds (right - repeatWidth, top, repeatWidth, height);
    right = repeat.getX() - 6;
    length.setBounds (right - lengthWidth, top, lengthWidth, height);
    right = length.getX() - 12;

    hintArea = { x + 12, 0, juce::jmax (0, right - x - 12), getHeight() };
    shownHint = getShownHint();
}

void EffectBottomBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateFromState();
}

void EffectBottomBar::updateFromState()
{
    const auto tool = state.getTool();

    selectTool.setToggleState (tool == Tool::select, juce::dontSendNotification);
    brushTool.setToggleState (tool == Tool::brush, juce::dontSendNotification);
    eraseTool.setToggleState (tool == Tool::erase, juce::dontSendNotification);
}

//==============================================================================
SoundStrip::SoundStrip (ArtFreqProcessor& processor)
    : lows (params::lowEndNames())
{
    setOpaque (true);
    setComponentID ("sound-strip");

    NumberField::Options levelOptions;
    levelOptions.format = [] (double v) { return juce::String (v, 1); };
    levelOptions.dragUnitsPerPixel = 0.25;
    levelOptions.snap = 0.5;
    levelOptions.editOnSingleClick = true;
    levelOptions.unit = "dB";
    levelOptions.textSize = 12.0f;
    level = std::make_unique<NumberField> (parameter (processor, params::id::level), levelOptions);
    level->setComponentID ("level");
    level->setTitle ("Level");
    level->setTooltip ("Level: how loud the drawing's sines are. The audio passing through keeps its own level. "
                       "Drag, or click to type.");

    NumberField::Options densityOptions;
    densityOptions.format = [] (double v) { return juce::String (juce::roundToInt (v)); };
    densityOptions.logDrag = true;
    densityOptions.snap = 1.0;
    densityOptions.editOnSingleClick = true;
    densityOptions.unit = "lines";
    densityOptions.textSize = 12.0f;
    density = std::make_unique<NumberField> (parameter (processor, params::id::density), densityOptions);
    density->setComponentID ("density");
    density->setTitle ("Density");
    density->setTooltip ("Density: how many lines share the height. Few = scan lines, many = solid shapes.");

    NumberField::Options contrastOptions;
    contrastOptions.format = [] (double v) { return juce::String (juce::roundToInt (v)); };
    contrastOptions.dragUnitsPerPixel = 0.25;
    contrastOptions.snap = 1.0;
    contrastOptions.editOnSingleClick = true;
    contrastOptions.unit = "dB";
    contrastOptions.textSize = 12.0f;
    contrast = std::make_unique<NumberField> (parameter (processor, params::id::contrast), contrastOptions);
    contrast->setComponentID ("contrast");
    contrast->setTitle ("Contrast");
    contrast->setTooltip ("Contrast: how far the quietest drawn parts sit below the loudest (dB)");

    lows.setComponentID ("lows");
    lows.setTitle ("Lows");
    lows.setTooltip ("Lows: how the crowded low end plays (below about 2 kHz on a log scale). "
                     "Smooth: sines gliding upwards keep shapes filled at every analyser FFT size. "
                     "Stripes: separate lines 25 Hz apart, which long FFTs show as stripes (an effect).");
    lowsAttachment = std::make_unique<ChoiceSegments> (lows, parameter (processor, params::id::lowEnd));

    for (auto* c : std::initializer_list<juce::Component*> { level.get(), density.get(), contrast.get(), &lows })
        addAndMakeVisible (c);
}

SoundStrip::~SoundStrip() = default;

void SoundStrip::paint (juce::Graphics& g)
{
    paintStripBackground (g, *this, true);

    drawCaps (g, "Sound", titleArea, juce::Justification::centredLeft, theme::text.withAlpha (0.82f));
    drawCaps (g, "Level", levelCaption, juce::Justification::centredLeft, theme::textDim);
    drawCaps (g, "Density", densityCaption, juce::Justification::centredLeft, theme::textDim);
    drawCaps (g, "Contrast", contrastCaption, juce::Justification::centredLeft, theme::textDim);
    drawCaps (g, "Lows", lowsCaption, juce::Justification::centredLeft, theme::textDim);
}

void SoundStrip::resized()
{
    const auto h = stripControlHeight;
    const auto y = (getHeight() - 1 - h) / 2;
    auto x = pad;

    titleArea = { x, 0, capsWidth ("Sound"), getHeight() - 1 };
    x = titleArea.getRight() + stripGroupGap;

    const auto group = [&] (const char* caption, juce::Rectangle<int>& captionArea, juce::Component& control, int width)
    {
        captionArea = { x, 0, capsWidth (caption), getHeight() - 1 };
        x = captionArea.getRight() + captionGap;
        control.setBounds (x, y, width, h);
        x += width + stripGroupGap;
    };

    group ("Level", levelCaption, *level, 70);
    group ("Density", densityCaption, *density, 76);
    group ("Contrast", contrastCaption, *contrast, 58);
    group ("Lows", lowsCaption, lows, 124);
}

//==============================================================================
BrushStrip::BrushStrip (EditorState& s)
    : brushSize (s, BrushSlider::Setting::size),
      brushSoftness (s, BrushSlider::Setting::softness),
      brushStrength (s, BrushSlider::Setting::strength),
      state (s),
      shownTool (s.getTool())
{
    setOpaque (true);
    setComponentID ("brush-strip");

    brushSize.setComponentID ("brush-size");
    brushSoftness.setComponentID ("brush-softness");
    brushStrength.setComponentID ("brush-strength");

    for (auto* slider : { &brushSize, &brushSoftness, &brushStrength })
        addAndMakeVisible (*slider);

    state.addChangeListener (this);
}

BrushStrip::~BrushStrip()
{
    state.removeChangeListener (this);
}

void BrushStrip::paint (juce::Graphics& g)
{
    paintStripBackground (g, *this, false);
    drawCaps (g, shownTool == Tool::erase ? "Eraser" : "Brush", titleArea, juce::Justification::centredLeft,
              theme::text.withAlpha (0.82f));
}

void BrushStrip::resized()
{
    const auto h = stripControlHeight;
    const auto y = 1 + (getHeight() - 1 - h) / 2;
    auto x = pad;

    // Wide enough for either name, so the bars keep their place when the tool changes.
    titleArea = { x, 1, juce::jmax (capsWidth ("Brush"), capsWidth ("Eraser")), getHeight() - 1 };
    x = titleArea.getRight() + stripGroupGap;

    for (auto* slider : { &brushSize, &brushSoftness, &brushStrength })
    {
        const auto w = slider->getIdealWidth();
        slider->setBounds (x, y, w, h);
        x += w + stripGroupGap;
    }
}

void BrushStrip::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // (The eraser is the brush taking away: the same three settings.)
    if (const auto tool = state.getTool(); tool != Tool::select && tool != shownTool)
    {
        shownTool = tool;
        repaint (titleArea);
    }
}

} // namespace artfreq::ui::shell
