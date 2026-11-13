// The plan: the canvas turned into a bank of sine lines.

#include "TestFramework.h"

#include "core/ArtPlan.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace freqart;

namespace
{

PlanSettings settingsFor (ScaleType type, int density, double minHz = 20.0, double maxHz = 20000.0)
{
    PlanSettings settings;
    settings.scale.type = type;
    settings.scale.minHz = minHz;
    settings.scale.maxHz = maxHz;
    settings.density = density;
    // The layout tests were written for a 5 Hz minimum spacing, which keeps every line of the
    // sparser layouts; the product default (wider, see planDefaultSpacing) is tested separately.
    settings.minLineSpacingHz = 5.0f;
    return settings;
}

/** The documented layout, written out independently: frequencies of the kept lines. */
std::vector<double> expectedFrequencies (const PlanSettings& settings)
{
    std::vector<double> kept;

    for (int i = 0; i < settings.density; ++i)
    {
        const auto hz = settings.scale.toHz ((i + 0.5) / settings.density);

        if (hz < 10.0 || hz > settings.maxFrequencyHz)
            continue;

        if (! kept.empty() && hz - kept.back() < settings.minLineSpacingHz)
            continue;

        kept.push_back (hz);
    }

    return kept;
}

double columnRms (const ArtPlan& plan, int column)
{
    double power = 0.0;

    for (int l = 0; l < plan.numLines; ++l)
        power += static_cast<double> (plan.amplitude (column, l)) * plan.amplitude (column, l);

    return std::sqrt (power / 2.0);
}

double loudestColumnRms (const ArtPlan& plan)
{
    double loudest = 0.0;

    for (int c = 0; c < plan.numColumns; ++c)
        loudest = std::max (loudest, columnRms (plan, c));

    return loudest;
}

void fillRow (Canvas& canvas, int row, float value, int firstColumn = 0, int lastColumn = Canvas::width - 1)
{
    for (int x = firstColumn; x <= lastColumn; ++x)
        canvas.at (x, row) = value;
}

double toDb (double gain)
{
    return 20.0 * std::log10 (gain);
}

double millisecondsSince (std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
}

/** Every column's active list must be exactly the lines sounding at c or c + 1 (wrapping). */
bool activeListsMatch (const ArtPlan& plan)
{
    if (plan.activeStart.size() != static_cast<size_t> (plan.numColumns) + 1 || plan.activeStart.front() != 0
        || plan.activeStart.back() != plan.activeLines.size())
        return false;

    for (int c = 0; c < plan.numColumns; ++c)
    {
        std::vector<uint16_t> expected;
        const auto next = (c + 1) % plan.numColumns;

        for (int l = 0; l < plan.numLines; ++l)
            if (plan.amplitude (c, l) > 0.0f || plan.amplitude (next, l) > 0.0f)
                expected.push_back (static_cast<uint16_t> (l));

        const std::vector<uint16_t> actual (plan.activeLines.begin() + plan.activeStart[static_cast<size_t> (c)],
                                            plan.activeLines.begin() + plan.activeStart[static_cast<size_t> (c) + 1]);

        if (actual != expected)
            return false;
    }

    return true;
}

} // namespace

