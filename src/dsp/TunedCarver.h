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
// - The drawing is read over the stretch of time each frame stands for (the hop around its
//   centre), at points about every 2.7 ms (8 a hop at any rate) weighted by the gate there, not
//   at one moment:
//   at short lengths a hop covers several of the drawing's columns, and a thin vertical stroke
//   between two moments would be missed. Carve and Stencil take the points' mean; with
//   holdLoudest (Carve + Draw) Carve takes their maximum, so the hole covers the sines wherever
//   they sound in the hop. A point exactly on the edge between two columns reads the later
//   one, whatever the host's rounding of the position (columnNudge). At a shape's edges the
//   frames' centres decide, as when the drawing was read there alone (sharpEdges): the mean
//   alone spread every edge over the frames whose stretch reached it (in the lab, time edges
//   6 ms wider at an analyser's FFT of 4096); so only strokes between two frames' centres are
//   left to it.
// - Carve: per bin, the drawing is averaged over the bin's band (not sampled at its centre), so
//   a stroke thinner than a bin is carved by its share instead of being missed, and partial
//   values (pictures) are followed smoothly. Then every carved bin is widened by
//   CarveSettings::haloBins (1 in Carve, 3 in Carve + Draw): the window spreads each frequency
//   over its neighbours, so a band carved exactly to its edge stays shallow there; one bin more
//   makes thin shapes (a sun's rays) dark, at the cost of carving a little beside the drawing
//   (most visible below ~150 Hz), and three make a dark edge around sines played in the hole.
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
//
// Work per call: a frame's transforms need the last sample of its hop, and the first sample
// played out after it needs them, so they run in the call that completes the hop. Reading the
// drawing for a frame (its columns at the points, then the gains) runs a stage at a time over
// the calls while its newest hop comes in, from the plan and settings of the call it began in
// (the plan stays in use until then: getFadingPlan). With blocks up to a thirteenth of a hop
// that call does nothing else; the output is the same however the work falls.

#include "SpectralCarver.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace artfreq
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
        float carveMaskMax = 0.0f;                // Carve, per bin: 0 = mean over its band, 1 = max
        float stencilMaskMax = 1.0f;              // the same in Stencil
        double pointSeconds = 128.0 / 48000.0;    // time between the points a hop reads the drawing at
        bool sharpEdges = true;                   // the centres decide at shapes' edges (see finishGains)
    };

    /** The most points a hop reads the drawing at (hops longer than this many point spacings
        space them further apart). */
    static constexpr int maxPointsPerHop = 32;

    /** How much of the work of reading the drawing this engine has done (for tests): the
        drawing's columns read and the frames transformed since takeWork was last called (each
        stops counting at `most`). */
    struct Work
    {
        static constexpr int most = 1 << 30;
        int columnsRead = 0;
        int framesTransformed = 0;
    };

    Work takeWork() noexcept { return std::exchange (work, Work()); }

    /** Added to a position before it is turned into a column: a point can fall exactly on the
        edge between two columns (a pass is often a whole number of samples per column), and a
        host's position there rounds either way depending on where its blocks start. Reading a
        billionth of the pass later makes the choice the same for any block sizes. */
    static constexpr double columnNudge = 1.0e-9;

    TunedCarver();
    explicit TunedCarver (const Options& options);

    const char* getName() const noexcept override { return name.c_str(); }
    int getLatencySamples() const noexcept override { return frameSize; }

    /** The plan the next frame's gains are being worked out from, while it is no longer the
        current plan (setPlan was called in the middle of its hop): it must stay alive too. */
    const MaskPlan* getFadingPlan() const noexcept override
    {
        return next.stage > 0 && ! next.complete && next.plan != plan ? next.plan : nullptr;
    }

    int getFrameSize() const noexcept { return frameSize; }
    int getHopSize() const noexcept { return hop; }
    int getFftSize() const noexcept { return fftSize; }
    int getPointsPerHop() const noexcept { return pointsPerHop; }

protected:
    void prepareCarver() override;
    void resetCarver() noexcept override;
    void carve (float* const* channels, int numChannels, int numSamples,
                const Timeline& timeline, const CarveSettings& settings) noexcept override;

private:
    struct Hop  // where the timeline was during one hop of input
    {
        double fraction = 0.0;    // the position at its first sample, or at the first where the gate is open
        bool openAtStart = false; // the gate is open at its first sample
        bool open = false;        // ... at any of its samples
        double pointFraction[maxPointsPerHop] {};  // the position at each of its points
        float pointGate[maxPointsPerHop] {};       // ... and the gate there (0: shut, not read)
    };

    struct Point  // one of the columns a frame reads, and its share
    {
        int column = 0;
        float weight = 0.0f;
    };

    // The next frame's gains, worked out a stage at a time while its newest hop comes in.
    struct NextGains
    {
        int stage = 0;               // stages done; 0: not started
        int numStages = 1;           // known after the first stage
        bool complete = false;       // `gains` and `changes` are the next frame's
        bool waiting = false;        // no point of its stretch is in the pass: settled when the gate
                                     // opens in the newest hop, or when that is complete
        bool sharp = false;          // the centres decide at shapes' edges (sharpEdges)
        bool drawn = false;          // a column it read had something drawn
        bool changes = false;        // the frame changes the audio
        bool atCentre = false, atBefore = false, atAfter = false;  // drawn at the centres (sharp)
        int numPoints = 0;
        const MaskPlan* plan = nullptr;   // the plan when it started (read to the end)
        CarveSettings settings;           // ... and the settings
    };

    const Hop& hopAt (int64_t index) const noexcept;
    void runFrame (int numChannels) noexcept;
    void advanceGains (const CarveSettings& settings, bool hopComplete) noexcept;
    void gainsStage() noexcept;
    void startGains (const CarveSettings& settings) noexcept;
    void finishGains() noexcept;
    int gatherPoints() noexcept;
    int nearestMoment() noexcept;
    bool columnAmounts (int column, bool stencil, float* out) noexcept;
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
    std::vector<float> columnValues;     // per bin: one column's share of `amounts`
    std::vector<float> besideBefore;     // per bin: the drawing at the centres of the frames either side
    std::vector<float> besideAfter;
    std::vector<float> scratch;          // per bin, for the halo
    std::vector<double> prefix;          // running sum of a plan column, numRows + 1
    std::vector<double> binEdgeRows;     // bin k's band runs from [k] to [k + 1], on the plan's rows
    FrequencyScale binRowsScale;         // what binEdgeRows were looked up for
    int binRowsCount = -1;
    int binsInRange = 0;                 // the bins below it start below the top of the plan's scale
                                         // (the others carve nothing)

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
    int pointsPerHop = 8;             // even, so half of them fall in each half of a hop
    int pointOffset[maxPointsPerHop] {};  // where each point is in its hop
    Point points[maxPointsPerHop + 1];    // what the frame being computed reads
    int64_t hopCount = 0;             // hops started since the reset
    int writePos = 0;                 // where the next input sample goes in the rings
    int hopFill = 0;                  // samples taken in since the last frame
    int untouchedRun = 0;             // frames in a row that changed nothing
    NextGains next;
    Work work;
};

} // namespace artfreq
