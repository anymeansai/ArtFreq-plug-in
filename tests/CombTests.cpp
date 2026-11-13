// Smooth lows (LowEnd::smooth), the rising comb: the crowded lows played by sines that glide
// upwards together, instead of lines 25 Hz apart. The plan's side (which lines it replaces, the
// loudness grid, the normalisation), the engine's (the band fills at long FFT sizes where lines
// show as stripes, its power stays in the drawn band, determinism, crossfades, speed) and the
// canvas's (the band drawn filled, as bright as the lines).

#include "TestFramework.h"
#include "TestPlans.h"

#include "core/ArtPlan.h"
#include "dsp/ArtEngine.h"
#include "ui/CanvasPlanImage.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace freqart;

namespace
{

constexpr double rate48 = 48000.0;

PlanSettings combSettings (LowEnd lowEnd, int density = 256, ScaleType type = ScaleType::log)
{
    PlanSettings settings;
    settings.scale.type = type;
    settings.density = density;
    settings.maxFrequencyHz = 22000.0f;
    settings.lowEnd = lowEnd;
    return settings;
}

constexpr auto smooth = LowEnd::smooth;
constexpr auto stripes = LowEnd::stripes;

/** A canvas lit (at `value`) over the rows whose centre lies between lowHz and highHz on the
    default log scale, in every column. */
Canvas bandCanvas (double lowHz, double highHz, float value = 1.0f)
{
    Canvas canvas;
    const FrequencyScale scale;

    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto hz = scale.toHz (1.0 - (y + 0.5) / Canvas::height);

        if (hz >= lowHz && hz <= highHz)
            for (int x = 0; x < Canvas::width; ++x)
                canvas.at (x, y) = value;
    }

    return canvas;
}

/** Plays a plan for `seconds` (an 8 s drawing, from its start) in blocks of `blockSize`. */
std::vector<float> render (const ArtPlan& plan, double seconds, int blockSize = 512)
{
    ArtEngine engine;
    engine.prepare (rate48, blockSize);
    engine.setPlan (&plan);
    return test::play (engine, static_cast<int> (seconds * rate48), 0.0, 1.0 / (8.0 * rate48), blockSize, true);
}

/** How a long-FFT analyser (32768 samples, Hann) sees a band: the share of its bins within 6 dB
    of their median, averaged over a few windows. */
double longFftFill (const std::vector<float>& audio, double lowHz, double highHz, double* medianDb = nullptr)
{
    double share = 0.0, median = 0.0;
    constexpr int windows = 4;

    for (int w = 0; w < windows; ++w)
    {
        const auto spectrum = test::spectrum (audio, static_cast<size_t> (9600 + w * 16384), 15, rate48);
        std::vector<double> levels;

        for (size_t b = 0; b < spectrum.magnitudes.size(); ++b)
        {
            const auto hz = static_cast<double> (b) * spectrum.binHz;

            if (hz >= lowHz && hz <= highHz)
                levels.push_back (test::toDb (spectrum.magnitudes[b]));
        }

        std::sort (levels.begin(), levels.end());
        const auto m = levels[levels.size() / 2];
        share += static_cast<double> (std::count_if (levels.begin(), levels.end(), [m] (double v) { return std::abs (v - m) <= 6.0; }))
               / static_cast<double> (levels.size());
        median += m;
    }

    if (medianDb != nullptr)
        *medianDb = median / windows;

    return share / windows;
}

/** The loudest bin (dB) of the same analyser between lowHz and highHz, over the same windows. */
double longFftLoudest (const std::vector<float>& audio, double lowHz, double highHz)
{
    double loudest = -300.0;

    for (int w = 0; w < 4; ++w)
    {
        const auto spectrum = test::spectrum (audio, static_cast<size_t> (9600 + w * 16384), 15, rate48);

        for (size_t b = 0; b < spectrum.magnitudes.size(); ++b)
        {
            const auto hz = static_cast<double> (b) * spectrum.binHz;

            if (hz >= lowHz && hz <= highHz)
                loudest = std::max (loudest, test::toDb (spectrum.magnitudes[b]));
        }
    }

    return loudest;
}

/** The power spectrum of x averaged over Hann frames of 2^order points (hop a quarter frame),
    as power per bin; and the bin width. */
