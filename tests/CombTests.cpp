// The rising comb (PlanSettings::lowMethod 3): the crowded lows played by sines that glide
// upwards together, instead of lines 25 Hz apart. The plan's side (which lines it replaces, the
// loudness grid, the normalisation) and the engine's (the band fills at long FFT sizes where lines
// show as stripes, nothing leaks out of it, determinism, crossfades, speed).

#include "TestFramework.h"
#include "TestPlans.h"

#include "core/ArtPlan.h"
#include "dsp/ArtEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace freqart;

namespace
{

constexpr double rate48 = 48000.0;

PlanSettings combSettings (int method, int density = 256, ScaleType type = ScaleType::log)
{
    PlanSettings settings;
    settings.scale.type = type;
    settings.density = density;
    settings.maxFrequencyHz = 22000.0f;
    settings.lowMethod = method;
    return settings;
}

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

} // namespace

FREQART_TEST (planCombReplacesTheCrowdedLows)
{
    Canvas canvas;
    std::fill (canvas.values.begin(), canvas.values.end(), 1.0f);

    for (const auto density : { 256, 1024 })
    {
        const auto lines = buildPlan (canvas, combSettings (0, density));
        const auto comb = buildPlan (canvas, combSettings (3, density));

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

    // Other methods are today's lines; so is the comb where nothing is crowded (a linear scale).
    const auto lines = buildPlan (canvas, combSettings (0));

    for (const auto method : { 1, 2 })
    {
        const auto other = buildPlan (canvas, combSettings (method));
        CHECK (other->combLines == 0);
        CHECK (other->frequencies == lines->frequencies);
        CHECK (other->amplitudes == lines->amplitudes);
        CHECK (other->layoutHash == lines->layoutHash);
    }

    const auto linear = buildPlan (canvas, combSettings (3, 256, ScaleType::linear));
    const auto linearLines = buildPlan (canvas, combSettings (0, 256, ScaleType::linear));
    CHECK (linear->combLines == 0);
    CHECK (linear->combActive.empty());
    CHECK (linear->frequencies == linearLines->frequencies);
    CHECK (linear->amplitudes == linearLines->amplitudes);

    // An empty canvas: laid out, silent, the comb never active.
    const auto empty = buildPlan (Canvas(), combSettings (3));
    CHECK (empty->silent);
    CHECK_GREATER (empty->combLines, 100);
}

FREQART_TEST (planCombGridFollowsTheDrawing)
{
    const auto plan = buildPlan (bandCanvas (100.0, 150.0), combSettings (3));
    CHECK_GREATER (plan->combLines, 100);

    // Grid lines inside the band are on, at the same level; those more than a cell and a row
    // away from it are off. Columns where nothing is drawn are not active.
    float level = -1.0f;

    for (int k = 0; k < plan->combLines; ++k)
    {
        const auto hz = plan->frequencies[static_cast<size_t> (k)];
        const auto amplitude = plan->amplitude (500, k);

        if (hz > 104.0f && hz < 146.0f)
        {
            CHECK_GREATER (amplitude, 0.0f);
            level = level < 0.0f ? amplitude : level;
            CHECK_NEAR (amplitude, level, 1.0e-6);
        }
        else if (hz < 94.0f || hz > 157.0f)
        {
            CHECK_MSG (amplitude <= 0.0f, "grid line at " << hz << " Hz");
        }
    }

    // A column with nothing drawn below the comb's limit leaves it silent there.
    Canvas highsOnly = bandCanvas (5000.0, 8000.0);
    const auto highs = buildPlan (highsOnly, combSettings (3));
    CHECK (std::none_of (highs->combActive.begin(), highs->combActive.end(), [] (uint8_t a) { return a != 0; }));
    CHECK (! highs->silent);
}

FREQART_TEST (engineCombFillsTheBand)
{
    // A block over 100 - 500 Hz. With lines 25 Hz apart a 32768-sample analyser shows stripes:
    // most bins far from the median. The comb fills the band.
    const auto canvas = bandCanvas (100.0, 500.0);
    const auto lines = buildPlan (canvas, combSettings (0));
    const auto comb = buildPlan (canvas, combSettings (3));

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
    const auto plan = buildPlan (bandCanvas (40.0, 1500.0), combSettings (3));

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
    const auto loud = buildPlan (bandCanvas (60.0, 900.0, 1.0f), combSettings (3));
    const auto soft = buildPlan (bandCanvas (200.0, 1200.0, 0.8f), combSettings (3));
    const auto lines = buildPlan (bandCanvas (60.0, 900.0, 1.0f), combSettings (0));
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
        const auto plan = buildPlan (canvas, combSettings (3, density));
        const auto lines = buildPlan (canvas, combSettings (0, density));

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
