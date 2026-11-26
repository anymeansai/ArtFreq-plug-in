#include "TextRaster.h"

#include "Fonts.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <vector>

namespace artfreq
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


// ---- emoji -----------------------------------------------------------------------------------
//
// The art fonts have no emoji, so a line is split into runs: ordinary text in the art font, emoji
// in Noto Emoji (fonts::emoji). Each emoji run is shaped as a whole, so the font's own
// substitutions build the sequences: a skin tone joins its emoji (one glyph: a monochrome font
// has one shade), a zero-width joiner makes one picture of several, two regional indicators a
// flag, a digit and U+20E3 a keycap.

using CodePoint = juce::juce_wchar;

constexpr CodePoint zeroWidthJoiner = 0x200d;
constexpr CodePoint textSelector = 0xfe0e;     // variation selector 15: "as text"
constexpr CodePoint emojiSelector = 0xfe0f;    // variation selector 16: "as an emoji"
constexpr CodePoint keycapMark = 0x20e3;

bool isVariationSelector (CodePoint c) noexcept  { return c == textSelector || c == emojiSelector; }
bool isSkinTone (CodePoint c) noexcept           { return c >= 0x1f3fb && c <= 0x1f3ff; }
bool isTag (CodePoint c) noexcept                { return c >= 0xe0020 && c <= 0xe007f; }
bool isKeycapBase (CodePoint c) noexcept         { return (c >= '0' && c <= '9') || c == '#' || c == '*'; }

/** Characters that are never an emoji of their own: they change the one before them. */
bool continuesEmoji (CodePoint c) noexcept
{
    return isVariationSelector (c) || c == zeroWidthJoiner || c == keycapMark || isSkinTone (c) || isTag (c);
}

bool hasGlyph (const juce::Typeface::Ptr& typeface, CodePoint c)
{
    return typeface != nullptr && typeface->getNominalGlyphForCodepoint (c).has_value();
}

struct TextRun
{
    juce::String text;
    bool emoji = false;
    bool foreign = false;   // text the art font has no glyphs for (the system's fonts may draw it)
};

/** Splits a line into text and emoji runs. A character is an emoji when the art font has no
    glyph for it and the emoji font has one, or when it asks to be one (followed by U+FE0F) and
    the emoji font can draw it. What changes an emoji stays with it; ASCII is always text, except
    the digit, # or * of a keycap. Characters neither font has stay text, as before. */
std::vector<TextRun> splitRuns (const juce::String& line, const juce::Typeface::Ptr& artTypeface)
{
    std::vector<CodePoint> chars;

    for (auto p = line.getCharPointer(); ! p.isEmpty();)
        chars.push_back (p.getAndAdvance());

    std::vector<TextRun> runs;
    juce::Typeface::Ptr emojiTypeface;   // fetched (and loaded) only when a character may need it
    auto emojiFetched = false;

    const auto emojiHas = [&] (CodePoint c)
    {
        if (! emojiFetched)
        {
            emojiTypeface = fonts::emojiTypeface();
            emojiFetched = true;
        }

        return hasGlyph (emojiTypeface, c);
    };

    const auto at = [&] (size_t index) { return index < chars.size() ? chars[index] : CodePoint (0); };

    auto inEmoji = false;   // the character before this one went to the emoji font

    for (size_t i = 0; i < chars.size(); ++i)
    {
        const auto c = chars[i];
        auto emoji = false;

        if (isVariationSelector (c) || c == zeroWidthJoiner)
            emoji = inEmoji;                                  // with whatever is before it
        else if (inEmoji && (continuesEmoji (c) || at (i - 1) == zeroWidthJoiner))
            emoji = emojiHas (c);                             // part of the emoji before it
        else if (isKeycapBase (c))
            emoji = at (isVariationSelector (at (i + 1)) ? i + 2 : i + 1) == keycapMark
                    && emojiHas (c) && emojiHas (keycapMark);
        else if (c < 0x80)
            emoji = false;
        else if (at (i + 1) == emojiSelector && emojiHas (c))
            emoji = true;
        else
            emoji = ! hasGlyph (artTypeface, c) && emojiHas (c);

        // (A tag the emoji font lacks is left out of its emoji: it could only be drawn as a
        // "missing" box in the middle of it.)
        if (! emoji && inEmoji && isTag (c))
            continue;

        // Text the art font cannot draw is shaped on its own, with whatever font the system
        // finds for it, so however that font lays it out the letters after it still follow it.
        const auto foreign = ! emoji && c >= 0x80 && ! hasGlyph (artTypeface, c);

        if (runs.empty() || runs.back().emoji != emoji || runs.back().foreign != foreign)
            runs.push_back ({ {}, emoji, foreign });

        runs.back().text += juce::String::charToString (c);
        inEmoji = emoji;
    }

    return runs;
}

