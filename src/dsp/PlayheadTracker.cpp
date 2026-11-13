#include "PlayheadTracker.h"

#include <cmath>

namespace freqart
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
        happened in), give or take `tolerance`. Allows for the wrap from 1 to 0. */
    bool followsOn (double position, double expected, double span, double tolerance) noexcept
    {
        auto difference = position - expected;
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
    expectedNextPosition = 0.0;
    lastRate = 0.0;
    lastLength = 0;
    hasExpected = false;
    oneShotActive = false;
    oneShotPosition = 0.0;
    wasPlaying = false;
    timeAnchored = false;
}

void PlayheadTracker::setPreview (bool shouldPreview) noexcept
{
    if (shouldPreview && ! preview)
        previewStartPending = true;

    if (! shouldPreview)
        previewStartPending = false;

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

    // The transport stopping, as opposed to being stopped (notes still play then), ends a pass.
    const auto transportStopped = wasPlaying && ! timing.isPlaying;
    wasPlaying = timing.isPlaying;

    // Anything but the transport counting from the host's time leaves its count stale.
    if (preview || settings.mode != TriggerMode::transport)
        timeAnchored = false;

    if (preview)
    {
        // The audition overrides everything, and a note that was playing does not come back
        // when it ends.
        oneShotActive = false;
        runOn (rate, previewStartPending, out);
        previewStartPending = false;
        return out.getCount();
    }

    if (settings.mode != TriggerMode::midiNote)
        oneShotActive = false;

    switch (settings.mode)
    {
        case TriggerMode::midiNote:   processMidi (midi, rate, transportStopped, out); break;
        case TriggerMode::freeLoop:   runOn (rate, false, out); break;
        case TriggerMode::transport:
        default:                      processTransport (timing, tempo, useHost, length, barLength, rate, out); break;
    }

    return out.getCount();
}

void PlayheadTracker::processTransport (const HostTiming& timing, double tempo, bool usingHostTempo,
                                        double length, double barLength, double rate, Writer& out) noexcept
{
    const auto numSamples = out.getLength();

    if (! timing.isPlaying)
    {
        // The position stays where it stopped, so starting again from there is not a jump.
        out.add (numSamples, false, hasExpected ? expectedNextPosition : 0.0, 0.0, false);
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
        // Anchored to the time when playback starts or the host relocates, then counted on at
        // each block's tempo, so changing the tempo while playing bends the position instead of
        // moving it (by the whole song's length times the change).
        if (! timeAnchored || std::abs (*seconds - expectedNextSeconds) > jumpToleranceSeconds)
            timeQuarterNotes = *seconds * tempo / 60.0;

        quarterNotes = timeQuarterNotes;
        timeQuarterNotes += numSamples / sampleRate * tempo / 60.0;
        expectedNextSeconds = *seconds + numSamples / sampleRate;
        timeAnchored = true;
    }

    if (! quarterNotes.has_value())
    {
        // Nothing usable from the host: keep running from where the last block ended.
        runOn (rate, false, out);
        return;
    }

    const auto position = wrap (modulo (*quarterNotes - barOffset, length) / length);
    const auto span = (rate - lastRate) * lastLength;
    const auto jump = ! hasExpected
                   || ! followsOn (position, expectedNextPosition, span, jumpToleranceSeconds * sampleRate * rate);

    out.add (numSamples, true, position, rate, jump);
    expectNext (position, rate, numSamples);
}

void PlayheadTracker::processMidi (const juce::MidiBuffer& midi, double rate, bool transportStopped,
                                   Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    auto restart = false;   // the next playing segment starts the drawing again

    // The engine holds the last column instead of wrapping round to the first.
    out.setOnce (true);

    if (transportStopped)
        oneShotActive = false;

    // Plays (or rests) from the writer's cursor up to `end`, splitting where a one-shot's gate
    // closes. Adds at most two segments.
    const auto playUntil = [&] (int end)
    {
        while (out.getCursor() < end)
        {
            if (! oneShotActive)
            {
                out.add (end, false, wrap (oneShotPosition), 0.0, false);
                continue;
            }

            // Samples until the gate closes: early enough for its fade-out to end with the
            // drawing, as a bounce does, rather than run on into the drawing's start (half way
            // through a pass too short for that).
            const auto toClose = rate > 0.0 ? std::floor ((1.0 - oneShotPosition) / rate - std::min (gateLeadSamples, 0.5 / rate))
                                            : 1.0e18;

            if (toClose <= 0.0)
            {
                oneShotActive = false;
                oneShotPosition = 1.0;
                continue;
            }

            const auto available = end - out.getCursor();

            if (toClose > static_cast<double> (available))
            {
                out.add (end, true, oneShotPosition, rate, restart);
                oneShotPosition += rate * available;
            }
            else
            {
                out.add (out.getCursor() + static_cast<int> (toClose), true, oneShotPosition, rate, restart);
                oneShotActive = false;
                oneShotPosition = 1.0;
            }

            restart = false;
        }
    };

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        // All Notes Off and All Sound Off (a panic, and what some hosts send on stop) end the
        // pass, although single note-offs do not.
        const auto silences = message.isAllNotesOff() || message.isAllSoundOff();

        if (! message.isNoteOn() && ! silences)
            continue;

        // Each note can add two segments before it and two after the last note: when there
        // is no room left, further notes in this block are merged into the one before. An
        // all-off always counts (the writer merges at worst, and the gate then closes in the
        // next block).
        if (! silences && out.getCount() > maxSegments - 4)
            continue;

        playUntil (juce::jlimit (0, numSamples - 1, metadata.samplePosition));

        if (silences)
        {
            oneShotActive = false;
            restart = false;
            continue;
        }

        oneShotActive = true;
        oneShotPosition = 0.0;
        restart = true;
    }

    playUntil (numSamples);

    expectedNextPosition = wrap (oneShotPosition);
    lastLength = 0;
    hasExpected = true;
}

void PlayheadTracker::runOn (double rate, bool forceJump, Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    const auto jump = forceJump || ! hasExpected;
    const auto position = forceJump || ! hasExpected ? 0.0 : expectedNextPosition;

    out.add (numSamples, true, position, rate, jump);
    expectNext (position, rate, numSamples);
}

void PlayheadTracker::expectNext (double position, double rate, int numSamples) noexcept
{
    expectedNextPosition = wrap (position + rate * numSamples);
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

} // namespace freqart
