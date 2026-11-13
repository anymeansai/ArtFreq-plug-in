#pragma once

// Engine 1: a tuned single-resolution short-time Fourier transform (one of the carving engines
// tools/CarveLab.cpp compares; the baseline is engine 0).
//
// Still one STFT like the baseline, but each part chosen by measuring what an analyser shows
// (the lab's numbers are in docs/DESIGN.md):
//
// - Frames of about 85 ms (4096 samples at 48 kHz; the same time at any rate), twice the
//   baseline's. Bins of 11.7 Hz instead of 23.4 Hz are what lets the lows carve deep (a third
//   of an octave at 60 Hz was a single bin); the time edges only widen by a few ms, since the
//   analyser blurs time about as much on its own.
// - sin^3 analysis and synthesis windows, 75 % overlap. Their product (Hann^3) adds up to a
//   constant a quarter frame apart, so untouched audio comes back exactly; its sidelobes fall
//   so fast that a carved band no longer leaks into what is kept (and the other way round), and
//   it is short enough in time that 75 % overlap is as good as 87.5 % at half the cost.
// - Carve: per bin, the drawing is averaged over the bin's band (not sampled at its centre), so
//   a stroke thinner than a bin is carved by its share instead of being missed, and partial
//   values (pictures) are followed smoothly. Then every carved bin is widened by one bin (a
//   halo): the window spreads each frequency over its neighbours, so a band carved exactly to
//   its edge stays shallow there; one bin more makes thin shapes (a sun's rays) dark, at the
//   cost of carving a little beside the drawing (most visible below ~150 Hz).
// - Stencil: per bin, the drawing's maximum over the bin's band, no halo, so what is drawn is
//   never thinned away and what is kept stays whole.
// - The gate is applied per sample at the output, not per frame: where it is shut the output is
//   the delayed input itself, where it is open the carved audio, with the gate's 5 ms fades in
//   between. A frame that reaches over the start or end of a pass carves as the pass's nearest
//   moment does, so the carve is complete right from the pass's first sample and nothing
//   outside the pass is touched (the baseline's Stencil muted 15 ms before and after it).
//
// Latency: one frame (4096 samples at 48 kHz, 85.3 ms), constant. Frames that change nothing
// skip the FFT, and once every frame overlapping a hop was such a frame the hop is the delayed
// input itself, bit for bit.

#include "SpectralCarver.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <string>
#include <vector>

namespace freqart
{

class TunedCarver final : public SpectralCarver
{
public:
    /** What can be tuned; the defaults are the engine (the lab chose them). */
    struct Options
    {
        double frameSeconds = 4096.0 / 48000.0;  // window length, at any sample rate
        int overlap = 4;                          // frames over each sample (hop = frame / overlap)
        float windowPower = 3.0f;                 // both windows are sin^p (1 = sqrt-Hann)
        int haloBins = 1;                         // Carve: widen what is carved by this many bins
        float carveMaskMax = 0.0f;                // Carve, per bin: 0 = mean over its band, 1 = max
        float stencilMaskMax = 1.0f;              // the same in Stencil
    };

    TunedCarver();
    explicit TunedCarver (const Options& options);

    const char* getName() const noexcept override { return name.c_str(); }
    int getLatencySamples() const noexcept override { return frameSize; }

    int getFrameSize() const noexcept { return frameSize; }
    int getHopSize() const noexcept { return hop; }
    int getFftSize() const noexcept { return fftSize; }

protected:
    void prepareCarver() override;
    void resetCarver() noexcept override;
    void planChanged() noexcept override;
    void carve (float* const* channels, int numChannels, int numSamples,
                const Timeline& timeline, const CarveSettings& settings) noexcept override;

private:
    struct Hop  // where the timeline was during one hop of input
    {
        double fraction = 0.0;    // the position at its first sample, or at the first where the gate is open
        bool openAtStart = false; // the gate is open at its first sample
        bool open = false;        // ... at any of its samples
    };

    const Hop& hopAt (int64_t index) const noexcept;
    void runFrame (int numChannels, const CarveSettings& settings) noexcept;
    bool computeGains (const CarveSettings& settings) noexcept;
    bool sampleMask (double fraction, bool stencil) noexcept;
    void widenAcrossBins (int radius) noexcept;
    void lookUpBinRows() noexcept;

    Options options;
    std::string name;

    int frameSize = 4096;             // window length = latency
    int hop = 1024;
    int fftOrder = 12;
    int fftSize = 4096;               // the next power of 2 from frameSize (the rest is zero padding)
    int numBins = 2049;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> analysisWindow;
    std::vector<float> synthesisWindow;  // divided by the overlapping window products: exact sums
    std::vector<float> untouched;        // analysis x synthesis: what a frame that changes nothing adds
    std::vector<float> fftBuffer;        // 2 x fftSize, for the real-only transforms
    std::vector<float> gains;            // per bin
    std::vector<float> amounts;          // per bin: how much of Depth to carve (0..1)
    std::vector<float> scratch;          // per bin, for the halo
    std::vector<double> prefix;          // running sum of a plan column, numRows + 1
    std::vector<double> binEdgeRows;     // bin k's band runs from [k] to [k + 1], on the plan's rows
    FrequencyScale binRowsScale;         // what binEdgeRows were looked up for
    int binRowsCount = -1;

    struct Channel
    {
        std::vector<float> history;   // the last frameSize input samples (a ring, oldest at writePos)
        std::vector<float> sum;       // overlap-add of the frames, frameSize; sum[0] is the oldest
        std::vector<float> out;       // the hop being played out
    };

    std::vector<Channel> channelState;
    std::vector<float> gateHistory;   // the gate of the last frameSize input samples (a ring like history)

    static constexpr int hopRingSize = 16;  // the last hops (>= overlap)
    Hop hopRing[hopRingSize];
    int64_t hopCount = 0;             // hops started since the reset
    int writePos = 0;                 // where the next input sample goes in the rings
    int hopFill = 0;                  // samples taken in since the last frame
    int untouchedRun = 0;             // frames in a row that changed nothing
};

} // namespace freqart
