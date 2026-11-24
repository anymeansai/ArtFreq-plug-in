// Smooth lows (LowEnd::smooth), the standing comb: the crowded lows played by steady lines on a
// fine grid, with phases that keep them from pulsing, instead of lines 25 Hz apart. The plan's
// side (which lines it replaces, its grid, gains and phases, the normalisation; nothing below the
// drawing, in Stripes too; the fades in time), the engine's (the band fills at long FFT sizes
// where lines show as stripes, nothing sounds outside the drawn band, the lines keep their phases
// through silence, determinism, crossfades, speed) and the canvas's (the band drawn filled, as
// bright as the lines).

#include "TestFramework.h"
#include "TestPlans.h"

#include "core/ArtPlan.h"
#include "dsp/ArtEngine.h"
#include "ui/CanvasPlanImage.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace artfreq;

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
    scale (the default log scale unless given), in every column. */
Canvas bandCanvas (double lowHz, double highHz, float value = 1.0f, const FrequencyScale& scale = {})
{
    Canvas canvas;

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

/** How a long-FFT analyser (2^order samples, Hann) sees a band: the share of its bins within 6 dB
    of their median, averaged over a few windows. */
double longFftFill (const std::vector<float>& audio, double lowHz, double highHz, double* medianDb = nullptr, int order = 15)
{
    double share = 0.0, median = 0.0;
    constexpr int windows = 4;

    for (int w = 0; w < windows; ++w)
    {
        const auto spectrum = test::spectrum (audio, static_cast<size_t> (9600 + w * 16384), order, rate48);
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

/** A canvas with one row lit, the one whose centre is nearest `hz` on the default log scale, in
    every column: the thinnest stroke there is. */
Canvas rowCanvas (double hz)
{
    Canvas canvas;
    const FrequencyScale scale;
    const auto y = juce::jlimit (0, Canvas::height - 1, static_cast<int> (std::lround ((1.0 - scale.toNorm (hz)) * Canvas::height - 0.5)));

    for (int x = 0; x < Canvas::width; ++x)
        canvas.at (x, y) = 1.0f;

    return canvas;
}

/** How much a thin feature at `hz` blinks: the spread (95th minus 5th percentile, dB) of the
    power within `withinHz` of it, frame by frame (Hann, 2^order points, a frame every 256 samples). */
double levelSpreadDb (const std::vector<float>& audio, double hz, int order, double withinHz = 20.0)
{
    std::vector<double> levels;
    const auto size = static_cast<size_t> (1) << order;

    for (size_t start = 9600; start + size <= audio.size(); start += 256)
    {
        const auto frame = test::spectrum (audio, start, order, rate48);
        double power = 1.0e-30;

        for (size_t b = 0; b < frame.magnitudes.size(); ++b)
            if (std::abs (static_cast<double> (b) * frame.binHz - hz) <= withinHz)
                power += frame.magnitudes[b] * frame.magnitudes[b];

        levels.push_back (10.0 * std::log10 (power));
    }

    std::sort (levels.begin(), levels.end());
    const auto at = [&] (double share) { return levels[static_cast<size_t> (share * static_cast<double> (levels.size() - 1))]; };
    return at (0.95) - at (0.05);
}

} // namespace

ARTFREQ_TEST (planCombReplacesTheCrowdedLows)
{
    Canvas canvas;
    std::fill (canvas.values.begin(), canvas.values.end(), 1.0f);

    for (const auto density : { 256, 1024 })
    {
        const auto lines = buildPlan (canvas, combSettings (stripes, density));
        const auto comb = buildPlan (canvas, combSettings (smooth, density));

        // The comb's lines: every multiple of combFineSpacingHz from the bottom of the canvas (20
        // Hz) to combFineTopHz, every multiple of combCoarseSpacingHz from there to below the
        // comb's limit.
        CHECK_GREATER (comb->combLines, 200);
        CHECK_LESS (comb->combLines, 270);
        CHECK_NEAR (comb->frequencies[0], 20.0f, 1.0e-4);
        CHECK (comb->combPhases.size() == static_cast<size_t> (comb->combLines));

        for (int k = 0; k < comb->combLines; ++k)
        {
            const auto hz = static_cast<double> (comb->frequencies[static_cast<size_t> (k)]);
            const auto spacing = static_cast<double> (hz < combFineTopHz ? combFineSpacingHz : combCoarseSpacingHz);
            CHECK_NEAR (hz / spacing, std::round (hz / spacing), 1.0e-6);

            if (k > 0)
                CHECK_NEAR (hz - comb->frequencies[static_cast<size_t> (k - 1)], hz <= combFineTopHz ? combFineSpacingHz : combCoarseSpacingHz, 1.0e-3);

            // The sweep: the phase falls with the square of the line's number on its grid.
            const auto number = std::round (hz / spacing);
            const auto expected = std::remainder (-juce::MathConstants<double>::pi * number * number / combPhaseLines, juce::MathConstants<double>::twoPi);
            CHECK_NEAR (std::remainder (static_cast<double> (comb->combPhases[static_cast<size_t> (k)]) - expected, juce::MathConstants<double>::twoPi), 0.0, 1.0e-3);
        }

        // (the comb reaches up to halfway to the first line kept, a little above the limit at most)
        const auto combTop = comb->frequencies[static_cast<size_t> (comb->combLines - 1)];
        CHECK_LESS (combTop, combTopLimitHz + 0.5f * 25.0f);

        // After it, the lines Stripes plays above the lows, the very same (the highs are
        // untouched), at amplitudes that differ only by the normalisation.
        const auto kept = comb->numLines - comb->combLines;
        const auto offset = lines->numLines - kept;   // the replaced lines, first in Stripes' plan
        CHECK_GREATER (offset, 10);
        CHECK_GREATER (kept, 50);
        CHECK (std::memcmp (comb->frequencies.data() + comb->combLines, lines->frequencies.data() + offset,
                            sizeof (float) * static_cast<size_t> (kept)) == 0);
        CHECK_GREATER (comb->frequencies[static_cast<size_t> (comb->combLines)], 1500.0f);
        CHECK_GREATER (comb->frequencies[static_cast<size_t> (comb->combLines)], combTop);
        CHECK_LESS (lines->frequencies[static_cast<size_t> (offset - 1)], combTopLimitHz);

        const auto ratio = comb->amplitude (0, comb->combLines) / lines->amplitude (0, offset);
        auto proportional = true;

        for (int c = 0; c < Canvas::width; c += 37)
            for (int l = 0; l < kept; ++l)
                proportional = proportional && std::abs (comb->amplitude (c, comb->combLines + l) - ratio * lines->amplitude (c, offset + l))
                                                   <= 1.0e-5f * lines->amplitude (c, offset + l) + 1.0e-9f;

        CHECK (proportional);

        // The comb's lines are lines like any other: active wherever they sound.
        CHECK (comb->activeLines.size() == static_cast<size_t> (comb->numLines) * static_cast<size_t> (Canvas::width));
        CHECK (comb->layoutHash != lines->layoutHash);

        // The normalisation: the loudest column (every column here) at planTargetRms.
        double power = 0.0;

        for (int l = 0; l < comb->numLines; ++l)
            power += static_cast<double> (comb->amplitude (0, l)) * comb->amplitude (0, l);

        CHECK_NEAR (std::sqrt (power / 2.0), planTargetRms, 1.0e-3);

        // Played as loud as the gliding comb was for its spacing (see combGain): the closer the
        // lines, the quieter each.
        const auto lineLevel = comb->amplitude (0, comb->combLines);
        CHECK_NEAR (comb->amplitude (0, 10) / lineLevel, combGain * std::sqrt (combFineSpacingHz / combGainSpacingHz), 0.01);
        CHECK_NEAR (comb->amplitude (0, comb->combLines - 10) / lineLevel, combGain * std::sqrt (combCoarseSpacingHz / combGainSpacingHz), 0.01);
    }

    // Stripes is plain lines (no comb); so is Smooth where nothing is crowded (a linear scale).
    const auto lines = buildPlan (canvas, combSettings (stripes));
    CHECK (lines->combLines == 0 && lines->combPhases.empty());

    const auto linear = buildPlan (canvas, combSettings (smooth, 256, ScaleType::linear));
    const auto linearLines = buildPlan (canvas, combSettings (stripes, 256, ScaleType::linear));
    CHECK (linear->combLines == 0);
    CHECK (linear->frequencies == linearLines->frequencies);
    CHECK (linear->amplitudes == linearLines->amplitudes);

    // An empty canvas: laid out, silent.
    const auto empty = buildPlan (Canvas(), combSettings (smooth));
    CHECK (empty->silent);
    CHECK_GREATER (empty->combLines, 200);
}

ARTFREQ_TEST (planLowsNeverPlayBelowTheDrawing)
{
    // The lines that sound in a column, in Hz.
    const auto sounding = [] (const ArtPlan& plan, int column)
    {
        std::vector<double> hz;

        for (int l = 0; l < plan.numLines; ++l)
            if (plan.amplitude (column, l) > 0.0f)
                hz.push_back (static_cast<double> (plan.frequencies[static_cast<size_t> (l)]));

        return hz;
    };

    // A block over 100 - 150 Hz. Smooth: exactly the comb's lines inside it, all at one level.
    // Stripes: its lines stand about 25 Hz apart, and the nearest one below the block (which
    // 0.6 played: a shape drawn from 100 Hz sounded from about 90) stays silent, as does the
    // nearest one above.
    for (const auto lowEnd : { smooth, stripes })
    {
        const auto plan = buildPlan (bandCanvas (100.0, 150.0), combSettings (lowEnd));
        const auto hz = sounding (*plan, 500);
        CHECK_GREATER (static_cast<int> (hz.size()), lowEnd == smooth ? 9 : 0);

        for (const auto f : hz)
            CHECK_MSG (f > 99.0 && f < 151.0, "a line at " << f << " Hz sounds for a block over 100 - 150 Hz");

        if (lowEnd == smooth)
        {
            CHECK (hz.size() == 11 || hz.size() == 10 || hz.size() == 9);
            const auto level = plan->amplitude (500, static_cast<int> (std::lround (125.0 / 5.0)) - 4);

            for (int l = 0; l < plan->combLines; ++l)
                if (plan->amplitude (500, l) > 0.0f)
                    CHECK_NEAR (plan->amplitude (500, l), level, 1.0e-6);
        }
    }

    // A thin stroke between two lines is played by the nearer one, so nothing drawn disappears:
    // one canvas row (0.7 Hz tall at 100 Hz), and a band 8 Hz wide, which holds two of the comb's
    // lines (two alone would beat: see combThinEdge; one of them plays for both).
    for (const auto lowEnd : { smooth, stripes })
    {
        for (const auto& drawing : { rowCanvas (103.0), bandCanvas (98.0, 106.0) })
        {
            const auto plan = buildPlan (drawing, combSettings (lowEnd));
            const auto hz = sounding (*plan, 500);
            CHECK (hz.size() == 1);

            if (hz.size() == 1)
            {
                CHECK_NEAR (hz.front(), 102.5, lowEnd == smooth ? 3.0 : 20.0);

                // The only thing drawn, so the loudest: sqrt (2) planTargetRms.
                for (int l = 0; l < plan->numLines; ++l)
                    if (plan->amplitude (500, l) > 0.0f)
                        CHECK_NEAR (plan->amplitude (500, l), std::sqrt (2.0f) * planTargetRms, 1.0e-4);
            }
        }
    }

    // A bright stroke with a faint fringe that reaches a line is played by that line at the
    // stroke's level, not the fringe's.
    {
        Canvas canvas = rowCanvas (103.0);
        const auto fringe = bandCanvas (99.0, 104.0, 0.1f);

        for (size_t i = 0; i < canvas.values.size(); ++i)
            canvas.values[i] = std::max (canvas.values[i], fringe.values[i]);

        const auto plan = buildPlan (canvas, combSettings (smooth));
        const auto hz = sounding (*plan, 500);
        CHECK (hz.size() == 1);
        CHECK_NEAR (*std::max_element (plan->amplitudes.begin(), plan->amplitudes.end()), std::sqrt (2.0f) * planTargetRms, 1.0e-4);
    }

    // A column with nothing drawn in the lows leaves them silent; the highs play as ever (every
    // row on its nearest line).
    const auto highs = buildPlan (bandCanvas (5000.0, 8000.0), combSettings (smooth));
    CHECK (! highs->silent);

    for (const auto f : sounding (*highs, 500))
        CHECK_GREATER (f, 4900.0);

    CHECK (buildPlan (bandCanvas (5000.0, 8000.0), combSettings (stripes))->amplitudes
           == std::vector<float> (highs->amplitudes.begin(), highs->amplitudes.end()) || highs->combLines > 0);
}

ARTFREQ_TEST (planFadesLowLinesInTime)
{
    // A block over 100 - 150 Hz and another over 4 - 5 kHz, both from column 256 to 767.
    Canvas canvas;

    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto hz = FrequencyScale().toHz (1.0 - (y + 0.5) / Canvas::height);

        if ((hz >= 100.0 && hz <= 150.0) || (hz >= 4000.0 && hz <= 5000.0))
            for (int x = 256; x < 768; ++x)
                canvas.at (x, y) = 1.0f;
    }

    const auto lineAt = [] (const ArtPlan& plan, double hz)
    {
        auto best = 0;

        for (int l = 1; l < plan.numLines; ++l)
            if (std::abs (plan.frequencies[static_cast<size_t> (l)] - hz) < std::abs (plan.frequencies[static_cast<size_t> (best)] - hz))
                best = l;

        return best;
    };

    for (const auto lowEnd : { smooth, stripes })
    {
        // Not told how long a pass takes: every line changes from one column to the next, as drawn.
        auto settings = combSettings (lowEnd);
        const auto hard = buildPlan (canvas, settings);
        const auto low = lineAt (*hard, 125.0), high = lineAt (*hard, 4500.0);
        CHECK (! (hard->amplitude (255, low) > 0.0f) && hard->amplitude (256, low) > 0.0f);
        CHECK (hard->amplitude (767, low) > 0.0f && ! (hard->amplitude (768, low) > 0.0f));

        // An 8 s pass: a column is 7.8 ms. The line at 125 Hz comes and goes over fadeCycles of
        // its sine (96 ms: 12.3 columns), half of it either side of the drawn edge, rising all
        // the way; the line at 4.5 kHz, whose fade would be shorter than a column, as drawn.
        settings.passSeconds = 8.0;
        const auto soft = buildPlan (canvas, settings);
        const auto hz = static_cast<double> (soft->frequencies[static_cast<size_t> (low)]);
        const auto reach = static_cast<int> (std::ceil (0.5 * fadeCycles / hz / (8.0 / Canvas::width)));
        const auto level = soft->amplitude (500, low);
        CHECK_GREATER (level, 0.0f);
        CHECK_GREATER (reach, 4);
        CHECK (! (soft->amplitude (255 - reach, low) > 0.0f));
        CHECK_GREATER (soft->amplitude (256 - reach + 2, low), 0.0f);
        CHECK_NEAR (soft->amplitude (256 + reach, low), level, 1.0e-6);
        CHECK_NEAR (0.5f * (soft->amplitude (255, low) + soft->amplitude (256, low)), 0.5f * level, 0.02f * level);
        CHECK_NEAR (0.5f * (soft->amplitude (767, low) + soft->amplitude (768, low)), 0.5f * level, 0.02f * level);
        CHECK (! (soft->amplitude (768 + reach, low) > 0.0f));

        for (int c = 256 - reach; c < 256 + reach; ++c)
            CHECK (soft->amplitude (c + 1, low) >= soft->amplitude (c, low));

        CHECK (! (soft->amplitude (255, high) > 0.0f) && soft->amplitude (767, high) > 0.0f && ! (soft->amplitude (768, high) > 0.0f));
        CHECK_NEAR (soft->amplitude (256, high), soft->amplitude (500, high), 1.0e-7);

        // The loudest column is still at planTargetRms, and the active lists follow the fades.
        double loudest = 0.0;

        for (int c = 0; c < Canvas::width; ++c)
        {
            double power = 0.0;

            for (int l = 0; l < soft->numLines; ++l)
                power += static_cast<double> (soft->amplitude (c, l)) * soft->amplitude (c, l);

            loudest = std::max (loudest, power);
        }

        CHECK_NEAR (std::sqrt (loudest / 2.0), planTargetRms, 1.0e-4);
        const auto active = [&] (int column, int line)
        {
            const auto first = soft->activeLines.begin() + soft->activeStart[static_cast<size_t> (column)];
            const auto last = soft->activeLines.begin() + soft->activeStart[static_cast<size_t> (column) + 1];
            return std::find (first, last, static_cast<uint16_t> (line)) != last;
        };

        CHECK (active (256 - reach + 2, low));
        CHECK (! active (250 - reach, low));

        // A longer pass has shorter columns... and a longer fade in columns; a very short one caps
        // the fade at a sixteenth of the pass.
        settings.passSeconds = 0.5;
        const auto quick = buildPlan (canvas, settings);
        CHECK (! (quick->amplitude (256 - 33, low) > 0.0f));
        CHECK_GREATER (quick->amplitude (256 - 30, low), 0.0f);
    }

    // Repeat: a shape that runs to the end of the pass leads round into its start. A single pass:
    // silence either side, and the low lines reach silence at both ends inside the pass.
    Canvas toTheEnd;

    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto hz = FrequencyScale().toHz (1.0 - (y + 0.5) / Canvas::height);

        if (hz >= 100.0 && hz <= 150.0)
            for (int x = 512; x < Canvas::width; ++x)
                toTheEnd.at (x, y) = 1.0f;
    }

    auto settings = combSettings (smooth);
    settings.passSeconds = 8.0;
    const auto looped = buildPlan (toTheEnd, settings);
    const auto line = lineAt (*looped, 125.0);
    CHECK_GREATER (looped->amplitude (0, line), 0.0f);
    CHECK_GREATER (looped->amplitude (Canvas::width - 1, line), looped->amplitude (0, line));
    CHECK (! (looped->amplitude (20, line) > 0.0f));

    settings.repeats = false;
    const auto single = buildPlan (toTheEnd, settings);
    CHECK (! (single->amplitude (0, line) > 0.0f));
    CHECK (! (single->amplitude (Canvas::width - 1, line) > 0.0f));
    CHECK_GREATER (single->amplitude (Canvas::width - 3, line), 0.0f);
    CHECK_NEAR (single->amplitude (800, line), looped->amplitude (800, line), 1.0e-6);
}

