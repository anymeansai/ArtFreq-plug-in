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

/** Never below the drawing (see buildPlan), for one column of the lows.

    `rows` holds the canvas over the lows' rows, bottom up (rows[0] is row `firstRow` from the
    bottom); line l of the `lines` lows has its own row at ownRow[l] and its span ends at row
    spanLast[l] (counted from the bottom of the canvas, ascending with l). Writes each line's
    value to values[l * stride]. */
void coverLows (const float* rows, int numRows, int firstRow, int lines, const int* ownRow, const int* spanLast,
                float* values, size_t stride) noexcept
{
    for (int l = 0; l < lines; ++l)
        values[static_cast<size_t> (l) * stride] = 0.0f;

    int next = 0;   // the first line whose own row is not below the rows looked at so far

    for (int r = 0; r < numRows;)
    {
        if (! (rows[r] > silenceThreshold))
        {
            ++r;
            continue;
        }

        const auto start = r;

        while (r < numRows && rows[r] > silenceThreshold)
            ++r;

        const auto runFirst = firstRow + start, runLast = firstRow + r - 1;

        while (next < lines && ownRow[next] < runFirst)
            ++next;

        const auto loudestIn = [rows, firstRow] (int from, int to)
        {
            auto loudest = 0.0f;

            for (int row = from; row <= to; ++row)
                loudest = std::max (loudest, rows[row - firstRow]);

            return loudest;
        };

        if (next < lines && ownRow[next] <= runLast)
        {
            // The lines inside the run: each plays the part of it in its span, the lowest of them
            // also what is below its span and the highest what is above (a bright stroke with a
            // faint fringe that reaches a line is played by that line, not lost).
            for (auto from = runFirst; next < lines && ownRow[next] <= runLast; ++next)
            {
                const auto isLast = next + 1 == lines || ownRow[next + 1] > runLast;
                const auto to = isLast ? runLast : std::min (runLast, spanLast[next]);
                auto& value = values[static_cast<size_t> (next) * stride];
                value = std::max (value, loudestIn (std::min (from, to), to));
                from = std::min (runLast, std::max (from, to));
            }

            continue;
        }

        // A stroke between two lines: the one nearest its middle (twice the middle row, to stay
        // in whole numbers).
        const auto middle = runFirst + runLast;
        auto nearest = next < lines ? next : lines - 1;

        if (next > 0 && next < lines && middle - 2 * ownRow[next - 1] <= 2 * ownRow[next] - middle)
            nearest = next - 1;

        if (nearest >= 0)
        {
            auto& value = values[static_cast<size_t> (nearest) * stride];
            value = std::max (value, loudestIn (runFirst, runLast));
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

    // The canvas is row-major and the plan column-major, and the fades run along each line: the
    // work goes in strips of a few columns into a copy that is line-major (the strip's values for
    // every line stay in cache while they are turned around), turned into the plan at the end.
    constexpr int strip = 16;
    std::vector<float> stripValues (lines * strip);
    std::vector<float> byLine (lines * static_cast<size_t> (numColumns));

    // The lows' rows of the strip, column by column and bottom up, for coverLows.
    const auto lowFirstRow = lowLines > 0 ? firstRow[0] : 0;
    const auto lowRows = lowLines > 0 ? lastRow[static_cast<size_t> (lowLines) - 1] - lowFirstRow + 1 : 0;
    std::vector<float> lowColumns (static_cast<size_t> (lowRows) * strip);

    for (int c0 = 0; c0 < numColumns; c0 += strip)
    {
        const auto width = std::min (strip, numColumns - c0);
        std::fill (stripValues.begin(), stripValues.end(), 0.0f);

        for (int r = 0; r < lowRows; ++r)
        {
            const auto* row = canvas.values.data() + static_cast<size_t> (Canvas::height - 1 - (lowFirstRow + r)) * Canvas::width
                            + static_cast<size_t> (c0);

            for (int i = 0; i < width; ++i)
                lowColumns[static_cast<size_t> (i) * static_cast<size_t> (lowRows) + static_cast<size_t> (r)] = row[i];
        }

        for (int i = 0; i < width && lowLines > 0; ++i)
            coverLows (lowColumns.data() + static_cast<size_t> (i) * static_cast<size_t> (lowRows), lowRows, lowFirstRow, lowLines,
                       ownRow.data(), lastRow.data(), stripValues.data() + i, static_cast<size_t> (strip));

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

        for (size_t l = 0; l < lines; ++l)
        {
            const auto* values = stripValues.data() + l * strip;
            auto* out = byLine.data() + l * static_cast<size_t> (numColumns) + static_cast<size_t> (c0);

            for (int i = 0; i < width; ++i)
                out[i] = loudness (values[i]) * slopeGain[l];
        }
    }

    // ---- Fades in time (see fadeCycles) ---------------------------------------------------------
    const auto passSeconds = std::isfinite (settings.passSeconds) ? settings.passSeconds : 0.0;

    if (passSeconds > 0.0)
    {
        const auto columnSeconds = passSeconds / numColumns;
        const auto longest = std::min (static_cast<double> (longestFadeSeconds), passSeconds * static_cast<double> (longestFadeOfPass));
        std::vector<float> kernel, scratch;

        for (size_t l = 0; l < lines; ++l)
        {
            const auto seconds = std::min (static_cast<double> (fadeCycles) / std::max (1.0, finalLines[l].hz), longest);
            fadeInTime (byLine.data() + l * static_cast<size_t> (numColumns), numColumns, seconds / columnSeconds,
                        settings.repeats, kernel, scratch);
        }
    }

    // ---- Into the plan, column by column; the loudest column ---------------------------------
    auto& amplitudes = plan->amplitudes;
    std::vector<double> columnPower (static_cast<size_t> (numColumns), 0.0);
    constexpr int tile = 64;

    for (int c0 = 0; c0 < numColumns; c0 += tile)
    {
        const auto cEnd = std::min (numColumns, c0 + tile);

        for (size_t l0 = 0; l0 < lines; l0 += tile)
        {
            const auto lEnd = std::min (lines, l0 + tile);

            for (auto l = l0; l < lEnd; ++l)
            {
                const auto* in = byLine.data() + l * static_cast<size_t> (numColumns);

                for (int c = c0; c < cEnd; ++c)
                {
                    const auto a = in[c];
                    amplitudes[static_cast<size_t> (c) * lines + l] = a;
                    columnPower[static_cast<size_t> (c)] += static_cast<double> (a) * static_cast<double> (a);
                }
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
