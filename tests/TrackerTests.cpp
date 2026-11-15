// PlayheadTracker: where in the drawing playback is, from the transport (looping, or a single
// pass at a bar of the song), MIDI notes (once, or looping while held) and the preview, and when
// that position jumps.

#include "TestFramework.h"
#include "TestPlans.h"

#include "dsp/PlayheadTracker.h"

#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

using artfreq::ArtEngine;
using artfreq::HostTiming;
using artfreq::PlaySegment;
using artfreq::PlayheadTracker;
using artfreq::TrackerSettings;
using artfreq::TriggerMode;

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

/** Settings for a mode. Unless told otherwise, each mode repeats as it always did before Repeat
    was a setting: Transport loops, MIDI notes play once. */
TrackerSettings settingsFor (TriggerMode mode, double lengthQuarterNotes = 16.0)
{
    TrackerSettings settings;
    settings.mode = mode;
    settings.repeat = mode == TriggerMode::transport;
    settings.lengthQuarterNotes = lengthQuarterNotes;
    return settings;
}

TrackerSettings settingsFor (TriggerMode mode, double lengthQuarterNotes, bool repeat, int startBar = 1)
{
    auto settings = settingsFor (mode, lengthQuarterNotes);
    settings.repeat = repeat;
    settings.startBar = startBar;
    return settings;
}

juce::MidiBuffer notesAt (std::initializer_list<int> samples)
{
    juce::MidiBuffer midi;

    for (auto sample : samples)
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), sample);

    return midi;
}

/** A host playing through a tempo map: each block reports the tempo at its first sample and the
    ppq counted sample by sample (as hosts with tempo tracks do, without splitting blocks at
    tempo changes), from ppq 8. `relocateAt` moves the song on by a quarter note at that block.
    Checks every block follows the host's ppq and returns how many after the first are jumps. */
int jumpsThroughTempoMap (const std::function<double (int64_t sample)>& tempoAt, int numBlocks, int blockLength,
                          int relocateAt = -1, const TrackerSettings& settings = settingsFor (TriggerMode::transport))
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    auto ppq = 8.0;
    auto jumps = 0;
    auto follows = true;

    for (int b = 0; b < numBlocks; ++b)
    {
        const auto start = static_cast<int64_t> (b) * blockLength;

        if (b == relocateAt)
            ppq += 1.0;

        HostTiming timing;
        timing.isPlaying = true;
        timing.bpm = tempoAt (start);
        timing.ppqPosition = ppq;
        timing.timeInSeconds = static_cast<double> (start) / sampleRate;

        const auto segments = run (tracker, timing, settings, blockLength);
        follows = follows && segments.size() == 1 && segments[0].playing
                  && std::abs (segments[0].fraction - std::fmod (ppq, 16.0) / 16.0) < 1.0e-9;

        if (b > 0 && segments[0].jump)
            ++jumps;

        for (int i = 0; i < blockLength; ++i)
            ppq += tempoAt (start + i) / 60.0 / sampleRate;
    }

    CHECK (follows);
    return jumps;
}

} // namespace

ARTFREQ_TEST (trackerTransportFollowsPpq)
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

ARTFREQ_TEST (trackerTransportWithTimeOnly)
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

ARTFREQ_TEST (trackerManualTempo)
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

ARTFREQ_TEST (trackerLoopWrapIsNotAJump)
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

ARTFREQ_TEST (trackerRelocationIsAJump)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport);
    const auto qnPerSample = blockQuarterNotes (120.0, 1);

    run (tracker, playingAt (4.0), settings);
    auto segments = run (tracker, playingAt (10.0), settings);
    CHECK (segments[0].jump);
    CHECK_NEAR (segments[0].fraction, 10.0 / 16.0, 1.0e-12);

    // Within 5 ms of where it should be: not a jump (hosts round, and some report the ppq
    // unevenly). Further: a jump.
    auto ppq = 10.0 + blockQuarterNotes (120.0) + 1.5 * qnPerSample;
    segments = run (tracker, playingAt (ppq), settings);
    CHECK (! segments[0].jump);

    ppq += blockQuarterNotes (120.0) - 200.0 * qnPerSample;
    segments = run (tracker, playingAt (ppq), settings);
    CHECK (! segments[0].jump);

    ppq += blockQuarterNotes (120.0) + 480.0 * qnPerSample;
    segments = run (tracker, playingAt (ppq), settings);
    CHECK (segments[0].jump);

    // Changing the length moves the position: a jump. Turning Repeat off where the single pass
    // is at the same place (the pass of bar 3 to 5 at ppq ~10.5, as the 2-bar loop) continues.
    ppq += blockQuarterNotes (120.0);
    segments = run (tracker, playingAt (ppq), settingsFor (TriggerMode::transport, 8.0));
    CHECK (segments[0].jump);

    ppq += blockQuarterNotes (120.0);
    const auto once = run (tracker, playingAt (ppq), settingsFor (TriggerMode::transport, 8.0, false, 3));
    CHECK (once.size() == 1 && once[0].playing && once[0].once && ! once[0].jump);
    CHECK_NEAR (once[0].fraction, segments[0].fraction + block * rateFor (120.0, 8.0), 1.0e-12);

    // ... and another start bar puts it somewhere else: a jump, here out of the pass, so silent.
    ppq += blockQuarterNotes (120.0);
    const auto elsewhere = run (tracker, playingAt (ppq), settingsFor (TriggerMode::transport, 8.0, false, 1));
    CHECK (elsewhere.size() == 1 && ! elsewhere[0].playing);
}

