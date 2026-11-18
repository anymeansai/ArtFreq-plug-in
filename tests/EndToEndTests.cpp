// The product's promise, measured: the spectrogram of ArtFreq's output looks like the drawing.
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

#include <algorithm>
#include <complex>
#include <cstdio>
#include <numeric>

using namespace artfreq;
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
                int gridH = 64, int lowEndIndex = params::smoothLowEndIndex)
{
    ArtFreqProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, 512);
    processor.prepareToPlay (sampleRate, 512);

    setParameter (processor, params::id::lowEnd, static_cast<float> (lowEndIndex));
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

/** The look of the lows on a long analyser (Smooth against Stripes): one pass of a drawing
    analysed with 2^order points (Hann, hop 1024, about 47 frames a second), read on a log grid of
    rows between 30 and 400 Hz (each row the dB interpolated between the bins around it, as
    MiniMeters draws) and compared with the canvas over 5 % - 95 % of the length. */
struct LowsLook
{
    double fill = 0.0;        // drawn cells within 6 dB of the drawn cells' median
    double fidelity = 0.0;    // correlation with the canvas (dB clamped to 60 dB under the loudest)
    double pulseDb = 0.0;     // how much the drawn band's mean level moves from frame to frame (SD)
    double pulsePeakDb = 0.0; // the strongest regular pulse in it, 2 - 20 Hz (amplitude, dB)
};

