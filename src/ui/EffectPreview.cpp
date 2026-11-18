#include "EffectPreview.h"

#include "CanvasPlanImage.h"
#include "Theme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace artfreq::ui
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

    /** What adding a quieter sound to a louder one adds to the louder, in colour levels, by how
        far apart they are in levels (0..255): powers add, as in an analyser's cell. */
    const std::array<float, 256>& powerSumTable()
    {
        static const auto table = []
        {
            std::array<float, 256> t {};

            for (size_t i = 0; i < t.size(); ++i)
                t[i] = 10.0f * std::log10 (1.0f + std::pow (10.0f, -static_cast<float> (i) / levelsPerDb / 10.0f)) * levelsPerDb;

            return t;
        }();

        return table;
    }
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

namespace
{
    /** The stand-in is worked out once, at this size, and resampled to whatever size the canvas
        has. Worked out pixel by pixel at the canvas's size it cost 50 to 200 ms on a Retina
        display whenever that size changed (every step of a corner drag), most of the canvas's
        cost. Its finest details are a few texels wide here: a hat (a 1/18 beat) 2.7, a held
        note (1 % of the height) 3.8, the grain's grid (192 x 128) 4 and 3. */
    constexpr int baseWidth = 768, baseHeight = 384;

    /** The stand-in at baseWidth x baseHeight, as colour-map levels (row 0 at the top). */
    const std::vector<float>& baseTexture()
    {
        static const auto base = []
        {
            std::vector<float> t (static_cast<size_t> (baseWidth) * static_cast<size_t> (baseHeight));

            for (int y = 0; y < baseHeight; ++y)
            {
                const auto up = 1.0 - (y + 0.5) / baseHeight;

                for (int x = 0; x < baseWidth; ++x)
                    t[static_cast<size_t> (y) * static_cast<size_t> (baseWidth) + static_cast<size_t> (x)]
                        = (CanvasPlanImage::windowDb + EffectPreviewImage::songDb ((x + 0.5) / baseWidth, up)) * levelsPerDb;
            }

            return t;
        }();

        return base;
    }

    /** For each of `size` pixels, the texel before its centre (of `baseSize`) and how far on to
        the next it lies, for a linear resample with the texels' centres where they were sampled. */
    void resampleSteps (int size, int baseSize, std::vector<int>& index, std::vector<float>& fraction)
    {
        index.resize (static_cast<size_t> (size));
        fraction.resize (static_cast<size_t> (size));

        for (int i = 0; i < size; ++i)
        {
            const auto at = (i + 0.5) * baseSize / size - 0.5;
            const auto before = juce::jlimit (0, baseSize - 2, static_cast<int> (std::floor (at)));
            index[static_cast<size_t> (i)] = before;
            fraction[static_cast<size_t> (i)] = static_cast<float> (juce::jlimit (0.0, 1.0, at - before));
        }
    }
} // namespace

void EffectPreviewImage::makeTexture (int width, int height)
{
    if (width == textureWidth && height == textureHeight)
        return;

    textureWidth = width;
    textureHeight = height;
    texture.resize (static_cast<size_t> (width) * static_cast<size_t> (height));

    const auto& base = baseTexture();
    std::vector<int> columns, rows;
    std::vector<float> columnFractions, rowFractions;
    resampleSteps (width, baseWidth, columns, columnFractions);
    resampleSteps (height, baseHeight, rows, rowFractions);

    for (int y = 0; y < height; ++y)
    {
        const auto* above = base.data() + static_cast<size_t> (rows[static_cast<size_t> (y)]) * static_cast<size_t> (baseWidth);
        const auto* below = above + baseWidth;
        const auto fy = rowFractions[static_cast<size_t> (y)];
        auto* out = texture.data() + static_cast<size_t> (y) * static_cast<size_t> (width);

        for (int x = 0; x < width; ++x)
        {
            const auto i = static_cast<size_t> (columns[static_cast<size_t> (x)]);
            const auto fx = columnFractions[static_cast<size_t> (x)];
            const auto top = above[i] + (above[i + 1] - above[i]) * fx;
            const auto bottom = below[i] + (below[i + 1] - below[i]) * fx;
            out[x] = top + (bottom - top) * fy;
        }
    }
}

float EffectPreviewImage::sinesDbFor (float loudestAmplitude, float outputGain) noexcept
{
    const auto amplitude = loudestAmplitude * outputGain;
    return amplitude > 0.0f && std::isfinite (amplitude) ? 20.0f * std::log10 (amplitude) + sinesCalibrationDb : -200.0f;
}

juce::Image EffectPreviewImage::get (const std::vector<uint8_t>& planLevels, uint64_t planKey, EffectMode mode, float depthDb,
                                     float sinesDb, const std::shared_ptr<const MaskPlanSet>& masks, int width, int height)
{
    width = juce::jmax (1, width);
    height = juce::jmax (1, height);
    sinesDb = std::isfinite (sinesDb) ? juce::jlimit (-200.0f, 60.0f, sinesDb) : -200.0f;

    // The sines only show in the modes that play them.
    if (! playsSines (mode))
        sinesDb = -200.0f;

    if (image.isValid() && image.getWidth() == width && image.getHeight() == height && imagePlanKey == planKey
        && imageMode == mode && juce::exactlyEqual (imageDepth, depthDb) && juce::exactlyEqual (imageSinesDb, sinesDb)
        && imageMasks == masks)
        return image;

    imagePlanKey = planKey;
    imageMode = mode;
    imageDepth = depthDb;
    imageSinesDb = sinesDb;
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
    // A plan level of 255 is the loudest sine, sinesDb from the top of the window.
    const auto sinesOffset = sinesDb * levelsPerDb;
    const auto withSines = playsSines (mode) && sinesOffset > -255.0f
                        && planLevels.size() == static_cast<size_t> (width) * static_cast<size_t> (height);
    const auto& table = theme::spectrumTable();
    const auto& sum = powerSumTable();

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

            // The sines added to what is left of the song, as powers (where both are about as
            // loud, the cell is 3 dB brighter than either).
            if (withSines)
            {
                if (const auto sine = planLevels[offset + static_cast<size_t> (x)]; sine > 0)
                {
                    if (const auto sineLevel = static_cast<float> (sine) + sinesOffset; sineLevel > 0.0f)
                    {
                        const auto louder = std::max (level, sineLevel);
                        const auto apart = juce::jlimit (0, 255, static_cast<int> (std::abs (level - sineLevel) + 0.5f));
                        level = louder + sum[static_cast<size_t> (apart)];
                    }
                }
            }

            const auto index = juce::jlimit (0, 255, static_cast<int> (level + 0.5f));
            rowPixels[static_cast<size_t> (x)] = table[static_cast<size_t> (index)];
        }

        writePixelRow (data, y, rowPixels.data(), width);
    }

    return image;
}

} // namespace artfreq::ui
