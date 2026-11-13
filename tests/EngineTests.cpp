// ArtEngine: the oscillator bank that plays a plan. Pitch, level, the column envelope, fades,
// crossfades between plans, the Nyquist guard, determinism and real-time speed.

#include "TestFramework.h"
#include "TestPlans.h"

#include "dsp/ArtEngine.h"

#include <chrono>
#include <cstdio>

using freqart::ArtEngine;
using freqart::ArtPlan;

namespace
{

constexpr double rate48 = 48000.0;

/** Position change per sample for a drawing `seconds` long. */
double perSampleFor (double seconds, double sampleRate = rate48) { return 1.0 / (seconds * sampleRate); }

/** The RMS of a window of `length` samples centred on `centre`. */
double rmsAround (const std::vector<float>& x, double centre, int length)
{
    const auto from = static_cast<size_t> (std::max (0.0, centre - length / 2.0));
    return test::rms (x, from, from + static_cast<size_t> (length));
}

std::vector<float> concat (std::vector<float> a, const std::vector<float>& b)
{
    a.insert (a.end(), b.begin(), b.end());
    return a;
}

/** A "text-like" plan: in every column a quarter of the lines are on, in runs of 8 lines that
    move with the columns, at varying levels. */
std::shared_ptr<ArtPlan> sparsePlan (int numLines, int numColumns = freqart::Canvas::width)
{
    return test::makePlan (test::logLines (numLines, 40.0, 16000.0), [] (int column, int line)
    {
        const auto on = ((line / 8) + (column / 32)) % 4 == 0;
        return on ? 0.01f + 0.002f * static_cast<float> ((line * 7 + column * 3) % 11) : 0.0f;
    }, numColumns);
}

} // namespace

FREQART_TEST (engineSingleLineFrequencyAndLevel)
{
    const auto plan = test::makeConstantPlan ({ 1000.0f }, 0.5f);
    ArtEngine engine;
    engine.prepare (rate48, 512);
    engine.setPlan (plan.get());

    const auto out = test::play (engine, 48000, 0.0, perSampleFor (8.0), 512, true);
    const auto spectrum = test::spectrum (out, 4800, 15, rate48);

    CHECK_NEAR (spectrum.peakHz(), 1000.0, 0.5);
    CHECK_NEAR (spectrum.levelAt (1000.0), 0.5, 0.01);

    // 600 whole periods after the fade-in: the RMS of a sine of peak 0.5.
    CHECK_NEAR (test::rms (out, 4800, 4800 + 600 * 48), 0.5 / std::sqrt (2.0), 0.0005);
    CHECK_NEAR (test::peak (out, 4800), 0.5, 0.001);
    CHECK (engine.isSounding());

    // Nothing else in the spectrum (harmonics, noise from the phasor).
    double strongestElsewhere = 0.0;

    for (size_t b = 0; b < spectrum.magnitudes.size(); ++b)
        if (std::abs (static_cast<double> (b) * spectrum.binHz - 1000.0) > 50.0)
            strongestElsewhere = std::max (strongestElsewhere, spectrum.magnitudes[b]);

    CHECK_LESS (test::toDb (strongestElsewhere / 0.5), -90.0);
}

FREQART_TEST (engineSilenceForNullAndSilentPlans)
{
    ArtEngine engine;
    engine.prepare (rate48, 512);

    engine.setPlan (nullptr);
    CHECK (test::peak (test::play (engine, 4800, 0.0, perSampleFor (2.0))) <= 0.0);

    const auto silent = test::makeConstantPlan ({ 200.0f, 1000.0f, 5000.0f }, 0.0f);
    CHECK (silent->silent);
    engine.setPlan (silent.get());
    CHECK (test::peak (test::play (engine, 4800, 0.0, perSampleFor (2.0))) <= 0.0);

    // A loud plan with the gate closed is silent too.
    const auto loud = test::makeConstantPlan ({ 1000.0f }, 0.5f);
    ArtEngine closed;
    closed.prepare (rate48, 512);
    closed.setPlan (loud.get());
    CHECK (test::peak (test::stop (closed, 4800)) <= 0.0);
    CHECK (! closed.isSounding());
}

