#include "AnyMeansMark.h"

#include <array>
#include <cmath>

namespace anymeans::brand
{
namespace
{
    // The letterforms in cap units: the cap height is 1, x runs from 0 to glyphWidth. The A is
    // the ArtFreq emblem's A (same leg, same slant, same stem), so the two marks are one family.
    constexpr float apexX      = 0.36f;     // where the A's leg meets the top...
    constexpr float legWidth   = 0.182f;    // ...and how wide it is, measured across
    constexpr float stemLeft   = 0.545f;    // the A's right leg and the M's first stem
    constexpr float stemRight  = 0.715f;
    constexpr float diagWidth  = 0.185f;    // the M's diagonals, measured across
    constexpr float stem2Left  = 1.09f;     // the M's right stem
    constexpr float stem2Right = 1.26f;
    constexpr float vertexY    = 0.74f;     // the bottom of the M's V
    constexpr float glyphWidth = stem2Right;
    constexpr float vertexX    = 0.5f * (stemLeft + stem2Right);
    constexpr float barTopShare = 0.53f;    // the A's crossbar, as in the emblem
    constexpr float barShare    = 0.13f;
    constexpr float tinyHeight  = 16.0f;    // physical pixels: below it the V is drawn in steps

    // The dissolve, as shares of the glyph's width: solid up to fadeStart, all lines by fadeEnd.
    // The A's leg stays whole; the lines take over across the M. Small, the gaps open within a
    // few pixels, from the shared stem on (as the emblem's do from its stem), so the whole M is
    // lines and the gaps are soon whole pixels.
    constexpr float fadeStart = 0.3f;
    constexpr float fadeEnd   = 1.0f;
    constexpr float smallFadeStart = 0.4f;
    constexpr float smallFadeEnd   = 0.48f;

    // The spectrogram colour map (ArtFreq's theme::spectrum, copied): 0 silent, 1 loudest.
    juce::Colour spectrum (float t) noexcept
    {
        static constexpr std::array<juce::uint32, 9> stops {
            0xff07080c, 0xff1b1036, 0xff3d1466, 0xff7a1f7f, 0xffb83a6e,
            0xffe8664a, 0xfff9a13a, 0xfffce7a0, 0xffffffff
        };

        t = juce::jlimit (0.0f, 1.0f, t);
        const auto scaled = t * static_cast<float> (stops.size() - 1);
        const auto index = juce::jmin (static_cast<int> (scaled), static_cast<int> (stops.size()) - 2);
        const auto frac = scaled - static_cast<float> (index);
        return juce::Colour (stops[static_cast<size_t> (index)])
            .interpolatedWith (juce::Colour (stops[static_cast<size_t> (index + 1)]), frac);
    }

    const juce::Colour ink { 0xff0b0d12 };  // ArtFreq's background, the letters on light grounds

    float smoothStep (float edge0, float edge1, float x)
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    /** A stable pseudo-random value in [-0.5, 0.5) per line, so lines let go at slightly
        different points the way a real analyser's do. */
    float lineJitter (int row)
    {
        const auto v = std::sin (static_cast<float> (row) * 12.9898f + 4.1414f) * 43758.5453f;
        return v - std::floor (v) - 0.5f;
    }

    struct Layout
    {
        float scale;    // physical pixels per logical pixel
        float left, top, unit, pitch;
        int rows;
        float gapShare; // of a line's pitch, at the right-hand end

        float x (float u) const noexcept { return left + u * unit; }
        float y (float v) const noexcept { return top + v * unit; }
        float snap (float v) const noexcept { return std::round (v * scale) / scale; }
        bool small() const noexcept { return pitch * scale < 5.0f; }
    };

