// The AnyMeans logo lab: renders the AnyMeans mark (src/brand/AnyMeansMark.h), exports the
// brand files and draws the overview.
//
//   AnyMeansLogoLab review <dir>                 sheets to look at while designing
//   AnyMeansLogoLab brand <dir>                  the brand files (docs/brand)
//   AnyMeansLogoLab overview <file.png>          one page: the mark, lockups, small sizes, the AF emblem
//   AnyMeansLogoLab svg <in.svg> <out.png> <w>   draws an SVG with JUCE's own parser, to check it
//   AnyMeansLogoLab mark-svg <dark|light|mono> <lines> <out.svg>
//                                                the mark as SVG with another line count (the
//                                                brand files have 14), e.g. for print
//
// review writes:
//   compare-128.png, compare-512.png   the ArtFreq emblem and the AnyMeans icon side by side
//   mark-<size>-dark.png / -light.png  the mark alone at 16 to 1024 px, on dark and on white
//   mono.png                           the one-colour marks on a colour and on paper
//   lockup-dark.png / -light.png       the lockups on their grounds, and -1x at header size
//   avatar.png                         the icon cropped to a circle, large and small
//   zoom.png                           16, 24, 32 px marks and 16, 32 px favicons, magnified
//   tabs.png                           browser tabs with the favicon, 1x and 2x

#include "brand/AnyMeansMark.h"
#include "core/Fonts.h"
#include "ui/Emblem.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <iostream>

using namespace artfreq;
using anymeans::brand::Variant;
namespace brand = anymeans::brand;
namespace bc = anymeans::brand::colours;

namespace
{
    // The vector files' line count: crisp from about 24 px of cap height (a website header)
    // and bold, not busy, at poster size.
    constexpr int svgLines = 14;

    // The lockup: the mark's letters are this many times the wordmark's cap height, the gap
    // between them this share of the mark's height, and the clear space around it this share.
    constexpr float markToCap = 1.7f;
    constexpr float lockupGap = 0.4f;
    constexpr float lockupClear = 0.25f;

    bool isDark (Variant v) { return v == Variant::onDark || v == Variant::whiteOnly; }
    juce::Colour ground (Variant v) { return isDark (v) ? bc::ink : bc::paper; }
    juce::Colour wordmarkColour (Variant v) { return isDark (v) ? bc::wordOnDark : bc::ink; }
    const char* suffix (Variant v) { return isDark (v) ? "dark" : "light"; }

    bool save (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        const auto ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << " (" << image.getWidth()
                  << " x " << image.getHeight() << ")\n";
        return ok;
    }

    bool saveText (const juce::String& text, const juce::File& file)
    {
        const auto ok = file.replaceWithText (text, false, false, "\n");
        std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName() << "\n";
        return ok;
    }

    juce::Rectangle<float> boxOf (juce::Image& image)
    {
        return image.getBounds().toFloat();
    }

    // ---------------------------------------------------------------------------------------
    // The pieces

    /** The mark alone, centred on a size x size square (transparent, or the variant's ground),
        its letters markShare of the square's width. */
    juce::Image markSquare (int size, Variant variant, bool transparent, float markShare)
    {
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        if (! transparent)
            g.fillAll (ground (variant));
        const auto w = static_cast<float> (size) * markShare;
        brand::drawAnyMeansMark (g, juce::Rectangle<float> (w, w / brand::markAspect()).withCentre (boxOf (image).getCentre()),
                                 variant);
        return image;
    }

