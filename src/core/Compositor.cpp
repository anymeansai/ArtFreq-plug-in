#include "Compositor.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace artfreq
{

namespace
{

/** Resampling weights along one axis: destination pixels [first, first + count) each take
    taps [start[i], start[i + 1]) of (source index, weight). */
struct Taps
{
    int first = 0;
    std::vector<int> start;
    std::vector<int> index;
    std::vector<float> weight;
    int lowestSource = 0;
    int highestSource = -1;
};

/** Weights for drawing `sourceSize` samples across the destination span
    [origin, origin + length) (in destination pixels), for destination pixels [first, last).

    Shrinking averages the source samples each destination pixel covers (no aliasing);
    enlarging interpolates bilinearly between the nearest two. Destination pixels only partly
    inside the span are weighted by how much of them it covers, so edges are anti-aliased and
    objects move smoothly by fractions of a pixel. */
Taps makeTaps (int sourceSize, double origin, double length, int first, int last)
{
    Taps taps;
    taps.first = first;
    taps.start.reserve (static_cast<size_t> (std::max (0, last - first) + 1));
    taps.lowestSource = sourceSize;

    const auto perPixel = static_cast<double> (sourceSize) / length;  // source samples per destination pixel
    const auto add = [&taps] (int index, double weight)
    {
        if (weight <= 1.0e-7)
            return;

        taps.index.push_back (index);
        taps.weight.push_back (static_cast<float> (weight));
        taps.lowestSource = std::min (taps.lowestSource, index);
        taps.highestSource = std::max (taps.highestSource, index);
    };

    for (int i = first; i < last; ++i)
    {
        taps.start.push_back (static_cast<int> (taps.index.size()));

        if (perPixel > 1.0)
        {
            const auto a = std::max (0.0, (i - origin) * perPixel);
            const auto b = std::min (static_cast<double> (sourceSize), (i + 1 - origin) * perPixel);

            for (auto j = static_cast<int> (std::floor (a)); j < b && j < sourceSize; ++j)
                add (j, (std::min (b, j + 1.0) - std::max (a, static_cast<double> (j))) / perPixel);
        }
        else
        {
            const auto coverage = std::clamp (std::min (i + 1.0, origin + length) - std::max (static_cast<double> (i), origin), 0.0, 1.0);

            if (coverage <= 0.0)
                continue;

            const auto centre = std::clamp ((i + 0.5 - origin) * perPixel - 0.5, 0.0, static_cast<double> (sourceSize - 1));
            const auto j = static_cast<int> (std::floor (centre));
            const auto fraction = centre - j;

            add (j, (1.0 - fraction) * coverage);

            if (j + 1 < sourceSize)
                add (j + 1, fraction * coverage);
        }
    }

    taps.start.push_back (static_cast<int> (taps.index.size()));
    return taps;
}

/** The paint layer (1024 x 512 bytes) over the canvas height: each canvas row takes the paint
    at the same frequency, read through the scale the paint was made on (paintScale), so paint
    keeps its frequencies like objects do when the scale or range changes. Rows outside the
    paint's range get nothing. Where the paint is stretched it is read bilinearly; where it is
    squeezed (several paint rows per canvas row) a canvas row takes the loudest of them, so a
    thin stroke never falls between rows. */
void drawPaint (const std::vector<uint8_t>& paint, const FrequencyScale& scale, const FrequencyScale& paintScale, Canvas& out)
{
    constexpr auto paintWidth = ArtDocument::paintWidth;
    constexpr auto paintHeight = ArtDocument::paintHeight;
    static_assert (paintWidth == Canvas::width, "the paint layer is as wide as the canvas");

    if (paint.size() != static_cast<size_t> (paintWidth * paintHeight))
        return;

    const auto canvasScale = scale.sanitised();
    const auto layerScale = paintScale.sanitised();

    // Canvas y (0 = top, in rows) to paint y (0 = top, in paint rows).
    const auto toPaintY = [&canvasScale, &layerScale] (double y)
    {
        return canvasToPaintY (y / Canvas::height, canvasScale, layerScale);
    };

    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto centre = toPaintY (y + 0.5);

        if (! (centre >= 0.0 && centre <= static_cast<double> (paintHeight)))
            continue;

        auto* dst = out.values.data() + static_cast<size_t> (y) * Canvas::width;
        const auto top = toPaintY (y);
        const auto bottom = toPaintY (y + 1.0);

        if (bottom - top > 1.0)
        {
            // The paint rows whose centres fall inside this canvas row.
            const auto first = std::clamp (static_cast<int> (std::ceil (top - 0.5)), 0, paintHeight - 1);
            const auto last = std::clamp (static_cast<int> (std::ceil (bottom - 0.5)) - 1, first, paintHeight - 1);

            for (int row = first; row <= last; ++row)
            {
                const auto* p = paint.data() + static_cast<size_t> (row) * paintWidth;

                for (int x = 0; x < Canvas::width; ++x)
                    dst[x] = std::max (dst[x], static_cast<float> (p[x]) * (1.0f / 255.0f));
            }

            continue;
        }

        // Pixel centres line up: the paint is sampled at the canvas row centre's height.
        const auto source = std::clamp (centre - 0.5, 0.0, static_cast<double> (paintHeight - 1));
        const auto row0 = static_cast<int> (source);
        const auto row1 = std::min (paintHeight - 1, row0 + 1);
        const auto f = static_cast<float> (source - row0);

        const auto* p0 = paint.data() + static_cast<size_t> (row0) * paintWidth;
        const auto* p1 = paint.data() + static_cast<size_t> (row1) * paintWidth;

        const auto w0 = (1.0f - f) / 255.0f;
        const auto w1 = f / 255.0f;

        for (int x = 0; x < Canvas::width; ++x)
            dst[x] = std::max (dst[x], static_cast<float> (p0[x]) * w0 + static_cast<float> (p1[x]) * w1);
    }
}

juce::Rectangle<float> pixelRect (const ArtObject& object, const FrequencyScale& scale) noexcept
{
    const auto norm = objectRectNorm (object, scale);
    return { norm.getX() * Canvas::width, norm.getY() * Canvas::height,
             norm.getWidth() * Canvas::width, norm.getHeight() * Canvas::height };
}

/** The canvas pixels a rectangle touches, or an empty rectangle. */
juce::Rectangle<int> canvasClip (juce::Rectangle<float> area)
{
    if (! area.isFinite() || area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
        return {};

    // Bounded first so a rectangle far off the canvas cannot overflow an int.
    const auto bounded = area.getIntersection ({ -1.0f, -1.0f, Canvas::width + 2.0f, Canvas::height + 2.0f });
    return bounded.getSmallestIntegerContainer().getIntersection ({ 0, 0, Canvas::width, Canvas::height });
}

/** How far a turned object's drawing may reach outside its turned box, in canvas pixels: the
    outline style's stroke (half of 7 % of the box's height, see renderTextInto), which the turn
    can bring nearer the edge than the letters' inset, and a pixel of anti-aliasing. */
float turnedReach (juce::Rectangle<float> area) noexcept
{
    return std::max (3.0f, area.getHeight() * 0.07f) * 0.5f + 2.0f;
}

void drawText (const ArtObject& object, juce::Rectangle<float> area, float intensity, Canvas& out)
{
    const auto degrees = ArtDocument::normaliseRotation (object.rotation);
    const auto turned = ! juce::exactlyEqual (degrees, 0.0f);
    const auto clip = canvasClip (turned ? area.transformedBy (rotationTransform (area, degrees)).expanded (turnedReach (area)) : area);

    if (clip.isEmpty() || object.text.trim().isEmpty())
        return;

    // In the mask's pixels (the clip's top-left at 0, 0). Canvas pixels are a square grid over
    // the canvas, like normalised coordinates, so the turn takes displayAspect as it is.
    const auto local = area.translated (static_cast<float> (-clip.getX()), static_cast<float> (-clip.getY()));
    juce::Image mask (juce::Image::SingleChannel, clip.getWidth(), clip.getHeight(), true, juce::SoftwareImageType());
    renderTextInto (mask, local, object.text, object.fontIndex, object.textStyle,
                    turned ? rotationTransform (local, degrees) : juce::AffineTransform());

    const juce::Image::BitmapData data (mask, juce::Image::BitmapData::readOnly);
    const auto scale = intensity / 255.0f;

    for (int y = 0; y < clip.getHeight(); ++y)
    {
        const auto* src = data.getLinePointer (y);
        auto* dst = out.values.data() + static_cast<size_t> (y + clip.getY()) * Canvas::width + static_cast<size_t> (clip.getX());

        for (int x = 0; x < clip.getWidth(); ++x)
        {
            const auto value = src[x * data.pixelStride];

            if (value != 0)
                dst[x] = std::max (dst[x], static_cast<float> (value) * scale);
        }
    }
}

void drawImage (const ProcessedImage& image, juce::Rectangle<float> area, float intensity, Canvas& out)
{
    const auto clip = canvasClip (area);

    if (clip.isEmpty() || ! image.isValid())
        return;

    const auto columns = makeTaps (image.width, area.getX(), area.getWidth(), clip.getX(), clip.getRight());
    const auto rows = makeTaps (image.height, area.getY(), area.getHeight(), clip.getY(), clip.getBottom());

    if (rows.highestSource < rows.lowestSource || columns.highestSource < columns.lowestSource)
        return;

    // Horizontal pass over just the source rows the vertical pass needs.
    const auto width = clip.getWidth();
    const auto firstRow = rows.lowestSource;
    const auto numRows = rows.highestSource - rows.lowestSource + 1;
    std::vector<float> stretched (static_cast<size_t> (numRows) * static_cast<size_t> (width));

    for (int r = 0; r < numRows; ++r)
    {
        const auto* src = image.values.data() + static_cast<size_t> (firstRow + r) * static_cast<size_t> (image.width);
        auto* dst = stretched.data() + static_cast<size_t> (r) * static_cast<size_t> (width);

        for (int x = 0; x < width; ++x)
        {
            float sum = 0.0f;

            for (auto t = columns.start[static_cast<size_t> (x)]; t < columns.start[static_cast<size_t> (x + 1)]; ++t)
                sum += src[columns.index[static_cast<size_t> (t)]] * columns.weight[static_cast<size_t> (t)];

            dst[x] = sum;
        }
    }

    // Vertical pass, straight into the canvas (max combine).
    for (int y = 0; y < clip.getHeight(); ++y)
    {
        auto* dst = out.values.data() + static_cast<size_t> (y + clip.getY()) * Canvas::width + static_cast<size_t> (clip.getX());
        const auto tapBegin = rows.start[static_cast<size_t> (y)];
        const auto tapEnd = rows.start[static_cast<size_t> (y + 1)];

        if (tapBegin == tapEnd)
            continue;

        for (int x = 0; x < width; ++x)
        {
            float sum = 0.0f;

            for (auto t = tapBegin; t < tapEnd; ++t)
                sum += stretched[static_cast<size_t> (rows.index[static_cast<size_t> (t)] - firstRow) * static_cast<size_t> (width) + static_cast<size_t> (x)]
                       * rows.weight[static_cast<size_t> (t)];

            dst[x] = std::max (dst[x], juce::jlimit (0.0f, 1.0f, sum * intensity));
        }
    }
}

/** A turned picture: the processed picture is first resampled to the grid it covers once
    turned (as many cells along each of its sides as canvas pixels those sides span), area-
    averaged when shrinking and bilinear when enlarging, as drawImage does; then each canvas
    pixel's centre is taken back into the box and the grid read there bilinearly, with nothing
    outside the box, so the edges are anti-aliased. */
void drawTurnedImage (const ProcessedImage& image, juce::Rectangle<float> area, const juce::AffineTransform& turn,
                      float intensity, Canvas& out)
{
    const auto clip = canvasClip (area.transformedBy (turn).expanded (1.0f));

    if (clip.isEmpty() || ! image.isValid() || area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
        return;

    // How many canvas pixels each side of the box spans once turned.
    const auto origin = area.getTopLeft().transformedBy (turn);
    const auto spanU = area.getTopRight().transformedBy (turn).getDistanceFrom (origin);
    const auto spanV = area.getBottomLeft().transformedBy (turn).getDistanceFrom (origin);

    // Bounded (an object may reach far off the canvas): 16 million cells at the most.
    constexpr double maxCells = 16.0e6;
    auto cellsU = std::clamp (std::ceil (static_cast<double> (spanU)), 1.0, 8192.0);
    auto cellsV = std::clamp (std::ceil (static_cast<double> (spanV)), 1.0, 8192.0);

    if (cellsU * cellsV > maxCells)
    {
        const auto shrink = std::sqrt (maxCells / (cellsU * cellsV));
        cellsU = std::max (1.0, std::floor (cellsU * shrink));
        cellsV = std::max (1.0, std::floor (cellsV * shrink));
    }

    const auto nu = static_cast<int> (cellsU), nv = static_cast<int> (cellsV);
    const auto columns = makeTaps (image.width, 0.0, nu, 0, nu);
    const auto rows = makeTaps (image.height, 0.0, nv, 0, nv);

    if (rows.highestSource < rows.lowestSource || columns.highestSource < columns.lowestSource)
        return;

    // The grid: a horizontal pass over the source rows the vertical pass needs, then that.
    const auto firstRow = rows.lowestSource;
    const auto numRows = rows.highestSource - rows.lowestSource + 1;
    std::vector<float> stretched (static_cast<size_t> (numRows) * static_cast<size_t> (nu));

    for (int r = 0; r < numRows; ++r)
    {
        const auto* src = image.values.data() + static_cast<size_t> (firstRow + r) * static_cast<size_t> (image.width);
        auto* dst = stretched.data() + static_cast<size_t> (r) * static_cast<size_t> (nu);

        for (int x = 0; x < nu; ++x)
        {
            float sum = 0.0f;

            for (auto t = columns.start[static_cast<size_t> (x)]; t < columns.start[static_cast<size_t> (x + 1)]; ++t)
                sum += src[columns.index[static_cast<size_t> (t)]] * columns.weight[static_cast<size_t> (t)];

            dst[x] = sum;
        }
    }

    std::vector<float> grid (static_cast<size_t> (nu) * static_cast<size_t> (nv), 0.0f);

    for (int y = 0; y < nv; ++y)
    {
        auto* dst = grid.data() + static_cast<size_t> (y) * static_cast<size_t> (nu);

        for (auto t = rows.start[static_cast<size_t> (y)]; t < rows.start[static_cast<size_t> (y + 1)]; ++t)
        {
            const auto* src = stretched.data() + static_cast<size_t> (rows.index[static_cast<size_t> (t)] - firstRow) * static_cast<size_t> (nu);
            const auto w = rows.weight[static_cast<size_t> (t)];

            for (int x = 0; x < nu; ++x)
                dst[x] += src[x] * w;
        }
    }

    // Canvas pixel centre -> grid cell coordinates (cell centres at whole numbers).
    const auto back = turn.inverted()
                          .translated (-area.getX(), -area.getY())
                          .scaled (static_cast<float> (nu) / area.getWidth(), static_cast<float> (nv) / area.getHeight())
                          .translated (-0.5f, -0.5f);

    const auto cell = [&grid, nu, nv] (int x, int y) noexcept
    {
        return x >= 0 && y >= 0 && x < nu && y < nv ? grid[static_cast<size_t> (y) * static_cast<size_t> (nu) + static_cast<size_t> (x)] : 0.0f;
    };

    for (int y = clip.getY(); y < clip.getBottom(); ++y)
    {
        auto* dst = out.values.data() + static_cast<size_t> (y) * Canvas::width;

        for (int x = clip.getX(); x < clip.getRight(); ++x)
        {
            auto gx = static_cast<float> (x) + 0.5f, gy = static_cast<float> (y) + 0.5f;
            back.transformPoint (gx, gy);

            if (! (gx > -1.0f && gy > -1.0f && gx < static_cast<float> (nu) && gy < static_cast<float> (nv)))
                continue;

            const auto x0 = static_cast<int> (std::floor (gx)), y0 = static_cast<int> (std::floor (gy));
            const auto fx = gx - static_cast<float> (x0), fy = gy - static_cast<float> (y0);
            const auto value = (cell (x0, y0) * (1.0f - fx) + cell (x0 + 1, y0) * fx) * (1.0f - fy)
                             + (cell (x0, y0 + 1) * (1.0f - fx) + cell (x0 + 1, y0 + 1) * fx) * fy;

            if (value > 0.0f)
                dst[x] = std::max (dst[x], juce::jlimit (0.0f, 1.0f, value * intensity));
        }
    }
}

} // namespace

juce::AffineTransform rotationTransform (juce::Rectangle<float> box, float degrees, float xScale) noexcept
{
    degrees = ArtDocument::normaliseRotation (degrees);

    if (juce::exactlyEqual (degrees, 0.0f) || ! (xScale > 0.0f) || ! std::isfinite (xScale))
        return {};

    const auto centre = box.getCentre();
    return juce::AffineTransform::translation (-centre.x, -centre.y)
               .scaled (xScale, 1.0f)
               .rotated (juce::degreesToRadians (degrees))
               .scaled (1.0f / xScale, 1.0f)
               .translated (centre.x, centre.y);
}

std::array<juce::Point<float>, 4> objectCornersNorm (const ArtObject& object, const FrequencyScale& scale) noexcept
{
    const auto r = objectRectNorm (object, scale);
    const auto turn = rotationTransform (r, object.rotation);
    return { r.getTopLeft().transformedBy (turn), r.getTopRight().transformedBy (turn),
             r.getBottomRight().transformedBy (turn), r.getBottomLeft().transformedBy (turn) };
}

juce::Rectangle<float> objectBoundsNorm (const ArtObject& object, const FrequencyScale& scale) noexcept
{
    const auto r = objectRectNorm (object, scale);

    if (juce::exactlyEqual (ArtDocument::normaliseRotation (object.rotation), 0.0f))
        return r;

    const auto corners = objectCornersNorm (object, scale);
    return juce::Rectangle<float>::findAreaContainingPoints (corners.data(), static_cast<int> (corners.size()));
}

float displayAspectOf (const ArtObject& object, const FrequencyScale& scale) noexcept
{
    const auto r = objectRectNorm (object, scale);
    return r.getHeight() > 0.0f ? r.getWidth() * ArtDocument::displayAspect / r.getHeight() : 1.0f;
}

float shapeAspect (ShapePreset preset, const ArtObject& object) noexcept
{
    switch (preset)
    {
        case ShapePreset::original:
            return object.image.isValid() && object.image.getHeight() > 0
                       ? static_cast<float> (object.image.getWidth()) / static_cast<float> (object.image.getHeight())
                       : 1.0f;
        case ShapePreset::square:        return 1.0f;
        case ShapePreset::fourByThree:   return 4.0f / 3.0f;
        case ShapePreset::threeByFour:   return 3.0f / 4.0f;
        case ShapePreset::sixteenByNine: return 16.0f / 9.0f;
        case ShapePreset::nineBySixteen: return 9.0f / 16.0f;
    }

    return 1.0f;
}

void applyShapeAspect (ArtObject& object, float aspect, const FrequencyScale& scale) noexcept
{
    if (! (aspect > 0.0f) || ! std::isfinite (aspect))
        return;

    // In display units: the canvas is displayAspect wide and 1 tall.
    constexpr auto canvasWidth = static_cast<double> (ArtDocument::displayAspect);
    const auto r = objectRectNorm (object, scale);
    const auto area = std::max (1.0e-8, static_cast<double> (r.getWidth()) * canvasWidth * static_cast<double> (r.getHeight()));
    auto width = std::sqrt (area * aspect);
    auto height = std::sqrt (area / aspect);
    auto cx = static_cast<double> (r.getCentreX()) * canvasWidth;
    auto cy = static_cast<double> (r.getCentreY());

    // No larger than the canvas, then no further than its edges from the centre (shrinking to
    // half the size at the most: past that the centre moves in instead).
    const auto fit = std::min ({ 1.0, canvasWidth / width, 1.0 / height });
    width *= fit;
    height *= fit;

    const auto roomX = 2.0 * std::max (0.0, std::min (cx, canvasWidth - cx));
    const auto roomY = 2.0 * std::max (0.0, std::min (cy, 1.0 - cy));
    const auto shrink = std::clamp (std::min (roomX / width, roomY / height), 0.5, 1.0);
    width *= shrink;
    height *= shrink;

    cx = std::clamp (cx, width * 0.5, canvasWidth - width * 0.5);
    cy = std::clamp (cy, height * 0.5, 1.0 - height * 0.5);

    const auto top = static_cast<float> (cy - height * 0.5);
    const auto bottom = static_cast<float> (cy + height * 0.5);
    const auto hz = normSpanToHz (top, bottom, scale);

    object.startTime = static_cast<float> ((cx - width * 0.5) / canvasWidth);
    object.endTime = static_cast<float> ((cx + width * 0.5) / canvasWidth);
    object.lowHz = static_cast<float> (hz.low);
    object.highHz = static_cast<float> (hz.high);
}

int matchingShapePreset (const ArtObject& object, const FrequencyScale& scale) noexcept
{
    const auto aspect = displayAspectOf (object, scale);

    for (int i = 0; i < numShapePresets; ++i)
    {
        const auto preset = static_cast<ShapePreset> (i);

        if (preset == ShapePreset::original && ! object.image.isValid())
            continue;

        if (std::abs (aspect / shapeAspect (preset, object) - 1.0f) <= 0.02f)
            return i;
    }

    return -1;
}

juce::Rectangle<float> objectRectNorm (const ArtObject& object, const FrequencyScale& scale) noexcept
{
    const auto s = scale.sanitised();
    const auto top = 1.0 - s.toNorm (static_cast<double> (object.highHz));
    const auto bottom = 1.0 - s.toNorm (static_cast<double> (object.lowHz));

    return { object.startTime, static_cast<float> (top),
             object.endTime - object.startTime, static_cast<float> (bottom - top) };
}

HzRange normSpanToHz (float top, float bottom, const FrequencyScale& scale) noexcept
{
    if (top > bottom)
        std::swap (top, bottom);

    const auto s = scale.sanitised();
    return { s.toHz (1.0 - static_cast<double> (bottom)), s.toHz (1.0 - static_cast<double> (top)) };
}

double canvasToPaintY (double canvasY, const FrequencyScale& scale, const FrequencyScale& paintScale) noexcept
{
    if (scale == paintScale)
        return canvasY * ArtDocument::paintHeight;

    // Bounded so a point far off the canvas stays finite (1 Hz keeps the log of it finite).
    const auto hz = std::max (1.0, scale.toHz (1.0 - std::clamp (canvasY, -1.0, 2.0)));
    return (1.0 - paintScale.toNorm (hz)) * ArtDocument::paintHeight;
}

void Compositor::render (const ArtDocument& document, const FrequencyScale& scale, Canvas& out)
{
    if (out.values.size() != static_cast<size_t> (Canvas::width * Canvas::height))
        out.values.assign (static_cast<size_t> (Canvas::width * Canvas::height), 0.0f);
    else
        out.clear();

    // Hidden layers are left out (they stay in the document, silent).
    if (document.paintVisible && document.hasPaint())
        drawPaint (document.paint, scale, document.paintScale, out);

    for (const auto& object : document.objects)
    {
        if (! object.visible)
            continue;

        const auto intensity = std::isfinite (object.intensity) ? juce::jlimit (0.0f, 1.0f, object.intensity) : 0.0f;

        if (intensity <= 0.0f)
            continue;

        const auto area = pixelRect (object, scale);

        const auto upright = juce::exactlyEqual (ArtDocument::normaliseRotation (object.rotation), 0.0f);

        if (canvasClip (upright ? area : area.transformedBy (rotationTransform (area, object.rotation)).expanded (turnedReach (area))).isEmpty())
            continue;

        if (object.type == ArtObject::Type::text)
        {
            drawText (object, area, intensity, out);
        }
        else if (object.image.isValid())
        {
            const auto degrees = ArtDocument::normaliseRotation (object.rotation);

            if (juce::exactlyEqual (degrees, 0.0f))
                drawImage (processed (object), area, intensity, out);
            else
                drawTurnedImage (processed (object), area, rotationTransform (area, degrees), intensity, out);
        }
    }

    // Work for pictures that are gone (or whose mode or clean-up changed) is dropped right away:
    // each processed picture can be several megabytes. A hidden picture keeps its work, so
    // showing it again is instant.
    trimCache (document);
}

void Compositor::trimCache (const ArtDocument& document)
{
    std::set<CacheKey> used;

    for (const auto& object : document.objects)
        if (object.type == ArtObject::Type::image && object.image.isValid())
            used.insert (keyFor (object));

    for (auto it = cache.begin(); it != cache.end();)
    {
        if (used.count (it->first) == 0)
            it = cache.erase (it);
        else
            ++it;
    }
}

Compositor::CacheKey Compositor::keyFor (const ArtObject& object)
{
    const auto cleanup = std::isfinite (object.cleanup) ? juce::jlimit (0.0f, 0.9f, object.cleanup) : 0.0f;
    return { object.image.getPixelData().get(), static_cast<int> (object.imageMode), juce::roundToInt (cleanup * 1000.0f) };
}

const ProcessedImage& Compositor::processed (const ArtObject& object)
{
    const auto key = keyFor (object);
    const auto found = cache.find (key);

    if (found != cache.end())
        return found->second.processed;

    CacheEntry entry;
    entry.image = object.image;
    entry.processed = processImage (object.image, object.imageMode, static_cast<float> (key.cleanupMillis) / 1000.0f);
    return cache.emplace (key, std::move (entry)).first->second.processed;
}

} // namespace artfreq