ARTFREQ_TEST (trackerTempoChangesAreNotJumps)
{
    // A tempo change inside a block (the host reports it with the next block), small and large.
    CHECK (jumpsThroughTempoMap ([] (int64_t s) { return s < 20 * 512 + 100 ? 120.0 : 100.0; }, 60, 512) == 0);
    CHECK (jumpsThroughTempoMap ([] (int64_t s) { return s < 20 * 2048 + 10 ? 120.0 : 60.0; }, 60, 2048) == 0);
    CHECK (jumpsThroughTempoMap ([] (int64_t s) { return s < 20 * 512 + 300 ? 120.0 : 121.0; }, 60, 512) == 0);

    // A steep ramp, 120 to 180 over 4 s, in large blocks.
    const auto ramp = [] (int64_t s) { return 120.0 + 60.0 * juce::jlimit (0.0, 1.0, static_cast<double> (s) / (4.0 * sampleRate)); };
    CHECK (jumpsThroughTempoMap (ramp, 110, 2048) == 0);

    // Host tempos outside 20..400: the drawing moves at the host's rate.
    CHECK (jumpsThroughTempoMap ([] (int64_t) { return 450.0; }, 100, 512) == 0);
    CHECK (jumpsThroughTempoMap ([] (int64_t) { return 15.0; }, 100, 512) == 0);

    PlayheadTracker fast;
    fast.prepare (sampleRate);
    const auto segments = run (fast, playingAt (2.0, 450.0), settingsFor (TriggerMode::transport));
    CHECK_NEAR (segments[0].fractionPerSample, rateFor (450.0, 16.0), 1.0e-15);

    // A real relocation is still a jump, ramp or not.
    CHECK (jumpsThroughTempoMap (ramp, 110, 2048, 50) == 1);
    CHECK (jumpsThroughTempoMap ([] (int64_t) { return 120.0; }, 60, 512, 30) == 1);

    // The same inside a single pass (bars 1 to 4, which the song is in throughout).
    const auto once = settingsFor (TriggerMode::transport, 16.0, false);
    CHECK (jumpsThroughTempoMap ([] (int64_t s) { return s < 20 * 512 + 100 ? 120.0 : 100.0; }, 60, 512, -1, once) == 0);
    CHECK (jumpsThroughTempoMap ([] (int64_t s) { return s < 20 * 2048 + 10 ? 120.0 : 60.0; }, 60, 2048, -1, once) == 0);
    CHECK (jumpsThroughTempoMap ([] (int64_t) { return 120.0; }, 60, 512, 30, once) == 1);
}

ARTFREQ_TEST (trackerManualTempoChangesWhilePlaying)
{
    // Transport with a typed-in tempo, a minute into the song: changing the BPM while playing
    // (automation, or dragging the field) bends the position instead of jumping it.
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    auto settings = settingsFor (TriggerMode::transport);
    settings.useHostTempo = false;
    settings.manualBpm = 120.0;

    auto seconds = 60.0;
    const auto at = [&seconds] { return playingAt (seconds * 2.0, 120.0); };

    auto segments = run (tracker, at(), settings);
    CHECK_NEAR (segments[0].fraction, std::fmod (120.0, 16.0) / 16.0, 1.0e-9);   // anchored to the time

    auto jumps = 0;
    auto bends = true;

    for (int b = 0; b < 300; ++b)
    {
        const auto expected = segments[0].fraction + block * segments[0].fractionPerSample;
        seconds += block / sampleRate;
        settings.manualBpm += b < 200 ? 0.02 : 0.0;

        if (b == 250)
            settings.manualBpm += 1.0;   // and a step

        segments = run (tracker, at(), settings);
        jumps += segments[0].jump ? 1 : 0;
        bends = bends && std::abs (segments[0].fraction - (expected - std::floor (expected))) < 1.0e-9
                && std::abs (segments[0].fractionPerSample - rateFor (settings.manualBpm, 16.0)) < 1.0e-15;
    }

    CHECK (jumps == 0);
    CHECK (bends);

    // Stopping and playing on from the same place continues; a relocation re-anchors (a jump).
    seconds += block / sampleRate;
    HostTiming stopped = at();
    stopped.isPlaying = false;
    run (tracker, stopped, settings);
    CHECK (! run (tracker, at(), settings)[0].jump);

    seconds = 30.0;
    segments = run (tracker, at(), settings);
    CHECK (segments[0].jump);
    CHECK_NEAR (segments[0].fraction, std::fmod (30.0 * settings.manualBpm / 60.0, 16.0) / 16.0, 1.0e-9);
}

ARTFREQ_TEST (trackerStartsOnTheHostsBarLines)
{
    // A song of one 4/4 bar, then 3/4 bars from ppq 4 (or a 2/4 pickup bar, then 4/4), played
    // in 32-sample blocks at 120 BPM with the host reporting where each bar starts.
    struct Meter { double from, quarterNotesPerBar; };

    const auto play = [] (std::vector<Meter> meters, double barsPerLength, std::vector<double> barStarts)
    {
        PlayheadTracker tracker;
        tracker.prepare (sampleRate);
        auto jumps = 0;
        std::vector<double> positionsAtBars;

        for (int b = 0; b < 16 * 750; ++b)   // 16 quarter notes of 24000 samples
        {
            const auto ppq = b * 32.0 / 24000.0;
            auto meter = meters.front();

            for (const auto& m : meters)
                if (ppq >= m.from - 1.0e-9)
                    meter = m;

            auto barStart = meter.from;

            while (barStart + meter.quarterNotesPerBar <= ppq + 1.0e-9)
                barStart += meter.quarterNotesPerBar;

            auto timing = playingAt (ppq);
            timing.ppqPositionOfLastBarStart = barStart;

            auto settings = settingsFor (TriggerMode::transport, barsPerLength * meter.quarterNotesPerBar);
            settings.quarterNotesPerBar = meter.quarterNotesPerBar;

            if (barsPerLength < 1.0)
                settings.lengthQuarterNotes = 1.0;   // "1 beat"

            const auto segments = run (tracker, timing, settings, 32);
            jumps += b > 0 && segments[0].jump ? 1 : 0;

            for (const auto bar : barStarts)
                if (std::abs (ppq - bar) < 1.0e-6)
                    positionsAtBars.push_back (segments[0].fraction);
        }

        return std::make_pair (jumps, positionsAtBars);
    };

    // (A host's ppq can be a hair short of the bar start it reports: 0.9999... is 0.)
    const auto near = [] (double position, double target)
    {
        const auto difference = position - target;
        return std::abs (difference - std::round (difference)) < 1.0e-6;
    };

    // "1 bar": every bar starts the drawing, before and after the change, which is seamless.
    auto [jumps, positions] = play ({ { 0.0, 4.0 }, { 4.0, 3.0 } }, 1.0, { 4.0, 7.0, 10.0, 13.0 });
    CHECK (jumps == 0);
    CHECK (positions.size() == 4);

    for (const auto position : positions)
        CHECK (near (position, 0.0));

    // "4 bars": bar lines land on the drawing's bar lines.
    std::tie (jumps, positions) = play ({ { 0.0, 4.0 }, { 4.0, 3.0 } }, 4.0, { 4.0, 7.0, 10.0, 13.0 });
    CHECK (jumps == 0);
    CHECK (positions.size() == 4);

    for (size_t i = 0; i < positions.size(); ++i)
        CHECK (near (positions[i], 0.25 * static_cast<double> ((i + 1) % 4)));

    // A 2/4 pickup bar: the 4/4 bars still start on the drawing's bar lines.
    std::tie (jumps, positions) = play ({ { 0.0, 2.0 }, { 2.0, 4.0 } }, 1.0, { 2.0, 6.0, 10.0, 14.0 });
    CHECK (jumps == 0);

    for (const auto position : positions)
        CHECK (near (position, 0.0));

    std::tie (jumps, positions) = play ({ { 0.0, 2.0 }, { 2.0, 4.0 } }, 4.0, { 6.0, 10.0, 14.0 });

    for (const auto position : positions)
        CHECK (near (position * 4.0, std::round (position * 4.0)));

    // "1 beat" in 7/8 (bars of 3.5 quarter notes): each bar starts it again.
    std::tie (jumps, positions) = play ({ { 0.0, 3.5 } }, 0.0, { 3.5, 7.0, 10.5, 14.0 });
    CHECK (positions.size() == 4);

    for (const auto position : positions)
        CHECK (near (position, 0.0));
}

