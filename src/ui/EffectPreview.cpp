#include "EffectPreview.h"

#include "CanvasPlanImage.h"
#include "Theme.h"
#include "dsp/StandInSong.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace artfreq::ui
{
namespace
{
    constexpr float silentDb = -200.0f;

    /** What adding a quieter sound to a louder one adds to the louder, in dB, by how far apart
        they are (in steps of a quarter dB, up to 40 dB): powers add, as in an analyser's cell. */
    constexpr int powerSteps = 160;

    const std::array<float, powerSteps + 1>& powerSumTable()
    {
        static const auto table = []
        {
            std::array<float, powerSteps + 1> t {};

            for (size_t i = 0; i < t.size(); ++i)
                t[i] = 10.0f * std::log10 (1.0f + std::pow (10.0f, -0.25f * static_cast<float> (i) / 10.0f));

            return t;
        }();

        return table;
    }

    inline float powerSum (float a, float b) noexcept
    {
        const auto louder = std::max (a, b);
        const auto apart = static_cast<int> (std::abs (a - b) * 4.0f + 0.5f);
        return apart >= powerSteps ? louder : louder + powerSumTable()[static_cast<size_t> (apart)];
    }

    /** The carver's frame (TunedCarver: about 85 ms at any rate), and its bins' width. */
    constexpr double carverFrameSeconds = 4096.0 / 48000.0;

    /** A symmetric box of `width` over each row of `values` (`width` x `height`), the parts
        beyond the ends reading `outside`. */
    void boxRows (float* values, int width, int height, int boxWidth, float outside, std::vector<double>& sums)
    {
        if (boxWidth <= 1)
            return;

        const auto before = boxWidth / 2, after = boxWidth - 1 - before;
        sums.resize (static_cast<size_t> (width) + 1);

        for (int y = 0; y < height; ++y)
        {
            auto* row = values + static_cast<size_t> (y) * static_cast<size_t> (width);
            sums[0] = 0.0;

            for (int x = 0; x < width; ++x)
                sums[static_cast<size_t> (x) + 1] = sums[static_cast<size_t> (x)] + row[x];

            for (int x = 0; x < width; ++x)
            {
                const auto from = x - before, to = x + after + 1;
                const auto inFrom = std::max (0, from), inTo = std::min (width, to);
                const auto outsideCount = (inFrom - from) + (to - inTo);
                const auto total = sums[static_cast<size_t> (inTo)] - sums[static_cast<size_t> (inFrom)] + outsideCount * static_cast<double> (outside);
                row[x] = static_cast<float> (std::max (0.0, total / boxWidth));
            }
        }
    }
} // namespace

float EffectPreviewImage::songDb (double x, double hz) noexcept
{
    return standInSongDb (x, hz);
}

void EffectPreviewImage::setMeasuredSong (std::shared_ptr<const MeasuredSpectrogram> newSong)
{
    if (newSong != nullptr && (newSong->input.empty() || newSong->frames <= 0 || newSong->rows <= 0))
        newSong = nullptr;

    if (newSong == measuredSong)
        return;

    measuredSong = std::move (newSong);
    songDirty = true;
}

void EffectPreviewImage::makeSong (const Settings& settings, const FrequencyScale& scale, int width, int height)
{
    if (! songDirty && width == songWidth && height == songHeight && songFft == settings.view.fftSize
        && songMeasured == (measuredSong != nullptr) && songScale == scale)
        return;

    songDirty = false;
    songWidth = width;
    songHeight = height;
    songFft = settings.view.fftSize;
    songScale = scale;
    songMeasured = measuredSong != nullptr;
    song.resize (static_cast<size_t> (width) * static_cast<size_t> (height));

    if (measuredSong != nullptr)
    {
        // The stand-in as the analyser really showed it (stretched to this size).
        const auto& a = *measuredSong;
        std::vector<int> frame0 (static_cast<size_t> (width)), frame1 (static_cast<size_t> (width));
        std::vector<float> fraction (static_cast<size_t> (width));

        for (int x = 0; x < width; ++x)
            MeasuredSpectrogram::columnPosition (x, width, a.frames, frame0[static_cast<size_t> (x)], frame1[static_cast<size_t> (x)],
                                                 fraction[static_cast<size_t> (x)]);

        for (int y = 0; y < height; ++y)
        {
            const auto row = juce::jlimit (0, a.rows - 1, static_cast<int> ((y + 0.5) * a.rows / height));
            auto* out = song.data() + static_cast<size_t> (y) * static_cast<size_t> (width);

            for (int x = 0; x < width; ++x)
            {
                const auto d0 = a.inputDb (row, frame0[static_cast<size_t> (x)]);
                const auto d1 = a.inputDb (row, frame1[static_cast<size_t> (x)]);
                out[x] = d0 + (d1 - d0) * fraction[static_cast<size_t> (x)];
            }
        }

        return;
    }

    // The level it is made with: a cell of noise reads 3 dB lower per doubling of the FFT (each
    // bin is half as wide), from the 4096 points its levels are given at.
    const auto offset = -10.0f * std::log10 (static_cast<float> (settings.view.fftSize) / 4096.0f);
    const auto& base = standInSongTexture();
    std::vector<int> columns (static_cast<size_t> (width)), rows (static_cast<size_t> (height));
    std::vector<float> columnFractions (static_cast<size_t> (width)), rowFractions (static_cast<size_t> (height));

    const auto steps = [] (int size, int baseSize, std::vector<int>& index, std::vector<float>& fraction)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto at = (i + 0.5) * baseSize / size - 0.5;
            const auto before = juce::jlimit (0, baseSize - 2, static_cast<int> (std::floor (at)));
            index[static_cast<size_t> (i)] = before;
            fraction[static_cast<size_t> (i)] = static_cast<float> (juce::jlimit (0.0, 1.0, at - before));
        }
    };

    steps (width, standInTextureWidth, columns, columnFractions);

    // The texture's rows run in log frequency: each canvas row reads the one at its height.
    const auto s = scale.sanitised();

    for (int y = 0; y < height; ++y)
    {
        const auto at = standInTextureRow (s.toHz (1.0 - (y + 0.5) / height));
        const auto before = juce::jmin (standInTextureHeight - 2, static_cast<int> (at));
        rows[static_cast<size_t> (y)] = before;
        rowFractions[static_cast<size_t> (y)] = static_cast<float> (juce::jlimit (0.0, 1.0, at - before));
    }

    for (int y = 0; y < height; ++y)
    {
        const auto* above = base.data() + static_cast<size_t> (rows[static_cast<size_t> (y)]) * static_cast<size_t> (standInTextureWidth);
        const auto* below = above + standInTextureWidth;
        const auto fy = rowFractions[static_cast<size_t> (y)];
        auto* out = song.data() + static_cast<size_t> (y) * static_cast<size_t> (width);

        for (int x = 0; x < width; ++x)
        {
            const auto i = static_cast<size_t> (columns[static_cast<size_t> (x)]);
            const auto fx = columnFractions[static_cast<size_t> (x)];
            const auto top = above[i] + (above[i + 1] - above[i]) * fx;
            const auto bottom = below[i] + (below[i + 1] - below[i]) * fx;
            out[x] = top + (bottom - top) * fy + offset;
        }
    }
}

