// PlayheadTracker: where in the drawing playback is, from the transport, MIDI notes, the free
// loop and the preview, and when that position jumps.

#include "TestFramework.h"

#include "dsp/PlayheadTracker.h"

#include <cmath>
#include <limits>
#include <vector>

using freqart::HostTiming;
using freqart::PlaySegment;
using freqart::PlayheadTracker;
using freqart::TrackerSettings;
using freqart::TriggerMode;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int block = 512;

std::vector<PlaySegment> run (PlayheadTracker& tracker, const HostTiming& timing, const TrackerSettings& settings,
                              int numSamples = block, const juce::MidiBuffer& midi = {})
{
    PlaySegment segments[PlayheadTracker::maxSegments];
    const auto count = tracker.process (timing, midi, numSamples, settings, segments);
    return { segments, segments + count };
}

/** The segments follow each other from 0 to numSamples without gaps or empty ones. */
bool covers (const std::vector<PlaySegment>& segments, int numSamples)
{
    auto next = 0;

    for (const auto& segment : segments)
    {
        if (segment.start != next || segment.length <= 0)
            return false;

        next += segment.length;
    }

    return next == numSamples;
}

HostTiming playingAt (double ppq, double bpm = 120.0)
{
    HostTiming timing;
    timing.isPlaying = true;
    timing.bpm = bpm;
    timing.ppqPosition = ppq;
    timing.timeInSeconds = ppq * 60.0 / bpm;
    return timing;
}

/** Quarter notes in one block at a tempo. */
double blockQuarterNotes (double bpm, int numSamples = block) { return numSamples * bpm / 60.0 / sampleRate; }

double rateFor (double bpm, double lengthQuarterNotes) { return bpm / 60.0 / sampleRate / lengthQuarterNotes; }

TrackerSettings settingsFor (TriggerMode mode, double lengthQuarterNotes = 16.0)
{
    TrackerSettings settings;
    settings.mode = mode;
    settings.lengthQuarterNotes = lengthQuarterNotes;
    return settings;
}

juce::MidiBuffer notesAt (std::initializer_list<int> samples)
{
    juce::MidiBuffer midi;

    for (auto sample : samples)
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), sample);

    return midi;
}

} // namespace

FREQART_TEST (trackerTransportFollowsPpq)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport);
    const auto rate = rateFor (120.0, 16.0);

    auto segments = run (tracker, playingAt (4.0), settings);
    CHECK (covers (segments, block));
    CHECK (segments.size() == 1);
    CHECK (segments[0].playing);
    CHECK_NEAR (segments[0].fraction, 0.25, 1.0e-12);
    CHECK_NEAR (segments[0].fractionPerSample, rate, 1.0e-15);
    CHECK (segments[0].jump);   // nothing to follow on from yet
    CHECK_NEAR (tracker.getTempo(), 120.0, 1.0e-12);

    segments = run (tracker, playingAt (4.0 + blockQuarterNotes (120.0)), settings);
    CHECK (segments.size() == 1);
    CHECK_NEAR (segments[0].fraction, 0.25 + block * rate, 1.0e-12);
    CHECK (! segments[0].jump);

    // Before the song start (pre-roll) the position still lands inside the drawing.
    PlayheadTracker preRoll;
    preRoll.prepare (sampleRate);
    segments = run (preRoll, playingAt (-1.0), settings);
    CHECK_NEAR (segments[0].fraction, 15.0 / 16.0, 1.0e-12);
}

FREQART_TEST (trackerTransportWithTimeOnly)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport);

    HostTiming timing;
    timing.isPlaying = true;
    timing.bpm = 120.0;
    timing.timeInSeconds = 2.0;   // 4 quarter notes at 120

    auto segments = run (tracker, timing, settings);
    CHECK_NEAR (segments[0].fraction, 0.25, 1.0e-12);

    timing.timeInSeconds = 2.0 + block / sampleRate;
    segments = run (tracker, timing, settings);
    CHECK_NEAR (segments[0].fraction, 0.25 + block * rateFor (120.0, 16.0), 1.0e-12);
    CHECK (! segments[0].jump);

    // No tempo from the host at all: the typed-in tempo stands in.
    PlayheadTracker noTempo;
    noTempo.prepare (sampleRate);
    auto fallback = settings;
    fallback.manualBpm = 90.0;
    HostTiming timeOnly;
    timeOnly.isPlaying = true;
    timeOnly.timeInSeconds = 2.0;   // 3 quarter notes at 90
    segments = run (noTempo, timeOnly, fallback);
    CHECK_NEAR (segments[0].fraction, 3.0 / 16.0, 1.0e-12);
    CHECK_NEAR (segments[0].fractionPerSample, rateFor (90.0, 16.0), 1.0e-15);
    CHECK_NEAR (noTempo.getTempo(), 90.0, 1.0e-12);

    // Nothing usable at all: it runs on by itself.
    HostTiming bare;
    bare.isPlaying = true;
    segments = run (noTempo, bare, fallback);
    CHECK (segments[0].playing);
    CHECK (! segments[0].jump);
    CHECK_NEAR (segments[0].fraction, 3.0 / 16.0 + block * rateFor (90.0, 16.0), 1.0e-12);
}