ARTFREQ_TEST (trackerStoppedIsNotPlaying)
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

ARTFREQ_TEST (trackerMidiOneShot)
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

    // It plays until the gate's fade-out just fits before the end of the drawing, then stops,
    // splitting that block.
    const auto fade = ArtEngine::gateFadeSeconds * sampleRate;
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
                // The fade-out after the last playing sample ends before 1, and only just.
                CHECK_LESS (segment.fraction + (segment.length + fade) * rate, 1.0);
                CHECK_GREATER (segment.fraction + (segment.length + fade + 3.0) * rate, 1.0);
                CHECK (segment.once);
                endChecked = true;
            }
        }
    }

    CHECK (endChecked);
    CHECK_NEAR (played, 24000 - fade, 2);
    CHECK (! segments.back().playing);
}

ARTFREQ_TEST (trackerMidiOneShotEndsWithTheDrawing)
{
    // A one-shot through the engine, 1 bar at 120 (96000 samples), with sound only at the very
    // start of the drawing: nothing of that start comes back at the end, neither while the last
    // column plays nor during the fade-out after it.
    const auto settings = settingsFor (TriggerMode::midiNote, 4.0);
    const auto passSamples = 96000;
    const auto atStart = test::makePlan ({ 5000.0f }, [] (int column, int) { return column < 20 ? 0.3f : 0.0f; });
    const auto everywhere = test::makeConstantPlan ({ 5000.0f }, 0.3f);

    const auto playOnce = [&] (const artfreq::ArtPlan& plan)
    {
        PlayheadTracker tracker;
        tracker.prepare (sampleRate);
        ArtEngine engine;
        engine.prepare (sampleRate, block);
        engine.setPlan (&plan);

        std::vector<float> out;
        PlaySegment segments[PlayheadTracker::maxSegments];

        for (int start = 0; start < passSamples + 12000; start += block)
        {
            const auto midi = start == 0 ? notesAt ({ 0 }) : juce::MidiBuffer();
            const auto count = tracker.process ({}, midi, block, settings, segments);
            std::vector<float> chunk (static_cast<size_t> (block));
            engine.render (chunk.data(), block, segments, count);
            out.insert (out.end(), chunk.begin(), chunk.end());
        }

        CHECK (! engine.isSounding());
        return out;
    };

    const auto start = playOnce (*atStart);
    CHECK_GREATER (test::peak (start, 0, 1500), 0.25);
    CHECK (test::peak (start, 3000) <= 0.0);

    // Sound up to the end: it fades out with the drawing, without a step, and is gone at its end.
    const auto full = playOnce (*everywhere);
    const auto fade = static_cast<size_t> (ArtEngine::gateFadeSeconds * sampleRate);
    CHECK_GREATER (test::peak (full, passSamples - fade - 200, passSamples - fade - 2), 0.28);
    CHECK (test::peak (full, passSamples) <= 0.0);
    const auto fadeSlope = 1.5 / (ArtEngine::gateFadeSeconds * sampleRate) * 0.3;
    CHECK_LESS (test::largestStep (full, 1000), test::sineStepBound ({ 5000.0f }, { 0.3f }, sampleRate) + fadeSlope + 1.0e-4);
}

ARTFREQ_TEST (trackerMidiPassEndsOnAllOffAndStop)
{
    const auto settings = settingsFor (TriggerMode::midiNote);   // 4 bars: 8 s at 120
    HostTiming stopped;
    stopped.bpm = 120.0;

    const auto stillPlaying = [&] (PlayheadTracker& tracker, const HostTiming& timing)
    {
        auto playing = false;

        for (int b = 0; b < 20; ++b)
            for (const auto& segment : run (tracker, timing, settings))
                playing = playing || segment.playing;

        return playing;
    };

    // All Notes Off and All Sound Off end the pass where they are; a plain note-off does not.
    for (const auto& message : { juce::MidiMessage::allNotesOff (1), juce::MidiMessage::allSoundOff (16) })
    {
        PlayheadTracker tracker;
        tracker.prepare (sampleRate);
        run (tracker, stopped, settings, block, notesAt ({ 0 }));

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 100);
        midi.addEvent (message, 200);
        const auto segments = run (tracker, stopped, settings, block, midi);
        CHECK (covers (segments, block));
        CHECK (segments.size() == 2);
        CHECK (segments[0].playing && segments[0].length == 200);
        CHECK (! segments[1].playing);
        CHECK (! stillPlaying (tracker, stopped));

        // A note afterwards plays again.
        const auto again = run (tracker, stopped, settings, block, notesAt ({ 50 }));
        CHECK (again.size() == 2 && again[1].playing && again[1].jump);
    }

    // The transport stopping ends a pass; notes while it stays stopped still play.
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto playing = playingAt (8.0);
    run (tracker, playing, settings, block, notesAt ({ 0 }));
    CHECK (run (tracker, playing, settings)[0].playing);

    auto segments = run (tracker, stopped, settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);
    CHECK (! stillPlaying (tracker, stopped));

    segments = run (tracker, stopped, settings, block, notesAt ({ 0 }));
    CHECK (segments[0].playing);
    CHECK (stillPlaying (tracker, stopped));
}

ARTFREQ_TEST (trackerMidiRetrigger)
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

ARTFREQ_TEST (trackerPreview)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    HostTiming stopped;
    stopped.bpm = 120.0;
    stopped.ppqPosition = 7.0;

    for (auto [mode, repeat] : { std::pair (TriggerMode::transport, true), std::pair (TriggerMode::transport, false),
                                 std::pair (TriggerMode::midiNote, false), std::pair (TriggerMode::midiNote, true) })
    {
        const auto settings = settingsFor (mode, 16.0, repeat);
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

        // Nothing plays by itself: with the DAW stopped and no note, turning it off is silence.
        tracker.setPreview (false);
        segments = run (tracker, stopped, settings);
        CHECK (! segments[0].playing);
        CHECK (! tracker.hasPreviewEnded());
    }
}

