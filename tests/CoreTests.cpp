// The drawing core: frequency scale, fonts, text, pictures, the document and the compositor.

#include "TestFramework.h"

#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Fonts.h"
#include "core/FrequencyScale.h"
#include "core/ImageProcessing.h"
#include "core/TextRaster.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>

using namespace freqart;

namespace
{

const ScaleType allScaleTypes[] = { ScaleType::log, ScaleType::linear, ScaleType::mel };

FrequencyScale makeScale (ScaleType type, double minHz = 20.0, double maxHz = 20000.0)
{
    FrequencyScale scale;
    scale.type = type;
    scale.minHz = minHz;
    scale.maxHz = maxHz;
    return scale;
}

double millisecondsSince (std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
}

/** An opaque image of one colour with an optional rectangle of another. */
juce::Image makeImage (int width, int height, juce::Colour background,
                       juce::Rectangle<int> shape = {}, juce::Colour shapeColour = {})
{
    juce::Image image (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    g.fillAll (background);

    if (! shape.isEmpty())
    {
        g.setColour (shapeColour);
        g.fillRect (shape);
    }

    return image;
}

int countAbove (const juce::Image& image, juce::Rectangle<int> region, int threshold)
{
    int count = 0;
    region = region.getIntersection (image.getBounds());
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            if (data.getLinePointer (y)[x * data.pixelStride] > threshold)
                ++count;

    return count;
}

int countOutside (const juce::Image& image, juce::Rectangle<int> region, int threshold)
{
    return countAbove (image, image.getBounds(), threshold) - countAbove (image, region, threshold);
}

juce::Image singleChannel (int width, int height)
{
    return juce::Image (juce::Image::SingleChannel, width, height, true, juce::SoftwareImageType());
}

ArtObject whiteImageObject (float startTime, float endTime, float lowHz, float highHz, int size = 64)
{
    auto object = ArtDocument::makeImageObject (prepareImportedImage (makeImage (size, size, juce::Colours::white)), lowHz, highHz);
    object.imageMode = ImageMode::original;  // Auto would see a light background and invert it
    object.cleanup = 0.0f;
    object.startTime = startTime;
    object.endTime = endTime;
    return object;
}

float maxIn (const Canvas& canvas, juce::Rectangle<int> region)
{
    float result = 0.0f;
    region = region.getIntersection ({ 0, 0, Canvas::width, Canvas::height });

    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            result = std::max (result, canvas.get (x, y));

    return result;
}

float minIn (const Canvas& canvas, juce::Rectangle<int> region)
{
    float result = 1.0f;
    region = region.getIntersection ({ 0, 0, Canvas::width, Canvas::height });

    for (int y = region.getY(); y < region.getBottom(); ++y)
        for (int x = region.getX(); x < region.getRight(); ++x)
            result = std::min (result, canvas.get (x, y));

    return result;
}

uint8_t paintAt (const ArtDocument& document, int x, int y)
{
    return document.paint[static_cast<size_t> (y * ArtDocument::paintWidth + x)];
}

} // namespace

// ================================================================================================
// Frequency scale
// ================================================================================================

FREQART_TEST (scaleRoundTrips)
{
    const std::pair<double, double> ranges[] = { { 20.0, 20000.0 }, { 10.0, 24000.0 }, { 1000.0, 2000.0 }, { 100.0, 5000.0 } };

    for (const auto type : allScaleTypes)
    {
        for (const auto& range : ranges)
        {
            const auto scale = makeScale (type, range.first, range.second);

            CHECK_NEAR (scale.toNorm (range.first), 0.0, 1.0e-12);
            CHECK_NEAR (scale.toNorm (range.second), 1.0, 1.0e-12);
            CHECK_NEAR (scale.toHz (0.0), range.first, range.first * 1.0e-12);
            CHECK_NEAR (scale.toHz (1.0), range.second, range.second * 1.0e-12);

            for (double norm = -0.25; norm <= 1.25; norm += 0.05)
                CHECK_NEAR (scale.toNorm (scale.toHz (norm)), norm, 1.0e-9);

            for (const auto hz : { 1.0, 15.0, 20.0, 99.0, 1000.0, 7777.0, 20000.0, 30000.0 })
                CHECK_NEAR (scale.toHz (scale.toNorm (hz)), hz, hz * 1.0e-9);

            // Monotonic.
            double previous = -1.0e300;

            for (double hz = 1.0; hz < 40000.0; hz *= 1.1)
            {
                const auto norm = scale.toNorm (hz);
                CHECK (norm > previous);
                previous = norm;
            }
        }
    }

    // Known values.
    CHECK_NEAR (makeScale (ScaleType::log).toHz (0.5), std::sqrt (20.0 * 20000.0), 1.0e-6);
    CHECK_NEAR (makeScale (ScaleType::linear).toNorm (10010.0), 0.5, 1.0e-12);
    CHECK_NEAR (hzToMel (700.0), 2595.0 * std::log10 (2.0), 1.0e-9);
    CHECK_NEAR (hzToMel (1000.0), 1000.0, 0.1);
    CHECK_NEAR (melToHz (hzToMel (4321.0)), 4321.0, 1.0e-7);

    // 0 Hz and below: finite and far below the bottom on the log scale.
    const auto log = makeScale (ScaleType::log);
    CHECK (std::isfinite (log.toNorm (0.0)));
    CHECK (std::isfinite (log.toNorm (-50.0)));
    CHECK (log.toNorm (0.0) < -1.0);

    for (const auto type : allScaleTypes)
        CHECK (std::isfinite (makeScale (type).toNorm (-10.0)));
}

FREQART_TEST (scaleSanitised)
{
    const auto fine = makeScale (ScaleType::mel, 30.0, 18000.0);
    CHECK (fine.sanitised() == fine);

    const auto zero = makeScale (ScaleType::log, 0.0, 20000.0).sanitised();
    CHECK (zero.minHz >= 1.0);

    const auto reversed = makeScale (ScaleType::log, 20000.0, 20.0).sanitised();
    CHECK_NEAR (reversed.minHz, 20.0, 1.0e-9);
    CHECK_NEAR (reversed.maxHz, 20000.0, 1.0e-9);

    const auto narrow = makeScale (ScaleType::linear, 1000.0, 1100.0).sanitised();
    CHECK (narrow.maxHz > narrow.minHz * 1.5);

    const auto garbage = makeScale (ScaleType::log, std::nan (""), std::numeric_limits<double>::infinity()).sanitised();
    CHECK (std::isfinite (garbage.minHz) && std::isfinite (garbage.maxHz));
    CHECK (garbage.maxHz > garbage.minHz * 1.5);
    CHECK (std::isfinite (makeScale (ScaleType::log, 5.0, 5.0).toNorm (100.0)));
}

FREQART_TEST (scaleBandRanges)
{
    const auto log = makeScale (ScaleType::log);
    CHECK_NEAR (bandRange (Band::low, log).low, 40.0, 1.0e-9);
    CHECK_NEAR (bandRange (Band::low, log).high, 250.0, 1.0e-9);
    CHECK_NEAR (bandRange (Band::mid, log).low, 300.0, 1.0e-9);
    CHECK_NEAR (bandRange (Band::mid, log).high, 3000.0, 1.0e-9);
    CHECK_NEAR (bandRange (Band::high, log).low, 3500.0, 1.0e-9);
    CHECK_NEAR (bandRange (Band::high, log).high, 16000.0, 1.0e-9);
    CHECK_NEAR (bandRange (Band::full, log).low, log.toHz (0.02), 1.0e-9);
    CHECK_NEAR (bandRange (Band::full, log).high, log.toHz (0.98), 1.0e-6);

    const std::pair<double, double> ranges[] = { { 20.0, 20000.0 }, { 10.0, 24000.0 }, { 1000.0, 20000.0 },
                                                 { 20.0, 2000.0 }, { 100.0, 5000.0 }, { 500.0, 1000.0 } };

    for (const auto type : allScaleTypes)
    {
        for (const auto& range : ranges)
        {
            const auto scale = makeScale (type, range.first, range.second);

            for (const auto band : { Band::low, Band::mid, Band::high, Band::full })
            {
                const auto r = bandRange (band, scale);
                CHECK_MSG (r.low < r.high, "band " << static_cast<int> (band) << " empty on " << range.first << ".." << range.second);
                CHECK (r.low >= range.first * (1.0 - 1.0e-9));
                CHECK (r.high <= range.second * (1.0 + 1.0e-9));
                // Always a usable strip of the canvas.
                CHECK_GREATER (scale.toNorm (r.high) - scale.toNorm (r.low), 0.05);
            }
        }
    }

    // "Low" on an analyser that starts at 1 kHz still means the bottom of the canvas.
    const auto highOnly = makeScale (ScaleType::log, 1000.0, 20000.0);
    CHECK_NEAR (highOnly.toNorm (bandRange (Band::low, highOnly).low), 0.0, 1.0e-9);
}

FREQART_TEST (scaleFormatHz)
{
    CHECK (formatHz (20.0) == "20 Hz");
    CHECK (formatHz (250.0) == "250 Hz");
    CHECK (formatHz (1000.0) == "1 kHz");
    CHECK (formatHz (2500.0) == "2.5 kHz");
    CHECK (formatHz (16000.0) == "16 kHz");
    CHECK (formatHz (3527.0) == "3.5 kHz");
    CHECK (formatHz (999.7) == "1 kHz");
    CHECK (formatHz (20000.0) == "20 kHz");
    CHECK (formatHz (31.4) == "31 Hz");
    CHECK (formatHz (0.0) == "0 Hz");
    CHECK (formatHz (std::nan ("")) == "0 Hz");
}

FREQART_TEST (scaleGridFrequencies)
{
    const std::vector<double> expectedLog { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    CHECK (gridFrequencies (makeScale (ScaleType::log)) == expectedLog);

    const std::pair<double, double> ranges[] = { { 20.0, 20000.0 }, { 10.0, 24000.0 }, { 1000.0, 2000.0 },
                                                 { 100.0, 5000.0 }, { 37.0, 2345.0 } };

    for (const auto type : allScaleTypes)
    {
        for (const auto& range : ranges)
        {
            const auto scale = makeScale (type, range.first, range.second);
            const auto grid = gridFrequencies (scale);

            CHECK_MSG (grid.size() >= 3, "type " << static_cast<int> (type) << " " << range.first << ".." << range.second);
            CHECK (std::is_sorted (grid.begin(), grid.end()));
            CHECK (std::adjacent_find (grid.begin(), grid.end()) == grid.end());

            for (const auto hz : grid)
                CHECK (hz >= range.first * (1.0 - 1.0e-9) && hz <= range.second * (1.0 + 1.0e-9));

            if (type == ScaleType::linear)
                CHECK (grid.size() >= 5 && grid.size() <= 10);

            if (type == ScaleType::mel)
                for (size_t i = 1; i < grid.size(); ++i)
                    CHECK_GREATER (scale.toNorm (grid[i]) - scale.toNorm (grid[i - 1]), 0.04);
        }
    }
}

// ================================================================================================
// Fonts and text
// ================================================================================================

FREQART_TEST (fontsAreEmbedded)
{
    CHECK (fonts::numArtFonts() == 5);
    CHECK (fonts::artFontName (0) == "Inter Bold");
    CHECK (fonts::artFontName (4) == "DM Mono");
    CHECK (fonts::artFontName (99) == "Inter Bold");

    for (int i = -1; i <= fonts::numArtFonts(); ++i)
        CHECK (fonts::artTypeface (i) != nullptr);

    CHECK (fonts::artTypeface (0)->getName().containsIgnoreCase ("Inter"));
    CHECK (fonts::artTypeface (1)->getName().containsIgnoreCase ("Space Grotesk"));
    CHECK (fonts::artTypeface (3)->getName().containsIgnoreCase ("Fraunces"));
    CHECK (fonts::mono (13.0f).getTypefacePtr()->getName().containsIgnoreCase ("DM Mono"));
    CHECK (fonts::ui (13.0f, fonts::UiWeight::semiBold).getTypefacePtr()->getName().containsIgnoreCase ("Inter"));
    CHECK_GREATER (fonts::ui (13.0f).getHeight(), 5.0f);
    CHECK_GREATER (fonts::art (2, 40.0f).getHeight(), 20.0f);
}

FREQART_TEST (textPathShape)
{
    CHECK (makeTextPath ("", 0).isEmpty());
    CHECK (makeTextPath ("  \n\t ", 0).isEmpty());

    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        const auto path = makeTextPath ("FreqArt", font);
        CHECK (! path.isEmpty());

        const auto bounds = path.getBounds();
        CHECK_NEAR (bounds.getX(), 0.0f, 1.0e-3f);
        CHECK_NEAR (bounds.getY(), 0.0f, 1.0e-3f);
        CHECK_GREATER (bounds.getWidth(), bounds.getHeight());
    }

    // Two lines are about twice as tall and as wide as the longer line, centred on each other.
    const auto one = makeTextPath ("HHHH", 0).getBounds();
    const auto two = makeTextPath ("HHHH\nHH", 0).getBounds();
    CHECK_GREATER (two.getHeight(), one.getHeight() * 1.8f);
    CHECK_NEAR (two.getWidth(), one.getWidth(), one.getWidth() * 0.02f);

    // The short line is centred under the long one: rasterise and find its ink in the lower rows.
    const auto twoLines = makeTextPath ("HHHH\nHH", 0);
    const auto scale = 0.25f;
    const auto width = juce::roundToInt (two.getWidth() * scale) + 2;
    const auto height = juce::roundToInt (two.getHeight() * scale) + 2;
    auto image = singleChannel (width, height);
    {
        juce::Graphics g (image);
        g.setColour (juce::Colours::white);
        g.fillPath (twoLines, juce::AffineTransform::scale (scale).translated (1.0f, 1.0f));
    }

    int left = width, right = -1;
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

    for (int y = height * 7 / 10; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (data.getLinePointer (y)[x * data.pixelStride] > 128)
            {
                left = std::min (left, x);
                right = std::max (right, x);
            }

    CHECK_NEAR ((left + right) * 0.5, width * 0.5, width * 0.03);
    CHECK_NEAR (right - left, width * 0.5, width * 0.1);
}

FREQART_TEST (textRenderCoverage)
{
    const juce::Rectangle<float> area (50.0f, 40.0f, 300.0f, 100.0f);
    const auto areaInt = area.toNearestInt();

    auto solid = singleChannel (400, 200);
    renderTextInto (solid, area, "FreqArt", 0, TextStyle::solid);

    const auto lit = countAbove (solid, areaInt, 0);
    const auto areaPixels = areaInt.getWidth() * areaInt.getHeight();
    CHECK_GREATER (lit, areaPixels / 5);
    CHECK_LESS (lit, areaPixels * 4 / 5);
    CHECK (countOutside (solid, areaInt, 0) == 0);
    CHECK_GREATER (countAbove (solid, areaInt, 254), lit / 2);  // solid letters are fully on

    // The letters are stretched to the box, inset by 4 % of its height.
    const auto inset = juce::roundToInt (area.getHeight() * 0.04f);
    const auto inner = areaInt.reduced (inset);
    CHECK (countAbove (solid, inner.withWidth (3), 128) > 0);
    CHECK (countAbove (solid, inner.withTrimmedLeft (inner.getWidth() - 3), 128) > 0);
    CHECK (countAbove (solid, inner.withHeight (3), 128) > 0);
    CHECK (countAbove (solid, inner.withTrimmedTop (inner.getHeight() - 3), 128) > 0);
    CHECK (countAbove (solid, areaInt.withTrimmedLeft (areaInt.getWidth() - inset + 1), 0) == 0);

    auto outline = singleChannel (400, 200);
    renderTextInto (outline, area, "FreqArt", 0, TextStyle::outline);
    const auto outlineLit = countAbove (outline, areaInt, 127);
    CHECK_GREATER (outlineLit, areaPixels / 20);
    CHECK_LESS (outlineLit, countAbove (solid, areaInt, 127));
    CHECK (countOutside (outline, areaInt, 0) == 0);

    // Every font draws something sensible.
    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        auto image = singleChannel (400, 200);
        renderTextInto (image, area, "Hello", font, TextStyle::solid);
        CHECK_GREATER (countAbove (image, areaInt, 127), areaPixels / 6);
        CHECK (countOutside (image, areaInt, 0) == 0);
    }

    // Empty text and empty areas draw nothing.
    auto blank = singleChannel (400, 200);
    renderTextInto (blank, area, "   ", 0, TextStyle::solid);
    renderTextInto (blank, {}, "Hello", 0, TextStyle::solid);
    CHECK (countAbove (blank, blank.getBounds(), 0) == 0);
}

FREQART_TEST (textRenderMaxCombinesAndClips)
{
    auto image = singleChannel (200, 100);
    image.clear (image.getBounds(), juce::Colour::fromRGBA (0, 0, 0, 100));
    CHECK (countAbove (image, image.getBounds(), 99) == 200 * 100);

    renderTextInto (image, { 10.0f, 10.0f, 180.0f, 80.0f }, "OK", 0, TextStyle::solid);

    // Nothing went below what was there, letters reached full value.
    CHECK (countAbove (image, image.getBounds(), 99) == 200 * 100);
    CHECK_GREATER (countAbove (image, image.getBounds(), 254), 500);

    // An area hanging off the image is clipped, not refused.
    auto clipped = singleChannel (200, 100);
    renderTextInto (clipped, { -100.0f, 50.0f, 300.0f, 100.0f }, "WIDE TEXT", 0, TextStyle::outline);
    CHECK_GREATER (countAbove (clipped, clipped.getBounds(), 127), 100);
    CHECK (countAbove (clipped, { 0, 0, 200, 50 }, 0) == 0);
}

FREQART_TEST (textRenderTiming)
{
    auto target = singleChannel (Canvas::width, Canvas::height);
    const juce::Rectangle<float> area (0.0f, 300.0f, 1024.0f, 340.0f);
    renderTextInto (target, area, "FreqArt", 0, TextStyle::solid);  // warm up (font loading)

    constexpr int runs = 10;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < runs; ++i)
        renderTextInto (target, area, "FreqArt", i % 2, i % 3 == 0 ? TextStyle::outline : TextStyle::solid);

