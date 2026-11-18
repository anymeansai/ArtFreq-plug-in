// The AnyMeans logo lab: renders the AnyMeans mark (src/brand/AnyMeansMark.h) to PNG files to
// look at.
//
//   AnyMeansLogoLab <output-dir>
//
// Writes:
//   compare-128.png, compare-512.png   the ArtFreq emblem and the AnyMeans mark as icon tiles
//   mark-<size>-dark.png / -light.png  the mark alone at 16, 32, 48, 64, 128, 512 and 1024 px
//   lockup-dark.png / -light.png       the mark and the wordmark "AnyMeans" (Inter SemiBold), 2x
//   lockup-dark-1x.png / -light-1x.png the same at 1x, the size of a website header
//   avatar-dark.png / -light.png       the mark in a circle, as a social profile picture
//   zoom.png                           the 16, 24 and 32 px marks, dark and light, magnified
//   tabs.png                           browser tabs with the mark as a favicon, 1x and 2x

#include "brand/AnyMeansMark.h"
#include "ui/Emblem.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

using namespace artfreq;
using anymeans::brand::Variant;

namespace
{
    const juce::Colour darkGround { 0xff0b0d12 };   // ArtFreq's background
    const juce::Colour lightGround { 0xffffffff };
    const juce::Colour darkText { 0xffe8ebf2 };     // ArtFreq's text colour
    const juce::Colour lightText { 0xff0b0d12 };

    juce::Colour ground (Variant v) { return v == Variant::onDark ? darkGround : lightGround; }
    juce::Colour wordmarkColour (Variant v) { return v == Variant::onDark ? darkText : lightText; }
    const char* suffix (Variant v) { return v == Variant::onDark ? "dark" : "light"; }

    bool save (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        const auto ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "\n";
        return ok;
    }

    /** The mark alone, centred on a size x size square of the ground colour. Small sizes fill
        the square (favicons); larger ones keep a margin. */
    juce::Image markAlone (int size, Variant variant)
    {
        const auto s = static_cast<float> (size);
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        g.fillAll (ground (variant));
        const auto margin = size <= 32 ? 0.0f : (size <= 64 ? 0.06f * s : 0.12f * s);
        anymeans::brand::drawAnyMeansMark (g, juce::Rectangle<float> (s, s).reduced (margin), variant);
        return image;
    }

    /** A mark on a near-black rounded tile, as an icon (as ArtFreqEmblemLab draws the emblem). */
    void drawTile (juce::Graphics& g, juce::Rectangle<float> bounds, bool anyMeans)
    {
        const auto s = bounds.getWidth();
        g.setColour (ui::theme::bg);
        g.fillRoundedRectangle (bounds, s * 0.22f);
        g.setColour (ui::theme::line);
        g.drawRoundedRectangle (bounds.reduced (s * 0.004f), s * 0.22f, juce::jmax (1.0f, s * 0.004f));

        if (anyMeans)
            anymeans::brand::drawAnyMeansMark (g, bounds.reduced (s * 0.15f), Variant::onDark);
        else
            ui::drawEmblem (g, bounds.reduced (s * 0.17f));
    }

    /** The ArtFreq emblem and the AnyMeans mark side by side, as tiles of the given size. */
    juce::Image compare (int size)
    {
        const auto pad = juce::jmax (16, size / 8);
        juce::Image image (juce::Image::ARGB, size * 2 + pad * 3, size + pad * 2, true);
        juce::Graphics g (image);
        g.fillAll (ui::theme::panel);
        const auto s = static_cast<float> (size);
        drawTile (g, { static_cast<float> (pad), static_cast<float> (pad), s, s }, false);
        drawTile (g, { static_cast<float> (pad * 2 + size), static_cast<float> (pad), s, s }, true);
        return image;
    }

    /** The mark and "AnyMeans" in Inter SemiBold. The mark's letters are 1.65 x the wordmark's
        cap height (a little more than the ArtFreq header's emblem is to "ArtFreq", as the
        company mark leads) and centred on it. */
    juce::Image lockup (Variant variant, float scale)
    {
        constexpr float wordmarkSize = 30.0f;
        constexpr float height = 64.0f;
        constexpr float pad = 20.0f;

        const auto font = fonts::ui (wordmarkSize, fonts::UiWeight::semiBold);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, "AnyMeans", 0.0f, 0.0f);
        juce::Path text;
        glyphs.createPath (text);
        juce::Path capA;
        glyphs.getGlyph (0).createPath (capA);
        const auto capHeight = -capA.getBounds().getY();
        const auto textBounds = text.getBounds();

        const auto markHeight = std::round (capHeight * 1.65f);
        const auto markWidth = markHeight * anymeans::brand::markAspect();
        const auto gap = std::round (markHeight * 0.42f);
        const auto width = pad + markWidth + gap + textBounds.getRight() + pad;