FREQART_TEST (planLineLayout)
{
    const Canvas empty;

    for (const auto type : { ScaleType::log, ScaleType::linear, ScaleType::mel })
    {
        uint64_t previousHash = 0;

        for (const auto density : { 16, 256, 1024 })
        {
            const auto settings = settingsFor (type, density);
            const auto plan = buildPlan (empty, settings);
            const auto expected = expectedFrequencies (settings);

            CHECK_MSG (plan->numLines == static_cast<int> (expected.size()),
                       "type " << static_cast<int> (type) << " density " << density << ": " << plan->numLines << " lines");
            CHECK (plan->numColumns == Canvas::width);
            CHECK (plan->frequencies.size() == static_cast<size_t> (plan->numLines));
            CHECK (plan->lineNorm.size() == static_cast<size_t> (plan->numLines));
            CHECK (plan->amplitudes.size() == static_cast<size_t> (plan->numLines) * Canvas::width);

            for (size_t l = 0; l < expected.size() && l < plan->frequencies.size(); ++l)
            {
                // Crowded lines are offset by up to a quarter of the minimum spacing.
                CHECK_NEAR (plan->frequencies[l], expected[l],
                            std::max (expected[l] * 1.0e-6, 0.25 * settings.minLineSpacingHz + 1.0e-3));
                CHECK_NEAR (plan->lineNorm[l], settings.scale.toNorm (plan->frequencies[l]), 1.0e-5);
            }

            for (size_t l = 1; l < plan->frequencies.size(); ++l)
                CHECK (plan->frequencies[l] - plan->frequencies[l - 1] >= 0.5f * settings.minLineSpacingHz - 1.0e-3f);

            CHECK (plan->frequencies.front() >= 10.0f);
            CHECK (plan->frequencies.back() <= settings.maxFrequencyHz);

            // Same settings, same layout; different density, different layout.
            CHECK (buildPlan (empty, settings)->layoutHash == plan->layoutHash);
            CHECK (plan->layoutHash != previousHash);
            previousHash = plan->layoutHash;
        }
    }

    // Spot checks: no merging is needed at density 16 or on a linear axis.
    CHECK (buildPlan (empty, settingsFor (ScaleType::log, 16))->numLines == 16);
    CHECK (buildPlan (empty, settingsFor (ScaleType::linear, 1024))->numLines == 1024);
    CHECK_NEAR (buildPlan (empty, settingsFor (ScaleType::linear, 16))->frequencies[0], 20.0 + 19980.0 / 32.0, 1.0e-2);

    // Log at density 1024: the lowest lines are only fractions of a Hz apart and get merged.
    const auto dense = buildPlan (empty, settingsFor (ScaleType::log, 1024));
    CHECK_LESS (dense->numLines, 1024);
    CHECK_GREATER (dense->numLines, 550);  // about 140 merged lines below 740 Hz, one per row above

    // Density is clamped to 16..1024.
    CHECK (buildPlan (empty, settingsFor (ScaleType::linear, 3))->numLines == 16);
    CHECK (buildPlan (empty, settingsFor (ScaleType::linear, 5000))->numLines == 1024);
}

FREQART_TEST (planSpacingAndLimits)
{
    const Canvas empty;

    auto wide = settingsFor (ScaleType::log, 1024);
    wide.minLineSpacingHz = 50.0f;
    const auto spaced = buildPlan (empty, wide);

    for (size_t l = 1; l < spaced->frequencies.size(); ++l)
        CHECK (spaced->frequencies[l] - spaced->frequencies[l - 1] >= 25.0f - 1.0e-3f);

    // Lines above the maximum frequency are dropped.
    auto capped = settingsFor (ScaleType::linear, 256, 20.0, 24000.0);
    capped.maxFrequencyHz = 10000.0f;
    const auto cappedPlan = buildPlan (empty, capped);
    CHECK (cappedPlan->frequencies.back() <= 10000.0f);
    CHECK_LESS (cappedPlan->numLines, 128);

    // Lines below 10 Hz are dropped.
    const auto low = buildPlan (empty, settingsFor (ScaleType::linear, 1024, 1.0, 2000.0));
    CHECK (low->frequencies.front() >= 10.0f);
    CHECK_LESS (low->numLines, 1024);
}

FREQART_TEST (planSingleRowBecomesOneLine)
{
    for (const auto type : { ScaleType::log, ScaleType::linear, ScaleType::mel })
    {
        for (const auto density : { 16, 256, 1024 })
        {
            Canvas canvas;
            constexpr int row = 300;
            fillRow (canvas, row, 1.0f);

            const auto settings = settingsFor (type, density);
            const auto plan = buildPlan (canvas, settings);
            CHECK (! plan->silent);

            // The line whose span holds the row: its cell is floor (rowFromBottom * density / height).
            const auto rowFromBottom = Canvas::height - 1 - row;
            const auto cell = rowFromBottom * density / Canvas::height;
            const auto expectedNorm = (cell + 0.5) / density;

            int sounding = 0, soundingLine = -1;

            for (int l = 0; l < plan->numLines; ++l)
            {
                if (plan->amplitude (0, l) > 0.0f)
                {
                    ++sounding;
                    soundingLine = l;
                }
            }

            CHECK_MSG (sounding == 1, "type " << static_cast<int> (type) << " density " << density << ": " << sounding << " lines");

            if (soundingLine >= 0)
            {
                CHECK_NEAR (plan->lineNorm[static_cast<size_t> (soundingLine)], expectedNorm, 1.0e-6);

                // One sine: RMS = a / sqrt (2) = planTargetRms in every column.
                for (const auto c : { 0, 500, 1023 })
                    CHECK_NEAR (plan->amplitude (c, soundingLine), planTargetRms * std::sqrt (2.0), 1.0e-4);
            }

            CHECK_NEAR (loudestColumnRms (*plan), planTargetRms, 1.0e-3);
        }
    }

    // Where density does not divide the canvas height a row can sit on a boundary between two
    // spans: then both lines take it, never none.
    Canvas canvas;
    for (int row = 0; row < Canvas::height; row += 37)
        fillRow (canvas, row, 1.0f, row, row);

    const auto plan = buildPlan (canvas, settingsFor (ScaleType::linear, 300));

    for (int row = 0; row < Canvas::height; row += 37)
    {
        int sounding = 0;

        for (int l = 0; l < plan->numLines; ++l)
            if (plan->amplitude (row, l) > 0.0f)
                ++sounding;

        CHECK (sounding == 1 || sounding == 2);
    }
}