    const auto ms = millisecondsSince (start) / runs;
    std::printf ("    renderTextInto 1024 x 340: %.2f ms (budget 5 ms)\n", ms);
    CHECK_LESS (ms, 4 * 5.0);
}

// ================================================================================================
// Pictures
// ================================================================================================

FREQART_TEST (imageWhiteBackgroundBecomesNegative)
{
    auto image = makeImage (100, 80, juce::Colours::white);
    {
        juce::Graphics g (image);
        g.setColour (juce::Colours::black);
        g.fillEllipse (30.0f, 20.0f, 40.0f, 40.0f);
    }

    CHECK (! hasMeaningfulTransparency (image));
    CHECK (resolveAutoMode (image) == ImageMode::negative);

    const auto processed = processImage (image, ImageMode::automatic, 0.0f);
    CHECK (processed.isValid());
    CHECK (processed.width == 100 && processed.height == 80);
    CHECK (processed.resolvedMode == ImageMode::negative);
    CHECK_NEAR (processed.at (50, 40), 1.0f, 1.0e-3f);
    CHECK_NEAR (processed.at (2, 2), 0.0f, 1.0e-3f);

    // The modes themselves.
    const auto original = processImage (image, ImageMode::original, 0.0f);
    CHECK (original.resolvedMode == ImageMode::original);
    CHECK_NEAR (original.at (50, 40), 0.0f, 1.0e-3f);
    CHECK_NEAR (original.at (2, 2), 1.0f, 1.0e-3f);

    const auto negative = processImage (image, ImageMode::negative, 0.0f);
    CHECK (negative.resolvedMode == ImageMode::negative);
    CHECK_NEAR (negative.at (50, 40), 1.0f, 1.0e-3f);
}

