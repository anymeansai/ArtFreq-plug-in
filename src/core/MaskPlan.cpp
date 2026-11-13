#include "MaskPlan.h"

#include <algorithm>
#include <cmath>

namespace freqart
{

namespace
{
    /** Replaces each of `count` elements (element i is the `width` floats from data + i *
        stride) by the maximum of the elements within `radius` of it, elementwise; elements
        beyond the ends count as 0 (all values are >= 0, so that is the same as stopping at the
        ends). A window of 2r + 1 is built up from shifted maxima of doubling length (windows of
        1, 2, 4, ... then one shift for the rest), so the cost grows with log (radius), not with
        the radius: a time halo can be dozens of columns wide. */
    void dilate (float* data, int count, int width, size_t stride, int radius, std::vector<float>& scratch)
    {
        if (radius <= 0 || count <= 0 || width <= 0)
            return;

        const auto w = static_cast<size_t> (width);
        const auto length = static_cast<size_t> (count) + 2 * static_cast<size_t> (radius);
        scratch.assign (length * w, 0.0f);

        // The elements r from the start, zeros either side: element y of the result is then the
        // maximum of scratch elements y .. y + 2r.
        for (size_t i = 0; i < static_cast<size_t> (count); ++i)
            std::copy (data + i * stride, data + i * stride + w, scratch.data() + (i + static_cast<size_t> (radius)) * w);

        const auto shiftMax = [&] (size_t shift)
        {
            // In place, forwards: element y + shift has not been changed yet when y reads it.
            for (size_t y = 0; y + shift < length; ++y)
            {
                auto* to = scratch.data() + y * w;
                const auto* from = to + shift * w;

                for (size_t k = 0; k < w; ++k)
                    to[k] = std::max (to[k], from[k]);
            }
        };

        const auto window = 2 * static_cast<size_t> (radius) + 1;
        size_t covered = 1;

        while (covered * 2 <= window)
        {
            shiftMax (covered);
            covered *= 2;
        }

        if (covered < window)
            shiftMax (window - covered);

        for (size_t i = 0; i < static_cast<size_t> (count); ++i)
            std::copy (scratch.data() + i * w, scratch.data() + (i + 1) * w, data + i * stride);
    }

    /** Widens every shape by haloRows rows up and down and haloColumns columns either side. */
    void dilatePlan (MaskPlan& plan, int haloRows, int haloColumns)
    {
        std::vector<float> scratch;
        const auto rows = static_cast<size_t> (plan.numRows);

        // Along each column (contiguous, low rows first), then across the columns (whole
        // columns at a time).
        if (haloRows > 0)
            for (int c = 0; c < plan.numColumns; ++c)
                dilate (plan.values.data() + static_cast<size_t> (c) * rows, plan.numRows, 1, 1, haloRows, scratch);

        if (haloColumns > 0)
            dilate (plan.values.data(), plan.numColumns, plan.numRows, rows, haloColumns, scratch);
    }