ARTFREQ_TEST (engineCombFillsTheBand)
{
    // A block over 100 - 500 Hz. With lines 25 Hz apart a long analyser shows stripes: most bins
    // far from the median. The comb fills the band: wholly at 16384 points, and at 32768, where
    // its own lines begin to show, still mostly.
    const auto canvas = bandCanvas (100.0, 500.0);
    const auto lines = buildPlan (canvas, combSettings (stripes));
    const auto comb = buildPlan (canvas, combSettings (smooth));

    const auto linesAudio = render (*lines, 2.0);   // the four windows of longFftFill and no less
    const auto combAudio = render (*comb, 2.0);

    double medianDb = 0.0;

    for (const auto order : { 14, 15 })
    {
        const auto linesFill = longFftFill (linesAudio, 130.0, 470.0, nullptr, order);
        const auto combFill = longFftFill (combAudio, 130.0, 470.0, &medianDb, order);
        std::printf ("    %d-sample FFT, 130 - 470 Hz within 6 dB of the median: lines %.2f, comb %.2f\n", 1 << order, linesFill, combFill);
        CHECK_LESS (linesFill, 0.5);
        CHECK_GREATER (combFill, order == 14 ? 0.85 : 0.65);
    }

    // Nothing sounds outside the band: a steady line has no skirt but the analyser's own. (The
    // gliding comb of 0.6 left -30 dB below the band and -40 dB above it.)
    const auto below = longFftLoudest (combAudio, 20.0, 80.0) - medianDb;
    const auto above = longFftLoudest (combAudio, 560.0, 4000.0) - medianDb;
    std::printf ("    loudest bin against the band's median: %.1f dB below it (20 - 80 Hz), %.1f dB above (560 Hz - 4 kHz)\n", below, above);
    CHECK_LESS (below, -60.0);
    CHECK_LESS (above, -60.0);

    // At the plan's level: the loudest column (every column here) at planTargetRms.
    CHECK_NEAR (test::toDb (test::rms (combAudio, 9600) / planTargetRms), 0.0, 1.0);
}

