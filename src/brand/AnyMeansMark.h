#pragma once

// The AnyMeans mark: a capital A and M joined into one monogram, built like the ArtFreq emblem
// (src/ui/Emblem.h). The A is the emblem's A; its right leg is the M's first stem, as the
// emblem's is the F's; the M's V and right stem follow. It is solid on the left and dissolves,
// left to right, into horizontal lines in the spectrogram's colours with growing gaps between
// them, so the M ends as sound.
//
// Self-contained (only juce_graphics): the palette is copied here, not taken from ArtFreq's
// theme, so the file can go into any AnyMeans project. docs/brand/ holds the exported files.

#include <juce_graphics/juce_graphics.h>

namespace anymeans::brand
{

enum class Variant
{
    onDark,     // white letters, white-hot lines with yellow / orange / magenta edges
    onLight,    // ink letters, ink lines warming to plum / magenta with orange edges
    whiteOnly,  // one colour, for print, embossing, photos and coloured grounds
    inkOnly
};

/** The colours the mark and its wordmark use. */
namespace colours
{
    inline const juce::Colour ink        { 0xff0b0d12 };    // letters on light, the dark ground
    inline const juce::Colour paper      { 0xffffffff };
    inline const juce::Colour wordOnDark { 0xffe8ebf2 };    // the wordmark on dark grounds
    inline const juce::Colour yellow     { 0xfffce7a0 };    // the spectrogram's hot end...
    inline const juce::Colour orange     { 0xfff9a13a };
    inline const juce::Colour coral      { 0xffe8664a };
    inline const juce::Colour magenta    { 0xffb83a6e };
    inline const juce::Colour plum       { 0xff7a1f7f };    // ...and its warm middle
}

/** The mark's width over its height (the cap height). */
float markAspect() noexcept;

/** Draws the mark centred in area, as large as fits, with a transparent background. The
    number of lines follows the size in physical pixels, so it stays crisp as a 16 px favicon
    and gains detail when large. */
void drawAnyMeansMark (juce::Graphics& g, juce::Rectangle<float> area, Variant variant);

/** Where drawAnyMeansMark puts the mark's letters inside area (they snap to whole lines, so
    they can be a little smaller than area), for lining up a wordmark beside it. */
juce::Rectangle<float> markBounds (juce::Graphics& g, juce::Rectangle<float> area);

/** The mark as SVG markup, drawn into area (in the SVG's own units) with a fixed number of
    lines, since a vector file has no pixel size to adapt to. defs goes inside <defs>, body
    where the mark is drawn; every id starts with idPrefix, so several marks can share a page. */
struct SvgMark
{
    juce::String defs, body;
    juce::Rectangle<float> bounds;     // the letters
};

SvgMark markSvg (juce::Rectangle<float> area, Variant variant, int lines, const juce::String& idPrefix);

/** A whole SVG file of the mark alone: transparent, its view box tight around the letters,
    whose cap height is 100 units. */
juce::String markSvgFile (Variant variant, int lines);

/** A path as SVG path data (M, L, Q, C and Z commands), after transform. */
juce::String svgPathData (const juce::Path& path, const juce::AffineTransform& transform = {});

/** A colour as #rrggbb. */
juce::String svgColour (juce::Colour colour);

} // namespace anymeans::brand
