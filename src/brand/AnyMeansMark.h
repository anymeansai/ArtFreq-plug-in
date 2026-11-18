#pragma once

// The AnyMeans mark: a capital A and M joined into one monogram, built like the ArtFreq emblem
// (src/ui/Emblem.h). The A's right leg is the M's first stem; the M's V and right stem follow.
// It is solid on the left and dissolves, left to right, into horizontal lines in the
// spectrogram's colours with growing gaps between them, so the M ends as sound.
//
// Self-contained (only juce_graphics): the palette is copied here, not taken from ArtFreq's
// theme, so the file can go into any AnyMeans project.

#include <juce_graphics/juce_graphics.h>

namespace anymeans::brand
{

enum class Variant
{
    onDark,     // white letters, white-hot lines with yellow / orange / magenta edges
    onLight     // ink letters, ink lines with purple / magenta / orange edges
};

/** The mark's width over its height (the cap height). */
float markAspect() noexcept;

/** Draws the mark centred in area, as large as fits, with a transparent background. The
    number of lines follows the size in physical pixels, so it stays crisp as a 16 px favicon
    and gains detail when large. */
void drawAnyMeansMark (juce::Graphics& g, juce::Rectangle<float> area, Variant variant);

/** Where drawAnyMeansMark puts the mark's letters inside area (they snap to whole lines, so
    they can be a little smaller than area), for lining up a wordmark beside it. */
juce::Rectangle<float> markBounds (juce::Graphics& g, juce::Rectangle<float> area);

} // namespace anymeans::brand