FREQART_TEST (engineAmplitudeFollowsColumns)
{
    // 16 columns of 4800 samples. Columns 0 and 8..11 are loud, the rest quiet; amplitudes
    // move linearly from one column to the next, and from the last back to the first.
    constexpr int columnSamples = 4800;
    const auto loud = [] (int column) { return column == 0 || (column >= 8 && column <= 11); };
    const auto plan = test::makePlan ({ 1000.0f }, [&] (int column, int) { return loud (column) ? 0.8f : 0.1f; }, 16);

    ArtEngine engine;
    engine.prepare (rate48, 512);
    engine.setPlan (plan.get());
    const auto out = test::play (engine, 16 * columnSamples, 0.0, perSampleFor (16.0 * columnSamples / rate48), 512, true);

    const auto amplitudeAt = [&] (double columns) { return rmsAround (out, columns * columnSamples, 480) * std::sqrt (2.0); };

    CHECK_NEAR (amplitudeAt (0.5), 0.45, 0.01);    // from column 0 (0.8) to 1 (0.1)
    CHECK_NEAR (amplitudeAt (3.5), 0.1, 0.005);
    CHECK_NEAR (amplitudeAt (7.25), 0.275, 0.01);  // rising from column 7 to 8
    CHECK_NEAR (amplitudeAt (7.5), 0.45, 0.01);
    CHECK_NEAR (amplitudeAt (7.75), 0.625, 0.01);
    CHECK_NEAR (amplitudeAt (9.5), 0.8, 0.005);
    CHECK_NEAR (amplitudeAt (11.5), 0.45, 0.01);   // falling from column 11 to 12
    CHECK_NEAR (amplitudeAt (13.5), 0.1, 0.005);
    CHECK_NEAR (amplitudeAt (15.5), 0.45, 0.01);   // the wrap: column 15 (0.1) to column 0 (0.8)

    // Smooth: no steps beyond what the sine itself does.
    CHECK_LESS (test::largestStep (out), test::sineStepBound ({ 1000.0f }, { 0.8f }, rate48) * 1.02 + 1.0e-4);
}

FREQART_TEST (engineCrossfadeSameLayoutKeepsPhases)
{
    const std::vector<float> lines { 500.0f, 1500.0f, 4000.0f };
    const auto first = test::makeConstantPlan (lines, 0.2f);
    const auto second = test::makePlan (lines, [] (int, int line) { return line == 0 ? 0.4f : (line == 1 ? 0.1f : 0.3f); });
    CHECK (first->layoutHash == second->layoutHash);

    const auto perSample = perSampleFor (8.0);
    ArtEngine engine;
    engine.prepare (rate48, 512);
    engine.setPlan (first.get());
    auto out = test::play (engine, 4800, 0.0, perSample, 64, true);

    engine.setPlan (second.get());
    CHECK (engine.getPlan() == second.get());
    CHECK (engine.getFadingPlan() == first.get());

    const auto fade = test::play (engine, 960, 4800 * perSample, perSample, 64);
    CHECK (engine.getFadingPlan() == nullptr);
    out = concat (out, fade);
    out = concat (out, test::play (engine, 4800, 5760 * perSample, perSample, 64));

    CHECK_LESS (test::largestStep (out), test::sineStepBound (lines, { 0.4f, 0.2f, 0.3f }, rate48) * 1.02 + 1.0e-4);
    CHECK_NEAR (test::rms (out, 5760 + 480, 5760 + 480 + 96 * 40), std::sqrt ((0.16 + 0.01 + 0.09) / 2.0), 0.01);

    // The oscillators ran on through the crossfade: after it, the output is exactly what an
    // engine that played the second plan all along makes.
    ArtEngine reference;
    reference.prepare (rate48, 512);
    reference.setPlan (second.get());
    const auto expected = test::play (reference, 4800 + 960 + 4800, 0.0, perSample, 64, true);

    double largestDifference = 0.0;

    for (size_t i = 5760; i < out.size(); ++i)
        largestDifference = std::max (largestDifference, static_cast<double> (std::abs (out[i] - expected[i])));

    CHECK_LESS (largestDifference, 1.0e-4);
}

