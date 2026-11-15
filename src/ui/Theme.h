#pragma once

// Colours, sizes and the spectrogram colour map. See docs/DESIGN.md, "Look".

#include "core/Fonts.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace artfreq::ui::theme
{

inline const juce::Colour bg          { 0xff0b0d12 };
inline const juce::Colour panel       { 0xff12151c };
inline const juce::Colour raised      { 0xff1a1e27 };
inline const juce::Colour raisedHover { 0xff222735 };
inline const juce::Colour line        { 0xff262b36 };
inline const juce::Colour text        { 0xffe8ebf2 };
inline const juce::Colour textDim     { 0xff8c94a6 };
inline const juce::Colour textFaint   { 0xff5a6274 };
inline const juce::Colour accent      { 0xff7c6cff };
inline const juce::Colour accent2     { 0xff36d7c4 };
inline const juce::Colour danger      { 0xffff5c6c };

inline constexpr float controlCorner = 6.0f;
inline constexpr float panelCorner = 10.0f;

// Type scale (logical pixels)
inline constexpr float labelSize = 11.0f;    // uppercase labels, +6 % tracking
inline constexpr float controlSize = 13.0f;
inline constexpr float titleSize = 15.0f;
inline constexpr float wordmarkSize = 20.0f;

/** The spectrogram colour map: t = 0 silent, t = 1 loudest. Used by the canvas and the live
    view so they look alike. */
inline juce::Colour spectrum (float t) noexcept
{
    static const std::array<juce::uint32, 9> stops {
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

/** A 256-entry lookup of spectrum() as packed ARGB, for filling images quickly. */
inline const std::array<juce::PixelARGB, 256>& spectrumTable()
{
    static const auto table = []
    {
        std::array<juce::PixelARGB, 256> t {};
        for (size_t i = 0; i < t.size(); ++i)
        {
            const auto c = spectrum (static_cast<float> (i) / 255.0f);
            t[i] = juce::PixelARGB (255, c.getRed(), c.getGreen(), c.getBlue());
        }
        return t;
    }();
    return table;
}

/** Uppercase label text with the design's tracking. */
inline juce::Font labelFont() { return fonts::ui (labelSize, fonts::UiWeight::medium).withExtraKerningFactor (0.06f); }

} // namespace artfreq::ui::theme