FREQART_TEST (planMergedLinesKeepTheirRows)
{
    // Log, density 1024: the bottom rows are merged into few lines. A thin stroke on any row
    // must still sound (it lands in the span of the line it was merged into).
    const auto settings = settingsFor (ScaleType::log, 1024);

    for (const auto row : { 1023, 1015, 1000, 990 })
    {
        Canvas canvas;
        fillRow (canvas, row, 1.0f);
        const auto plan = buildPlan (canvas, settings);
        CHECK_MSG (! plan->silent, "row " << row);

        // The sounding line sits at or below the row's height (merged lines keep the lowest).
        const auto rowNorm = (Canvas::height - 1 - row + 0.5) / Canvas::height;

        for (int l = 0; l < plan->numLines; ++l)
            if (plan->amplitude (0, l) > 0.0f)
                CHECK (plan->lineNorm[static_cast<size_t> (l)] <= rowNorm + 1.0e-6);
    }
}

FREQART_TEST (planContrastMapping)
{
    for (const auto contrast : { 12.0f, 40.0f, 72.0f })
    {
        Canvas canvas;
        fillRow (canvas, 200, 1.0f);
        fillRow (canvas, 600, 0.5f);
        fillRow (canvas, 800, 0.0005f);  // below the silence threshold

        auto settings = settingsFor (ScaleType::linear, 1024);
        settings.contrastDb = contrast;
        const auto plan = buildPlan (canvas, settings);

        const auto loud = Canvas::height - 1 - 200;   // linear, density 1024: line index = row from bottom
        const auto half = Canvas::height - 1 - 600;
        const auto faint = Canvas::height - 1 - 800;

        CHECK_NEAR (toDb (plan->amplitude (10, half) / plan->amplitude (10, loud)), -contrast / 2.0, 0.01);
        CHECK (plan->amplitude (10, faint) <= 0.0f);
        CHECK_NEAR (loudestColumnRms (*plan), planTargetRms, 1.0e-3);
    }

    // A value of 1 vs a value just above 0 is the whole contrast.
    Canvas canvas;
    fillRow (canvas, 100, 1.0f);
    fillRow (canvas, 900, 0.0011f);
    auto settings = settingsFor (ScaleType::linear, 1024);
    settings.contrastDb = 40.0f;
    const auto plan = buildPlan (canvas, settings);
    CHECK_NEAR (toDb (plan->amplitude (0, Canvas::height - 1 - 900) / plan->amplitude (0, Canvas::height - 1 - 100)), -40.0 * (1.0 - 0.0011), 0.01);
}

FREQART_TEST (planSlopeCompensation)
{
    // A log scale on which line 4 is exactly 500 Hz and line 8 exactly 1 kHz (4 lines per octave).
    const auto minHz = 500.0 / std::pow (2.0, 1.125);
    auto settings = settingsFor (ScaleType::log, 16, minHz, minHz * 16.0);
    settings.slopeDbPerOctave = 4.5f;

    Canvas canvas;
    const auto rowForLine = [] (int line) { return Canvas::height - 1 - (line * 64 + 32); };
    fillRow (canvas, rowForLine (4), 1.0f);
    fillRow (canvas, rowForLine (8), 1.0f);
    fillRow (canvas, rowForLine (12), 1.0f);

    const auto plan = buildPlan (canvas, settings);
    CHECK (plan->numLines == 16);
    CHECK_NEAR (plan->frequencies[4], 500.0, 1.0e-3);
    CHECK_NEAR (plan->frequencies[8], 1000.0, 1.0e-3);

    // The analyser shows highs louder by 4.5 dB/oct: one octave below the pivot is lifted 4.5 dB.
    CHECK_NEAR (toDb (plan->amplitude (0, 4) / plan->amplitude (0, 8)), 4.5, 0.01);
    CHECK_NEAR (toDb (plan->amplitude (0, 12) / plan->amplitude (0, 8)), -4.5, 0.01);
    CHECK_NEAR (loudestColumnRms (*plan), planTargetRms, 1.0e-3);

    settings.slopeDbPerOctave = 0.0f;
    const auto flat = buildPlan (canvas, settings);
    CHECK_NEAR (toDb (flat->amplitude (0, 4) / flat->amplitude (0, 8)), 0.0, 0.01);

    settings.slopeDbPerOctave = -3.0f;
    const auto negative = buildPlan (canvas, settings);
    CHECK_NEAR (toDb (negative->amplitude (0, 4) / negative->amplitude (0, 8)), -3.0, 0.01);
}