FREQART_TEST (engineCrossfadeDifferentLayout)
{
    const auto first = test::makeConstantPlan ({ 1000.0f }, 0.5f);
    const auto second = test::makeConstantPlan ({ 1500.0f, 3000.0f }, 0.3f);
    const auto third = test::makeConstantPlan ({ 700.0f }, 0.4f);
    CHECK (first->layoutHash != second->layoutHash);

    const auto perSample = perSampleFor (8.0);
    ArtEngine engine;
    engine.prepare (rate48, 512);
    engine.setPlan (first.get());
    auto out = test::play (engine, 4800, 0.0, perSample, 128, true);

    engine.setPlan (second.get());
    CHECK (engine.getFadingPlan() == first.get());
    out = concat (out, test::play (engine, 384, 4800 * perSample, perSample, 128));
    CHECK (engine.getFadingPlan() == first.get());

    // A third plan during the crossfade waits for it, then fades in.
    engine.setPlan (third.get());
    CHECK (engine.getQueuedPlan() == third.get());
    CHECK (engine.getPlan() == second.get());
    out = concat (out, test::play (engine, 640, 5184 * perSample, perSample, 128));
    CHECK (engine.getQueuedPlan() == nullptr);
    CHECK (engine.getPlan() == third.get());
    CHECK (engine.getFadingPlan() == second.get());

    out = concat (out, test::play (engine, 4800, 5824 * perSample, perSample, 128));
    CHECK (engine.getFadingPlan() == nullptr);

    CHECK_LESS (test::largestStep (out),
                test::sineStepBound ({ 1000.0f, 1500.0f, 3000.0f, 700.0f }, { 0.5f, 0.3f, 0.3f, 0.4f }, rate48) + 1.0e-4);

    // Only the third plan is left.
    const auto spectrum = test::spectrum (out, out.size() - 4096, 12, rate48);
    CHECK_NEAR (spectrum.levelAt (700.0), 0.4, 0.02);
    CHECK_LESS (spectrum.levelAt (1000.0), 1.0e-3);
    CHECK_LESS (spectrum.levelAt (3000.0), 1.0e-3);

    // Equal-power crossfade between unrelated lines: the level does not dip in the middle.
    const auto fadeMiddle = rmsAround (out, 4800 + 480, 192);
    CHECK_GREATER (fadeMiddle, 0.8 * 0.5 / std::sqrt (2.0));

    // A crossfade to no plan at all fades out.
    engine.setPlan (nullptr);
    const auto tail = test::play (engine, 4800, 0.5, perSample, 128);
    CHECK_LESS (test::largestStep (tail), test::sineStepBound ({ 700.0f }, { 0.4f }, rate48) + 1.0e-4);
    CHECK (test::peak (tail, 960) <= 0.0);
    CHECK (engine.getFadingPlan() == nullptr);
}

FREQART_TEST (engineJumpFadesWithoutClick)
{
    // Quiet first half, loud second half: a jump between them without a fade would step.
    const auto plan = test::makePlan ({ 500.0f }, [] (int column, int) { return column < 8 ? 0.1f : 0.9f; }, 16);
    const auto perSample = perSampleFor (16.0);

    ArtEngine engine;
    engine.prepare (rate48, 512);
    engine.setPlan (plan.get());
    auto out = test::play (engine, 4800, 0.1, perSample, 512, true);

    // Jump to 0.6 in the middle of a block.
    std::vector<float> block (512);
    const freqart::PlaySegment segments[] = {
        test::segment (0, 200, true, 0.1 + 4800 * perSample, perSample),
        test::segment (200, 312, true, 0.6, perSample, true)
    };
    engine.render (block.data(), 512, segments, 2);
    out = concat (out, block);
    out = concat (out, test::play (engine, 4800, 0.6 + 312 * perSample, perSample));

    // Bound: the sine's own slope at the loud level plus the jump fade's.
    const auto fadeSlope = 1.5 / (ArtEngine::jumpFadeSeconds * rate48) * 0.9;
    CHECK_LESS (test::largestStep (out), test::sineStepBound ({ 500.0f }, { 0.9f }, rate48) + fadeSlope + 1.0e-4);

    // It faded out at the old position (quiet), then came in at the new one (loud).
    const auto jumpAt = 4800.0 + 200.0;
    CHECK_LESS (rmsAround (out, jumpAt + 144, 24), 0.02);
    CHECK_NEAR (rmsAround (out, jumpAt + 1000, 960) * std::sqrt (2.0), 0.9, 0.02);

    // The new position was advanced by the fade time: it is exactly where the tracker said.
    CHECK_NEAR (engine.getFraction(), 0.6 + (312 + 4800) * perSample, 1.0e-9);
    CHECK (engine.isSounding());

    // A jump while nothing is heard just moves.
    ArtEngine quiet;
    quiet.prepare (rate48, 512);
    quiet.setPlan (plan.get());
    test::stop (quiet, 512, 0.2);
    const auto jumped = test::segment (0, 0, true, 0.7, perSample, true);
    std::vector<float> none (1);
    quiet.render (none.data(), 1, &jumped, 1);
    CHECK_NEAR (quiet.getFraction(), 0.7 + perSample, 1.0e-12);
}

