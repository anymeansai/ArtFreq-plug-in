#include "ShellHeader.h"

#include "ShellControls.h"

namespace freqart::ui::shell
{
namespace
{
    constexpr int logoSize = 20;
    constexpr int switchWidth = 168;
    constexpr int buttonGap = 4;

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

Header::Header()
    : undoButton ("Undo", icons::undo()),
      redoButton ("Redo", icons::redo()),
      helpButton ("Help", icons::help()),
      settingsButton ("Settings", icons::gear())
{
    setOpaque (true);

    viewSwitch.setSelectedIndex (0, juce::dontSendNotification);
    viewSwitch.setTitle ("View");
    viewSwitch.setComponentID ("view-switch");

    undoButton.setTooltip ("Undo  " + shortcutText ("Cmd+Z"));
    redoButton.setTooltip ("Redo  " + shortcutText ("Cmd+Shift+Z"));
    helpButton.setTooltip ("How FreqArt works");
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

void Header::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    drawHorizontalDivider (g, getHeight() - 1, 0, getWidth());

    drawLogo (g, logoArea);

    // Wordmark and byline share a baseline, centred on the bar by the wordmark's cap height.
    const auto wordmarkFont = fonts::ui (theme::wordmarkSize, fonts::UiWeight::semiBold);
    const auto baseline = getHeight() / 2 + 7;
    const auto wordmarkX = logoArea.getRight() + 10;

    g.setColour (theme::text);
    g.setFont (wordmarkFont);
    g.drawSingleLineText ("FreqArt", wordmarkX, baseline);

    g.setColour (theme::textDim);
    g.setFont (fonts::ui (theme::labelSize, fonts::UiWeight::medium));
    g.drawSingleLineText ("by AnyMeans", wordmarkX + textWidth (wordmarkFont, "FreqArt") + 8, baseline);

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

    // The icons sit inside their buttons: the buttons overhang the margin a little so the icons
    // line up with it optically.
    const auto right = getWidth() - pad + 4;

    settingsButton.setBounds (right - button, buttonY, button, button);
    helpButton.setBounds (settingsButton.getX() - buttonGap - button, buttonY, button, button);

    // A 1 px divider centred in a 23 px gap between the two groups.
    dividerX = helpButton.getX() - 12;
    redoButton.setBounds (dividerX - 11 - button, buttonY, button, button);
    undoButton.setBounds (redoButton.getX() - buttonGap - button, buttonY, button, button);
}

} // namespace freqart::ui::shell