        juce::Image image (juce::Image::ARGB, juce::roundToInt (width * scale), juce::roundToInt (height * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.fillAll (ground (variant));

        const auto markArea = juce::Rectangle<float> (pad, (height - markHeight) * 0.5f, markWidth, markHeight);
        anymeans::brand::drawAnyMeansMark (g, markArea, variant);
        const auto drawn = anymeans::brand::markBounds (g, markArea);

        // The wordmark's caps centred on the mark's letters, its left edge a gap after them.
        const auto baseline = drawn.getCentreY() + capHeight * 0.5f;
        g.setColour (wordmarkColour (variant));
        g.fillPath (text, juce::AffineTransform::translation (drawn.getRight() + gap - textBounds.getX(), baseline));
        return image;
    }

    /** The mark on a circle of the ground colour (a social profile picture), on a mid grey so
        the circle shows. */
    juce::Image avatar (Variant variant)
    {
        constexpr int size = 400;
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff6b7080));
        const auto circle = juce::Rectangle<float> (static_cast<float> (size), static_cast<float> (size));
        g.setColour (ground (variant));
        g.fillEllipse (circle);
        anymeans::brand::drawAnyMeansMark (g, circle.reduced (size * 0.2f), variant);
        return image;
    }

    /** Browser tabs at 1x and 2x, dark and light, each with the 16 px mark as its favicon
        (drawn at 16 and 32 physical pixels) and the site's name. */
    juce::Image tabs()
    {
        constexpr int tabWidth = 200, tabHeight = 32;
        juce::Image image (juce::Image::ARGB, tabWidth * 4 + 16, tabHeight * 3 + 8, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff6b7080));

        auto drawTab = [&] (int x, int y, float scale, Variant variant)
        {
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::scale (scale).translated (static_cast<float> (x), static_cast<float> (y)));
            const auto tab = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (tabWidth), static_cast<float> (tabHeight));
            g.setColour (variant == Variant::onDark ? juce::Colour (0xff35363a) : juce::Colour (0xffffffff));
            g.fillRect (tab);
            anymeans::brand::drawAnyMeansMark (g, { 10.0f, 8.0f, 16.0f, 16.0f }, variant);
            g.setColour (variant == Variant::onDark ? juce::Colour (0xffe8eaed) : juce::Colour (0xff202124));
            g.setFont (fonts::ui (12.0f));
            g.drawSingleLineText ("AnyMeans", 34, 20);
        };

        drawTab (0, 0, 1.0f, Variant::onDark);
        drawTab (tabWidth + 8, 0, 1.0f, Variant::onLight);
        drawTab (0, tabHeight + 8, 2.0f, Variant::onDark);
        drawTab (tabWidth * 2 + 16, tabHeight + 8, 2.0f, Variant::onLight);
        return image;
    }

    /** The 16, 24 and 32 px marks on dark (top) and on light (bottom), magnified with square
        pixels. */
    juce::Image zoom()
    {
        constexpr int factor = 10;
        constexpr int gap = 16;
        constexpr int sizes[] = { 16, 24, 32 };
        const auto w = 32 * factor;
        juce::Image image (juce::Image::ARGB, (w + gap) * 3 + gap, (w + gap) * 2 + gap, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff6b7080));
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);

        for (int row = 0; row < 2; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                const auto size = sizes[column];
                const auto mark = markAlone (size, row == 0 ? Variant::onDark : Variant::onLight);
                g.drawImage (mark, gap + column * (w + gap), gap + row * (w + gap), size * factor, size * factor,
                             0, 0, size, size);
            }
        }
        return image;
    }
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 2)
    {
        std::cerr << "usage: AnyMeansLogoLab <output-dir>\n";
        return 1;
    }

    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    dir.createDirectory();

    auto ok = save (compare (128), dir.getChildFile ("compare-128.png"))
            & save (compare (512), dir.getChildFile ("compare-512.png"));

    for (auto variant : { Variant::onDark, Variant::onLight })
    {
        for (auto size : { 16, 32, 48, 64, 128, 512, 1024 })
            ok = ok & save (markAlone (size, variant),
                            dir.getChildFile ("mark-" + juce::String (size) + "-" + suffix (variant) + ".png"));

        ok = ok & save (lockup (variant, 2.0f), dir.getChildFile (juce::String ("lockup-") + suffix (variant) + ".png"))
                & save (lockup (variant, 1.0f), dir.getChildFile (juce::String ("lockup-") + suffix (variant) + "-1x.png"))
                & save (avatar (variant), dir.getChildFile (juce::String ("avatar-") + suffix (variant) + ".png"));
    }

    ok = ok & save (zoom(), dir.getChildFile ("zoom.png"))
            & save (tabs(), dir.getChildFile ("tabs.png"));
    return ok ? 0 : 1;
}