std::pair<std::vector<double>, double> averagePower (const std::vector<float>& x, size_t from, int order)
{
    const auto size = static_cast<size_t> (1) << order;
    std::vector<double> power (size / 2 + 1, 0.0);
    int frames = 0;

    for (auto start = from; start + size <= x.size(); start += size / 4, ++frames)
    {
        const auto frame = test::spectrum (x, start, order, rate48);

        for (size_t b = 0; b < power.size(); ++b)
            power[b] += frame.magnitudes[b] * frame.magnitudes[b];
    }

    for (auto& p : power)
        p /= std::max (1, frames);

    return { power, rate48 / static_cast<double> (size) };
}

double powerBetween (const std::vector<double>& power, double binHz, double lowHz, double highHz)
{
    double sum = 0.0;

    for (size_t b = 0; b < power.size(); ++b)
        if (static_cast<double> (b) * binHz >= lowHz && static_cast<double> (b) * binHz < highHz)
            sum += power[b];

    return sum;
}

} // namespace

FREQART_TEST (planCombReplacesTheCrowdedLows)
{
    Canvas canvas;
    std::fill (canvas.values.begin(), canvas.values.end(), 1.0f);

    for (const auto density : { 256, 1024 })
    {
        const auto lines = buildPlan (canvas, combSettings (stripes, density));
        const auto comb = buildPlan (canvas, combSettings (smooth, density));

        // The grid: evenly spaced, about combGridSpacingHz apart, from the bottom of the canvas
        // to below the comb's limit.
        CHECK_GREATER (comb->combLines, 100);
        CHECK (comb->combGridHz > 4.0f && comb->combGridHz < 6.0f);
        CHECK_NEAR (comb->combGridLowHz - 0.5f * comb->combGridHz, 20.0f, 0.01f);
        CHECK_NEAR (comb->combSlope, combSlopeHzPerSecond, 1.0e-6);
        CHECK_NEAR (comb->combEdgeHz, combEdgeFadeHz, 1.0e-6);

        for (int k = 0; k < comb->combLines; ++k)
            CHECK_NEAR (comb->frequencies[static_cast<size_t> (k)], comb->combGridLowHz + static_cast<float> (k) * comb->combGridHz, 1.0e-2);

        // (the grid reaches halfway to the first line kept, a little above the limit at most)
        const auto gridTop = comb->frequencies[static_cast<size_t> (comb->combLines - 1)];
        CHECK_LESS (gridTop, combTopLimitHz + 0.5f * 25.0f);
        const auto glide = static_cast<double> (gridTop - comb->combGridLowHz) + 2.0 * comb->combEdgeHz;
        CHECK_NEAR (comb->combSines, glide / combSpacingHz, 1.0);

        // Above it, the very same lines as without the comb (the highs are untouched), at
        // amplitudes that differ only by the normalisation.
        const auto kept = comb->numLines - comb->combLines;
        CHECK_GREATER (kept, 50);
        CHECK_GREATER (comb->frequencies[static_cast<size_t> (comb->combLines)], 1500.0f);
        CHECK_GREATER (comb->frequencies[static_cast<size_t> (comb->combLines)], gridTop);
        const auto offset = lines->numLines - kept;
        auto sameLines = true;

        for (int l = 0; l < kept; ++l)
            sameLines = sameLines && std::memcmp (&comb->frequencies[static_cast<size_t> (comb->combLines + l)],
                                                  &lines->frequencies[static_cast<size_t> (offset + l)], sizeof (float)) == 0;

        CHECK (sameLines);

        const auto ratio = comb->amplitude (0, comb->combLines) / lines->amplitude (0, offset);
        auto proportional = true;

        for (int c = 0; c < Canvas::width; c += 37)
            for (int l = 0; l < kept; ++l)
                proportional = proportional && std::abs (comb->amplitude (c, comb->combLines + l) - ratio * lines->amplitude (c, offset + l))
                                                   <= 1.0e-5f * lines->amplitude (c, offset + l) + 1.0e-9f;

        CHECK (proportional);

        // The replaced lines were all crowded lows.
        for (int l = 0; l < offset; ++l)
            CHECK_LESS (lines->frequencies[static_cast<size_t> (l)], combTopLimitHz);

        // The grid is played by the comb, never as lines.
        CHECK (std::none_of (comb->activeLines.begin(), comb->activeLines.end(),
                             [&] (uint16_t line) { return line < comb->combLines; }));
        CHECK (comb->combActive.size() == static_cast<size_t> (Canvas::width));
        CHECK (std::all_of (comb->combActive.begin(), comb->combActive.end(), [] (uint8_t a) { return a != 0; }));
        CHECK (comb->layoutHash != lines->layoutHash);

        // The normalisation counts the comb as its sines: each spends cellHz / glide of its time
        // at each grid line.
        double power = 0.0;
        const auto weight = comb->combSines * static_cast<double> (comb->combGridHz) / glide;

        for (int l = 0; l < comb->numLines; ++l)
            power += static_cast<double> (comb->amplitude (0, l)) * comb->amplitude (0, l) * (l < comb->combLines ? weight : 1.0);

        CHECK_NEAR (std::sqrt (power / 2.0), planTargetRms, 1.0e-3);

        // Played louder than lines, to look as bright (see combGain).
        CHECK_NEAR (comb->amplitude (0, comb->combLines / 2) / comb->amplitude (0, comb->combLines), combGain, 0.05);
    }

    // Stripes is plain lines (no comb); so is Smooth where nothing is crowded (a linear scale).
    const auto lines = buildPlan (canvas, combSettings (stripes));
    CHECK (lines->combLines == 0 && lines->combActive.empty());

    const auto linear = buildPlan (canvas, combSettings (smooth, 256, ScaleType::linear));
    const auto linearLines = buildPlan (canvas, combSettings (stripes, 256, ScaleType::linear));
    CHECK (linear->combLines == 0);
    CHECK (linear->combActive.empty());
    CHECK (linear->frequencies == linearLines->frequencies);
    CHECK (linear->amplitudes == linearLines->amplitudes);

    // An empty canvas: laid out, silent, the comb never active.
    const auto empty = buildPlan (Canvas(), combSettings (smooth));
    CHECK (empty->silent);
    CHECK_GREATER (empty->combLines, 100);
}