ARTFREQ_TEST (trackerTransportPlaysOnceFromItsStartBar)
{
    // Repeat off, from bar 3 (ppq 8 in 4/4) for 1 bar (4 quarter notes: 96000 samples at 120),
    // played from just before bar 2 to bar 6 in blocks that do not line up with the bars.
    const auto settings = settingsFor (TriggerMode::transport, 4.0, false, 3);
    const auto rate = rateFor (120.0, 4.0);
    const auto qnPerSample = blockQuarterNotes (120.0, 1);
    const auto fade = ArtEngine::gateFadeSeconds * sampleRate;
    const auto first = 4.0 - 37.0 * qnPerSample;   // bar 3 is 96037 samples in

    PlayheadTracker tracker;
    tracker.prepare (sampleRate);

    int64_t firstPlayed = -1, lastPlayed = -1, played = 0;
    auto jumps = 0;
    auto inOrder = true, allOnce = true, inPlace = true;

    for (int b = 0; b < 4 * 96000 / block; ++b)
    {
        const auto at = static_cast<int64_t> (b) * block;
        const auto segments = run (tracker, playingAt (first + static_cast<double> (at) * qnPerSample), settings);
        inOrder = inOrder && covers (segments, block) && segments.size() <= 3;

        for (const auto& segment : segments)
        {
            allOnce = allOnce && segment.once;

            if (! segment.playing)
                continue;

            const auto sample = at + segment.start;
            const auto qn = first + static_cast<double> (sample) * qnPerSample;
            inPlace = inPlace && std::abs (segment.fraction - (qn - 8.0) / 4.0) < 1.0e-9
                      && std::abs (segment.fractionPerSample - rate) < 1.0e-15;
            jumps += segment.jump ? 1 : 0;

            if (firstPlayed < 0)
                firstPlayed = sample;

            lastPlayed = sample + segment.length;
            played += segment.length;
        }
    }

    CHECK (inOrder);
    CHECK (allOnce);
    CHECK (inPlace);
    CHECK (jumps == 0);   // played through from before it: it follows on
    CHECK (firstPlayed == 96037);

    // It ends where the gate's fade-out just fits before the end of the bar, in one piece.
    CHECK_NEAR (static_cast<double> (lastPlayed), 96037.0 + 96000.0 - fade - 1.0, 1.0);
    CHECK (played == lastPlayed - firstPlayed);
}

ARTFREQ_TEST (trackerTransportSinglePassFollowsTheTimeline)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport, 16.0, false);   // bars 1 to 4

    // Starting the DAW in the middle plays from there (a jump: there is nothing before it).
    auto segments = run (tracker, playingAt (6.0), settings);
    CHECK (segments.size() == 1 && segments[0].playing && segments[0].jump && segments[0].once);
    CHECK_NEAR (segments[0].fraction, 6.0 / 16.0, 1.0e-12);
    CHECK_NEAR (segments[0].fractionPerSample, rateFor (120.0, 16.0), 1.0e-15);

    segments = run (tracker, playingAt (6.0 + blockQuarterNotes (120.0)), settings);
    CHECK (segments.size() == 1 && segments[0].playing && ! segments[0].jump);

    // Relocated after it or before it (the count-in before bar 1): silent.
    segments = run (tracker, playingAt (20.0), settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);
    segments = run (tracker, playingAt (-2.0), settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);

    // Back inside: plays from there, as a jump (so the engine fades out whatever it played first).
    segments = run (tracker, playingAt (10.0), settings);
    CHECK (segments.size() == 1 && segments[0].playing && segments[0].jump);
    CHECK_NEAR (segments[0].fraction, 10.0 / 16.0, 1.0e-12);

    // Stopping silences it; playing on from where it stopped follows on.
    const auto ppq = 10.0 + blockQuarterNotes (120.0);
    HostTiming stopped;
    stopped.bpm = 120.0;
    stopped.ppqPosition = ppq;
    segments = run (tracker, stopped, settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);
    segments = run (tracker, playingAt (ppq), settings);
    CHECK (segments.size() == 1 && segments[0].playing && ! segments[0].jump);

    // Repeat on again: the same place in the loop, so it carries on.
    segments = run (tracker, playingAt (ppq + blockQuarterNotes (120.0)), settingsFor (TriggerMode::transport, 16.0, true));
    CHECK (segments.size() == 1 && segments[0].playing && ! segments[0].jump && ! segments[0].once);
}

ARTFREQ_TEST (trackerTransportSinglePassUnderAHostLoop)
{
    // The host loops over the pass (bars 1 to 4 exactly), then over bars 2 to 3 inside it, in
    // blocks of 500 samples (16 quarter notes = 768 of them).
    const auto settings = settingsFor (TriggerMode::transport, 16.0, false);
    const auto fade = ArtEngine::gateFadeSeconds * sampleRate;
    const auto qnPerBlock = blockQuarterNotes (120.0, 500);

    const auto cycle = [&] (double loopStart, double loopEnd, int cycles, std::vector<int>& playedPerCycle)
    {
        PlayheadTracker tracker;
        tracker.prepare (sampleRate);
        auto jumpsAtWraps = 0, otherJumps = 0;
        const auto blocksPerCycle = juce::roundToInt ((loopEnd - loopStart) / qnPerBlock);

        for (int c = 0; c < cycles; ++c)
        {
            auto played = 0;

            for (int b = 0; b < blocksPerCycle; ++b)
            {
                const auto segments = run (tracker, playingAt (loopStart + b * qnPerBlock), settings, 500);

                for (const auto& segment : segments)
                {
                    if (! segment.playing)
                        continue;

                    played += segment.length;

                    if (segment.jump)
                        (b == 0 && c > 0 ? jumpsAtWraps : otherJumps) += 1;
                }
            }

            playedPerCycle.push_back (played);
        }

        return std::make_pair (jumpsAtWraps, otherJumps);
    };

    // Each time round it plays again from the start, the whole pass but its last gate fade.
    std::vector<int> played;
    auto [atWraps, others] = cycle (0.0, 16.0, 3, played);
    CHECK (atWraps == 2);
    CHECK (others == 1);   // the very first block: nothing to follow on from

    for (const auto samples : played)
        CHECK_NEAR (samples, 384000.0 - fade - 1.0, 1.0);

    // A loop inside the pass plays all of it every time; each wrap is a jump back.
    played.clear();
    std::tie (atWraps, others) = cycle (4.0, 12.0, 3, played);
    CHECK (atWraps == 2);

    for (const auto samples : played)
        CHECK (samples == 192000);
}