FREQART_TEST (engineGateFades)
{
    const auto plan = test::makeConstantPlan ({ 1000.0f }, 0.5f);
    ArtEngine engine;
    engine.prepare (rate48, 512);
    engine.setPlan (plan.get());

    const auto fadeSamples = static_cast<int> (ArtEngine::gateFadeSeconds * rate48);   // 240
    const auto on = test::play (engine, 4800, 0.0, perSampleFor (8.0), 512, true);

    CHECK (std::abs (on[0]) <= 0.0f);

    auto withinEnvelope = true;

    for (int i = 0; i < fadeSamples; ++i)
    {
        const auto e = static_cast<float> (i) / static_cast<float> (fadeSamples);
        withinEnvelope = withinEnvelope && std::abs (on[static_cast<size_t> (i)]) <= 0.5f * e * e * (3.0f - 2.0f * e) + 1.0e-6f;
    }

    CHECK (withinEnvelope);
    CHECK_NEAR (test::peak (on, static_cast<size_t> (fadeSamples), 4800), 0.5, 0.001);

    // Gate off: gone within the fade time, and then exactly silent.
    const auto off = test::stop (engine, 4800, 0.1);
    CHECK_GREATER (test::peak (off, 0, 48), 0.1);
    CHECK (test::peak (off, static_cast<size_t> (fadeSamples) + 1) <= 0.0);
    CHECK (! engine.isSounding());

    const auto all = concat (on, off);
    CHECK_LESS (test::largestStep (all), test::sineStepBound ({ 1000.0f }, { 0.5f }, rate48) + 1.0e-4);

    // A gate change inside a block happens at the segment boundary.
    ArtEngine split;
    split.prepare (rate48, 512);
    split.setPlan (plan.get());
    test::play (split, 2048, 0.0, perSampleFor (8.0), 512, true);
    std::vector<float> block (1024);
    const freqart::PlaySegment segments[] = {
        test::segment (0, 300, true, 2048 * perSampleFor (8.0), perSampleFor (8.0)),
        test::segment (300, 724, false, 2348 * perSampleFor (8.0), 0.0)
    };
    split.render (block.data(), 1024, segments, 2);
    CHECK_NEAR (test::peak (block, 200, 300), 0.5, 0.01);
    CHECK (test::peak (block, static_cast<size_t> (300 + fadeSamples + 1)) <= 0.0);
}

FREQART_TEST (engineNyquistGuard)
{
    constexpr double rate44 = 44100.0;
    // 21 kHz is below the guard (0.48 x 44.1 kHz = 21168 Hz); 21.5 kHz is above it; 30 kHz is
    // above Nyquist and would alias to 14.1 kHz.
    const auto plan = test::makeConstantPlan ({ 1000.0f, 21000.0f, 21500.0f, 30000.0f }, 0.2f);

    ArtEngine engine;
    engine.prepare (rate44, 512);
    engine.setPlan (plan.get());
    const auto out = test::play (engine, 44100, 0.0, perSampleFor (8.0, rate44), 512, true);
    const auto spectrum = test::spectrum (out, 4410, 15, rate44);

    CHECK_NEAR (spectrum.levelAt (1000.0), 0.2, 0.005);
    CHECK_NEAR (spectrum.levelAt (21000.0), 0.2, 0.005);
    CHECK_LESS (spectrum.levelAt (21500.0), 1.0e-5);
    CHECK_LESS (spectrum.levelAt (14100.0), 1.0e-5);
}

