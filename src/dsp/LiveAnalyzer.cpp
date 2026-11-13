#include "LiveAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace freqart
{

namespace
{
    constexpr float floorDb = -140.0f;
}

LiveAnalyzer::LiveAnalyzer()
    : fifoData (static_cast<size_t> (fifoSize), 0.0f),
      history (static_cast<size_t> (fftSize), 0.0f),
      window (static_cast<size_t> (fftSize), 0.0f),
      work (static_cast<size_t> (2 * fftSize), 0.0f),
      magnitudes (static_cast<size_t> (numBins), floorDb)
{
    // 4-term Blackman-Harris (periodic): sidelobes below -92 dB, so quiet lines next to loud
    // ones stay visible, as in the analysers the art is made for.
    constexpr double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    constexpr double twoPi = 6.283185307179586476925286766559;
    double sum = 0.0;

    for (int i = 0; i < fftSize; ++i)
    {
        const auto x = twoPi * i / fftSize;
        const auto w = a0 - a1 * std::cos (x) + a2 * std::cos (2.0 * x) - a3 * std::cos (3.0 * x);
        window[static_cast<size_t> (i)] = static_cast<float> (w);
        sum += w;
    }

    // A sine of peak amplitude A centred on a bin gives a magnitude of A * sum (w) / 2 there.
    magnitudeScale = static_cast<float> (2.0 / sum);
}

void LiveAnalyzer::prepare (double newSampleRate)
{
    sampleRate.store (newSampleRate > 0.0 ? newSampleRate : 48000.0);
    fifo.reset();
    std::fill (history.begin(), history.end(), 0.0f);
    historyPosition = 0;
    samplesSinceColumn = 0;
}

void LiveAnalyzer::push (const float* mono, int numSamples) noexcept
{
    if (mono == nullptr || numSamples <= 0)
        return;

    // Takes what fits; the rest is dropped (the view simply misses a moment).
    const auto scope = fifo.write (numSamples);

    if (scope.blockSize1 > 0)
        std::copy (mono, mono + scope.blockSize1, fifoData.begin() + scope.startIndex1);

    if (scope.blockSize2 > 0)
        std::copy (mono + scope.blockSize1, mono + scope.blockSize1 + scope.blockSize2, fifoData.begin() + scope.startIndex2);
}

int LiveAnalyzer::process (const std::function<void (const float* magnitudesDb, int numBins)>& onColumn)
{
    int columns = 0;
    auto ready = fifo.getNumReady();

    while (ready > 0)
    {
        const auto take = std::min (ready, hopSize - samplesSinceColumn);

        {
            const auto scope = fifo.read (take);

            scope.forEach ([this] (int index)
            {
                history[static_cast<size_t> (historyPosition)] = fifoData[static_cast<size_t> (index)];
                historyPosition = (historyPosition + 1) & (fftSize - 1);
            });
        }

        ready -= take;
        samplesSinceColumn += take;

        if (samplesSinceColumn >= hopSize)
        {
            samplesSinceColumn = 0;
            computeColumn();
            ++columns;

            if (onColumn)
                onColumn (magnitudes.data(), numBins);
        }
    }

    return columns;
}

void LiveAnalyzer::computeColumn()
{
    for (int i = 0; i < fftSize; ++i)
    {
        const auto index = static_cast<size_t> ((historyPosition + i) & (fftSize - 1));
        work[static_cast<size_t> (i)] = history[index] * window[static_cast<size_t> (i)];
    }

    std::fill (work.begin() + fftSize, work.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (work.data(), true);

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto amplitude = work[static_cast<size_t> (bin)] * magnitudeScale;
        magnitudes[static_cast<size_t> (bin)] = amplitude > 1.0e-7f ? std::max (floorDb, 20.0f * std::log10 (amplitude)) : floorDb;
    }
}

} // namespace freqart