FREQART_TEST (planCombGridFollowsTheDrawing)
{
    const auto plan = buildPlan (bandCanvas (100.0, 150.0), combSettings (smooth));
    CHECK_GREATER (plan->combLines, 100);

    // Grid lines inside the band are on; those more than a cell and a row away from it are off.
    // The run of lit lines is level in its middle, and softened at both of its edges: its
    // outermost line at combEdgeTaper[0] of that level, the next at combEdgeTaper[1].
    const auto runAt = [] (const ArtPlan& p, int column, double insideLow, double insideHigh, double outsideLow, double outsideHigh)
    {
        std::vector<float> run;
        auto last = -2;
        auto contiguous = true;

        for (int k = 0; k < p.combLines; ++k)
        {
            const auto hz = static_cast<double> (p.frequencies[static_cast<size_t> (k)]);
            const auto amplitude = p.amplitude (column, k);

            if (hz > insideLow && hz < insideHigh)
                CHECK_MSG (amplitude > 0.0f, "grid line at " << hz << " Hz is on");

            if (amplitude > 0.0f)
            {
                CHECK_MSG (hz > outsideLow && hz < outsideHigh, "grid line at " << hz << " Hz is off");
                contiguous = contiguous && (last < 0 || last == k - 1);
                last = k;
                run.push_back (amplitude);
            }
        }

        CHECK (contiguous);
        return run;
    };

    const auto run = runAt (*plan, 500, 104.0, 146.0, 94.0, 157.0);
    CHECK_GREATER (static_cast<int> (run.size()), 2 * combEdgeTaperMinCells - 1);

    if (run.size() >= 5)
    {
        const auto level = run[run.size() / 2];
        const auto last = run.size() - 1;

        for (size_t i = 2; i + 2 <= last; ++i)
            CHECK_NEAR (run[i], level, 1.0e-6);

        CHECK_NEAR (run[0], combEdgeTaper[0] * level, 1.0e-6);
        CHECK_NEAR (run[1], combEdgeTaper[1] * level, 1.0e-6);
        CHECK_NEAR (run[last], combEdgeTaper[0] * level, 1.0e-6);
        CHECK_NEAR (run[last - 1], combEdgeTaper[1] * level, 1.0e-6);
    }

    // A thin stroke (15 Hz, three or four grid lines) keeps its full level throughout; so does a
    // shape's edge at the end of the grid, where the glide fades of its own accord.
    const auto thin = runAt (*buildPlan (bandCanvas (100.0, 115.0), combSettings (smooth)), 500, 101.0, 114.0, 94.0, 122.0);
    CHECK (! thin.empty() && thin.size() < static_cast<size_t> (combEdgeTaperMinCells));
    CHECK (std::all_of (thin.begin(), thin.end(), [&] (float a) { return std::abs (a - thin.front()) < 1.0e-6f; }));

    const auto fromBottom = runAt (*buildPlan (bandCanvas (10.0, 60.0), combSettings (smooth)), 500, 0.0, 56.0, 0.0, 67.0);
    CHECK (fromBottom.size() >= 6 && std::abs (fromBottom[0] - fromBottom[fromBottom.size() / 2]) < 1.0e-6f
           && std::abs (fromBottom.back() - combEdgeTaper[0] * fromBottom[fromBottom.size() / 2]) < 1.0e-6f);

    // A column with nothing drawn below the comb's limit leaves it silent there.
    Canvas highsOnly = bandCanvas (5000.0, 8000.0);
    const auto highs = buildPlan (highsOnly, combSettings (smooth));
    CHECK (std::none_of (highs->combActive.begin(), highs->combActive.end(), [] (uint8_t a) { return a != 0; }));
    CHECK (! highs->silent);
}

