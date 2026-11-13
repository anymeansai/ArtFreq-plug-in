#include "CanvasPlanImage.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace freqart::ui
{
namespace
{
    /** log2 to within 0.005 (0.03 dB), several times faster than std::log2: the canvas maps up
        to a million amplitudes to colours whenever the plan changes. x must be a positive,
        normal float. */
    inline float fastLog2 (float x) noexcept
    {
        int32_t bits = 0;
        std::memcpy (&bits, &x, sizeof (bits));
        // The polynomial gives log2 (m) + 1 for the mantissa m in [1, 2): hence 128, not 127.
        const auto exponent = static_cast<float> (((bits >> 23) & 0xff) - 128);
        bits = (bits & 0x007fffff) | 0x3f800000;
        float mantissa = 0.0f;
        std::memcpy (&mantissa, &bits, sizeof (mantissa));
        return exponent + (-0.34484843f * mantissa + 2.02466578f) * mantissa - 0.67487759f;
    }

    constexpr float dbPerOctave = 6.0205999f;  // 20 log10 (2)
} // namespace

void writePixelRow (juce::Image::BitmapData& data, int y, const juce::PixelARGB* source, int width) noexcept
{
    auto* line = data.getLinePointer (y);

    if (data.pixelFormat == juce::Image::ARGB && data.pixelStride == 4)
    {
        std::memcpy (line, source, static_cast<size_t> (width) * sizeof (juce::PixelARGB));
        return;
    }

    // RGB images are 3 bytes a pixel in software and 4 in some native formats.
    for (int x = 0; x < width; ++x)
        reinterpret_cast<juce::PixelRGB*> (line + x * data.pixelStride)->set (source[x]);
}

uint8_t CanvasPlanImage::levelFor (float amplitude, float loudest) noexcept
{
    if (! (amplitude > 0.0f) || ! (loudest > 0.0f))
        return 0;

    const auto ratio = amplitude / loudest;

    if (ratio <= 0.001f)  // -60 dB and below: the colour of silence
        return 0;

    const auto db = dbPerOctave * fastLog2 (std::min (ratio, 1.0f));
    const auto t = 1.0f + db / windowDb;
    return static_cast<uint8_t> (juce::jlimit (0, 255, static_cast<int> (t * 255.0f + 0.5f)));
}

juce::Image CanvasPlanImage::get (const std::shared_ptr<const ArtPlan>& plan, const FrequencyScale& scale,
                                  int width, int height, float pixelsPerLogical)
{
    width = juce::jmax (1, width);
    height = juce::jmax (1, height);

    const auto* planPointer = plan.get();
    const auto version = plan != nullptr ? plan->version : 0;

    if (image.isValid() && image.getWidth() == width && image.getHeight() == height
        && imagePlan == planPointer && imageVersion == version && imageScale == scale
        && juce::exactlyEqual (imagePixelsPerLogical, pixelsPerLogical))
        return image;

    if (plan != nullptr && (levelsPlan != plan || levelsPlan->version != version))
    {
        levelsPlan = plan;
        updateLevels (*plan);
    }
    else if (plan == nullptr)
    {
        levelsPlan.reset();
    }

    render (planPointer, scale, width, height, pixelsPerLogical);

    imagePlan = planPointer;
    imageVersion = version;
    imageScale = scale;
    imagePixelsPerLogical = pixelsPerLogical;
    return image;
}

void CanvasPlanImage::updateLevels (const ArtPlan& plan)
{
    const auto numLines = plan.numLines;
    const auto numColumns = plan.numColumns;
    const auto total = static_cast<size_t> (numLines) * static_cast<size_t> (numColumns);

    levels.assign (total, 0);
    lineAudible.assign (static_cast<size_t> (juce::jmax (0, numLines)), 0);

    if (total == 0 || plan.amplitudes.size() < total || plan.silent)
        return;

    // The rising comb is played combGain louder than a line so that an analyser shows it as
    // bright (its energy is spread along its glide): its grid is drawn at the brightness the
    // analyser shows, as a line of amplitude / combGain would be.
    const auto combLines = juce::jlimit (0, numLines, plan.combLines);
    const auto shown = [combLines] (int line, float amplitude) { return line < combLines ? amplitude / combGain : amplitude; };
    auto loudest = 0.0f;

    for (int column = 0; column < numColumns; ++column)
    {
        const auto* amplitudes = plan.amplitudes.data() + static_cast<size_t> (column) * static_cast<size_t> (numLines);

        for (int line = 0; line < numLines; ++line)
            loudest = std::max (loudest, shown (line, amplitudes[line]));
    }

    // The plan is column-major; the picture is drawn row by row, so store it line-major.
    for (int column = 0; column < numColumns; ++column)
    {
        const auto* amplitudes = plan.amplitudes.data() + static_cast<size_t> (column) * static_cast<size_t> (numLines);

        for (int line = 0; line < numLines; ++line)
        {
            if (! (amplitudes[line] > 0.0f))
                continue;

            const auto level = levelFor (shown (line, amplitudes[line]), loudest);
            levels[static_cast<size_t> (line) * static_cast<size_t> (numColumns) + static_cast<size_t> (column)] = level;
            lineAudible[static_cast<size_t> (line)] |= static_cast<uint8_t> (level > 0 ? 1 : 0);
        }
    }
}

void CanvasPlanImage::render (const ArtPlan* plan, const FrequencyScale& scale, int width, int height, float pixelsPerLogical)
{
    // Opaque RGB: drawn with a straight copy rather than blended pixel by pixel.
    if (! image.isValid() || image.getWidth() != width || image.getHeight() != height)
        image = juce::Image (juce::Image::RGB, width, height, false);

    const auto& table = theme::spectrumTable();
    const auto silence = table[0];

    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    rowPixels.resize (static_cast<size_t> (width));

    const auto writeRow = [&] (int y, const juce::PixelARGB* source)
    {
        writePixelRow (data, y, source, width);
    };

    const auto fillRow = [&] (int y)
    {
        std::fill (rowPixels.begin(), rowPixels.end(), silence);
        writeRow (y, rowPixels.data());
    };

    const auto numLines = plan != nullptr ? plan->numLines : 0;
    const auto numColumns = plan != nullptr ? plan->numColumns : 0;

    if (plan == nullptr || plan->silent || numLines <= 0 || numColumns <= 0
        || static_cast<int> (plan->frequencies.size()) < numLines
        || levels.size() < static_cast<size_t> (numLines) * static_cast<size_t> (numColumns))
    {
        for (int y = 0; y < height; ++y)
            fillRow (y);
        return;
    }

    // Columns: the engine glides from each column's amplitudes to the next one's (and from the
    // last back to the first), so each image column shows the plan interpolated at its centre.
    columnOf.resize (static_cast<size_t> (width));
    nextColumnOf.resize (static_cast<size_t> (width));
    columnFraction.resize (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
    {
        const auto position = (static_cast<double> (x) + 0.5) * numColumns / width;
        const auto column = juce::jlimit (0, numColumns - 1, static_cast<int> (position));
        columnOf[static_cast<size_t> (x)] = column;
        nextColumnOf[static_cast<size_t> (x)] = (column + 1) % numColumns;
        columnFraction[static_cast<size_t> (x)] = static_cast<float> (juce::jlimit (0.0, 1.0, position - column));
    }

    // Rows: where each audible line sits. A line owns the band halfway to its neighbours; it is
    // drawn over all of it when that is thin (solid shapes at high density), else as a line of
    // maxLineThickness at its frequency (scan lines at low density). The rising comb's grid lines
    // (Smooth lows) each own their cell of the comb's band and are always drawn over all of it:
    // the comb's sines pass every height, so the analyser shows the band filled, never stripes.
    const auto s = scale.sanitised();
    const auto h = static_cast<double> (height);
    const auto maxThickness = static_cast<double> (maxLineThickness * pixelsPerLogical);
    const auto normOf = [&] (int line) { return s.toNorm (static_cast<double> (plan->frequencies[static_cast<size_t> (line)])); };
    const auto combLines = juce::jlimit (0, numLines, plan->combLines);
    const auto halfCell = 0.5 * static_cast<double> (plan->combGridHz);

    struct Extent
    {
        int line;
        double top, bottom;
        bool dense;
    };

    std::vector<Extent> extents;
    extents.reserve (static_cast<size_t> (numLines));

    for (int line = 0; line < numLines; ++line)
    {
        if (lineAudible[static_cast<size_t> (line)] == 0)
            continue;

        const auto norm = normOf (line);
        auto low = 0.0, high = 0.0;

        if (line < combLines)
        {
            const auto hz = static_cast<double> (plan->frequencies[static_cast<size_t> (line)]);
            low = s.toNorm (hz - halfCell);
            high = s.toNorm (hz + halfCell);
        }
        else
        {
            const auto below = line > 0 ? normOf (line - 1) : (numLines > 1 ? 2.0 * norm - normOf (line + 1) : norm - 0.5);
            const auto above = line + 1 < numLines ? normOf (line + 1) : (numLines > 1 ? 2.0 * norm - normOf (line - 1) : norm + 0.5);
            low = (below + norm) * 0.5;
            high = (norm + above) * 0.5;

            // The first line above the comb takes over where the comb's band ends (halfway to the
            // comb's top grid line would leave a dark seam between them).
            if (line == combLines && combLines > 0)
                low = s.toNorm (static_cast<double> (plan->frequencies[static_cast<size_t> (combLines - 1)]) + halfCell);
        }

        const auto span = (high - low) * h;

        Extent extent { line, 0.0, 0.0, span < 1.0 };

        if (span <= maxThickness || line < combLines)
        {
            extent.top = (1.0 - high) * h;
            extent.bottom = (1.0 - low) * h;
        }
        else
        {
            const auto centre = (1.0 - norm) * h;
            extent.top = centre - maxThickness * 0.5;
            extent.bottom = centre + maxThickness * 0.5;
        }

        if (extent.bottom > 0.0 && extent.top < h && extent.bottom > extent.top)
            extents.push_back (extent);
    }

    // Bucket the coverage by image row (counting sort: two passes over the extents).
    rowStart.assign (static_cast<size_t> (height) + 1, 0);
    rowDense.assign (static_cast<size_t> (height), 0);

    const auto rowsOf = [height] (const Extent& e)
    {
        return juce::Range<int> (juce::jmax (0, static_cast<int> (std::floor (e.top))),
                                 juce::jmin (height, static_cast<int> (std::ceil (e.bottom))));
    };

    for (const auto& e : extents)
        for (auto y = rowsOf (e).getStart(); y < rowsOf (e).getEnd(); ++y)
            ++rowStart[static_cast<size_t> (y) + 1];

    for (size_t y = 1; y < rowStart.size(); ++y)
        rowStart[y] += rowStart[y - 1];

    contributions.resize (static_cast<size_t> (rowStart.back()));
    std::vector<int> fill (rowStart.begin(), rowStart.end() - 1);

    for (const auto& e : extents)
    {
        const auto rows = rowsOf (e);

        for (auto y = rows.getStart(); y < rows.getEnd(); ++y)
        {
            const auto weight = std::min (e.bottom, static_cast<double> (y + 1)) - std::max (e.top, static_cast<double> (y));
            contributions[static_cast<size_t> (fill[static_cast<size_t> (y)]++)] = { e.line, static_cast<float> (juce::jlimit (0.0, 1.0, weight)) };
            rowDense[static_cast<size_t> (y)] |= static_cast<uint8_t> (e.dense ? 1 : 0);
        }
    }

    // Pixels. Rows covered by several lines (more lines than pixels) show the loudest, so a thin
    // stroke never fades; otherwise each line adds its share of the row (anti-aliased edges).
    accumulator.resize (static_cast<size_t> (width));
    const auto* columns = columnOf.data();
    const auto* nextColumns = nextColumnOf.data();
    const auto* fractions = columnFraction.data();

    for (int y = 0; y < height; ++y)
    {
        const auto first = rowStart[static_cast<size_t> (y)];
        const auto last = rowStart[static_cast<size_t> (y) + 1];

        if (first == last)
        {
            fillRow (y);
            continue;
        }

        auto* acc = accumulator.data();
        std::fill (acc, acc + width, 0.0f);
        const auto dense = rowDense[static_cast<size_t> (y)] != 0;

        for (auto i = first; i < last; ++i)
        {
            const auto& c = contributions[static_cast<size_t> (i)];
            const auto* lineLevels = levels.data() + static_cast<size_t> (c.line) * static_cast<size_t> (numColumns);

            const auto levelAt = [&] (int x)
            {
                const auto a = static_cast<float> (lineLevels[columns[x]]);
                const auto b = static_cast<float> (lineLevels[nextColumns[x]]);
                return a + (b - a) * fractions[x];
            };

            if (dense)
            {
                for (int x = 0; x < width; ++x)
                    acc[x] = std::max (acc[x], levelAt (x));
            }
            else
            {
                const auto w = c.weight;

                for (int x = 0; x < width; ++x)
                    acc[x] += w * levelAt (x);
            }
        }

        auto* pixels = rowPixels.data();

        for (int x = 0; x < width; ++x)
            pixels[x] = table[static_cast<size_t> (std::min (255, static_cast<int> (acc[x] + 0.5f)))];

        writeRow (y, pixels);
    }
}

} // namespace freqart::ui
