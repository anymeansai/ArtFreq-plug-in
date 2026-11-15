#include "PlayheadTracker.h"

#include <cmath>
#include <utility>

namespace artfreq
{

namespace
{
    constexpr double minTempo = 20.0;
    constexpr double maxTempo = 400.0;
    constexpr double minHostTempo = 1.0;     // where the host's ppq sets the position, its tempo
    constexpr double maxHostTempo = 1000.0;  // is only kept sane: the drawing must keep up with it
    constexpr double jumpToleranceSeconds = 0.005;  // hosts round, and some report ppq unevenly
    constexpr double defaultLengthQuarterNotes = 16.0;
    constexpr double shortestLengthQuarterNotes = 1.0 / 64.0;
    constexpr int lastStartBar = 999;

    /** x wrapped into [0, 1). */
    double wrap (double x) noexcept
    {
        auto wrapped = x - std::floor (x);
        return wrapped >= 1.0 ? 0.0 : wrapped;
    }

    /** x modulo m (m > 0), in [0, m). */
    double modulo (double x, double m) noexcept
    {
        auto remainder = std::fmod (x, m);

        if (remainder < 0.0)
            remainder += m;

        return remainder >= m ? 0.0 : remainder;
    }

    /** Whether `position` continues from `expected`, where the previous block's rate leads: it may
        also be up to `span` further on, where this block's rate would have led (the host only
        reports a tempo change, or the steps of a tempo ramp, in the block after the one it
        happened in), give or take `tolerance`. With `wraps`, allows for the wrap from 1 to 0. */
    bool followsOn (double position, double expected, double span, double tolerance, bool wraps) noexcept
    {
        auto difference = position - expected;

        if (wraps)
            difference -= std::round (difference - 0.5 * span);

        return difference >= std::min (0.0, span) - tolerance - 1.0e-12
            && difference <= std::max (0.0, span) + tolerance + 1.0e-12;
    }

    std::optional<double> finite (std::optional<double> value) noexcept
    {
        if (value.has_value() && std::isfinite (*value))
            return value;

        return std::nullopt;
    }

    std::optional<double> finite (const juce::Optional<double>& value) noexcept
    {
        if (value.hasValue() && std::isfinite (*value))
            return *value;

        return std::nullopt;
    }

    /** Notes and controllers are at most 3 bytes. Anything longer (SysEx) is no trigger, and is
        not turned into a MidiMessage, which would allocate for it on the audio thread. */
    bool isChannelMessage (const juce::MidiMessageMetadata& metadata) noexcept
    {
        return metadata.numBytes > 0 && metadata.numBytes <= 3;
    }
} // namespace

// Writes segments one after the other from the start of the block, so they always cover it in
// order without gaps.
class PlayheadTracker::Writer
{
public:
    Writer (PlaySegment* destination, int blockLength) noexcept
        : segments (destination), numSamples (blockLength) {}

    /** Marks the segments that follow as a single pass (see PlaySegment::once). */
    void setOnce (bool playsOnce) noexcept   { once = playsOnce; }

    /** Appends a segment from where the last one ended up to `end`. */
    void add (int end, bool playing, double fraction, double fractionPerSample, bool jump) noexcept
    {
        end = juce::jlimit (cursor, numSamples, end);

        if (end <= cursor)
            return;

        if (count == maxSegments)
        {
            // Out of room (the callers leave enough, this is only a safety net): the last
            // segment simply runs on.
            segments[count - 1].length = end - segments[count - 1].start;
        }
        else
        {
            auto& segment = segments[count++];
            segment.start = cursor;
            segment.length = end - cursor;
            segment.playing = playing;
            segment.fraction = fraction;
            segment.fractionPerSample = fractionPerSample;
            segment.jump = jump;
            segment.once = once;
        }

        cursor = end;
    }

    int getCount() const noexcept    { return count; }
    int getCursor() const noexcept   { return cursor; }
    int getLength() const noexcept   { return numSamples; }

private:
    PlaySegment* segments;
    int numSamples;
    int cursor = 0;
    int count = 0;
    bool once = false;
};

void PlayheadTracker::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 && std::isfinite (newSampleRate) ? newSampleRate : 48000.0;
    // One sample more than the engine's gate fade takes, so it has ended before the position
    // reaches the end of the drawing.
    gateLeadSamples = std::ceil (ArtEngine::gateFadeSeconds * sampleRate) + 1.0;
    reset();
}

