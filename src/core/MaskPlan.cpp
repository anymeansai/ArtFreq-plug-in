#include "MaskPlan.h"

#include <algorithm>
#include <cmath>

namespace freqart
{

namespace
{
    /** Replaces every value by the maximum within `radius` steps along one direction (a
        separable square dilation is this along the rows, then along the columns). `stride` is
        the step between neighbours, `count` the number of steps in that direction and `lines`
        how many such lines there are, `lineStride` apart. */
    void dilate (std::vector<float>& values, int radius, int count, size_t stride, int lines, size_t lineStride)
    {
        if (radius <= 0)
            return;

        std::vector<float> line (static_cast<size_t> (count));

        for (int l = 0; l < lines; ++l)
        {
            auto* first = values.data() + static_cast<size_t> (l) * lineStride;

            for (int i = 0; i < count; ++i)
                line[static_cast<size_t> (i)] = first[static_cast<size_t> (i) * stride];

            for (int i = 0; i < count; ++i)
            {
                const auto from = std::max (0, i - radius);
                const auto to = std::min (count - 1, i + radius);
                first[static_cast<size_t> (i) * stride] = *std::max_element (line.begin() + from, line.begin() + to + 1);
            }
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

    dilate (plan->values, settings.haloRows, Canvas::height, 1, Canvas::width, rows);
    dilate (plan->values, settings.haloColumns, Canvas::width, rows, Canvas::height, 1);

    plan->columnUsed.assign (static_cast<size_t> (Canvas::width), 0);

    for (int x = 0; x < Canvas::width; ++x)
    {
        const auto* column = plan->column (x);
        const auto used = std::any_of (column, column + Canvas::height, [] (float v) { return v > 0.0f; });
        plan->columnUsed[static_cast<size_t> (x)] = used ? 1 : 0;
        plan->empty = plan->empty && ! used;
    }

    return plan;
}

} // namespace freqart
