#pragma once

// The emblem: a capital A and F joined into one monogram. The A's right leg is the F's stem, the
// A's apex runs into the F's top arm and its crossbar carries on as the F's middle arm. It is
// solid on the left and dissolves, left to right, into horizontal lines in the spectrogram's
// colours with growing gaps between them: the way a drawing looks once it has become sound.

#include <juce_graphics/juce_graphics.h>

namespace artfreq::ui
{

/** Draws the emblem centred in area, as large as fits (it is a little wider than tall), with a
    transparent background. The number of lines follows the size in physical pixels, so it
    stays crisp at 20-26 px and gains detail when large. */
void drawEmblem (juce::Graphics& g, juce::Rectangle<float> area);

} // namespace artfreq::ui