void PlayheadTracker::reset() noexcept
{
    previewStartPending = preview;
    previewEnded = false;
    expectedNextPosition = 0.0;
    lastRate = 0.0;
    lastLength = 0;
    hasExpected = false;
    passActive = false;
    passPosition = 0.0;
    heldNotes.reset();
    wasPlaying = false;
    timeAnchored = false;
    passElapsedCounting = false;
    songExpected = false;
}

void PlayheadTracker::setPreview (bool shouldPreview) noexcept
{
    if (shouldPreview && ! preview)
        previewStartPending = true;

    if (! shouldPreview)
    {
        // A pass the preview was playing does not carry on as a MIDI note's.
        if (preview)
            passActive = false;

        previewStartPending = false;
        previewEnded = false;
    }

    preview = shouldPreview;
}

int PlayheadTracker::process (const HostTiming& timing, const juce::MidiBuffer& midi, int numSamples,
                              const TrackerSettings& settings, PlaySegment* segments) noexcept
{
    if (numSamples <= 0 || segments == nullptr)
        return 0;

    const auto hostBpm = finite (timing.bpm);
    const auto useHost = settings.useHostTempo && hostBpm.has_value() && *hostBpm > 0.0;
    const auto requested = useHost ? *hostBpm : settings.manualBpm;
    const auto tempo = std::isfinite (requested) ? juce::jlimit (minTempo, maxTempo, requested) : 120.0;
    lastTempo = tempo;

    auto length = settings.lengthQuarterNotes;

    if (! std::isfinite (length) || length <= 0.0)
        length = defaultLengthQuarterNotes;

    length = std::max (length, shortestLengthQuarterNotes);

    auto barLength = settings.quarterNotesPerBar;

    if (! std::isfinite (barLength) || barLength <= 0.0)
        barLength = 4.0;

    const auto rate = tempo / 60.0 / sampleRate / length;
    Writer out (segments, numSamples);

    // The transport stopping, as opposed to being stopped (notes still play then), ends a pass,
    // and the keys counted as held are forgotten (some hosts send no note-offs on stop), even
    // during the preview.
    const auto transportStopped = wasPlaying && ! timing.isPlaying;
    wasPlaying = timing.isPlaying;

    if (transportStopped)
        heldNotes.reset();

    // Anything but the transport counting from the host's time leaves its count stale, and
    // anything but the single pass leaves the song's place stale for it (coming back to it is like
    // a relocation).
    if (preview || settings.mode != TriggerMode::transport)
        timeAnchored = false;

    if (preview || settings.mode != TriggerMode::transport || settings.repeat)
        songExpected = false;

    if (preview)
    {
        processPreview (midi, rate, settings.repeat, out);
        return out.getCount();
    }

    if (settings.mode != TriggerMode::midiNote)
    {
        // Notes are not followed outside MIDI mode: what was held is forgotten, so a release
        // missed meanwhile cannot keep a later pass looping.
        passActive = false;
        heldNotes.reset();
    }

    if (settings.mode == TriggerMode::midiNote)
        processMidi (midi, rate, settings.repeat, transportStopped, out);
    else
        processTransport (timing, tempo, useHost, length, barLength, rate, settings, out);

    return out.getCount();
}