ARTFREQ_TEST (trackerTransportSinglePassInTheHostsMeterAndTempo)
{
    // 3/4 at 90 BPM: "1 bar" is 3 quarter notes and bar 4 starts at ppq 9.
    auto settings = settingsFor (TriggerMode::transport, 3.0, false, 4);
    settings.quarterNotesPerBar = 3.0;

    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    auto segments = run (tracker, playingAt (8.9, 90.0), settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);

    segments = run (tracker, playingAt (9.5, 90.0), settings);
    CHECK (segments.size() == 1 && segments[0].playing);
    CHECK_NEAR (segments[0].fraction, 0.5 / 3.0, 1.0e-12);
    CHECK_NEAR (segments[0].fractionPerSample, rateFor (90.0, 3.0), 1.0e-15);

    segments = run (tracker, playingAt (12.1, 90.0), settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);

    // A typed-in tempo still starts on the DAW's bar (bar 2 of 4/4 at ppq 4, 2 s in at 120) and
    // plays at its own tempo from there.
    auto manual = settingsFor (TriggerMode::transport, 16.0, false, 2);
    manual.useHostTempo = false;
    manual.manualBpm = 60.0;

    PlayheadTracker typed;
    typed.prepare (sampleRate);
    segments = run (typed, playingAt (4.0 - blockQuarterNotes (120.0) * 0.5, 120.0), manual);
    CHECK (segments.size() == 2 && ! segments[0].playing && segments[1].playing);
    CHECK (segments.size() == 2 && segments[1].start == block / 2);
    CHECK (segments.size() == 2 && std::abs (segments[1].fractionPerSample - rateFor (60.0, 16.0)) < 1.0e-15);
}

ARTFREQ_TEST (trackerSinglePassFadesAtItsEdges)
{
    // Through the engine: bar 2 for 1 bar (96000 samples) with sound everywhere in the drawing,
    // played from the middle of bar 1 to bar 4. Silence outside the pass, no step at its edges.
    const auto settings = settingsFor (TriggerMode::transport, 4.0, false, 2);
    const auto plan = test::makeConstantPlan ({ 5000.0f }, 0.3f);

    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    ArtEngine engine;
    engine.prepare (sampleRate, block);
    engine.setPlan (plan.get());

    std::vector<float> out;
    PlaySegment segments[PlayheadTracker::maxSegments];
    const auto qnPerSample = blockQuarterNotes (120.0, 1);

    for (int start = 0; start < 6 * 24000; start += block)
    {
        const auto count = tracker.process (playingAt (2.0 + start * qnPerSample), {}, block, settings, segments);
        std::vector<float> chunk (static_cast<size_t> (block));
        engine.render (chunk.data(), block, segments, count);
        out.insert (out.end(), chunk.begin(), chunk.end());
    }

    const auto passStart = static_cast<size_t> (2 * 24000);
    const auto passEnd = passStart + 96000;
    const auto fade = static_cast<size_t> (ArtEngine::gateFadeSeconds * sampleRate);

    CHECK (test::peak (out, 0, passStart) <= 0.0);
    CHECK_GREATER (test::peak (out, passStart + fade, passStart + 2 * fade), 0.28);
    CHECK_GREATER (test::peak (out, passEnd - fade - 200, passEnd - fade - 2), 0.28);
    CHECK (test::peak (out, passEnd) <= 0.0);

    const auto fadeSlope = 1.5 / (ArtEngine::gateFadeSeconds * sampleRate) * 0.3;
    CHECK_LESS (test::largestStep (out), test::sineStepBound ({ 5000.0f }, { 0.3f }, sampleRate) + fadeSlope + 1.0e-4);
}

namespace
{

/** A run of bars of one time signature, from a bar line of the run before. */
struct MeterRun
{
    double from, quarterNotesPerBar;
};

/** Where the bar at `ppq` starts and how long it is, in a song of these runs. */
std::pair<double, double> barAt (const std::vector<MeterRun>& meters, double ppq)
{
    auto barStart = 0.0;

    for (;;)
    {
        auto barLength = meters.front().quarterNotesPerBar;

        for (const auto& run : meters)
            if (barStart >= run.from - 1.0e-9)
                barLength = run.quarterNotesPerBar;

        if (ppq < barStart + barLength - 1.0e-9)
            return { barStart, barLength };

        barStart += barLength;
    }
}

/** What a single pass did over a stretch of the song. */
struct PassTrace
{
    double firstPlayed = -1.0;   // where it started and stopped sounding, in the song's quarter notes
    double lastPlayed = -1.0;
    double played = 0.0;         // how long it sounded in all, in quarter notes
    int jumps = 0;               // sounding segments marked as jumps
    bool inPlace = true;         // every sounding segment at (ppq - start) / length
};

/** Plays a song of these meters at 120 BPM from `from` to `to` (quarter notes) in blocks of 500
    samples, reporting each block's time signature and bar start as a host does (at the block's
    first sample), with Length `lengthBars` in that signature, as the processor gives it. The pass
    is expected at `start` for `length` quarter notes. */
PassTrace playThroughMeters (PlayheadTracker& tracker, const std::vector<MeterRun>& meters, int startBar, double lengthBars,
                             double from, double to, double start, double length)
{
    PassTrace trace;
    const auto qnPerSample = blockQuarterNotes (120.0, 1);
    const auto blockLength = 500;

    for (int b = 0;; ++b)
    {
        const auto ppq = from + b * blockLength * qnPerSample;

        if (ppq >= to)
            break;

        const auto [barStart, barLength] = barAt (meters, ppq);
        auto settings = settingsFor (TriggerMode::transport, lengthBars * barLength, false, startBar);
        settings.quarterNotesPerBar = barLength;

        auto timing = playingAt (ppq);
        timing.ppqPositionOfLastBarStart = barStart;

        for (const auto& segment : run (tracker, timing, settings, blockLength))
        {
            if (! segment.playing)
                continue;

            // (The block that opens the pass is reported in the bar before it, so it may run at the
            // length in that bar's signature; the pass takes the start bar's in the next block.)
            const auto at = ppq + segment.start * qnPerSample;
            trace.inPlace = trace.inPlace && std::abs (segment.fraction - (at - start) / length) < 1.0e-6
                            && (trace.firstPlayed < 0.0 || std::abs (segment.fractionPerSample - qnPerSample / length) < 1.0e-12);
            trace.jumps += segment.jump ? 1 : 0;

            if (trace.firstPlayed < 0.0)
                trace.firstPlayed = at;

            trace.lastPlayed = at + segment.length * qnPerSample;
            trace.played += segment.length * qnPerSample;
        }
    }

    return trace;
}

PassTrace playThroughMeters (const std::vector<MeterRun>& meters, int startBar, double lengthBars, double from, double to,
                             double start, double length)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    return playThroughMeters (tracker, meters, startBar, lengthBars, from, to, start, length);
}

/** The pass sounded once, from `start` (within a sample) to its gate's close before `start + length`,
    in place and without a jump. */
