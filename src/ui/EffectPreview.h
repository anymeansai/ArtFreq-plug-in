#pragma once

// FreqArt FX's canvas: what an analyser will show of the effect's output in each mode.
//
// The effect works on a song the canvas does not have, so a stand-in is drawn under the drawing:
// a texture with a song's shape (louder lows, a beat, a few held notes, grain), carved by the
// same masks the audio thread carves with, as deep as Depth:
//
//  - Carve: the stand-in with the drawing as dark shapes;
//  - Stencil: the drawing filled with the stand-in, the rest dark;
//  - Carve + Draw: the stand-in with a dark hole a little larger than the drawing (the halo
//    masks) and the drawing's sines bright in it;
//  - Draw: the stand-in with the drawing's sines on top.
//
// Levels use the canvas's colour window (CanvasPlanImage::windowDb): a carve of 60 dB or more is
// black. Nothing drawn (or everything hidden) carves nothing, as in the effect.

#include "Parameters.h"
#include "core/MaskPlan.h"

#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace freqart::ui
{

class EffectPreviewImage
{
public:
    /** The picture for `mode`, `width` x `height` pixels. `planLevels` is the sine plan's picture
        as colour-map levels, row by row (CanvasPlanImage::getPixelLevels), and `planKey`
        changes whenever it does. Rebuilt only when something changed; otherwise the cached
        image is returned. */
    juce::Image get (const std::vector<uint8_t>& planLevels, uint64_t planKey, EffectMode mode, float depthDb,
                     const std::shared_ptr<const MaskPlanSet>& masks, int width, int height);

    /** The stand-in song's level at a place on the canvas, in dB below its loudest moment (x 0..1
        along the length, y 0..1 from the bottom of the scale to the top). */
    static float songDb (double x, double y) noexcept;

private:
    void makeTexture (int width, int height);

    juce::Image image;
    uint64_t imagePlanKey = 0;
    EffectMode imageMode = EffectMode::carve;
    float imageDepth = 1.0f;
    std::shared_ptr<const MaskPlanSet> imageMasks;

    std::vector<float> texture;   // the stand-in, as colour-map levels (0..255, not rounded)
    int textureWidth = 0, textureHeight = 0;
    std::vector<int> columnOf, rowOf;
    std::vector<juce::PixelARGB> rowPixels;
};

} // namespace freqart::ui
