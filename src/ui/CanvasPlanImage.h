#pragma once

// The fast preview: the picture of a plan as the user's analyser would show it, worked out from
// the plan without rendering any sound, quickly enough to follow a drag. The measured preview
// (MeasuredPreview.h), which renders and analyses the real sound, takes over once the drawing
// stays still; this one is made to look as much like it as can be had cheaply, so the swap is
// gentle:
//
//  - each sine line is drawn as the analyser draws a sine: its window's main lobe and side lobes
//    (Hann), as many Hz wide at every height (sampleRate / fftSize a bin), so at low density the
//    highs show scan lines one pixel tall with dark gaps, and the lows are blurred and thick;
//  - in time, each line's loudness is blurred by the analyser's window (fftSize samples);
//  - the rising comb (Smooth lows) is drawn as a filled band at the brightness the analyser shows
//    it (its energy spreads along its glide: about 3 dB darker per doubling of the FFT);
//  - under a shape in the comb's band, the faint haze its gliding sines leave below it (45 dB
//    under the shape: measured, 40 to 55);
//  - colours are absolute levels, dBFS at the output Level with the analyser's slope, over the
//    analyser's range from Settings, as the measured preview and the live view colour them.
//
// What it leaves out, and only the measured preview shows: the comb's shimmer, the energy spilled
// where shapes start and stop elsewhere, lines beating against each other. (Truth lab, against the
// real output: correlation 0.76 - 0.98 by band, the drawing standing out at most 9 dB more than in
// reality, but in the few rows of a linear scale's lows, where it leaves out a haze.)

#include "core/ArtPlan.h"
#include "dsp/SpectrumAnalysis.h"

namespace artfreq::ui
{
struct MeasuredSpectrogram;   // MeasuredPreview.h
}

