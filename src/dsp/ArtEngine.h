#pragma once

// The sound of the drawing: a bank of sine oscillators, one per plan line, whose amplitudes
// follow the plan's columns as the playback position moves through the drawing.
//
// Real-time safe: no allocation, locking or I/O in setPlan or render (prepare allocates).

#include "core/ArtPlan.h"

#include <cstdint>
#include <vector>

namespace freqart
{

/** A stretch of the block over which playback is uniform. The PlayheadTracker produces these. */
struct PlaySegment
{
    int start = 0;                   // first sample in the block
    int length = 0;                  // number of samples
    bool playing = false;            // gate: false fades the sound out and keeps it silent
    double fraction = 0.0;           // position in the drawing at `start`, 0..1 (wraps)
    double fractionPerSample = 0.0;  // how far the position moves per sample
    bool jump = false;               // the position does not follow on from the previous segment
                                     // (relocation, retrigger, preview start)
    bool once = false;               // a single pass (a MIDI one-shot): the last column holds
                                     // rather than leading back into the first, and the position
                                     // stops at the end rather than wrapping
};

class ArtEngine
{
public:
    static constexpr double gateFadeSeconds = 0.005;     // fade in/out when the gate changes
    static constexpr double jumpFadeSeconds = 0.003;     // fade out before, and in after, a jump
    static constexpr double planFadeSeconds = 0.020;     // crossfade when the plan changes
    static constexpr int maxLines = 1024;                // plans with more lines are truncated

    ArtEngine();

    /** Allocates everything. Call before render, and again if the rate or block size grows. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Silences and forgets the position and fades; keeps the plan. */
    void reset() noexcept;

    /** Starts using `plan` (may be nullptr: silence). If sound is playing, crossfades from the
        current plan over planFadeSeconds; if both plans have the same line layout, the
        oscillators keep their phases and only the amplitudes crossfade. The engine does not
        own plans: the caller keeps them alive while getPlan(), getFadingPlan() or
        getQueuedPlan() returns them. */
    void setPlan (const ArtPlan* plan) noexcept;

    const ArtPlan* getPlan() const noexcept { return currentPlan; }
    const ArtPlan* getFadingPlan() const noexcept { return fadingPlan; }  // during a crossfade

    /** Renders the block (overwrites `out`). The segments cover [0, numSamples) in order.

        - Amplitudes are interpolated linearly between columns (wrapping from the last column
          to the first, unless the segment plays `once`) and move smoothly within the block.
        - A segment with jump == true while sound is audible fades out over jumpFadeSeconds at
          the old position, then continues from the new position (advanced by the fade time)
          with a fade in, so retriggers and relocations never click.
        - The gate fades over gateFadeSeconds.
        - Lines at or above 0.48 * sampleRate are skipped (never alias).
        - Silent lines cost (almost) nothing.
        - Output is the plan's level (loudest column RMS = planTargetRms); the caller applies
          the user's level and the safety clipper. */
    void render (float* out, int numSamples, const PlaySegment* segments, int numSegments) noexcept;

    /** Position of the drawing being heard (0..1) and whether anything is audible. */
    double getFraction() const noexcept { return position; }
    bool isSounding() const noexcept { return envelope > 0.0f; }

    /** A plan set while a crossfade is running waits for that crossfade to end (at most
        planFadeSeconds) and is then faded to in turn. The caller keeps it alive meanwhile;
        PlanExchange does, as it is newer than the plans reported in use. */
    const ArtPlan* getQueuedPlan() const noexcept { return hasQueuedPlan ? queuedPlan : nullptr; }

private:
    static constexpr int lanes = 16;         // lines rendered side by side (four 4-float registers)
    static constexpr int chunkSize = 256;    // the longest stretch rendered in one go
    static constexpr int dummyLine = maxLines;  // a silent slot that pads groups of lanes

    struct Bank
    {
        // One oscillator per line: a unit phasor rotated by (cosStep, sinStep) each sample.
        // Sized maxLines + 1: the last slot is the dummy line.
        std::vector<float> re, im, cosStep, sinStep;
        std::vector<float> frequencies;
        std::vector<uint8_t> usable;  // 0 for lines above the Nyquist guard
        uint64_t layoutHash = 0;
        int numLines = 0;
        bool valid = false;           // set up for some plan
    };

    void setUpBank (Bank& bank, const ArtPlan& plan) noexcept;
    bool hasLayout (const Bank& bank, const ArtPlan& plan) const noexcept;
    void startPlan (const ArtPlan* plan) noexcept;
    void finishPlanFade() noexcept;
    void applySegment (const PlaySegment& segment) noexcept;
    void moveOn (int numSamples) noexcept;
    void renderRange (float* out, int from, int to) noexcept;
    int renderChunk (float* out, int maxSamples) noexcept;
    int samplesLeftInColumn (const ArtPlan& plan) const noexcept;
    int gather (const ArtPlan* plan, const ArtPlan* second, const Bank& bank) noexcept;
    void synthesise (const ArtPlan* plan, const ArtPlan* second, Bank& bank, float* sum, float* secondSum, int numSamples) noexcept;

    double sampleRate = 48000.0;
    int maxBlock = 0;                // any block length works (it is rendered in chunks); kept for reference

    const ArtPlan* currentPlan = nullptr;
    const ArtPlan* fadingPlan = nullptr;
    const ArtPlan* queuedPlan = nullptr;
    bool hasQueuedPlan = false;
    Bank banks[2];
    int currentBank = 0;
    bool fadingSharesBank = false;   // same layout: the fading plan plays on the current bank
    int planFadeRemaining = 0;       // > 0 while a crossfade runs
    int planFadeLength = 1;

    double position = 0.0;
    double rate = 0.0;
    float envelope = 0.0f;           // gate / jump fade state, 0..1 (heard through a smoothstep)
    float gateStep = 0.0f;           // envelope change per sample for the gate fades
    float jumpStep = 0.0f;           // ... and for the jump fades
    bool gateOn = false;
    bool once = false;               // a single pass: no wrap from the end to the start
    bool fastFadeIn = false;         // fading in after a jump: jump speed
    bool jumpPending = false;        // fading out before moving to jumpTarget
    double jumpTarget = 0.0;         // where the jump lands, advanced while the fade-out runs
    double jumpRate = 0.0;

    // prepare-sized work buffers
    std::vector<float> initialRe, initialIm;  // deterministic starting phase of each line
    std::vector<uint16_t> workLines;          // the lines of one chunk, padded to whole groups
    std::vector<float> workAmp, workStep, workAmp2, workStep2;
    std::vector<uint8_t> workMark;
    std::vector<float> laneSums, laneSums2;   // chunkSize x 4 partial sums
    std::vector<float> sumA, sumB, gain;      // chunkSize
};

} // namespace freqart
