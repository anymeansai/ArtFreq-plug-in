#pragma once

// The baseline carving engine: a plain short-time Fourier transform, the reference the other
// engines are measured against (tools/CarveLab.cpp).
//
// Frames of 2048 samples (at 44.1 / 48 kHz; 4096 at 88.2 / 96 kHz and 8192 above, so a frame
// always lasts about 43 ms), sqrt-Hann analysis and synthesis windows, 75 % overlap (a hop of a
// quarter frame), which add back up to the input exactly where nothing is carved. Each frame
// takes the drawing's column at the frame's centre and scales every bin by
// 10 ^ (depth * amount / 20), with amount = gate x mask (Carve) or gate x (1 - mask) (Stencil),
// the mask sampled at the bin's centre frequency. No smoothing across bins or frames.
//
// Latency: one frame (2048 samples at 48 kHz, 42.7 ms). Frames that change nothing (gate shut,
// an empty column in Carve, depth 0) skip the FFT, and once every frame overlapping a hop was
// such a frame the hop is the delayed input itself, bit for bit.

#include "SpectralCarver.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace artfreq
{

class BaselineCarver final : public SpectralCarver
{
public:
    const char* getName() const noexcept override { return "baseline: STFT 2048, sqrt-Hann, 75 % overlap, mask at bin centres"; }
    int getLatencySamples() const noexcept override { return fftSize; }

    int getFftSize() const noexcept { return fftSize; }
    int getHopSize() const noexcept { return hop; }

protected:
    void prepareCarver() override;
    void resetCarver() noexcept override;
    void planChanged() noexcept override;
    void carve (float* const* channels, int numChannels, int numSamples,
                const Timeline& timeline, const CarveSettings& settings) noexcept override;

private:
    struct HopStart
    {
        double fraction = 0.0;
        float gate = 0.0f;
    };

    void runFrame (int numChannels, const CarveSettings& settings) noexcept;
    bool computeGains (const HopStart& centre, const CarveSettings& settings) noexcept;
    void lookUpBinRows() noexcept;

    int fftOrder = 11;
    int fftSize = 2048;
    int hop = 512;
    int numBins = 1025;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> window;        // sqrt-Hann, periodic: analysis and synthesis
    std::vector<float> untouched;     // window^2 / 2: what a frame that changes nothing adds
    std::vector<float> fftBuffer;     // 2 x fftSize, for the real-only transforms
    std::vector<float> gains;         // per bin
    std::vector<double> binRows;      // per bin: its centre frequency on the plan's rows

    struct Channel
    {
        std::vector<float> history;   // the last fftSize input samples (a ring, oldest at writePos)
        std::vector<float> sum;       // overlap-add of the frames, fftSize; sum[0] is the oldest
        std::vector<float> out;       // the hop being played out, hop samples
    };

    std::vector<Channel> channelState;
    int writePos = 0;                 // where the next input sample goes in every history ring
    int hopFill = 0;                  // samples taken in since the last frame
    int untouchedRun = 0;             // frames in a row that changed nothing
    HopStart currentHop, previousHop; // the timeline at the first sample of the last two hops
    FrequencyScale binRowsScale;      // what binRows were looked up for
    int binRowsCount = -1;
};

} // namespace artfreq
