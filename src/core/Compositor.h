#pragma once

// Flattens a document into one picture of the spectrogram: the canvas.

#include "Document.h"
#include "FrequencyScale.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace artfreq
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

/** How a rotated object's content is turned: by `degrees` (clockwise on screen) about the
    centre of `box`, in display space. `xScale` is how much wider than tall one unit of the
    coordinates `box` is in is on screen, relative to its height: the plug-in's display aspect
    (ArtDocument::displayAspectFor) for normalised canvas coordinates and for canvas pixels
    (both are square grids over the canvas); that aspect * height / width for a view `width` x
    `height` pixels big (1 for a view shown at the plug-in's aspect). So the turn is a true
    rotation on a canvas shown at that aspect, whatever the coordinates. The identity for 0
    degrees.

    The functions below that take a `displayAspect` take the plug-in's (see
    ArtDocument::displayAspectFor): the shape of the canvas the object is seen on. */
juce::AffineTransform rotationTransform (juce::Rectangle<float> box, float degrees, float xScale) noexcept;

/** The corners of an object's box once turned, in normalised coordinates (as objectRectNorm):
    top-left, top-right, bottom-right, bottom-left of the unrotated box. */
std::array<juce::Point<float>, 4> objectCornersNorm (const ArtObject& object, const FrequencyScale& scale,
                                                     float displayAspect) noexcept;

/** The smallest upright rectangle around an object's turned box (objectRectNorm for an
    upright object), in normalised coordinates: what the object covers on the canvas. */
juce::Rectangle<float> objectBoundsNorm (const ArtObject& object, const FrequencyScale& scale, float displayAspect) noexcept;

/** A box's shape as seen on screen: its width over its height in display space, of the
    unrotated box. */
float displayAspectOf (const ArtObject& object, const FrequencyScale& scale, float displayAspect) noexcept;

/** Shapes a picture's box can be given at once (the inspector's Shape). */
enum class ShapePreset
{
    original = 0,   // the picture's own proportions (its pixels' width / height)
    square,         // 1:1
    fourByThree,    // 4:3
    threeByFour,    // 3:4
    sixteenByNine,  // 16:9
    nineBySixteen   // 9:16
};

constexpr int numShapePresets = 6;

/** The display aspect (width / height on screen) a preset asks for: for Original the picture's
    (1 for an object without one). */
float shapeAspect (ShapePreset preset, const ArtObject& object) noexcept;

/** Reshapes an object's box to `aspect` (its width / height on screen), about its centre,
    keeping its area on screen (the geometric mean of its sides). It stays on the canvas (time
    0..1, the scale's range): shrunk about the centre if it would cross an edge (to no less
    than half its size, after which its centre moves in). The rotation is kept. */
void applyShapeAspect (ArtObject& object, float aspect, const FrequencyScale& scale, float displayAspect) noexcept;

/** The preset the object's box has (within 2 %), Original first; -1 if none. */
int matchingShapePreset (const ArtObject& object, const FrequencyScale& scale, float displayAspect) noexcept;

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
    /** Turns rotated objects for a canvas shown `displayAspect` wide (the plug-in's, see
        ArtDocument::displayAspectFor; ArtFreq's unless told otherwise). Upright objects do not
        depend on it. */
    explicit Compositor (float displayAspect = ArtDocument::instrumentDisplayAspect) noexcept
        : aspect (displayAspect) {}

    float getDisplayAspect() const noexcept { return aspect; }

    /** Draws everything into `out` (which is cleared first):
          - the paint layer (1024 x 512), mapped by frequency from its paintScale to `scale`
            (bilinear where stretched, the loudest paint row where squeezed; nothing outside
            the paint's range), so it stays at its frequencies like the objects;
          - each object, multiplied by its intensity, into the canvas pixels of objectRectNorm:
              text:  renderTextInto (stretched to fill the rectangle)
              image: processImage (mode, cleanup), stretched to fill the rectangle (area-averaged
                     when shrinking, bilinear when enlarging);
            a rotated object is fitted to its rectangle the same way and then turned about the
            rectangle's centre (rotationTransform, at this compositor's display aspect): text as
            a turned path, a picture resampled first to the grid it covers once turned
            (area-averaged or bilinear, as above), then read bilinearly at each canvas pixel,
            its edges anti-aliased. An upright object is drawn exactly as before objects could
            be rotated;
        combined by taking the maximum. Objects partly off the visible range are clipped.
        Hidden objects (and a hidden paint layer) are left out.

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

    float aspect;
    std::map<CacheKey, CacheEntry> cache;
};

} // namespace artfreq
