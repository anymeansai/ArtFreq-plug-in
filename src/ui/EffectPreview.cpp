#include "EffectPreview.h"

#include "CanvasPlanImage.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace freqart::ui
{
namespace
{
    /** A repeatable value 0..1 for a pair of integers: the stand-in's grain. */
    float hash (int a, int b) noexcept
    {
        auto h = static_cast<uint32_t> (a) * 73856093u ^ static_cast<uint32_t> (b) * 19349663u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return static_cast<float> (h & 0xffffu) / 65535.0f;
    }

    constexpr float levelsPerDb = 255.0f / CanvasPlanImage::windowDb;
} // namespace

float EffectPreviewImage::songDb (double x, double y) noexcept
{
    // A spectrum that falls towards the highs, as a mix's does on a log scale, well below the
    // loudest colours (the sines of the Draw modes are drawn at their own level over it).
    auto db = -22.0 - 18.0 * y;

    // Sixteen beats across the length: a kick in the lows on the beat, a hat in the highs on the
    // off-beat, each decaying.
    const auto beats = x * 16.0;
    const auto onBeat = beats - std::floor (beats);
    const auto offBeat = beats + 0.5 - std::floor (beats + 0.5);

    if (y < 0.35)
        db += 9.0 * std::exp (-7.0 * onBeat) * (1.0 - y / 0.35);

    if (y > 0.6)
        db += 7.0 * std::exp (-18.0 * offBeat) * (y - 0.6) / 0.4;

    // Held notes: five partials, a new chord every four beats.
    const auto chord = static_cast<int> (std::floor (beats / 4.0));

    for (int k = 0; k < 5; ++k)
    {
        const auto centre = 0.30 + 0.06 * k + 0.03 * static_cast<double> (hash (chord, k));
        const auto distance = std::abs (y - centre);

        if (distance < 0.005)
            db += 8.0 * (1.0 - distance / 0.005);
    }

    // Grain, smooth over a grid of the canvas (so it does not shimmer when the window resizes).
    const auto gx = x * 192.0, gy = y * 128.0;
    const auto ix = static_cast<int> (std::floor (gx)), iy = static_cast<int> (std::floor (gy));
    const auto fx = static_cast<float> (gx - ix), fy = static_cast<float> (gy - iy);
    const auto top = hash (ix, iy + 1) + (hash (ix + 1, iy + 1) - hash (ix, iy + 1)) * fx;
    const auto bottom = hash (ix, iy) + (hash (ix + 1, iy) - hash (ix, iy)) * fx;
    db += 8.0 * (static_cast<double> (bottom + (top - bottom) * fy) - 0.5);

    return static_cast<float> (std::min (0.0, db));
}

void EffectPreviewImage::makeTexture (int width, int height)
{
    if (width == textureWidth && height == textureHeight)
        return;

    textureWidth = width;
    textureHeight = height;
    texture.resize (static_cast<size_t> (width) * static_cast<size_t> (height));

    for (int y = 0; y < height; ++y)
    {
        const auto up = 1.0 - (y + 0.5) / height;

        for (int x = 0; x < width; ++x)
        {
            const auto level = (CanvasPlanImage::windowDb + songDb ((x + 0.5) / width, up)) * levelsPerDb;
            texture[static_cast<size_t> (y) * static_cast<size_t> (width) + static_cast<size_t> (x)] = level;
        }
    }
}

juce::Image EffectPreviewImage::get (const std::vector<uint8_t>& planLevels, uint64_t planKey, EffectMode mode, float depthDb,
                                     const std::shared_ptr<const MaskPlanSet>& masks, int width, int height)
{
    width = juce::jmax (1, width);
    height = juce::jmax (1, height);

    if (image.isValid() && image.getWidth() == width && image.getHeight() == height && imagePlanKey == planKey
        && imageMode == mode && juce::exactlyEqual (imageDepth, depthDb) && imageMasks == masks)
        return image;

    imagePlanKey = planKey;
    imageMode = mode;
    imageDepth = depthDb;
    imageMasks = masks;

    makeTexture (width, height);

    if (! image.isValid() || image.getWidth() != width || image.getHeight() != height)
        image = juce::Image (juce::Image::RGB, width, height, false);

    // The mask this mode carves with (Carve + Draw's is wider), or none.
    const MaskPlan* mask = nullptr;

    if (masks != nullptr && carvesAudio (mode))
        mask = mode == EffectMode::carveDraw ? masks->halo.get() : masks->exact.get();

    if (mask != nullptr && (mask->empty || mask->numColumns <= 0 || mask->numRows <= 0))
        mask = nullptr;

    if (mask != nullptr)
    {
        columnOf.resize (static_cast<size_t> (width));
        rowOf.resize (static_cast<size_t> (height));

        for (int x = 0; x < width; ++x)
            columnOf[static_cast<size_t> (x)] = juce::jlimit (0, mask->numColumns - 1, static_cast<int> ((x + 0.5) * mask->numColumns / width));

        // The mask's rows run evenly over the scale from the bottom, as the canvas's do.
        for (int y = 0; y < height; ++y)
            rowOf[static_cast<size_t> (y)] = juce::jlimit (0, mask->numRows - 1, static_cast<int> ((1.0 - (y + 0.5) / height) * mask->numRows));
    }

    const auto stencil = mode == EffectMode::stencil;
    const auto perUnit = juce::jlimit (params::minDepthDb, 0.0f, depthDb) * levelsPerDb;   // levels per unit of mask
    const auto withSines = playsSines (mode) && planLevels.size() == static_cast<size_t> (width) * static_cast<size_t> (height);
    const auto& table = theme::spectrumTable();

    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    rowPixels.resize (static_cast<size_t> (width));

    for (int y = 0; y < height; ++y)
    {
        const auto offset = static_cast<size_t> (y) * static_cast<size_t> (width);
        const auto* song = texture.data() + offset;

        for (int x = 0; x < width; ++x)
        {
            auto level = song[x];

            if (mask != nullptr)
            {
                const auto m = mask->column (columnOf[static_cast<size_t> (x)])[rowOf[static_cast<size_t> (y)]];
                level += (stencil ? 1.0f - m : m) * perUnit;
            }

            auto index = juce::jlimit (0, 255, static_cast<int> (level + 0.5f));

            // The sines, where they are louder than what is left of the song.
            if (withSines)
                index = std::max (index, static_cast<int> (planLevels[offset + static_cast<size_t> (x)]));

            rowPixels[static_cast<size_t> (x)] = table[static_cast<size_t> (index)];
        }

        writePixelRow (data, y, rowPixels.data(), width);
    }

    return image;
}

} // namespace freqart::ui
