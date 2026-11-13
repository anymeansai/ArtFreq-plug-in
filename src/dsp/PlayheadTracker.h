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
    transport = 0,  // follows the DAW's timeline: the drawing loops every `length`, starting on
                    // the host's bar lines (position 0 at the song start when the host gives no
                    // bars), and plays only while the DAW plays
    midiNote = 1,   // any note-on plays the drawing once from the start (sample-accurate);
                    // a new note-on restarts it; note-offs are ignored, but All Notes Off, All
                    // Sound Off and the DAW stopping end the pass
    freeLoop = 2    // always plays, looping, from when playback of the plug-in started
};

struct TrackerSettings
{
    TriggerMode mode = TriggerMode::transport;
    bool useHostTempo = true;       // false: manualBpm
    double manualBpm = 120.0;       // also the fallback when the host gives no tempo
    double lengthQuarterNotes = 16; // the drawing's length in quarter notes (4 bars of 4/4)
    double quarterNotesPerBar = 4;  // the host's bar (from its time signature), for bar alignment
};

/** Host information for one block (all optional: hosts differ in what they provide). */
struct HostTiming
{
    bool isPlaying = false;
    std::optional<double> bpm;
    std::optional<double> ppqPosition;     // quarter notes since the song start
    std::optional<double> timeInSeconds;   // since the song start
    std::optional<double> ppqPositionOfLastBarStart;  // where the bar the block starts in began
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
        sample. The exception is the transport following the host's ppq: there the position
        moves at the host's own tempo (only kept within 1..1000), so it keeps up with the ppq.

        transport: while timing.isPlaying, the position at the block start is
          ((qn - barOffset) mod length) / length. With the host tempo, qn = ppqPosition and
          barOffset = ppqPositionOfLastBarStart mod min (length, quarterNotesPerBar), so every
          bar line falls on a multiple of the bar (or on the start of a drawing under a bar),
          also after a time-signature change; 0 when the host gives no bars. Otherwise (a typed-in
          tempo, or no ppqPosition) qn is anchored to timeInSeconds * tempo / 60 when playback
          starts or the host's time relocates, and counted on at the tempo of each block in
          between, so tempo changes bend the position rather than move it. A position that does
          not follow on from the previous block's end is a jump: following on means landing
          between where the previous block's rate and this block's rate lead (a tempo change or
          ramp inside the previous block), give or take 5 ms, allowing for the loop wrap. Not
          playing: one non-playing segment.
        midiNote: each note-on splits the block at its sample position and restarts the drawing
          (jump, position 0). The gate closes (playing = false) a gate fade before the position
          reaches 1, so the fade-out ends with the drawing; that end splits the block too, as do
          All Notes Off and All Sound Off, which end the pass where they are. The transport
          stopping (playing in the previous block, not in this one) ends it at the block start;
          notes while it is stopped still play. Every segment is marked `once`.
        freeLoop: always playing; the position runs on from block to block and wraps.

        Changing the mode, the length or the tempo source makes the next segment a jump if the
        computed position does not follow on. */
    int process (const HostTiming& timing, const juce::MidiBuffer& midi, int numSamples,
                 const TrackerSettings& settings, PlaySegment* segments) noexcept;

    /** The tempo used in the last block, within 20..400 (the host's ppq can run faster or slower). */
    double getTempo() const noexcept { return lastTempo; }

private:
    class Writer;

    void processTransport (const HostTiming& timing, double tempo, bool usingHostTempo, double length,
                           double barLength, double rate, Writer& out) noexcept;
    void processMidi (const juce::MidiBuffer& midi, double rate, bool transportStopped, Writer& out) noexcept;
    void runOn (double rate, bool forceJump, Writer& out) noexcept;
    void expectNext (double position, double rate, int numSamples) noexcept;

    double sampleRate = 48000.0;
    bool preview = false;
    bool previewStartPending = false;

    double expectedNextPosition = 0.0;  // where the previous block ended, 0..1
    double lastRate = 0.0;              // the previous block's rate and length, for the span a
    int lastLength = 0;                 // tempo change inside it can add (0: none)
    bool hasExpected = false;           // false after reset: nothing to follow on from

    double gateLeadSamples = 241.0;     // midiNote: how long before the end the gate closes
    bool wasPlaying = false;            // the host transport in the previous block

    double timeQuarterNotes = 0.0;      // transport without the host's ppq: the count from the
    double expectedNextSeconds = 0.0;   // time anchor, and the host time it expects next
    bool timeAnchored = false;

    bool oneShotActive = false;         // midiNote mode
    double oneShotPosition = 0.0;

    double lastTempo = 120.0;
};

/** Converts a JUCE position to HostTiming. */
HostTiming makeHostTiming (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept;

} // namespace freqart