void EffectPreviewImage::makeCarve (const MaskPlan* mask, const Settings& settings, const FrequencyScale& scale, int width, int height)
{
    const auto pixels = static_cast<size_t> (width) * static_cast<size_t> (height);
    carve.assign (pixels, 0.0f);

    if (mask == nullptr)
        return;

    const auto stencil = settings.mode == EffectMode::stencil;
    const auto depth = juce::jlimit (params::minDepthDb, 0.0f, settings.depthDb);

    // What the carver leaves of the song's power, pixel by pixel: the mask's nearest cell.
    std::vector<int> columnOf (static_cast<size_t> (width)), rowOf (static_cast<size_t> (height));

    for (int x = 0; x < width; ++x)
        columnOf[static_cast<size_t> (x)] = juce::jlimit (0, mask->numColumns - 1, static_cast<int> ((x + 0.5) * mask->numColumns / width));

    // The mask's rows run evenly over the scale from the bottom, as the canvas's do.
    for (int y = 0; y < height; ++y)
        rowOf[static_cast<size_t> (y)] = juce::jlimit (0, mask->numRows - 1, static_cast<int> ((1.0 - (y + 0.5) / height) * mask->numRows));

    std::array<float, 256> powerOf {};

    for (size_t i = 0; i < powerOf.size(); ++i)
        powerOf[i] = std::pow (10.0f, depth * static_cast<float> (i) / 255.0f / 10.0f);

    for (int y = 0; y < height; ++y)
    {
        auto* out = carve.data() + static_cast<size_t> (y) * static_cast<size_t> (width);
        const auto row = rowOf[static_cast<size_t> (y)];

        for (int x = 0; x < width; ++x)
        {
            const auto m = mask->column (columnOf[static_cast<size_t> (x)])[row];
            const auto amount = juce::jlimit (0.0f, 1.0f, stencil ? 1.0f - m : m);
            out[x] = powerOf[static_cast<size_t> (amount * 255.0f + 0.5f)];
        }
    }

    // In frequency: about a bin of the analyser and half a bin of the carver either way, as many
    // Hz at every height (many rows in the lows, under one in the highs). A sliding window whose
    // size follows the row, with sums in double: the powers run down to 1e-12.
    const auto s = scale.sanitised();
    const auto blurHz = settings.sampleRate / settings.view.fftSize + 0.5 / carverFrameSeconds;
    const auto h = static_cast<double> (height);
    std::vector<int> radius (static_cast<size_t> (height));

    for (int y = 0; y < height; ++y)
    {
        const auto hzPerRow = s.toHz (1.0 - y / h) - s.toHz (1.0 - (y + 1.0) / h);
        radius[static_cast<size_t> (y)] = juce::jlimit (0, height, static_cast<int> (std::lround (blurHz / std::max (1.0e-6, hzPerRow))));
    }

    if (std::any_of (radius.begin(), radius.end(), [] (int r) { return r > 0; }))
    {
        carveScratch.resize (pixels);
        runningSums.assign (static_cast<size_t> (width), 0.0);
        auto lo = 0, hi = -1;   // the rows in the window, inclusive

        for (int y = 0; y < height; ++y)
        {
            const auto r = radius[static_cast<size_t> (y)];
            const auto wantLo = std::max (0, y - r), wantHi = std::min (height - 1, y + r);
            const auto addRow = [&] (int row, double sign)
            {
                const auto* values = carve.data() + static_cast<size_t> (row) * static_cast<size_t> (width);

                for (int x = 0; x < width; ++x)
                    runningSums[static_cast<size_t> (x)] += sign * values[x];
            };

            while (hi < wantHi) addRow (++hi, 1.0);
            while (lo > wantLo) addRow (--lo, 1.0);
            while (lo < wantLo) addRow (lo++, -1.0);
            while (hi > wantHi) addRow (hi--, -1.0);

            const auto count = static_cast<double> (hi - lo + 1);
            auto* out = carveScratch.data() + static_cast<size_t> (y) * static_cast<size_t> (width);

            for (int x = 0; x < width; ++x)
                out[x] = static_cast<float> (std::max (1.0e-13, runningSums[static_cast<size_t> (x)] / count));
        }

        carve.swap (carveScratch);
    }

    // In time: half of the analyser's window and a quarter of the carver's frame, twice (a
    // triangle). Outside the pass the song is not carved.
    const auto boxSeconds = 0.5 * settings.view.fftSize / settings.sampleRate + 0.25 * carverFrameSeconds;
    const auto boxWidth = static_cast<int> (std::lround (boxSeconds / std::max (1.0e-6, settings.passSeconds) * width));
    boxRows (carve.data(), width, height, boxWidth, 1.0f, runningSums);
    boxRows (carve.data(), width, height, boxWidth, 1.0f, runningSums);

    // To dB (a fast log: this runs on every drag step).
    for (auto& value : carve)
    {
        // value = m 2^(bits - 1) with m in [1, 2); the polynomial is log2 (m) + 1 to within 0.005.
        auto bits = 0;
        const auto m = 2.0f * std::frexp (std::max (1.0e-13f, value), &bits);
        value = 3.0103f * (static_cast<float> (bits - 2) + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f);
    }
}