    /** Works out which columns hold anything, and whether any does. */
    void markUsedColumns (MaskPlan& plan)
    {
        plan.columnUsed.assign (static_cast<size_t> (plan.numColumns), 0);
        plan.empty = true;

        for (int x = 0; x < plan.numColumns; ++x)
        {
            const auto* column = plan.column (x);
            const auto used = std::any_of (column, column + plan.numRows, [] (float v) { return v > 0.0f; });
            plan.columnUsed[static_cast<size_t> (x)] = used ? 1 : 0;
            plan.empty = plan.empty && ! used;
        }
    }
}

int MaskPlan::columnAt (double fraction) const noexcept
{
    if (numColumns <= 0 || ! (fraction > 0.0))  // also NaN
        return 0;

    const auto c = std::floor (fraction * static_cast<double> (numColumns));
    return c >= static_cast<double> (numColumns - 1) ? numColumns - 1 : static_cast<int> (c);
}

double MaskPlan::rowAt (double hz) const noexcept
{
    return scale.toNorm (hz) * static_cast<double> (numRows) - 0.5;
}

float MaskPlan::valueAt (int c, double hz) const noexcept
{
    return valueAtRow (c, rowAt (hz));
}

float MaskPlan::valueAtRow (int c, double x) const noexcept
{
    if (numRows <= 0 || c < 0 || c >= numColumns)
        return 0.0f;

    const auto top = static_cast<double> (numRows) - 0.5;

    if (! (x >= -0.5 && x <= top))  // also NaN
        return 0.0f;

    const auto* cells = column (c);
    const auto clamped = std::clamp (x, 0.0, static_cast<double> (numRows - 1));
    const auto below = static_cast<int> (clamped);
    const auto above = std::min (below + 1, numRows - 1);
    const auto t = static_cast<float> (clamped - below);
    return cells[below] + (cells[above] - cells[below]) * t;
}

float MaskPlan::averageOver (int c, double lowHz, double highHz) const noexcept
{
    if (numRows <= 0 || c < 0 || c >= numColumns)
        return 0.0f;

    const auto lo = rowAt (lowHz);
    const auto hi = rowAt (highHz);

    if (! (hi > lo))  // also NaN
        return valueAt (c, 0.5 * (lowHz + highHz));

    // Row r is a cell from r - 0.5 to r + 0.5; integrate the cells the span covers.
    const auto from = std::max (lo, -0.5);
    const auto to = std::min (hi, static_cast<double> (numRows) - 0.5);
    const auto* cells = column (c);
    double sum = 0.0;

    for (auto r = static_cast<int> (std::floor (from + 0.5)); r < numRows && static_cast<double> (r) - 0.5 < to; ++r)
    {
        const auto covered = std::min (to, r + 0.5) - std::max (from, r - 0.5);

        if (covered > 0.0)
            sum += covered * static_cast<double> (cells[r]);
    }

    return static_cast<float> (sum / (hi - lo));
}

float MaskPlan::maxOver (int c, double lowHz, double highHz) const noexcept
{
    if (numRows <= 0 || c < 0 || c >= numColumns)
        return 0.0f;

    const auto lo = rowAt (lowHz);
    const auto hi = rowAt (highHz);

    if (! (hi >= lo))  // also NaN
        return valueAt (c, 0.5 * (lowHz + highHz));

    const auto from = std::max (lo, -0.5);
    const auto to = std::min (hi, static_cast<double> (numRows) - 0.5);
    const auto* cells = column (c);
    auto loudest = 0.0f;

    for (auto r = static_cast<int> (std::floor (from + 0.5)); r < numRows && static_cast<double> (r) - 0.5 <= to; ++r)
        loudest = std::max (loudest, cells[r]);

    return loudest;
}

std::unique_ptr<MaskPlan> buildMaskPlan (const Canvas& canvas, const MaskSettings& settings)
{
    auto plan = std::make_unique<MaskPlan>();
    plan->scale = settings.scale.sanitised();
    plan->numColumns = Canvas::width;
    plan->numRows = Canvas::height;

    const auto rows = static_cast<size_t> (Canvas::height);
    plan->values.assign (static_cast<size_t> (Canvas::width) * rows, 0.0f);

    // The canvas is row-major from the top; the plan is column-major from the bottom, so that
    // one column (one moment) is one contiguous run, low frequencies first.
    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto row = static_cast<size_t> (Canvas::height - 1 - y);

        for (int x = 0; x < Canvas::width; ++x)
        {
            const auto v = canvas.get (x, y);
            plan->values[static_cast<size_t> (x) * rows + row] = v > settings.threshold ? std::min (v, 1.0f) : 0.0f;
        }
    }

    dilatePlan (*plan, settings.haloRows, settings.haloColumns);
    markUsedColumns (*plan);
    return plan;
}

std::unique_ptr<MaskPlan> widenMaskPlan (const MaskPlan& plan, int haloRows, int haloColumns)
{
    auto widened = std::make_unique<MaskPlan>();
    widened->scale = plan.scale;
    widened->numColumns = plan.numColumns;
    widened->numRows = plan.numRows;
    widened->values = plan.values;

    dilatePlan (*widened, std::max (0, haloRows), std::max (0, haloColumns));
    markUsedColumns (*widened);
    return widened;
}

} // namespace freqart