FREQART_TEST (planEmptyCanvasIsSilent)
{
    const Canvas empty;
    const auto plan = buildPlan (empty, settingsFor (ScaleType::log, 256));
    CHECK (plan->silent);
    CHECK_GREATER (plan->numLines, 0);
    CHECK (plan->activeLines.empty());
    CHECK (std::all_of (plan->activeStart.begin(), plan->activeStart.end(), [] (uint32_t s) { return s == 0; }));
    CHECK (std::all_of (plan->amplitudes.begin(), plan->amplitudes.end(), [] (float a) { return a <= 0.0f; }));
    CHECK (plan->version == 0);

    // Values at or below 0.001 count as nothing.
    Canvas faint;
    fillRow (faint, 500, 0.001f);
    CHECK (buildPlan (faint, settingsFor (ScaleType::log, 256))->silent);
}

FREQART_TEST (planNormalisationAndActiveLists)
{
    juce::Random random (7);
    Canvas canvas;

    // Blobs of varying loudness, one touching only the last column and one only the first, so
    // the wrap from the last column to the first is exercised.
    for (int blob = 0; blob < 40; ++blob)
    {
        const auto x0 = random.nextInt (Canvas::width - 40);
        const auto y0 = random.nextInt (Canvas::height - 40);
        const auto value = 0.2f + 0.8f * random.nextFloat();

        for (int y = y0; y < y0 + random.nextInt (30) + 1; ++y)
            for (int x = x0; x < x0 + random.nextInt (30) + 1; ++x)
                canvas.at (x, y) = value;
    }

    fillRow (canvas, 100, 0.7f, Canvas::width - 1, Canvas::width - 1);
    fillRow (canvas, 900, 0.9f, 0, 0);

    for (const auto type : { ScaleType::log, ScaleType::linear, ScaleType::mel })
    {
        for (const auto density : { 16, 100, 256, 1024 })
        {
            const auto plan = buildPlan (canvas, settingsFor (type, density));
            CHECK (! plan->silent);
            CHECK_NEAR (loudestColumnRms (*plan), planTargetRms, 1.0e-3);
            CHECK_MSG (activeListsMatch (*plan), "type " << static_cast<int> (type) << " density " << density);
        }
    }

    // The wrap: the last column's list includes what sounds in column 0.
    const auto plan = buildPlan (canvas, settingsFor (ScaleType::linear, 1024));
    const auto line900 = Canvas::height - 1 - 900;
    const auto lastBegin = plan->activeLines.begin() + plan->activeStart[Canvas::width - 1];
    const auto lastEnd = plan->activeLines.begin() + plan->activeStart[Canvas::width];
    CHECK (std::find (lastBegin, lastEnd, static_cast<uint16_t> (line900)) != lastEnd);
    CHECK (plan->amplitude (Canvas::width - 1, line900) <= 0.0f);
}

FREQART_TEST (planTiming)
{
    juce::Random random (3);
    Canvas canvas;

    for (auto& v : canvas.values)
        v = random.nextFloat() < 0.4f ? random.nextFloat() : 0.0f;

    const auto settings = settingsFor (ScaleType::log, 1024);
    buildPlan (canvas, settings);  // warm up

    constexpr int runs = 5;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < runs; ++i)
        CHECK (! buildPlan (canvas, settings)->silent);

    const auto ms = millisecondsSince (start) / runs;
    std::printf ("    buildPlan density 1024: %.2f ms (budget 15 ms)\n", ms);
    CHECK_LESS (ms, 4 * 15.0);
}

FREQART_TEST (planDefaultSpacing)
{
    // Lines closer than an analyser can separate beat in visible pulses; the default spacing
    // keeps the lows steady (25 Hz beats average out inside a typical analysis window).
    PlanSettings settings;
    CHECK_NEAR (settings.minLineSpacingHz, 25.0f, 1.0e-6);

    settings.density = 1024;
    const Canvas empty;
    const auto plan = buildPlan (empty, settings);

    for (int l = 1; l < plan->numLines; ++l)
        CHECK (plan->frequencies[static_cast<size_t> (l)] - plan->frequencies[static_cast<size_t> (l - 1)] >= 12.5f - 1.0e-3f);

    // The highs keep their full density.
    CHECK_GREATER (plan->numLines, 300);
}
