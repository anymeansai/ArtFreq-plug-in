#pragma once

// Turning a picture into something that reads well in a spectrogram.
//
// In a spectrogram, loud is bright and silence is dark. A photo or logo on a white background
// drawn as-is would come out as a loud block with the subject cut out of it, so ArtFreq maps
// each picture to "how loud is this pixel" (0 = silent, 1 = loudest) with one of five modes.

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace artfreq
{

enum class ImageMode
{
    automatic = 0,  // pick for the user (see resolveAutoMode)
    original = 1,   // bright pixels are loud
    negative = 2,   // dark pixels are loud (the "negative" of the picture)
    outline = 3,    // only the edges are drawn (line art), from the Auto-resolved picture
    blackAndWhite = 4  // "B&W": two levels, loud or silent, from the Auto-resolved picture
};

// (Saved projects store the number: new modes are added at the end.)
constexpr int numImageModes = 5;

/** Loads a PNG, JPEG or GIF from disk. Returns an invalid image if the file cannot be read. */
juce::Image loadImageFile (const juce::File& file);

/** Converts any image to ARGB, scaled down (high quality, keeping its proportions) so that its
    longer side is at most maxSide pixels. Images already small enough are converted but not
    scaled. Returns an invalid image for an invalid input. */
juce::Image prepareImportedImage (const juce::Image& raw, int maxSide = 1024);

/** What Auto picks for this image:
      - if at least 2 % of the pixels are clearly transparent (alpha < 128), the picture is a
        shape on a transparent background: its alpha is drawn (reported as ImageMode::original);
      - otherwise, if the outer ring of the picture (3 % of each side) is light on average
        (mean luminance > 0.5), the background is light: ImageMode::negative;
      - otherwise ImageMode::original. */
ImageMode resolveAutoMode (const juce::Image& image);

/** True if the image has the transparency described above (Auto then draws its alpha). */
bool hasMeaningfulTransparency (const juce::Image& image);

struct ProcessedImage
{
    int width = 0;
    int height = 0;
    std::vector<float> values;           // width * height, row-major, row 0 = top, each 0..1
    ImageMode resolvedMode = ImageMode::original;  // what Auto chose (or the mode itself)

    float at (int x, int y) const noexcept { return values[static_cast<size_t> (y * width + x)]; }
    bool isValid() const noexcept { return width > 0 && height > 0; }
};

/** Maps an image to loudness values.

    Per pixel, with lum = Rec. 709 luminance of the un-premultiplied colour (0..1) and a = alpha:
      original: v = a * lum            (transparent images under Auto: v = a)
      negative: v = a * (1 - lum)
      outline:  Sobel edge magnitude of the Auto-resolved values
      blackAndWhite: the Auto-resolved values (so the subject is drawn, not its background)
    Then, in order:
      1. auto-levels: values are scaled so the 99th percentile becomes 1 (skipped if that
         percentile is below 0.02, i.e. the picture is essentially empty), then clamped to 0..1;
      2. clean-up: v = max (0, (v - cleanup) / (1 - cleanup)), which silences faint
         backgrounds, grain and JPEG noise. cleanup is clamped to 0..0.9;
      3. blackAndWhite only: every value becomes 0 or 1, split at the threshold Otsu's method
         picks from the histogram of the visible pixels (no greys: a photo's midtones turn to
         mush in a spectrogram, two levels read crisply). A picture of one level is all loud
         if that level is at least half, else silent; a fully transparent one is silent.
         (Edges soften again only where the compositor scales the picture to its place.)
*/
ProcessedImage processImage (const juce::Image& image, ImageMode mode, float cleanup);

} // namespace artfreq