LowsLook lowsLook (const ArtDocument& document, int lowEndIndex, int order, const char* label)
{
    ArtFreqProcessor processor;
    processor.setPlayConfigDetails (0, 2, sampleRate, 512);
    processor.prepareToPlay (sampleRate, 512);
    setParameter (processor, params::id::lowEnd, static_cast<float> (lowEndIndex));
    setParameter (processor, params::id::tempoSource, 1.0f);
    setParameter (processor, params::id::bpm, 120.0f);
    setParameter (processor, params::id::length, 4.0f);   // 4 bars = 8 s at 120 BPM
    setParameter (processor, params::id::repeat, 1.0f);   // looped, so every window is full
    processor.setDocument (document);
    processor.rebuildPlanIfNeeded();

    const auto passSeconds = processor.getLengthSeconds();
    const auto pass = static_cast<int> (passSeconds * sampleRate);
    processor.setPreview (true);
    const auto audio = render (processor, 2 * pass, 512);

    const auto scale = processor.getScale();
    Canvas canvas;
    Compositor compositor;
    compositor.render (document, scale, canvas);

    // The STFT of the second pass (the first one's start is the fade-in), windows centred on it.
    const auto size = 1 << order;
    const auto hopSize = 1024;
    juce::dsp::FFT fft (order);
    std::vector<float> window (static_cast<size_t> (size)), work (static_cast<size_t> (2 * size));

    for (int i = 0; i < size; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (size));

    // Static: Visual Studio's C++17 lambdas want local constexpr doubles captured.
    static constexpr int rows = 48;
    static constexpr double lowestHz = 30.0, highestHz = 400.0;
    const auto rowHz = [] (int row) { return lowestHz * std::pow (highestHz / lowestHz, (row + 0.5) / rows); };
    const auto binHz = sampleRate / size;

    struct Frame
    {
        double position = 0.0;     // in the pass, 0..1
        std::vector<double> db;    // per row
    };

    std::vector<Frame> frames;

    for (auto centre = static_cast<double> (pass); centre < 2.0 * pass; centre += hopSize)
    {
        const auto start = static_cast<int> (centre) - size / 2;

        if (start < 0 || start + size > static_cast<int> (audio.size()))
            continue;

        std::fill (work.begin(), work.end(), 0.0f);

        for (int i = 0; i < size; ++i)
            work[static_cast<size_t> (i)] = audio[static_cast<size_t> (start + i)] * window[static_cast<size_t> (i)];

        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        Frame frame;
        frame.position = (centre - pass) / pass;

        for (int row = 0; row < rows; ++row)
        {
            const auto bin = rowHz (row) / binHz;
            const auto below = static_cast<size_t> (bin);
            const auto fraction = bin - static_cast<double> (below);
            const auto dbOf = [&] (size_t b) { return static_cast<double> (juce::Decibels::gainToDecibels (work[b] / (size / 4.0f), -140.0f)); };
            frame.db.push_back (dbOf (below) + (dbOf (below + 1) - dbOf (below)) * fraction);
        }

        frames.push_back (std::move (frame));
    }

    // What was drawn at each cell, and where the drawing is solid (away from its edges).
    std::vector<double> heard, drawn, solid, bandLevel;

    for (const auto& frame : frames)
    {
        if (frame.position < 0.05 || frame.position > 0.95)
            continue;

        const auto column = juce::jlimit (0, Canvas::width - 1, static_cast<int> (frame.position * Canvas::width));
        double levelSum = 0.0;
        int levelCount = 0;

        for (int row = 0; row < rows; ++row)
        {
            const auto valueAt = [&] (double hz)
            {
                const auto canvasRow = juce::jlimit (0, Canvas::height - 1, static_cast<int> ((1.0 - scale.toNorm (hz)) * Canvas::height));
                return static_cast<double> (canvas.get (column, canvasRow));
            };

            const auto hz = rowHz (row);
            heard.push_back (frame.db[static_cast<size_t> (row)]);
            drawn.push_back (valueAt (hz));

            // Solid: drawn here and a sixth of an octave either way.
            if (valueAt (hz) > 0.99 && valueAt (hz * 0.89) > 0.99 && valueAt (hz * 1.12) > 0.99)
            {
                solid.push_back (frame.db[static_cast<size_t> (row)]);
                levelSum += std::pow (10.0, frame.db[static_cast<size_t> (row)] / 10.0);
                ++levelCount;
            }
        }

        if (levelCount > 0)
            bandLevel.push_back (10.0 * std::log10 (levelSum / levelCount));
    }

    LowsLook look;

    const auto top = *std::max_element (heard.begin(), heard.end());

    for (auto& v : heard)
        v = juce::jmax (top - 60.0, v);

    look.fidelity = pearson (heard, drawn);

    if (! solid.empty())
    {
        auto sorted = solid;
        std::sort (sorted.begin(), sorted.end());
        const auto median = sorted[sorted.size() / 2];
        look.fill = static_cast<double> (std::count_if (solid.begin(), solid.end(), [median] (double v) { return std::abs (v - median) <= 6.0; }))
                  / static_cast<double> (solid.size());
    }

    // The band's level over time: its spread, and its strongest pulse between 2 and 20 Hz (the
    // amplitude of a sine in the level, Hann-windowed DFT of the frame series).
    if (bandLevel.size() > 16)
    {
        const auto n = bandLevel.size();
        const auto mean = std::accumulate (bandLevel.begin(), bandLevel.end(), 0.0) / static_cast<double> (n);
        double squares = 0.0, windowSum = 0.0;

        for (const auto v : bandLevel)
            squares += (v - mean) * (v - mean);

        look.pulseDb = std::sqrt (squares / static_cast<double> (n));

        const auto framesPerSecond = sampleRate / hopSize;

        for (size_t i = 0; i < n; ++i)
            windowSum += 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * static_cast<double> (i) / static_cast<double> (n));

        for (auto hz = 2.0; hz <= 20.0; hz += 0.1)
        {
            std::complex<double> sum;

            for (size_t i = 0; i < n; ++i)
            {
                const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * static_cast<double> (i) / static_cast<double> (n));
                sum += w * (bandLevel[i] - mean) * std::polar (1.0, -juce::MathConstants<double>::twoPi * hz * static_cast<double> (i) / framesPerSecond);
            }

            look.pulsePeakDb = std::max (look.pulsePeakDb, 2.0 * std::abs (sum) / windowSum);
        }
    }

    std::printf ("    %-30s FFT %5d: fill %.2f, fidelity %.2f, band level SD %.2f dB, strongest pulse %.2f dB\n",
                 label, size, look.fill, look.fidelity, look.pulseDb, look.pulsePeakDb);
    return look;
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

ARTFREQ_TEST (endToEndTextInTheHighs)
{
    const auto result = measure (textIn ("HI", Band::high), 0, 0, 256.0f, "text HI, highs, log, 256 lines");
    CHECK_GREATER (result.correlation, 0.6);
    CHECK_GREATER (result.leftHalfEarly, 0.6);
    CHECK_GREATER (result.energyInside, 0.95);
}

ARTFREQ_TEST (endToEndTextInTheMids)
{
    const auto result = measure (textIn ("FREQ", Band::mid), 0, 0, 256.0f, "text FREQ, mids, log, 256 lines");
    CHECK_GREATER (result.correlation, 0.6);
    CHECK_GREATER (result.energyInside, 0.95);
}