/** Where emoji go beside the art font's letters, for fonts at layoutHeight. */
struct EmojiPlacement
{
    juce::Font font { juce::FontOptions() };
    float baselineOffset = 0.0f;   // added to the line's baseline (positive: lower)
};

/** The height of the art font's capitals, in ems. */
float capHeightOf (const juce::Typeface::Ptr& typeface)
{
    if (typeface != nullptr)
    {
        if (const auto glyph = typeface->getNominalGlyphForCodepoint ('H'))
        {
            juce::Path outline;
            typeface->getOutlineForGlyph (static_cast<int> (*glyph), outline);
            const auto height = -outline.getBounds().getY();

            if (height > 0.3f && height < 1.5f)
                return height;
        }
    }

    return 0.7f;
}

EmojiPlacement emojiPlacementFor (const juce::Typeface::Ptr& artTypeface)
{
    // Noto Emoji draws every emoji in the same square: 2100 units high in a 2048 unit em, from
    // 350 units under the baseline to 1750 above it.
    static constexpr float boxHeight = 2100.0f / 2048.0f;
    static constexpr float boxCentreAboveBaseline = 700.0f / 2048.0f;

    // An emoji is a little taller than a capital letter and centred on the capitals, as
    // operating systems set them: it reaches as far over the capitals as under the baseline.
    static constexpr float heightInCapitals = 1.3f;

    const auto capHeight = capHeightOf (artTypeface) * layoutHeight;
    const auto size = capHeight * heightInCapitals / boxHeight;

    return { fonts::emoji (size), size * boxCentreAboveBaseline - capHeight * 0.5f };
}

/** Adds a run of ordinary text at the pen; returns where the pen ends up. The run starts at the
    pen whichever way the shaper laid it out: a run that begins with a right-to-left letter (or
    a fallback font's glyph for one) can come back laid out leftwards from where it was asked
    to start, over the run before it. */
float addTextRun (juce::Path& path, const juce::String& text, const juce::Font& font, float pen, float baseline)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, baseline);

    if (glyphs.getNumGlyphs() == 0)
        return pen;

    auto left = std::numeric_limits<float>::max();
    auto right = std::numeric_limits<float>::lowest();

    for (const auto& glyph : glyphs)
    {
        left = std::min (left, glyph.getLeft());
        right = std::max (right, glyph.getRight());
    }

    juce::Path run;
    glyphs.createPath (run);
    path.addPath (run, juce::AffineTransform::translation (pen - left, 0.0f));
    return pen + std::max (0.0f, right - left);
}

/** Adds a run of emoji at the pen; returns where the pen ends up. */
float addEmojiRun (juce::Path& path, const juce::String& text, const EmojiPlacement& placement, float pen, float baseline)
{
    const auto typeface = placement.font.getTypefacePtr();

    if (typeface == nullptr)
        return pen;

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (placement.font, text, 0.0f, 0.0f);

    const auto size = placement.font.getHeightInPoints();
    auto first = true;

    const auto isToneGlyph = [&typeface] (int index)
    {
        for (CodePoint tone = 0x1f3fb; tone <= 0x1f3ff; ++tone)
            if (typeface->getNominalGlyphForCodepoint (tone) == static_cast<uint32_t> (index))
                return true;

        return false;
    };

    for (const auto& glyph : glyphs)
    {
        const auto index = glyph.getGlyphIndex();

        // Never a "missing" box. And a skin tone left over after shaping is one its emoji cannot
        // take, which a monochrome font would draw as a square beside it: only a tone on its own
        // is drawn (as that square).
        if (index <= 0 || (! first && isToneGlyph (index)))
            continue;

        first = false;

        if (! glyph.isWhitespace())
        {
            juce::Path outline;
            typeface->getOutlineForGlyph (index, outline);
            path.addPath (outline, juce::AffineTransform::scale (size).translated (pen, baseline + placement.baselineOffset));
        }

        pen += glyph.getRight() - glyph.getLeft();
    }

    return pen;
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
    const auto artTypeface = fonts::artTypeface (fontIndex);

    for (int i = 0; i < lines.size(); ++i)
    {
        if (lines[i].trim().isEmpty())
            continue;  // a blank line still takes its height (the baseline below moves on)

        const auto baseline = static_cast<float> (i) * lineHeight;
        const auto runs = splitRuns (lines[i], artTypeface);
        juce::Path linePath;

        if (runs.size() == 1 && ! runs.front().emoji && ! runs.front().foreign)
        {
            // Only the art font's own letters: the line is shaped in one piece.
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, lines[i], 0.0f, baseline);
            glyphs.createPath (linePath);
        }
        else
        {
            const auto emoji = emojiPlacementFor (artTypeface);
            auto pen = 0.0f;

            for (const auto& run : runs)
                pen = run.emoji ? addEmojiRun (linePath, run.text, emoji, pen, baseline)
                                : addTextRun (linePath, run.text, font, pen, baseline);
        }

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

} // namespace artfreq
