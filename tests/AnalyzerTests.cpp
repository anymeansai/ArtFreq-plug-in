// LiveAnalyzer: the spectrum columns behind the live spectrogram view.

#include "TestFramework.h"
#include "TestPlans.h"

#include "dsp/LiveAnalyzer.h"

#include <vector>

using artfreq::LiveAnalyzer;

namespace
{

constexpr double sampleRate = 48000.0;

std::vector<float> sine (double hz, double amplitude, int numSamples)
{
    std::vector<float> out (static_cast<size_t> (numSamples));

    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] = static_cast<float> (amplitude * std::sin (test::twoPi * hz * i / sampleRate));

    return out;
}

/** Pushes the signal in blocks and returns the last column (and how many there were). */
std::vector<float> analyse (LiveAnalyzer& analyzer, const std::vector<float>& signal, int& numColumns)
{
    std::vector<float> last;
    numColumns = 0;

    for (size_t done = 0; done < signal.size(); done += 480)
    {
        analyzer.push (signal.data() + done, static_cast<int> (std::min<size_t> (480, signal.size() - done)));
        numColumns += analyzer.process ([&] (const float* magnitudes, int numBins)
        {
            last.assign (magnitudes, magnitudes + numBins);
        });
    }

    return last;
}

} // namespace

ARTFREQ_TEST (analyzerFullScaleSineReadsZeroDb)
{
    LiveAnalyzer analyzer;
    analyzer.prepare (sampleRate);
    CHECK_NEAR (analyzer.getSampleRate(), sampleRate, 0.0);

    // 1 kHz falls between two bins: the strongest bin reads within the window's scalloping
    // (Blackman-Harris: up to 0.8 dB) of 0 dB.
    int columns = 0;
    auto column = analyse (analyzer, sine (1000.0, 1.0, 16384), columns);
    CHECK (analyzer.getFftSize() == LiveAnalyzer::defaultFftSize);
    CHECK (columns == 16384 / analyzer.getHopSize());
    CHECK (column.size() == static_cast<size_t> (analyzer.getNumBins()));

    const auto binHz = sampleRate / analyzer.getFftSize();
    const auto strongest = static_cast<size_t> (std::max_element (column.begin(), column.end()) - column.begin());
    CHECK_NEAR (static_cast<double> (strongest) * binHz, 1000.0, binHz);
    CHECK_NEAR (column[strongest], 0.0, 1.5);
    std::printf ("    1 kHz full-scale sine: strongest bin %.2f dBFS (bin %d)\n", static_cast<double> (column[strongest]),
                 static_cast<int> (strongest));

    // Centred on a bin it reads 0 dB exactly, and half scale reads -6 dB.
    LiveAnalyzer centred;
    centred.prepare (sampleRate);
    column = analyse (centred, sine (100.0 * binHz, 1.0, 16384), columns);
    CHECK_NEAR (column[100], 0.0, 0.01);

    centred.prepare (sampleRate);
    column = analyse (centred, sine (100.0 * binHz, 0.5, 16384), columns);
    CHECK_NEAR (column[100], -6.02, 0.02);

    // Far from the tone the spectrum is deep down (the window's side lobes: 92 dB).
    CHECK_LESS (column[300], -90.0f);

    // The FFT size follows the analyser's (Settings): a column per sixteenth of the window.
    LiveAnalyzer resized;
    resized.prepare (sampleRate);
    resized.setFftSize (16384);
    column = analyse (resized, sine (1000.0, 1.0, 32768), columns);
    CHECK (resized.getFftSize() == 16384);
    CHECK (column.size() == 8193u);
    CHECK (columns == 32768 / 1024);
}

ARTFREQ_TEST (analyzerSilenceReadsTheFloor)
{
    LiveAnalyzer analyzer;
    analyzer.prepare (sampleRate);

    int columns = 0;
    const auto column = analyse (analyzer, std::vector<float> (8192, 0.0f), columns);
    CHECK (columns == 16);
    CHECK (*std::max_element (column.begin(), column.end()) <= artfreq::SpectrumAnalyser::floorDb);
    CHECK (*std::min_element (column.begin(), column.end()) >= artfreq::SpectrumAnalyser::floorDb);
}

ARTFREQ_TEST (analyzerDropsWhatDoesNotFit)
{
    LiveAnalyzer analyzer;
    analyzer.prepare (sampleRate);
    analyzer.setActive (true);
    CHECK (analyzer.isActive());

    // Far more than the FIFO holds, with nobody reading: the rest is dropped, nothing breaks.
    const auto loud = sine (440.0, 0.5, 4096);

    for (int i = 0; i < 40; ++i)
        analyzer.push (loud.data(), static_cast<int> (loud.size()));

    const auto columns = analyzer.process ({});
    CHECK (columns <= LiveAnalyzer::fifoSize / analyzer.getHopSize());
    CHECK (columns >= (LiveAnalyzer::fifoSize - 4096) / analyzer.getHopSize());

    // And it carries on afterwards.
    analyzer.push (loud.data(), static_cast<int> (loud.size()));
    CHECK (analyzer.process ({}) == 4096 / analyzer.getHopSize());
    CHECK (analyzer.process ({}) == 0);
}
