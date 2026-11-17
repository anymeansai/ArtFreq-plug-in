#pragma once

// Decides where in the drawing playback is, sample by sample, from the host's transport, MIDI
// notes, the tempo and the drawing's length.

#include "ArtEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <bitset>
#include <optional>

namespace artfreq
{

enum class TriggerMode
{
    transport = 0,  // follows the DAW's timeline and plays only while the DAW plays. With Repeat
                    // the drawing loops every `length`, starting on the host's bar lines
                    // (position 0 at the song start when the host gives no bars); without it,
                    // it plays once: from where the DAW starts playing, or from the start of bar
                    // `startBar` (see StartAt), to `length` later
    midiNote = 1    // a note-on plays the drawing from the start (sample-accurately; a new note-on
                    // restarts it). Without Repeat it plays once and note-offs are ignored; with
                    // Repeat it loops while any note is held and ends when the last is released.
                    // All Notes Off, All Sound Off and the DAW stopping end it either way
};

/** Where the transport's single pass (Repeat off) begins. */
enum class StartAt
{
    play = 0,   // "On play": where the DAW starts playing, wherever that is in the song. Press
                // play and it plays, once
    bar = 1     // "At bar": at the start of bar `startBar` of the song, and nowhere else
};

struct TrackerSettings
{
    TriggerMode mode = TriggerMode::transport;
    bool repeat = false;            // loop (see TriggerMode); the preview loops with it too
    StartAt startAt = StartAt::play;  // transport without Repeat: where the single pass begins
    int startBar = 1;               // ... with StartAt::bar: the bar it starts on
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

    /** Forgets the playback state (the next block is a fresh start). Where a single pass sits at
        its start bar is kept: playing on from inside or after it finds it where it was. A pass
        that began on play is forgotten: if the DAW is playing in the next block, a new one begins
        there. */
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
        sample. The exception is the transport following the host's ppq at the host's tempo:
        there the position moves at the host's own tempo (only kept within 1..1000), so it keeps
        up with the ppq.

