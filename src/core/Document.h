#pragma once

// The drawing: what the user has made, independent of how it is turned into sound.
//
// A document is a paint layer (free-hand brush strokes) plus any number of objects (text and
// pictures). The canvas they are drawn on is the spectrogram: x runs over the drawing's length
// in time, y over the analyser's frequency scale. The final picture is the maximum of the paint
// layer and every object, pixel by pixel.
//
// Only the message thread touches a document. It is a plain value type: copying it is how
// undo snapshots and the processor's state are taken (pictures are shared, never modified in
// place, so copies are cheap apart from the paint layer).

#include "FrequencyScale.h"
#include "ImageProcessing.h"
#include "TextRaster.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <vector>

namespace freqart
{

struct ArtObject
{
    enum class Type
    {
        text = 0,
        image = 1
    };

    juce::Uuid id;
    Type type = Type::text;

    // Placement. Time is a fraction of the drawing's length (0 = start, 1 = end, start < end).
    // Frequency is in Hz, so a placement keeps its meaning ("the highs") if the analyser scale
    // changes. lowHz < highHz.
    float startTime = 0.04f;
    float endTime = 0.96f;
    float lowHz = 3500.0f;
    float highHz = 16000.0f;

    float intensity = 1.0f;  // 0..1, multiplies the object's values

    // Text objects
    juce::String text;
    int fontIndex = 0;
    TextStyle textStyle = TextStyle::solid;

    // Image objects. `image` is ARGB, already passed through prepareImportedImage, and is never
    // modified in place (replace it instead), so documents can share it.
    juce::Image image;
    ImageMode imageMode = ImageMode::automatic;
    float cleanup = 0.08f;  // 0..0.9, see processImage
};

/** A brush dab or stroke on the paint layer. Coordinates are paint-layer pixels (x 0..1023,
    y 0..511, y = 0 at the top, which is the document's paintScale.maxHz). Radii are separate so
    a brush that is round on screen stays round however the canvas is stretched. */
struct BrushStroke
{
    juce::Point<float> from;
    juce::Point<float> to;
    float radiusX = 8.0f;
    float radiusY = 8.0f;
    float softness = 0.5f;  // 0 = hard edge, 1 = fully feathered (value falls off from the centre)
    float strength = 1.0f;  // 0..1: the value the brush paints at its centre
    bool erase = false;     // erasing lowers values towards 0 by the same profile
};

class ArtDocument
{
public:
    static constexpr int paintWidth = 1024;
    static constexpr int paintHeight = 512;

    ArtDocument();  // empty paint layer, no objects

    /** What a fresh instance shows: the word "FreqArt" in the mid band, Inter Bold, solid. */
    static ArtDocument createDefault();

    std::vector<ArtObject> objects;  // drawn in this order (the order does not change the result)
    std::vector<uint8_t> paint;      // paintWidth * paintHeight, row-major, row 0 = paintScale.maxHz

    // The analyser scale the paint layer was painted on: its rows run evenly over this scale,
    // from maxHz (row 0) to minHz. The compositor and the canvas map them through it, so paint
    // keeps its frequencies, like objects, when the user changes the scale or range (the
    // pixels are never resampled, so switching back and forth loses nothing). Set to the
    // current scale by the first stroke on an empty layer; clearPaint resets it.
    FrequencyScale paintScale;

    bool isEmpty() const noexcept;   // no objects and nothing painted
    bool hasPaint() const noexcept;

    ArtObject* find (const juce::Uuid& id) noexcept;
    const ArtObject* find (const juce::Uuid& id) const noexcept;
    bool remove (const juce::Uuid& id);

    /** Paints (or erases) along a stroke: dabs every max (1, min (rx, ry) / 4) pixels from
        `from` to `to`. Each dab sets a pixel to max (pixel, strength * profile) when painting,
        or to pixel * (1 - profile) when erasing, where profile is 1 inside the hard core
        (radius * (1 - softness)) and falls smoothly (smoothstep) to 0 at the radius. Within one
        call the dabs are combined first (strongest profile per pixel), so overlapping dabs do
        not compound an erase. Pixel (x, y) is measured at its centre (x + 0.5, y + 0.5). */
    void paintStroke (const BrushStroke& stroke);

    /** Empties the paint layer and puts paintScale back to the default scale. */
    void clearPaint();

    /** Serialisation. The ValueTree type is "FreqArtDocument" (version 1):
          properties: version, paint (base64 of zlib-compressed paint bytes; absent if empty),
                      paintScale, paintMinHz, paintMaxHz (with the paint; a document without
                      them was painted on the default Log 20 Hz - 20 kHz scale)
          children "Object": id, type, startTime, endTime, lowHz, highHz, intensity, text,
                             fontIndex, textStyle, imageMode, cleanup, image (base64 PNG)
        fromValueTree tolerates missing or out-of-range values (it clamps them and drops
        objects it cannot use) and returns an empty document for an invalid tree. */
    juce::ValueTree toValueTree() const;
    static ArtDocument fromValueTree (const juce::ValueTree& tree);

    /** A new text object with sensible defaults, placed in the given frequency range. */
    static ArtObject makeTextObject (const juce::String& text, float lowHz, float highHz);

    /** A new image object for an image already passed through prepareImportedImage. */
    static ArtObject makeImageObject (const juce::Image& preparedImage, float lowHz, float highHz);
};

/** Undo/redo by whole-document snapshots. */
class DocumentHistory
{
public:
    explicit DocumentHistory (int maxSteps = 40) : limit (maxSteps) {}

    void clear();

    /** Records `before` (the document as it was before an edit) as an undo step and clears the
        redo steps. */
    void push (const ArtDocument& before);

    bool canUndo() const noexcept { return ! undoSteps.empty(); }
    bool canRedo() const noexcept { return ! redoSteps.empty(); }

    /** Replaces `current` with the previous snapshot (moving `current` onto the redo stack).
        Returns false (and leaves `current` alone) if there is nothing to undo. */
    bool undo (ArtDocument& current);
    bool redo (ArtDocument& current);

private:
    int limit;
    std::vector<ArtDocument> undoSteps;
    std::vector<ArtDocument> redoSteps;
};

} // namespace freqart
