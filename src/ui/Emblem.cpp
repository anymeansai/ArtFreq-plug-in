#include "Emblem.h"

#include "Theme.h"

#include <cmath>

namespace freqart::ui
{
namespace
{
    // The letterforms in cap units: the cap height is 1, x runs from 0 to glyphWidth. Horizontal
    // strokes are whole lines (rows), so the F's arms are spectral lines of their own.
    constexpr float glyphWidth  = 1.06f;
    constexpr float apexX       = 0.36f;    // where the A's leg meets the top...
    constexpr float legWidth    = 0.182f;   // ...and how wide it is, measured across
    constexpr float stemLeft    = 0.545f;   // the A's right leg and the F's stem
    constexpr float stemRight   = 0.715f;
    constexpr float midArmRight = 0.95f;    // the A's crossbar carries on to here
    constexpr float topArmShare = 0.155f;   // of the cap height
    constexpr float barTopShare = 0.53f;
    constexpr float barShare    = 0.13f;

    // The dissolve, as shares of the glyph's width: solid up to fadeStart, all lines by fadeEnd.
    constexpr float fadeStart = 0.27f;
    constexpr float fadeEnd   = 1.0f;
    constexpr float smallFadeStart = 0.34f;
    constexpr float smallFadeEnd   = 0.5f;

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
    };

    Layout makeLayout (juce::Graphics& g, juce::Rectangle<float> area)
    {
        Layout l {};
        l.scale = juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());

        // Whole physical pixels per line: about 3 at header size, more and finer lines when large.
        const auto fitHeight = juce::jmin (area.getHeight(), area.getWidth() / glyphWidth) * l.scale;
        const auto pitchPx = juce::jmax (3.0f, std::round (0.68f * std::sqrt (fitHeight)),
                                         std::ceil (fitHeight / 30.0f));
        l.rows = juce::jmax (5, static_cast<int> (std::floor (fitHeight / pitchPx)));
        l.pitch = pitchPx / l.scale;
        l.unit = static_cast<float> (l.rows) * l.pitch;
        l.gapShare = juce::jlimit (0.3f, 0.48f, 0.22f + 0.018f * pitchPx);
        l.left = l.snap (area.getCentreX() - glyphWidth * l.unit * 0.5f);
        l.top = l.snap (area.getCentreY() - l.unit * 0.5f);
        return l;
    }

    juce::Path makeGlyph (const Layout& l)
    {
        juce::Path p;   // every part wound the same way, so the overlaps fill (non-zero winding)

        // The A's left leg, from its foot up to the apex.
        p.startNewSubPath (l.x (0.0f), l.y (1.0f));
        p.lineTo (l.x (apexX), l.y (0.0f));
        p.lineTo (l.x (apexX + legWidth), l.y (0.0f));
        p.lineTo (l.x (legWidth), l.y (1.0f));
        p.closeSubPath();

        // The stem: the A's right leg and the F's upright.
        const auto stemL = l.snap (l.x (stemLeft));
        const auto stemR = l.snap (l.x (stemRight));
        p.addRectangle (stemL, l.top, stemR - stemL, l.unit);

        // The apex running into the F's top arm.
        const auto topRows = juce::jmax (1, juce::roundToInt (topArmShare * static_cast<float> (l.rows)));
        const auto armL = l.x (apexX + 0.01f);
        p.addRectangle (armL, l.top, l.snap (l.x (glyphWidth)) - armL, static_cast<float> (topRows) * l.pitch);

        // The A's crossbar carrying on as the F's middle arm.
        const auto barRows = juce::jmax (1, juce::roundToInt (barShare * static_cast<float> (l.rows)));
        const auto barFirst = juce::jlimit (topRows + 1, l.rows - barRows - 1,
                                            juce::roundToInt (barTopShare * static_cast<float> (l.rows)));
        const auto barBottom = static_cast<float> (barFirst + barRows) / static_cast<float> (l.rows);
        const auto barL = l.x (apexX * (1.0f - barBottom) + legWidth * 0.5f);   // inside the leg
        p.addRectangle (barL, l.top + static_cast<float> (barFirst) * l.pitch,
                        l.snap (l.x (midArmRight)) - barL, static_cast<float> (barRows) * l.pitch);
        return p;
    }

    /** One horizontal band per line, full height (touching) on the left, thinning to
        lineShare x the pitch on the right. Centred in its row when large; when small, the top
        edge stays put and the gap opens below it, so lines stay on whole pixels. */
    juce::Path makeLines (const Layout& l, float lineShare, bool small)
    {
        constexpr int steps = 24;
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

            auto mid = [&] (float half) { return centre - anchor * (0.5f * l.pitch - half); };

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
} // namespace

void drawEmblem (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (area.isEmpty())
        return;

    const auto l = makeLayout (g, area);
    const auto glyph = makeGlyph (l);
    const auto lineShare = 1.0f - l.gapShare;
    const auto white = juce::Colours::white;

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (glyph);

    if (l.pitch * l.scale < 5.0f)
    {
        // Small: one colour per line, white-hot on the left, warming to the canvas's orange.
        // The gap is a whole pixel at the right-hand end.
        const auto smallShare = 1.0f - 1.0f / (l.pitch * l.scale);
        g.setGradientFill (across (l, { { 0.0, white }, { 0.4, white },
                                        { 0.7, theme::spectrum (0.88f) },
                                        { 1.0, theme::spectrum (0.8f) } }));
        g.fillPath (makeLines (l, smallShare, true));
        return;
    }

    // Large: each line has a white-hot core inside yellow / orange and magenta edges, as the
    // spectrogram shows a loud sine line.
    g.setGradientFill (across (l, { { 0.0, theme::spectrum (0.85f) }, { 0.5, theme::spectrum (0.66f) },
                                    { 1.0, theme::spectrum (0.48f) } }));
    g.fillPath (makeLines (l, lineShare, false));

    g.setGradientFill (across (l, { { 0.0, theme::spectrum (0.96f) }, { 0.5, theme::spectrum (0.8f) },
                                    { 1.0, theme::spectrum (0.68f) } }));
    g.fillPath (makeLines (l, lineShare * 0.7f, false));

    g.setGradientFill (across (l, { { 0.0, white }, { 0.42, white }, { 0.75, theme::spectrum (0.93f) },
                                    { 1.0, theme::spectrum (0.85f) } }));
    g.fillPath (makeLines (l, lineShare * 0.38f, false));
}

} // namespace freqart::ui