FREQART_TEST (imageDarkBackgroundStaysOriginal)
{
    const auto image = makeImage (120, 90, juce::Colour (0xff101010), { 40, 30, 40, 30 }, juce::Colours::white);
    CHECK (resolveAutoMode (image) == ImageMode::original);

    const auto processed = processImage (image, ImageMode::automatic, 0.0f);
    CHECK (processed.resolvedMode == ImageMode::original);
    CHECK_NEAR (processed.at (60, 45), 1.0f, 1.0e-3f);
    CHECK_LESS (processed.at (3, 3), 0.08f);

    // Rec. 709 weights: pure green is much brighter than pure blue.
    const auto green = processImage (makeImage (50, 50, juce::Colours::black, { 0, 0, 25, 50 }, juce::Colour (0xff00ff00)), ImageMode::original, 0.0f);
    const auto blue = processImage (makeImage (50, 50, juce::Colours::black, { 0, 0, 25, 50 }, juce::Colour (0xff0000ff)), ImageMode::original, 0.0f);
    CHECK_NEAR (green.at (10, 10), 1.0f, 1.0e-3f);  // auto-levels lift the brightest to 1
    const auto blueRaw = processImage (makeImage (50, 50, juce::Colours::white, { 0, 0, 25, 50 }, juce::Colour (0xff0000ff)), ImageMode::original, 0.0f);
    CHECK_NEAR (blueRaw.at (10, 10), 0.0722f, 0.01f);
    CHECK_NEAR (blue.at (40, 10), 0.0f, 1.0e-6f);
}

FREQART_TEST (imageTransparentPngUsesAlpha)
{
    // A black logo on a transparent background: its shape (alpha) is what should sound.
    juce::Image logo (juce::Image::ARGB, 64, 64, true, juce::SoftwareImageType());
    {
        juce::Graphics g (logo);
        g.setColour (juce::Colours::black);
        g.fillRect (16, 16, 32, 32);
    }

    // Through a PNG file's bytes, as a user would bring it in.
    juce::MemoryOutputStream png;
    CHECK (juce::PNGImageFormat().writeImageToStream (logo, png));
    const auto loaded = prepareImportedImage (juce::ImageFileFormat::loadFrom (png.getData(), png.getDataSize()));
    CHECK (loaded.isValid());

    CHECK (hasMeaningfulTransparency (loaded));
    CHECK (resolveAutoMode (loaded) == ImageMode::original);

    const auto processed = processImage (loaded, ImageMode::automatic, 0.0f);
    CHECK (processed.resolvedMode == ImageMode::original);
    CHECK_NEAR (processed.at (32, 32), 1.0f, 1.0e-3f);
    CHECK_NEAR (processed.at (4, 4), 0.0f, 1.0e-6f);

    // Original mode on the same picture: a black shape is silent.
    CHECK_NEAR (processImage (loaded, ImageMode::original, 0.0f).at (32, 32), 0.0f, 1.0e-3f);

    // Fewer than 2 % transparent pixels do not count.
    auto almostOpaque = makeImage (100, 100, juce::Colours::white);
    almostOpaque.clear ({ 0, 0, 10, 10 });  // 1 %
    CHECK (! hasMeaningfulTransparency (almostOpaque));
    CHECK (! hasMeaningfulTransparency (makeImage (10, 10, juce::Colours::red).convertedToFormat (juce::Image::RGB)));
}

FREQART_TEST (imageOutlineDrawsEdgesOnly)
{
    const juce::Rectangle<int> square (30, 30, 40, 40);
    const auto image = makeImage (100, 100, juce::Colours::black, square, juce::Colours::white);
    const auto outline = processImage (image, ImageMode::outline, 0.0f);
    const auto filled = processImage (image, ImageMode::original, 0.0f);

    CHECK (outline.resolvedMode == ImageMode::outline);
    CHECK_NEAR (outline.at (50, 50), 0.0f, 1.0e-6f);  // inside the square: flat
    CHECK_NEAR (outline.at (5, 5), 0.0f, 1.0e-6f);    // background: flat
    CHECK_GREATER (outline.at (30, 50), 0.5f);         // left edge
    CHECK_GREATER (outline.at (69, 50), 0.5f);         // right edge
    CHECK_GREATER (outline.at (50, 29), 0.5f);         // top edge (just outside)

    const auto lit = [] (const ProcessedImage& p)
    {
        return std::count_if (p.values.begin(), p.values.end(), [] (float v) { return v > 0.1f; });
    };

    CHECK_LESS (lit (outline), lit (filled) / 2);

    // Outline follows Auto: on a white page the dark shape's edges are drawn, the frame is not.
    const auto page = makeImage (100, 100, juce::Colours::white, square, juce::Colours::black);
    const auto pageOutline = processImage (page, ImageMode::outline, 0.0f);
    CHECK_GREATER (pageOutline.at (30, 50), 0.5f);
    CHECK_NEAR (pageOutline.at (0, 50), 0.0f, 1.0e-6f);
}

