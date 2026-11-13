#pragma once

// Decides where in the drawing playback is, sample by sample, from the host's transport, MIDI
// notes, the tempo and the drawing's length.

#include "ArtEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <bitset>
#include <optional>

namespace freqart
{

enum class TriggerMode
{
    transport = 0,  // follows the DAW's timeline and plays only while the DAW plays. With Repeat
                    // the drawing loops every `length`, starting on the host's bar lines
                    // (position 0 at the song start when the host gives no bars); without it,
                    // it plays once, from the start of bar `startBar` to `length` later
    midiNote = 1    // a note-on plays the drawing from the start (sample-accurately; a new note-on
                    // restarts it). Without Repeat it plays once and note-offs are ignored; with
                    // Repeat it loops while any note is held and ends when the last is released.
                    // All Notes Off, All Sound Off and the DAW stopping end it either way
};

struct TrackerSettings
{
    TriggerMode mode = TriggerMode::transport;
    bool repeat = false;            // loop (see TriggerMode); the preview loops with it too
    int startBar = 1;               // transport without Repeat: the bar the single pass starts on
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

    /** Turns preview on or off. While on, the drawing plays from its start (a jump to 0 when it
        is turned on) whatever the trigger mode and transport say: on a loop with Repeat, else
        once, after which hasPreviewEnded() says so (the caller turns the preview off). */
    void setPreview (bool shouldPreview) noexcept;

    /** True once a preview without Repeat has played its pass and its gate has closed. Cleared
        when the preview is turned off or on again. */
    bool hasPreviewEnded() const noexcept { return previewEnded; }

    /** Fills `segments` (up to maxSegments) covering [0, numSamples) and returns how many.

        Tempo: settings.useHostTempo && timing.bpm ? *timing.bpm : settings.manualBpm, clamped
        to 20..400. The position advances by tempo / 60 / sampleRate / lengthQuarterNotes per
        sample. The exception is the transport following the host's ppq: there the position
        moves at the host's own tempo (only kept within 1..1000), so it keeps up with the ppq.

        transport: while timing.isPlaying, the song position qn at the block start is the
          host's ppqPosition with the host tempo. Otherwise (a typed-in tempo, or no
          ppqPosition) qn is anchored to timeInSeconds * tempo / 60 when playback starts or the
          host's time relocates, and counted on at the tempo of each block in between, so tempo
          changes bend the position rather than move it. A position that does not follow on from
          the previous block's end is a jump: following on means landing between where the
          previous block's rate and this block's rate lead (a tempo change or ramp inside the
          previous block), give or take 5 ms. Not playing: one non-playing segment.
          - Repeat: the position is ((qn - barOffset) mod length) / length, where barOffset is
            ppqPositionOfLastBarStart mod min (length, quarterNotesPerBar) with the host's ppq
            (so every bar line falls on a multiple of the bar, or on the start of a drawing under
            a bar, also after a time-signature change), else 0. Following on allows for the wrap.
            With nothing usable from the host it runs on from where the last block ended.
          - No repeat: the position is (qn - (startBar - 1) * quarterNotesPerBar) / length, and
            sound plays only while it is within the pass: from its start to a gate fade before
            its end (as a MIDI one-shot), so outside the pass is silent and the edges fade.
            Bars are counted in the host's current time signature, assumed constant from the
            song start (as in most songs; a signature change before the start bar moves it).
            The pass's start and end split the block; every segment is marked `once`.
        midiNote: each note-on splits the block at its sample position and restarts the drawing
          (jump, position 0). Without Repeat the gate closes (playing = false) a gate fade before
          the position reaches 1, so the fade-out ends with the drawing; that end splits the
          block too, and every segment is marked `once`. With Repeat the drawing loops (the
          position wraps) while any note is held, and the release of the last held note (a
          note-off, or a note-on with velocity 0) ends the pass where it is; a pass with no note
          held when it reaches the end (Repeat turned on during it) ends there as one without.
          All Notes Off and All Sound Off end the pass where they are and forget the held notes.
          The transport stopping (playing in the previous block, not in this one) ends it at the
          block start, also forgetting them; notes while it is stopped still play. SysEx and
          other messages longer than 3 bytes are ignored.

        Changing the mode, Repeat, the start bar, the length or the tempo source makes the next
        segment a jump if the computed position does not follow on. */
    int process (const HostTiming& timing, const juce::MidiBuffer& midi, int numSamples,
                 const TrackerSettings& settings, PlaySegment* segments) noexcept;

    /** The tempo used in the last block, within 20..400 (the host's ppq can run faster or slower). */
    double getTempo() const noexcept { return lastTempo; }

private:
    class Writer;

    void processTransport (const HostTiming& timing, double tempo, bool usingHostTempo, double length,
                           double barLength, double rate, const TrackerSettings& settings, Writer& out) noexcept;
    void playSinglePass (double position, double rate, bool jump, Writer& out) noexcept;
    void processMidi (const juce::MidiBuffer& midi, double rate, bool repeat, bool transportStopped, Writer& out) noexcept;
    void processPreview (const juce::MidiBuffer& midi, double rate, bool repeat, Writer& out) noexcept;
    void playPass (int end, double rate, bool loops, bool& restart, Writer& out) noexcept;
    void followNotes (const juce::MidiMessage& message) noexcept;
    void runOn (double rate, bool forceJump, Writer& out) noexcept;
    void expectNext (double position, double rate, int numSamples, bool wraps) noexcept;

    static int noteIndex (const juce::MidiMessage& message) noexcept;

    double sampleRate = 48000.0;
    bool preview = false;
    bool previewStartPending = false;
    bool previewEnded = false;

    double expectedNextPosition = 0.0;  // where the previous block ended (0..1, or beyond the
                                        // single pass's ends without Repeat)
    double lastRate = 0.0;              // the previous block's rate and length, for the span a
    int lastLength = 0;                 // tempo change inside it can add (0: none)
    bool hasExpected = false;           // false after reset: nothing to follow on from

    double gateLeadSamples = 241.0;     // a pass that ends: how long before the end the gate closes
    bool wasPlaying = false;            // the host transport in the previous block

    double timeQuarterNotes = 0.0;      // transport without the host's ppq: the count from the
    double expectedNextSeconds = 0.0;   // time anchor, and the host time it expects next
    bool timeAnchored = false;

    bool passActive = false;            // midiNote and preview: a pass of the drawing is playing
    double passPosition = 0.0;
    std::bitset<16 * 128> heldNotes;    // midiNote: the keys held down (channel x note)

    double lastTempo = 120.0;
};

/** Converts a JUCE position to HostTiming. */
HostTiming makeHostTiming (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept;

} // namespace freqart
