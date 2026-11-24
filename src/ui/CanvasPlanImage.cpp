#include "CanvasPlanImage.h"

#include "MeasuredPreview.h"
#include "Theme.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <cstring>

namespace artfreq::ui
{
namespace
{
    /** log2 to within 0.005 (0.03 dB), several times faster than std::log2: the canvas maps up
        to millions of amplitudes to levels whenever the plan changes. x must be a positive,
        normal float. */
    inline float fastLog2 (float x) noexcept
    {
        int32_t bits = 0;
        std::memcpy (&bits, &x, sizeof (bits));
        // The polynomial gives log2 (m) + 1 for the mantissa m in [1, 2): hence 128, not 127.
        const auto exponent = static_cast<float> (((bits >> 23) & 0xff) - 128);
        bits = (bits & 0x007fffff) | 0x3f800000;
        float mantissa = 0.0f;
        std::memcpy (&mantissa, &bits, sizeof (mantissa));
        return exponent + (-0.34484843f * mantissa + 2.02466578f) * mantissa - 0.67487759f;
    }

    constexpr float dbPerOctave = 6.0205999f;  // 20 log10 (2)
    constexpr float silentDb = -200.0f;

    /** A box blur of `width` columns over `values` (zero beyond its ends), into `out`; `early`
        puts an even width's extra column before the centre rather than after it. */
    void boxBlur (const float* values, float* out, int count, int width, bool early, std::vector<float>& prefix)
    {
        prefix.resize (static_cast<size_t> (count) + 1);
        prefix[0] = 0.0f;

        for (int i = 0; i < count; ++i)
            prefix[static_cast<size_t> (i) + 1] = prefix[static_cast<size_t> (i)] + values[i];

        const auto before = early ? width / 2 : (width - 1) / 2;
        const auto after = width - 1 - before;
        const auto scale = 1.0f / static_cast<float> (width);

        for (int i = 0; i < count; ++i)
        {
            const auto from = std::max (0, i - before);
            const auto to = std::min (count, i + after + 1);
            out[i] = std::max (0.0f, (prefix[static_cast<size_t> (to)] - prefix[static_cast<size_t> (from)]) * scale);
        }
    }
} // namespace

void writePixelRow (juce::Image::BitmapData& data, int y, const juce::PixelARGB* source, int width) noexcept
{
    auto* line = data.getLinePointer (y);

    if (data.pixelFormat == juce::Image::ARGB && data.pixelStride == 4)
    {
        std::memcpy (line, source, static_cast<size_t> (width) * sizeof (juce::PixelARGB));
        return;
    }

    // RGB images are 3 bytes a pixel in software and 4 in some native formats.
    for (int x = 0; x < width; ++x)
        reinterpret_cast<juce::PixelRGB*> (line + x * data.pixelStride)->set (source[x]);
}

void CanvasPlanImage::stretchSteps (int workWidth, int width, std::vector<int>& before, std::vector<float>& fraction)
{
    before.resize (static_cast<size_t> (std::max (0, width)));
    fraction.resize (static_cast<size_t> (std::max (0, width)));

    for (int x = 0; x < width; ++x)
    {
        const auto at = (x + 0.5) * workWidth / width - 0.5;
        const auto b = juce::jlimit (0, std::max (0, workWidth - 2), static_cast<int> (std::floor (at)));
        before[static_cast<size_t> (x)] = b;
        fraction[static_cast<size_t> (x)] = workWidth > 1 ? static_cast<float> (juce::jlimit (0.0, 1.0, at - b)) : 0.0f;
    }
}

float CanvasPlanImage::lobe (float bins) noexcept
{
    const auto d = std::abs (bins);

    if (d >= analysisWindowLobeBins)
        return 0.0f;   // the side lobes, 92 dB down: under any floor

    // The window's transform: each of its cosines is a pair of sincs, k bins either side.
    const auto sinc = [] (float x)
    {
        const auto px = juce::MathConstants<float>::pi * x;
        return std::abs (px) < 1.0e-4f ? 1.0f : std::sin (px) / px;
    };

    auto sum = analysisWindow[0] * sinc (d);

    for (size_t k = 1; k < std::size (analysisWindow); ++k)
        sum += 0.5f * analysisWindow[k] * (sinc (d - static_cast<float> (k)) + sinc (d + static_cast<float> (k)));

    return std::abs (sum) / analysisWindow[0];
}

float CanvasPlanImage::combBrightness (int fftSize, double sampleRate, float spacingHz) noexcept
{
    // Lines closer than the window's noise bandwidth add up in every bin, in power, as their
    // phases keep them from pulsing together (ArtPlan.h); lines further apart are each read at
    // their own level.
    const auto bandwidthHz = static_cast<double> (analysisWindowNoiseBins) * sampleRate / std::max (256, fftSize);
    return static_cast<float> (std::sqrt (std::max (1.0, bandwidthHz / std::max (0.1, static_cast<double> (spacingHz)))));
}

void MeasuredPatch::prepare (int width)
{
    frame0.resize (static_cast<size_t> (width));
    frame1.resize (static_cast<size_t> (width));
    fraction.resize (static_cast<size_t> (width));

    if (analysis != nullptr)
        for (int x = 0; x < width; ++x)
            MeasuredSpectrogram::columnPosition (x, width, analysis->frames, frame0[static_cast<size_t> (x)], frame1[static_cast<size_t> (x)],
                                                 fraction[static_cast<size_t> (x)]);
}

float MeasuredPatch::blend (float modelDb, int x, int y, int height) const noexcept
{
    const auto w = weight[static_cast<size_t> (x)];

    if (w <= 0.0f || analysis == nullptr)
        return modelDb;

    const auto row = juce::jlimit (0, analysis->rows - 1, static_cast<int> ((y + 0.5) * analysis->rows / height));
    const auto a = analysis->outputDb (row, frame0[static_cast<size_t> (x)]);
    const auto b = analysis->outputDb (row, frame1[static_cast<size_t> (x)]);
    const auto measured = a + (b - a) * fraction[static_cast<size_t> (x)] + shiftDb;
    return modelDb + (measured - modelDb) * w;
}

bool CanvasPlanImage::updateGeometry (const std::shared_ptr<const ArtPlan>& plan, const AnalyserModel& model, int width, int height)
{
    const auto* planPointer = plan.get();
    const auto version = plan != nullptr ? plan->version : 0;

    // The geometry: everything but Level and the colours (the floor matters: lobes are drawn as
    // far as they show above it).
    const auto sameGeometry = geometryValid && geometryPlan == planPointer && geometryVersion == version
                           && geometryWidth == width && geometryHeight == height
                           && geometryModel.scale == model.scale
                           && juce::exactlyEqual (geometryModel.slopeDbPerOctave, model.slopeDbPerOctave)
                           && geometryModel.view.fftSize == model.view.fftSize
                           && juce::exactlyEqual (geometryModel.view.floorDb, model.view.floorDb)
                           && juce::exactlyEqual (geometryModel.sampleRate, model.sampleRate)
                           && juce::exactlyEqual (geometryModel.passSeconds, model.passSeconds);

    if (sameGeometry)
        return false;

    if (plan != nullptr && (amplitudesPlan != plan || amplitudesVersion != version
                            || ! juce::exactlyEqual (amplitudesPassSeconds, model.passSeconds)
                            || ! juce::exactlyEqual (amplitudesSampleRate, model.sampleRate)
                            || amplitudesFft != model.view.fftSize))
    {
        amplitudesPlan = plan;
        amplitudesVersion = version;
        amplitudesPassSeconds = model.passSeconds;
        amplitudesSampleRate = model.sampleRate;
        amplitudesFft = model.view.fftSize;
        updateAmplitudes (*plan, model);
    }
    else if (plan == nullptr)
    {
        amplitudesPlan.reset();
    }

    renderGeometry (planPointer, model, width, height);

    geometryValid = true;
    geometryPlan = planPointer;
    geometryVersion = version;
    geometryModel = model;
    geometryWidth = width;
    geometryHeight = height;
    return true;
}

void CanvasPlanImage::prepareLevels (const std::shared_ptr<const ArtPlan>& plan, const AnalyserModel& modelIn, int width, int height)
{
    auto model = modelIn;
    model.scale = model.scale.sanitised();
    model.view = model.view.sanitised();
    updateGeometry (plan, model, juce::jmax (1, width), juce::jmax (1, height));
    levelDb = model.outputGain > 0.0f ? 20.0f * std::log10 (model.outputGain) : silentDb;
}

juce::Image CanvasPlanImage::get (const std::shared_ptr<const ArtPlan>& plan, const AnalyserModel& modelIn,
                                  int width, int height, const MeasuredPatch* patch)
{
    width = juce::jmax (1, width);
    height = juce::jmax (1, height);

    auto model = modelIn;
    model.scale = model.scale.sanitised();
    model.view = model.view.sanitised();

    const auto rebuilt = updateGeometry (plan, model, width, height);

    if (! rebuilt && patch == nullptr && ! imagePatched && image.isValid() && image.getWidth() == width && image.getHeight() == height
        && imageModel == model)
        return image;

    colour (model, width, height, patch);
    imagePatched = patch != nullptr;
    imageModel = model;
    return image;
}

void CanvasPlanImage::updateAmplitudes (const ArtPlan& plan, const AnalyserModel& model)
{
    const auto numLines = plan.numLines;
    const auto numColumns = plan.numColumns;
    const auto total = static_cast<size_t> (std::max (0, numLines)) * static_cast<size_t> (std::max (0, numColumns));

    amplitudes.assign (total, 0.0f);
    lineLoudest.assign (static_cast<size_t> (std::max (0, numLines)), 0.0f);

    if (total == 0 || plan.amplitudes.size() < total || plan.silent)
        return;

    // The standing comb's lines are drawn at the brightness the analyser shows them: they stand
    // closer than its bins are wide, so each bin holds several.
    const auto combLines = juce::jlimit (0, numLines, plan.combLines);
    std::vector<float> comb (static_cast<size_t> (combLines), 1.0f);

    for (int line = 0; line < combLines; ++line)
    {
        const auto at = [&plan] (int l) { return plan.frequencies[static_cast<size_t> (l)]; };
        const auto spacing = line + 1 < combLines ? at (line + 1) - at (line) : (line > 0 ? at (line) - at (line - 1) : 1.0e6f);
        comb[static_cast<size_t> (line)] = combBrightness (model.view.fftSize, model.sampleRate, spacing);
    }

    // The plan is column by column, the blur below runs along each line: turned round in tiles,
    // so both sides stay in the cache (column by column, every write missed it: 1024 lines x 1024
    // columns took 12 ms).
    constexpr int tile = 64;

    for (int column0 = 0; column0 < numColumns; column0 += tile)
    {
        const auto columnEnd = std::min (numColumns, column0 + tile);

        for (int line0 = 0; line0 < numLines; line0 += tile)
        {
            const auto lineEnd = std::min (numLines, line0 + tile);

            for (int column = column0; column < columnEnd; ++column)
            {
                const auto* a = plan.amplitudes.data() + static_cast<size_t> (column) * static_cast<size_t> (numLines);

                for (int line = line0; line < lineEnd; ++line)
                    amplitudes[static_cast<size_t> (line) * static_cast<size_t> (numColumns) + static_cast<size_t> (column)]
                        = line < combLines ? a[line] * comb[static_cast<size_t> (line)] : a[line];
            }
        }
    }

    // In time, the analyser's window weighs the sound over fftSize samples, most of it near the
    // middle: a sine's loudness is seen through it (two boxes of a third of the window: a
    // triangle as wide at half height as the window, 0.34 of its length).
    const auto columnSeconds = model.passSeconds / std::max (1, numColumns);
    const auto windowColumns = (model.view.fftSize / model.sampleRate) / std::max (1.0e-9, columnSeconds);
    const auto boxWidth = std::max (1, static_cast<int> (std::lround (windowColumns * 0.344)));
    std::vector<float> once (static_cast<size_t> (numColumns));

    for (int line = 0; line < numLines; ++line)
    {
        auto* row = amplitudes.data() + static_cast<size_t> (line) * static_cast<size_t> (numColumns);

        if (std::none_of (row, row + numColumns, [] (float v) { return v > 0.0f; }))
            continue;

        if (boxWidth > 1)
        {
            boxBlur (row, once.data(), numColumns, boxWidth, true, scratch);
            boxBlur (once.data(), row, numColumns, boxWidth, false, scratch);
        }

        lineLoudest[static_cast<size_t> (line)] = *std::max_element (row, row + numColumns);
    }
}

void CanvasPlanImage::renderGeometry (const ArtPlan* plan, const AnalyserModel& model, int pictureWidth, int height)
{
    const auto width = workWidthFor (pictureWidth);
    geometryWorkWidth = width;
    const auto pixels = static_cast<size_t> (width) * static_cast<size_t> (height);
    geometryDb.assign (pixels, silentDb);
    ++renderCount;

    const auto numLines = plan != nullptr ? plan->numLines : 0;
    const auto numColumns = plan != nullptr ? plan->numColumns : 0;

    if (plan == nullptr || plan->silent || numLines <= 0 || numColumns <= 0
        || static_cast<int> (plan->frequencies.size()) < numLines
        || amplitudes.size() < static_cast<size_t> (numLines) * static_cast<size_t> (numColumns))
        return;

    // Columns: the engine glides from each column's amplitudes to the next one's, so each image
    // column shows the plan interpolated at its centre.
    columnOf.resize (static_cast<size_t> (width));
    nextColumnOf.resize (static_cast<size_t> (width));
    columnFraction.resize (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
    {
        const auto position = (static_cast<double> (x) + 0.5) * numColumns / width - 0.5;
        const auto column = juce::jlimit (0, numColumns - 1, static_cast<int> (std::floor (position)));
        columnOf[static_cast<size_t> (x)] = column;
        nextColumnOf[static_cast<size_t> (x)] = juce::jmin (numColumns - 1, column + 1);
        columnFraction[static_cast<size_t> (x)] = static_cast<float> (juce::jlimit (0.0, 1.0, position - column));
    }

    // Rows: each one's span in Hz, and the analyser's slope at its centre.
    const auto& s = model.scale;
    const auto h = static_cast<double> (height);
    std::vector<double> rowLow (static_cast<size_t> (height)), rowHigh (static_cast<size_t> (height));
    std::vector<float> rowOffset (static_cast<size_t> (height));
    auto mostOffset = 0.0f;

    for (int y = 0; y < height; ++y)
    {
        rowHigh[static_cast<size_t> (y)] = s.toHz (1.0 - y / h);
        rowLow[static_cast<size_t> (y)] = s.toHz (1.0 - (y + 1.0) / h);
        const auto centre = s.toHz (1.0 - (y + 0.5) / h);
        rowOffset[static_cast<size_t> (y)] = model.slopeDbPerOctave * static_cast<float> (std::log2 (centre / SpectrumRows::slopePivotHz));
        mostOffset = std::max (mostOffset, rowOffset[static_cast<size_t> (y)]);
    }

    // Which lines reach which rows, and how much of them: the lobe at the row's nearest Hz. A
    // contribution too faint to show above the floor, at full Level, is left out.
    const auto binHz = model.sampleRate / model.view.fftSize;
    const auto nyquistGuard = 0.48 * model.sampleRate;
    const auto faintest = std::pow (10.0f, (model.view.floorDb - mostOffset) / 20.0f);
    const auto rowAt = [&] (double hz) { return (1.0 - s.toNorm (hz)) * h; };

    struct Reach
    {
        int line, firstRow, lastRow;
    };

    std::vector<Reach> reaches;
    reaches.reserve (static_cast<size_t> (numLines));
    rowStart.assign (static_cast<size_t> (height) + 1, 0);

    for (int line = 0; line < numLines; ++line)
    {
        const auto loudest = lineLoudest[static_cast<size_t> (line)];
        const auto hz = static_cast<double> (plan->frequencies[static_cast<size_t> (line)]);

        if (! (loudest * lobe (0.0f) > faintest) || hz >= nyquistGuard)
            continue;

        const auto reachHz = static_cast<double> (analysisWindowLobeBins) * binHz;   // the main lobe: all there is to see
        const auto first = juce::jmax (0, static_cast<int> (std::floor (rowAt (hz + reachHz))));
        const auto last = juce::jmin (height - 1, static_cast<int> (std::floor (rowAt (std::max (s.minHz * 0.5, hz - reachHz)))));

        if (first > last)
            continue;

        reaches.push_back ({ line, first, last });

        for (auto y = first; y <= last; ++y)
            ++rowStart[static_cast<size_t> (y) + 1];
    }

    for (size_t y = 1; y < rowStart.size(); ++y)
        rowStart[y] += rowStart[y - 1];

    contributions.assign (static_cast<size_t> (rowStart.back()), { 0, 0.0f });
    std::vector<int> fill (rowStart.begin(), rowStart.end() - 1);

    for (const auto& r : reaches)
    {
        const auto hz = static_cast<double> (plan->frequencies[static_cast<size_t> (r.line)]);
        const auto loudest = lineLoudest[static_cast<size_t> (r.line)];

        for (auto y = r.firstRow; y <= r.lastRow; ++y)
        {
            const auto outside = std::max (0.0, std::max (rowLow[static_cast<size_t> (y)] - hz, hz - rowHigh[static_cast<size_t> (y)]));
            const auto weight = lobe (static_cast<float> (outside / binHz));

            if (weight * loudest > faintest)
                contributions[static_cast<size_t> (fill[static_cast<size_t> (y)]++)] = { r.line, weight };
        }
    }

    // Pixels: each row shows the loudest of what reaches it (an analyser's row shows the loudest
    // bin inside it).
    accumulator.resize (static_cast<size_t> (width));
    const auto oneToOne = width == numColumns;
    const auto* columns = columnOf.data();
    const auto* nextColumns = nextColumnOf.data();
    const auto* fractions = columnFraction.data();

    for (int y = 0; y < height; ++y)
    {
        const auto first = rowStart[static_cast<size_t> (y)];
        const auto last = fill[static_cast<size_t> (y)];

        if (first == last)
            continue;

        auto* acc = accumulator.data();
        std::fill (acc, acc + width, 0.0f);

        for (auto i = first; i < last; ++i)
        {
            const auto& c = contributions[static_cast<size_t> (i)];
            const auto* lineAmplitudes = amplitudes.data() + static_cast<size_t> (c.line) * static_cast<size_t> (numColumns);
            const auto w = c.weight;
            auto* target = acc;

            if (oneToOne)
            {
                // A pixel column per plan column (the work width is the plan's): no interpolation,
                // a loop the compiler vectorises.
                for (int x = 0; x < width; ++x)
                    target[x] = std::max (target[x], w * lineAmplitudes[x]);
            }
            else
            {
                for (int x = 0; x < width; ++x)
                {
                    const auto a = lineAmplitudes[columns[x]];
                    const auto b = lineAmplitudes[nextColumns[x]];
                    target[x] = std::max (target[x], w * (a + (b - a) * fractions[x]));
                }
            }
        }

        auto* rowDb = geometryDb.data() + static_cast<size_t> (y) * static_cast<size_t> (width);
        const auto offset = rowOffset[static_cast<size_t> (y)];

        for (int x = 0; x < width; ++x)
            rowDb[x] = acc[x] > 1.0e-10f ? dbPerOctave * fastLog2 (acc[x]) + offset : silentDb;
    }
}

void CanvasPlanImage::colour (const AnalyserModel& model, int width, int height, const MeasuredPatch* patch)
{
    // Opaque RGB: drawn with a straight copy rather than blended pixel by pixel. A picture still
    // held elsewhere (the canvas draws the last one while this one is made on another thread) is
    // left alone: a new one is made.
    if (! image.isValid() || image.getWidth() != width || image.getHeight() != height || image.getReferenceCount() > 1)
        image = juce::Image (juce::Image::RGB, width, height, false);

    const auto gainDb = model.outputGain > 0.0f ? 20.0f * std::log10 (model.outputGain) : silentDb;
    const auto& table = theme::spectrumTable();
    const auto floorDb = model.view.floorDb, ceilingDb = model.view.ceilingDb;
    const auto work = geometryWorkWidth;
    stretchSteps (work, width, stretchBefore, stretchFraction);

    levelDb = gainDb;
    rowPixels.resize (static_cast<size_t> (width));
    rowIndex.resize (static_cast<size_t> (work));
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    // Colour-map index = (dB - floor) * perDb, worked out per column, stretched per pixel.
    const auto perDb = 255.0f / (ceilingDb - floorDb);
    const auto from = (gainDb - floorDb) * perDb + 0.5f;

    for (int y = 0; y < height; ++y)
    {
        const auto* row = geometryDb.data() + static_cast<size_t> (y) * static_cast<size_t> (work);

        if (patch != nullptr)
        {
            // The model where the drawing changed, the measurement elsewhere, blended between.
            for (int x = 0; x < width; ++x)
            {
                const auto b = static_cast<size_t> (stretchBefore[static_cast<size_t> (x)]);
                const auto d0 = row[b], d1 = row[std::min (static_cast<size_t> (work - 1), b + 1)];
                const auto level = patch->blend (d0 + (d1 - d0) * stretchFraction[static_cast<size_t> (x)] + gainDb, x, y, height);
                const auto index = (level - floorDb) * perDb + 0.5f;
                rowPixels[static_cast<size_t> (x)] = table[static_cast<size_t> (index <= 0.0f ? 0 : index >= 255.0f ? 255 : static_cast<int> (index))];
            }

            writePixelRow (data, y, rowPixels.data(), width);
            continue;
        }

        for (int x = 0; x < work; ++x)
            rowIndex[static_cast<size_t> (x)] = row[x] * perDb + from;

        for (int x = 0; x < width; ++x)
        {
            const auto b = static_cast<size_t> (stretchBefore[static_cast<size_t> (x)]);
            const auto i0 = rowIndex[b];
            const auto i1 = rowIndex[std::min (static_cast<size_t> (work - 1), b + 1)];
            const auto index = i0 + (i1 - i0) * stretchFraction[static_cast<size_t> (x)];
            rowPixels[static_cast<size_t> (x)] = table[static_cast<size_t> (index <= 0.0f ? 0 : index >= 255.0f ? 255 : static_cast<int> (index))];
        }

        writePixelRow (data, y, rowPixels.data(), width);
    }
}

} // namespace artfreq::ui