FREQART_TEST (imageCleanupAndLevels)
{
    // A bright square on a dark background with faint grain.
    auto image = makeImage (100, 100, juce::Colours::black, { 20, 20, 30, 30 }, juce::Colours::white);
    juce::Random random (42);

    for (int y = 60; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            image.setPixelAt (x, y, juce::Colour::greyLevel (0.02f + 0.04f * random.nextFloat()));

    const auto raw = processImage (image, ImageMode::original, 0.0f);
    CHECK_GREATER (raw.at (50, 80), 0.01f);
    CHECK_NEAR (raw.at (30, 30), 1.0f, 1.0e-3f);

    const auto cleaned = processImage (image, ImageMode::original, 0.08f);
    int noisy = 0;

    for (int y = 60; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            if (cleaned.at (x, y) > 0.0f)
                ++noisy;

    CHECK (noisy == 0);
    CHECK_NEAR (cleaned.at (30, 30), 1.0f, 1.0e-3f);

    // The clean-up formula, and its clamp to 0.9.
    const auto grey = makeImage (100, 100, juce::Colours::black, { 0, 0, 50, 100 }, juce::Colour::greyLevel (0.5f));
    const auto half = processImage (grey, ImageMode::original, 0.0f);  // levelled: grey becomes 1
    CHECK_NEAR (half.at (10, 10), 1.0f, 1.0e-3f);

    const auto gradient = [] ()
    {
        juce::Image g (juce::Image::ARGB, 256, 4, true, juce::SoftwareImageType());

        for (int x = 0; x < 256; ++x)
            for (int y = 0; y < 4; ++y)
                g.setPixelAt (x, y, juce::Colour (static_cast<juce::uint8> (x), static_cast<juce::uint8> (x), static_cast<juce::uint8> (x)));

        return g;
    }();

    const auto levelled = processImage (gradient, ImageMode::original, 0.0f);
    const auto cleanedHalf = processImage (gradient, ImageMode::original, 0.5f);

    for (int x = 0; x < 256; x += 17)
    {
        const auto v = levelled.at (x, 1);
        CHECK_NEAR (cleanedHalf.at (x, 1), std::max (0.0f, (v - 0.5f) / 0.5f), 1.0e-4f);
    }

    const auto over = processImage (gradient, ImageMode::original, 5.0f);
    CHECK_NEAR (over.at (250, 1), std::max (0.0f, (levelled.at (250, 1) - 0.9f) / 0.1f), 1.0e-3f);

    // Auto-levels: the 99th percentile becomes 1.
    std::vector<float> sorted (levelled.values);
    std::sort (sorted.begin(), sorted.end());
    CHECK_NEAR (sorted[static_cast<size_t> (std::ceil (0.99 * static_cast<double> (sorted.size()))) - 1], 1.0f, 1.0e-4f);

    // An essentially empty (very dark) picture is not blown up.
    const auto dark = processImage (makeImage (50, 50, juce::Colour::greyLevel (0.01f)), ImageMode::original, 0.0f);
    CHECK_LESS (dark.at (25, 25), 0.02f);
    CHECK (! processImage ({}, ImageMode::automatic, 0.0f).isValid());
}

FREQART_TEST (imagePrepareAndLoad)
{
    CHECK (! prepareImportedImage ({}).isValid());
    CHECK (! loadImageFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("freqart-no-such-file.png")).isValid());

    const auto big = prepareImportedImage (makeImage (3000, 1500, juce::Colours::red));
    CHECK (big.getWidth() == 1024 && big.getHeight() == 512);
    CHECK (big.getFormat() == juce::Image::ARGB);
    CHECK (big.getPixelAt (500, 300).getRed() == 255 && big.getPixelAt (500, 300).getGreen() == 0);

    const auto tall = prepareImportedImage (makeImage (300, 2000, juce::Colours::blue), 500);
    CHECK (tall.getHeight() == 500 && tall.getWidth() == 75);

    const auto small = prepareImportedImage (makeImage (200, 100, juce::Colours::green).convertedToFormat (juce::Image::RGB));
    CHECK (small.getWidth() == 200 && small.getHeight() == 100);
    CHECK (small.getFormat() == juce::Image::ARGB);

    // Area averaging: a one-pixel checkerboard halved becomes an even grey, not a pattern.
    juce::Image checker (juce::Image::ARGB, 2048, 2048, true, juce::SoftwareImageType());
    {
        juce::Image::BitmapData data (checker, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < 2048; ++y)
            for (int x = 0; x < 2048; ++x)
                data.setPixelColour (x, y, ((x + y) & 1) != 0 ? juce::Colours::white : juce::Colours::black);
    }

    const auto halved = prepareImportedImage (checker);
    CHECK (halved.getWidth() == 1024);

    for (const auto& p : { juce::Point<int> (0, 0), juce::Point<int> (511, 700), juce::Point<int> (1023, 1023) })
        CHECK_NEAR (static_cast<int> (halved.getPixelAt (p.x, p.y).getBrightness() * 255.0f), 128, 2);

    // A real file on disk.
    const auto file = juce::File::createTempFile (".png");
    {
        juce::FileOutputStream out (file);
        CHECK (juce::PNGImageFormat().writeImageToStream (makeImage (30, 20, juce::Colours::orange), out));
    }
    const auto fromDisk = loadImageFile (file);
    CHECK (fromDisk.isValid() && fromDisk.getWidth() == 30 && fromDisk.getHeight() == 20);
    file.deleteFile();
}

// ================================================================================================
// Document and history
// ================================================================================================

FREQART_TEST (documentDefaults)
{
    const ArtDocument empty;
    CHECK (empty.isEmpty());
    CHECK (! empty.hasPaint());
    CHECK (empty.paint.size() == static_cast<size_t> (ArtDocument::paintWidth * ArtDocument::paintHeight));

    const auto document = ArtDocument::createDefault();
    CHECK (! document.isEmpty());
    CHECK (document.objects.size() == 1);

    const auto& text = document.objects.front();
    CHECK (text.type == ArtObject::Type::text);
    CHECK (text.text == "FreqArt");
    CHECK (text.fontIndex == 0);
    CHECK (text.textStyle == TextStyle::solid);
    CHECK_NEAR (text.lowHz, 300.0f, 1.0e-3f);
    CHECK_NEAR (text.highHz, 3000.0f, 1.0e-3f);
    CHECK_NEAR (text.startTime, 0.04f, 1.0e-6f);
    CHECK_NEAR (text.endTime, 0.96f, 1.0e-6f);
    CHECK_NEAR (text.intensity, 1.0f, 1.0e-6f);
    CHECK (! text.id.isNull());

    // Lookup and removal by id.
    auto copy = document;
    CHECK (copy.find (text.id) != nullptr);
    CHECK (copy.find (juce::Uuid()) == nullptr);
    CHECK (copy.remove (text.id));
    CHECK (! copy.remove (text.id));
    CHECK (copy.isEmpty());

    const auto made = ArtDocument::makeTextObject ("Hi", 5000.0f, 1000.0f);
    CHECK (made.lowHz < made.highHz);
    CHECK (made.id != text.id);
}

FREQART_TEST (documentPaintStroke)
{
    // A single hard dab: full inside the radius, nothing outside. Pixel centres are measured.
    ArtDocument document;
    document.paintStroke ({ { 100.5f, 100.5f }, { 100.5f, 100.5f }, 8.0f, 8.0f, 0.0f, 1.0f, false });
    CHECK (paintAt (document, 100, 100) == 255);
    CHECK (paintAt (document, 107, 100) == 255);
    CHECK (paintAt (document, 109, 100) == 0);
    CHECK (paintAt (document, 100, 93) == 255);
    CHECK (paintAt (document, 100, 91) == 0);
    CHECK (document.hasPaint());

    // Strength scales the value; painting keeps the maximum.
    ArtDocument weak;
    weak.paintStroke ({ { 50.5f, 50.5f }, { 50.5f, 50.5f }, 4.0f, 4.0f, 0.0f, 0.6f, false });
    CHECK (paintAt (weak, 50, 50) == 153);
    weak.paintStroke ({ { 50.5f, 50.5f }, { 50.5f, 50.5f }, 4.0f, 4.0f, 0.0f, 0.3f, false });
    CHECK (paintAt (weak, 50, 50) == 153);

    // Fully soft: smoothstep from the centre; half way out is half the value.
    ArtDocument soft;
    soft.paintStroke ({ { 200.5f, 200.5f }, { 200.5f, 200.5f }, 8.0f, 8.0f, 1.0f, 1.0f, false });
    CHECK (paintAt (soft, 200, 200) == 255);
    CHECK_NEAR (static_cast<int> (paintAt (soft, 204, 200)), 128, 1);
    CHECK_NEAR (static_cast<int> (paintAt (soft, 202, 200)), juce::roundToInt (255.0f * (1.0f - 0.25f * 0.25f * (3.0f - 0.5f))), 1);
    CHECK (paintAt (soft, 208, 200) == 0);

    // Half soft: hard core to half the radius.
    ArtDocument halfSoft;
    halfSoft.paintStroke ({ { 300.5f, 300.5f }, { 300.5f, 300.5f }, 8.0f, 8.0f, 0.5f, 1.0f, false });
    CHECK (paintAt (halfSoft, 303, 300) == 255);
    CHECK_NEAR (static_cast<int> (paintAt (halfSoft, 306, 300)), 128, 1);  // t = 0.75: half way through the falloff

    // Erasing: hard erase clears, soft erase scales by (1 - profile).
    ArtDocument erased;
    erased.paintStroke ({ { 400.5f, 200.5f }, { 600.5f, 200.5f }, 30.0f, 30.0f, 0.0f, 1.0f, false });
    erased.paintStroke ({ { 500.5f, 200.5f }, { 500.5f, 200.5f }, 8.0f, 8.0f, 0.0f, 1.0f, true });
    CHECK (paintAt (erased, 500, 200) == 0);
    CHECK (paintAt (erased, 510, 200) == 255);
    erased.paintStroke ({ { 450.5f, 200.5f }, { 450.5f, 200.5f }, 8.0f, 8.0f, 1.0f, 1.0f, true });
    CHECK (paintAt (erased, 450, 200) == 0);
    CHECK_NEAR (static_cast<int> (paintAt (erased, 454, 200)), 128, 1);
    CHECK (paintAt (erased, 459, 200) == 255);

    // A stroke covers its whole path; elliptical radii are honoured.
    ArtDocument line;
    line.paintStroke ({ { 100.5f, 100.5f }, { 300.5f, 100.5f }, 4.0f, 4.0f, 0.0f, 1.0f, false });

    for (int x = 100; x <= 300; ++x)
        CHECK (paintAt (line, x, 100) == 255);

    CHECK (paintAt (line, 350, 100) == 0);

    ArtDocument ellipse;
    ellipse.paintStroke ({ { 100.5f, 100.5f }, { 100.5f, 100.5f }, 16.0f, 4.0f, 0.0f, 1.0f, false });
    CHECK (paintAt (ellipse, 114, 100) == 255);
    CHECK (paintAt (ellipse, 100, 106) == 0);

    // Off the edges: clipped, no crash; garbage is ignored.
    ArtDocument edge;
    edge.paintStroke ({ { -5.0f, -5.0f }, { 2000.0f, 900.0f }, 10.0f, 10.0f, 0.2f, 1.0f, false });
    CHECK (paintAt (edge, 0, 0) == 255);
    edge.paintStroke ({ { std::nanf (""), 0.0f }, { 3.0f, 3.0f }, 10.0f, 10.0f, 0.2f, 1.0f, false });

    edge.clearPaint();
    CHECK (! edge.hasPaint());
}

FREQART_TEST (documentRoundTrip)
{
    ArtDocument document;

    auto text = ArtDocument::makeTextObject ("Hello\nWorld", 3500.0f, 16000.0f);
    text.startTime = 0.1f;
    text.endTime = 0.7f;
    text.intensity = 0.65f;
    text.fontIndex = 3;
    text.textStyle = TextStyle::outline;
    document.objects.push_back (text);

    auto picture = makeImage (40, 30, juce::Colours::black, { 5, 5, 10, 10 }, juce::Colour (0xff336699));
    picture.clear ({ 30, 0, 10, 30 });  // a transparent strip
    auto image = ArtDocument::makeImageObject (prepareImportedImage (picture), 40.0f, 250.0f);
    image.imageMode = ImageMode::negative;
    image.cleanup = 0.3f;
    image.startTime = 0.5f;
    image.endTime = 0.9f;
    document.objects.push_back (image);

    document.paintStroke ({ { 10.0f, 10.0f }, { 900.0f, 400.0f }, 12.0f, 6.0f, 0.4f, 0.8f, false });

    // Through XML text, as the plug-in state travels.
    const auto xml = document.toValueTree().createXml();
    CHECK (xml != nullptr);
    const auto tree = juce::ValueTree::fromXml (*xml);
    CHECK (tree.hasType ("FreqArtDocument"));
    CHECK (static_cast<int> (tree["version"]) == 1);

    const auto loaded = ArtDocument::fromValueTree (tree);
    CHECK (loaded.paint == document.paint);
    CHECK (loaded.objects.size() == 2);

    if (loaded.objects.size() == 2)
    {
        const auto& t = loaded.objects[0];
        CHECK (t.id == text.id);
        CHECK (t.type == ArtObject::Type::text);
        CHECK (t.text == "Hello\nWorld");
        CHECK_NEAR (t.startTime, 0.1f, 1.0e-6f);
        CHECK_NEAR (t.endTime, 0.7f, 1.0e-6f);
        CHECK_NEAR (t.lowHz, 3500.0f, 1.0e-3f);
        CHECK_NEAR (t.highHz, 16000.0f, 1.0e-3f);
        CHECK_NEAR (t.intensity, 0.65f, 1.0e-6f);
        CHECK (t.fontIndex == 3);
        CHECK (t.textStyle == TextStyle::outline);

        const auto& i = loaded.objects[1];
        CHECK (i.id == image.id);
        CHECK (i.type == ArtObject::Type::image);
        CHECK (i.imageMode == ImageMode::negative);
        CHECK_NEAR (i.cleanup, 0.3f, 1.0e-6f);
        CHECK_NEAR (i.lowHz, 40.0f, 1.0e-4f);
        CHECK (i.image.getWidth() == 40 && i.image.getHeight() == 30);
        CHECK (i.image.getFormat() == juce::Image::ARGB);

        int different = 0;

        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 40; ++x)
                if (i.image.getPixelAt (x, y) != image.image.getPixelAt (x, y))
                    ++different;

        CHECK (different == 0);
    }

    // Saving twice gives the same text (pictures are encoded once and remembered); a different
    // picture of the same size gives a different encoding.
    const auto again = document.toValueTree();
    CHECK (again.getChild (1)["image"].toString() == tree.getChild (1)["image"].toString());
    CHECK (again["paint"].toString() == tree["paint"].toString());

    auto other = document;
    other.objects[1].image = prepareImportedImage (makeImage (40, 30, juce::Colours::white));
    CHECK (other.toValueTree().getChild (1)["image"].toString() != tree.getChild (1)["image"].toString());
    CHECK (document.toValueTree().getChild (1)["image"].toString() == tree.getChild (1)["image"].toString());

    // An empty document has no paint property at all.
    CHECK (! ArtDocument().toValueTree().hasProperty ("paint"));
    CHECK (ArtDocument::fromValueTree (ArtDocument().toValueTree()).isEmpty());
}