ARTFREQ_TEST (engineCombIsDeterministic)
{
    const auto plan = buildPlan (bandCanvas (40.0, 1500.0), combSettings (smooth));

    const auto first = render (*plan, 1.0);
    const auto second = render (*plan, 1.0);
    CHECK (first == second);
    CHECK_GREATER (test::rms (first), 0.05);

    // The block size moves only rounding.
    const auto oddBlocks = render (*plan, 1.0, 61);
    double largestDifference = 0.0;

    for (size_t i = 0; i < first.size(); ++i)
        largestDifference = std::max (largestDifference, static_cast<double> (std::abs (first[i] - oddBlocks[i])));

    CHECK_LESS (largestDifference, 5.0e-3);

    // The sum never peaks: the comb's phases keep its lines from lining up (whole numbers of
    // lines per sweep made a train of clicks 5 to 10 dB taller).
    const auto crestDb = test::toDb (test::peak (first, 9600) / test::rms (first, 9600));
    std::printf ("    crest factor of a block over 40 Hz - 1.5 kHz: %.1f dB\n", crestDb);
    CHECK_LESS (crestDb, 12.0);
}

ARTFREQ_TEST (engineCombLinesKeepTheirPhasesThroughSilence)
{
    // A block over 100 - 150 Hz that plays, stops and plays again, against the same block playing
    // throughout: once it is back, the sound is the very same, sample for sample. Each of the
    // comb's lines starts again where it would have been had it never stopped, which is what
    // keeps the lines of a shape in their pattern whenever each of them came on.
    Canvas throughout = bandCanvas (100.0, 150.0), interrupted = throughout;

    for (int y = 0; y < Canvas::height; ++y)
        for (int x = 300; x < 500; ++x)
            interrupted.at (x, y) = 0.0f;

    const auto a = render (*buildPlan (throughout, combSettings (smooth)), 6.0);
    const auto b = render (*buildPlan (interrupted, combSettings (smooth)), 6.0);
    const auto column = [] (int c) { return static_cast<size_t> (c * 8.0 * rate48 / Canvas::width); };

    CHECK_LESS (test::peak (b, column (305), column (495)), 1.0e-6);

    double largestDifference = 0.0;

    for (auto i = column (510); i < column (700); ++i)
        largestDifference = std::max (largestDifference, static_cast<double> (std::abs (a[i] - b[i])));

    std::printf ("    after 1.5 s of silence: largest difference from the uninterrupted sound %.2g (peak %.2f)\n",
                 largestDifference, test::peak (a, column (510), column (700)));
    CHECK_LESS (largestDifference, 2.0e-3);
}

