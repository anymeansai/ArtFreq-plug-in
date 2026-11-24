#include "ArtPlan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace artfreq
{

namespace
{

constexpr auto lowestLineHz = static_cast<double> (lowestPlayedHz);   // below this nothing is audible or visible: dropped
constexpr float silenceThreshold = 0.001f;
constexpr int minDensity = 16, maxDensity = 1024;

/** A kept line: its frequency and the canvas cells (one per density step, 0 = bottom) it stands
    for (its own and those of the lines merged into it). Only the outer edges of the first and
    last lines' cells bound the rows that sound; between lines, each row goes to the nearest. */
struct LineLayout
{
    double hz = 0.0;
    double norm = 0.0;
    int firstCell = 0;
    int lastCell = 0;
};

float finiteOr (float value, float fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

/** 10^((v - 1) * contrast / 20) tabulated over v = 0..1 and read with linear interpolation:
    a million pow() calls per plan would take longer than the whole rest of the build. */
class LoudnessTable
{
public:
    explicit LoudnessTable (double contrastDb)
    {
        for (size_t i = 0; i < table.size(); ++i)
        {
            const auto v = static_cast<double> (i) / static_cast<double> (size);
            table[i] = static_cast<float> (std::pow (10.0, (v - 1.0) * contrastDb / 20.0));
        }
    }

    /** Amplitude for a canvas value; 0 at or below the silence threshold (and for NaN).
        Branch-free, as canvases full of fine detail would make the branches unpredictable. */
    float operator() (float value) const noexcept
    {
        const auto v = std::min (std::max (0.0f, value), 1.0f);
        const auto position = v * static_cast<float> (size);
        const auto index = std::min (size - 1, static_cast<int> (position));
        const auto fraction = position - static_cast<float> (index);
        const auto a = table[static_cast<size_t> (index)];
        const auto b = table[static_cast<size_t> (index + 1)];
        const auto amplitude = a + (b - a) * fraction;
        return v > silenceThreshold ? amplitude : 0.0f;
    }

private:
    static constexpr int size = 4096;
    std::array<float, size + 1> table {};
};

uint64_t hashFrequencies (const std::vector<float>& frequencies, int combLines) noexcept
{
    // FNV-1a over the line count and the exact bit patterns of the frequencies.
    uint64_t hash = 14695981039346656037ull;

    const auto mix = [&hash] (uint32_t word)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            hash ^= (word >> shift) & 0xffu;
            hash *= 1099511628211ull;
        }
    };

    mix (static_cast<uint32_t> (frequencies.size()));

    for (const auto f : frequencies)
    {
        uint32_t bits = 0;
        std::memcpy (&bits, &f, sizeof (bits));
        mix (bits);
    }

    // The comb's lines start from phases of their own: never the same layout as plain lines.
    if (combLines > 0)
        mix (0xC0B0C0B0u ^ static_cast<uint32_t> (combLines));

    return hash != 0 ? hash : 1;  // 0 is left to mean "no layout"
}

constexpr int stripColumns = 16;   // buildPlan works through the canvas in strips this wide

/** Never below the drawing (see buildPlan), for the lows of one strip of the canvas: columns c0
    to c0 + width - 1.

    Line l of the `lines` lows has its own row at ownRow[l], and its span runs from row
    spanFirst[l] to spanLast[l] (rows counted from the bottom of the canvas, ascending with l).
    Writes each line's value in the strip's column i to values[l * stripColumns + i].

    The rows between two neighbouring lines are looked at in two halves, the lower line's and the
    upper line's (where their spans meet). Each half is its line's, as everywhere on the canvas,
    with one exception: where one of the two lines' own rows is drawn and the other's is not, and
    every row of the drawn line's half is drawn too, the drawing reaches on into the other half
    from the drawn side: an edge of a shape. Then the drawn line plays both halves, and the other
    one, which the drawing does not reach, stays silent.

    A stroke that touches neither line's row is played by the nearer line. One that lies in both
    halves (it straddles the place where the spans meet) is played by the line that has more of
    its rows, and by that line alone: played half by each, as the spans would have it, the two
    lines beat against each other, five times a second in the comb (a stroke that looks like a
    row of beads and sounds like a tremolo; at 60 Hz the comb's lines are 15 canvas rows apart, so
    four in ten of the thin strokes drawn there straddle).

    `comb`: the lines are the standing comb's, where two or three lines joined by drawn rows, and
    joined to no further line, are a thin stroke (see combThinEdge). */
void coverLows (const Canvas& canvas, int c0, int width, int lines, const int* ownRow, const int* spanFirst, const int* spanLast,
                bool comb, float* values, std::vector<uint8_t>& joined) noexcept
{
    constexpr auto strip = stripColumns;

    const auto rowOf = [&canvas, c0] (int rowFromBottom)
    {
        return canvas.values.data() + static_cast<size_t> (Canvas::height - 1 - rowFromBottom) * Canvas::width + static_cast<size_t> (c0);
    };

    // The loudest and the quietest value of rows `from` to `to`, column by column, and how many
    // of them are drawn (no rows: silence, and "every row drawn").
    const auto over = [&rowOf, width] (int from, int to, float* loudest, float* quietest, float* drawn)
    {
        std::fill (loudest, loudest + strip, 0.0f);
        std::fill (quietest, quietest + strip, 1.0f);
        std::fill (drawn, drawn + strip, 0.0f);

        for (int row = from; row <= to; ++row)
        {
            const auto* v = rowOf (row);

            for (int i = 0; i < width; ++i)
            {
                loudest[i] = std::max (loudest[i], v[i]);
                quietest[i] = std::min (quietest[i], v[i]);
                drawn[i] += v[i] > silenceThreshold ? 1.0f : 0.0f;
            }
        }
    };

    float own[strip] {}, ownAbove[strip] {}, lowLoudest[strip], lowQuietest[strip], highLoudest[strip], highQuietest[strip];
    float lowRows[strip], highRows[strip];
    const auto at = [values] (int line) { return values + static_cast<size_t> (line) * strip; };

    // The lowest line also plays what lies below its own row (there is no line down there).
    over (spanFirst[0], ownRow[0], at (0), lowQuietest, lowRows);
    std::copy (rowOf (ownRow[0]), rowOf (ownRow[0]) + width, own);

    for (int l = 0; l + 1 < lines; ++l)
    {
        // (two lines may share a row where rows are taller than the lines are apart: then there
        // is nothing between them)
        const auto meet = std::min (std::max (spanLast[l], ownRow[l]), ownRow[l + 1] - 1);
        over (ownRow[l] + 1, meet, lowLoudest, lowQuietest, lowRows);
        over (meet + 1, ownRow[l + 1] - 1, highLoudest, highQuietest, highRows);
        std::copy (rowOf (ownRow[l + 1]), rowOf (ownRow[l + 1]) + width, ownAbove);

        auto* below = at (l);
        auto* above = at (l + 1);
        auto* join = joined.data() + static_cast<size_t> (l) * strip;

        for (int i = 0; i < width; ++i)
        {
            const auto belowDrawn = own[i] > silenceThreshold, aboveDrawn = ownAbove[i] > silenceThreshold;
            const auto lowDrawn = lowQuietest[i] > silenceThreshold, highDrawn = highQuietest[i] > silenceThreshold;
            const auto both = std::max (lowLoudest[i], highLoudest[i]);

            // (all the cases are worked out and one chosen, so that noisy drawings cost no more)
            const auto between = ! belowDrawn && ! aboveDrawn && lowRows[i] > 0.0f && highRows[i] > 0.0f;
            const auto allBelow = (belowDrawn && ! aboveDrawn && lowDrawn) || (between && lowRows[i] >= highRows[i]);
            const auto allAbove = (aboveDrawn && ! belowDrawn && highDrawn) || (between && lowRows[i] < highRows[i]);
            below[i] = std::max (below[i], allBelow ? both : (allAbove ? 0.0f : lowLoudest[i]));
            above[i] = std::max (ownAbove[i], allAbove ? both : (allBelow ? 0.0f : highLoudest[i]));
            join[i] = belowDrawn && aboveDrawn && lowDrawn && highDrawn ? 1 : 0;
        }

        std::copy (ownAbove, ownAbove + strip, own);
    }

    // ... and the highest of the lows what lies above its own row, up to the end of its span.
    over (ownRow[lines - 1] + 1, spanLast[lines - 1], lowLoudest, lowQuietest, lowRows);

    for (int i = 0; i < width; ++i)
        at (lines - 1)[i] = std::max (at (lines - 1)[i], lowLoudest[i]);

    if (! comb)
        return;

    // Thin strokes: two or three joined lines on their own.
    for (int i = 0; i < width; ++i)
    {
        for (int first = 0; first + 1 < lines;)
        {
            auto last = first;

            while (last + 1 < lines && joined[static_cast<size_t> (last) * strip + static_cast<size_t> (i)] != 0)
                ++last;

            if (last == first + 1)
            {
                // Two lines alone beat as one pulse: the louder plays for both.
                const auto loudest = std::max (at (first)[i], at (last)[i]);
                const auto keep = at (last)[i] > at (first)[i] ? last : first;
                at (first)[i] = at (last)[i] = 0.0f;
                at (keep)[i] = loudest;
            }
            else if (last == first + 2)
            {
                at (first)[i] *= combThinEdge;
                at (last)[i] *= combThinEdge;
            }

            first = last + 1;
        }
    }
}

/** A line's loudness over the columns, smoothed with a raised cosine `width` columns long (see
    fadeCycles), in place. `wraps`: the last column leads into the first; else there is silence
    either side, and the line also reaches silence at the two ends. */
void fadeInTime (float* line, int columns, double width, bool wraps, std::vector<float>& kernel, std::vector<float>& scratch)
{
    const auto reach = static_cast<int> (std::ceil (width * 0.5 - 1.0e-9)) - 1;   // offsets with a weight above 0

    if (reach < 1 || columns < 2)
        return;

    const auto first = static_cast<int> (std::find_if (line, line + columns, [] (float a) { return a > 0.0f; }) - line);

    if (first == columns)
        return;

    auto last = columns - 1;

    while (! (line[last] > 0.0f))
        --last;

    kernel.resize (static_cast<size_t> (reach) + 1);
    auto sum = 0.0;

    for (int j = 0; j <= reach; ++j)
    {
        kernel[static_cast<size_t> (j)] = static_cast<float> (1.0 + std::cos (6.283185307179586 * j / width));
        sum += (j == 0 ? 1.0 : 2.0) * static_cast<double> (kernel[static_cast<size_t> (j)]);
    }

    for (auto& weight : kernel)
        weight = static_cast<float> (static_cast<double> (weight) / sum);

    // Three copies of the line side by side (silence either side without Repeat), so the sums
    // below need no wrapping; only the columns within reach of the drawn ones are worked out.
    const auto c = static_cast<size_t> (columns);
    scratch.assign (3 * c, 0.0f);
    std::copy (line, line + columns, scratch.begin() + static_cast<std::ptrdiff_t> (c));

    if (wraps)
    {
        std::copy (line, line + columns, scratch.begin());
        std::copy (line, line + columns, scratch.begin() + static_cast<std::ptrdiff_t> (2 * c));
    }

    const auto from = first - reach, to = last + reach;
    const auto whole = to - from + 1 >= columns;
    const auto* centre = scratch.data() + c;

    for (int i = whole ? 0 : from; i <= (whole ? columns - 1 : to); ++i)
    {
        const auto column = ((i % columns) + columns) % columns;

        if (! wraps && column != i)
            continue;

        const auto* at = centre + column;
        auto value = kernel[0] * at[0];

        for (int j = 1; j <= reach; ++j)
            value += kernel[static_cast<size_t> (j)] * (at[-j] + at[j]);

        line[column] = value;
    }

    if (wraps)
        return;

    // A single pass: silent at its first and last column, full after `width` columns.
    const auto ramp = std::min (columns / 2, static_cast<int> (std::ceil (width)));

    for (int i = 0; i < ramp; ++i)
    {
        const auto gain = static_cast<float> (0.5 - 0.5 * std::cos (3.141592653589793 * i / width));
        line[i] *= gain;
        line[columns - 1 - i] *= gain;
    }
}

} // namespace