FREQART_TEST (trackerManualTempo)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    auto settings = settingsFor (TriggerMode::transport);
    settings.useHostTempo = false;
    settings.manualBpm = 60.0;

    // The host plays at 120, the drawing at 60: its position comes from the time.
    auto segments = run (tracker, playingAt (8.0, 120.0), settings);   // 4 s
    CHECK_NEAR (segments[0].fraction, 4.0 / 16.0, 1.0e-12);
    CHECK_NEAR (segments[0].fractionPerSample, rateFor (60.0, 16.0), 1.0e-15);
    CHECK_NEAR (tracker.getTempo(), 60.0, 1.0e-12);

    segments = run (tracker, playingAt (8.0 + blockQuarterNotes (120.0), 120.0), settings);
    CHECK (! segments[0].jump);

    // The tempo is kept in range.
    settings.manualBpm = 1000.0;
    run (tracker, playingAt (9.0), settings);
    CHECK_NEAR (tracker.getTempo(), 400.0, 1.0e-12);
    settings.useHostTempo = true;
    run (tracker, playingAt (9.0, 5.0), settings);
    CHECK_NEAR (tracker.getTempo(), 20.0, 1.0e-12);
}

FREQART_TEST (trackerLoopWrapIsNotAJump)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport);
    const auto start = 16.0 - 0.01;

    auto segments = run (tracker, playingAt (start), settings);
    CHECK_NEAR (segments[0].fraction, start / 16.0, 1.0e-12);

    segments = run (tracker, playingAt (start + blockQuarterNotes (120.0)), settings);
    CHECK_NEAR (segments[0].fraction, (start + blockQuarterNotes (120.0) - 16.0) / 16.0, 1.0e-12);
    CHECK (! segments[0].jump);

    // Further on, the second pass through the drawing.
    segments = run (tracker, playingAt (start + 2.0 * blockQuarterNotes (120.0)), settings);
    CHECK (! segments[0].jump);
}

FREQART_TEST (trackerRelocationIsAJump)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport);
    const auto qnPerSample = blockQuarterNotes (120.0, 1);

    run (tracker, playingAt (4.0), settings);
    auto segments = run (tracker, playingAt (10.0), settings);
    CHECK (segments[0].jump);
    CHECK_NEAR (segments[0].fraction, 10.0 / 16.0, 1.0e-12);

    // Within two samples of where it should be: not a jump (hosts round). Three: a jump.
    auto ppq = 10.0 + blockQuarterNotes (120.0) + 1.5 * qnPerSample;
    segments = run (tracker, playingAt (ppq), settings);
    CHECK (! segments[0].jump);

    ppq += blockQuarterNotes (120.0) + 3.0 * qnPerSample;
    segments = run (tracker, playingAt (ppq), settings);
    CHECK (segments[0].jump);

    // Changing the length moves the position: a jump. Changing to the free loop continues.
    ppq += blockQuarterNotes (120.0);
    segments = run (tracker, playingAt (ppq), settingsFor (TriggerMode::transport, 8.0));
    CHECK (segments[0].jump);

    const auto loop = run (tracker, playingAt (ppq + blockQuarterNotes (120.0)), settingsFor (TriggerMode::freeLoop, 8.0));
    CHECK (! loop[0].jump);
    CHECK_NEAR (loop[0].fraction, segments[0].fraction + block * rateFor (120.0, 8.0), 1.0e-12);
}

