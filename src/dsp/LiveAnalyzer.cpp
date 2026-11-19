#include "LiveAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace artfreq
{

LiveAnalyzer::LiveAnalyzer()
    : fifoData (static_cast<size_t> (fifoSize), 0.0f)
{
    configure (defaultFftSize);
}

void LiveAnalyzer::configure (int size)
{
    fftSize = size;
    analyser = std::make_unique<SpectrumAnalyser> (size);
    history.assign (static_cast<size_t> (size), 0.0f);
    frame.assign (static_cast<size_t> (size), 0.0f);
    magnitudes.assign (static_cast<size_t> (size / 2 + 1), SpectrumAnalyser::floorDb);
    historyPosition = 0;
    samplesSinceColumn = 0;
}

void LiveAnalyzer::prepare (double newSampleRate)
{
    sampleRate.store (newSampleRate > 0.0 ? newSampleRate : 48000.0);
    fifo.reset();
    std::fill (history.begin(), history.end(), 0.0f);
    historyPosition = 0;
    samplesSinceColumn = 0;
}

void LiveAnalyzer::setFftSize (int size) noexcept
{
    wantedFftSize.store (juce::isPowerOfTwo (size) ? juce::jlimit (1024, 32768, size) : defaultFftSize);
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
    if (const auto wanted = wantedFftSize.load(); wanted != fftSize)
        configure (wanted);

    int columns = 0;
    const auto hop = getHopSize();
    auto ready = fifo.getNumReady();

    while (ready > 0)
    {
        const auto take = std::min (ready, hop - samplesSinceColumn);

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

        if (samplesSinceColumn >= hop)
        {
            samplesSinceColumn = 0;
            computeColumn();
            ++columns;

            if (onColumn)
                onColumn (magnitudes.data(), getNumBins());
        }
    }

    return columns;
}

void LiveAnalyzer::computeColumn()
{
    // Oldest first.
    for (int i = 0; i < fftSize; ++i)
        frame[static_cast<size_t> (i)] = history[static_cast<size_t> ((historyPosition + i) & (fftSize - 1))];

    analyser->analyse (frame.data(), magnitudes.data());
}

} // namespace artfreq
