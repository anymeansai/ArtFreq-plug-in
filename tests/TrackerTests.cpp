// PlayheadTracker: where in the drawing playback is, from the transport, MIDI notes, the free
// loop and the preview, and when that position jumps.

#include "TestFramework.h"
#include "TestPlans.h"

#include "dsp/PlayheadTracker.h"

#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

using freqart::ArtEngine;
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

/** A host playing through a tempo map: each block reports the tempo at its first sample and the
    ppq counted sample by sample (as hosts with tempo tracks do, without splitting blocks at
    tempo changes), from ppq 8. `relocateAt` moves the song on by a quarter note at that block.
    Checks every block follows the host's ppq and returns how many after the first are jumps. */
int jumpsThroughTempoMap (const std::function<double (int64_t sample)>& tempoAt, int numBlocks, int blockLength,
                          int relocateAt = -1)
{
    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    const auto settings = settingsFor (TriggerMode::transport);
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

    // Changing the length moves the position: a jump. Changing to the free loop continues.
    ppq += blockQuarterNotes (120.0);
    segments = run (tracker, playingAt (ppq), settingsFor (TriggerMode::transport, 8.0));
    CHECK (segments[0].jump);

    const auto loop = run (tracker, playingAt (ppq + blockQuarterNotes (120.0)), settingsFor (TriggerMode::freeLoop, 8.0));
    CHECK (! loop[0].jump);
    CHECK_NEAR (loop[0].fraction, segments[0].fraction + block * rateFor (120.0, 8.0), 1.0e-12);
}

FREQART_TEST (trackerTempoChangesAreNotJumps)
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
}

FREQART_TEST (trackerManualTempoChangesWhilePlaying)
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

FREQART_TEST (trackerStartsOnTheHostsBarLines)
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

FREQART_TEST (trackerMidiOneShotEndsWithTheDrawing)
{
    // A one-shot through the engine, 1 bar at 120 (96000 samples), with sound only at the very
    // start of the drawing: nothing of that start comes back at the end, neither while the last
    // column plays nor during the fade-out after it.
    const auto settings = settingsFor (TriggerMode::midiNote, 4.0);
    const auto passSamples = 96000;
    const auto atStart = test::makePlan ({ 5000.0f }, [] (int column, int) { return column < 20 ? 0.3f : 0.0f; });
    const auto everywhere = test::makeConstantPlan ({ 5000.0f }, 0.3f);

    const auto playOnce = [&] (const freqart::ArtPlan& plan)
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

FREQART_TEST (trackerMidiPassEndsOnAllOffAndStop)
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