FREQART_TEST (engineCombFillsTheBand)
{
    // A block over 100 - 500 Hz. With lines 25 Hz apart a 32768-sample analyser shows stripes:
    // most bins far from the median. The comb fills the band.
    const auto canvas = bandCanvas (100.0, 500.0);
    const auto lines = buildPlan (canvas, combSettings (stripes));
    const auto comb = buildPlan (canvas, combSettings (smooth));

    const auto linesAudio = render (*lines, 1.8);
    const auto combAudio = render (*comb, 1.8);

    const auto linesFill = longFftFill (linesAudio, 130.0, 470.0);
    double medianDb = 0.0;
    const auto combFill = longFftFill (combAudio, 130.0, 470.0, &medianDb);
    std::printf ("    32768-sample FFT, 130 - 470 Hz within 6 dB of the median: lines %.2f, comb %.2f\n", linesFill, combFill);
    CHECK_LESS (linesFill, 0.5);
    CHECK_GREATER (combFill, 0.8);

    // Nothing leaks far from the band: the comb's edges blur over a few grid lines and its
    // glide's spread (about the square root of the slope, 14 Hz), no further. Lines have the
    // same floor of window leakage far below.
    const auto linesMedian = [&]
    {
        double m = 0.0;
        longFftFill (linesAudio, 130.0, 470.0, &m);
        return m;
    }();

    CHECK_LESS (longFftLoudest (combAudio, 20.0, 45.0) - medianDb, -30.0);
    CHECK_LESS (longFftLoudest (combAudio, 20.0, 45.0) - medianDb, longFftLoudest (linesAudio, 20.0, 45.0) - linesMedian + 3.0);
    CHECK_LESS (longFftLoudest (combAudio, 560.0, 4000.0) - medianDb, -40.0);

    // At the plan's level: the loudest column (every column here) at planTargetRms.
    CHECK_NEAR (test::toDb (test::rms (combAudio, 9600) / planTargetRms), 0.0, 1.0);
}

FREQART_TEST (engineCombIsDeterministic)
{
    const auto plan = buildPlan (bandCanvas (40.0, 1500.0), combSettings (smooth));

    const auto first = render (*plan, 1.0);
    const auto second = render (*plan, 1.0);
    CHECK (first == second);
    CHECK_GREATER (test::rms (first), 0.05);

    // The block size moves only rounding: every chunk starts each sine's step afresh from the
    // time since reset.
    const auto oddBlocks = render (*plan, 1.0, 61);
    double largestDifference = 0.0;

    for (size_t i = 0; i < first.size(); ++i)
        largestDifference = std::max (largestDifference, static_cast<double> (std::abs (first[i] - oddBlocks[i])));

    CHECK_LESS (largestDifference, 5.0e-3);
}

