#include "Compositor.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace freqart
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

void drawText (const ArtObject& object, juce::Rectangle<float> area, float intensity, Canvas& out)
{
    const auto clip = canvasClip (area);

    if (clip.isEmpty() || object.text.trim().isEmpty())
        return;

    juce::Image mask (juce::Image::SingleChannel, clip.getWidth(), clip.getHeight(), true, juce::SoftwareImageType());
    renderTextInto (mask, area.translated (static_cast<float> (-clip.getX()), static_cast<float> (-clip.getY())),
                    object.text, object.fontIndex, object.textStyle);

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

} // namespace

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

        if (canvasClip (area).isEmpty())
            continue;

        if (object.type == ArtObject::Type::text)
            drawText (object, area, intensity, out);
        else if (object.image.isValid())
            drawImage (processed (object), area, intensity, out);
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

} // namespace freqart