void PlayheadTracker::processTransport (const HostTiming& timing, double tempo, bool usingHostTempo, double length,
                                        double barLength, double rate, const TrackerSettings& settings,
                                        Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    const auto repeat = settings.repeat;
    out.setOnce (! repeat);

    if (! timing.isPlaying)
    {
        // The position stays where it stopped, so starting again from there is not a jump.
        const auto held = ! hasExpected ? 0.0
                        : repeat ? wrap (expectedNextPosition)
                                 : (expectedNextPosition >= 0.0 && expectedNextPosition < 1.0 ? expectedNextPosition : 0.0);
        out.add (numSamples, false, held, 0.0, false);
        return;
    }

    if (! repeat)
    {
        processSinglePass (timing, tempo, usingHostTempo, length, barLength, settings.startBar, out);
        return;
    }

    const auto ppq = finite (timing.ppqPosition);
    const auto seconds = finite (timing.timeInSeconds);

    // Quarter notes since the song start, in the tempo the drawing plays at, and where the bar
    // lines fall in them. The host's ppq only counts in that tempo when the tempo is the host's;
    // with a typed-in tempo the position comes from the time instead.
    std::optional<double> quarterNotes;
    auto barOffset = 0.0;

    if (usingHostTempo && ppq.has_value())
    {
        quarterNotes = *ppq;
        timeAnchored = false;

        // At the rate the host's ppq really moves, even at tempos outside 20..400, or every
        // block would land somewhere other than where the last one led.
        rate = juce::jlimit (minHostTempo, maxHostTempo, *timing.bpm) / 60.0 / sampleRate / length;

        // Bar lines on multiples of the bar (VST3 gives no bar number, so which bar of a longer
        // drawing a bar starts is arbitrary) or, under a bar, on the drawing's start: the drawing
        // starts on them even after a time-signature change or a pickup bar.
        if (const auto barStart = finite (timing.ppqPositionOfLastBarStart))
            barOffset = modulo (*barStart, std::min (length, barLength));
    }
    else if (seconds.has_value())
    {
        quarterNotes = countFromTime (*seconds, tempo, numSamples);
    }

    if (! quarterNotes.has_value())
    {
        // Nothing usable from the host: keep running from where the last block ended.
        runOn (rate, false, out);
        return;
    }

    const auto span = (rate - lastRate) * lastLength;
    const auto tolerance = jumpToleranceSeconds * sampleRate * rate;
    const auto position = wrap (modulo (*quarterNotes - barOffset, length) / length);
    const auto jump = ! hasExpected || ! followsOn (position, expectedNextPosition, span, tolerance, true);

    out.add (numSamples, true, position, rate, jump);
    expectNext (position, rate, numSamples, true);
}

double PlayheadTracker::countFromTime (double seconds, double tempo, int numSamples) noexcept
{
    // Anchored to the time when playback starts or the host relocates, then counted on at each
    // block's tempo, so changing the tempo while playing bends the position instead of moving it
    // (by the whole song's length times the change).
    if (! timeAnchored || std::abs (seconds - expectedNextSeconds) > jumpToleranceSeconds)
        timeQuarterNotes = seconds * tempo / 60.0;

    const auto quarterNotes = timeQuarterNotes;
    timeQuarterNotes += numSamples / sampleRate * tempo / 60.0;
    expectedNextSeconds = seconds + numSamples / sampleRate;
    timeAnchored = true;
    return quarterNotes;
}

void PlayheadTracker::processSinglePass (const HostTiming& timing, double tempo, bool usingHostTempo, double length,
                                         double barLength, int startBar, Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    const auto ppq = finite (timing.ppqPosition);
    const auto hostBpm = finite (timing.bpm);
    const auto seconds = finite (timing.timeInSeconds);
    const auto wasCounting = std::exchange (passElapsedCounting, false);

    // Where the song is, in quarter notes since its start, and how far that moves per sample.
    // With the host's ppq and tempo that is the DAW's own count whatever tempo the drawing plays
    // at, so the start bar is the DAW's bar; without them it is counted from the time.
    const auto hostCount = ppq.has_value() && hostBpm.has_value() && *hostBpm > 0.0;
    std::optional<double> song;
    std::optional<double> barStart;
    auto songRate = tempo / 60.0 / sampleRate;

    if (hostCount)
    {
        song = *ppq;
        barStart = finite (timing.ppqPositionOfLastBarStart);
        timeAnchored = false;

        // At the rate the host's ppq really moves, even at tempos outside 20..400, or every
        // block would land somewhere other than where the last one led.
        songRate = juce::jlimit (minHostTempo, maxHostTempo, *hostBpm) / 60.0 / sampleRate;
    }
    else if (seconds.has_value())
    {
        song = countFromTime (*seconds, tempo, numSamples);
    }

    if (! song.has_value())
    {
        // Nothing usable from the host: the pass runs on from where the last block ended.
        songExpected = false;
        const auto rate = tempo / 60.0 / sampleRate / (passPlaced ? passLength : length);
        const auto position = hasExpected ? expectedNextPosition : 0.0;

        playSinglePass (position, rate, ! hasExpected, out);
        expectNext (position, rate, numSamples, false);
        return;
    }

    const auto follows = placePass (*song, songRate, barStart, length, barLength, startBar, numSamples);
    auto position = (*song - passStart) / passLength;
    auto rate = songRate / passLength;

    if (hostCount && ! usingHostTempo)
    {
        // A typed-in tempo: the pass starts where the DAW reaches its start bar and plays at the
        // typed tempo from there, counted on (so a change of the typed tempo bends the position).
        // Before the pass, and when the DAW moves into it, the count is where the DAW's position
        // puts it at the DAW's current tempo.
        const auto quarterNotesPerSample = tempo / 60.0 / sampleRate;

        if (! wasCounting || ! follows || *song < passStart)
            passElapsed = (*song - passStart) / songRate * quarterNotesPerSample;

        position = passElapsed / passLength;
        rate = quarterNotesPerSample / passLength;
        passElapsed += quarterNotesPerSample * numSamples;
        passElapsedCounting = true;
    }

    const auto span = (rate - lastRate) * lastLength;
    const auto tolerance = jumpToleranceSeconds * sampleRate * rate;
    const auto jump = ! hasExpected || ! followsOn (position, expectedNextPosition, span, tolerance, false);

    playSinglePass (position, rate, jump, out);
    expectNext (position, rate, numSamples, false);
}