FREQART_TEST (engineCombCrossfadesWithoutClicks)
{
    // Two plans with one layout (the comb's sines go on, their amplitudes cross), one without a
    // comb (other sines entirely), and none.
    const auto loud = buildPlan (bandCanvas (60.0, 900.0, 1.0f), combSettings (smooth));
    const auto soft = buildPlan (bandCanvas (200.0, 1200.0, 0.8f), combSettings (smooth));
    const auto lines = buildPlan (bandCanvas (60.0, 900.0, 1.0f), combSettings (stripes));
    CHECK (loud->layoutHash == soft->layoutHash);
    CHECK (loud->layoutHash != lines->layoutHash);

    const auto perSample = 1.0 / (8.0 * rate48);
    ArtEngine engine;
    engine.prepare (rate48, 256);
    engine.setPlan (loud.get());
    const auto steady = test::play (engine, 24000, 0.0, perSample, 256, true);
    const auto steadyStep = test::largestStep (steady, 2400);
    std::vector<float> out;
    auto at = 24000;

    const auto playOn = [&] (int samples)
    {
        const auto block = test::play (engine, samples, at * perSample, perSample, 256);
        out.insert (out.end(), block.begin(), block.end());
        at += samples;
    };

    engine.setPlan (soft.get());
    CHECK (engine.getFadingPlan() == loud.get());
    playOn (4800);
    engine.setPlan (lines.get());
    playOn (4800);
    engine.setPlan (loud.get());
    playOn (4800);
    engine.setPlan (nullptr);
    playOn (4800);

    // A click would be a step of the order of the whole signal; the sines themselves, even
    // where two crossfading plans add up, stay well under twice the steady largest step.
    CHECK_LESS (test::largestStep (out), 2.0 * steadyStep);
    CHECK_LESS (test::peak (out, out.size() - 960), 1.0e-6);
}

FREQART_TEST (engineCombPerformance)
{
    using clock = std::chrono::steady_clock;

    Canvas canvas;
    std::fill (canvas.values.begin(), canvas.values.end(), 0.8f);

    // Best of three, so other work on the machine does not decide the result.
    const auto speed = [] (const ArtPlan& plan, double passSeconds)
    {
        double best = 0.0;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            ArtEngine engine;
            engine.prepare (rate48, 512);
            engine.setPlan (&plan);
            std::vector<float> block (512);
            const auto perSample = 1.0 / (passSeconds * rate48);
            const auto started = clock::now();

            for (int done = 0; done < 96000; done += 512)
            {
                const auto s = test::segment (0, 512, true, done * perSample, perSample, done == 0);
                engine.render (block.data(), 512, &s, 1);
            }

            const auto elapsed = std::chrono::duration<double> (clock::now() - started).count();
            best = std::max (best, 2.0 / std::max (elapsed, 1.0e-9));
        }

        return best;
    };

    for (const auto density : { 256, 1024 })
    {
        const auto plan = buildPlan (canvas, combSettings (smooth, density));
        const auto lines = buildPlan (canvas, combSettings (stripes, density));

        // A one-beat drawing (0.5 s at 120 BPM) has the shortest columns, so the most chunks to
        // set up.
        for (const auto passSeconds : { 8.0, 0.5 })
        {
            const auto combSpeed = speed (*plan, passSeconds);
            std::printf ("    density %4d, %.1f s drawing, all active: comb (%d sines + %d lines) %6.1f x real time, lines (%d) %6.1f x\n",
                         density, passSeconds, plan->combSines, plan->numLines - plan->combLines, combSpeed,
                         lines->numLines, speed (*lines, passSeconds));
            CHECK_GREATER (combSpeed, 40.0);
        }
    }
}

