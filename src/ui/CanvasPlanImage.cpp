#include "CanvasPlanImage.h"

#include "MeasuredPreview.h"
#include "Theme.h"

#include <algorithm>
#include <array>
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

    /** How far the lobes are drawn, in bins: at least four (the main lobe and the first side lobes,
        to -44 dB), and where a bin is a small part of a row (the highs, a linear scale) as far as
        the side lobes still show (32 bins: -100 dB), over at most about 24 rows: in the lows of a
        log scale, where a bin covers many rows, the far side lobes would cost more than they
        show. Lobes fainter than the floor are left out anyway. */
    constexpr double minLobeReachBins = 4.0, maxLobeReachBins = 32.0, lobeReachRows = 24.0;

    /** The faint haze under a shape in the comb's band (see renderGeometry): how far under the
        shape it lies, by how far below the shape's lowest edge, in octaves. Measured (the truth
        lab's scenarios, the median below the default drawing at FFT 8192): 52 dB a quarter of an
        octave down, 62 at half an octave, 70 at one, 77 from two on. */
    float combHazeDropDb (float octaves) noexcept
    {
        constexpr float at[] { 46.0f, 52.0f, 62.0f, 66.0f, 70.0f, 73.5f, 77.0f };   // 0, 0.25, 0.5, 0.75, 1, 1.5, 2
        constexpr float octave[] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };

        if (octaves >= 2.0f)
            return at[6];

        auto i = 0;

        while (i < 5 && octaves > octave[i + 1])
            ++i;

        const auto t = juce::jlimit (0.0f, 1.0f, (octaves - octave[i]) / (octave[i + 1] - octave[i]));
        return at[i] + (at[i + 1] - at[i]) * t;
    }

    /** ... and its streaks: the haze is not even but streaks in time, here a fixed pattern of
        them (+-12 dB, a new value every 25 ms of the pass, the same from one picture to the next,
        so it does not shimmer while dragging). */
    float hazeStreakDb (double seconds) noexcept
    {
        auto bits = static_cast<uint32_t> (static_cast<int64_t> (std::floor (seconds / 0.025)) & 0xffffffff);
        bits ^= bits >> 16;
        bits *= 0x7feb352du;
        bits ^= bits >> 15;
        bits *= 0x846ca68bu;
        bits ^= bits >> 16;
        return 24.0f * static_cast<float> (bits & 0xffff) / 65535.0f - 12.0f;
    }

    /** The comb's texture: its sines, combSpacingHz apart and gliding up together, show on an
        analyser as stripes rising across the shape, about this many dB either way (the truth
        lab: 6 to 8 dB between the 10th and 90th percentile inside low shapes). */
    constexpr float combRippleDb = 3.5f;

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

    if (d < 1.0e-4f)
        return 1.0f;

    if (d < 2.0f)
    {
        // Hann's main lobe: sinc (d) / (1 - d^2); 0.5 at one bin (the limit).
        if (std::abs (d - 1.0f) < 1.0e-3f)
            return 0.5f;

        const auto pd = juce::MathConstants<float>::pi * d;
        return std::abs (std::sin (pd) / (pd * (1.0f - d * d)));
    }

    // The side lobes' envelope, a little under their peaks (between them they dip to nothing).
    const auto d2 = std::max (d, 2.5f);
    return 0.7f / (juce::MathConstants<float>::pi * d2 * (d2 * d2 - 1.0f)) * juce::jlimit (0.0f, 1.0f, (d - 2.0f) * 2.0f);
}

