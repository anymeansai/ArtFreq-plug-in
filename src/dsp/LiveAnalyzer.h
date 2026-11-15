#pragma once

// The analysis behind the live spectrogram view: the audio thread pushes what ArtFreq outputs,
// the message thread turns it into spectrum columns.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <functional>
#include <vector>

namespace artfreq
{

class LiveAnalyzer
{
public:
    static constexpr int fftOrder = 12;              // 4096 points
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numBins = fftSize / 2 + 1;
    static constexpr int hopSize = 512;
    static constexpr int fifoSize = 1 << 16;

    LiveAnalyzer();

    /** Message thread (or prepareToPlay): resets the analysis for a sample rate. */
    void prepare (double sampleRate);

    /** The editor turns this on while the live view is visible; the processor pushes only
        while it is on. */
    void setActive (bool shouldBeActive) noexcept { active.store (shouldBeActive); }
    bool isActive() const noexcept { return active.load(); }

    /** Audio thread: wait-free; drops samples if the FIFO is full. */
    void push (const float* mono, int numSamples) noexcept;

    /** Message thread: for every complete hop available, computes a Blackman-Harris windowed
        FFT of the last fftSize samples and calls `onColumn` with numBins magnitudes in dBFS
        (a full-scale sine reads 0 dB; floor -140 dB). Returns the number of columns. */
    int process (const std::function<void (const float* magnitudesDb, int numBins)>& onColumn);

    double getSampleRate() const noexcept { return sampleRate.load(); }

private:
    std::atomic<bool> active { false };
    std::atomic<double> sampleRate { 48000.0 };
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;
    std::vector<float> history;      // last fftSize samples, a ring
    int historyPosition = 0;         // the oldest sample in `history` (and where the next goes)
    int samplesSinceColumn = 0;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, work, magnitudes;
    float magnitudeScale = 1.0f;     // turns an FFT magnitude into a sine's peak amplitude

    void computeColumn();
};

} // namespace artfreq