ARTFREQ_TEST (endToEndTextInTheLows)
{
    // (Stripes: how the lows played before Smooth, kept as a choice. Smooth's own test follows.)
    // The lows are merged into lines at least 25 Hz apart (about 8 lines between 40 and 250 Hz
    // at 256 lines), so it is compared on a grid as coarse as its lines. Each row must play on
    // its nearest line, so the energy stays in the band instead of sinking to the 20 Hz line
    // below it (which took about 17 % when merged lines played at the bottom of their rows).
    // Down here the analysis window's main lobe (+-23 Hz) reaches past the band's 6 % margin,
    // hence the lower bar than in the mids and highs.
    const auto result = measure (textIn ("LOW", Band::low), 0, 0, 256.0f, "text LOW, lows, log, 256 lines (8 rows)", 8,
                                 params::stripesLowEndIndex);
    CHECK_GREATER (result.correlation, 0.4);
    CHECK_GREATER (result.energyInside, 0.9);
}

ARTFREQ_TEST (endToEndSmoothLowsFillWhereStripesShowGaps)
{
    // The customer's case: an analyser with a long FFT (MiniMeters at 8192 or 16384 points)
    // resolves the lows' lines, 25 Hz apart, into stripes with dark gaps. Smooth (the rising
    // comb) fills the drawing there and reads closer to it, and does not pulse instead: the
    // drawn band's level holds steady from frame to frame, with no regular beat.
    ArtDocument block;   // a solid block over 50 - 250 Hz, 10 % - 90 % of the length
    const FrequencyScale scale;

    for (int y = 0; y < ArtDocument::paintHeight; ++y)
    {
        const auto hz = scale.toHz (1.0 - (y + 0.5) / ArtDocument::paintHeight);

        if (hz >= 50.0 && hz <= 250.0)
            for (int x = ArtDocument::paintWidth / 10; x < ArtDocument::paintWidth * 9 / 10; ++x)
                block.paint[static_cast<size_t> (y * ArtDocument::paintWidth + x)] = 255;
    }

    const auto text = textIn ("LOW", Band::low);

    for (const auto order : { 13, 14 })
    {
        const auto smoothBlock = lowsLook (block, params::smoothLowEndIndex, order, "block 50 - 250 Hz, Smooth");
        const auto stripesBlock = lowsLook (block, params::stripesLowEndIndex, order, "block 50 - 250 Hz, Stripes");
        const auto smoothText = lowsLook (text, params::smoothLowEndIndex, order, "text LOW, Smooth");
        const auto stripesText = lowsLook (text, params::stripesLowEndIndex, order, "text LOW, Stripes");

        CHECK_LESS (stripesBlock.fill, 0.6);
        CHECK_GREATER (smoothBlock.fill, 0.85);
        CHECK_GREATER (smoothBlock.fidelity, stripesBlock.fidelity + 0.1);
        CHECK_GREATER (smoothText.fidelity, stripesText.fidelity + 0.1);
        CHECK_GREATER (smoothText.fill, stripesText.fill);

        // A solid block's level is steady: no regular pulse (the comb's sines pass each height ten
        // times a second, out of step with each other). The text's level follows its letters;
        // the comb adds no pulse of its own to that.
        CHECK_LESS (smoothBlock.pulseDb, 1.0);
        CHECK_LESS (smoothBlock.pulsePeakDb, 0.5);
        CHECK_LESS (smoothText.pulsePeakDb, stripesText.pulsePeakDb + 0.25);
    }
}

ARTFREQ_TEST (endToEndPicture)
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

ARTFREQ_TEST (endToEndLinearScale)
{
    FrequencyScale linear;
    linear.type = ScaleType::linear;
    const auto result = measure (textIn ("LIN", Band::high, linear), 0, 1, 256.0f, "text LIN, highs, linear, 256 lines");
    CHECK_GREATER (result.correlation, 0.6);
    CHECK_GREATER (result.energyInside, 0.95);
}

ARTFREQ_TEST (endToEndDensityExtremes)
{
    // 32 lines over the whole spectrum leave about 7 in the highs: a deliberate scan-line look,
    // so it is compared on a grid as coarse as its lines.
    const auto sparse = measure (textIn ("HI", Band::high), 0, 0, 32.0f, "text HI, highs, log, 32 lines (7 rows)", 7);
    const auto dense = measure (textIn ("HI", Band::high), 0, 0, 1024.0f, "text HI, highs, log, 1024 lines");
    CHECK_GREATER (sparse.correlation, 0.4);
    CHECK_GREATER (dense.correlation, 0.6);
    CHECK_GREATER (dense.energyInside, 0.95);
}
