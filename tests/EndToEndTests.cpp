// The product's promise, measured: the spectrogram of FreqArt's output looks like the drawing.
//
// Each test draws something, plays one pass through the processor, analyses the output with
// a short-time Fourier transform like an analyser would, resamples that spectrogram onto the
// canvas grid through the same frequency scale, and compares it with the composited canvas.

#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"

#include <juce_dsp/juce_dsp.h>

#include <cstdio>

using namespace freqart;
using namespace testutil;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fftOrder = 12;
constexpr int fftSize = 1 << fftOrder;
constexpr int hop = 256;

/** Magnitude spectrogram in dB, frames centred at frame * hop + fftSize / 2. */
struct Spectrogram
{
    std::vector<std::vector<float>> frames;  // [frame][bin] dB

    int frameAt (double seconds) const
    {
        return juce::jlimit (0, static_cast<int> (frames.size()) - 1,
                             static_cast<int> (std::lround ((seconds * sampleRate - fftSize / 2.0) / hop)));
    }

    /** What an analyser's pixel covering this time and frequency span shows: the loudest bin
        in it (at least the bin nearest the centre). */
    float cell (double fromSeconds, double toSeconds, double lowHz, double highHz) const
    {
        const auto firstFrame = frameAt (fromSeconds);
        const auto lastFrame = juce::jmax (firstFrame, frameAt (toSeconds));
        const auto toBin = [] (double hz) { return juce::jlimit (1, fftSize / 2, static_cast<int> (std::lround (hz * fftSize / sampleRate))); };
        const auto lowBin = toBin (lowHz);
        const auto highBin = juce::jmax (lowBin, toBin (highHz));

        auto loudest = -140.0f;
        for (int frame = firstFrame; frame <= lastFrame; ++frame)
            for (int bin = lowBin; bin <= highBin; ++bin)
                loudest = juce::jmax (loudest, frames[static_cast<size_t> (frame)][static_cast<size_t> (bin)]);

        return loudest;
    }
};

Spectrogram analyse (const std::vector<float>& signal)
{
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / fftSize);

    Spectrogram result;
    std::vector<float> work (fftSize * 2);

    for (size_t start = 0; start + fftSize <= signal.size(); start += hop)
    {
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            work[static_cast<size_t> (i)] = signal[start + static_cast<size_t> (i)] * window[static_cast<size_t> (i)];

        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        std::vector<float> frame (fftSize / 2 + 1);
        for (size_t bin = 0; bin < frame.size(); ++bin)
            frame[bin] = juce::Decibels::gainToDecibels (work[bin] / (fftSize / 4.0f), -140.0f);

        result.frames.push_back (std::move (frame));
    }

    return result;
}

double pearson (const std::vector<double>& a, const std::vector<double>& b)
{
    const auto n = static_cast<double> (a.size());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;

    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }

    return saa > 0 && sbb > 0 ? sab / std::sqrt (saa * sbb) : 0.0;
}

struct Result
{
    double correlation = 0.0;
    double energyInside = 0.0;      // fraction of the energy inside the object's Hz range
    double leftHalfEarly = 0.0;     // correlation over the first half of the object's time span
};

/** Plays one pass of `document` and compares what an analyser sees with the canvas, over the
    rectangle of object `objectIndex` (or the whole canvas for -1). */