FREQART_TEST (trackerStoppedIsNotPlaying)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport);

    HostTiming stopped;
    stopped.bpm = 120.0;
    stopped.ppqPosition = 4.0;

    auto segments = run (tracker, stopped, settings);
    CHECK (covers (segments, block));
    CHECK (segments.size() == 1);
    CHECK (! segments[0].playing);

    // Play, stop, and play on from the same place: not a jump. From elsewhere: a jump.
    run (tracker, playingAt (4.0), settings);
    const auto ppq = 4.0 + blockQuarterNotes (120.0);
    stopped.ppqPosition = ppq;
    segments = run (tracker, stopped, settings);
    CHECK (! segments[0].playing);
    CHECK_NEAR (segments[0].fraction, ppq / 16.0, 1.0e-12);

    segments = run (tracker, playingAt (ppq), settings);
    CHECK (segments[0].playing);
    CHECK (! segments[0].jump);

    run (tracker, stopped, settings);
    segments = run (tracker, playingAt (1.0), settings);
    CHECK (segments[0].jump);
}

FREQART_TEST (trackerMidiOneShot)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::midiNote, 1.0);   // one beat: 24000 samples at 120
    const auto rate = rateFor (120.0, 1.0);
    HostTiming stopped;
    stopped.bpm = 120.0;

    // Nothing until a note.
    auto segments = run (tracker, stopped, settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);

    segments = run (tracker, stopped, settings, block, notesAt ({ 100 }));
    CHECK (covers (segments, block));
    CHECK (segments.size() == 2);
    CHECK (! segments[0].playing);
    CHECK (segments[1].start == 100);
    CHECK (segments[1].playing);
    CHECK (segments[1].jump);
    CHECK_NEAR (segments[1].fraction, 0.0, 1.0e-15);
    CHECK_NEAR (segments[1].fractionPerSample, rate, 1.0e-15);

    // It plays to the end of the drawing, then stops, splitting that block.
    auto played = segments[1].length;
    auto endChecked = false;

    for (int b = 0; b < 60; ++b)
    {
        segments = run (tracker, stopped, settings);
        CHECK (covers (segments, block));

        for (const auto& segment : segments)
        {
            if (! segment.playing)
                continue;

            CHECK (! segment.jump);
            played += segment.length;

            if (segment.start + segment.length < block)
            {
                // The last playing sample is before 1, the next would be at or past it.
                CHECK_LESS (segment.fraction + (segment.length - 1) * rate, 1.0);
                CHECK_GREATER (segment.fraction + segment.length * rate, 1.0 - 1.0e-9);
                endChecked = true;
            }
        }
    }

    CHECK (endChecked);
    CHECK_NEAR (played, 24000, 1);
    CHECK (! segments.back().playing);
}

FREQART_TEST (trackerMidiRetrigger)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::midiNote);
    HostTiming timing;

    run (tracker, timing, settings, block, notesAt ({ 0 }));

    // A second note restarts the drawing at its sample; note-offs change nothing.
    auto midi = notesAt ({ 200 });
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 50);
    auto segments = run (tracker, timing, settings, block, midi);
    CHECK (covers (segments, block));
    CHECK (segments.size() == 2);
    CHECK (segments[0].playing && ! segments[0].jump);
    CHECK_NEAR (segments[0].fraction, block * rateFor (120.0, 16.0), 1.0e-12);
    CHECK (segments[1].start == 200 && segments[1].playing && segments[1].jump);
    CHECK_NEAR (segments[1].fraction, 0.0, 1.0e-15);

    // Two notes on one sample are one restart; a note at sample 0 needs no split.
    segments = run (tracker, timing, settings, block, notesAt ({ 0, 0 }));
    CHECK (segments.size() == 1);
    CHECK (segments[0].jump);

    // A flood of notes stays within the segment limit and still covers the block in order.
    juce::MidiBuffer flood;

    for (int i = 0; i < 300; ++i)
        flood.addEvent (juce::MidiMessage::noteOn (1, 40 + i % 40, 1.0f), i * 13);

    segments = run (tracker, timing, settingsFor (TriggerMode::midiNote, 0.01), 4096, flood);
    CHECK (covers (segments, 4096));
    CHECK (segments.size() <= static_cast<size_t> (PlayheadTracker::maxSegments));
    CHECK (segments.size() > 30);

    // Leaving MIDI mode forgets the note.
    segments = run (tracker, timing, settingsFor (TriggerMode::transport));
    CHECK (! segments[0].playing);
}