float CanvasPlanImage::combBrightness (int fftSize) noexcept
{
    // combGain makes the comb as bright as a line at 8192; each doubling darkens it by 3 dB.
    return std::sqrt (8192.0f / static_cast<float> (std::max (256, fftSize))) / combGain;
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

    // The comb's grid is drawn at the brightness the analyser shows it.
    const auto combLines = juce::jlimit (0, numLines, plan.combLines);
    const auto comb = combBrightness (model.view.fftSize);

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
                        = line < combLines ? a[line] * comb : a[line];
            }
        }
    }

    // In time, the analyser's window averages the sound over fftSize samples: a sine's loudness
    // is seen through it (two boxes of half the window: a triangle as long as it, close to Hann).
    const auto columnSeconds = model.passSeconds / std::max (1, numColumns);
    const auto windowColumns = (model.view.fftSize / model.sampleRate) / std::max (1.0e-9, columnSeconds);
    const auto boxWidth = std::max (1, static_cast<int> (std::lround (windowColumns * 0.5)));
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

    // The comb's sines glide (combSlope Hz a second) while the analyser's window looks at them:
    // each is smeared over the Hz it glides through, so the comb's grid is drawn with a lobe
    // that much wider (at 8192 points 1.8 times, at 16384 6 times: a soft fill, as measured).
    const auto combGridLinesForReach = juce::jlimit (0, numLines, plan->combLines);
    const auto glideInWindowHz = static_cast<double> (plan->combSlope) * model.view.fftSize / model.sampleRate;
    const auto combWiden = std::sqrt (1.0 + std::pow (0.5 * glideInWindowHz / (2.0 * binHz), 2.0));

    std::vector<Reach> reaches;
    reaches.reserve (static_cast<size_t> (numLines));
    rowStart.assign (static_cast<size_t> (height) + 1, 0);

    for (int line = 0; line < numLines; ++line)
    {
        const auto loudest = lineLoudest[static_cast<size_t> (line)];
        const auto hz = static_cast<double> (plan->frequencies[static_cast<size_t> (line)]);

        if (! (loudest * lobe (0.0f) > faintest) || hz >= nyquistGuard)
            continue;

        const auto hzPerRow = std::max (1.0e-6, rowHigh[static_cast<size_t> (juce::jlimit (0, height - 1, static_cast<int> (rowAt (hz))))]
                                                  - rowLow[static_cast<size_t> (juce::jlimit (0, height - 1, static_cast<int> (rowAt (hz))))]);
        const auto widen = line < combGridLinesForReach ? combWiden : 1.0;
        const auto reachBins = juce::jlimit (minLobeReachBins, maxLobeReachBins, lobeReachRows * hzPerRow / binHz);
        const auto reachHz = reachBins * binHz * widen;
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

        const auto lobeHz = binHz * (r.line < combGridLinesForReach ? combWiden : 1.0);

        for (auto y = r.firstRow; y <= r.lastRow; ++y)
        {
            const auto outside = std::max (0.0, std::max (rowLow[static_cast<size_t> (y)] - hz, hz - rowHigh[static_cast<size_t> (y)]));
            const auto weight = lobe (static_cast<float> (outside / lobeHz));

            if (weight * loudest > faintest)
                contributions[static_cast<size_t> (fill[static_cast<size_t> (y)]++)] = { r.line, weight };
        }
    }

    constexpr int rippleSteps = 64;
    std::array<float, rippleSteps> ripple {};

    for (int i = 0; i < rippleSteps; ++i)
        ripple[static_cast<size_t> (i)] = std::pow (10.0f, combRippleDb / 20.0f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / rippleSteps));

    const auto combGlideHz = plan->combLines > 1 ? (plan->combLines - 1) * static_cast<double> (plan->combGridHz) + 2.0 * plan->combEdgeHz : 1.0;
    const auto combBaseHz = static_cast<double> (plan->combGridLowHz) - plan->combEdgeHz;

    // Pixels: each row shows the loudest of what reaches it (an analyser's row shows the loudest
    // bin inside it).
    accumulator.resize (static_cast<size_t> (width));
    combAccumulator.resize (static_cast<size_t> (width));
    const auto combGridLines = juce::jlimit (0, numLines, plan->combLines);
    combDb.assign (combGridLines > 0 ? pixels : 0, silentDb);
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
        auto* combAcc = combAccumulator.data();
        std::fill (acc, acc + width, 0.0f);
        std::fill (combAcc, combAcc + width, 0.0f);

        for (auto i = first; i < last; ++i)
        {
            const auto& c = contributions[static_cast<size_t> (i)];
            const auto* lineAmplitudes = amplitudes.data() + static_cast<size_t> (c.line) * static_cast<size_t> (numColumns);
            const auto w = c.weight;
            auto* target = c.line < combGridLines ? combAcc : acc;   // the comb's grid apart: its haze (below)

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

        // The comb's stripes: sine j is at base + (j L / sines + slope t) mod L, so at this
        // row's Hz the phase among them moves on by slope t (the measured preview starts the
        // pass at t = 0, as here).
        if (combGridLines > 0 && plan->combSines > 0)
        {
            const auto rowHz = 0.5 * (rowLow[static_cast<size_t> (y)] + rowHigh[static_cast<size_t> (y)]);
            const auto spacing = combGlideHz / plan->combSines;
            const auto atStart = (rowHz - combBaseHz) / spacing;
            const auto perColumn = static_cast<double> (plan->combSlope) * model.passSeconds / width / spacing;

            for (int x = 0; x < width; ++x)
            {
                if (combAcc[x] <= 0.0f)
                    continue;

                const auto phase = atStart - perColumn * (x + 0.5);
                const auto index = static_cast<int> ((phase - std::floor (phase)) * static_cast<double> (rippleSteps)) & (rippleSteps - 1);
                combAcc[x] *= ripple[static_cast<size_t> (index)];
            }
        }

        for (int x = 0; x < width; ++x)
        {
            const auto value = std::max (acc[x], combAcc[x]);
            rowDb[x] = value > 1.0e-10f ? dbPerOctave * fastLog2 (value) + offset : silentDb;
        }

        if (combGridLines > 0)
        {
            auto* combRow = combDb.data() + static_cast<size_t> (y) * static_cast<size_t> (width);

            for (int x = 0; x < width; ++x)
                combRow[x] = combAcc[x] > 1.0e-10f ? dbPerOctave * fastLog2 (combAcc[x]) + offset : silentDb;
        }
    }

    // The haze under the comb's shapes: its sines glide up into a shape from below and spill
    // energy as they enter it, so an analyser shows a faint haze under a bright shape in the
    // lows, falling away below the shape (combHazeDropDb: about 52 dB under it a quarter of an
    // octave down, 77 from two octaves on) and in streaks (hazeStreakDb). Each column of the
    // band is followed from the top down: the brightest of the comb above, and the lowest row
    // where it was about that bright (the shape's lowest edge).
    if (combGridLines > 0)
    {
        const auto combTopHz = static_cast<double> (plan->frequencies[static_cast<size_t> (combGridLines - 1)]) + 0.5 * plan->combGridHz;
        const auto topRow = juce::jlimit (0, height, static_cast<int> (std::floor (rowAt (combTopHz))));
        std::vector<float> brightest (static_cast<size_t> (width), silentDb), edgeOctave (static_cast<size_t> (width), 0.0f), streak (static_cast<size_t> (width));

        for (int x = 0; x < width; ++x)
            streak[static_cast<size_t> (x)] = hazeStreakDb ((x + 0.5) / width * model.passSeconds);

        for (int y = topRow; y < height; ++y)
        {
            auto* rowDb = geometryDb.data() + static_cast<size_t> (y) * static_cast<size_t> (width);
            const auto* combRow = combDb.data() + static_cast<size_t> (y) * static_cast<size_t> (width);
            const auto octave = static_cast<float> (std::log2 (std::max (1.0, 0.5 * (rowLow[static_cast<size_t> (y)] + rowHigh[static_cast<size_t> (y)]))));

            for (int x = 0; x < width; ++x)
            {
                auto& above = brightest[static_cast<size_t> (x)];

                if (above > silentDb + 1.0f)
                    rowDb[x] = std::max (rowDb[x], above - combHazeDropDb (edgeOctave[static_cast<size_t> (x)] - octave) + streak[static_cast<size_t> (x)]);

                if (combRow[x] > above - 6.0f)
                {
                    above = std::max (above, combRow[x]);
                    edgeOctave[static_cast<size_t> (x)] = octave;
                }
            }
        }
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
