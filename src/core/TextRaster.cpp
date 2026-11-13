#include "TextRaster.h"

#include "Fonts.h"

#include <algorithm>
#include <cmath>

namespace freqart
{

namespace
{

// Glyphs are laid out at this size; the result is stretched to its box anyway, so it only needs
// to be large enough for the outlines to be smooth.
constexpr float layoutHeight = 200.0f;

/** Max-combines `source` (a single-channel image) into `target` at `origin`. */
void maxCombine (juce::Image& target, const juce::Image& source, juce::Point<int> origin)
{
    const juce::Image::BitmapData src (source, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (target, origin.x, origin.y, source.getWidth(), source.getHeight(),
                                 juce::Image::BitmapData::readWrite);

    for (int y = 0; y < source.getHeight(); ++y)
    {
        const auto* s = src.getLinePointer (y);
        auto* d = dst.getLinePointer (y);

        for (int x = 0; x < source.getWidth(); ++x)
        {
            const auto value = s[x * src.pixelStride];
            auto& out = d[x * dst.pixelStride];
            out = std::max (out, value);
        }
    }
}

} // namespace

juce::Path makeTextPath (const juce::String& text, int fontIndex)
{
    juce::Path result;

    if (text.trim().isEmpty())
        return result;

    const auto font = fonts::art (fontIndex, layoutHeight);
    const auto lineHeight = font.getHeight();
    const auto lines = juce::StringArray::fromLines (text);

    for (int i = 0; i < lines.size(); ++i)
    {
        if (lines[i].trim().isEmpty())
            continue;  // a blank line still takes its height (the baseline below moves on)

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, lines[i], 0.0f, static_cast<float> (i) * lineHeight);

        juce::Path linePath;
        glyphs.createPath (linePath);

        if (linePath.isEmpty())
            continue;

        // Centre each line on x = 0 by its ink, which is what the eye compares.
        const auto bounds = linePath.getBounds();
        linePath.applyTransform (juce::AffineTransform::translation (-bounds.getCentreX(), 0.0f));
        result.addPath (linePath);
    }

    if (! result.isEmpty())
    {
        const auto bounds = result.getBounds();
        result.applyTransform (juce::AffineTransform::translation (-bounds.getX(), -bounds.getY()));
    }

    return result;
}

void renderTextInto (juce::Image& target, juce::Rectangle<float> area,
                     const juce::String& text, int fontIndex, TextStyle style)
{
    if (! target.isValid() || target.getFormat() != juce::Image::SingleChannel)
    {
        jassertfalse;  // the canvas is single-channel
        return;
    }

    if (! (area.getWidth() > 0.0f && area.getHeight() > 0.0f) || ! area.isFinite())
        return;

    const auto inner = area.reduced (area.getHeight() * 0.04f);

    if (inner.getWidth() <= 0.0f || inner.getHeight() <= 0.0f)
        return;

    auto path = makeTextPath (text, fontIndex);
    const auto pathBounds = path.getBounds();

    if (pathBounds.getWidth() < 1.0e-3f || pathBounds.getHeight() < 1.0e-3f)
        return;

    // Stretch (not uniformly): the letters fill the box the user placed.
    path.applyTransform (juce::AffineTransform::translation (-pathBounds.getX(), -pathBounds.getY())
                             .scaled (inner.getWidth() / pathBounds.getWidth(), inner.getHeight() / pathBounds.getHeight())
                             .translated (inner.getX(), inner.getY()));

    const auto outline = style == TextStyle::outline;

    // Outline: a stroke of 7 % of the box height centred on the letters' outlines, of which only
    // the part outside the letters is kept. Centred strokes that wide fill the stems of bold
    // letters completely (the text just gets fatter); keeping the outer half leaves every letter
    // a silent shape ringed by sound, whatever the font, and still inside the 4 % inset.
    const auto strokeWidth = std::max (3.0f, area.getHeight() * 0.07f);

    // Only the part of the target the drawing can reach is rendered, never outside the box.
    const auto reach = outline ? inner.expanded (strokeWidth * 0.5f + 1.0f) : inner.expanded (1.0f);
    const auto box = area.getSmallestIntegerContainer();
    const auto clip = reach.getSmallestIntegerContainer().getIntersection (box).getIntersection (target.getBounds());

    if (clip.isEmpty())
        return;

    const auto toScratch = juce::AffineTransform::translation (static_cast<float> (-clip.getX()), static_cast<float> (-clip.getY()));

    // Software images, so the result is the same on every platform and the pixels are plain memory.
    juce::Image scratch (juce::Image::SingleChannel, clip.getWidth(), clip.getHeight(), true, juce::SoftwareImageType());

    {
        juce::Graphics g (scratch);
        g.setColour (juce::Colours::white);

        if (outline)
            g.strokePath (path, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), toScratch);
        else
            g.fillPath (path, toScratch);
    }

    if (outline)
    {
        juce::Image letters (juce::Image::SingleChannel, clip.getWidth(), clip.getHeight(), true, juce::SoftwareImageType());

        {
            juce::Graphics g (letters);
            g.setColour (juce::Colours::white);
            g.fillPath (path, toScratch);
        }

        // stroke * (1 - letter), anti-aliased on both edges.
        juce::Image::BitmapData ring (scratch, juce::Image::BitmapData::readWrite);
        const juce::Image::BitmapData inside (letters, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < clip.getHeight(); ++y)
        {
            auto* r = ring.getLinePointer (y);
            const auto* l = inside.getLinePointer (y);

            for (int x = 0; x < clip.getWidth(); ++x)
            {
                auto& value = r[x * ring.pixelStride];
                value = static_cast<juce::uint8> ((value * (255 - l[x * inside.pixelStride]) + 127) / 255);
            }
        }
    }

    maxCombine (target, scratch, clip.getPosition());
}

} // namespace freqart
