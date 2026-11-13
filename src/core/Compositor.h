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

/** Where a height on the canvas falls on the paint layer, through the frequency at that height:
    canvasY runs from the TOP of a canvas on `scale` (0) to its bottom (1); the result is in
    paint-layer pixels from the top of a layer painted on `paintScale` (0) to its bottom
    (ArtDocument::paintHeight). Heights off the canvas, or off the paint's range, give rows off
    the layer. On the scale the paint was made on it is a plain stretch. The brush uses it to
    place strokes and the compositor to draw them back, so the two always agree. */
double canvasToPaintY (double canvasY, const FrequencyScale& scale, const FrequencyScale& paintScale) noexcept;

class Compositor
{
public:
    /** Draws everything into `out` (which is cleared first):
          - the paint layer (1024 x 512), mapped by frequency from its paintScale to `scale`
            (bilinear where stretched, the loudest paint row where squeezed; nothing outside
            the paint's range), so it stays at its frequencies like the objects;
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
