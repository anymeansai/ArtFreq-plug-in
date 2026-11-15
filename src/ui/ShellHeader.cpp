#include "ShellHeader.h"

#include "ShellControls.h"

namespace artfreq::ui::shell
{
namespace
{
    constexpr int logoSize = 20;
    constexpr int switchWidth = 168;
    constexpr int buttonGap = 4;
    constexpr int effectGap = 24;   // ArtFreq FX: around Canvas | Live and the effect's controls
    constexpr const char* wordmark = "ArtFreq";
    constexpr const char* effectMark = "FX";
    constexpr const char* byline = "by AnyMeans";

    juce::Font wordmarkFont()   { return fonts::ui (theme::wordmarkSize, fonts::UiWeight::semiBold); }
    juce::Font bylineFont()     { return fonts::ui (theme::labelSize, fonts::UiWeight::medium); }

    /** The mark: five scan lines, coloured from the bottom of the spectrogram colour map to the
        top, their lengths tracing a shape the way FreqArt's lines do in an analyser. */
    void drawLogo (juce::Graphics& g, juce::Rectangle<int> area)
    {
        static constexpr float lengths[] = { 0.55f, 0.85f, 1.0f, 0.7f, 0.4f };   // top to bottom
        static constexpr float levels[] = { 0.92f, 0.78f, 0.64f, 0.5f, 0.36f };

        const auto x = static_cast<float> (area.getX());
        const auto width = static_cast<float> (area.getWidth());

        for (int i = 0; i < 5; ++i)
        {
            const auto y = static_cast<float> (area.getY() + 1 + i * 4);
            g.setColour (theme::spectrum (levels[i]));
            g.fillRoundedRectangle (x, y, std::round (width * lengths[i]), 2.0f, 1.0f);
        }
    }
} // namespace

juce::String shortcutText (const juce::String& keys)
{
   #if JUCE_MAC
    return keys.replace ("Cmd+", juce::String::fromUTF8 ("\xe2\x8c\x98"))
               .replace ("Shift+", juce::String::fromUTF8 ("\xe2\x87\xa7"));
   #else
    return keys.replace ("Cmd+", "Ctrl+");
   #endif
}

Header::Header (bool forEffect)
    : undoButton ("Undo", icons::undo()),
      redoButton ("Redo", icons::redo()),
      helpButton ("Help", icons::help()),
      settingsButton ("Settings", icons::gear()),
      effect (forEffect)
{
    setOpaque (true);

    viewSwitch.setSelectedIndex (0, juce::dontSendNotification);
    viewSwitch.setTitle ("View");
    viewSwitch.setComponentID ("view-switch");

    undoButton.setTooltip ("Undo  " + shortcutText ("Cmd+Z"));
    redoButton.setTooltip ("Redo  " + shortcutText ("Cmd+Shift+Z"));
    helpButton.setTooltip (effect ? "How ArtFreq FX works" : "How ArtFreq works");
    settingsButton.setTooltip ("Settings: match your analyser, window size");

    undoButton.setComponentID ("undo");
    redoButton.setComponentID ("redo");
    helpButton.setComponentID ("help");
    settingsButton.setComponentID ("settings");

    for (auto* button : { &undoButton, &redoButton, &helpButton, &settingsButton })
    {
        button->setFlat (true);
        addAndMakeVisible (*button);
    }

    addAndMakeVisible (viewSwitch);
}

void Header::setEffectControls (juce::Component* controls, int idealWidth)
{
    if (effectControls != nullptr)
        removeChildComponent (effectControls);

    effectControls = controls;
    effectControlsWidth = idealWidth;

    if (effectControls != nullptr)
        addAndMakeVisible (effectControls);

    resized();
}

int Header::bylineRight() const
{
    // Where paint() ends the byline.
    auto x = logoArea.getRight() + 10 + textWidth (wordmarkFont(), wordmark);

    if (effect)
        x += 5 + textWidth (wordmarkFont(), effectMark);

    return x + 8 + textWidth (bylineFont(), byline);
}

void Header::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    drawHorizontalDivider (g, getHeight() - 1, 0, getWidth());

    drawLogo (g, logoArea);

    // Wordmark and byline share a baseline, centred on the bar by the wordmark's cap height.
    // The effect's "FX" is in the playing colour: the same plug-in, the other way round.
    const auto baseline = getHeight() / 2 + 7;
    auto x = logoArea.getRight() + 10;

    g.setColour (theme::text);
    g.setFont (wordmarkFont());
    g.drawSingleLineText (wordmark, x, baseline);
    x += textWidth (wordmarkFont(), wordmark);

    if (effect)
    {
        x += 5;
        g.setColour (theme::accent2);
        g.drawSingleLineText (effectMark, x, baseline);
        x += textWidth (wordmarkFont(), effectMark);
    }

    g.setColour (theme::textDim);
    g.setFont (bylineFont());
    g.drawSingleLineText (byline, x + 8, baseline);

    drawVerticalDivider (g, dividerX, getHeight() / 2 - 10, getHeight() / 2 + 10);
}

void Header::resized()
{
    const auto pad = metrics::pad;
    const auto centreY = getHeight() / 2;
    const auto button = metrics::headerButton;
    const auto buttonY = centreY - button / 2;

    logoArea = { pad, centreY - logoSize / 2, logoSize, logoSize };

    viewSwitch.setBounds ((getWidth() - switchWidth) / 2, centreY - metrics::controlHeight / 2,
                          switchWidth, metrics::controlHeight);

    // ArtFreq FX: Canvas | Live beside the wordmark, the effect's controls after it (laid out
    // below, once the buttons they end before are placed).
    if (effectControls != nullptr)
        viewSwitch.setTopLeftPosition (bylineRight() + effectGap, viewSwitch.getY());

    // The icons sit inside their buttons: the buttons overhang the margin a little so the icons
    // line up with it optically.
    const auto right = getWidth() - pad + 4;

    settingsButton.setBounds (right - button, buttonY, button, button);
    helpButton.setBounds (settingsButton.getX() - buttonGap - button, buttonY, button, button);

    // A 1 px divider centred in a 23 px gap between the two groups.
    dividerX = helpButton.getX() - 12;
    redoButton.setBounds (dividerX - 11 - button, buttonY, button, button);
    undoButton.setBounds (redoButton.getX() - buttonGap - button, buttonY, button, button);

    if (effectControls != nullptr)
    {
        // Centred between Canvas | Live and undo, as far as it fits.
        const auto from = viewSwitch.getRight() + effectGap;
        const auto to = undoButton.getX() - effectGap;
        const auto x = juce::jmax (from, (from + to - effectControlsWidth) / 2);
        effectControls->setBounds (x, 0, juce::jmin (effectControlsWidth, to - x), getHeight());
    }
}

} // namespace artfreq::ui::shell