juce::Image EffectPreviewImage::get (const std::vector<float>& sinesDb, float levelDb, uint64_t sinesKey, const Settings& settingsIn,
                                     const std::shared_ptr<const MaskPlanSet>& masks, const FrequencyScale& scale, int width, int height)
{
    width = juce::jmax (1, width);
    height = juce::jmax (1, height);

    auto settings = settingsIn;
    settings.view = settings.view.sanitised();

    // The sines only show in the modes that play them.
    if (! playsSines (settings.mode))
    {
        sinesKey = 0;
        levelDb = 0.0f;
    }

    if (image.isValid() && image.getWidth() == width && image.getHeight() == height && imageSinesKey == sinesKey
        && juce::exactlyEqual (imageLevelDb, levelDb) && imageSettings == settings && imageScale == scale
        && imageMasks == masks && ! songDirty)
        return image;

    imageSinesKey = sinesKey;
    imageLevelDb = levelDb;
    imageSettings = settings;
    imageScale = scale;
    imageMasks = masks;

    // Worked out at the plan's and the masks' columns at most, and stretched across the width.
    const auto work = CanvasPlanImage::workWidthFor (width);
    makeSong (settings, scale, work, height);

    // The mask this mode carves with (Carve + Draw's is wider), or none.
    const MaskPlan* mask = nullptr;

    if (masks != nullptr && carvesAudio (settings.mode))
        mask = settings.mode == EffectMode::carveDraw ? masks->halo.get() : masks->exact.get();

    if (mask != nullptr && (mask->empty || mask->numColumns <= 0 || mask->numRows <= 0))
        mask = nullptr;

    // The carve depends on neither the sines nor the colours: kept while only those change.
    if (! carveValid || mask != carveMask || masks != carveMasks || carveWidth != work || carveHeight != height
        || carveScale != scale || carveSettings.mode != settings.mode || ! juce::exactlyEqual (carveSettings.depthDb, settings.depthDb)
        || carveSettings.view.fftSize != settings.view.fftSize || ! juce::exactlyEqual (carveSettings.sampleRate, settings.sampleRate)
        || ! juce::exactlyEqual (carveSettings.passSeconds, settings.passSeconds))
    {
        makeCarve (mask, settings, scale, work, height);
        carveValid = true;
        carveMask = mask;
        carveMasks = masks;
        carveSettings = settings;
        carveScale = scale;
        carveWidth = work;
        carveHeight = height;
    }

    // (A picture still held elsewhere, the canvas's last one, is left alone: a new one is made.)
    if (! image.isValid() || image.getWidth() != width || image.getHeight() != height || image.getReferenceCount() > 1)
        image = juce::Image (juce::Image::RGB, width, height, false);

    const auto withSines = playsSines (settings.mode) && sinesDb.size() == static_cast<size_t> (work) * static_cast<size_t> (height);
    const auto& table = theme::spectrumTable();
    const auto floorDb = settings.view.floorDb;
    const auto perDb = 255.0f / (settings.view.ceilingDb - floorDb);
    CanvasPlanImage::stretchSteps (work, width, stretchBefore, stretchFraction);

    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    rowPixels.resize (static_cast<size_t> (width));
    rowLevels.resize (static_cast<size_t> (work));

    for (int y = 0; y < height; ++y)
    {
        const auto offset = static_cast<size_t> (y) * static_cast<size_t> (work);

        for (int x = 0; x < work; ++x)
        {
            auto level = song[offset + static_cast<size_t> (x)] + carve[offset + static_cast<size_t> (x)];

            // The sines added to what is left of the song, as powers (where both are about as
            // loud, the cell is 3 dB brighter than either).
            if (withSines)
                if (const auto sine = sinesDb[offset + static_cast<size_t> (x)]; sine > silentDb + 1.0f)
                    level = powerSum (level, sine + levelDb);

            rowLevels[static_cast<size_t> (x)] = level;
        }

        for (int x = 0; x < width; ++x)
        {
            const auto b = static_cast<size_t> (stretchBefore[static_cast<size_t> (x)]);
            const auto l0 = rowLevels[b];
            const auto l1 = rowLevels[std::min (static_cast<size_t> (work - 1), b + 1)];
            const auto index = (l0 + (l1 - l0) * stretchFraction[static_cast<size_t> (x)] - floorDb) * perDb + 0.5f;
            rowPixels[static_cast<size_t> (x)] = table[static_cast<size_t> (index <= 0.0f ? 0 : index >= 255.0f ? 255 : static_cast<int> (index))];
        }

        writePixelRow (data, y, rowPixels.data(), width);
    }

    return image;
}

} // namespace artfreq::ui
