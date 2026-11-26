#include "ImageProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace artfreq
{

namespace
{

constexpr float lumR = 0.2126f;  // Rec. 709
constexpr float lumG = 0.7152f;
constexpr float lumB = 0.0722f;

juce::Image asARGB (const juce::Image& image)
{
    return image.getFormat() == juce::Image::ARGB ? image : image.convertedToFormat (juce::Image::ARGB);
}

/** Luminance (0..1) of the un-premultiplied colour and alpha (0..1) of every pixel. */
struct LumaAlpha
{
    int width = 0;
    int height = 0;
    std::vector<float> lum;
    std::vector<float> alpha;
};

LumaAlpha readLumaAlpha (const juce::Image& image)
{
    LumaAlpha result;
    const auto argb = asARGB (image);
    result.width = argb.getWidth();
    result.height = argb.getHeight();

    const auto count = static_cast<size_t> (result.width) * static_cast<size_t> (result.height);
    result.lum.resize (count);
    result.alpha.resize (count);

    const juce::Image::BitmapData data (argb, juce::Image::BitmapData::readOnly);

    for (int y = 0; y < result.height; ++y)
    {
        const auto* line = data.getLinePointer (y);
        auto index = static_cast<size_t> (y) * static_cast<size_t> (result.width);

        for (int x = 0; x < result.width; ++x, ++index)
        {
            const auto& pixel = *reinterpret_cast<const juce::PixelARGB*> (line + x * data.pixelStride);
            const auto a = pixel.getAlpha();

            if (a == 0)
            {
                result.lum[index] = 0.0f;
                result.alpha[index] = 0.0f;
                continue;
            }

            // JUCE stores ARGB premultiplied: divide the alpha back out before measuring.
            const auto weighted = lumR * pixel.getRed() + lumG * pixel.getGreen() + lumB * pixel.getBlue();
            result.lum[index] = juce::jlimit (0.0f, 1.0f, weighted / static_cast<float> (a));
            result.alpha[index] = static_cast<float> (a) / 255.0f;
        }
    }

    return result;
}

bool transparencyIn (const LumaAlpha& pixels)
{
    if (pixels.alpha.empty())
        return false;

    const auto clear = std::count_if (pixels.alpha.begin(), pixels.alpha.end(), [] (float a) { return a < 128.0f / 255.0f; });
    return static_cast<double> (clear) >= 0.02 * static_cast<double> (pixels.alpha.size());
}

/** What Auto does with a picture: draw its alpha (a cut-out shape), its negative (a light
    background) or the picture itself. */
struct AutoChoice
{
    ImageMode mode = ImageMode::original;
    bool drawAlpha = false;
};

AutoChoice autoChoiceFor (const LumaAlpha& pixels)
{
    if (pixels.width <= 0 || pixels.height <= 0)
        return {};

    if (transparencyIn (pixels))
        return { ImageMode::original, true };

    // The outer ring (3 % of each side, at least one pixel) stands for the background.
    const auto ringX = std::max (1, juce::roundToInt (0.03 * pixels.width));
    const auto ringY = std::max (1, juce::roundToInt (0.03 * pixels.height));

    double sum = 0.0;
    size_t count = 0;

    for (int y = 0; y < pixels.height; ++y)
    {
        const auto wholeRow = y < ringY || y >= pixels.height - ringY;
        const auto* lum = pixels.lum.data() + static_cast<size_t> (y) * static_cast<size_t> (pixels.width);

        for (int x = 0; x < pixels.width; ++x)
        {
            if (wholeRow || x < ringX || x >= pixels.width - ringX)
            {
                sum += static_cast<double> (lum[x]);
                ++count;
            }
        }
    }

    const auto lightBackground = count > 0 && sum / static_cast<double> (count) > 0.5;
    return { lightBackground ? ImageMode::negative : ImageMode::original, false };
}

/** The values Auto draws. */
std::vector<float> autoValues (const LumaAlpha& pixels, const AutoChoice& choice)
{
    std::vector<float> values (pixels.lum.size());

    for (size_t i = 0; i < values.size(); ++i)
    {
        const auto a = pixels.alpha[i];
        values[i] = choice.drawAlpha ? a
                  : choice.mode == ImageMode::negative ? a * (1.0f - pixels.lum[i])
                                                       : a * pixels.lum[i];
    }

    return values;
}

/** Sobel gradient magnitude, divided by 4 so a hard 0-to-1 edge measures 1. Borders repeat the
    edge pixels, so the picture's frame is not an edge. */
std::vector<float> sobel (const std::vector<float>& values, int width, int height)
{
    std::vector<float> result (values.size(), 0.0f);

    const auto at = [&] (int x, int y)
    {
        x = juce::jlimit (0, width - 1, x);
        y = juce::jlimit (0, height - 1, y);
        return values[static_cast<size_t> (y) * static_cast<size_t> (width) + static_cast<size_t> (x)];
    };

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto gx = (at (x + 1, y - 1) + 2.0f * at (x + 1, y) + at (x + 1, y + 1))
                          - (at (x - 1, y - 1) + 2.0f * at (x - 1, y) + at (x - 1, y + 1));
            const auto gy = (at (x - 1, y + 1) + 2.0f * at (x, y + 1) + at (x + 1, y + 1))
                          - (at (x - 1, y - 1) + 2.0f * at (x, y - 1) + at (x + 1, y - 1));

            result[static_cast<size_t> (y) * static_cast<size_t> (width) + static_cast<size_t> (x)]
                = std::sqrt (gx * gx + gy * gy) * 0.25f;
        }
    }

    return result;
}

