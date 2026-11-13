#pragma once

// Flattens a document into one picture of the spectrogram: the canvas.

#include "Document.h"
#include "FrequencyScale.h"

#include <cstdint>
#include <map>
#include <vector>

namespace freqart
{

struct Canvas
{
    static constexpr int width = 1024;   // columns: time, 0 = start of the drawing
    static constexpr int height = 1024;  // rows: frequency on the scale, row 0 = top (scale.maxHz)

    Canvas() : values (static_cast<size_t> (width * height), 0.0f) {}

    std::vector<float> values;  // row-major, each 0..1 (0 = silent, 1 = loudest)

    float get (int x, int y) const noexcept { return values[static_cast<size_t> (y * width + x)]; }
    float& at (int x, int y) noexcept { return values[static_cast<size_t> (y * width + x)]; }

    void clear() { std::fill (values.begin(), values.end(), 0.0f); }
};

/** Where an object sits on the canvas, in normalised coordinates: x 0..1 is time, y 0..1 runs
    from the TOP (scale.maxHz) to the bottom (scale.minHz), like screen and image coordinates.
    Parts outside 0..1 are off the visible range (and are not drawn). */
juce::Rectangle<float> objectRectNorm (const ArtObject& object, const FrequencyScale& scale) noexcept;

/** The inverse of objectRectNorm for the frequency part: the Hz range a normalised vertical span
    (top < bottom, y = 0 at the top) covers on the scale. */
HzRange normSpanToHz (float top, float bottom, const FrequencyScale& scale) noexcept;

class Compositor
{
public:
    /** Draws everything into `out` (which is cleared first):
          - the paint layer, resampled bilinearly from 1024 x 512 to the canvas (row 0 = top);
          - each object, multiplied by its intensity, into the canvas pixels of objectRectNorm:
              text:  renderTextInto (stretched to fill the rectangle)
              image: processImage (mode, cleanup), stretched to fill the rectangle (area-averaged
                     when shrinking, bilinear when enlarging);
        combined by taking the maximum. Objects partly off the visible range are clipped.

        Processed images are cached between calls (keyed by the image's pixel data, mode and
        clean-up), so re-rendering after moving an object or painting is cheap. */
    void render (const ArtDocument& document, const FrequencyScale& scale, Canvas& out);

    /** Drops cached work for images no longer used by `document`. render() does this itself
        after every pass, so the cache never holds more than the pictures last drawn. */
    void trimCache (const ArtDocument& document);

    /** How many processed pictures are cached (for tests and diagnostics). */
    size_t getNumCachedImages() const noexcept { return cache.size(); }

private:
    struct CacheKey
    {
        const void* pixels;
        int mode;
        int cleanupMillis;
        bool operator< (const CacheKey& other) const noexcept
        {
            if (pixels != other.pixels) return pixels < other.pixels;
            if (mode != other.mode) return mode < other.mode;
            return cleanupMillis < other.cleanupMillis;
        }
    };

    struct CacheEntry
    {
        juce::Image image;  // keeps the source pixels alive so the key stays unique
        ProcessedImage processed;
    };

    static CacheKey keyFor (const ArtObject& object);
    const ProcessedImage& processed (const ArtObject& object);

    std::map<CacheKey, CacheEntry> cache;
};

} // namespace freqart
