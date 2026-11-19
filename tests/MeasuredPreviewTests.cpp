// The measured preview: the real sound, analysed as the user's analyser will show it.

#include "TestFramework.h"
#include "TestPlans.h"

#include "PluginProcessor.h"
#include "dsp/SpectrumAnalysis.h"
#include "dsp/StandInSong.h"
#include "dsp/WavExporter.h"
#include "ui/MeasuredPreview.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using namespace artfreq;

namespace
{
    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;

        std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    double secondsSince (std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    }
} // namespace

ARTFREQ_TEST (spectrumAnalyserReadsASineAtItsLevel)
{
    // A full-scale sine on a bin reads 0 dB; half of it -6 dB; between bins, the rows read the
    // spectrum interpolated, never brighter than the sine.
    constexpr double rate = 48000.0;
    constexpr int size = 8192;
    SpectrumAnalyser analyser (size);
    std::vector<float> signal (size), bins (static_cast<size_t> (analyser.getNumBins()));
    const auto bin = 200;
    const auto hz = bin * rate / size;

    for (int n = 0; n < size; ++n)
        signal[static_cast<size_t> (n)] = 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * hz * n / rate));

    analyser.analyse (signal.data(), bins.data());
    CHECK_NEAR (bins[bin], -6.02f, 0.05f);
    CHECK_LESS (bins[bin + 3], -40.0f);

    SpectrumRows rows;
    FrequencyScale scale;
    rows.prepare (scale, 600, rate, size, 0.0f);
    std::vector<float> rowDb (600);
    rows.apply (bins.data(), rowDb.data());
    const auto loudest = *std::max_element (rowDb.begin(), rowDb.end());
    CHECK_NEAR (loudest, -6.02f, 0.3f);

    // The slope tilts the picture about 1 kHz: +3 dB/oct reads 3 dB brighter an octave up.
    SpectrumRows sloped;
    sloped.prepare (scale, 600, rate, size, 3.0f);
    std::vector<float> slopedDb (600);
    sloped.apply (bins.data(), slopedDb.data());
    const auto peakRow = static_cast<size_t> (std::max_element (rowDb.begin(), rowDb.end()) - rowDb.begin());
    CHECK_NEAR (slopedDb[peakRow] - rowDb[peakRow], 3.0f * static_cast<float> (std::log2 (hz / 1000.0)), 0.2f);
}

ARTFREQ_TEST (analyserViewIsSanitised)
{
    AnalyserView view;
    view.fftSize = 5000;
    view.floorDb = -30.0f;
    view.ceilingDb = -40.0f;
    const auto v = view.sanitised();
    CHECK (v.fftSize == 8192);
    CHECK_NEAR (v.ceilingDb - v.floorDb, AnalyserView::minSpanDb, 0.001f);
    CHECK (colourLevelFor (-100.0f, -100.0f, 0.0f) == 0);
    CHECK (colourLevelFor (0.0f, -100.0f, 0.0f) == 255);
    CHECK (colourLevelFor (12.0f, -100.0f, 0.0f) == 255);
    CHECK (colourLevelFor (-50.0f, -100.0f, 0.0f) == 128);
}

