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
//  - colours are absolute levels, dBFS at the output Level with the analyser's slope, over the
//    analyser's range from Settings, as the measured preview and the live view colour them.
//
// What it leaves out, and only the measured preview shows: the comb's shimmer, the faint energy
// spilled where shapes start and stop, lines beating against each other.

#include "core/ArtPlan.h"
#include "dsp/SpectrumAnalysis.h"

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

class CanvasPlanImage
{
public:
    /** The picture of `plan` (null: silence), `width` x `height` image pixels. Rebuilt only when
        something changed; a change of Level or of the colours' range only recolours it. */
    juce::Image get (const std::shared_ptr<const ArtPlan>& plan, const AnalyserModel& model, int width, int height);

    /** Forgets the cached picture (the next get() rebuilds it). */
    void invalidate() noexcept { image = {}; geometryValid = false; }

    /** The level in dBFS of every pixel of the picture last returned, row by row, before Level
        (the slope included; -200 for silence), and Level in dB: what ArtFreq FX's fast preview
        adds to its stand-in for the song. */
    const std::vector<float>& getPixelDbBeforeLevel() const noexcept { return geometryDb; }
    float getLevelDb() const noexcept { return levelDb; }

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
    void colour (const AnalyserModel& model, int width, int height);

    juce::Image image;

    // What the geometry (pixelDb before Level) was worked out for
    bool geometryValid = false;
    const ArtPlan* geometryPlan = nullptr;
    uint64_t geometryVersion = 0;
    AnalyserModel geometryModel;   // its outputGain and view's range do not matter
    int geometryWidth = 0, geometryHeight = 0;

    // ... and the picture's colours
    AnalyserModel imageModel;
    std::vector<float> geometryDb; // width x height, without Level (silence: -200)
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
    std::vector<float> accumulator, scratch;
    std::vector<juce::PixelARGB> rowPixels;
};

/** Copies a row of opaque colours into an ARGB or RGB image (whatever its pixel layout). */
void writePixelRow (juce::Image::BitmapData& data, int y, const juce::PixelARGB* source, int width) noexcept;

} // namespace artfreq::ui