FREQART_TEST (engineIsDeterministic)
{
    const auto plan = sparsePlan (256);
    const auto perSample = perSampleFor (2.0);

    ArtEngine a, b;
    a.prepare (rate48, 512);
    b.prepare (rate48, 512);
    a.setPlan (plan.get());
    b.setPlan (plan.get());

    const auto first = test::play (a, 48000, 0.0, perSample, 512, true);
    const auto second = test::play (b, 48000, 0.0, perSample, 512, true);
    CHECK (first == second);
    CHECK_GREATER (test::rms (first), 0.01);

    // reset() starts from the same phases again.
    a.reset();
    const auto again = test::play (a, 48000, 0.0, perSample, 512, true);
    CHECK (first == again);

    // The block size does not change the sound (only rounding). Every line is on all the time
    // here: a line that falls silent keeps its phase where it stopped, and when it comes back
    // in (from an amplitude of 0) depends on rounding at the column edges, which the block
    // size moves.
    const auto alwaysOn = test::makePlan (test::logLines (64, 40.0, 16000.0), [] (int column, int line)
    {
        return 0.005f + 0.001f * static_cast<float> ((line * 5 + column * 3) % 13);
    });

    ArtEngine big, odd;
    big.prepare (rate48, 512);
    odd.prepare (rate48, 61);
    big.setPlan (alwaysOn.get());
    odd.setPlan (alwaysOn.get());
    const auto bigBlocks = test::play (big, 48000, 0.0, perSample, 512, true);
    const auto oddBlocks = test::play (odd, 48000, 0.0, perSample, 61, true);
    double largestDifference = 0.0;

    for (size_t i = 0; i < bigBlocks.size(); ++i)
        largestDifference = std::max (largestDifference, static_cast<double> (std::abs (bigBlocks[i] - oddBlocks[i])));

    CHECK_LESS (largestDifference, 1.0e-5);
}

FREQART_TEST (enginePositionAndLongBlocks)
{
    const auto plan = test::makeConstantPlan ({ 440.0f }, 0.3f);
    const auto perSample = perSampleFor (1.0);

    ArtEngine engine;
    engine.prepare (rate48, 256);
    engine.setPlan (plan.get());

    // Blocks longer than prepared for are fine.
    const auto out = test::play (engine, 9000, 0.25, perSample, 3000, true);
    CHECK_NEAR (engine.getFraction(), 0.25 + 9000 * perSample, 1.0e-9);
    CHECK_NEAR (test::peak (out, 480), 0.3, 0.001);

    // The position wraps.
    test::play (engine, 48000, engine.getFraction(), perSample, 3000);
    CHECK_NEAR (engine.getFraction(), 0.25 + 9000 * perSample, 1.0e-9);
}

FREQART_TEST (engineTruncatesToMaxLines)
{
    // 1100 lines 15 Hz apart (30 Hz .. 16.5 kHz); only line 10 (180 Hz) and line 1050
    // (15780 Hz) are on. Line 1050 is past maxLines and must not play.
    std::vector<float> lines;

    for (int i = 0; i < 1100; ++i)
        lines.push_back (30.0f + 15.0f * static_cast<float> (i));

    const auto plan = test::makePlan (lines, [] (int, int line) { return line == 10 || line == 1050 ? 0.5f : 0.0f; });
    ArtEngine engine;
    engine.prepare (rate48, 512);
    engine.setPlan (plan.get());
    const auto out = test::play (engine, 24576, 0.0, perSampleFor (4.0), 512, true);
    const auto spectrum = test::spectrum (out, 4800, 14, rate48);

    CHECK_NEAR (spectrum.levelAt (180.0), 0.5, 0.01);
    CHECK_LESS (spectrum.levelAt (15780.0), 1.0e-5);
}