FREQART_TEST (documentToleratesGarbage)
{
    CHECK (ArtDocument::fromValueTree ({}).isEmpty());
    CHECK (ArtDocument::fromValueTree (juce::ValueTree ("SomethingElse")).isEmpty());

    juce::ValueTree tree ("FreqArtDocument");
    tree.setProperty ("version", 99, nullptr);
    tree.setProperty ("paint", "!!! not base64 !!!", nullptr);

    const auto addObject = [&tree] (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        juce::ValueTree child ("Object");

        for (const auto& p : properties)
            child.setProperty (p.first, p.second, nullptr);

        tree.appendChild (child, nullptr);
    };

    const juce::Uuid sharedId;

    addObject ({ { "type", "banana" } });                                          // dropped
    addObject ({ { "type", 1 }, { "image", "AAAA" } });                            // unreadable picture: dropped
    addObject ({ { "type", 1 } });                                                 // no picture: dropped
    addObject ({ { "type", 0 }, { "id", sharedId.toString() }, { "text", "ok" },
                 { "startTime", 0.9 }, { "endTime", 0.2 }, { "lowHz", -50.0 }, { "highHz", "abc" },
                 { "intensity", 7.0 }, { "fontIndex", 99 }, { "textStyle", 7 } });
    addObject ({ { "type", "text" }, { "id", sharedId.toString() }, { "startTime", "nonsense" },
                 { "lowHz", 5000.0 }, { "highHz", 5000.0 }, { "intensity", -1.0 }, { "fontIndex", -4 } });
    tree.appendChild (juce::ValueTree ("NotAnObject"), nullptr);

    const auto document = ArtDocument::fromValueTree (tree);
    CHECK (! document.hasPaint());
    CHECK (document.paint.size() == static_cast<size_t> (ArtDocument::paintWidth * ArtDocument::paintHeight));
    CHECK (document.objects.size() == 2);

    if (document.objects.size() == 2)
    {
        const auto& a = document.objects[0];
        CHECK (a.text == "ok");
        CHECK (a.startTime < a.endTime);
        CHECK_NEAR (a.startTime, 0.2f, 1.0e-6f);
        CHECK (a.lowHz >= 1.0f && a.lowHz < a.highHz);
        CHECK_NEAR (a.intensity, 1.0f, 1.0e-6f);
        CHECK (a.fontIndex == fonts::numArtFonts() - 1);
        CHECK (a.textStyle == TextStyle::outline);

        const auto& b = document.objects[1];
        CHECK (b.id != a.id);  // duplicate ids are replaced
        CHECK (b.startTime < b.endTime);
        CHECK (b.lowHz < b.highHz);
        CHECK_NEAR (b.intensity, 0.0f, 1.0e-6f);
        CHECK (b.fontIndex == 0);
    }

    // Paint that decodes but is the wrong size is ignored.
    juce::MemoryOutputStream compressed;
    {
        juce::GZIPCompressorOutputStream zip (compressed, 9);
        const std::vector<uint8_t> tooShort (1000, 200);
        zip.write (tooShort.data(), tooShort.size());
    }
    juce::ValueTree wrongSize ("FreqArtDocument");
    wrongSize.setProperty ("paint", juce::Base64::toBase64 (compressed.getData(), compressed.getDataSize()), nullptr);
    CHECK (! ArtDocument::fromValueTree (wrongSize).hasPaint());

    // Out-of-range picture settings are clamped.
    juce::ValueTree withImage ("FreqArtDocument");
    auto good = ArtDocument::makeImageObject (prepareImportedImage (makeImage (8, 8, juce::Colours::white)), 100.0f, 200.0f);
    ArtDocument holder;
    holder.objects.push_back (good);
    auto child = holder.toValueTree().getChild (0).createCopy();
    child.setProperty ("imageMode", -3, nullptr);
    child.setProperty ("cleanup", 5.0, nullptr);
    withImage.appendChild (child, nullptr);
    const auto clamped = ArtDocument::fromValueTree (withImage);
    CHECK (clamped.objects.size() == 1);

    if (clamped.objects.size() == 1)
    {
        CHECK (clamped.objects[0].imageMode == ImageMode::automatic);
        CHECK_NEAR (clamped.objects[0].cleanup, 0.9f, 1.0e-6f);
    }
}