void checkOnePass (const PassTrace& trace, double start, double length, int expectedJumps = 0)
{
    const auto qnPerSample = blockQuarterNotes (120.0, 1);
    const auto lead = (std::ceil (ArtEngine::gateFadeSeconds * sampleRate) + 1.0) * qnPerSample;

    CHECK_NEAR (trace.firstPlayed, start, qnPerSample);
    CHECK_NEAR (trace.lastPlayed, start + length - lead, 2.0 * qnPerSample);
    CHECK_NEAR (trace.played, length - lead, 2.0 * qnPerSample);
    CHECK (trace.jumps == expectedJumps);
    CHECK (trace.inPlace);
}

} // namespace

ARTFREQ_TEST (trackerSinglePassKeepsItsPlaceThroughMeterChanges)
{
    // Played from the song start. 4/4, then 3/4 from bar 9, where the pass starts (a new section):
    // it starts there, from the drawing's start, for Length in the start bar's signature ("1 bar"
    // is 3 quarter notes, "4 bars" 12), although the bars before it were longer.
    checkOnePass (playThroughMeters ({ { 0.0, 4.0 }, { 32.0, 3.0 } }, 9, 1.0, 0.0, 60.0, 32.0, 3.0), 32.0, 3.0);
    checkOnePass (playThroughMeters ({ { 0.0, 4.0 }, { 32.0, 3.0 } }, 9, 4.0, 0.0, 60.0, 32.0, 12.0), 32.0, 12.0);

    // A change inside it: no jump, and it ends where its length puts it.
    checkOnePass (playThroughMeters ({ { 0.0, 4.0 }, { 12.0, 3.0 } }, 2, 4.0, 0.0, 30.0, 4.0, 16.0), 4.0, 16.0);
    checkOnePass (playThroughMeters ({ { 0.0, 4.0 }, { 40.0, 3.0 } }, 9, 4.0, 0.0, 60.0, 32.0, 16.0), 32.0, 16.0);

    // A change after it, to a longer or a shorter bar, does not play it again.
    checkOnePass (playThroughMeters ({ { 0.0, 4.0 }, { 16.0, 7.0 } }, 3, 1.0, 0.0, 40.0, 8.0, 4.0), 8.0, 4.0);
    checkOnePass (playThroughMeters ({ { 0.0, 4.0 }, { 16.0, 6.0 } }, 3, 1.0, 0.0, 40.0, 8.0, 4.0), 8.0, 4.0);   // 12/8
    checkOnePass (playThroughMeters ({ { 0.0, 3.0 }, { 36.0, 4.0 } }, 9, 4.0, 0.0, 60.0, 24.0, 12.0), 24.0, 12.0);

    // The DAW looping over the pass after it played through: each time round from the drawing's
    // start (the 3/4 at the loop start does not move it), each wrap a jump back.
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const std::vector<MeterRun> newSection { { 0.0, 4.0 }, { 32.0, 3.0 } };
    checkOnePass (playThroughMeters (tracker, newSection, 9, 4.0, 0.0, 50.0, 32.0, 12.0), 32.0, 12.0);

    for (int round = 0; round < 2; ++round)
        checkOnePass (playThroughMeters (tracker, newSection, 9, 4.0, 32.0, 50.0, 32.0, 12.0), 32.0, 12.0, 1);
}

ARTFREQ_TEST (trackerSinglePassStartsOnTheDawsBars)
{
    // 4/4 with one 2/4 bar at bar 5 (quarter notes 16 to 18), so bar N starts at 4N - 6 after it.
    // Played from the song start, the bars are counted as they go by: the pass starts on the DAW's
    // bar N, and the odd bar, where the start bar would be a different place, sounds nothing. "1
    // bar" is the start bar's: 2 quarter notes from bar 5, 4 from the bar after it.
    const std::vector<MeterRun> oddBar { { 0.0, 4.0 }, { 16.0, 2.0 }, { 18.0, 4.0 } };

    for (const auto bar : { 4, 5, 6, 7, 9, 10, 20 })
    {
        const auto start = bar <= 5 ? 4.0 * (bar - 1) : 4.0 * bar - 6.0;
        const auto length = bar == 5 ? 2.0 : 4.0;
        checkOnePass (playThroughMeters (oddBar, bar, 1.0, 0.0, start + 8.0, start, length), start, length);
    }

    // Started after the odd bar, the DAW's position is all there is to go on: bar N in 4/4 from the
    // song start (4N - 4), moved to the nearest bar line (the earlier one, half way). That is the
    // DAW's bar N after a shorter bar, and the same bar line every time.
    for (const auto bar : { 9, 10, 20 })
    {
        const auto start = 4.0 * bar - 6.0;
        checkOnePass (playThroughMeters (oddBar, bar, 1.0, 22.5, start + 8.0, start, 4.0), start, 4.0);
    }

    // A 3/4 bar instead (16 to 19): bar N at 4N - 5, counted or the nearest bar line.
    const std::vector<MeterRun> threeFour { { 0.0, 4.0 }, { 16.0, 3.0 }, { 19.0, 4.0 } };
    checkOnePass (playThroughMeters (threeFour, 9, 1.0, 0.0, 40.0, 31.0, 4.0), 31.0, 4.0);
    checkOnePass (playThroughMeters (threeFour, 9, 1.0, 20.0, 40.0, 31.0, 4.0), 31.0, 4.0);

    // A new time signature before the start bar, played through: 3/4 from bar 5, bar 9 at 28.
    // Started again a bar before the pass, after the change, it is still there (counting all the
    // bars in 3/4 would make bar 9 quarter note 24); from the song start it is counted again.
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const std::vector<MeterRun> newSection { { 0.0, 4.0 }, { 16.0, 3.0 } };

    for (const auto from : { 0.0, 25.0, 0.0, 22.0 })
        checkOnePass (playThroughMeters (tracker, newSection, 9, 1.0, from, 40.0, 28.0, 3.0), 28.0, 3.0);
}