ARTFREQ_TEST (engineCombCrossfadesWithoutClicks)
{
    // Two plans with one layout (the lines go on, their amplitudes cross), one without a comb
    // (other lines entirely), and none.
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

ARTFREQ_TEST (engineCombPerformance)
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
            std::printf ("    density %4d, %.1f s drawing, all active: smooth (%d comb lines + %d lines) %6.1f x real time, stripes (%d lines) %6.1f x\n",
                         density, passSeconds, plan->combLines, plan->numLines - plan->combLines, combSpeed,
                         lines->numLines, speed (*lines, passSeconds));
            CHECK_GREATER (combSpeed, 40.0);
        }
    }
}

ARTFREQ_TEST (engineCombKeepsItsPowerInTheDrawnBand)
{
    // A block over 100 - 300 Hz, played for 8 s and averaged by a long analyser (32768 points,
    // Hann, hop a quarter): the power lies in the drawn band and nowhere else, spread evenly
    // over it. (The gliding comb of 0.6: 0.995 inside, 2e-5 beyond 40 Hz from the band; here
    // only the analyser's own window is left out there.)
    const auto plan = buildPlan (bandCanvas (100.0, 300.0), combSettings (smooth));
    const auto audio = render (*plan, 8.0);
    const auto [power, binHz] = averagePower (audio, 9600, 15);

    const auto total = powerBetween (power, binHz, 0.0, 24000.0);
    const auto inside = powerBetween (power, binHz, 95.0, 305.0);
    const auto far = powerBetween (power, binHz, 0.0, 60.0) + powerBetween (power, binHz, 340.0, 24000.0);
    std::printf ("    inside %.6f, beyond 40 Hz %.2g\n", inside / total, far / total);
    CHECK_GREATER (inside / total, 0.9999);
    CHECK_LESS (far / total, 1.0e-7);

    // Even across the band: each 20 Hz of it holds a tenth of the power, within 1.5 dB.
    for (double hz = 100.0; hz < 299.0; hz += 20.0)
    {
        const auto share = powerBetween (power, binHz, hz - 2.5, hz + 17.5) / inside;
        CHECK_MSG (std::abs (10.0 * std::log10 (share * 205.0 / 20.0)) < 1.5,
                   hz << " - " << hz + 20.0 << " Hz holds " << 10.0 * std::log10 (share * 205.0 / 20.0) << " dB of its share");
    }
}

