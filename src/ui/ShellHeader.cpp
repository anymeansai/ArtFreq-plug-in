#include "ShellHeader.h"

#include "Emblem.h"
#include "ShellControls.h"

namespace artfreq::ui::shell
{
namespace
{
    constexpr int logoSize = 24;
    constexpr int switchWidth = 168;
    constexpr int buttonGap = 4;
    constexpr const char* wordmark = "ArtFreq";
    constexpr const char* byline = "by AnyMeans";

    juce::Font wordmarkFont()   { return fonts::ui (theme::wordmarkSize, fonts::UiWeight::semiBold); }
    juce::Font bylineFont()     { return fonts::ui (theme::labelSize, fonts::UiWeight::medium); }

    /** The emblem (Emblem.h): the A and F monogram dissolving into spectral lines. */
    void drawLogo (juce::Graphics& g, juce::Rectangle<int> area)
    {
        drawEmblem (g, area.toFloat());
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
    helpButton.setTooltip ("How ArtFreq works");
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
    const auto baseline = getHeight() / 2 + 7;
    auto x = logoArea.getRight() + 10;

    g.setColour (theme::text);
    g.setFont (wordmarkFont());
    g.drawSingleLineText (wordmark, x, baseline);
    x += textWidth (wordmarkFont(), wordmark);

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

} // namespace artfreq::ui::shell
