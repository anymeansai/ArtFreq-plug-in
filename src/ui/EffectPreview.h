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
//
// The sines are drawn at their own level against the stand-in, Level included, and added to it as
// powers (as in an analyser's cell), so the canvas shows how far they stand out of a song: the
// window's top is a full-scale sine in one analyser cell, and the stand-in sits about where a
// loud master (-10 dBFS RMS) shows on an analyser of 4096 points (about -32 dB a cell at 1 kHz;
// pink noise at -10 dBFS RMS: -34). The sines are drawn sinesCalibrationDb above their plan
// amplitude, which makes the canvas show what the analyser shows: measured through the
// processor, the default drawing at the default Level on pink noise at -10 dBFS RMS stands
// +5.3 dB above the song in Draw and +9.4 dB in Carve + Draw (drawn cells against the others,
// 400 Hz - 2.5 kHz, FFT 4096), and the canvas the same (+5.4 / +9.6). (A sine's energy spreads
// over neighbouring cells, where the drawing's lines add up, and an analyser's noise reads below
// its mean power, which the stand-in's smooth texture does not.) At -60 dB the sines are gone.

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
        changes whenever it does; its top level (255) stands for sines at `sinesDb` (dB against
        a full-scale sine: the plan's loudest amplitude with the output Level), so 0 draws them at
        the top of the window. Rebuilt only when something changed; otherwise the cached image is
        returned. */
    juce::Image get (const std::vector<uint8_t>& planLevels, uint64_t planKey, EffectMode mode, float depthDb,
                     float sinesDb, const std::shared_ptr<const MaskPlanSet>& masks, int width, int height);

    /** sinesDb for a plan's loudest amplitude (CanvasPlanImage::getLoudestAmplitude) played at an
        output gain (linear), as an analyser shows it against the stand-in. */
    static float sinesDbFor (float loudestAmplitude, float outputGain) noexcept;

    /** How much brighter than its plan amplitude a sine shows against the stand-in (see above). */
    static constexpr float sinesCalibrationDb = 10.0f;

    /** The stand-in song's level at a place on the canvas, in dB below its loudest moment (x 0..1
        along the length, y 0..1 from the bottom of the scale to the top). */
    static float songDb (double x, double y) noexcept;

private:
    void makeTexture (int width, int height);

    juce::Image image;
    uint64_t imagePlanKey = 0;
    EffectMode imageMode = EffectMode::carve;
    float imageDepth = 1.0f;
    float imageSinesDb = 1.0f;
    std::shared_ptr<const MaskPlanSet> imageMasks;

    std::vector<float> texture;   // the stand-in, as colour-map levels (0..255, not rounded)
    int textureWidth = 0, textureHeight = 0;
    std::vector<int> columnOf, rowOf;
    std::vector<juce::PixelARGB> rowPixels;
};

} // namespace freqart::ui