FREQART_TEST (documentPaintScale)
{
    // The paint layer remembers the scale it was painted on, through a save and a load.
    ArtDocument document;
    CHECK (document.paintScale == FrequencyScale {});
    document.paintStroke ({ { 100.5f, 100.5f }, { 300.5f, 100.5f }, 4.0f, 4.0f, 0.0f, 1.0f, false });
    document.paintScale = makeScale (ScaleType::mel, 40.0, 16000.0);

    const auto xml = document.toValueTree().createXml();
    CHECK (xml != nullptr);
    const auto tree = juce::ValueTree::fromXml (*xml);
    const auto loaded = ArtDocument::fromValueTree (tree);
    CHECK (loaded.paint == document.paint);
    CHECK (loaded.paintScale == document.paintScale);

    // Documents saved before the paint was tagged were painted on the default scale.
    auto old = tree.createCopy();

    for (const auto* name : { "paintScale", "paintMinHz", "paintMaxHz" })
        old.removeProperty (name, nullptr);

    const auto fromOld = ArtDocument::fromValueTree (old);
    CHECK (fromOld.paint == document.paint);
    CHECK (fromOld.paintScale == FrequencyScale {});

    // Nonsense becomes a usable scale.
    auto garbage = tree.createCopy();
    garbage.setProperty ("paintScale", 7, nullptr);
    garbage.setProperty ("paintMinHz", "abc", nullptr);
    garbage.setProperty ("paintMaxHz", -5.0, nullptr);
    const auto fromGarbage = ArtDocument::fromValueTree (garbage).paintScale;
    CHECK (fromGarbage == fromGarbage.sanitised());
    CHECK (fromGarbage.type == ScaleType::mel);

    // An empty layer carries no tag, and clearing the paint (Clear, Reset) drops it.
    CHECK (! ArtDocument().toValueTree().hasProperty ("paintScale"));
    CHECK (ArtDocument::createDefault().paintScale == FrequencyScale {});

    auto cleared = document;
    cleared.clearPaint();
    CHECK (! cleared.hasPaint());
    CHECK (cleared.paintScale == FrequencyScale {});
}

FREQART_TEST (documentHistory)
{
    DocumentHistory history (3);
    ArtDocument current;
    CHECK (! history.canUndo() && ! history.canRedo());
    CHECK (! history.undo (current));

    // Edits: each push records the state before it.
    const auto withText = [] (const juce::String& text)
    {
        ArtDocument d;
        d.objects.push_back (ArtDocument::makeTextObject (text, 100.0f, 200.0f));
        return d;
    };

    std::vector<ArtDocument> states { ArtDocument(), withText ("1"), withText ("2"), withText ("3"), withText ("4") };

    for (size_t i = 1; i < states.size(); ++i)
    {
        history.push (current);
        current = states[i];
    }

    // Limit 3: the oldest step (the empty document) has gone.
    CHECK (history.undo (current) && current.objects.front().text == "3");
    CHECK (history.undo (current) && current.objects.front().text == "2");
    CHECK (history.undo (current) && current.objects.front().text == "1");
    CHECK (! history.undo (current));
    CHECK (current.objects.front().text == "1");

    CHECK (history.redo (current) && current.objects.front().text == "2");
    CHECK (history.canUndo() && history.canRedo());

    // A new edit clears the redo steps.
    history.push (current);
    current = withText ("new");
    CHECK (! history.canRedo());
    CHECK (! history.redo (current));
    CHECK (history.undo (current) && current.objects.front().text == "2");

    history.clear();
    CHECK (! history.canUndo() && ! history.canRedo());
}

// ================================================================================================
// Compositor
// ================================================================================================