void autoLevels (std::vector<float>& values)
{
    if (values.empty())
        return;

    // 99th percentile (nearest rank), found without sorting everything.
    auto copy = values;
    const auto rank = static_cast<size_t> (std::ceil (0.99 * static_cast<double> (copy.size()))) - 1;
    std::nth_element (copy.begin(), copy.begin() + static_cast<std::ptrdiff_t> (rank), copy.end());
    const auto percentile = copy[rank];

    const auto gain = percentile >= 0.02f ? 1.0f / percentile : 1.0f;

    for (auto& v : values)
        v = juce::jlimit (0.0f, 1.0f, v * gain);
}

void cleanUp (std::vector<float>& values, float cleanup)
{
    const auto c = std::isfinite (cleanup) ? juce::jlimit (0.0f, 0.9f, cleanup) : 0.0f;

    if (c <= 0.0f)
        return;

    const auto scale = 1.0f / (1.0f - c);

    for (auto& v : values)
        v = std::max (0.0f, (v - c) * scale);
}

/** Black and white: every value becomes 0 or 1, split at the threshold Otsu's method finds in
    the histogram (256 levels) of the pixels that can be seen (alpha above 0; all of them when
    the picture's alpha is what is drawn). That threshold is the one that separates the values
    into two groups as far apart, and each as tight, as possible; where several do equally well
    (a gap in the histogram), the middle one. A picture of a single level has nothing to split:
    it is loud if that level is at least half, silent otherwise, and so is one nobody can see. */
void splitInTwo (std::vector<float>& values, const std::vector<float>& alpha, bool alphaIsDrawn)
{
    constexpr int numLevels = 256;
    const auto levelOf = [] (float v) { return juce::jlimit (0, numLevels - 1, juce::roundToInt (v * static_cast<float> (numLevels - 1))); };

    std::array<double, numLevels> histogram {};
    auto total = 0.0, weighted = 0.0;

    for (size_t i = 0; i < values.size(); ++i)
    {
        if (alphaIsDrawn || alpha[i] > 0.0f)
        {
            const auto level = levelOf (values[i]);
            histogram[static_cast<size_t> (level)] += 1.0;
            total += 1.0;
            weighted += static_cast<double> (level);
        }
    }

    // Levels above `split` are loud. With nothing to tell apart: the upper half.
    auto split = (numLevels - 1) / 2;

    if (total > 0.0)
    {
        auto best = 0.0;
        auto firstBest = -1, lastBest = -1;
        auto below = 0.0, belowWeighted = 0.0;

        for (int level = 0; level < numLevels - 1; ++level)
        {
            below += histogram[static_cast<size_t> (level)];
            belowWeighted += histogram[static_cast<size_t> (level)] * static_cast<double> (level);
            const auto above = total - below;

            if (below <= 0.0 || above <= 0.0)
                continue;  // everything on one side: not a split

            const auto difference = belowWeighted / below - (weighted - belowWeighted) / above;
            const auto separation = below * above * difference * difference;   // the between-class variance

            if (separation > best * (1.0 + 1.0e-9))
            {
                best = separation;
                firstBest = lastBest = level;
            }
            else if (separation >= best * (1.0 - 1.0e-9) && lastBest == level - 1)
            {
                lastBest = level;  // the same again, with no pixels in between
            }
        }

        if (firstBest >= 0)
            split = (firstBest + lastBest) / 2;
    }

    for (auto& v : values)
        v = levelOf (v) > split ? 1.0f : 0.0f;
}

