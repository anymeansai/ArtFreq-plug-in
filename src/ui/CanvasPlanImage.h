#pragma once

// The picture of a plan as an analyser would show it: each sine line at its height, coloured by
// its loudness (dB relative to the plan's loudest point, over a 60 dB window) with the
// spectrogram colour map. Kept separate from the canvas component so it can be tested and
// timed on its own.

#include "core/ArtPlan.h"

#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace freqart::ui
{

class CanvasPlanImage
{
public:
    static constexpr float windowDb = 60.0f;  // the quietest colour is this far below the loudest

    /** Lines are drawn this tall at most (logical px): fewer lines than fit show as scan lines,
        more merge into solid shapes. */
    static constexpr float maxLineThickness = 2.0f;

    /** The picture of `plan` (null: silence) on `scale`, `width` x `height` image pixels, where
        `pixelsPerLogical` image pixels make one logical pixel. Rebuilt only when one of these
        changes; otherwise the cached image is returned. */
    juce::Image get (const std::shared_ptr<const ArtPlan>& plan, const FrequencyScale& scale,
                     int width, int height, float pixelsPerLogical);

    /** Forgets the cached picture (the next get() rebuilds it). */
    void invalidate() noexcept { image = {}; }

    /** The colour-map index 0..255 for a linear amplitude relative to the loudest (exposed for
        tests). 0 for silence, 255 at the loudest, windowDb below it maps to 0. */
    static uint8_t levelFor (float amplitude, float loudest) noexcept;

private:
    void updateLevels (const ArtPlan& plan);
    void render (const ArtPlan* plan, const FrequencyScale& scale, int width, int height, float pixelsPerLogical);

    // What the cached image shows
    juce::Image image;
    const ArtPlan* imagePlan = nullptr;
    uint64_t imageVersion = 0;
    FrequencyScale imageScale;
    float imagePixelsPerLogical = 0.0f;

    // Colour-map levels of the plan: levels[line * numColumns + column]
    std::shared_ptr<const ArtPlan> levelsPlan;  // kept so the pointer cannot be reused
    std::vector<uint8_t> levels;
    std::vector<uint8_t> lineAudible;

    // Scratch, kept to avoid reallocating per rebuild
    struct Contribution
    {
        int line;
        float weight;
    };

    std::vector<Contribution> contributions;
    std::vector<int> rowStart;
    std::vector<uint8_t> rowDense;
    std::vector<int> columnOf, nextColumnOf;
    std::vector<float> columnFraction;
    std::vector<float> accumulator;
    std::vector<juce::PixelARGB> rowPixels;
};

/** Copies a row of opaque colours into an ARGB or RGB image (whatever its pixel layout). */
void writePixelRow (juce::Image::BitmapData& data, int y, const juce::PixelARGB* source, int width) noexcept;

} // namespace freqart::ui