ARTFREQ_TEST (engineCombPlaysThinStrokesSteadily)
{
    // Two of the comb's lines alone would beat as one pulse, five times a second (14 dB on an
    // analyser of 4096 points, 8 dB at 8192): a stroke that thin must hold steady instead, played
    // by one line: one canvas row and 5 Hz wide, at 100 and 400 Hz (where a row is 0.7 and 2.7 Hz
    // tall). The narrowest strokes the comb itself plays (10 Hz: three lines, the outer two
    // softened; 20 Hz: four or five) shimmer, by more than a wide shape does (a few lines are too
    // few for the sweep to even out), but never blink: under 8 dB at 4096 points, 4 dB at 8192.
    // (The gliding comb of 0.6 kept strokes of 20 Hz within 4 and 2 dB, and played thinner ones
    // on one line: its sines were 20 Hz apart, one in such a stroke at any time; the price of
    // that was the drip.)
    struct Stroke
    {
        double hz, widthHz;   // width 0: one canvas row
        bool thin;
    };

    for (const auto& stroke : { Stroke { 100.0, 0.0, true }, Stroke { 100.0, 5.0, true }, Stroke { 102.5, 6.0, true },
                                Stroke { 400.0, 0.0, true }, Stroke { 400.0, 5.0, true },
                                Stroke { 100.0, 10.0, false }, Stroke { 400.0, 10.0, false },
                                Stroke { 100.0, 20.0, false }, Stroke { 400.0, 20.0, false }, Stroke { 100.0, 40.0, false } })
    {
        const auto hz = stroke.hz;
        const auto canvas = stroke.widthHz > 0.0 ? bandCanvas (hz - 0.5 * stroke.widthHz, hz + 0.5 * stroke.widthHz) : rowCanvas (hz);
        const auto smoothAudio = render (*buildPlan (canvas, combSettings (smooth)), 2.0);
        const auto stripesAudio = render (*buildPlan (canvas, combSettings (stripes)), 2.0);

        for (const auto order : { 12, 13 })
        {
            const auto smoothSpread = levelSpreadDb (smoothAudio, hz, order, 20.0 + 0.5 * stroke.widthHz);
            const auto stripesSpread = levelSpreadDb (stripesAudio, hz, order, 20.0 + 0.5 * stroke.widthHz);
            std::printf ("    %.0f Hz stroke %s%.0f Hz wide, FFT %d: level spread smooth %.1f dB, stripes %.1f dB\n", hz,
                         stroke.widthHz > 0.0 ? "" : "(one row) ", stroke.widthHz, 1 << order, smoothSpread, stripesSpread);

            if (stroke.thin)
                CHECK_LESS (smoothSpread, 0.5);
            else
                CHECK_LESS (smoothSpread, order == 12 ? 8.0 : 4.0);
        }

        // It sounds, at the plan's level, where it was drawn.
        CHECK_NEAR (test::toDb (test::rms (smoothAudio, 9600) / planTargetRms), 0.0, 1.0);
        const auto [power, binHz] = averagePower (smoothAudio, 9600, 14);
        CHECK_GREATER (powerBetween (power, binHz, hz - 25.0, hz + 25.0) / powerBetween (power, binHz, 0.0, 24000.0), 0.999);
    }
}