FREQART_TEST (compositorPlacesObjectsByHz)
{
    for (const auto type : allScaleTypes)
    {
        const auto scale = makeScale (type);
        auto object = whiteImageObject (0.25f, 0.5f, 1000.0f, 4000.0f);

        ArtDocument document;
        document.objects.push_back (object);

        Compositor compositor;
        Canvas canvas;
        compositor.render (document, scale, canvas);

        const auto top = (1.0 - scale.toNorm (4000.0)) * Canvas::height;
        const auto bottom = (1.0 - scale.toNorm (1000.0)) * Canvas::height;
        const auto left = 0.25 * Canvas::width, right = 0.5 * Canvas::width;

        const auto inside = juce::Rectangle<double> (left, top, right - left, bottom - top).toNearestInt().reduced (2);
        const auto outside = juce::Rectangle<double> (left, top, right - left, bottom - top).getSmallestIntegerContainer().expanded (1);

        CHECK_MSG (minIn (canvas, inside) > 0.999f, "scale " << static_cast<int> (type));
        CHECK_NEAR (maxIn (canvas, { 0, 0, Canvas::width, outside.getY() }), 0.0f, 1.0e-6f);
        CHECK_NEAR (maxIn (canvas, { 0, outside.getBottom(), Canvas::width, Canvas::height }), 0.0f, 1.0e-6f);
        CHECK_NEAR (maxIn (canvas, { 0, 0, outside.getX(), Canvas::height }), 0.0f, 1.0e-6f);
        CHECK_NEAR (maxIn (canvas, { outside.getRight(), 0, Canvas::width, Canvas::height }), 0.0f, 1.0e-6f);

        // The normalised rectangle and its inverse agree with the scale.
        const auto rect = objectRectNorm (object, scale);
        CHECK_NEAR (rect.getX(), 0.25f, 1.0e-6f);
        CHECK_NEAR (rect.getRight(), 0.5f, 1.0e-6f);
        CHECK_NEAR (rect.getY(), 1.0 - scale.toNorm (4000.0), 1.0e-5);
        CHECK_NEAR (rect.getBottom(), 1.0 - scale.toNorm (1000.0), 1.0e-5);

        const auto hz = normSpanToHz (rect.getY(), rect.getBottom(), scale);
        CHECK_NEAR (hz.low, 1000.0, 0.5);
        CHECK_NEAR (hz.high, 4000.0, 2.0);
        const auto swapped = normSpanToHz (rect.getBottom(), rect.getY(), scale);
        CHECK_NEAR (swapped.low, hz.low, 1.0e-9);
    }

    // On a log scale, octaves are equally tall; on a linear one, 1-4 kHz is a thin strip.
    const auto log = objectRectNorm (whiteImageObject (0.0f, 1.0f, 1000.0f, 4000.0f), makeScale (ScaleType::log));
    const auto linear = objectRectNorm (whiteImageObject (0.0f, 1.0f, 1000.0f, 4000.0f), makeScale (ScaleType::linear));
    CHECK_NEAR (log.getHeight(), 2.0 / std::log2 (1000.0), 1.0e-5);
    CHECK_NEAR (linear.getHeight(), 3000.0 / 19980.0, 1.0e-5);
}

FREQART_TEST (compositorIntensityAndMax)
{
    ArtDocument document;
    auto a = whiteImageObject (0.1f, 0.5f, 1000.0f, 4000.0f);
    a.intensity = 0.5f;
    auto b = whiteImageObject (0.3f, 0.7f, 1000.0f, 4000.0f);
    b.intensity = 0.8f;
    document.objects.push_back (b);
    document.objects.push_back (a);

    // Paint over part of a only.
    document.paintStroke ({ { 140.0f, 0.0f }, { 140.0f, 511.0f }, 20.0f, 20.0f, 0.0f, 1.0f, false });

    Compositor compositor;
    Canvas canvas;
    const auto scale = makeScale (ScaleType::log);
    compositor.render (document, scale, canvas);

    const auto row = juce::roundToInt ((1.0 - scale.toNorm (2000.0)) * Canvas::height);
    CHECK_NEAR (canvas.get (250, row), 0.5f, 1.0e-4f);  // a only
    CHECK_NEAR (canvas.get (410, row), 0.8f, 1.0e-4f);  // overlap: the louder wins
    CHECK_NEAR (canvas.get (600, row), 0.8f, 1.0e-4f);  // b only
    CHECK_NEAR (canvas.get (140, row), 1.0f, 1.0e-4f);  // paint over a
    CHECK_NEAR (canvas.get (140, 10), 1.0f, 1.0e-4f);   // paint alone
    CHECK_NEAR (canvas.get (900, row), 0.0f, 1.0e-6f);

    // Intensity 0 draws nothing; the order of objects does not matter.
    std::swap (document.objects[0], document.objects[1]);
    Canvas swapped;
    compositor.render (document, scale, swapped);
    CHECK (swapped.values == canvas.values);

    document.objects[0].intensity = 0.0f;
    document.objects[1].intensity = 0.0f;
    document.clearPaint();
    compositor.render (document, scale, canvas);
    CHECK_NEAR (maxIn (canvas, { 0, 0, Canvas::width, Canvas::height }), 0.0f, 1.0e-9f);
}

FREQART_TEST (compositorPaintUpsampling)
{
    ArtDocument document;

    for (int x = 0; x < ArtDocument::paintWidth; ++x)
        document.paint[static_cast<size_t> (100 * ArtDocument::paintWidth + x)] = 255;

    Compositor compositor;
    Canvas canvas;
    compositor.render (document, makeScale (ScaleType::log), canvas);

    // Bilinear, pixel centres aligned: paint row 100 spreads over canvas rows 199..202.
    for (const auto x : { 0, 511, 1023 })
    {
        CHECK_NEAR (canvas.get (x, 198), 0.0f, 1.0e-6f);
        CHECK_NEAR (canvas.get (x, 199), 0.25f, 1.0e-5f);
        CHECK_NEAR (canvas.get (x, 200), 0.75f, 1.0e-5f);
        CHECK_NEAR (canvas.get (x, 201), 0.75f, 1.0e-5f);
        CHECK_NEAR (canvas.get (x, 202), 0.25f, 1.0e-5f);
        CHECK_NEAR (canvas.get (x, 203), 0.0f, 1.0e-6f);
    }

    std::fill (document.paint.begin(), document.paint.end(), static_cast<uint8_t> (255));
    compositor.render (document, makeScale (ScaleType::log), canvas);
    CHECK_NEAR (minIn (canvas, { 0, 0, Canvas::width, Canvas::height }), 1.0f, 1.0e-6f);
}

