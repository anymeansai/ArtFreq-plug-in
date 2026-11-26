#pragma once

// Text drawn into the spectrogram.

#include <juce_graphics/juce_graphics.h>

namespace artfreq
{

enum class TextStyle
{
    solid = 0,    // filled letters
    outline = 1   // only the letters' outlines: the outer half of a stroke 7 % of the area's height
                  // wide (min 3 px, so at least 1.5 px shows), the insides of the letters silent
};

/** The outline of the text in the given art font, at an arbitrary size, origin at the top left
    of its bounds. Several lines are allowed (separated by '\n'); they are centred on each
    other. Empty or whitespace-only text gives an empty path. */
juce::Path makeTextPath (const juce::String& text, int fontIndex);

/** Draws the text stretched to fill `area` (inset by 4 % of its height on every side, so the
    letters do not touch the edges of their box) into `target`, which must be a
    SingleChannel image. Pixel values are 0 (silent) to 255 (loudest), anti-aliased, combined
    with what is already in the image by taking the maximum. `area` may extend past the image;
    the drawing is clipped.

    `turn`, if given, moves the letters after they are fitted to `area` (in the target's
    pixels): a rotated object's text is fitted to its unrotated box and then turned (see
    rotationTransform in Compositor.h). The outline's stroke is drawn after the turn, so it
    keeps its width. Without it (the identity) the result is exactly as before rotation. */
void renderTextInto (juce::Image& target, juce::Rectangle<float> area,
                     const juce::String& text, int fontIndex, TextStyle style,
                     const juce::AffineTransform& turn = {});

} // namespace artfreq