ARTFREQ_TEST (engineLowsLeaveNothingBelowTheCanvas)
{
    // A drawing from the canvas's bottom up to 200 Hz, with and without an analyser slope (which
    // makes the lowest heights the loudest), at the lowest low end, the default one and a higher
    // one: nothing is played below the lowest line (10 Hz at the lowest), nothing far above the
    // drawing, and no clicks. (The gliding comb's sines faded in at the bottom over a cycle or
    // so, every 0.1 s: about -30 dB of rumble below 5 Hz.)
    for (const auto lowHz : { 10.0, 20.0, 30.0 })
    {
        FrequencyScale scale;
        scale.minHz = lowHz;
        const auto canvas = bandCanvas (0.0, 200.0, 1.0f, scale);

        for (const auto slope : { 0.0f, 4.5f })
        {
            for (const auto lowEnd : { smooth, stripes })
            {
                auto settings = combSettings (lowEnd);
                settings.scale = scale;
                settings.slopeDbPerOctave = slope;
                const auto plan = buildPlan (canvas, settings);
                const auto audio = render (*plan, 4.0);
                const auto [power, binHz] = averagePower (audio, 9600, 16);
                const auto total = powerBetween (power, binHz, 0.0, 24000.0);
                const auto dB = [] (double ratio) { return 10.0 * std::log10 (ratio + 1.0e-30); };
                const auto belowDb = dB (powerBetween (power, binHz, 0.0, lowHz - 5.0) / total);
                const auto aboveDb = dB (powerBetween (power, binHz, 400.0, 24000.0) / total);
                std::printf ("    low end %.0f Hz, slope %.1f, %s: lowest line %.1f Hz; more than 5 Hz below the low end %.1f dB; above 400 Hz %.1f dB\n",
                             lowHz, static_cast<double> (slope), lowEnd == smooth ? "smooth" : "stripes",
                             static_cast<double> (plan->frequencies[0]), belowDb, aboveDb);

                CHECK (plan->frequencies[0] >= static_cast<float> (lowHz) && plan->frequencies[0] >= lowestPlayedHz);
                CHECK_LESS (aboveDb, -72.0);

                if (lowHz >= 20.0)
                    CHECK_LESS (belowDb, -60.0);
            }
        }
    }
}