FREQART_TEST (compositorPaintKeepsItsFrequencies)
{
    // Paint stays at its frequencies when the canvas scale or range changes, as objects do (they
    // are placed in Hz). It used to keep its height on screen instead, so a bass line painted on
    // Log became a 7 kHz line on Linear, running through the text above it.
    const auto paintScale = makeScale (ScaleType::log);

    // The frequencies a canvas row covers, from its bottom edge to its top edge.
    const auto rowSpan = [] (const FrequencyScale& scale, int row) -> HzRange
    {
        return { scale.toHz (1.0 - (row + 1.0) / Canvas::height), scale.toHz (1.0 - static_cast<double> (row) / Canvas::height) };
    };

    for (const auto& scale : { makeScale (ScaleType::log), makeScale (ScaleType::linear), makeScale (ScaleType::mel),
                               makeScale (ScaleType::log, 40.0, 16000.0), makeScale (ScaleType::linear, 100.0, 5000.0) })
    {
        for (const auto hz : { 200.0, 1000.0, 4000.0 })
        {
            // A thin stroke where the brush puts it: at the height of `hz` on the canvas shown.
            ArtDocument document;
            document.paintScale = paintScale;
            const auto y = static_cast<float> (canvasToPaintY (1.0 - scale.toNorm (hz), scale, paintScale));
            document.paintStroke ({ { 0.0f, y }, { 1023.0f, y }, 1.0f, 1.0f, 0.0f, 1.0f, false });

            Compositor compositor;
            Canvas canvas;
            compositor.render (document, scale, canvas);

            // Drawn back at `hz`, at full strength even where many paint rows share a canvas row
            // (the lows of a linear canvas), and nowhere else.
            const auto row = static_cast<int> ((1.0 - scale.toNorm (hz)) * Canvas::height);
            CHECK_MSG (canvas.get (512, row) > 0.9f, "type " << static_cast<int> (scale.type) << " " << hz << " Hz");

            for (int r = 0; r < Canvas::height; ++r)
            {
                const auto span = rowSpan (scale, r);

                if (canvas.get (512, r) > 0.01f)
                    CHECK_MSG (span.high > hz * 0.96 && span.low < hz * 1.04,
                               "type " << static_cast<int> (scale.type) << " " << hz << " Hz: lit at " << span.low << " Hz");
            }
        }
    }

    // On its own scale the mapping is a plain stretch; parts of the canvas outside the paint's
    // range get nothing; switching back and forth loses nothing.
    for (const auto y : { -0.25, 0.0, 0.3, 1.0 })
        CHECK (juce::exactlyEqual (canvasToPaintY (y, paintScale, paintScale), y * ArtDocument::paintHeight));

    ArtDocument narrow;
    narrow.paintScale = makeScale (ScaleType::log, 100.0, 10000.0);
    std::fill (narrow.paint.begin(), narrow.paint.end(), static_cast<uint8_t> (255));

    Compositor compositor;
    Canvas canvas, before, after;
    const auto wide = makeScale (ScaleType::log);
    compositor.render (narrow, wide, canvas);

    for (int r = 0; r < Canvas::height; ++r)
    {
        const auto span = rowSpan (wide, r);

        if (span.high < 100.0 || span.low > 10000.0)
            CHECK_NEAR (canvas.get (300, r), 0.0f, 1.0e-6f);
        else if (span.low > 100.0 && span.high < 10000.0)
            CHECK_NEAR (canvas.get (300, r), 1.0f, 1.0e-6f);
    }

    compositor.render (narrow, narrow.paintScale, before);
    compositor.render (narrow, makeScale (ScaleType::mel), canvas);
    compositor.render (narrow, narrow.paintScale, after);
    CHECK (before.values == after.values);
}

FREQART_TEST (compositorClipsAndDrawsText)
{
    const auto scale = makeScale (ScaleType::log);
    Compositor compositor;
    Canvas canvas;

    // Hanging off the top (above 20 kHz) and the start (before 0).
    ArtDocument partly;
    partly.objects.push_back (whiteImageObject (-0.2f, 0.3f, 10000.0f, 40000.0f));
    compositor.render (partly, scale, canvas);
    const auto bottom = juce::roundToInt ((1.0 - scale.toNorm (10000.0)) * Canvas::height);
    CHECK_NEAR (canvas.get (0, 0), 1.0f, 1.0e-4f);
    CHECK_NEAR (canvas.get (300, bottom - 3), 1.0f, 1.0e-4f);
    CHECK_NEAR (canvas.get (320, 5), 0.0f, 1.0e-6f);
    CHECK_NEAR (canvas.get (5, bottom + 3), 0.0f, 1.0e-6f);

    // Entirely off the canvas: nothing.
    ArtDocument off;
    off.objects.push_back (whiteImageObject (1.2f, 1.5f, 1000.0f, 2000.0f));
    off.objects.push_back (whiteImageObject (0.2f, 0.5f, 21000.0f, 30000.0f));
    compositor.render (off, scale, canvas);
    CHECK_NEAR (maxIn (canvas, { 0, 0, Canvas::width, Canvas::height }), 0.0f, 1.0e-9f);

    // Text lands inside its rectangle, filling a sensible part of it.
    const auto document = ArtDocument::createDefault();
    compositor.render (document, scale, canvas);
    const auto rect = objectRectNorm (document.objects.front(), scale);
    const auto pixels = juce::Rectangle<float> (rect.getX() * Canvas::width, rect.getY() * Canvas::height,
                                                rect.getWidth() * Canvas::width, rect.getHeight() * Canvas::height);
    const auto outer = pixels.getSmallestIntegerContainer();

    int inside = 0, outside = 0;

    for (int y = 0; y < Canvas::height; ++y)
        for (int x = 0; x < Canvas::width; ++x)
            if (canvas.get (x, y) > 0.0f)
                ++(outer.contains (x, y) ? inside : outside);

    CHECK (outside == 0);
    CHECK_GREATER (inside, outer.getWidth() * outer.getHeight() / 5);
    CHECK_LESS (inside, outer.getWidth() * outer.getHeight() * 4 / 5);

    // Scaled pictures: shrinking averages (a fine checkerboard turns grey, not striped).
    juce::Image checker (juce::Image::ARGB, 512, 512, true, juce::SoftwareImageType());
    {
        juce::Image::BitmapData data (checker, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < 512; ++y)
            for (int x = 0; x < 512; ++x)
                data.setPixelColour (x, y, ((x + y) & 1) != 0 ? juce::Colours::white : juce::Colours::black);
    }

    auto picture = ArtDocument::makeImageObject (checker, 100.0f, 10000.0f);
    picture.imageMode = ImageMode::original;
    picture.cleanup = 0.0f;
    picture.startTime = 0.0f;
    picture.endTime = 0.125f;  // 128 columns for 512 source columns
    ArtDocument shrunk;
    shrunk.objects.push_back (picture);
    compositor.render (shrunk, scale, canvas);
    const auto midRow = juce::roundToInt ((1.0 - scale.toNorm (1000.0)) * Canvas::height);

    for (int x = 10; x < 120; x += 7)
        CHECK_NEAR (canvas.get (x, midRow), 0.5f, 0.02f);
}

FREQART_TEST (compositorTiming)
{
    ArtDocument document;
    auto text = ArtDocument::makeTextObject ("FreqArt", 300.0f, 3000.0f);
    text.startTime = 0.0f;
    text.endTime = 1.0f;
    document.objects.push_back (text);

    auto photo = makeImage (1024, 768, juce::Colours::black);
    {
        juce::Graphics g (photo);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0.0f, 0.0f, juce::Colours::darkblue, 1024.0f, 768.0f, false));
        g.fillEllipse (100.0f, 100.0f, 800.0f, 500.0f);
    }

    document.objects.push_back (ArtDocument::makeImageObject (prepareImportedImage (photo), 3500.0f, 16000.0f));

    for (int i = 0; i < 20; ++i)
        document.paintStroke ({ { 50.0f * static_cast<float> (i), 300.0f }, { 50.0f * static_cast<float> (i) + 40.0f, 500.0f }, 10.0f, 10.0f, 0.5f, 1.0f, false });

    Compositor compositor;
    Canvas canvas;
    const auto scale = makeScale (ScaleType::log);
    compositor.render (document, scale, canvas);  // first call processes the picture

    constexpr int runs = 5;
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < runs; ++i)
    {
        document.objects[1].startTime = 0.01f * static_cast<float> (i);  // moving: cache hit
        compositor.render (document, scale, canvas);
    }

    const auto ms = millisecondsSince (start) / runs;
    std::printf ("    Compositor::render (text + 1024 x 768 picture + paint): %.2f ms (budget 15 ms)\n", ms);
    CHECK_LESS (ms, 4 * 15.0);

    // Changing the clean-up reprocesses the picture; the old work is dropped and the result
    // matches a fresh compositor.
    CHECK (compositor.getNumCachedImages() == 1);
    document.objects[1].cleanup = 0.3f;
    compositor.render (document, scale, canvas);
    CHECK (compositor.getNumCachedImages() == 1);
    Compositor fresh;
    Canvas reference;
    fresh.render (document, scale, reference);
    CHECK (reference.values == canvas.values);

    // Two pictures (one shown twice) are two entries; trimming to a document without them
    // empties the cache.
    auto copy = document.objects[1];
    copy.id = juce::Uuid();
    document.objects.push_back (copy);
    document.objects.push_back (whiteImageObject (0.0f, 0.2f, 100.0f, 200.0f));
    compositor.render (document, scale, canvas);
    CHECK (compositor.getNumCachedImages() == 2);
    compositor.trimCache (ArtDocument::createDefault());
    CHECK (compositor.getNumCachedImages() == 0);
}