/** Area-average weights for shrinking n source samples into m (m <= n): each destination
    sample covers n / m source samples, partial ones weighted by their overlap. */
struct BoxTap
{
    int first = 0;
    int second = 0;
    float firstWeight = 0.0f;   // share of source sample i going to destination `first`
    float secondWeight = 0.0f;  // the rest goes to `second` (only when the sample straddles two)
};

std::vector<BoxTap> boxTaps (int sourceSize, int destSize)
{
    std::vector<BoxTap> taps (static_cast<size_t> (sourceSize));
    const auto ratio = static_cast<double> (destSize) / static_cast<double> (sourceSize);  // dest per source

    for (int i = 0; i < sourceSize; ++i)
    {
        const auto start = i * ratio;
        const auto end = (i + 1) * ratio;
        auto& tap = taps[static_cast<size_t> (i)];
        tap.first = std::min (destSize - 1, static_cast<int> (std::floor (start)));
        const auto boundary = static_cast<double> (tap.first + 1);

        if (end > boundary + 1.0e-9 && tap.first + 1 < destSize)
        {
            tap.second = tap.first + 1;
            tap.firstWeight = static_cast<float> (boundary - start);
            tap.secondWeight = static_cast<float> (end - boundary);
        }
        else
        {
            tap.second = tap.first;
            tap.firstWeight = static_cast<float> (end - start);
            tap.secondWeight = 0.0f;
        }
    }

    return taps;
}

/** Shrinks a premultiplied ARGB image by area averaging (a box filter), the high-quality way to
    reduce a picture a lot: every source pixel contributes exactly its share. */
juce::Image boxDownscale (const juce::Image& source, int destWidth, int destHeight)
{
    const auto srcW = source.getWidth();
    const auto srcH = source.getHeight();
    const auto columns = boxTaps (srcW, destWidth);
    const auto rows = boxTaps (srcH, destHeight);

    // Premultiplied channels are averaged, which is correct for transparency too.
    std::vector<float> sums (static_cast<size_t> (destWidth) * static_cast<size_t> (destHeight) * 4, 0.0f);
    std::vector<float> rowSums (static_cast<size_t> (destWidth) * 4);

    const juce::Image::BitmapData src (source, juce::Image::BitmapData::readOnly);

    for (int y = 0; y < srcH; ++y)
    {
        std::fill (rowSums.begin(), rowSums.end(), 0.0f);
        const auto* line = src.getLinePointer (y);

        for (int x = 0; x < srcW; ++x)
        {
            const auto& pixel = *reinterpret_cast<const juce::PixelARGB*> (line + x * src.pixelStride);
            const float channels[] = { static_cast<float> (pixel.getAlpha()), static_cast<float> (pixel.getRed()),
                                       static_cast<float> (pixel.getGreen()), static_cast<float> (pixel.getBlue()) };
            const auto& tap = columns[static_cast<size_t> (x)];
            auto* first = rowSums.data() + static_cast<size_t> (tap.first) * 4;
            auto* second = rowSums.data() + static_cast<size_t> (tap.second) * 4;

            for (size_t c = 0; c < 4; ++c)
            {
                first[c] += channels[c] * tap.firstWeight;
                second[c] += channels[c] * tap.secondWeight;
            }
        }

        const auto& tap = rows[static_cast<size_t> (y)];
        auto* first = sums.data() + static_cast<size_t> (tap.first) * static_cast<size_t> (destWidth) * 4;
        auto* second = sums.data() + static_cast<size_t> (tap.second) * static_cast<size_t> (destWidth) * 4;

        for (size_t i = 0; i < rowSums.size(); ++i)
        {
            first[i] += rowSums[i] * tap.firstWeight;
            second[i] += rowSums[i] * tap.secondWeight;
        }
    }

    // The weights are overlaps measured in destination pixels, so each destination pixel's
    // weights add up to 1 and the sums are already averages.
    juce::Image result (juce::Image::ARGB, destWidth, destHeight, false, juce::SoftwareImageType());
    juce::Image::BitmapData dst (result, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < destHeight; ++y)
    {
        auto* line = dst.getLinePointer (y);
        const auto* sum = sums.data() + static_cast<size_t> (y) * static_cast<size_t> (destWidth) * 4;

        for (int x = 0; x < destWidth; ++x, sum += 4)
        {
            const auto toByte = [] (float v) { return static_cast<juce::uint8> (juce::jlimit (0, 255, juce::roundToInt (v))); };
            const auto a = toByte (sum[0]);

            // Rounding must not leave a colour channel above alpha (invalid premultiplied data).
            auto& pixel = *reinterpret_cast<juce::PixelARGB*> (line + x * dst.pixelStride);
            pixel.setARGB (a, std::min (a, toByte (sum[1])), std::min (a, toByte (sum[2])), std::min (a, toByte (sum[3])));
        }
    }

    return result;
}

} // namespace