Result measure (const ArtDocument& document, int objectIndex, int scaleIndex, float density, const char* label,
                int gridH = 64)
{
    FreqArtProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, 512);
    processor.prepareToPlay (sampleRate, 512);

    setParameter (processor, params::id::scale, static_cast<float> (scaleIndex));
    setParameter (processor, params::id::density, density);
    setParameter (processor, params::id::tempoSource, 1.0f);
    setParameter (processor, params::id::bpm, 120.0f);
    setParameter (processor, params::id::length, 4.0f);   // 4 bars = 8 s at 120 BPM
    processor.setDocument (document);
    processor.rebuildPlanIfNeeded();

    const auto passSeconds = processor.getLengthSeconds();
    processor.setPreview (true);
    const auto audio = render (processor, static_cast<int> (passSeconds * sampleRate), 512);
    const auto spectrogram = analyse (audio);

    const auto scale = processor.getScale();
    Canvas canvas;
    Compositor compositor;
    compositor.render (document, scale, canvas);

    auto area = juce::Rectangle<float> (0.0f, 0.0f, 1.0f, 1.0f);
    HzRange hz { scale.minHz, scale.maxHz };

    if (objectIndex >= 0)
    {
        const auto& object = document.objects[static_cast<size_t> (objectIndex)];
        area = objectRectNorm (object, scale).getIntersection ({ 0.0f, 0.0f, 1.0f, 1.0f });
        hz = { object.lowHz, object.highHz };
    }

    // Compare on a grid inside the area, keeping clear of the very first and last frames.
    constexpr int gridW = 128;
    std::vector<double> heard, drawn, heardLeft, drawnLeft;

    for (int gy = 0; gy < gridH; ++gy)
    {
        const auto yNorm = area.getY() + (static_cast<float> (gy) + 0.5f) / gridH * area.getHeight();
        const auto cellHeight = static_cast<double> (area.getHeight()) / gridH;
        const auto highHz = scale.toHz (1.0 - static_cast<double> (yNorm) + cellHeight / 2.0);
        const auto lowHz = scale.toHz (1.0 - static_cast<double> (yNorm) - cellHeight / 2.0);
        const auto row = juce::jlimit (0, Canvas::height - 1, static_cast<int> (yNorm * Canvas::height));

        for (int gx = 0; gx < gridW; ++gx)
        {
            const auto xNorm = area.getX() + (static_cast<float> (gx) + 0.5f) / gridW * area.getWidth();
            const auto column = juce::jlimit (0, Canvas::width - 1, static_cast<int> (xNorm * Canvas::width));
            const auto seconds = static_cast<double> (xNorm) * passSeconds;
            const auto cellSeconds = static_cast<double> (area.getWidth()) / gridW * passSeconds;

            // Analyser view: dB clamped to a 60 dB window under the loudest point.
            const auto db = static_cast<double> (spectrogram.cell (seconds - cellSeconds / 2.0, seconds + cellSeconds / 2.0, lowHz, highHz));
            heard.push_back (db);
            drawn.push_back (static_cast<double> (canvas.get (column, row)));

            if (gx < gridW / 2)
            {
                heardLeft.push_back (db);
                drawnLeft.push_back (drawn.back());
            }
        }
    }

    const auto clampWindow = [] (std::vector<double>& values)
    {
        const auto top = *std::max_element (values.begin(), values.end());
        for (auto& v : values)
            v = juce::jmax (top - 60.0, v);
    };

    clampWindow (heard);
    clampWindow (heardLeft);

    Result result;
    result.correlation = pearson (heard, drawn);
    result.leftHalfEarly = pearson (heardLeft, drawnLeft);

    // Energy inside the object's band, over the frames inside its time span.
    double inside = 0.0, total = 0.0;
    const auto firstFrame = static_cast<size_t> (juce::jmax (0.0, (area.getX() * passSeconds * sampleRate) / hop));
    const auto lastFrame = juce::jmin (spectrogram.frames.size(),
                                       static_cast<size_t> ((area.getRight() * passSeconds * sampleRate - fftSize) / hop));
    // A little margin around the band for the window's skirts.
    const auto lowBin = hz.low * 0.94 * fftSize / sampleRate;
    const auto highBin = hz.high * 1.06 * fftSize / sampleRate;

    for (auto frame = firstFrame; frame < lastFrame; ++frame)
        for (size_t bin = 1; bin < spectrogram.frames[frame].size(); ++bin)
        {
            const auto power = std::pow (10.0, spectrogram.frames[frame][bin] / 10.0);
            total += power;
            if (static_cast<double> (bin) >= lowBin && static_cast<double> (bin) <= highBin)
                inside += power;
        }

    result.energyInside = total > 0.0 ? inside / total : 0.0;

    std::printf ("    %-34s correlation %.3f (left half %.3f), energy in band %.4f\n",
                 label, result.correlation, result.leftHalfEarly, result.energyInside);
    return result;
}