        transport: while timing.isPlaying, the song position qn at the block start is the
          host's ppqPosition with the host tempo (without Repeat, also with a typed-in one: see
          below). Otherwise (a typed-in tempo with Repeat, or no ppqPosition) qn is anchored to
          timeInSeconds * tempo / 60 when playback starts or the host's time relocates, and
          counted on at the tempo of each block in between, so tempo changes bend the position
          rather than move it. A position that does not follow on from the previous block's end
          is a jump: following on means landing between where the previous block's rate and this
          block's rate lead (a tempo change or ramp inside the previous block), give or take
          5 ms. Not playing: one non-playing segment.
          - Repeat: the position is ((qn - barOffset) mod length) / length, where barOffset is
            ppqPositionOfLastBarStart mod min (length, quarterNotesPerBar) with the host's ppq
            (so every bar line falls on a multiple of the bar, or on the start of a drawing under
            a bar, also after a time-signature change), else 0. Following on allows for the wrap.
            With nothing usable from the host it runs on from where the last block ended.
          - No repeat: the drawing plays once, at (qn - passStart) / passLength. Here qn is the
            host's ppq whenever it gives one with its tempo, also with a typed-in tempo (so the
            pass starts at the DAW's place; the typed tempo only sets how fast it then plays,
            counted on from where the DAW reached its start, or from the DAW's position after a
            relocation into it); else the count from the time. Sound plays only while the position
            is within the pass: from its start to a gate fade before its end (as a MIDI one-shot),
            so outside the pass is silent and the edges fade. The pass's start and end split the
            block; every segment is `once`. Where the pass is, is kept from block to block:

            StartAt::play ("On play", the default): press play and it plays.
            . A pass begins at the first sample of the block in which the DAW starts playing,
              wherever the song is: also in the first block after prepare() or reset() if the DAW
              is already playing (the plug-in was put on the track during playback, or came out
              of bypass), and in the first block after another setting (Repeat, StartAt::bar, the
              MIDI trigger) while the DAW plays: switching to it counts as a start. It is as long
              as Length in the time signature of that block.
            . From there it follows the song as the pass at a start bar does (the host's ppq and
              tempo, tempo changes and ramps, the typed-tempo rule), plays once, then is silent.
            . The DAW moving while it plays (a relocation, or its loop wrapping round: a position
              that does not follow on) to a place inside the pass carries on at that place in the
              same pass, so a DAW loop over it draws the same picture every time round. A place
              before its start (by more than the jump tolerance) or after the last moment it can
              sound (its gate's close) begins a new pass right there. So under a DAW loop shorter
              than the pass, every round draws the same first part of the drawing; under a longer
              one, the whole drawing and then silence.
            . The DAW stopping forgets the pass: every start is a new pass.
            . It never begins before the song does: at a negative position (a count-in before
              bar 1) it begins at 0 and the count-in is silent. A pre-roll inside the song is
              playback like any other: the pass begins where the DAW starts playing. So does an
              offline bounce, at its first block.
            . Time-signature changes while it plays move and stretch nothing (it lines up with no
              bars); a new Length changes its length at once, the start staying put (the position
              jumps, or the pass is over).
            . Under the preview it keeps its place (the song is still followed, unheard), so the
              preview ending does not start it again.
            . With nothing usable from the host (no ppq, no time) it begins at the first playing
              block and runs on at the tempo, once; relocations cannot be seen.
            . What it cannot see: a host that stops calling process() while it is stopped, without
              a reset(), and starts again exactly where it stopped. That is playback that never
              stopped, as far as the positions go, and a pass that was over stays over. No clock
              is asked: a slow offline bounce looks the same, and would draw the picture twice.

            StartAt::bar ("At bar"): from the start of bar `startBar`. VST3 gives no bar
            numbers, so:
            . It is placed afresh when there is nothing to go on, for a new start bar, and when
              playback starts or moves to before it (except to bars it was counted through since
              the last time-signature change it saw, in that signature): startBar - 1 bars in the
              current time signature from the song start, moved to the nearest of the host's bar
              lines (ppqPositionOfLastBarStart; the earlier one half way), with Length in that
              signature.
            . While the song plays on towards it, the bars still to go are counted in the bar the
              song is in, so a time-signature change on the way (an odd bar, a new section) moves
              it with the bars. Played into, its length becomes Length in the start bar's own
              signature in the first block inside it (the host reports a block's bar at its start).
            . From then on, and when playback moves into or after it, it stays put: a time
              signature change inside or after it moves nothing, and a DAW loop over it plays the
              same pass. Only a new Length (not a new time signature) changes its length then.
            . With nothing usable from the host it runs on from where the last block ended.
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

        Changing the mode, Repeat, where the pass starts, the start bar, the length or the tempo
        source makes the next segment a jump if the computed position does not follow on (the
        engine then fades the old sound out before it moves: no setting clicks). */
    int process (const HostTiming& timing, const juce::MidiBuffer& midi, int numSamples,
                 const TrackerSettings& settings, PlaySegment* segments) noexcept;

    /** The tempo used in the last block, within 20..400 (the host's ppq can run faster or slower). */
    double getTempo() const noexcept { return lastTempo; }

private:
    class Writer;

    void processTransport (const HostTiming& timing, double tempo, bool usingHostTempo, double length,
                           double barLength, double rate, const TrackerSettings& settings, Writer& out) noexcept;
    void processSinglePass (const HostTiming& timing, double tempo, bool usingHostTempo, double length,
                            double barLength, const TrackerSettings& settings, Writer& out) noexcept;
    bool placePass (double song, double songRate, std::optional<double> barStart, double length, double barLength,
                    int startBar, int numSamples) noexcept;
    bool placePassOnPlay (double song, double songRate, double playRate, double length, double barLength,
                          int numSamples) noexcept;
    bool songFollowsOn (double song, double songRate) const noexcept;
    void expectSong (double song, double songRate, int numSamples, double length, double barLength) noexcept;
    void forgetPassOnPlay() noexcept;
    double countFromTime (double seconds, double tempo, int numSamples) noexcept;
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

    // Transport without Repeat: where the single pass is on the song's timeline (see process()).
    // At a start bar it depends on the song, not on the audio set-up, so reset() keeps it; one
    // that began on play belongs to that playback, and is forgotten with it.
    bool passPlaced = false;
    bool passFromPlay = false;          // it began where the DAW started playing (StartAt::play)
    int passStartBar = 1;               // the start bar it was placed for
    double passStart = 0.0;             // where it starts, in the song's quarter notes
    double passLength = 16.0;           // how long it is, in quarter notes
    double passBarLength = 4.0;         // the bar the bars still to go before it are counted in,
    double passCountedFrom = 0.0;       // ... since this bar line
    bool approachingPass = false;       // the song was before it in the last block
    double passElapsed = 0.0;           // with a typed-in tempo: quarter notes (at it) since its start
    bool passElapsedCounting = false;   // ... counted on in the last block
    double expectedSong = 0.0;          // where the song should be next if it plays on, as
    double lastSongRate = 0.0;          // expectedNextPosition, lastRate and lastLength are for
    int lastSongLength = 0;             // the position
    bool songExpected = false;
    double lastLengthSetting = 0.0;     // the last block's length and bar, which tell a new Length
    double lastBarLengthSetting = 0.0;  // from a new time signature

    bool passActive = false;            // midiNote and preview: a pass of the drawing is playing
    double passPosition = 0.0;
    std::bitset<16 * 128> heldNotes;    // midiNote: the keys held down (channel x note)

    double lastTempo = 120.0;
};

/** Converts a JUCE position to HostTiming. */
HostTiming makeHostTiming (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept;

} // namespace artfreq
