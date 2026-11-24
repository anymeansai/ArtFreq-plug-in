#pragma once

// What a spectrogram analyser (MiniMeters, or any scrolling spectrogram) shows of a sound: a
// windowed FFT of the last few thousand samples, its bins in dB, read off at the heights of the
// picture's rows and coloured over a fixed range of levels.
//
// One implementation for everything that draws a sound as an analyser does, so they cannot
// disagree: the canvas's measured preview (ui/MeasuredPreview.h), the live view and the truth
// lab (tools/TruthLab.cpp), which checks the canvas against the real output.

#include "core/FrequencyScale.h"

#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <vector>

namespace artfreq
{

/** The window: Blackman-Harris, four terms (w[n] = a0 - a1 cos (2 pi n / N) + a2 cos (4 pi n / N)
    - a3 cos (6 pi n / N)), side lobes 92 dB down.

    Why not Hann (as up to 0.6): Hann's side lobes start 31 dB under a tone and fall slowly (60 dB
    down seven bins away), so on a log scale every low tone, however clean, was drawn with a skirt
    hanging under it to the bottom of the picture: at 8192 points and 48 kHz a 50 Hz tone 60 dB
    above the darkest colour showed from 10 to 90 Hz, and the customer read that as sound "dripping"
    below the drawing. This window shows a tone as its main lobe and nothing else, four bins
    either side at the very faintest (the same 50 Hz tone: 27 to 73 Hz), at the price of a main
    lobe about a third wider near its top (2.7 bins at half height against Hann's 2). What it
    shows under a shape is then in the sound. An analyser that uses Hann draws the lows a little
    thinner and adds that skirt; nothing else differs. */
constexpr float analysisWindow[] { 0.35875f, 0.48829f, 0.14128f, 0.01168f };
constexpr float analysisWindowNoiseBins = 2.0f;   // its noise bandwidth: what closely spaced sines add up over
constexpr float analysisWindowLobeBins = 4.0f;    // its main lobe ends this many bins from a sine

/** How the user's analyser draws (Settings, "Match your analyser"): the size of its FFT, which
    sets how sharp it is in pitch and how blurred in time (and how blurred the lows are: a bin is
    sampleRate / fftSize wide at every height), and the levels its colours span. A full-scale sine
    reads 0 dBFS. */
struct AnalyserView
{
    int fftSize = 8192;
    float floorDb = -80.0f;      // shown as the darkest colour, and everything quieter
    float ceilingDb = 0.0f;      // shown as the brightest colour, and everything louder

    /** A power of two from 1024 to 32768, and a range at least minSpanDb wide. */
    AnalyserView sanitised() const noexcept;

    static constexpr float minSpanDb = 20.0f;

    bool operator== (const AnalyserView& other) const noexcept
    {
        return fftSize == other.fftSize && juce::exactlyEqual (floorDb, other.floorDb)
            && juce::exactlyEqual (ceilingDb, other.ceilingDb);
    }

    bool operator!= (const AnalyserView& other) const noexcept { return ! operator== (other); }
};

/** The colour-map index 0..255 (theme::spectrumTable) for a level in dBFS over a range. */
inline uint8_t colourLevelFor (float db, float floorDb, float ceilingDb) noexcept
{
    const auto t = (db - floorDb) / (ceilingDb - floorDb);

    if (! (t > 0.0f))   // also NaN
        return 0;

    return static_cast<uint8_t> (t >= 1.0f ? 255 : static_cast<int> (t * 255.0f + 0.5f));
}

/** One analyser column: the window (analysisWindow), FFT, the magnitude of each bin in dBFS,
    scaled so that a full-scale sine centred on a bin reads 0 dB. Allocates in the constructor
    only. */
class SpectrumAnalyser
{
public:
    static constexpr float floorDb = -160.0f;   // what silence reads

    explicit SpectrumAnalyser (int fftSize);

    int getFftSize() const noexcept { return size; }
    int getNumBins() const noexcept { return size / 2 + 1; }

    /** `samples`: getFftSize() of them, oldest first. Writes getNumBins() levels to `binsDb`. */
    void analyse (const float* samples, float* binsDb) noexcept;

private:
    int size;
    juce::dsp::FFT fft;
    std::vector<float> window, work;
    float toAmplitude = 1.0f;
};

/** How each row of a picture on a frequency scale reads an analyser column: the spectrum
    interpolated between the bins around the row's centre, or the loudest bin inside the row
    where it spans several (so a line never falls between rows), plus the analyser's slope at
    that height (pivot 1 kHz). Row 0 is the top of the scale. Rows above Nyquist read floorDb. */
class SpectrumRows
{
public:
    static constexpr double slopePivotHz = 1000.0;

    void prepare (const FrequencyScale& scale, int height, double sampleRate, int fftSize, float slopeDbPerOctave);

    int getHeight() const noexcept { return static_cast<int> (rows.size()); }

    /** `binsDb`: the column, fftSize / 2 + 1 levels. Writes getHeight() levels to `rowsDb`. */
    void apply (const float* binsDb, float* rowsDb) const noexcept;

private:
    struct Row
    {
        int below = 0;          // the bin at or below the row's centre
        float fraction = 0.0f;  // ... and how far on to the next one the centre lies
        int first = 0, last = -1;  // the bins whose centres lie inside the row (none: last < first)
        float offsetDb = 0.0f;  // the analyser's slope here
        bool valid = false;
    };

    std::vector<Row> rows;
};

} // namespace artfreq
