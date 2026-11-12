#pragma once

// Decides where in the drawing playback is, sample by sample, from the host's transport, MIDI
// notes, the tempo and the drawing's length.

#include "ArtEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace freqart
{

enum class TriggerMode
{
    transport = 0,  // follows the DAW's timeline: the drawing loops every `length`, aligned to
                    // bar 1 (position 0 at the song start), and plays only while the DAW plays
    midiNote = 1,   // any note-on plays the drawing once from the start (sample-accurate);
                    // a new note-on restarts it; note-offs are ignored
    freeLoop = 2    // always plays, looping, from when playback of the plug-in started
};

struct TrackerSettings
{
    TriggerMode mode = TriggerMode::transport;
    bool useHostTempo = true;       // false: manualBpm
    double manualBpm = 120.0;       // also the fallback when the host gives no tempo
    double lengthQuarterNotes = 16; // the drawing's length in quarter notes (4 bars of 4/4)
};

/** Host information for one block (all optional: hosts differ in what they provide). */
struct HostTiming
{
    bool isPlaying = false;
    std::optional<double> bpm;
    std::optional<double> ppqPosition;     // quarter notes since the song start
    std::optional<double> timeInSeconds;   // since the song start
};

class PlayheadTracker
{
public:
    static constexpr int maxSegments = 64;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Turns preview on or off. While on, the drawing loops from its start (a jump to 0 when
        it is turned on) whatever the trigger mode and transport say. */
    void setPreview (bool shouldPreview) noexcept;

    /** Fills `segments` (up to maxSegments) covering [0, numSamples) and returns how many.

        Tempo: settings.useHostTempo && timing.bpm ? *timing.bpm : settings.manualBpm, clamped
        to 20..400. The position advances by tempo / 60 / sampleRate / lengthQuarterNotes per
        sample.

        transport: while timing.isPlaying, the position at the block start is
          (qn mod length) / length, where qn = ppqPosition with the host tempo, otherwise
          timeInSeconds * tempo / 60 (also used when the host gives no ppqPosition). A position
          that does not follow on from the previous block's end (beyond a tolerance of 2 samples'
          worth, allowing for the loop wrap) is a jump. Not playing: one non-playing segment.
        midiNote: each note-on splits the block at its sample position and restarts the drawing
          (jump, position 0). Playback stops (playing = false) when the position reaches 1; that
          end splits the block too.
        freeLoop: always playing; the position runs on from block to block and wraps.

        Changing the mode, the length or the tempo source makes the next segment a jump if the
        computed position does not follow on. */
    int process (const HostTiming& timing, const juce::MidiBuffer& midi, int numSamples,
                 const TrackerSettings& settings, PlaySegment* segments) noexcept;

    /** The tempo used in the last block. */
    double getTempo() const noexcept { return lastTempo; }

private:
    double sampleRate = 48000.0;
    bool preview = false;
    bool previewStartPending = false;
    bool previewing = false;

    bool wasPlaying = false;
    double expectedNextPosition = 0.0;  // where the previous block ended, 0..1
    bool hasExpected = false;

    bool oneShotActive = false;         // midiNote mode
    double oneShotPosition = 0.0;

    double freePosition = 0.0;          // freeLoop and preview
    double lastTempo = 120.0;
    TriggerMode lastMode = TriggerMode::transport;
};

/** Converts a JUCE position to HostTiming. */
HostTiming makeHostTiming (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept;

} // namespace freqart