ARTFREQ_TEST (trackerSinglePassAtATypedTempoStartsOnTheDawsBar)
{
    // The DAW at 90 BPM in 4/4, the drawing at a typed 120 BPM: "1 bar" from bar 9 starts on the
    // DAW's bar 9 (ppq 32, 21.33 s in: sample 1024000) and plays its 4 quarter notes at 120 BPM
    // (2 s), not where bar 9 of a song at 120 BPM would be.
    auto settings = settingsFor (TriggerMode::transport, 4.0, false, 9);
    settings.useHostTempo = false;
    settings.manualBpm = 120.0;

    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto hostQnPerSample = blockQuarterNotes (90.0, 1);
    const auto rate = rateFor (120.0, 4.0);
    const auto lead = std::ceil (ArtEngine::gateFadeSeconds * sampleRate) + 1.0;
    int64_t firstPlayed = -1, lastPlayed = -1;
    auto jumps = 0;
    auto inPlace = true;

    for (int b = 0; b < 25 * 48000 / block; ++b)
    {
        const auto at = static_cast<int64_t> (b) * block;

        for (const auto& segment : run (tracker, playingAt (static_cast<double> (at) * hostQnPerSample, 90.0), settings))
        {
            if (! segment.playing)
                continue;

            const auto sample = at + segment.start;
            inPlace = inPlace && std::abs (segment.fraction - static_cast<double> (sample - 1024000) * rate) < 1.0e-9
                      && std::abs (segment.fractionPerSample - rate) < 1.0e-15;
            jumps += segment.jump ? 1 : 0;

            if (firstPlayed < 0)
                firstPlayed = sample;

            lastPlayed = sample + segment.length;
        }
    }

    CHECK (firstPlayed == 1024000);
    CHECK_NEAR (static_cast<double> (lastPlayed), 1024000.0 + 96000.0 - lead, 1.0);
    CHECK (jumps == 0);
    CHECK (inPlace);

    // Relocated into it: a quarter note of the DAW (2/3 s) after bar 9 is 4/3 of the drawing's
    // quarter notes at 120 BPM, and it plays on at 120 from there.
    auto segments = run (tracker, playingAt (33.0, 90.0), settings);
    CHECK (segments.size() == 1 && segments[0].playing && segments[0].jump);
    CHECK_NEAR (segments[0].fraction, (4.0 / 3.0) / 4.0, 1.0e-9);
    segments = run (tracker, playingAt (33.0 + blockQuarterNotes (90.0), 90.0), settings);
    CHECK (segments.size() == 1 && segments[0].playing && ! segments[0].jump);
    CHECK_NEAR (segments[0].fraction, (4.0 / 3.0) / 4.0 + rate * block, 1.0e-9);

    // The DAW's tempo changing before bar 9 still starts it on that bar: 60 BPM to ppq 16, then
    // 150, so bar 9 is 16 s + 6.4 s in.
    PlayheadTracker ramped;
    ramped.prepare (sampleRate);
    auto ppq = 0.0;
    firstPlayed = -1;

    for (int64_t at = 0; at < 30 * 48000 && firstPlayed < 0; at += block)
    {
        const auto bpm = ppq < 16.0 - 1.0e-9 ? 60.0 : 150.0;
        auto timing = playingAt (ppq, bpm);
        timing.timeInSeconds = static_cast<double> (at) / sampleRate;

        for (const auto& segment : run (ramped, timing, settings))
            if (segment.playing && firstPlayed < 0)
                firstPlayed = at + segment.start;

        ppq += blockQuarterNotes (bpm);
    }

    CHECK_NEAR (static_cast<double> (firstPlayed), 22.4 * sampleRate, 1.0);
}

ARTFREQ_TEST (trackerMidiRepeatLoopsWhileANoteIsHeld)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::midiNote, 1.0, true);   // one beat: 24000 samples at 120
    const auto rate = rateFor (120.0, 1.0);
    HostTiming stopped;
    stopped.bpm = 120.0;

    const auto keys = [] (std::initializer_list<std::tuple<int, bool, int>> events)
    {
        juce::MidiBuffer midi;

        for (const auto& [note, down, sample] : events)
            midi.addEvent (down ? juce::MidiMessage::noteOn (1, note, 0.8f) : juce::MidiMessage::noteOff (1, note), sample);

        return midi;
    };

    const auto silentFor = [&] (int blocks, const TrackerSettings& s)
    {
        auto silent = true;

        for (int b = 0; b < blocks; ++b)
            for (const auto& segment : run (tracker, stopped, s))
                silent = silent && ! segment.playing;

        return silent;
    };

    // A note starts it from the start, as without Repeat; it is not a single pass.
    auto segments = run (tracker, stopped, settings, block, keys ({ { 60, true, 100 } }));
    CHECK (covers (segments, block) && segments.size() == 2);
    CHECK (segments.size() == 2 && ! segments[0].playing && segments[1].playing && segments[1].jump && ! segments[1].once);

    // Held past the end of the drawing, it goes round: no gate closing, no jump, twice over.
    auto expected = (block - 100) * rate;
    auto loops = true, wrapped = false;

    for (int b = 0; b < 100; ++b)
    {
        segments = run (tracker, stopped, settings);
        loops = loops && segments.size() == 1 && segments[0].playing && ! segments[0].jump && ! segments[0].once
                && std::abs (segments[0].fraction - expected) < 1.0e-9;
        expected += block * rate;

        if (expected >= 1.0)
        {
            expected -= 1.0;
            wrapped = true;
        }
    }

    CHECK (loops);
    CHECK (wrapped);

    // A second key restarts it, as every note-on does. Letting go of the first keeps it going
    // while the second is down; letting go of that ends it there, and it stays silent.
    segments = run (tracker, stopped, settings, block, keys ({ { 64, true, 50 } }));
    CHECK (segments.size() == 2 && segments[1].start == 50 && segments[1].playing && segments[1].jump);
    segments = run (tracker, stopped, settings, block, keys ({ { 60, false, 100 } }));
    CHECK (segments.size() == 1 && segments[0].playing && ! segments[0].jump);
    segments = run (tracker, stopped, settings, block, keys ({ { 64, false, 300 } }));
    CHECK (covers (segments, block) && segments.size() == 2);
    CHECK (segments.size() == 2 && segments[0].playing && segments[0].length == 300 && ! segments[1].playing);
    CHECK (silentFor (100, settings));

    // A key that is not down lets go of nothing; a note-on at velocity 0 is a release.
    run (tracker, stopped, settings, block, keys ({ { 60, true, 0 } }));
    segments = run (tracker, stopped, settings, block, keys ({ { 61, false, 10 } }));
    CHECK (segments.size() == 1 && segments[0].playing);
    juce::MidiBuffer zeroVelocity;
    zeroVelocity.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (0)), 20);
    segments = run (tracker, stopped, settings, block, zeroVelocity);
    CHECK (segments.size() == 2 && segments[0].playing && segments[0].length == 20 && ! segments[1].playing);

    // Repeat turned off while it loops: the pass it is in is its last, ending as a one-shot does.
    run (tracker, stopped, settings, block, keys ({ { 60, true, 0 } }));

    for (int b = 0; b < 60; ++b)
        run (tracker, stopped, settings);

    auto ended = false;
    const auto once = settingsFor (TriggerMode::midiNote, 1.0, false);

    for (int b = 0; b < 60 && ! ended; ++b)
        for (const auto& segment : run (tracker, stopped, once))
            if (segment.playing && segment.start + segment.length < block)
            {
                CHECK (segment.once);
                CHECK_LESS (segment.fraction + (segment.length + ArtEngine::gateFadeSeconds * sampleRate) * rate, 1.0);
                ended = true;
            }

    CHECK (ended);
    CHECK (silentFor (60, once));
    run (tracker, stopped, settings, block, keys ({ { 60, false, 0 } }));

    // All Notes Off ends it with keys still down and forgets them: the next note loops only
    // while its own key is held.
    run (tracker, stopped, settings, block, keys ({ { 60, true, 0 }, { 62, true, 10 } }));
    juce::MidiBuffer panic;
    panic.addEvent (juce::MidiMessage::allNotesOff (1), 100);
    segments = run (tracker, stopped, settings, block, panic);
    CHECK (segments.size() == 2 && segments[0].playing && segments[0].length == 100 && ! segments[1].playing);
    run (tracker, stopped, settings, block, keys ({ { 65, true, 0 } }));
    segments = run (tracker, stopped, settings, block, keys ({ { 65, false, 50 } }));
    CHECK (segments.size() == 2 && segments[0].length == 50 && ! segments[1].playing);

    // So does the DAW stopping.
    run (tracker, playingAt (8.0), settings, block, keys ({ { 60, true, 0 }, { 62, true, 0 } }));
    CHECK (run (tracker, playingAt (8.0 + blockQuarterNotes (120.0)), settings)[0].playing);
    segments = run (tracker, stopped, settings);
    CHECK (segments.size() == 1 && ! segments[0].playing);
    run (tracker, stopped, settings, block, keys ({ { 67, true, 0 } }));
    segments = run (tracker, stopped, settings, block, keys ({ { 67, false, 40 } }));
    CHECK (segments.size() == 2 && segments[0].length == 40 && ! segments[1].playing);

    // Without Repeat, letting go changes nothing: the pass plays on.
    run (tracker, stopped, once, block, keys ({ { 60, true, 0 } }));
    segments = run (tracker, stopped, once, block, keys ({ { 60, false, 100 } }));
    CHECK (segments.size() == 1 && segments[0].playing && segments[0].once);

    // SysEx is no trigger (nor read: it could not be without allocating).
    const juce::uint8 sysex[] { 0xf0, 0x7d, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0xf7 };
    juce::MidiBuffer withSysex = keys ({ { 61, false, 0 } });
    withSysex.addEvent (sysex, static_cast<int> (sizeof (sysex)), 30);
    segments = run (tracker, stopped, once, block, withSysex);
    CHECK (segments.size() == 1 && segments[0].playing && ! segments[0].jump);
}

