#pragma once

// ArtFreq FX: the user's own song, as it came in during the last pass that played, for the
// canvas's measured preview (ui/MeasuredPreview.h).
//
// The canvas shows what the buyer's analyser will show of the effect's output. That depends on
// the song as much as on the drawing: a carve into a bass track shows nothing in the highs,
// where there is nothing to carve; into a quiet mix it is as much quieter. Until a pass has
// played, the canvas carves a stand-in (dsp/StandInSong.h). Once one has, it carves the audio
// that really came in, at the place in the pass it came in.
//
// The audio thread writes the input (the channels' mean, as the analyser hears them) into one of
// two preallocated buffers while a pass plays: a "take", from where it starts to where the pass
// ends, is stopped or jumps (at most maxSamples of it). When a take ends, and covers enough of the
// pass, and is not the same as the last one (a DAW looping the same bars sends the same audio
// again and again: no new picture is needed), the two buffers swap: that one becomes the one the
// canvas reads, the other is recorded into next. No allocation, lock or wait on the audio thread;
// a reader copying the published take (a few ms) only makes the audio thread keep the take it
// has rather than swap (the next pass's end tries again).
//
// Memory: 2 x maxSamples 16-bit samples (16 MB), made only once an editor has been on screen
// (host scans and validators make none); nothing is saved with the project.

#include "ArtEngine.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace artfreq
{

/** One take of the user's song: what came in from pass position `start` (in samples of a pass of
    `passSamples`) on, for `samples.size()` samples. Immutable once made. */
struct CapturedSong
{
    std::vector<int16_t> samples;      // the input, SongCapture::sampleScale per unit
    int64_t start = 0;                 // where in the pass samples[0] came in
    int64_t passSamples = 0;           // the pass it came in during
    double sampleRate = 0.0;
    uint32_t serial = 0;               // which take (counts from 1)

    int64_t length() const noexcept { return static_cast<int64_t> (samples.size()); }
    bool coversPass() const noexcept { return start == 0 && length() >= passSamples; }

    /** The song for a pass's timeline, as a DAW playing it would give it: the take where it came
        in, and around it, the take again (looped, the seam crossfaded over 10 ms), so a take
        that covers part of the pass, or a pass longer than a take, still gets the song's sound
        everywhere. render() continues from where the last call stopped. */
    class Reader
    {
    public:
        Reader (const CapturedSong& song, int64_t firstSample) noexcept;
        void render (float* out, int numSamples) noexcept;

    private:
        float at (int64_t loopPosition) const noexcept;

        const CapturedSong& song;
        int64_t position = 0;      // timeline sample render() gives next
        int64_t loopLength = 1;    // the period of the loop (the take less the crossfade)
        int64_t fade = 0;          // crossfade, samples
    };
};

class SongCapture
{
public:
    static constexpr int64_t maxSamples = int64_t (1) << 22;   // per take: 87 s at 48 kHz, 44 s at 96 kHz
    static constexpr float sampleScale = 4096.0f;               // full scale is +-8 (+18 dBFS): masters never reach it
    static constexpr int numSlots = 16;                         // the fingerprint's slots across the pass

    SongCapture();   // allocates both buffers: message thread

    /** Audio thread, once per block (or chunk) before the input is changed: `channels` hold the
        input, `segments` are the tracker's for the same samples (only playing ones are kept). */
    void process (const float* const* channels, int numChannels, int numSamples,
                  const PlaySegment* segments, int numSegments, double sampleRate) noexcept;

    /** Audio thread: the transport stopped, the host flushed, or the effect was bypassed: the take
        ends here. */
    void endTake() noexcept;

    /** Any thread but the audio thread: which take is published (0: none yet). */
    uint32_t getSerial() const noexcept { return publishedSerial.load (std::memory_order_acquire); }

    /** Any thread but the audio thread: a copy of the published take (null if none). The last copy
        is shared while anyone holds it. */
    std::shared_ptr<const CapturedSong> getLatest() const;

private:
    struct Take
    {
        std::vector<int16_t> samples;   // maxSamples
        int64_t start = 0, length = 0, passSamples = 0;
        double sampleRate = 0.0;
        uint32_t serial = 0;
        std::array<float, numSlots * 2> energy {};   // per slot: below and above about 250 Hz
    };

    bool isWorthPublishing (const Take& take, const Take& published) const noexcept;
    void startTake (int64_t position, int64_t passSamples, double sampleRate) noexcept;

    Take takes[2];

    // Bit 0: the published take's index; bit 1: a reader is copying it.
    static constexpr uint32_t readingBit = 2;
    mutable std::atomic<uint32_t> state { 0 };   // a reader (const) sets and clears its bit
    std::atomic<uint32_t> publishedSerial { 0 };

    // audio thread
    int recording = 1;
    bool inTake = false;
    int64_t skipped = 0;       // samples of the take's pass played past what a take holds
    uint32_t nextSerial = 1;
    float splitState = 0.0f;   // the one-pole split of the fingerprint

    mutable std::mutex readers;                         // readers among themselves (never the audio thread)
    mutable std::weak_ptr<const CapturedSong> lastCopy;
};

} // namespace artfreq
