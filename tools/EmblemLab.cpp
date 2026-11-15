// The emblem lab: renders the emblem (src/ui/Emblem.h) to PNG files to look at.
//
//   FreqArtEmblemLab <output-dir> [--docs <file.png>]
//
// Writes emblem-24.png and emblem-48.png (the bare mark on the header's colour), emblem-128,
// -512 and -1024.png (as an icon tile), header-1x.png and header-2x.png (the header's lockup:
// emblem, wordmark and byline) and zoom.png (the 24 px mark at 1x and 2x, magnified). With
// --docs, also the 1024 px tile to that file.

#include "ui/Emblem.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

using namespace freqart;

namespace
{
    constexpr int headerHeight = 52;    // ui/ShellControls.h
    constexpr int headerPad = 16;
    constexpr int logoSize = 24;        // ui/ShellHeader.cpp

    bool save (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        const auto ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "\n";
        return ok;
    }

    /** The bare mark on the header's colour, size x size logical pixels at the given scale. */
    juce::Image bare (int size, float scale)
    {
        const auto px = juce::roundToInt (static_cast<float> (size) * scale);
        juce::Image image (juce::Image::ARGB, px, px, true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (ui::theme::panel);
        ui::drawEmblem (g, { 0.0f, 0.0f, static_cast<float> (size), static_cast<float> (size) });
        return image;
    }

    /** The mark on a near-black rounded tile, as an icon. */
    juce::Image tile (int size)
    {
        const auto s = static_cast<float> (size);
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        const auto bounds = juce::Rectangle<float> (s, s);
        g.setColour (ui::theme::bg);
        g.fillRoundedRectangle (bounds, s * 0.22f);
        g.setColour (ui::theme::line);
        g.drawRoundedRectangle (bounds.reduced (s * 0.004f), s * 0.22f, juce::jmax (1.0f, s * 0.004f));
        ui::drawEmblem (g, bounds.reduced (s * 0.17f));
        return image;
    }

    /** The header's left end: emblem, "ArtFreq" and "by AnyMeans", laid out as Header::paint does. */
    juce::Image lockup (float scale)
    {
        constexpr int width = 300;
        juce::Image image (juce::Image::ARGB, juce::roundToInt (width * scale),
                           juce::roundToInt (headerHeight * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (ui::theme::panel);
        g.setColour (ui::theme::line);
        g.fillRect (0, headerHeight - 1, width, 1);

        const auto logo = juce::Rectangle<int> (headerPad, headerHeight / 2 - logoSize / 2, logoSize, logoSize);
        ui::drawEmblem (g, logo.toFloat());

        const auto wordmarkFont = fonts::ui (ui::theme::wordmarkSize, fonts::UiWeight::semiBold);
        const auto bylineFont = fonts::ui (ui::theme::labelSize, fonts::UiWeight::medium);
        const auto baseline = headerHeight / 2 + 7;
        auto x = logo.getRight() + 10;

        g.setColour (ui::theme::text);
        g.setFont (wordmarkFont);
        g.drawSingleLineText ("ArtFreq", x, baseline);
        x += juce::roundToInt (juce::GlyphArrangement::getStringWidth (wordmarkFont, "ArtFreq"));

        g.setColour (ui::theme::textDim);
        g.setFont (bylineFont);
        g.drawSingleLineText ("by AnyMeans", x + 8, baseline);
        return image;
    }

    /** The 24 px mark at 1x and 2x, magnified with square pixels. */
    juce::Image zoom()
    {
        constexpr int factor = 12;
        const auto one = bare (logoSize, 1.0f);
        const auto two = bare (logoSize, 2.0f);
        const auto w = logoSize * factor;
        juce::Image image (juce::Image::ARGB, w * 2 + 16, w, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colours::black);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (one, 0, 0, w, w, 0, 0, one.getWidth(), one.getHeight());
        g.drawImage (two, w + 16, 0, w, w, 0, 0, two.getWidth(), two.getHeight());
        return image;
    }
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cerr << "usage: FreqArtEmblemLab <output-dir> [--docs <file.png>]\n";
        return 1;
    }

    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    dir.createDirectory();

    auto ok = save (bare (24, 1.0f), dir.getChildFile ("emblem-24.png"))
            & save (bare (48, 1.0f), dir.getChildFile ("emblem-48.png"))
            & save (tile (128), dir.getChildFile ("emblem-128.png"))
            & save (tile (512), dir.getChildFile ("emblem-512.png"))
            & save (tile (1024), dir.getChildFile ("emblem-1024.png"))
            & save (lockup (1.0f), dir.getChildFile ("header-1x.png"))
            & save (lockup (2.0f), dir.getChildFile ("header-2x.png"))
            & save (zoom(), dir.getChildFile ("zoom.png"));

    for (int i = 2; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--docs")
            ok = ok & save (tile (1024), juce::File::getCurrentWorkingDirectory().getChildFile (argv[i + 1]));

    return ok ? 0 : 1;
}