ARTFREQ_TEST (canvasShowsSmoothLowsAsAFilledBand)
{
    // A block over 60 - 900 Hz and another over 3 - 4 kHz, all at full value. The canvas's fast
    // preview draws what an analyser of 8192 points shows: the comb's band filled at every
    // height, Stripes as lines with darker gaps between them, and the comb as bright as the lines
    // above it.
    Canvas canvas = bandCanvas (60.0, 900.0);
    const auto highs = bandCanvas (3000.0, 4000.0);

    for (size_t i = 0; i < canvas.values.size(); ++i)
        canvas.values[i] = std::max (canvas.values[i], highs.values[i]);

    const FrequencyScale scale;
    constexpr int width = 200, height = 500;
    const auto& table = ui::theme::spectrumTable();
    ui::AnalyserModel model;
    model.scale = scale;

    const auto picture = [&] (LowEnd lowEnd)
    {
        std::shared_ptr<ArtPlan> plan (buildPlan (canvas, combSettings (lowEnd)).release());
        plan->version = lowEnd == smooth ? 1 : 2;
        ui::CanvasPlanImage image;
        return image.get (plan, model, width, height);
    };

    // The colour-map level of a pixel (0..255).
    const auto levelAt = [&] (const juce::Image& image, int x, int y)
    {
        const auto pixel = image.getPixelAt (x, y);
        auto best = 0, bestDistance = 1 << 30;

        for (int i = 0; i < 256; ++i)
        {
            const auto c = juce::Colour (table[static_cast<size_t> (i)].getUnpremultiplied());
            const auto dr = c.getRed() - pixel.getRed(), dg = c.getGreen() - pixel.getGreen(), db = c.getBlue() - pixel.getBlue();

            if (dr * dr + dg * dg + db * db < bestDistance)
            {
                bestDistance = dr * dr + dg * dg + db * db;
                best = i;
            }
        }

        return best;
    };

    const auto rowOf = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * height); };

    // Rows more than 12 dB (31 levels) darker than the brightest row in a range of heights.
    const auto gapRows = [&] (const juce::Image& image, double lowHz, double highHz)
    {
        auto brightest = 0;

        for (int y = rowOf (highHz); y <= rowOf (lowHz); ++y)
            brightest = std::max (brightest, levelAt (image, width / 2, y));

        int gaps = 0;

        for (int y = rowOf (highHz); y <= rowOf (lowHz); ++y)
            gaps += levelAt (image, width / 2, y) < brightest - 31 ? 1 : 0;

        return gaps;
    };

    const auto smoothImage = picture (smooth);
    const auto stripesImage = picture (stripes);
    const auto rows = rowOf (200.0) - rowOf (800.0) + 1;
    std::printf ("    rows 12 dB darker in 200 - 800 Hz (of %d): smooth %d, stripes %d\n", rows, gapRows (smoothImage, 200.0, 800.0),
                 gapRows (stripesImage, 200.0, 800.0));
    CHECK (gapRows (smoothImage, 70.0, 800.0) == 0);
    CHECK_GREATER (gapRows (stripesImage, 200.0, 800.0), rows / 5);   // (a fifth: the analyser's window is a third wider than Hann's)

    // Inside the comb's band and on the lines at 3 - 4 kHz: as bright as each other (within
    // 3 dB), and the level an analyser reads, far above the floor.
    const auto combLevel = levelAt (smoothImage, width / 2, rowOf (200.0));
    const auto lineLevel = levelAt (smoothImage, width / 2, rowOf (3500.0));
    CHECK_NEAR (combLevel, lineLevel, 8);
    CHECK_GREATER (combLevel, 128);

    // Nothing is drawn under the block but the analyser's own blur: dark from an octave below.
    CHECK (levelAt (smoothImage, width / 2, rowOf (28.0)) == 0);

    // A thin stroke is one line, drawn as the analyser draws a sine: lit where it plays, dark
    // again a few bins away.
    for (const auto hz : { 21.0, 100.0, 400.0 })
    {
        std::shared_ptr<ArtPlan> plan (buildPlan (rowCanvas (hz), combSettings (smooth)).release());
        plan->version = 3;
        ui::CanvasPlanImage image;
        const auto thin = image.get (plan, model, width, height);
        auto lit = 0;

        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; x += 7)
                lit += thin.getPixelAt (x, y).getBrightness() > 0.2f ? 1 : 0;

        CHECK_GREATER (lit, 0);
        CHECK_GREATER (levelAt (thin, width / 2, rowOf (hz)), 128);
        CHECK (levelAt (thin, width / 2, rowOf (hz * 1.5 + 30.0)) == 0);
    }
}
