#include "ArtPlan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>

namespace freqart
{

namespace
{

constexpr double lowestLineHz = 10.0;   // below this nothing is audible or visible: dropped
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

    // A comb plays its grid lines in a different way: never the same layout as plain lines.
    if (combLines > 0)
        mix (0xC0B0C0B0u ^ static_cast<uint32_t> (combLines));

    return hash != 0 ? hash : 1;  // 0 is left to mean "no layout"
}

/** Softens the comb's grid (one column) at the edges of each run of drawn grid lines, see
    combEdgeTaper. Only edges inside the grid count: below its first line and above its last the
    glide's own fades take over. */
void softenCombEdges (float* grid, int cells) noexcept
{
    constexpr auto taperCells = static_cast<int> (std::size (combEdgeTaper));

    for (int k = 0; k < cells;)
    {
        if (! (grid[k] > 0.0f))
        {
            ++k;
            continue;
        }

        const auto start = k;

        while (k < cells && grid[k] > 0.0f)
            ++k;

        if (k - start < combEdgeTaperMinCells)
            continue;

        for (int d = 0; d < taperCells; ++d)
        {
            if (start > 0)
                grid[start + d] *= combEdgeTaper[d];

            if (k < cells)
                grid[k - 1 - d] *= combEdgeTaper[d];
        }
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

    // ---- Smooth lows: the rising comb ----------------------------------------------------------
    // It takes over the crowded lows: the lines from the bottom up to the last one that stands
    // closer than twice the minimum spacing to the one below (a line with room on both sides is
    // the density's own scan line, and stays), and below the comb's limit. At least two lines,
    // else there is nothing to smooth.
    size_t combReplaces = 0;

    if (settings.lowEnd == LowEnd::smooth && minSpacing > 0.0)
    {
        while (combReplaces + 1 < layout.size() && crowded[combReplaces + 1] != 0
               && layout[combReplaces + 1].hz < static_cast<double> (combTopLimitHz))
            ++combReplaces;

        combReplaces = combReplaces >= 1 ? combReplaces + 1 : 0;
    }

    struct FinalLine
    {
        double hz, norm;
        int firstRow, lastRow;
        float gain;
        double powerWeight;  // how much of its power sounds: less than all for the comb's grid
    };

    std::vector<FinalLine> finalLines;
    finalLines.reserve (layout.size() + 512);

    if (combReplaces > 0)
    {
        // The grid covers the replaced lines' spans exactly, so the lines above keep theirs.
        const auto bottomHz = scale.toHz (spanBottom.front());
        const auto topHz = scale.toHz (spanTop[combReplaces - 1]);
        // No plan has more than maxDensity lines (the engine plays no more): with the lines kept
        // above it, a fine grid could pass that on unusual scales and ranges; it is coarsened then.
        const auto room = maxDensity - static_cast<int> (layout.size() - combReplaces);
        const auto cells = std::clamp (static_cast<int> (std::lround ((topHz - bottomHz) / static_cast<double> (combGridSpacingHz))), 2, std::max (2, room));
        const auto cellHz = (topHz - bottomHz) / cells;
        const auto edgeHz = static_cast<double> (combEdgeFadeHz);
        const auto glideHz = (cells - 1) * cellHz + 2.0 * edgeHz;
        const auto sines = std::clamp (static_cast<int> (std::lround (glideHz / static_cast<double> (combSpacingHz))), 2, combMaxSines);

        plan->combLines = cells;
        plan->combGridLowHz = static_cast<float> (bottomHz + 0.5 * cellHz);
        plan->combGridHz = static_cast<float> (cellHz);
        plan->combEdgeHz = static_cast<float> (edgeHz);
        plan->combSines = sines;
        plan->combSlope = combSlopeHzPerSecond;

        // On average a sine spends cellHz / glideHz of its time at each grid line.
        const auto weight = sines * cellHz / glideHz;

        for (int k = 0; k < cells; ++k)
        {
            const auto hz = static_cast<double> (plan->combGridLowHz) + k * static_cast<double> (plan->combGridHz);
            const auto low = k == 0 ? spanBottom.front() : scale.toNorm (bottomHz + k * cellHz);
            const auto high = k + 1 == cells ? spanTop[combReplaces - 1] : scale.toNorm (bottomHz + (k + 1) * cellHz);
            const auto first = rowAtNorm (low, false);
            finalLines.push_back ({ hz, scale.toNorm (hz), first, std::max (first, rowAtNorm (high, true)), combGain, weight });
        }
    }

    for (size_t l = combReplaces; l < layout.size(); ++l)
    {
        const auto first = rowAtNorm (spanBottom[l], false);
        finalLines.push_back ({ layout[l].hz, layout[l].norm, first, std::max (first, rowAtNorm (spanTop[l], true)), 1.0f, 1.0 });
    }

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
    plan->combActive.assign (plan->combLines > 0 ? static_cast<size_t> (numColumns) : 0, 0);
    plan->silent = true;

    if (numLines == 0 || canvas.values.size() != static_cast<size_t> (Canvas::width * Canvas::height))
        return plan;

    // ---- Values: the maximum over each line's span of rows ---------------------------------
    const LoudnessTable loudness (contrastDb);
    auto& amplitudes = plan->amplitudes;

    std::vector<int> firstRow (lines), lastRow (lines);
    std::vector<float> slopeGain (lines);
    std::vector<double> powerWeight (lines);

    for (size_t l = 0; l < lines; ++l)
    {
        powerWeight[l] = finalLines[l].powerWeight;
        firstRow[l] = finalLines[l].firstRow;
        lastRow[l] = finalLines[l].lastRow;

        // Analysers with a slope show highs louder: tilt the other way around 1 kHz.
        slopeGain[l] = static_cast<float> (std::pow (10.0, -slope * std::log2 (finalLines[l].hz / 1000.0) / 20.0))
                     * finalLines[l].gain;
    }

    // The canvas is row-major and the plan column-major, so the work goes in strips of a few
    // columns: the strip's values for every line stay in cache while they are turned around.
    constexpr int strip = 16;
    std::vector<float> stripValues (lines * strip);
    double loudestPower = 0.0;

    for (int c0 = 0; c0 < numColumns; c0 += strip)
    {
        const auto width = std::min (strip, numColumns - c0);
        std::fill (stripValues.begin(), stripValues.end(), 0.0f);

        for (size_t l = 0; l < lines; ++l)
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

            softenCombEdges (column, plan->combLines);

            double power = 0.0;

            for (size_t l = 0; l < lines; ++l)
                power += static_cast<double> (column[l]) * static_cast<double> (column[l]) * powerWeight[l];

            loudestPower = std::max (loudestPower, power);
        }
    }

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
    // The comb's grid lines are played by the comb, never as lines of their own.
    auto& active = plan->activeLines;
    active.resize (static_cast<size_t> (numColumns) * lines);
    size_t count = 0;
    const auto firstLine = static_cast<size_t> (plan->combLines);

    for (int c = 0; c < numColumns; ++c)
    {
        const auto* here = amplitudes.data() + static_cast<size_t> (c) * lines;
        const auto* next = amplitudes.data() + static_cast<size_t> ((c + 1) % numColumns) * lines;
        auto* out = active.data();

        plan->activeStart[static_cast<size_t> (c)] = static_cast<uint32_t> (count);

        for (size_t l = firstLine; l < lines; ++l)
        {
            out[count] = static_cast<uint16_t> (l);
            count += (here[l] > 0.0f || next[l] > 0.0f) ? 1 : 0;
        }

        uint8_t combSounds = 0;

        for (size_t l = 0; l < firstLine; ++l)
            combSounds |= (here[l] > 0.0f || next[l] > 0.0f) ? 1 : 0;

        if (firstLine > 0)
            plan->combActive[static_cast<size_t> (c)] = combSounds;
    }

    plan->activeStart[static_cast<size_t> (numColumns)] = static_cast<uint32_t> (count);
    active.resize (count);
    active.shrink_to_fit();
    return plan;
}

} // namespace freqart