    float physicalScale (juce::Graphics& g)
    {
        return juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    Layout makeLayout (float scale, juce::Rectangle<float> area)
    {
        Layout l {};
        l.scale = scale;

        // Whole physical pixels per line: 3 when small, more and finer lines when large.
        const auto fitHeight = juce::jmin (area.getHeight(), area.getWidth() / glyphWidth) * l.scale;
        const auto pitchPx = juce::jmax (3.0f, std::round (0.68f * std::sqrt (fitHeight)),
                                         std::ceil (fitHeight / 30.0f));
        l.rows = juce::jmax (4, static_cast<int> (std::floor (fitHeight / pitchPx)));
        l.pitch = pitchPx / l.scale;
        l.unit = static_cast<float> (l.rows) * l.pitch;
        l.gapShare = juce::jlimit (0.3f, 0.48f, 0.22f + 0.018f * pitchPx);
        l.left = l.snap (area.getCentreX() - glyphWidth * l.unit * 0.5f);
        l.top = l.snap (area.getCentreY() - l.unit * 0.5f);
        return l;
    }

    void addQuad (juce::Path& p, juce::Point<float> bl, juce::Point<float> tl,
                  juce::Point<float> tr, juce::Point<float> br)
    {
        // Wound like Path::addRectangle, so overlapping parts fill (non-zero winding).
        p.startNewSubPath (bl);
        p.lineTo (tl);
        p.lineTo (tr);
        p.lineTo (br);
        p.closeSubPath();
    }

    juce::Path makeGlyph (const Layout& l)
    {
        juce::Path p;

        // The stems, on whole pixels: the A's right leg / the M's first stem, and the M's right.
        // Both as wide as each other in whole pixels.
        const auto stemWidth = juce::jmax (1.0f / l.scale, l.snap ((stemRight - stemLeft) * l.unit));
        const auto stemL = l.snap (l.x (stemLeft));
        const auto stemR = stemL + stemWidth;
        const auto stem2R = l.snap (l.x (stem2Right));
        const auto stem2L = stem2R - stemWidth;
        p.addRectangle (stemL, l.top, stemR - stemL, l.unit);
        p.addRectangle (stem2L, l.top, stem2R - stem2L, l.unit);

        // The A's left leg, from its foot up to the apex, where it runs into the stem.
        const auto overlap = juce::jmax (1.0f / l.scale, 0.02f * l.unit);
        addQuad (p, { l.x (0.0f), l.y (1.0f) }, { l.x (apexX), l.top },
                 { stemL + overlap, l.top }, { l.x (legWidth), l.y (1.0f) });

        // The M's V: down from the first stem's top, up to the right stem's top. Flat-cut at the
        // bottom, as the A's apex and feet are.
        const auto vx = l.x (vertexX);
        const auto vy = l.y (vertexY);
        const auto half = 0.5f * diagWidth * l.unit;
        const auto diag = diagWidth * l.unit;

        if (l.unit * l.scale >= tinyHeight)
        {
            addQuad (p, { vx - half, vy }, { stemL, l.top }, { stemL + diag, l.top }, { vx + half, vy });
            addQuad (p, { vx - half, vy }, { stem2R - diag, l.top }, { stem2R, l.top }, { vx + half, vy });
        }
        else
        {
            // Tiny (a favicon), the diagonals are whole-pixel steps, one per line, mirrored so
            // the V is symmetric: the first step just clear of the stems (the M's shoulders),
            // the last one in the middle. Slanted edges a few pixels long would only blur. They
            // thin to leave a notch of at least three pixels between them on the first line.
            const auto stemPx = std::round (stemWidth * l.scale);
            const auto innerPx = std::round ((stem2L - stemR) * l.scale);
            const auto spanPx = std::round ((stem2R - stemL) * l.scale);
            const auto w = juce::jlimit (1.0f, juce::jmax (1.0f, std::floor ((innerPx - 3.0f) * 0.5f)),
                                         std::round (diag * l.scale));
            const auto travel = std::ceil ((spanPx - 2.0f * w) * 0.5f);
            const auto steps = juce::jmax (2, juce::roundToInt (vertexY * static_cast<float> (l.rows)) - 1);

            for (int row = 0; row < steps; ++row)
            {
                const auto offset = stemPx + std::round ((travel - stemPx) * static_cast<float> (row)
                                                         / static_cast<float> (steps - 1));
                const auto y0 = l.top + static_cast<float> (row) * l.pitch;
                p.addRectangle (stemL + offset / l.scale, y0, w / l.scale, l.pitch);
                p.addRectangle (stem2R - (offset + w) / l.scale, y0, w / l.scale, l.pitch);
            }
        }

        // The A's crossbar, whole lines, from inside the leg to the stem.
        const auto barRows = juce::jmax (1, juce::roundToInt (barShare * static_cast<float> (l.rows)));
        const auto barFirst = juce::jlimit (1, l.rows - barRows - 1,
                                            juce::roundToInt (barTopShare * static_cast<float> (l.rows)));
        const auto barBottom = static_cast<float> (barFirst + barRows) / static_cast<float> (l.rows);
        const auto barL = l.x (apexX * (1.0f - barBottom) + legWidth * 0.5f);   // inside the leg
        p.addRectangle (barL, l.top + static_cast<float> (barFirst) * l.pitch,
                        stemL + overlap - barL, static_cast<float> (barRows) * l.pitch);
        return p;
    }

    /** One horizontal band per line, full height (touching) on the left, thinning to
        lineShare x the pitch on the right. Centred in its row when large; when small, the top
        edge stays put and the gap opens below it, so lines stay on whole pixels. */
    juce::Path makeLines (const Layout& l, float lineShare)
    {
        constexpr int steps = 24;
        const auto small = l.small();
        const auto jitterAmount = 0.07f * smoothStep (8.0f, 20.0f, static_cast<float> (l.rows));
        const auto anchor = small ? 1.0f : 0.0f;

        juce::Path p;
        juce::Array<juce::Point<float>> edge;   // (x, half thickness)

        for (int row = 0; row < l.rows; ++row)
        {
            const auto centre = l.top + (static_cast<float> (row) + 0.5f) * l.pitch;
            const auto shift = lineJitter (row) * jitterAmount;

            auto halfAt = [&] (float u)
            {
                // Small, the gaps open over a few pixels so they are soon whole and crisp.
                const auto s = small ? smoothStep (smallFadeStart, smallFadeEnd, u)
                                     : smoothStep (fadeStart + shift, fadeEnd + shift * 0.5f, u);
                return 0.5f * l.pitch * juce::jmap (s, 1.04f, lineShare);
            };

            edge.clearQuick();
            edge.add ({ l.x (-0.1f), halfAt (-0.1f) });
            for (int i = 0; i <= steps; ++i)
            {
                const auto u = juce::jmap (static_cast<float> (i) / steps, fadeStart - 0.06f, fadeEnd + 0.06f);
                edge.add ({ l.x (u * glyphWidth), halfAt (u) });
            }
            edge.add ({ l.x (glyphWidth + 0.1f), halfAt (1.1f) });

            auto mid = [&] (float halfHeight) { return centre - anchor * (0.5f * l.pitch - halfHeight); };

            // Wound like Path::addRectangle: up the left, along the top, back along the bottom.
            p.startNewSubPath (edge.getFirst().x, mid (edge.getFirst().y) + edge.getFirst().y);
            for (auto e : edge)
                p.lineTo (e.x, mid (e.y) - e.y);
            for (int i = edge.size(); --i >= 0;)
                p.lineTo (edge[i].x, mid (edge[i].y) + edge[i].y);
            p.closeSubPath();
        }

        return p;
    }

    juce::ColourGradient across (const Layout& l, std::initializer_list<std::pair<double, juce::Colour>> stops)
    {
        juce::ColourGradient grad;
        grad.point1 = { l.x (0.0f), 0.0f };
        grad.point2 = { l.x (glyphWidth), 0.0f };
        grad.isRadial = false;
        for (const auto& [position, colour] : stops)
            grad.addColour (position, colour);
        return grad;
    }

    /** The three layers of a line, outermost first, and the one colour per line when small. */
    struct Palette
    {
        std::array<juce::ColourGradient, 3> layers;
        juce::ColourGradient small;
    };

    Palette makePalette (const Layout& l, Variant variant)
    {
        if (variant == Variant::onLight)
        {
            // The same colour map from its dark end: ink cores inside purple, magenta and
            // orange edges, the way a loud line reads printed on paper.
            return { { across (l, { { 0.0, ink }, { 0.3, ink }, { 0.6, spectrum (0.55f) }, { 1.0, spectrum (0.74f) } }),
                       across (l, { { 0.0, ink }, { 0.35, ink }, { 0.65, spectrum (0.4f) }, { 1.0, spectrum (0.53f) } }),
                       across (l, { { 0.0, ink }, { 0.45, ink }, { 0.8, spectrum (0.2f) }, { 1.0, spectrum (0.3f) } }) },
                     across (l, { { 0.0, ink }, { 0.5, ink }, { 0.75, spectrum (0.32f) }, { 1.0, spectrum (0.46f) } }) };
        }

        // As the emblem: white-hot cores inside yellow / orange and magenta edges, as the
        // spectrogram shows a loud sine line.
        const auto white = juce::Colours::white;
        return { { across (l, { { 0.0, spectrum (0.85f) }, { 0.5, spectrum (0.66f) }, { 1.0, spectrum (0.48f) } }),
                   across (l, { { 0.0, spectrum (0.96f) }, { 0.5, spectrum (0.8f) }, { 1.0, spectrum (0.68f) } }),
                   across (l, { { 0.0, white }, { 0.42, white }, { 0.75, spectrum (0.93f) }, { 1.0, spectrum (0.85f) } }) },
                 across (l, { { 0.0, white }, { 0.45, white }, { 0.75, spectrum (0.88f) }, { 1.0, spectrum (0.8f) } }) };
    }
} // namespace

float markAspect() noexcept
{
    return glyphWidth;
}

juce::Rectangle<float> markBounds (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto l = makeLayout (physicalScale (g), area);
    return { l.left, l.top, glyphWidth * l.unit, l.unit };
}

void drawAnyMeansMark (juce::Graphics& g, juce::Rectangle<float> area, Variant variant)
{
    if (area.isEmpty())
        return;

    const auto l = makeLayout (physicalScale (g), area);
    const auto palette = makePalette (l, variant);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (makeGlyph (l));

    if (l.small())
    {
        // Small: one colour per line; the gap is a whole pixel at the right-hand end.
        g.setGradientFill (palette.small);
        g.fillPath (makeLines (l, 1.0f - 1.0f / (l.pitch * l.scale)));
        return;
    }

    const auto lineShare = 1.0f - l.gapShare;
    const float shares[] = { 1.0f, 0.7f, 0.38f };
    for (size_t i = 0; i < palette.layers.size(); ++i)
    {
        g.setGradientFill (palette.layers[i]);
        g.fillPath (makeLines (l, lineShare * shares[i]));
    }
}

} // namespace anymeans::brand