bool PlayheadTracker::placePass (double song, double songRate, std::optional<double> barStart, double length,
                                 double barLength, int startBar, int numSamples) noexcept
{
    startBar = juce::jlimit (1, lastStartBar, startBar);

    // Whether the song plays on from the last block (as the position does, see followsOn), and
    // whether Length changed rather than the time signature (which also changes the length).
    const auto songTolerance = jumpToleranceSeconds * sampleRate * songRate;
    const auto follows = songExpected
                      && followsOn (song, expectedSong, (songRate - lastSongRate) * lastSongLength, songTolerance, false);
    const auto newLength = ! juce::exactlyEqual (length, lastLengthSetting)
                        && juce::exactlyEqual (barLength, lastBarLengthSetting);

    // Moved to before the pass: its place still holds if the song is somewhere its bars were
    // counted through since the last time-signature change (in that signature). Further back, a
    // change may lie between the song and the pass, to be counted again.
    const auto movedBefore = ! follows && song < passStart - songTolerance;
    const auto counted = song >= passCountedFrom - songTolerance && juce::exactlyEqual (barLength, passBarLength);

    if (! passPlaced || startBar != passStartBar || (movedBefore && ! counted))
    {
        // Placed afresh. VST3 gives no bar numbers, so bar N is N - 1 bars in the current time
        // signature from the song start, moved to the nearest of the host's bar lines (the
        // earlier one half way): after an odd bar (a 2/4 bar in 4/4, a pickup) that is still a
        // bar line, and the DAW's bar N after a shorter bar.
        const auto nominal = static_cast<double> (startBar - 1) * barLength;
        passStart = nominal;

        if (barStart.has_value())
        {
            const auto snapped = *barStart + std::ceil ((nominal - *barStart) / barLength - 0.5 - 1.0e-9) * barLength;

            // (A host's bar start can be a hair off; only a real shift moves the pass.)
            if (std::abs (snapped - nominal) > 1.0e-6)
                passStart = snapped;
        }

        passStartBar = startBar;
        passBarLength = barLength;
        passCountedFrom = barStart.value_or (song);
        passLength = length;
        passPlaced = true;
    }
    else if (song < passStart)
    {
        // Playing on towards it (or moved to where it was counted): the bars still to go are
        // counted in the bar the song is in, so a new time signature on the way (an odd bar, a new
        // section) moves the pass with the bars rather than counting all the bars before it again
        // in the new one. Until the song is in the start bar, its length is Length in this bar.
        if (! juce::exactlyEqual (barLength, passBarLength))
        {
            const auto from = barStart.value_or (song);
            passStart = from + (passStart - from) / passBarLength * barLength;
            passBarLength = barLength;
            passCountedFrom = from;
        }

        passLength = length;
    }
    else if (follows && approachingPass)
    {
        // The first block inside the pass, played into: the host now reports the start bar's own
        // time signature (the block that opened the pass was in the bar before), so the length
        // becomes Length in it. The position moves on by at most this block's share of the
        // difference, so what the last block led to is scaled with it, not taken for a jump.
        if (! juce::exactlyEqual (length, passLength))
        {
            expectedNextPosition *= passLength / length;
            lastRate *= passLength / length;
            passLength = length;
        }
    }
    else if (newLength)
    {
        passLength = length;
    }

    // Otherwise, playing in or after the pass, or moved into or after it, it stays where it is: a
    // time signature from its start bar on moves nothing, and a DAW loop over it plays it the same.

    approachingPass = song < passStart;
    expectedSong = song + songRate * numSamples;
    lastSongRate = songRate;
    lastSongLength = numSamples;
    songExpected = true;
    lastLengthSetting = length;
    lastBarLengthSetting = barLength;
    return follows;
}