FREQART_TEST (enginePerformance)
{
    using clock = std::chrono::steady_clock;

    // Best of three, so other work on the machine does not decide the result.
    const auto speed = [] (const ArtPlan& plan, double seconds)
    {
        const auto numSamples = static_cast<int> (seconds * rate48);
        std::vector<float> block (512);
        double best = 0.0;

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            ArtEngine engine;
            engine.prepare (rate48, 512);
            engine.setPlan (&plan);
            const auto perSample = perSampleFor (8.0);
            const auto started = clock::now();

            for (int done = 0; done < numSamples; done += 512)
            {
                const auto s = test::segment (0, 512, true, done * perSample, perSample, done == 0);
                engine.render (block.data(), 512, &s, 1);
            }

            const auto elapsed = std::chrono::duration<double> (clock::now() - started).count();
            best = std::max (best, seconds / std::max (elapsed, 1.0e-9));
        }

        return best;
    };

    const auto dense = test::makePlan (test::logLines (1024, 20.0, 20000.0),
                                       [] (int column, int line) { return 0.001f + 0.0001f * static_cast<float> ((column + line) % 7); });
    const auto denseSpeed = speed (*dense, 2.0);

    const auto sparse = sparsePlan (256);
    const auto sparseSpeed = speed (*sparse, 8.0);

    const auto silent = test::makeConstantPlan (test::logLines (1024, 20.0, 20000.0), 0.0f);
    const auto silentSpeed = speed (*silent, 8.0);

    std::printf ("    1024 lines, all active:      %7.1f x real time (48 kHz mono)\n", denseSpeed);
    std::printf ("    256 lines, ~25 %% active:     %7.1f x real time\n", sparseSpeed);
    std::printf ("    1024 lines, silent plan:     %7.1f x real time\n", silentSpeed);

    CHECK_GREATER (denseSpeed, 5.0);
    CHECK_GREATER (sparseSpeed, 40.0);
}

FREQART_TEST (engineSurvivesRandomUse)
{
    // Random blocks, segments, gate changes, jumps and plan changes (same layout, another
    // layout, silent, none, several within one crossfade), as a host and a busy editor might
    // produce them. The output must stay finite, within the plans' total amplitude, and free
    // of clicks: no step beyond what the sines and the fades themselves can make.
    const std::vector<float> layoutA { 300.0f, 900.0f, 2500.0f };
    const std::vector<float> layoutB { 450.0f, 3100.0f };
    const std::vector<std::shared_ptr<ArtPlan>> plans {
        test::makePlan (layoutA, [] (int column, int line) { return 0.05f + 0.1f * static_cast<float> ((column / 64 + line) % 3); }),
        test::makePlan (layoutA, [] (int column, int line) { return column % 200 < 100 ? 0.2f : (line == 1 ? 0.25f : 0.0f); }),
        test::makePlan (layoutB, [] (int column, int) { return column < 700 ? 0.3f : 0.0f; }),
        test::makeConstantPlan (layoutB, 0.0f)
    };

    const auto amplitudeBound = 3 * 0.25 + 2 * 0.3;
    const auto stepBound = amplitudeBound * (2.0 * std::sin (0.5 * test::twoPi * 3100.0 / rate48)
                                             + 1.5 / (ArtEngine::jumpFadeSeconds * rate48)
                                             + 1.6 / (ArtEngine::planFadeSeconds * rate48)) * 1.2;

    juce::Random random (1234);
    ArtEngine engine;
    engine.prepare (rate48, 700);

    std::vector<float> out, block (700);
    double position = 0.0;
    double perSample = perSampleFor (2.0);
    auto playing = false;

    for (int b = 0; b < 3000; ++b)
    {
        if (random.nextInt (8) == 0)
        {
            const auto choice = random.nextInt (static_cast<int> (plans.size()) + 1);
            engine.setPlan (choice < static_cast<int> (plans.size()) ? plans[static_cast<size_t> (choice)].get() : nullptr);
        }

        const auto length = 1 + random.nextInt (700);
        freqart::PlaySegment segments[3];
        const auto numSegments = 1 + random.nextInt (std::min (3, length));
        auto start = 0;

        for (int s = 0; s < numSegments; ++s)
        {
            const auto end = s == numSegments - 1 ? length : start + 1 + random.nextInt (length - start - (numSegments - s - 1));
            auto jump = random.nextInt (20) == 0;

            if (random.nextInt (10) == 0)
                playing = ! playing;

            if (random.nextInt (30) == 0)
                perSample = perSampleFor (0.1 + 4.0 * random.nextDouble());

            if (jump)
                position = random.nextDouble();

            segments[s] = test::segment (start, end - start, playing, position, playing ? perSample : 0.0, jump);

            if (playing)
                position = std::fmod (position + (end - start) * perSample, 1.0);

            start = end;
        }

        engine.render (block.data(), length, segments, numSegments);
        out.insert (out.end(), block.begin(), block.begin() + length);
    }

    auto finite = true;

    for (auto x : out)
        finite = finite && std::isfinite (x);

    CHECK (finite);
    CHECK_LESS (test::peak (out), amplitudeBound);
    CHECK_LESS (test::largestStep (out), stepBound);
    CHECK_GREATER (test::rms (out), 0.01);
}