juce::Image loadImageFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    return juce::ImageFileFormat::loadFrom (file);
}

juce::Image prepareImportedImage (const juce::Image& raw, int maxSide)
{
    if (! raw.isValid())
        return {};

    // A software ARGB image: plain memory on every platform, so processing is fast and identical.
    const auto argb = juce::SoftwareImageType().convert (asARGB (raw));
    maxSide = std::max (1, maxSide);

    const auto width = argb.getWidth();
    const auto height = argb.getHeight();
    const auto longest = std::max (width, height);

    // Always pixels of its own: documents rely on their pictures never changing, and the caller
    // may go on drawing into `raw`.
    if (longest <= maxSide)
        return argb.getPixelData() == raw.getPixelData() ? argb.createCopy() : argb;

    const auto scale = static_cast<double> (maxSide) / static_cast<double> (longest);
    const auto newWidth = juce::jlimit (1, maxSide, juce::roundToInt (width * scale));
    const auto newHeight = juce::jlimit (1, maxSide, juce::roundToInt (height * scale));

    return boxDownscale (argb, newWidth, newHeight);
}

bool hasMeaningfulTransparency (const juce::Image& image)
{
    if (! image.isValid() || ! image.hasAlphaChannel())
        return false;

    return transparencyIn (readLumaAlpha (image));
}

ImageMode resolveAutoMode (const juce::Image& image)
{
    if (! image.isValid())
        return ImageMode::original;

    return autoChoiceFor (readLumaAlpha (image)).mode;
}

ProcessedImage processImage (const juce::Image& image, ImageMode mode, float cleanup)
{
    ProcessedImage result;

    if (! image.isValid())
        return result;

    const auto pixels = readLumaAlpha (image);
    result.width = pixels.width;
    result.height = pixels.height;

    switch (mode)
    {
        case ImageMode::original:
            result.resolvedMode = ImageMode::original;
            result.values.resize (pixels.lum.size());

            for (size_t i = 0; i < result.values.size(); ++i)
                result.values[i] = pixels.alpha[i] * pixels.lum[i];

            break;

        case ImageMode::negative:
            result.resolvedMode = ImageMode::negative;
            result.values.resize (pixels.lum.size());

            for (size_t i = 0; i < result.values.size(); ++i)
                result.values[i] = pixels.alpha[i] * (1.0f - pixels.lum[i]);

            break;

        case ImageMode::outline:
            result.values = sobel (autoValues (pixels, autoChoiceFor (pixels)), pixels.width, pixels.height);
            result.resolvedMode = ImageMode::outline;
            break;

        case ImageMode::blackAndWhite:
        {
            // What Auto draws (levels and clean-up included), then split in two below.
            const auto choice = autoChoiceFor (pixels);
            result.values = autoValues (pixels, choice);
            result.resolvedMode = ImageMode::blackAndWhite;
            autoLevels (result.values);
            cleanUp (result.values, cleanup);
            splitInTwo (result.values, pixels.alpha, choice.drawAlpha);
            return result;
        }

        case ImageMode::automatic:
        default:
        {
            const auto choice = autoChoiceFor (pixels);
            result.values = autoValues (pixels, choice);
            result.resolvedMode = choice.mode;
            break;
        }
    }

    autoLevels (result.values);
    cleanUp (result.values, cleanup);
    return result;
}

} // namespace artfreq