ARTFREQ_TEST (standInSongHasTheTexturesLevels)
{
    // The stand-in, made into sound and analysed at 4096 points, reads the levels the fast
    // preview draws (the median over a few seconds, at every height of the scale).
    constexpr double rate = 48000.0;
    constexpr int size = 4096;
    const FrequencyScale scale;
    const auto passSamples = static_cast<int64_t> (8.0 * rate);
    StandInSong song (rate, passSamples, 0);
    std::vector<float> audio (static_cast<size_t> (4.0 * rate));
    song.render (audio.data(), static_cast<int> (audio.size()));

    SpectrumAnalyser analyser (size);
    std::vector<float> bins (static_cast<size_t> (analyser.getNumBins()));
    std::vector<double> differences, lows, highs;
    constexpr double edges[] { 20.0, 60.0, 120.0, 250.0, 500.0, 1000.0, 2000.0 };
    std::vector<double> byRegion[6];

    for (int start = 0; start + size <= static_cast<int> (audio.size()); start += 777)
    {
        analyser.analyse (audio.data() + start, bins.data());
        const auto x = (start + size / 2) / static_cast<double> (passSamples);

        for (int bin = 2; bin < analyser.getNumBins() - 1; bin += 3)
        {
            const auto hz = bin * rate / size;

            if (hz < scale.minHz || hz > scale.maxHz)
                continue;

            const auto d = static_cast<double> (bins[static_cast<size_t> (bin)]) - standInSongTextureDb (x, hz);
            differences.push_back (d);
            (hz < 300.0 ? lows : highs).push_back (d);

            for (size_t r = 0; r < 6; ++r)
                if (hz >= edges[r] && hz < edges[r + 1])
                    byRegion[r].push_back (d);
        }
    }

    std::printf ("    stand-in at FFT 4096, measured - texture: median %.2f dB (lows %.2f, highs %.2f); 20-60-120-250-500-1k-2k Hz:",
                 median (differences), median (lows), median (highs));

    for (const auto& region : byRegion)
        std::printf (" %.1f", median (region));

    std::printf ("\n");
    CHECK_NEAR (median (differences), 0.0, 1.0);
    CHECK_NEAR (median (lows), 0.0, 2.0);
    CHECK_NEAR (median (highs), 0.0, 2.0);

    // The same timeline gives the same noise, wherever the render starts.
    StandInSong later (rate, passSamples, 12345);
    std::vector<float> part (2000);
    later.render (part.data(), static_cast<int> (part.size()));
    auto same = true;

    for (size_t i = 0; i < part.size(); ++i)
        same = same && std::abs (part[i] - audio[12345 + i]) < 1.0e-5f;

    CHECK (same);
}

ARTFREQ_TEST (measuredPreviewShowsTheRealLevels)
{
    // A steady band at 1 - 2 kHz: the measured preview reads it at the level an analyser shows
    // for its loudest line (the plan's amplitude, Level included), dark above and below.
    Canvas canvas;
    const FrequencyScale scale;
    const auto rowOfHz = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * Canvas::height); };

    for (int y = rowOfHz (2000.0); y < rowOfHz (1000.0); ++y)
        for (int x = 0; x < Canvas::width; ++x)
            canvas.at (x, y) = 1.0f;

    PlanSettings settings;
    settings.scale = scale;
    settings.density = 1024;
    std::shared_ptr<ArtPlan> plan (buildPlan (canvas, settings).release());
    plan->version = 1;

    ui::MeasureRequest request;
    request.plan = plan;
    request.outputGain = 0.5f;
    request.passSeconds = 4.0;
    request.scale = scale;
    request.width = 300;
    request.height = 400;

    const auto started = std::chrono::steady_clock::now();
    const auto result = ui::MeasuredPreview::renderNow (request);
    std::printf ("    4 s pass, density 1024, 300 x 400: %.0f ms\n", secondsSince (started) * 1000.0);
    CHECK (result != nullptr);

    if (result == nullptr)
        return;

    auto loudest = 0.0f;

    for (int line = 0; line < plan->numLines; ++line)
        loudest = std::max (loudest, plan->amplitude (Canvas::width / 2, line));

    const auto expected = 20.0f * std::log10 (loudest * request.outputGain);
    const auto& a = *result->analysis;
    const auto rowAt = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * request.height); };
    const auto middle = a.frames / 2;
    auto bandTop = -200.0f;

    for (int y = rowAt (1900.0); y <= rowAt (1100.0); ++y)
        bandTop = std::max (bandTop, a.outputDb (y, middle));

    std::printf ("    band reads %.1f dB, its loudest line %.1f dB\n", bandTop, expected);
    CHECK_NEAR (bandTop, expected, 3.0f);
    CHECK_LESS (a.outputDb (rowAt (5000.0), middle), expected - 60.0f);
    CHECK_LESS (a.outputDb (rowAt (200.0), middle), expected - 60.0f);
}
