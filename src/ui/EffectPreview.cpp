#include "EffectPreview.h"

#include "CanvasPlanImage.h"
#include "PluginProcessor.h"
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

    /** Up to here the carve is modelled bin by bin through the analyser's lobe (makeCarve). */
    constexpr double lobeModelTopHz = 1500.0;

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
    ++songVersion;
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

void EffectPreviewImage::makeCarve (const MaskPlan* mask, const Settings& settings, const FrequencyScale& scaleIn, int width, int height)
{
    // What the carver leaves of the song, as the analyser shows it: per pixel, the share of the
    // song's power left (as dB to add to the song), modelled on what TunedCarver and the analyser
    // really do, so a carve looks as soft and as filled-in as it will (in the lows above all):
    //
    //  1. the carver's bins (sampleRate / its FFT: 11.7 Hz): per bin, the drawing's mean over the
    //     bin's band (Stencil: its maximum, and everything not drawn goes), each carved bin
    //     widened by the mode's halo bins (1, Carve + Draw 3), gain = Depth ^ amount;
    //  2. the analyser's window spreading the song across frequency: in the lows and low mids,
    //     where a row is narrower than two of its bins, each bin reads the song's power around it
    //     through the window's main lobe and first side lobes, carved as it is there (a hole
    //     narrower than the lobe fills in, from the song's own loud lines above all); higher up a
    //     row reads its loudest bin, so the least carved bin inside it;
    //  3. in time, the analyser's window and a quarter of the carver's frame (thin strokes in
    //     time come out faint).
    const auto pixels = static_cast<size_t> (width) * static_cast<size_t> (height);
    carve.assign (pixels, 0.0f);

    if (mask == nullptr)
        return;

    const auto scale = scaleIn.sanitised();
    const auto stencil = settings.mode == EffectMode::stencil;
    const auto halo = stencil ? 0 : (settings.mode == EffectMode::carveDraw ? ArtFreqProcessor::carveDrawHaloBins : 1);
    const auto depth = juce::jlimit (params::minDepthDb, 0.0f, settings.depthDb);
    const auto rate = settings.sampleRate;
    const auto h = static_cast<double> (height);

    // The carver's FFT at this rate (TunedCarver::prepareCarver: about 85 ms, whole hops, the
    // next power of two).
    const auto frame = std::max (64, juce::roundToInt (carverFrameSeconds * rate / 4.0) * 4);
    auto carverFft = 16;

    while (carverFft < frame)
        carverFft *= 2;

    const auto binHz = rate / carverFft;
    const auto numBins = carverFft / 2 + 1;

    // The bins' bands in the mask's rows (as the carver looks them up), and how many reach the
    // canvas's range.
    binEdgeRows.resize (static_cast<size_t> (numBins) + 1);

    for (int edge = 0; edge <= numBins; ++edge)
        binEdgeRows[static_cast<size_t> (edge)] = mask->rowAt (std::max (0.0, (edge - 0.5) * binHz));

    const auto top = static_cast<double> (mask->numRows) - 0.5;
    auto binsInRange = numBins;

    while (binsInRange > 0 && binEdgeRows[static_cast<size_t> (binsInRange - 1)] >= top)
        --binsInRange;

    // Per bin, its band among the mask's cells, as TunedCarver::columnAmounts reads it (the share
    // outside the canvas's range counts as nothing drawn).
    struct Band
    {
        int firstCell = 0, lastCell = -1;      // lastCell < 0: the band misses the canvas
        double fromFraction = 0.0, toFraction = 0.0, perRow = 0.0;
    };

    std::vector<Band> bands (static_cast<size_t> (binsInRange));
    const auto rows = mask->numRows;

    for (int k = 0; k < binsInRange; ++k)
    {
        const auto low = binEdgeRows[static_cast<size_t> (k)], high = binEdgeRows[static_cast<size_t> (k) + 1];
        const auto from = std::max (low, -0.5), to = std::min (high, top);

        if (! (to > from && high > low))
            continue;

        auto& band = bands[static_cast<size_t> (k)];
        band.firstCell = juce::jlimit (0, rows - 1, static_cast<int> (std::floor (from + 0.5)));
        band.lastCell = juce::jlimit (band.firstCell, rows - 1, static_cast<int> (std::ceil (to - 0.5)));
        band.fromFraction = from + 0.5 - band.firstCell;
        band.toFraction = to + 0.5 - band.lastCell;
        band.perRow = 1.0 / (high - low);
    }

    // The analyser's bins, up to where a row spans two of them; each row's place among them, or
    // the carver's bins inside it.
    const auto analyserBinHz = rate / settings.view.fftSize;
    std::vector<double> rowLowHz (static_cast<size_t> (height)), rowHighHz (static_cast<size_t> (height));
    auto lowRows = 0;   // rows [height - lowRows, height) are modelled bin by bin

    for (int y = 0; y < height; ++y)
    {
        rowHighHz[static_cast<size_t> (y)] = scale.toHz (1.0 - y / h);
        rowLowHz[static_cast<size_t> (y)] = scale.toHz (1.0 - (y + 1.0) / h);
    }

    // (Above lobeModelTopHz the lobe's +-23 Hz is a few rows at most: left out, it costs most.)
    for (int y = height - 1; y >= 0 && rowHighHz[static_cast<size_t> (y)] - rowLowHz[static_cast<size_t> (y)] < 2.0 * analyserBinHz
                             && rowHighHz[static_cast<size_t> (y)] < lobeModelTopHz; --y)
        ++lowRows;

    constexpr int lobeReach = 4;   // bins: the main lobe and the first side lobes (to -44 dB)
    const auto crossoverHz = lowRows > 0 ? rowHighHz[static_cast<size_t> (height - lowRows)] : 0.0;
    const auto numAnalyserBins = lowRows > 0 ? static_cast<int> (std::ceil (crossoverHz / analyserBinHz)) + lobeReach + 2 : 0;
    std::array<float, 2 * lobeReach + 1> lobePower {};

    for (int d = -lobeReach; d <= lobeReach; ++d)
    {
        const auto amplitude = CanvasPlanImage::lobe (static_cast<float> (d));
        lobePower[static_cast<size_t> (d + lobeReach)] = amplitude * amplitude;
    }

    // Per analyser bin: the song's row (fractional) and the carver's bin (fractional).
    std::vector<float> songRowOfBin (static_cast<size_t> (numAnalyserBins)), carverBinOfBin (static_cast<size_t> (numAnalyserBins));

    for (int b = 0; b < numAnalyserBins; ++b)
    {
        const auto hz = b * analyserBinHz;
        songRowOfBin[static_cast<size_t> (b)] = static_cast<float> (juce::jlimit (0.0, h - 1.0, (1.0 - scale.toNorm (std::max (scale.minHz, hz))) * h - 0.5));
        carverBinOfBin[static_cast<size_t> (b)] = static_cast<float> (hz / binHz);
    }

    // Per row above them: the carver's bins inside it (at least the one at its centre).
    std::vector<int> firstBinOfRow (static_cast<size_t> (height)), lastBinOfRow (static_cast<size_t> (height));

    for (int y = 0; y < height - lowRows; ++y)
    {
        const auto centre = 0.5 * (rowLowHz[static_cast<size_t> (y)] + rowHighHz[static_cast<size_t> (y)]);
        const auto middle = juce::jlimit (0, numBins - 1, static_cast<int> (std::lround (centre / binHz)));
        firstBinOfRow[static_cast<size_t> (y)] = std::min (middle, juce::jlimit (0, numBins - 1, static_cast<int> (std::ceil (rowLowHz[static_cast<size_t> (y)] / binHz - 0.5))));
        lastBinOfRow[static_cast<size_t> (y)] = std::max (middle, juce::jlimit (0, numBins - 1, static_cast<int> (std::floor (rowHighHz[static_cast<size_t> (y)] / binHz + 0.5))));
    }

    // The song's power at each analyser bin, column by column (it changes far less often than the
    // carve: kept while it is the same song at the same size).
    if (songBinVersion != songVersion || songBinWidth != width || songBinCount != numAnalyserBins || songBinHeight != height)
    {
        songBinVersion = songVersion;
        songBinWidth = width;
        songBinHeight = height;
        songBinCount = numAnalyserBins;
        songBinPower.resize (static_cast<size_t> (width) * static_cast<size_t> (numAnalyserBins));

        for (int b = 0; b < numAnalyserBins; ++b)
        {
            const auto rowPosition = songRowOfBin[static_cast<size_t> (b)];
            const auto r0 = static_cast<int> (rowPosition);
            const auto r1 = std::min (height - 1, r0 + 1);
            const auto t = rowPosition - static_cast<float> (r0);
            const auto* row0 = song.data() + static_cast<size_t> (r0) * static_cast<size_t> (width);
            const auto* row1 = song.data() + static_cast<size_t> (r1) * static_cast<size_t> (width);

            for (int x = 0; x < width; ++x)
                songBinPower[static_cast<size_t> (x) * static_cast<size_t> (numAnalyserBins) + static_cast<size_t> (b)]
                    = std::pow (10.0f, 0.1f * (row0[x] + (row1[x] - row0[x]) * t));
        }
    }

    std::vector<float> amounts (static_cast<size_t> (numBins)), widened (static_cast<size_t> (numBins)), gains (static_cast<size_t> (numBins));
    std::vector<float> songPower (static_cast<size_t> (numAnalyserBins)), carvedPower (static_cast<size_t> (numAnalyserBins)),
        binGain (static_cast<size_t> (numAnalyserBins));
    std::vector<double> prefix (static_cast<size_t> (mask->numRows) + 1);
    const auto all = std::pow (10.0f, depth / 10.0f);
    const auto perAmount = depth * 0.23025851f;   // ln (10) / 10: power

    for (int x = 0; x < width; ++x)
    {
        const auto c = juce::jlimit (0, mask->numColumns - 1, static_cast<int> ((x + 0.5) * mask->numColumns / width));
        const auto used = mask->isColumnUsed (c);

        // 1. The carver's gains in this column (power).
        if (! used)
        {
            // Nothing drawn: Carve leaves the song, Stencil takes it all.
            std::fill (gains.begin(), gains.end(), stencil ? all : 1.0f);
        }
        else
        {
            const auto* cells = mask->column (c);
            prefix[0] = 0.0;

            for (int r = 0; r < rows; ++r)
                prefix[static_cast<size_t> (r) + 1] = prefix[static_cast<size_t> (r)] + static_cast<double> (cells[r]);

            std::fill (amounts.begin(), amounts.end(), 0.0f);
            auto firstDrawn = numBins, lastDrawn = -1;   // the bins with something drawn in their band

            for (int k = 0; k < binsInRange; ++k)
            {
                const auto& band = bands[static_cast<size_t> (k)];

                if (band.lastCell < 0)
                    continue;

                float amount = 0.0f;

                if (stencil)
                {
                    amount = *std::max_element (cells + band.firstCell, cells + band.lastCell + 1);
                }
                else
                {
                    // The mean over the band, rows at its ends counted by how much of them it covers.
                    const auto atTo = prefix[static_cast<size_t> (band.lastCell)] + band.toFraction * static_cast<double> (cells[band.lastCell]);
                    const auto atFrom = prefix[static_cast<size_t> (band.firstCell)] + band.fromFraction * static_cast<double> (cells[band.firstCell]);
                    amount = static_cast<float> ((atTo - atFrom) * band.perRow);
                }

                if (amount > 0.0f)
                {
                    amounts[static_cast<size_t> (k)] = amount;
                    firstDrawn = std::min (firstDrawn, k);
                    lastDrawn = k;
                }
            }

            // Only the bins about what is drawn need more work: elsewhere Carve leaves the song
            // and Stencil takes it.
            const auto from = std::max (0, firstDrawn - halo), to = std::min (numBins - 1, lastDrawn + halo);
            std::fill (gains.begin(), gains.end(), stencil ? all : 1.0f);

            if (stencil)
            {
                for (int k = from; k <= to; ++k)
                    amounts[static_cast<size_t> (k)] = 1.0f - std::min (amounts[static_cast<size_t> (k)], 1.0f);
            }
            else if (halo > 0 && from <= to)
            {
                std::copy (amounts.begin() + from, amounts.begin() + to + 1, widened.begin() + from);

                for (int k = from; k <= to; ++k)
                    amounts[static_cast<size_t> (k)] = *std::max_element (widened.begin() + std::max (from, k - halo),
                                                                          widened.begin() + std::min (to, k + halo) + 1);
            }

            for (int k = from; k <= to; ++k)
            {
                const auto amount = amounts[static_cast<size_t> (k)];
                gains[static_cast<size_t> (k)] = amount <= 0.0f ? 1.0f : (amount >= 1.0f ? all : std::exp (perAmount * amount));
            }
        }

        // 2a. The lows, bin by bin: the song's power around each bin, carved, through the lobe.
        if (numAnalyserBins > 0)
        {
            const auto* songColumn = songBinPower.data() + static_cast<size_t> (x) * static_cast<size_t> (numAnalyserBins);

            for (int b = 0; b < numAnalyserBins; ++b)
            {
                const auto power = songColumn[b];
                const auto k = carverBinOfBin[static_cast<size_t> (b)];
                const auto k0 = std::min (numBins - 1, static_cast<int> (k));
                const auto k1 = std::min (numBins - 1, k0 + 1);
                const auto g = gains[static_cast<size_t> (k0)] + (gains[static_cast<size_t> (k1)] - gains[static_cast<size_t> (k0)]) * (k - static_cast<float> (k0));

                songPower[static_cast<size_t> (b)] = power;
                carvedPower[static_cast<size_t> (b)] = power * g;
            }

            for (int b = 0; b < numAnalyserBins; ++b)
            {
                auto kept = 0.0f, total = 0.0f;

                for (int d = -lobeReach; d <= lobeReach; ++d)
                {
                    const auto j = juce::jlimit (0, numAnalyserBins - 1, b + d);
                    const auto w = lobePower[static_cast<size_t> (d + lobeReach)];
                    kept += w * carvedPower[static_cast<size_t> (j)];
                    total += w * songPower[static_cast<size_t> (j)];
                }

                binGain[static_cast<size_t> (b)] = total > 0.0f ? kept / total : 1.0f;
            }

            for (int y = height - lowRows; y < height; ++y)
            {
                const auto centre = 0.5 * (rowLowHz[static_cast<size_t> (y)] + rowHighHz[static_cast<size_t> (y)]);
                const auto at = static_cast<float> (centre / analyserBinHz);
                const auto b0 = juce::jlimit (0, numAnalyserBins - 2, static_cast<int> (at));
                const auto t = juce::jlimit (0.0f, 1.0f, at - static_cast<float> (b0));
                carve[static_cast<size_t> (y) * static_cast<size_t> (width) + static_cast<size_t> (x)]
                    = binGain[static_cast<size_t> (b0)] + (binGain[static_cast<size_t> (b0) + 1] - binGain[static_cast<size_t> (b0)]) * t;
            }
        }

        // 2b. Higher up: the least carved of the carver's bins in each row (a bin left whole ends
        // the search: nothing is brighter).
        for (int y = 0; y < height - lowRows; ++y)
        {
            auto least = 0.0f;

            for (int k = firstBinOfRow[static_cast<size_t> (y)], end = lastBinOfRow[static_cast<size_t> (y)]; k <= end && least < 1.0f; ++k)
                least = std::max (least, gains[static_cast<size_t> (k)]);

            carve[static_cast<size_t> (y) * static_cast<size_t> (width) + static_cast<size_t> (x)] = least;
        }
    }

    // 3. In time: half of the analyser's window and a quarter of the carver's frame, twice (a
    // triangle). Outside the pass the song is not carved.
    const auto boxSeconds = 0.5 * settings.view.fftSize / rate + 0.25 * carverFrameSeconds;
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
                                     const std::shared_ptr<const MaskPlanSet>& masks, const FrequencyScale& scale, int width, int height,
                                     const MeasuredPatch* patch)
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
        && imageMasks == masks && ! songDirty && patch == nullptr && ! imagePatched)
        return image;

    imagePatched = patch != nullptr;

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
        || carveSongVersion != songVersion
        || carveScale != scale || carveSettings.mode != settings.mode || ! juce::exactlyEqual (carveSettings.depthDb, settings.depthDb)
        || carveSettings.view.fftSize != settings.view.fftSize || ! juce::exactlyEqual (carveSettings.sampleRate, settings.sampleRate)
        || ! juce::exactlyEqual (carveSettings.passSeconds, settings.passSeconds))
    {
        makeCarve (mask, settings, scale, work, height);
        carveValid = true;
        carveSongVersion = songVersion;
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
            auto level = l0 + (l1 - l0) * stretchFraction[static_cast<size_t> (x)];

            if (patch != nullptr)
                level = patch->blend (level, x, y, height);

            const auto index = (level - floorDb) * perDb + 0.5f;
            rowPixels[static_cast<size_t> (x)] = table[static_cast<size_t> (index <= 0.0f ? 0 : index >= 255.0f ? 255 : static_cast<int> (index))];
        }

        writePixelRow (data, y, rowPixels.data(), width);
    }

    return image;
}

} // namespace artfreq::ui
