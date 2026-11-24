#include "SpectrumAnalysis.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace artfreq
{

AnalyserView AnalyserView::sanitised() const noexcept
{
    AnalyserView v = *this;
    v.fftSize = juce::isPowerOfTwo (v.fftSize) ? juce::jlimit (1024, 32768, v.fftSize) : 8192;

    if (! std::isfinite (v.floorDb))
        v.floorDb = AnalyserView().floorDb;

    if (! std::isfinite (v.ceilingDb))
        v.ceilingDb = 0.0f;

    v.floorDb = juce::jlimit (-200.0f, 40.0f, v.floorDb);

    // A range turned inside out (the floor typed above the top) keeps its floor.
    if (v.ceilingDb < v.floorDb + minSpanDb)
        v.ceilingDb = v.floorDb + minSpanDb;

    return v;
}

//==============================================================================
SpectrumAnalyser::SpectrumAnalyser (int fftSize)
    : size (juce::isPowerOfTwo (fftSize) ? juce::jlimit (256, 65536, fftSize) : 8192),
      fft (juce::roundToInt (std::log2 (size))),
      window (static_cast<size_t> (size)),
      work (static_cast<size_t> (size) * 2, 0.0f)
{
    // Blackman-Harris, four terms (periodic): see analysisWindow in the header.
    double sum = 0.0;

    for (int n = 0; n < size; ++n)
    {
        const auto phase = juce::MathConstants<double>::twoPi * n / size;
        auto w = 0.0;

        for (size_t k = 0; k < std::size (analysisWindow); ++k)
            w += (k % 2 == 0 ? 1.0 : -1.0) * static_cast<double> (analysisWindow[k]) * std::cos (static_cast<double> (k) * phase);

        window[static_cast<size_t> (n)] = static_cast<float> (w);
        sum += w;
    }

    // A sine of amplitude A centred on a bin gives a magnitude of A * sum (w) / 2 there.
    toAmplitude = static_cast<float> (2.0 / sum);
}

void SpectrumAnalyser::analyse (const float* samples, float* binsDb) noexcept
{
    for (int n = 0; n < size; ++n)
        work[static_cast<size_t> (n)] = samples[n] * window[static_cast<size_t> (n)];

    std::fill (work.begin() + size, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);

    // 20 log10 (x) as ln (x) * 20 / ln (10): one transcendental per bin.
    constexpr float dbPerNeper = 8.6858896f;
    constexpr float quietest = 1.0e-8f;   // -160 dB

    for (int bin = 0; bin < size / 2 + 1; ++bin)
    {
        const auto amplitude = work[static_cast<size_t> (bin)] * toAmplitude;
        binsDb[bin] = amplitude > quietest ? dbPerNeper * std::log (amplitude) : floorDb;
    }
}

//==============================================================================
void SpectrumRows::prepare (const FrequencyScale& scale, int height, double sampleRate, int fftSize, float slopeDbPerOctave)
{
    rows.assign (static_cast<size_t> (juce::jmax (0, height)), {});

    const auto s = scale.sanitised();
    const auto numBins = fftSize / 2 + 1;
    const auto lastBin = numBins - 1;
    const auto binsPerHz = fftSize / sampleRate;
    const auto nyquist = sampleRate * 0.5;
    const auto h = static_cast<double> (height);

    for (int y = 0; y < height; ++y)
    {
        auto& row = rows[static_cast<size_t> (y)];
        const auto centreHz = s.toHz (1.0 - (y + 0.5) / h);
        const auto lowHz = s.toHz (1.0 - (y + 1.0) / h);
        const auto highHz = s.toHz (1.0 - y / h);

        if (! (centreHz > 0.0) || centreHz >= nyquist)
            continue;

        const auto centre = centreHz * binsPerHz;
        row.valid = true;
        row.below = juce::jlimit (0, lastBin - 1, static_cast<int> (std::floor (centre)));
        row.fraction = static_cast<float> (juce::jlimit (0.0, 1.0, centre - row.below));
        row.first = juce::jlimit (0, lastBin + 1, static_cast<int> (std::ceil (lowHz * binsPerHz)));
        row.last = juce::jlimit (-1, lastBin, static_cast<int> (std::ceil (highHz * binsPerHz)) - 1);
        row.offsetDb = slopeDbPerOctave * static_cast<float> (std::log2 (centreHz / slopePivotHz));
    }
}

void SpectrumRows::apply (const float* binsDb, float* rowsDb) const noexcept
{
    for (size_t y = 0; y < rows.size(); ++y)
    {
        const auto& row = rows[y];

        if (! row.valid)
        {
            rowsDb[y] = SpectrumAnalyser::floorDb;
            continue;
        }

        auto value = binsDb[row.below] + (binsDb[row.below + 1] - binsDb[row.below]) * row.fraction;

        for (auto bin = row.first; bin <= row.last; ++bin)
            value = std::max (value, binsDb[bin]);

        rowsDb[y] = value + row.offsetDb;
    }
}

} // namespace artfreq
