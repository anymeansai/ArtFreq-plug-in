#include "PlayheadTracker.h"

#include <cmath>

namespace freqart
{

namespace
{
    constexpr double minTempo = 20.0;
    constexpr double maxTempo = 400.0;
    constexpr double jumpToleranceSamples = 2.0;
    constexpr double defaultLengthQuarterNotes = 16.0;
    constexpr double shortestLengthQuarterNotes = 1.0 / 64.0;

    /** x wrapped into [0, 1). */
    double wrap (double x) noexcept
    {
        auto wrapped = x - std::floor (x);
        return wrapped >= 1.0 ? 0.0 : wrapped;
    }

    /** Whether `position` continues from `expected`, allowing for the wrap from 1 to 0. */
    bool followsOn (double position, double expected, double rate) noexcept
    {
        auto difference = position - expected;
        difference -= std::round (difference);
        return std::abs (difference) <= jumpToleranceSamples * rate + 1.0e-12;
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
};

void PlayheadTracker::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 && std::isfinite (newSampleRate) ? newSampleRate : 48000.0;
    reset();
}

void PlayheadTracker::reset() noexcept
{
    previewStartPending = preview;
    expectedNextPosition = 0.0;
    hasExpected = false;
    oneShotActive = false;
    oneShotPosition = 0.0;
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

    const auto rate = tempo / 60.0 / sampleRate / length;
    Writer out (segments, numSamples);

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
        case TriggerMode::midiNote:   processMidi (midi, rate, out); break;
        case TriggerMode::freeLoop:   runOn (rate, false, out); break;
        case TriggerMode::transport:
        default:                      processTransport (timing, tempo, useHost, length, rate, out); break;
    }

    return out.getCount();
}

void PlayheadTracker::processTransport (const HostTiming& timing, double tempo, bool usingHostTempo,
                                        double length, double rate, Writer& out) noexcept
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

    // Quarter notes since the song start, in the tempo the drawing plays at. The host's ppq
    // only counts in that tempo when the tempo is the host's; with a typed-in tempo the
    // position comes from the time instead.
    std::optional<double> quarterNotes;

    if (usingHostTempo && ppq.has_value())
        quarterNotes = *ppq;
    else if (seconds.has_value())
        quarterNotes = *seconds * tempo / 60.0;

    if (! quarterNotes.has_value())
    {
        // Nothing usable from the host: keep running from where the last block ended.
        runOn (rate, false, out);
        return;
    }

    auto inLength = std::fmod (*quarterNotes, length);

    if (inLength < 0.0)
        inLength += length;

    const auto position = wrap (inLength / length);
    const auto jump = ! hasExpected || ! followsOn (position, expectedNextPosition, rate);

    out.add (numSamples, true, position, rate, jump);
    expectedNextPosition = wrap (position + rate * numSamples);
    hasExpected = true;
}

void PlayheadTracker::processMidi (const juce::MidiBuffer& midi, double rate, Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    auto restart = false;   // the next playing segment starts the drawing again

    // Plays (or rests) from the writer's cursor up to `end`, splitting where a one-shot
    // reaches the end of the drawing. Adds at most two segments.
    const auto playUntil = [&] (int end)
    {
        while (out.getCursor() < end)
        {
            if (! oneShotActive)
            {
                out.add (end, false, wrap (oneShotPosition), 0.0, false);
                continue;
            }

            // Samples until the position reaches 1 (sample k is the first at or past it).
            const auto toEnd = rate > 0.0 ? std::ceil ((1.0 - oneShotPosition) / rate) : 1.0e18;

            if (toEnd <= 0.0)
            {
                oneShotActive = false;
                oneShotPosition = 1.0;
                continue;
            }

            const auto available = end - out.getCursor();

            if (toEnd > static_cast<double> (available))
            {
                out.add (end, true, oneShotPosition, rate, restart);
                oneShotPosition += rate * available;
            }
            else
            {
                out.add (out.getCursor() + static_cast<int> (toEnd), true, oneShotPosition, rate, restart);
                oneShotActive = false;
                oneShotPosition = 1.0;
            }

            restart = false;
        }
    };

    for (const auto metadata : midi)
    {
        if (! metadata.getMessage().isNoteOn())
            continue;

        // Each note can add two segments before it and two after the last note: when there
        // is no room left, further notes in this block are merged into the one before.
        if (out.getCount() > maxSegments - 4)
            continue;

        playUntil (juce::jlimit (0, numSamples - 1, metadata.samplePosition));
        oneShotActive = true;
        oneShotPosition = 0.0;
        restart = true;
    }

    playUntil (numSamples);

    expectedNextPosition = wrap (oneShotPosition);
    hasExpected = true;
}

void PlayheadTracker::runOn (double rate, bool forceJump, Writer& out) noexcept
{
    const auto numSamples = out.getLength();
    const auto jump = forceJump || ! hasExpected;
    const auto position = forceJump || ! hasExpected ? 0.0 : expectedNextPosition;

    out.add (numSamples, true, position, rate, jump);
    expectedNextPosition = wrap (position + rate * numSamples);
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
    return timing;
}

} // namespace freqart