void PlayheadTracker::playSinglePass (double position, double rate, bool jump, Writer& out) noexcept
{
    // The pass is the positions 0..1 of this block's straight line `position + rate * sample`:
    // sound from its first sample at or after 0 until the gate closes, a gate fade before 1 so
    // the fade-out ends with the drawing (as a MIDI one-shot and a bounce do). Before and after
    // it the gate is closed, so moving into it or out of it fades rather than clicks. A jump
    // (a relocation) carries to the sound, which then fades out where it was before moving.
    const auto numSamples = static_cast<double> (out.getLength());
    auto opens = numSamples;
    auto closes = numSamples;

    if (rate > 0.0)
    {
        // (A thousandth of a sample of slack: a host's ppq can be a hair short of a bar line.)
        opens = juce::jlimit (0.0, numSamples, std::ceil (-position / rate - 1.0e-3));
        closes = juce::jlimit (0.0, numSamples, std::floor ((1.0 - position) / rate - std::min (gateLeadSamples, 0.5 / rate)));
    }
    else if (position >= 0.0 && position < 1.0)
    {
        opens = 0.0;
    }

    if (closes <= opens)
    {
        out.add (out.getLength(), false, 0.0, 0.0, false);
        return;
    }

    out.add (static_cast<int> (opens), false, 0.0, 0.0, false);
    out.add (static_cast<int> (closes), true, juce::jlimit (0.0, 1.0, position + rate * opens), rate, jump);
    out.add (out.getLength(), false, 0.0, 0.0, false);
}

void PlayheadTracker::processMidi (const juce::MidiBuffer& midi, double rate, bool repeat, bool transportStopped,
                                   Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    auto restart = false;   // the next playing segment starts the drawing again

    if (transportStopped)
        passActive = false;

    // With Repeat the pass loops while a key is down; what is held only changes at an event.
    const auto playUntil = [&] (int end) { playPass (end, rate, repeat && heldNotes.any(), restart, out); };

    for (const auto metadata : midi)
    {
        if (! isChannelMessage (metadata))
            continue;

        const auto message = metadata.getMessage();
        const auto at = juce::jlimit (0, numSamples - 1, metadata.samplePosition);

        // All Notes Off and All Sound Off (a panic, and what some hosts send on stop) end the
        // pass where they are.
        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            playUntil (at);
            followNotes (message);
            passActive = false;
            restart = false;
            continue;
        }

        if (message.isNoteOff())
        {
            // With Repeat, letting go of the last key held ends the pass here; otherwise
            // note-offs only keep count of the keys.
            const auto index = noteIndex (message);
            const auto endsPass = repeat && passActive && index >= 0 && heldNotes[static_cast<size_t> (index)]
                               && heldNotes.count() == 1;

            if (endsPass)
                playUntil (at);

            followNotes (message);

            if (endsPass)
            {
                passActive = false;
                restart = false;
            }

            continue;
        }

        if (! message.isNoteOn())
            continue;

        // Each note can add two segments before it and two after the last event: when there is
        // no room left, further notes in this block are merged into the one before (they still
        // count as held). A release or an all-off always counts (the writer merges at worst,
        // and the gate then closes in the next block).
        if (out.getCount() <= maxSegments - 4)
        {
            playUntil (at);
            passActive = true;
            passPosition = 0.0;
            restart = true;
        }

        followNotes (message);
    }

    playUntil (numSamples);

    expectedNextPosition = wrap (passPosition);
    lastLength = 0;
    hasExpected = true;
}

