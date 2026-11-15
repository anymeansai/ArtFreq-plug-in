#pragma once

// The typefaces embedded in the plug-in (all SIL Open Font License, see assets/fonts).
//
// UI text uses Inter; numbers use DM Mono. Text drawn INTO the spectrogram uses one of the
// "art" fonts, chosen per text object.

#include <juce_graphics/juce_graphics.h>

namespace artfreq::fonts
{

enum class UiWeight
{
    regular,
    medium,
    semiBold,
    bold
};

/** Inter at the given weight and height (in points / logical pixels). */
juce::Font ui (float height, UiWeight weight = UiWeight::regular);

/** DM Mono (tabular digits) for read-outs. */
juce::Font mono (float height, bool medium = false);

/** The fonts offered for text in the spectrogram, in menu order:
      0 "Inter Bold", 1 "Space Grotesk", 2 "Jost", 3 "Fraunces", 4 "DM Mono". */
int numArtFonts() noexcept;
juce::String artFontName (int index);

/** The art font at the given height. Out-of-range indices give font 0. */
juce::Font art (int index, float height);

/** The typeface behind an art font (for building glyph outlines). Never null: falls back to
    the default sans-serif if an embedded face cannot be loaded. */
juce::Typeface::Ptr artTypeface (int index);

} // namespace artfreq::fonts