    /** The mark on a near-black rounded square: the app icon and social avatar, and (with
        less margin, as every pixel counts) the favicon. The mark sits a little right of centre
        because its solid A is heavier than its lined M. */
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> tile, bool favicon)
    {
        const auto s = tile.getWidth();
        g.setColour (bc::ink);
        g.fillRoundedRectangle (tile, s * (favicon ? 0.2f : 0.22f));

        const auto w = s * (favicon ? 0.86f : 0.66f);
        const auto nudge = favicon ? 0.0f : s * 0.012f;
        brand::drawAnyMeansMark (g, juce::Rectangle<float> (w, w / brand::markAspect())
                                        .withCentre (tile.getCentre().translated (nudge, 0.0f)),
                                 Variant::onDark);
    }

    juce::Image icon (int size, bool favicon)
    {
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        drawIcon (g, boxOf (image), favicon);
        return image;
    }

    /** The wordmark "AnyMeans" in Inter SemiBold (the ArtFreq header's face), a touch tighter
        than its text spacing as display sizes want, outlined, its baseline at y = 0 and its
        left edge at x = 0, scaled so its capitals are capHeight tall. */
    juce::Path wordmark (float capHeight)
    {
        const auto font = fonts::ui (100.0f, fonts::UiWeight::semiBold).withExtraKerningFactor (-0.012f);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, "AnyMeans", 0.0f, 0.0f);
        juce::Path text;
        glyphs.createPath (text);
        juce::Path capA;
        glyphs.getGlyph (0).createPath (capA);

        const auto k = capHeight / -capA.getBounds().getY();
        text.applyTransform (juce::AffineTransform::scale (k));
        text.applyTransform (juce::AffineTransform::translation (-text.getBounds().getX(), 0.0f));
        return text;
    }

    /** The lockup's parts for a wordmark cap height: the box the mark is drawn into (at the
        clear space's corner) and the wordmark placed beside the letters actually drawn, its
        capitals centred on them. */
    struct Lockup
    {
        juce::Rectangle<float> markBox;
        juce::Path text;
        float width, height;
    };

    Lockup makeLockup (float capHeight, std::function<juce::Rectangle<float> (juce::Rectangle<float>)> drawnBounds)
    {
        const auto markHeight = capHeight * markToCap;
        const auto clear = std::round (markHeight * lockupClear);

        Lockup lockup;
        lockup.markBox = { clear, clear, markHeight * brand::markAspect(), markHeight };
        const auto drawn = drawnBounds (lockup.markBox);

        lockup.text = wordmark (capHeight);
        const auto textWidth = lockup.text.getBounds().getRight();
        lockup.text.applyTransform (juce::AffineTransform::translation (drawn.getRight() + markHeight * lockupGap,
                                                                        drawn.getCentreY() + capHeight * 0.5f));
        lockup.width = std::ceil (drawn.getRight() + markHeight * lockupGap + textWidth + clear);
        lockup.height = std::ceil (markHeight + 2.0f * clear);
        return lockup;
    }

    /** The lockup as a PNG, transparent or on its ground, about width pixels wide. */
    juce::Image lockupImage (Variant variant, float width, bool transparent)
    {
        // The proportions do not depend on size: measure at cap height 100, then scale.
        const auto probe = makeLockup (100.0f, [] (auto box) { return box; });
        const auto capHeight = std::floor (100.0f * width / probe.width);

        juce::Image scratch (juce::Image::ARGB, 1, 1, true);
        juce::Graphics measure (scratch);
        const auto lockup = makeLockup (capHeight, [&] (auto box) { return brand::markBounds (measure, box); });

        juce::Image image (juce::Image::ARGB, juce::roundToInt (lockup.width), juce::roundToInt (lockup.height), true);
        juce::Graphics g (image);
        if (! transparent)
            g.fillAll (ground (variant));
        brand::drawAnyMeansMark (g, lockup.markBox, variant);
        g.setColour (wordmarkColour (variant));
        g.fillPath (lockup.text);
        return image;
    }

    /** The lockup as an SVG file, the wordmark outlined (no font needed). */
    juce::String lockupSvg (Variant variant)
    {
        juce::String defs, body;
        const auto lockup = makeLockup (100.0f, [&] (auto box)
        {
            const auto mark = brand::markSvg (box, variant, svgLines, juce::String ("am-lockup-") + suffix (variant) + "-");
            defs = mark.defs;
            body = mark.body;
            return mark.bounds;
        });

        juce::String s;
        s << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << lockup.width << " " << lockup.height
          << "\" width=\"" << lockup.width << "\" height=\"" << lockup.height << "\">\n"
          << "<title>AnyMeans</title>\n"
          << "<defs>\n" << defs << "</defs>\n"
          << body
          << "<path fill=\"" << brand::svgColour (wordmarkColour (variant)) << "\" d=\""
          << brand::svgPathData (lockup.text) << "\"/>\n"
          << "</svg>\n";
        return s;
    }

    /** An image drawn magnified with square pixels. */
    void drawMagnified (juce::Graphics& g, const juce::Image& image, int x, int y, int factor)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (image, x, y, image.getWidth() * factor, image.getHeight() * factor,
                     0, 0, image.getWidth(), image.getHeight());
    }

    /** The ArtFreq emblem on its icon tile, as ArtFreqEmblemLab draws it. */
    void drawEmblemTile (juce::Graphics& g, juce::Rectangle<float> tile)
    {
        const auto s = tile.getWidth();
        g.setColour (ui::theme::bg);
        g.fillRoundedRectangle (tile, s * 0.22f);
        ui::drawEmblem (g, tile.reduced (s * 0.17f));
    }

    /** A browser tab with the favicon and the site's name, at the given scale. */
    void drawTab (juce::Graphics& g, juce::Rectangle<float> tab, float scale, bool dark)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (scale).translated (tab.getX(), tab.getY()));
        g.setColour (dark ? juce::Colour (0xff35363a) : juce::Colour (0xffffffff));
        g.fillRoundedRectangle (0.0f, 0.0f, tab.getWidth() / scale, tab.getHeight() / scale, 8.0f);
        drawIcon (g, { 10.0f, 8.0f, 16.0f, 16.0f }, true);
        g.setColour (dark ? juce::Colour (0xffe8eaed) : juce::Colour (0xff202124));
        g.setFont (fonts::ui (12.0f));
        g.drawSingleLineText ("AnyMeans", 34, 20);
    }

    // ---------------------------------------------------------------------------------------
    // review

    juce::Image compare (int size)
    {
        const auto pad = juce::jmax (16, size / 8);
        juce::Image image (juce::Image::ARGB, size * 2 + pad * 3, size + pad * 2, true);
        juce::Graphics g (image);
        g.fillAll (ui::theme::panel);
        const auto s = static_cast<float> (size);
        drawEmblemTile (g, { static_cast<float> (pad), static_cast<float> (pad), s, s });
        drawIcon (g, { static_cast<float> (pad * 2 + size), static_cast<float> (pad), s, s }, false);
        return image;
    }

    juce::Image monoSheet()
    {
        juce::Image image (juce::Image::ARGB, 1000, 420, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff3a3d48));
        g.setColour (juce::Colour (0xff7c6cff));
        g.fillRect (20, 20, 470, 380);
        g.setColour (juce::Colour (0xffe6e1d8));
        g.fillRect (510, 20, 470, 380);
        brand::drawAnyMeansMark (g, { 75, 80, 360, 260 }, Variant::whiteOnly);
        brand::drawAnyMeansMark (g, { 565, 80, 360, 260 }, Variant::inkOnly);
        return image;
    }

    juce::Image avatarSheet()
    {
        juce::Image image (juce::Image::ARGB, 640, 420, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xffe9eaee));
        const auto big = icon (400, false);
        const auto small = icon (48, false);

        auto circle = [&] (const juce::Image& source, float x, float y, float d)
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path p;
            p.addEllipse (x, y, d, d);
            g.reduceClipRegion (p);
            g.drawImage (source, { x, y, d, d });
        };

        circle (big, 10, 10, 400);
        circle (small, 440, 10, 48);
        circle (icon (96, false), 440, 80, 96);
        return image;
    }

    juce::Image zoomSheet()
    {
        constexpr int factor = 10;
        constexpr int gap = 16;
        const auto cell = 32 * factor + gap;
        juce::Image image (juce::Image::ARGB, cell * 5 + gap, cell * 2 + gap, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff6b7080));

        int column = 0;
        for (auto size : { 16, 24, 32 })
        {
            drawMagnified (g, markSquare (size, Variant::onDark, false, 1.0f), gap + column * cell, gap, factor);
            drawMagnified (g, markSquare (size, Variant::onLight, false, 1.0f), gap + column * cell, gap + cell, factor);
            ++column;
        }

        drawMagnified (g, icon (16, true), gap + 3 * cell, gap, factor);
        drawMagnified (g, icon (32, true), gap + 4 * cell, gap, factor);
        drawMagnified (g, icon (32, true), gap + 3 * cell, gap + cell, factor / 2);
        drawMagnified (g, icon (64, true), gap + 4 * cell, gap + cell, factor / 2);
        return image;
    }

    juce::Image tabsSheet()
    {
        juce::Image image (juce::Image::ARGB, 2 * 200 + 3 * 8 + 2 * 400, 32 * 2 + 3 * 8 + 32, true);
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff6b7080));
        drawTab (g, { 8, 8, 200, 32 }, 1.0f, true);
        drawTab (g, { 216, 8, 200, 32 }, 1.0f, false);
        drawTab (g, { 8, 48, 400, 64 }, 2.0f, true);
        drawTab (g, { 416, 48, 400, 64 }, 2.0f, false);
        return image;
    }

    bool review (const juce::File& dir)
    {
        auto ok = save (compare (128), dir.getChildFile ("compare-128.png"))
                & save (compare (512), dir.getChildFile ("compare-512.png"))
                & save (monoSheet(), dir.getChildFile ("mono.png"))
                & save (avatarSheet(), dir.getChildFile ("avatar.png"))
                & save (zoomSheet(), dir.getChildFile ("zoom.png"))
                & save (tabsSheet(), dir.getChildFile ("tabs.png"));

        for (auto variant : { Variant::onDark, Variant::onLight })
        {
            for (auto size : { 16, 32, 48, 64, 128, 512, 1024 })
                ok = ok & save (markSquare (size, variant, false, size <= 32 ? 1.0f : 0.8f),
                                dir.getChildFile ("mark-" + juce::String (size) + "-" + suffix (variant) + ".png"));

            ok = ok & save (lockupImage (variant, 1100.0f, false), dir.getChildFile (juce::String ("lockup-") + suffix (variant) + ".png"))
                    & save (lockupImage (variant, 260.0f, false), dir.getChildFile (juce::String ("lockup-") + suffix (variant) + "-1x.png"));
        }

        return ok;
    }

    // ---------------------------------------------------------------------------------------
    // brand

    /** The mark's SVG file for dark, light or mono (one colour, the page's text colour). */
    juce::String markSvgFile (const juce::String& kind, int lines)
    {
        if (kind == "mono")
            return brand::markSvgFile (Variant::inkOnly, lines).replace (brand::svgColour (bc::ink), "currentColor");

        return brand::markSvgFile (kind == "light" ? Variant::onLight : Variant::onDark, lines);
    }

    bool exportBrand (const juce::File& dir)
    {
        const auto mono = markSvgFile ("mono", svgLines);

        return saveText (brand::markSvgFile (Variant::onDark, svgLines), dir.getChildFile ("anymeans-mark.svg"))
             & saveText (brand::markSvgFile (Variant::onLight, svgLines), dir.getChildFile ("anymeans-mark-light.svg"))
             & saveText (mono, dir.getChildFile ("anymeans-mark-mono.svg"))
             & saveText (lockupSvg (Variant::onDark), dir.getChildFile ("anymeans-lockup-dark.svg"))
             & saveText (lockupSvg (Variant::onLight), dir.getChildFile ("anymeans-lockup-light.svg"))
             & save (markSquare (1024, Variant::onDark, true, 0.8f), dir.getChildFile ("anymeans-mark-1024.png"))
             & save (markSquare (512, Variant::onDark, true, 0.8f), dir.getChildFile ("anymeans-mark-512.png"))
             & save (markSquare (1024, Variant::onLight, true, 0.8f), dir.getChildFile ("anymeans-mark-light-1024.png"))
             & save (markSquare (512, Variant::onLight, true, 0.8f), dir.getChildFile ("anymeans-mark-light-512.png"))
             & save (icon (1024, false), dir.getChildFile ("anymeans-icon-1024.png"))
             & save (icon (16, true), dir.getChildFile ("favicon-16.png"))
             & save (icon (32, true), dir.getChildFile ("favicon-32.png"))
             & save (icon (64, true), dir.getChildFile ("favicon-64.png"))
             & save (lockupImage (Variant::onDark, 2000.0f, true), dir.getChildFile ("anymeans-lockup-dark.png"))
             & save (lockupImage (Variant::onLight, 2000.0f, true), dir.getChildFile ("anymeans-lockup-light.png"));
    }

    // ---------------------------------------------------------------------------------------
    // overview

    void caption (juce::Graphics& g, const juce::String& text, float x, float y, juce::Colour colour)
    {
        g.setColour (colour);
        g.setFont (fonts::ui (22.0f, fonts::UiWeight::medium).withExtraKerningFactor (0.04f));
        g.drawSingleLineText (text.toUpperCase(), juce::roundToInt (x), juce::roundToInt (y));
    }

    juce::Image overview()
    {
        constexpr int W = 2400, H = 2020;
        constexpr float pad = 60.0f, gutter = 40.0f;
        const auto half = (W - 2.0f * pad - gutter) * 0.5f;
        const auto dim = juce::Colour (0xff8c94a6);
        const auto dimOnLight = juce::Colour (0xff6b7080);
        const auto board = juce::Colour (0xff16181f);

        juce::Image image (juce::Image::ARGB, W, H, true);
        juce::Graphics g (image);
        g.fillAll (board);

        // Title.
        g.setColour (bc::wordOnDark);
        g.setFont (fonts::ui (40.0f, fonts::UiWeight::semiBold));
        g.drawSingleLineText (juce::String::fromUTF8 ("AnyMeans \xe2\x80\x94 company mark"), static_cast<int> (pad), 92);
        g.setColour (dim);
        g.setFont (fonts::ui (24.0f));
        g.drawSingleLineText (juce::String::fromUTF8 ("The A and M share one stem, as ArtFreq\xe2\x80\x99s A and F do. Solid on the left, it turns into spectral lines on the right."),
                              static_cast<int> (pad), 134);

        // Row 1: the mark large, on dark and on light.
        const auto top1 = 180.0f, h1 = 760.0f;
        const juce::Rectangle<float> darkPanel (pad, top1, half, h1), lightPanel (pad + half + gutter, top1, half, h1);
        g.setColour (bc::ink);
        g.fillRoundedRectangle (darkPanel, 24.0f);
        g.setColour (bc::paper);
        g.fillRoundedRectangle (lightPanel, 24.0f);
        brand::drawAnyMeansMark (g, darkPanel.reduced (190.0f, 150.0f), Variant::onDark);
        brand::drawAnyMeansMark (g, lightPanel.reduced (190.0f, 150.0f), Variant::onLight);
        caption (g, "On dark", darkPanel.getX() + 36, darkPanel.getBottom() - 36, dim);
        caption (g, "On light", lightPanel.getX() + 36, lightPanel.getBottom() - 36, dimOnLight);

        // Row 2: the lockups.
        const auto top2 = top1 + h1 + gutter, h2 = 300.0f;
        const juce::Rectangle<float> darkLock (pad, top2, half, h2), lightLock (pad + half + gutter, top2, half, h2);
        g.setColour (bc::ink);
        g.fillRoundedRectangle (darkLock, 24.0f);
        g.setColour (bc::paper);
        g.fillRoundedRectangle (lightLock, 24.0f);
        for (auto [panel, variant] : { std::pair { darkLock, Variant::onDark }, std::pair { lightLock, Variant::onLight } })
        {
            const auto lockup = lockupImage (variant, 860.0f, true);
            g.drawImageAt (lockup, juce::roundToInt (panel.getCentreX() - lockup.getWidth() * 0.5f),
                           juce::roundToInt (panel.getCentreY() - lockup.getHeight() * 0.5f - 10.0f));
        }
        caption (g, "Lockup", darkLock.getX() + 36, darkLock.getBottom() - 30, dim);
        caption (g, "Lockup", lightLock.getX() + 36, lightLock.getBottom() - 30, dimOnLight);

        // Row 3, left: the family, the ArtFreq emblem beside the AnyMeans icon.
        const auto top3 = top2 + h2 + gutter, h3 = 640.0f;
        const juce::Rectangle<float> family (pad, top3, half, h3), small (pad + half + gutter, top3, half, h3);
        g.setColour (ui::theme::panel);
        g.fillRoundedRectangle (family, 24.0f);
        g.fillRoundedRectangle (small, 24.0f);
        g.setColour (ui::theme::line);
        g.drawRoundedRectangle (family.reduced (1.0f), 24.0f, 2.0f);
        g.drawRoundedRectangle (small.reduced (1.0f), 24.0f, 2.0f);

        const auto tile = 400.0f;
        const auto tilesLeft = family.getCentreX() - tile - 20.0f;
        drawEmblemTile (g, { tilesLeft, family.getY() + 70.0f, tile, tile });
        drawIcon (g, { tilesLeft + tile + 40.0f, family.getY() + 70.0f, tile, tile }, false);
        g.setColour (dim);
        g.setFont (fonts::ui (24.0f));
        g.drawSingleLineText ("ArtFreq", juce::roundToInt (tilesLeft), juce::roundToInt (family.getY() + 70.0f + tile + 44.0f));
        g.drawSingleLineText ("AnyMeans (app icon, social avatar)", juce::roundToInt (tilesLeft + tile + 40.0f),
                              juce::roundToInt (family.getY() + 70.0f + tile + 44.0f));
        caption (g, "One family", family.getX() + 36, family.getBottom() - 30, dim);

        // Row 3, right: small sizes at their real pixels, and the favicon magnified.
        auto x = small.getX() + 50.0f;
        auto y = small.getY() + 60.0f;
        drawTab (g, { x, y, 200, 32 }, 1.0f, true);
        drawTab (g, { x + 220, y, 200, 32 }, 1.0f, false);
        drawTab (g, { x + 440, y, 400, 64 }, 2.0f, false);

        y += 110.0f;
        x = small.getX() + 50.0f;
        g.setFont (fonts::ui (20.0f));
        for (auto size : { 16, 24, 32, 48, 64 })
        {
            const auto dark = markSquare (size, Variant::onDark, true, 1.0f);
            const auto light = markSquare (size, Variant::onLight, true, 1.0f);
            g.setColour (bc::ink);
            g.fillRect (x - 8, y - 8, size + 16.0f, size + 16.0f);
            g.drawImageAt (dark, juce::roundToInt (x), juce::roundToInt (y));
            g.setColour (bc::paper);
            g.fillRect (x - 8, y + 88, size + 16.0f, size + 16.0f);
            g.drawImageAt (light, juce::roundToInt (x), juce::roundToInt (y + 96));
            g.setColour (dim);
            g.drawSingleLineText (juce::String (size) + " px", juce::roundToInt (x - 6), juce::roundToInt (y + 210));
            x += static_cast<float> (size) + 54.0f;
        }

        // Social avatars, cropped to circles.
        auto circle = [&] (int d, float cx, float cy)
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path p;
            p.addEllipse (cx, cy, static_cast<float> (d), static_cast<float> (d));
            g.reduceClipRegion (p);
            g.drawImageAt (icon (d, false), juce::roundToInt (cx), juce::roundToInt (cy));
        };
        circle (120, x + 20.0f, y - 8.0f);
        circle (48, x + 160.0f, y + 64.0f);
        g.setColour (dim);
        g.drawSingleLineText ("avatar", juce::roundToInt (x + 20.0f), juce::roundToInt (y + 210));

        // The favicons magnified, so their pixels show.
        y += 250.0f;
        x = small.getX() + 50.0f;
        drawMagnified (g, icon (16, true), juce::roundToInt (x), juce::roundToInt (y), 8);
        drawMagnified (g, icon (32, true), juce::roundToInt (x + 160), juce::roundToInt (y), 4);
        drawMagnified (g, icon (64, true), juce::roundToInt (x + 320), juce::roundToInt (y), 2);
        g.setColour (dim);
        g.drawSingleLineText ("favicons 16, 32, 64 px, magnified", juce::roundToInt (x + 480), juce::roundToInt (y + 70));
        caption (g, "Small sizes", small.getX() + 36, small.getBottom() - 30, dim);
        return image;
    }

    // ---------------------------------------------------------------------------------------
    // svg

    bool renderSvg (const juce::File& svg, const juce::File& out, int width)
    {
        const auto xml = juce::XmlDocument::parse (svg);
        const auto drawable = xml != nullptr ? juce::Drawable::createFromSVG (*xml) : nullptr;
        if (drawable == nullptr)
        {
            std::cerr << "could not read " << svg.getFullPathName() << "\n";
            return false;
        }

        const auto bounds = drawable->getDrawableBounds();
        const auto height = juce::roundToInt (static_cast<float> (width) * bounds.getHeight() / bounds.getWidth());
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Graphics g (image);
        drawable->drawWithin (g, image.getBounds().toFloat(), juce::RectanglePlacement::centred, 1.0f);
        return save (image, out);
    }
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "";
    auto file = [&] (int i) { return juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]); };

    if (mode == "review" && argc == 3)
        return file (2).createDirectory() && review (file (2)) ? 0 : 1;

    if (mode == "brand" && argc == 3)
        return file (2).createDirectory() && exportBrand (file (2)) ? 0 : 1;

    if (mode == "overview" && argc == 3)
        return save (overview(), file (2)) ? 0 : 1;

    if (mode == "svg" && argc == 5)
        return renderSvg (file (2), file (3), juce::String (argv[4]).getIntValue()) ? 0 : 1;

    if (mode == "mark-svg" && argc == 5)
        return saveText (markSvgFile (argv[2], juce::String (argv[3]).getIntValue()), file (4)) ? 0 : 1;

    std::cerr << "usage: AnyMeansLogoLab review <dir> | brand <dir> | overview <file.png> | svg <in.svg> <out.png> <width>\n"
                 "       | mark-svg <dark|light|mono> <lines> <out.svg>\n";
    return 1;
}