void PlayheadTracker::processPreview (const juce::MidiBuffer& midi, double rate, bool repeat, Writer& out) noexcept
{
    // The audition overrides the trigger, so notes do not play; they are still counted, so a key
    // let go meanwhile is not taken for held afterwards.
    for (const auto metadata : midi)
        if (isChannelMessage (metadata))
            followNotes (metadata.getMessage());

    auto restart = false;

    if (previewStartPending)
    {
        previewStartPending = false;
        previewEnded = false;
        passActive = true;
        passPosition = 0.0;
        restart = true;
    }

    // Once its single pass has played, it stays silent until it is turned off (or on again).
    out.setOnce (! repeat);

    if (! previewEnded)
    {
        playPass (out.getLength(), rate, repeat, restart, out);
        previewEnded = ! passActive;
    }
    else
    {
        out.add (out.getLength(), false, 0.0, 0.0, false);
    }

    expectedNextPosition = wrap (passPosition);
    lastLength = 0;
    hasExpected = true;
}

void PlayheadTracker::playPass (int end, double rate, bool loops, bool& restart, Writer& out) noexcept
{
    // A pass that loops wraps from the end of the drawing to its start, so the engine leads the
    // last column into the first. One that does not is a single pass (the last column holds) and
    // ends where its gate closes: early enough for the fade-out to end with the drawing, as a
    // bounce does, rather than run on into the drawing's start (half way through a pass too
    // short for that). Adds at most two segments.
    out.setOnce (! loops);

    while (out.getCursor() < end)
    {
        if (! passActive)
        {
            out.add (end, false, wrap (passPosition), 0.0, false);
            continue;
        }

        const auto available = end - out.getCursor();

        if (loops)
        {
            out.add (end, true, wrap (passPosition), rate, restart);
            passPosition = wrap (passPosition + rate * available);
            restart = false;
            continue;
        }

        // Samples until the gate closes.
        const auto toClose = rate > 0.0 ? std::floor ((1.0 - passPosition) / rate - std::min (gateLeadSamples, 0.5 / rate))
                                        : 1.0e18;

        if (toClose <= 0.0)
        {
            passActive = false;
            passPosition = 1.0;
            continue;
        }

        if (toClose > static_cast<double> (available))
        {
            out.add (end, true, passPosition, rate, restart);
            passPosition += rate * available;
        }
        else
        {
            out.add (out.getCursor() + static_cast<int> (toClose), true, passPosition, rate, restart);
            passActive = false;
            passPosition = 1.0;
        }

        restart = false;
    }
}

int PlayheadTracker::noteIndex (const juce::MidiMessage& message) noexcept
{
    const auto channel = message.getChannel();
    const auto note = message.getNoteNumber();

    if (channel < 1 || channel > 16 || note < 0 || note > 127)
        return -1;

    return (channel - 1) * 128 + note;
}

void PlayheadTracker::followNotes (const juce::MidiMessage& message) noexcept
{
    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        heldNotes.reset();
        return;
    }

    const auto on = message.isNoteOn();

    if (! on && ! message.isNoteOff())
        return;

    if (const auto index = noteIndex (message); index >= 0)
        heldNotes[static_cast<size_t> (index)] = on;
}

void PlayheadTracker::runOn (double rate, bool forceJump, Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    const auto jump = forceJump || ! hasExpected;
    const auto position = forceJump || ! hasExpected ? 0.0 : wrap (expectedNextPosition);

    out.add (numSamples, true, position, rate, jump);
    expectNext (position, rate, numSamples, true);
}

void PlayheadTracker::expectNext (double position, double rate, int numSamples, bool wraps) noexcept
{
    const auto next = position + rate * numSamples;
    expectedNextPosition = wraps ? wrap (next) : next;
    lastRate = rate;
    lastLength = numSamples;
    hasExpected = true;
}

HostTiming makeHostTiming (const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept
{
    HostTiming timing;

    if (! position.hasValue())
        return timing;

    timing.isPlaying = position->getIsPlaying() || position->getIsRecording();

    const auto bpm = finite (position->getBpm());

    if (bpm.has_value() && *bpm > 0.0)
        timing.bpm = bpm;

    timing.ppqPosition = finite (position->getPpqPosition());
    timing.timeInSeconds = finite (position->getTimeInSeconds());
    timing.ppqPositionOfLastBarStart = finite (position->getPpqPositionOfLastBarStart());
    return timing;
}

} // namespace artfreq