ARTFREQ_TEST (trackerPreviewOnceEndsByItself)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    HostTiming stopped;
    stopped.bpm = 120.0;
    const auto once = settingsFor (TriggerMode::transport, 1.0, false);   // 24000 samples
    const auto fade = ArtEngine::gateFadeSeconds * sampleRate;

    // Without Repeat it plays one pass, then says so (the processor turns the preview off).
    tracker.setPreview (true);
    auto played = 0;
    auto endedIn = -1;

    for (int b = 0; b < 60; ++b)
    {
        const auto segments = run (tracker, stopped, once);
        CHECK (covers (segments, block));

        for (const auto& segment : segments)
        {
            CHECK (segment.once);
            played += segment.playing ? segment.length : 0;
        }

        if (tracker.hasPreviewEnded() && endedIn < 0)
            endedIn = b;
    }

    CHECK_NEAR (played, 24000.0 - fade - 1.0, 1.0);
    CHECK (endedIn == static_cast<int> ((24000.0 - fade - 1.0) / block));

    // It stays silent until turned off; turned on again, it plays from the start.
    CHECK (! run (tracker, stopped, once)[0].playing);
    CHECK (tracker.hasPreviewEnded());
    tracker.setPreview (false);
    CHECK (! tracker.hasPreviewEnded());
    tracker.setPreview (true);
    auto segments = run (tracker, stopped, once);
    CHECK (segments.size() == 1 && segments[0].playing && segments[0].jump);
    CHECK_NEAR (segments[0].fraction, 0.0, 1.0e-15);

    // With Repeat it goes round and never ends by itself.
    auto looping = true;

    for (int b = 0; b < 100; ++b)
    {
        segments = run (tracker, stopped, settingsFor (TriggerMode::transport, 1.0, true));
        looping = looping && segments.size() == 1 && segments[0].playing && ! segments[0].jump && ! segments[0].once;
    }

    CHECK (looping);
    CHECK (! tracker.hasPreviewEnded());

    // Notes played meanwhile are not played, but are counted: a key let go during the preview
    // does not keep a later note looping.
    tracker.setPreview (false);
    const auto midiRepeat = settingsFor (TriggerMode::midiNote, 1.0, true);
    juce::MidiBuffer down, up;
    down.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    up.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    run (tracker, stopped, midiRepeat, block, down);
    tracker.setPreview (true);
    run (tracker, stopped, midiRepeat, block, up);
    tracker.setPreview (false);
    CHECK (! run (tracker, stopped, midiRepeat)[0].playing);   // the note does not come back

    juce::MidiBuffer again, release;
    again.addEvent (juce::MidiMessage::noteOn (1, 62, 0.8f), 0);
    release.addEvent (juce::MidiMessage::noteOff (1, 62), 30);
    run (tracker, stopped, midiRepeat, block, again);
    segments = run (tracker, stopped, midiRepeat, block, release);
    CHECK (segments.size() == 2 && segments[0].length == 30 && ! segments[1].playing);
}

ARTFREQ_TEST (trackerLengthWithoutTimeSignature)
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

ARTFREQ_TEST (trackerMakeHostTiming)
{
    CHECK (! artfreq::makeHostTiming ({}).isPlaying);
    CHECK (! artfreq::makeHostTiming ({}).bpm.has_value());

    juce::AudioPlayHead::PositionInfo info;
    info.setIsPlaying (true);
    info.setBpm (128.0);
    info.setPpqPosition (3.5);
    info.setTimeInSeconds (1.640625);

    const auto timing = artfreq::makeHostTiming (info);
    CHECK (timing.isPlaying);
    CHECK (timing.bpm.has_value() && std::abs (*timing.bpm - 128.0) < 1.0e-12);
    CHECK (timing.ppqPosition.has_value() && std::abs (*timing.ppqPosition - 3.5) < 1.0e-12);
    CHECK (timing.timeInSeconds.has_value() && std::abs (*timing.timeInSeconds - 1.640625) < 1.0e-12);

    juce::AudioPlayHead::PositionInfo partial;
    partial.setBpm (std::numeric_limits<double>::quiet_NaN());
    const auto sparse = artfreq::makeHostTiming (partial);
    CHECK (! sparse.isPlaying);
    CHECK (! sparse.bpm.has_value());
    CHECK (! sparse.ppqPosition.has_value());
    CHECK (! sparse.timeInSeconds.has_value());
}
