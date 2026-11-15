#pragma once

// Spectral carving: ArtFreq's drawing applied to incoming audio instead of played as sines.
//
// Carve mutes the drawing's frequencies in the audio, so the drawing shows as dark shapes in
// the song's spectrogram; Stencil does the opposite, letting only the drawing's frequencies
// through. Where it carves comes from a MaskPlan (core/MaskPlan.h); when comes from the same
// PlaySegments the PlayheadTracker makes for the sine engine (Length, Trigger, Repeat, start
// bar, tempo), so a carve lines up with the drawing exactly as the sines do. Outside the
// drawing's pass (gate closed: before or after a single pass, DAW stopped) and with nothing
// drawn, the audio passes through unchanged apart from the latency.
//
// SpectralCarver is the interface every carving engine implements; createSpectralCarver picks
// one by number, so the measurement lab (tools/CarveLab.cpp) can compare them on equal terms.
// The base class turns the segments into a position and a gate per input sample and keeps
// the plan; an engine only does the signal processing.
//
// Real-time safe: prepare allocates; setPlan, reset and process never allocate, lock or call
// the system.

#include "ArtEngine.h"
#include "core/MaskPlan.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace artfreq
{

enum class CarveMode
{
    carve = 0,   // the drawing's frequencies drop by up to Depth (by its value: 1 = all of it)
    stencil = 1  // the opposite: everything but the drawing drops by up to Depth
};

struct CarveSettings
{
    CarveMode mode = CarveMode::carve;
    float depthDb = -60.0f;    // how far carved frequencies drop, minDepthDb..0 dB
    int haloBins = 1;          // Carve: also carve this many bins either side of what is drawn
                               // (the window spreads a frequency over its neighbours, so a band
                               // carved exactly to its edge stays shallow there). Stencil never
                               // widens. Engines without bins ignore it.
    bool holdLoudest = false;  // Carve: carve the most the drawing asks for anywhere in a frame's
                               // stretch of time rather than its average (Carve + Draw: the hole
                               // must cover the sines wherever they sound in it)
};

class SpectralCarver
{
public:
    static constexpr double gateFadeSeconds = ArtEngine::gateFadeSeconds;  // as the sines' gate
    static constexpr float minDepthDb = -120.0f;

    virtual ~SpectralCarver() = default;

    /** A short description for the lab's reports ("baseline STFT 2048 ..."). */
    virtual const char* getName() const noexcept = 0;

    /** Allocates everything for up to maxBlockSize samples per process call (longer calls are
        worked through in pieces) and numChannels channels, and resets. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Forgets the audio and the position (the output starts again from silence); keeps the
        plan. */
    void reset() noexcept;

    /** How much later the audio comes out than it went in, for the host's delay compensation.
        Fixed once prepared: the same in every mode, at every depth, with any plan or none. */
    virtual int getLatencySamples() const noexcept = 0;

    /** Starts using `plan` (may be nullptr: nothing is carved) from the next moment the engine
        decides what to carve. The engine does not own plans: the caller keeps `plan` alive
        while getPlan() or getFadingPlan() return it (MaskPlanExchange does, when told about
        them with reportInUse). */
    void setPlan (const MaskPlan* newPlan) noexcept;

    const MaskPlan* getPlan() const noexcept { return plan; }

    /** A plan the engine still reads after a newer one was set (an engine that crossfades
        between plans), else nullptr. */
    virtual const MaskPlan* getFadingPlan() const noexcept { return nullptr; }

    /** Carves the block in place: numChannels channels (mono or stereo; channels beyond those
        prepared are left alone) of numSamples, the audio coming in now. The segments are the
        tracker's for this block (covering [0, numSamples) in order): each input sample is
        carved with the drawing's column at its position, opened and closed by the segments'
        gate (with S-shaped gateFadeSeconds fades; a jump moves the position at once, since the
        audio jumps with it). */
    void process (float* const* channels, int numChannels, int numSamples,
                  const PlaySegment* segments, int numSegments, const CarveSettings& settings) noexcept;

protected:
    /** Where each input sample of a stretch of audio falls: its position in the drawing (0..1,
        a fraction of its length) and how far the carving is open (0 = not at all: the audio
        must pass unchanged; 1 = fully, as the mask says). Valid during carve() only. */
    struct Timeline
    {
        const double* fraction = nullptr;
        const float* gate = nullptr;
    };

    /** Allocates the engine's own state for the prepare() arguments (stored below). */
    virtual void prepareCarver() = 0;

    /** Clears the engine's audio state. */
    virtual void resetCarver() noexcept = 0;

    /** Called on the audio thread when setPlan changed `plan` (to look up what depends on its
        scale again). */
    virtual void planChanged() noexcept {}

    /** Carves numSamples (at most maxBlockSize) of up to maxChannels channels in place. */
    virtual void carve (float* const* channels, int numChannels, int numSamples,
                        const Timeline& timeline, const CarveSettings& settings) noexcept = 0;

    const MaskPlan* plan = nullptr;
    double sampleRate = 48000.0;
    int maxBlockSize = 0;
    int maxChannels = 0;

private:
    void applySegment (const PlaySegment& segment, int sample) noexcept;
    double positionAt (int sample) const noexcept;

    std::vector<double> fractions;
    std::vector<float> gates;
    std::vector<float*> chunkChannels;

    // The position runs on from the last segment's start at its rate (also through a gap
    // between segments), so it does not depend on how the host cuts the audio into blocks.
    int64_t segmentStart = 0;  // the sample (in the current block) the last segment started at
    double segmentFraction = 0.0;
    double rate = 0.0;
    bool once = false;
    bool gateOn = false;
    float envelope = 0.0f;     // 0..1, heard through a smoothstep
    float gateStep = 0.0f;
};

/** The carving engines, by number (the lab's --engine N): 0 is the baseline STFT, 1 the tuned
    STFT (dsp/TunedCarver.h). Returns nullptr for a number with no engine. */
std::unique_ptr<SpectralCarver> createSpectralCarver (int engine);

/** How many engines createSpectralCarver knows (numbers 0 to this - 1). */
int getNumSpectralCarvers() noexcept;

} // namespace artfreq