FREQART_TEST (trackerFreeLoop)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::freeLoop, 1.0);   // 24000 samples
    const auto rate = rateFor (120.0, 1.0);
    HostTiming stopped;
    stopped.bpm = 120.0;

    auto segments = run (tracker, stopped, settings);
    CHECK (segments.size() == 1);
    CHECK (segments[0].playing);
    CHECK_NEAR (segments[0].fraction, 0.0, 1.0e-15);

    auto expected = block * rate;
    auto wrapped = false;
    auto continuous = true;

    for (int b = 1; b < 60; ++b)
    {
        segments = run (tracker, stopped, settings);
        continuous = continuous && segments.size() == 1 && segments[0].playing && ! segments[0].jump
                     && std::abs (segments[0].fraction - expected) < 1.0e-9;
        wrapped = wrapped || segments[0].fraction < block * rate;
        expected += block * rate;

        if (expected >= 1.0)
            expected -= 1.0;
    }

    CHECK (continuous);
    CHECK (wrapped);

    // reset() starts again from the top.
    tracker.reset();
    segments = run (tracker, stopped, settings);
    CHECK_NEAR (segments[0].fraction, 0.0, 1.0e-15);
}

FREQART_TEST (trackerPreview)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    HostTiming stopped;
    stopped.bpm = 120.0;
    stopped.ppqPosition = 7.0;

    for (auto mode : { TriggerMode::transport, TriggerMode::midiNote, TriggerMode::freeLoop })
    {
        const auto settings = settingsFor (mode);
        run (tracker, playingAt (6.0), settings);

        tracker.setPreview (true);
        auto segments = run (tracker, stopped, settings);
        CHECK (segments.size() == 1);
        CHECK (segments[0].playing);
        CHECK (segments[0].jump);
        CHECK_NEAR (segments[0].fraction, 0.0, 1.0e-15);

        segments = run (tracker, stopped, settings);
        CHECK (segments[0].playing);
        CHECK (! segments[0].jump);
        CHECK_NEAR (segments[0].fraction, block * rateFor (120.0, 16.0), 1.0e-12);

        // Turning it on again while on changes nothing.
        tracker.setPreview (true);
        segments = run (tracker, stopped, settings);
        CHECK (! segments[0].jump);

        tracker.setPreview (false);
        segments = run (tracker, stopped, settings);

        if (mode == TriggerMode::freeLoop)
            CHECK (segments[0].playing && ! segments[0].jump);
        else
            CHECK (! segments[0].playing);
    }
}

FREQART_TEST (trackerLengthWithoutTimeSignature)
{
    // The tracker only knows lengths in quarter notes: a bar of 3/4 is 3, of 6/8 is 3, of 7/8 3.5.
    for (const auto length : { 3.0, 3.5, 6.0, 0.5 })
    {
        PlayheadTracker tracker;
        tracker.prepare (sampleRate);
        const auto settings = settingsFor (TriggerMode::transport, length);
        const auto ppq = 4.25;

        auto segments = run (tracker, playingAt (ppq), settings);
        CHECK_NEAR (segments[0].fraction, std::fmod (ppq, length) / length, 1.0e-12);
        CHECK_NEAR (segments[0].fractionPerSample, rateFor (120.0, length), 1.0e-15);

        segments = run (tracker, playingAt (ppq + blockQuarterNotes (120.0)), settings);
        CHECK (! segments[0].jump);
    }
}

FREQART_TEST (trackerMakeHostTiming)
{
    CHECK (! freqart::makeHostTiming ({}).isPlaying);
    CHECK (! freqart::makeHostTiming ({}).bpm.has_value());

    juce::AudioPlayHead::PositionInfo info;
    info.setIsPlaying (true);
    info.setBpm (128.0);
    info.setPpqPosition (3.5);
    info.setTimeInSeconds (1.640625);

    const auto timing = freqart::makeHostTiming (info);
    CHECK (timing.isPlaying);
    CHECK (timing.bpm.has_value() && std::abs (*timing.bpm - 128.0) < 1.0e-12);
    CHECK (timing.ppqPosition.has_value() && std::abs (*timing.ppqPosition - 3.5) < 1.0e-12);
    CHECK (timing.timeInSeconds.has_value() && std::abs (*timing.timeInSeconds - 1.640625) < 1.0e-12);

    juce::AudioPlayHead::PositionInfo partial;
    partial.setBpm (std::numeric_limits<double>::quiet_NaN());
    const auto sparse = freqart::makeHostTiming (partial);
    CHECK (! sparse.isPlaying);
    CHECK (! sparse.bpm.has_value());
    CHECK (! sparse.ppqPosition.has_value());
    CHECK (! sparse.timeInSeconds.has_value());
}