ArtDocument textIn (const juce::String& text, Band band, const FrequencyScale& scale = {})
{
    ArtDocument document;
    const auto range = bandRange (band, scale);
    auto object = ArtDocument::makeTextObject (text, static_cast<float> (range.low), static_cast<float> (range.high));
    object.startTime = 0.1f;
    object.endTime = 0.9f;
    document.objects.push_back (object);
    return document;
}

juce::Image ringOnWhite()
{
    // A dark ring and a dark square on a white page: Auto must pick Negative and draw them.
    juce::Image image (juce::Image::ARGB, 400, 200, true);
    juce::Graphics g (image);
    g.fillAll (juce::Colours::white);
    g.setColour (juce::Colours::black);
    g.drawEllipse (30.0f, 30.0f, 140.0f, 140.0f, 18.0f);
    g.fillRect (240, 50, 110, 100);
    return image;
}

} // namespace

FREQART_TEST (endToEndTextInTheHighs)
{
    const auto result = measure (textIn ("HI", Band::high), 0, 0, 256.0f, "text HI, highs, log, 256 lines");
    CHECK_GREATER (result.correlation, 0.6);
    CHECK_GREATER (result.leftHalfEarly, 0.6);
    CHECK_GREATER (result.energyInside, 0.95);
}

FREQART_TEST (endToEndTextInTheMids)
{
    const auto result = measure (textIn ("FREQ", Band::mid), 0, 0, 256.0f, "text FREQ, mids, log, 256 lines");
    CHECK_GREATER (result.correlation, 0.6);
    CHECK_GREATER (result.energyInside, 0.95);
}

FREQART_TEST (endToEndTextInTheLows)
{
    // The lows are merged into lines at least 25 Hz apart (about 8 lines between 40 and 250 Hz
    // at 256 lines), so it is compared on a grid as coarse as its lines. Each row must play on
    // its nearest line, so the energy stays in the band instead of sinking to the 20 Hz line
    // below it (which took about 17 % when merged lines played at the bottom of their rows).
    // Down here the analysis window's main lobe (+-23 Hz) reaches past the band's 6 % margin,
    // hence the lower bar than in the mids and highs.
    const auto result = measure (textIn ("LOW", Band::low), 0, 0, 256.0f, "text LOW, lows, log, 256 lines (8 rows)", 8);
    CHECK_GREATER (result.correlation, 0.4);
    CHECK_GREATER (result.energyInside, 0.9);
}

FREQART_TEST (endToEndPicture)
{
    ArtDocument document;
    const auto range = bandRange (Band::mid, FrequencyScale {});
    auto object = ArtDocument::makeImageObject (prepareImportedImage (ringOnWhite()),
                                                static_cast<float> (range.low), static_cast<float> (range.high));
    object.startTime = 0.1f;
    object.endTime = 0.9f;
    document.objects.push_back (object);

    const auto result = measure (document, 0, 0, 384.0f, "picture (Auto), mids, log, 384 lines");
    CHECK_GREATER (result.correlation, 0.6);
    CHECK_GREATER (result.energyInside, 0.95);
}

FREQART_TEST (endToEndLinearScale)
{
    FrequencyScale linear;
    linear.type = ScaleType::linear;
    const auto result = measure (textIn ("LIN", Band::high, linear), 0, 1, 256.0f, "text LIN, highs, linear, 256 lines");
    CHECK_GREATER (result.correlation, 0.6);
    CHECK_GREATER (result.energyInside, 0.95);
}

FREQART_TEST (endToEndDensityExtremes)
{
    // 32 lines over the whole spectrum leave about 7 in the highs: a deliberate scan-line look,
    // so it is compared on a grid as coarse as its lines.
    const auto sparse = measure (textIn ("HI", Band::high), 0, 0, 32.0f, "text HI, highs, log, 32 lines (7 rows)", 7);
    const auto dense = measure (textIn ("HI", Band::high), 0, 0, 1024.0f, "text HI, highs, log, 1024 lines");
    CHECK_GREATER (sparse.correlation, 0.4);
    CHECK_GREATER (dense.correlation, 0.6);
    CHECK_GREATER (dense.energyInside, 0.95);
}
