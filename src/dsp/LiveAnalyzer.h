#pragma once

// The analysis behind the live spectrogram view: the audio thread pushes what ArtFreq outputs,
// the message thread turns it into spectrum columns, analysed as the user's analyser does (the
// FFT size from Settings, a Hann window: SpectrumAnalyser), so the live view and the canvas show
// the same sound alike.

#include "SpectrumAnalysis.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace artfreq
{

class LiveAnalyzer
{
public:
    static constexpr int defaultFftSize = 8192;
    static constexpr int fifoSize = 1 << 16;

    /** A column every sixteenth of the window: the view scrolls in steps of about 5 to 21 ms
        (4096 to 16384 points at 48 kHz), and the window blurs far more than that. */
    static constexpr int hopsPerWindow = 16;

    LiveAnalyzer();

    /** Message thread (or prepareToPlay): resets the analysis for a sample rate. */
    void prepare (double sampleRate);

    /** Message thread: the FFT size to analyse with from the next process() on (a power of two,
        1024 to 32768); a new size starts the analysis afresh. */
    void setFftSize (int size) noexcept;

    /** Message thread: what process() uses (after the last one). */
    int getFftSize() const noexcept { return fftSize; }
    int getNumBins() const noexcept { return fftSize / 2 + 1; }
    int getHopSize() const noexcept { return fftSize / hopsPerWindow; }

    /** The editor turns this on while the live view is visible; the processor pushes only
        while it is on. */
    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive); }
    bool isActive() const noexcept { return active.load(); }

    /** Audio thread: wait-free; drops samples if the FIFO is full. */
    void push (const float* mono, int numSamples) noexcept;

    /** Message thread: for every complete hop available, analyses the last getFftSize() samples
        and calls `onColumn` with getNumBins() levels in dBFS (a full-scale sine reads 0 dB;
        silence SpectrumAnalyser::floorDb). Returns the number of columns. */
    int process (const std::function<void (const float* magnitudesDb, int numBins)>& onColumn);

    double getSampleRate() const noexcept { return sampleRate.load(); }

private:
    void configure (int size);
    void computeColumn();

    std::atomic<bool> active { false };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> wantedFftSize { defaultFftSize };
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;

    // message thread
    int fftSize = 0;
    std::unique_ptr<SpectrumAnalyser> analyser;
    std::vector<float> history;      // the last fftSize samples, a ring
    int historyPosition = 0;         // the oldest sample in `history` (and where the next goes)
    int samplesSinceColumn = 0;
    std::vector<float> frame, magnitudes;
};

} // namespace artfreq