#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace artfreq::ui
{

/** What the fast preview models: the analyser, and the sound's level and timing. */
struct AnalyserModel
{
    FrequencyScale scale;
    float slopeDbPerOctave = 0.0f;
    AnalyserView view;
    double sampleRate = 48000.0;
    double passSeconds = 8.0;
    float outputGain = 1.0f;   // Level, as a gain

    bool operator== (const AnalyserModel& other) const noexcept
    {
        return scale == other.scale && juce::exactlyEqual (slopeDbPerOctave, other.slopeDbPerOctave) && view == other.view
            && juce::exactlyEqual (sampleRate, other.sampleRate) && juce::exactlyEqual (passSeconds, other.passSeconds)
            && juce::exactlyEqual (outputGain, other.outputGain);
    }

    bool operator!= (const AnalyserModel& other) const noexcept { return ! operator== (other); }
};

/** Where the fast preview shows the last measurement rather than its model (see FastPreview.h):
    the columns of the picture the drawing did not change near since it was measured. Their sound
    is exactly what was measured (moved by `shiftDb`: Level, or the drawing's loudness scaled as a
    whole), so the picture there is the measured one, haze, blur and the comb's texture included. */
struct MeasuredPatch
{
    const MeasuredSpectrogram* analysis = nullptr;  // its output levels, rows as the picture's
    std::vector<float> weight;                      // per picture column: 1 the measurement, 0 the model, between: blended
    float shiftDb = 0.0f;

    /** Each picture pixel's level, blended: `modelDb` the model's, at pixel (x, y). */
    float blend (float modelDb, int x, int y, int height) const noexcept;

    /** Prepares the columns' frames for a picture `width` wide (call once before blend). */
    void prepare (int width);

private:
    std::vector<int> frame0, frame1;
    std::vector<float> fraction;
};

class CanvasPlanImage
{
public:
    /** The picture of `plan` (null: silence), `width` x `height` image pixels. Rebuilt only when
        something changed; a change of Level or of the colours' range only recolours it. With a
        `patch`, the columns it covers show the measurement (always recoloured). */
    juce::Image get (const std::shared_ptr<const ArtPlan>& plan, const AnalyserModel& model, int width, int height,
                     const MeasuredPatch* patch = nullptr);

    /** Works out what get() would, without making the picture: the levels alone
        (getPixelDbBeforeLevel), for ArtFreq FX, which makes its own picture from them. */
    void prepareLevels (const std::shared_ptr<const ArtPlan>& plan, const AnalyserModel& model, int width, int height);

    /** Forgets the cached picture (the next get() rebuilds it). */
    void invalidate() noexcept { image = {}; geometryValid = false; }

    /** The level in dBFS of the picture last returned, row by row, before Level (the slope
        included; -200 for silence), getWorkWidth() columns across (workWidthFor its width), and
        Level in dB: what ArtFreq FX's fast preview adds to its stand-in for the song. */
    const std::vector<float>& getPixelDbBeforeLevel() const noexcept { return geometryDb; }
    int getWorkWidth() const noexcept { return geometryWorkWidth; }
    float getLevelDb() const noexcept { return levelDb; }

    /** The previews work out a picture `width` pixels wide at this many columns and stretch them
        across it: the plan (and the masks) have Canvas::width columns, and pixels between them
        are what the columns either side make them (Retina windows at 150 and 200 % are 2300 to
        3100 pixels wide: a third of the work at the largest). */
    static int workWidthFor (int width) noexcept { return juce::jlimit (1, Canvas::width, width); }

    /** For each of `width` pixels, the work column before its centre and how far on to the next
        (as for the analyses: centres where they are). */
    static void stretchSteps (int workWidth, int width, std::vector<int>& before, std::vector<float>& fraction);

    /** Counts the pictures rendered: it changes whenever the picture last returned does. */
    uint64_t getRenderCount() const noexcept { return renderCount; }

    /** How the analyser's window spreads a sine over frequency: the amplitude it reads `bins` FFT
        bins away from the sine (Hann: 1 at 0, 0.5 at one bin, 0 at two, then side lobes, here
        their envelope, -32 dB at 2.5 bins and falling 18 dB an octave). */
    static float lobe (float bins) noexcept;

    /** How bright an analyser of fftSize points shows the rising comb, against a line of the
        same amplitude (combGain is the brightness at 8192). */
    static float combBrightness (int fftSize) noexcept;

private:
    void updateAmplitudes (const ArtPlan& plan, const AnalyserModel& model);
    void renderGeometry (const ArtPlan* plan, const AnalyserModel& model, int width, int height);
    void colour (const AnalyserModel& model, int width, int height, const MeasuredPatch* patch);
    bool updateGeometry (const std::shared_ptr<const ArtPlan>& plan, const AnalyserModel& model, int width, int height);

    juce::Image image;

    // What the geometry (pixelDb before Level) was worked out for
    bool geometryValid = false;
    const ArtPlan* geometryPlan = nullptr;
    uint64_t geometryVersion = 0;
    AnalyserModel geometryModel;   // its outputGain and view's range do not matter
    int geometryWidth = 0, geometryHeight = 0;

    // ... and the picture's colours
    AnalyserModel imageModel;
    bool imagePatched = false;
    std::vector<float> geometryDb; // geometryWorkWidth x height, without Level (silence: -200)
    int geometryWorkWidth = 1;
    float levelDb = 0.0f;
    uint64_t renderCount = 0;

    // Each line's amplitude per column as the analyser reads it (blurred in time), line by line
    std::shared_ptr<const ArtPlan> amplitudesPlan;   // kept so the pointer cannot be reused
    uint64_t amplitudesVersion = 0;
    double amplitudesPassSeconds = 0.0, amplitudesSampleRate = 0.0;
    int amplitudesFft = 0;
    std::vector<float> amplitudes;  // [line * numColumns + column]
    std::vector<float> lineLoudest; // per line, its loudest column

    // Scratch, kept to avoid reallocating per rebuild
    struct Contribution
    {
        int line;
        float weight;
    };

    std::vector<Contribution> contributions;
    std::vector<int> rowStart;
    std::vector<int> columnOf, nextColumnOf;
    std::vector<float> columnFraction;
    std::vector<float> accumulator, combAccumulator, combDb, scratch;
    std::vector<int> stretchBefore;
    std::vector<float> stretchFraction, rowIndex;
    std::vector<juce::PixelARGB> rowPixels;
};

/** Copies a row of opaque colours into an ARGB or RGB image (whatever its pixel layout). */
void writePixelRow (juce::Image::BitmapData& data, int y, const juce::PixelARGB* source, int width) noexcept;

} // namespace artfreq::ui