FREQART_TEST (engineCombKeepsItsPowerInTheDrawnBand)
{
    // (Ported from the wobbling-lines prototype's containment test, so that leakage from the
    // comb cannot grow unnoticed.) A block over 100 - 300 Hz, played for 8 s and averaged by a
    // long analyser (32768 points, Hann, hop a quarter): the power lies in the drawn band, give
    // or take a grid cell and the analyser's own window, spread evenly over it (no stripes, no
    // single line), and next to none of it far away.
    const auto plan = buildPlan (bandCanvas (100.0, 300.0), combSettings (smooth));
    const auto audio = render (*plan, 8.0);
    const auto [power, binHz] = averagePower (audio, 9600, 15);

    const auto total = powerBetween (power, binHz, 0.0, 24000.0);
    const auto inside = powerBetween (power, binHz, 95.0, 305.0);
    const auto near = powerBetween (power, binHz, 80.0, 320.0);
    const auto far = powerBetween (power, binHz, 0.0, 60.0) + powerBetween (power, binHz, 340.0, 24000.0);
    std::printf ("    inside %.4f, within 20 Hz %.5f, beyond 40 Hz %.2g\n", inside / total, near / total, far / total);
    CHECK_GREATER (inside / total, 0.995);
    CHECK_GREATER (near / total, 0.9995);
    CHECK_LESS (far / total, 2.0e-5);

    // Even across the band: each 20 Hz of it holds a tenth of the power, within 1.5 dB.
    for (double hz = 100.0; hz < 299.0; hz += 20.0)
    {
        const auto share = powerBetween (power, binHz, hz, hz + 20.0) / inside;
        CHECK_MSG (std::abs (10.0 * std::log10 (share * 210.0 / 20.0)) < 1.5,
                   hz << " - " << hz + 20.0 << " Hz holds " << 10.0 * std::log10 (share * 210.0 / 20.0) << " dB of its share");
    }

    // Nor lines: a steady sine puts about two thirds of its power in one bin; the band's 140
    // bins share it, none holding more than about three times its share.
    const auto strongest = *std::max_element (power.begin(), power.end()) / inside;
    std::printf ("    strongest bin %.4f of the power\n", strongest);
    CHECK_LESS (strongest, 0.02);
}

FREQART_TEST (canvasShowsSmoothLowsAsAFilledBand)
{
    // A block over 60 - 900 Hz and another over 3 - 4 kHz, all at full value. The canvas shows
    // the comb's band filled at every height (Stripes: scan lines with dark gaps, as before), and
    // as bright as the lines above it, as an analyser of 8192 points would.
    Canvas canvas = bandCanvas (60.0, 900.0);
    const auto highs = bandCanvas (3000.0, 4000.0);

    for (size_t i = 0; i < canvas.values.size(); ++i)
        canvas.values[i] = std::max (canvas.values[i], highs.values[i]);

    const FrequencyScale scale;
    constexpr int width = 200, height = 500;
    const auto& table = ui::theme::spectrumTable();

    const auto picture = [&] (LowEnd lowEnd)
    {
        std::shared_ptr<ArtPlan> plan (buildPlan (canvas, combSettings (lowEnd)).release());
        plan->version = lowEnd == smooth ? 1 : 2;
        ui::CanvasPlanImage image;
        return image.get (plan, scale, width, height, 1.0f);
    };

    const auto rowOf = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * height); };
    const auto darkRows = [&] (const juce::Image& image, double lowHz, double highHz)
    {
        int dark = 0;

        for (int y = rowOf (highHz); y <= rowOf (lowHz); ++y)
            dark += image.getPixelAt (width / 2, y).getBrightness() < 0.2f ? 1 : 0;

        return dark;
    };

    const auto smoothImage = picture (smooth);
    const auto stripesImage = picture (stripes);
    const auto rows = rowOf (70.0) - rowOf (800.0) + 1;
    std::printf ("    dark rows in 70 - 800 Hz (of %d): smooth %d, stripes %d\n", rows, darkRows (smoothImage, 70.0, 800.0),
                 darkRows (stripesImage, 70.0, 800.0));
    CHECK (darkRows (smoothImage, 70.0, 800.0) == 0);
    CHECK_GREATER (darkRows (stripesImage, 70.0, 800.0), rows / 3);

    // Inside the comb's band and on the lines at 3 - 4 kHz: the loudest colour of the map.
    const auto loudest = juce::Colour (table[255]);
    CHECK (smoothImage.getPixelAt (width / 2, rowOf (200.0)) == loudest);
    CHECK (smoothImage.getPixelAt (width / 2, rowOf (3500.0)) == loudest);
}