std::unique_ptr<ArtPlan> buildPlan (const Canvas& canvas, const PlanSettings& settings)
{
    auto plan = std::make_unique<ArtPlan>();

    const auto scale = settings.scale.sanitised();
    const auto density = juce::jlimit (minDensity, maxDensity, settings.density);
    const auto contrastDb = juce::jlimit (1.0f, 120.0f, finiteOr (settings.contrastDb, 40.0f));
    const auto slope = juce::jlimit (-24.0f, 24.0f, finiteOr (settings.slopeDbPerOctave, 0.0f));
    const auto minSpacing = std::max (0.0, static_cast<double> (finiteOr (settings.minLineSpacingHz, 5.0f)));
    const auto maxHz = static_cast<double> (finiteOr (settings.maxFrequencyHz, 20000.0f));

    // ---- Lines, bottom up --------------------------------------------------------------------
    std::vector<LineLayout> layout;
    layout.reserve (static_cast<size_t> (density));

    for (int cell = 0; cell < density; ++cell)
    {
        const auto norm = (cell + 0.5) / density;
        const auto hz = scale.toHz (norm);

        if (! (hz >= lowestLineHz && hz <= maxHz))
            continue;

        // Lines closer than the minimum spacing would beat against each other: merge them.
        if (! layout.empty() && hz - layout.back().hz < minSpacing)
        {
            layout.back().lastCell = cell;
            continue;
        }

        layout.push_back ({ hz, norm, cell, cell });
    }

    // Where lines crowd together (the merged lows on a log scale), they would sit exactly
    // minSpacing apart, so every pair beats at the same rate and in step: the band pulses in
    // regular blobs on an analyser. A small fixed offset per line (a quarter of the spacing at
    // most, either way) breaks that common period, leaving an even, noise-like texture.
    std::vector<uint8_t> crowded (layout.size(), 0);

    for (size_t l = 1; l < layout.size(); ++l)
    {
        const auto gap = layout[l].hz - layout[l - 1].hz;

        if (minSpacing > 0.0 && gap < 2.0 * minSpacing)
        {
            auto bits = static_cast<uint32_t> (l) * 0x9E3779B9u;
            bits ^= bits >> 16;
            bits *= 0x85EBCA6Bu;
            bits ^= bits >> 13;
            const auto unit = static_cast<double> (bits & 0xFFFFu) / 65535.0;  // 0..1
            layout[l].hz += (unit - 0.5) * 0.5 * minSpacing;
            layout[l].norm = scale.toNorm (layout[l].hz);
            crowded[l] = 1;
        }
    }

    // ---- Spans: the rows each line plays -----------------------------------------------------
    // A line's span runs halfway (in canvas height) to the final heights of its neighbours, so
    // every row plays on its nearest line. Spanning its merged cells instead would put a merged
    // line at the bottom of its span (it keeps its lowest cell's frequency) and the jitter moves
    // lines within theirs: low art would play up to a whole line step too low. Rows are counted
    // from the bottom here; canvas row = height - 1 - rowFromBottom.
    const auto rowAtNorm = [] (double norm, bool top)
    {
        return top ? std::clamp (static_cast<int> (std::ceil (norm * Canvas::height - 1.0e-6)) - 1, 0, Canvas::height - 1)
                   : std::clamp (static_cast<int> (std::floor (norm * Canvas::height + 1.0e-6)), 0, Canvas::height - 1);
    };

    std::vector<double> spanBottom (layout.size()), spanTop (layout.size());

    for (size_t l = 0; l < layout.size(); ++l)
    {
        spanBottom[l] = l == 0 ? static_cast<double> (layout[l].firstCell) / density
                               : 0.5 * (layout[l - 1].norm + layout[l].norm);
        spanTop[l] = l + 1 == layout.size() ? static_cast<double> (layout[l].lastCell + 1) / density
                                            : 0.5 * (layout[l].norm + layout[l + 1].norm);
    }

    // ---- The lows -----------------------------------------------------------------------------
    // The crowded lows: the lines from the bottom up to the last one that stands closer than
    // twice the minimum spacing to the one below (a line with room on both sides is the density's
    // own scan line), and below the comb's limit. At least two lines, else there are no lows to
    // speak of (a linear scale has none).
    size_t lows = 0;

    if (minSpacing > 0.0)
    {
        while (lows + 1 < layout.size() && crowded[lows + 1] != 0
               && layout[lows + 1].hz < static_cast<double> (combTopLimitHz))
            ++lows;

        lows = lows >= 1 ? lows + 1 : 0;
    }

    struct FinalLine
    {
        double hz, norm;
        int firstRow, lastRow;
        float gain;
    };

    std::vector<FinalLine> finalLines;
    finalLines.reserve (layout.size() + 512);

    // ---- Smooth lows: the standing comb ------------------------------------------------------
    // It takes the lows' place: steady lines on a fixed grid (see ArtPlan.h), over exactly the
    // band of the canvas the replaced lines played, so the lines above keep their spans.
    size_t combReplaces = 0;

    if (settings.lowEnd == LowEnd::smooth && lows > 0)
    {
        const auto bottomHz = std::max (scale.toHz (spanBottom.front()), lowestLineHz);
        const auto topHz = scale.toHz (spanTop[lows - 1]);
        const auto fineTopHz = std::min (topHz, static_cast<double> (combFineTopHz));

        struct CombLine
        {
            double hz, spacing;
        };

        std::vector<CombLine> comb;

        // No plan has more than maxDensity lines (the engine plays no more): the grid is
        // coarsened where it would pass that (unusual scales and ranges only).
        for (double coarsen = 1.0; coarsen <= 64.0; coarsen *= 2.0)
        {
            comb.clear();

            const auto add = [&comb] (double spacing, double from, double to)
            {
                for (auto k = std::ceil (from / spacing - 1.0e-9); k * spacing < to; k += 1.0)
                    comb.push_back ({ k * spacing, spacing });
            };

            add (coarsen * static_cast<double> (combFineSpacingHz), bottomHz, fineTopHz);
            add (coarsen * static_cast<double> (combCoarseSpacingHz), std::max (bottomHz, fineTopHz), topHz);

            if (comb.size() + layout.size() - lows <= static_cast<size_t> (maxDensity))
                break;
        }

        if (comb.size() >= 2 && comb.size() + layout.size() - lows <= static_cast<size_t> (maxDensity))
        {
            combReplaces = lows;
            plan->combLines = static_cast<int> (comb.size());
            plan->combPhases.resize (comb.size());

            for (size_t k = 0; k < comb.size(); ++k)
            {
                // Each line plays the rows nearest to it, between the ends of the replaced band.
                const auto norm = scale.toNorm (comb[k].hz);
                const auto low = k == 0 ? spanBottom.front() : 0.5 * (scale.toNorm (comb[k - 1].hz) + norm);
                const auto high = k + 1 == comb.size() ? spanTop[lows - 1] : 0.5 * (norm + scale.toNorm (comb[k + 1].hz));
                const auto first = rowAtNorm (low, false);

                // The sweep (see combPhaseLines): the phase falls with the square of the line's
                // number on its grid.
                const auto number = std::round (comb[k].hz / comb[k].spacing);
                const auto turns = -0.5 * number * number / static_cast<double> (combPhaseLines);
                const auto phase = 6.283185307179586 * (turns - std::floor (turns));
                const auto gain = static_cast<double> (combGain) * std::sqrt (comb[k].spacing / static_cast<double> (combGainSpacingHz));

                plan->combPhases[k] = static_cast<float> (phase);
                finalLines.push_back ({ comb[k].hz, norm, first, std::max (first, rowAtNorm (high, true)), static_cast<float> (gain) });
            }
        }
    }

    // The lines (all of them, or those above the comb).
    for (size_t l = combReplaces; l < layout.size(); ++l)
    {
        const auto first = rowAtNorm (spanBottom[l], false);
        finalLines.push_back ({ layout[l].hz, layout[l].norm, first, std::max (first, rowAtNorm (spanTop[l], true)), 1.0f });
    }

    // The lows as they are played: the comb's lines, or the crowded lines themselves.
    const auto lowLines = static_cast<int> (plan->combLines > 0 ? static_cast<size_t> (plan->combLines) : lows);

    const auto numLines = static_cast<int> (finalLines.size());
    const auto numColumns = Canvas::width;
    const auto lines = static_cast<size_t> (numLines);

    plan->numLines = numLines;
    plan->numColumns = numColumns;
    plan->frequencies.resize (lines);
    plan->lineNorm.resize (lines);

    for (size_t l = 0; l < lines; ++l)
    {
        plan->frequencies[l] = static_cast<float> (finalLines[l].hz);
        plan->lineNorm[l] = static_cast<float> (finalLines[l].norm);
    }

    plan->layoutHash = hashFrequencies (plan->frequencies, plan->combLines);
    plan->amplitudes.assign (static_cast<size_t> (numColumns) * lines, 0.0f);
    plan->activeStart.assign (static_cast<size_t> (numColumns) + 1, 0);
    plan->silent = true;

    if (numLines == 0 || canvas.values.size() != static_cast<size_t> (Canvas::width * Canvas::height))
        return plan;

    // ---- Values: the maximum over each line's span of rows ---------------------------------
    const LoudnessTable loudness (contrastDb);

    std::vector<int> firstRow (lines), lastRow (lines), ownRow (static_cast<size_t> (lowLines));
    std::vector<float> slopeGain (lines);

    for (size_t l = 0; l < lines; ++l)
    {
        firstRow[l] = finalLines[l].firstRow;
        lastRow[l] = finalLines[l].lastRow;

        if (l < ownRow.size())
            ownRow[l] = std::clamp (static_cast<int> (std::floor (finalLines[l].norm * Canvas::height)), firstRow[l], lastRow[l]);

        // Analysers with a slope show highs louder: tilt the other way around 1 kHz.
        slopeGain[l] = static_cast<float> (std::pow (10.0, -slope * std::log2 (finalLines[l].hz / 1000.0) / 20.0))
                     * finalLines[l].gain;
    }

    // ---- Fades in time (see fadeCycles): which lines, and how long ----------------------------
    // Only the lines whose fade is longer than two columns change at all (every line at the
    // default length; none where a column lasts 20 ms or more, and the engine's own glide from
    // column to column is gentle enough); they are kept line by line as well, as the fades run
    // along them.
    const auto passSeconds = std::isfinite (settings.passSeconds) ? settings.passSeconds : 0.0;
    std::vector<double> fadeColumns;    // per faded line, the length of its fade in columns
    std::vector<size_t> fadedLines;

    if (passSeconds > 0.0)
    {
        const auto columnSeconds = passSeconds / numColumns;
        const auto longest = std::min (static_cast<double> (longestFadeSeconds), passSeconds * static_cast<double> (longestFadeOfPass));
        const auto shortest = std::min (static_cast<double> (shortestFadeSeconds), static_cast<double> (shortestFadeColumns) * columnSeconds);

        for (size_t l = 0; l < lines; ++l)
        {
            const auto own = static_cast<double> (fadeCycles) / std::max (1.0, finalLines[l].hz);
            auto seconds = std::max (shortest, own);

            // Long columns (see coarsestFadeColumns).
            if (own > columnSeconds)
                seconds = std::max (seconds, static_cast<double> (coarsestFadeColumns) * columnSeconds);

            seconds = std::min (seconds, longest);

            if (seconds > 2.0 * columnSeconds)
            {
                fadedLines.push_back (l);
                fadeColumns.push_back (seconds / columnSeconds);
            }
        }
    }

    std::vector<float> byLine (fadedLines.size() * static_cast<size_t> (numColumns));

    // The canvas is row-major and the plan column-major, so the work goes in strips of a few
    // columns: the strip's values for every line stay in cache while they are turned around.
    constexpr auto strip = stripColumns;
    std::vector<float> stripValues (lines * strip);
    std::vector<double> columnPower (static_cast<size_t> (numColumns), 0.0);
    auto& amplitudes = plan->amplitudes;

    std::vector<uint8_t> joined (static_cast<size_t> (lowLines) * strip);   // coverLows' scratch

    for (int c0 = 0; c0 < numColumns; c0 += strip)
    {
        const auto width = std::min (strip, numColumns - c0);
        std::fill (stripValues.begin(), stripValues.end(), 0.0f);

        if (lowLines > 0)
            coverLows (canvas, c0, width, lowLines, ownRow.data(), firstRow.data(), lastRow.data(), plan->combLines > 0,
                       stripValues.data(), joined);

        for (auto l = static_cast<size_t> (lowLines); l < lines; ++l)
        {
            auto* values = stripValues.data() + l * strip;

            for (int rowFromBottom = firstRow[l]; rowFromBottom <= lastRow[l]; ++rowFromBottom)
            {
                const auto* row = canvas.values.data() + static_cast<size_t> (Canvas::height - 1 - rowFromBottom) * Canvas::width
                                + static_cast<size_t> (c0);

                for (int i = 0; i < width; ++i)
                    values[i] = std::max (values[i], row[i]);
            }
        }

        for (int i = 0; i < width; ++i)
        {
            auto* column = amplitudes.data() + static_cast<size_t> (c0 + i) * lines;

            for (size_t l = 0; l < lines; ++l)
                column[l] = loudness (stripValues[l * strip + static_cast<size_t> (i)]) * slopeGain[l];

            auto power = 0.0;

            for (size_t l = 0; l < lines; ++l)
                power += static_cast<double> (column[l]) * static_cast<double> (column[l]);

            columnPower[static_cast<size_t> (c0 + i)] = power;
        }

        for (size_t f = 0; f < fadedLines.size(); ++f)
        {
            const auto l = fadedLines[f];
            const auto* values = stripValues.data() + l * strip;
            auto* out = byLine.data() + f * static_cast<size_t> (numColumns) + static_cast<size_t> (c0);

            for (int i = 0; i < width; ++i)
                out[i] = loudness (values[i]) * slopeGain[l];
        }
    }

    if (! fadedLines.empty())
    {
        std::vector<float> kernel, scratch;

        // (each column's power follows: less the line as drawn, plus the line as faded)
        const auto addPower = [&columnPower, numColumns] (const float* line, double sign)
        {
            for (int c = 0; c < numColumns; ++c)
                columnPower[static_cast<size_t> (c)] += sign * static_cast<double> (line[c]) * static_cast<double> (line[c]);
        };

        for (size_t f = 0; f < fadedLines.size(); ++f)
        {
            auto* line = byLine.data() + f * static_cast<size_t> (numColumns);
            addPower (line, -1.0);
            fadeInTime (line, numColumns, fadeColumns[f], settings.repeats, kernel, scratch);
            addPower (line, 1.0);
        }

        // Back into the plan, a strip of columns at a time (the plan's part stays in cache).
        for (int c0 = 0; c0 < numColumns; c0 += strip)
        {
            const auto cEnd = std::min (numColumns, c0 + strip);

            for (size_t f = 0; f < fadedLines.size(); ++f)
            {
                const auto* in = byLine.data() + f * static_cast<size_t> (numColumns);

                for (int c = c0; c < cEnd; ++c)
                    amplitudes[static_cast<size_t> (c) * lines + fadedLines[f]] = in[c];
            }
        }
    }

    const auto loudestPower = *std::max_element (columnPower.begin(), columnPower.end());

    // ---- Normalisation: the loudest column sits at planTargetRms -----------------------------
    if (! (loudestPower > 0.0))
        return plan;  // nothing drawn: silent, lines still laid out

    // RMS of a sum of sines = sqrt (sum (a^2) / 2).
    const auto gain = static_cast<float> (static_cast<double> (planTargetRms) / std::sqrt (loudestPower * 0.5));

    for (auto& a : amplitudes)
        a *= gain;

    plan->silent = false;

    // ---- Active lines per column (both ends of the interpolation c -> c + 1) -----------------
    // Written through a pointer into room for every line in every column, then trimmed: a busy
    // canvas has nearly every line active everywhere, and push_back per entry is slow.
    auto& active = plan->activeLines;
    active.resize (static_cast<size_t> (numColumns) * lines);
    size_t count = 0;

    for (int c = 0; c < numColumns; ++c)
    {
        const auto* here = amplitudes.data() + static_cast<size_t> (c) * lines;
        const auto* next = amplitudes.data() + static_cast<size_t> ((c + 1) % numColumns) * lines;
        auto* out = active.data();

        plan->activeStart[static_cast<size_t> (c)] = static_cast<uint32_t> (count);

        for (size_t l = 0; l < lines; ++l)
        {
            out[count] = static_cast<uint16_t> (l);
            count += (here[l] > 0.0f || next[l] > 0.0f) ? 1 : 0;
        }
    }

    plan->activeStart[static_cast<size_t> (numColumns)] = static_cast<uint32_t> (count);
    active.resize (count);
    active.shrink_to_fit();
    return plan;
}

} // namespace artfreq
