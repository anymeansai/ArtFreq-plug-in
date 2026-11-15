// Spectral carving: the mask plan built from the canvas (and widened by a halo), its exchange,
// the baseline engine (transparency, carving and stencilling a band, the latency, the gate,
// block sizes) and the tuned engine (engine 1: the same, plus thin low strokes, the gate at the
// output, reading the drawing over each hop and at column edges alike, and the halo per mode).

#include "TestFramework.h"

#include "core/MaskPlan.h"
#include "dsp/PlanExchange.h"
#include "dsp/SpectralCarver.h"
#include "dsp/TunedCarver.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace artfreq;

namespace
{

constexpr double rate = 48000.0;

/** A canvas with `value` in every row whose centre lies between lowHz and highHz, over columns
    [fromColumn, toColumn). */
Canvas bandCanvas (double lowHz, double highHz, int fromColumn = 0, int toColumn = Canvas::width, float value = 1.0f,
                   const FrequencyScale& scale = {})
{
    Canvas canvas;
    const auto low = scale.toNorm (lowHz), high = scale.toNorm (highHz);

    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto norm = 1.0 - (y + 0.5) / Canvas::height;

        if (norm >= low && norm <= high)
            for (int x = fromColumn; x < toColumn; ++x)
                canvas.at (x, y) = value;
    }

    return canvas;
}

std::unique_ptr<MaskPlan> bandPlan (double lowHz, double highHz, int fromColumn = 0, int toColumn = Canvas::width)
{
    MaskSettings settings;
    return buildMaskPlan (bandCanvas (lowHz, highHz, fromColumn, toColumn), settings);
}

using Stereo = std::vector<std::vector<float>>;

/** Sines faded in over 10 ms (a sudden start would be a click with energy everywhere). */
Stereo sines (int numSamples, std::initializer_list<std::pair<double, float>> partials)
{
    Stereo audio (2, std::vector<float> (static_cast<size_t> (numSamples), 0.0f));
    constexpr int fadeIn = 480;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto fade = i < fadeIn ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / fadeIn) : 1.0;

        for (const auto& [hz, amplitude] : partials)
        {
            const auto phase = juce::MathConstants<double>::twoPi * hz * i / rate;
            audio[0][static_cast<size_t> (i)] += amplitude * static_cast<float> (fade * std::sin (phase));
            audio[1][static_cast<size_t> (i)] += amplitude * static_cast<float> (fade * std::sin (phase + 0.3));
        }
    }

    return audio;
}

Stereo noise (int numSamples, int seed = 7)
{
    juce::Random random (seed);
    Stereo audio (2, std::vector<float> (static_cast<size_t> (numSamples)));

    for (auto& channel : audio)
        for (auto& x : channel)
            x = random.nextFloat() - 0.5f;

    return audio;
}

/** Plays `audio` through the carver in blocks (sizes taken in turn from `blocks`), the drawing
    playing on a loop of `passSamples` from the first sample, or with the gate shut before
    `gateFrom`; each block's position off by `positionError` (a host's rounding). */
Stereo run (SpectralCarver& carver, const Stereo& audio, const std::vector<int>& blocks, const CarveSettings& settings,
            double passSamples = 8.0 * rate, int gateFrom = 0, double positionError = 0.0)
{
    auto out = audio;
    const auto total = static_cast<int> (audio[0].size());
    size_t next = 0;

    for (int start = 0; start < total;)
    {
        const auto length = std::min (blocks[next++ % blocks.size()], total - start);
        PlaySegment segments[2];
        auto count = 0;

        const auto addSegment = [&] (int from, int to)
        {
            if (to <= from)
                return;

            auto& segment = segments[count++];
            segment.start = from - start;
            segment.length = to - from;
            segment.playing = from >= gateFrom;
            segment.fraction = std::fmod (from / passSamples, 1.0) + positionError;
            segment.fractionPerSample = 1.0 / passSamples;
        };

        const auto split = juce::jlimit (start, start + length, gateFrom);
        addSegment (start, split);
        addSegment (split, start + length);

        float* channels[2] = { out[0].data() + start, out[1].data() + start };
        carver.process (channels, 2, length, segments, count, settings);
        start += length;
    }

    return out;
}

/** The same with the gate open only over [gateFrom, gateTo): the pass starts at gateFrom (its
    first sample is the drawing's start) and is cut off at gateTo. */
Stereo runPass (SpectralCarver& carver, const Stereo& audio, const std::vector<int>& blocks, const CarveSettings& settings,
                int gateFrom, int gateTo, double passSamples = 8.0 * rate)
{
    auto out = audio;
    const auto total = static_cast<int> (audio[0].size());
    size_t next = 0;

    for (int start = 0; start < total;)
    {
        const auto length = std::min (blocks[next++ % blocks.size()], total - start);
        PlaySegment segments[3];
        auto count = 0;

        const auto addSegment = [&] (int from, int to)
        {
            if (to <= from)
                return;

            auto& segment = segments[count++];
            segment.start = from - start;
            segment.length = to - from;
            segment.playing = from >= gateFrom && from < gateTo;
            segment.once = true;
            segment.fraction = std::clamp ((from - gateFrom) / passSamples, 0.0, 1.0);
            segment.fractionPerSample = 1.0 / passSamples;
        };

        const auto first = juce::jlimit (start, start + length, gateFrom);
        const auto second = juce::jlimit (first, start + length, gateTo);
        addSegment (start, first);
        addSegment (first, second);
        addSegment (second, start + length);

        float* channels[2] = { out[0].data() + start, out[1].data() + start };
        carver.process (channels, 2, length, segments, count, settings);
        start += length;
    }

    return out;
}

/** The largest difference between the output and the input delayed by `latency`. */
float differenceFromDelayed (const Stereo& in, const Stereo& out, int latency, int from = 0)
{
    auto largest = 0.0f;

    for (size_t channel = 0; channel < in.size(); ++channel)
        for (auto i = static_cast<size_t> (std::max (from, latency)); i < out[channel].size(); ++i)
            largest = std::max (largest, std::abs (out[channel][i] - in[channel][i - static_cast<size_t> (latency)]));

    return largest;
}

/** The largest difference between two outputs, over both channels. */
float largestDifference (const Stereo& a, const Stereo& b)
{
    auto largest = 0.0f;

    for (size_t channel = 0; channel < a.size(); ++channel)
        for (size_t i = 0; i < a[channel].size(); ++i)
            largest = std::max (largest, std::abs (a[channel][i] - b[channel][i]));

    return largest;
}

/** Level of a sine of `hz` in channel 0 over [from, to), in dB (a full-scale sine is 0 dB). */
double levelDb (const Stereo& audio, double hz, int from, int to)
{
    double re = 0.0, im = 0.0, windowSum = 0.0;

    for (int i = from; i < to; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - from) / (to - from));
        const auto phase = juce::MathConstants<double>::twoPi * hz * i / rate;
        re += w * audio[0][static_cast<size_t> (i)] * std::cos (phase);
        im += w * audio[0][static_cast<size_t> (i)] * std::sin (phase);
        windowSum += w;
    }

    return 20.0 * std::log10 (std::max (1e-12, 2.0 * std::sqrt (re * re + im * im) / windowSum));
}

std::unique_ptr<SpectralCarver> preparedBaseline (double sampleRate = rate, int maxBlock = 512)
{
    auto carver = createSpectralCarver (0);
    carver->prepare (sampleRate, maxBlock, 2);
    return carver;
}

std::unique_ptr<SpectralCarver> preparedTuned (double sampleRate = rate, int maxBlock = 512)
{
    auto carver = createSpectralCarver (1);
    carver->prepare (sampleRate, maxBlock, 2);
    return carver;
}

} // namespace

ARTFREQ_TEST (maskPlanFollowsTheCanvas)
{
    const auto plan = bandPlan (900.0, 1100.0, 100, 200);

    CHECK (! plan->empty);
    CHECK_NEAR (plan->numColumns, Canvas::width, 0);
    CHECK_NEAR (plan->numRows, Canvas::height, 0);
    CHECK (plan->isColumnUsed (150));
    CHECK (! plan->isColumnUsed (50));
    CHECK_NEAR (plan->valueAt (150, 1000.0), 1.0, 1e-6);
    CHECK_NEAR (plan->valueAt (150, 2000.0), 0.0, 1e-6);
    CHECK_NEAR (plan->valueAt (150, 500.0), 0.0, 1e-6);
    CHECK_NEAR (plan->valueAt (50, 1000.0), 0.0, 1e-6);

    // Outside the scale's range nothing is carved.
    CHECK_NEAR (plan->valueAt (150, 10.0), 0.0, 1e-6);
    CHECK_NEAR (plan->valueAt (150, 22000.0), 0.0, 1e-6);

    // Positions map to columns as the canvas's pixels do.
    CHECK_NEAR (plan->columnAt (0.0), 0, 0);
    CHECK_NEAR (plan->columnAt (150.5 / Canvas::width), 150, 0);
    CHECK_NEAR (plan->columnAt (1.0), Canvas::width - 1, 0);
    CHECK_NEAR (plan->columnAt (-0.2), 0, 0);

    // A span inside the band averages to 1, one half inside it (on the scale) to about half;
    // the maximum finds the band from a span that only reaches into it.
    CHECK_GREATER (plan->averageOver (150, 950.0, 1050.0), 0.99);
    CHECK_NEAR (plan->averageOver (150, 900.0 * 900.0 / 1100.0, 1100.0), 0.5, 0.03);
    CHECK_NEAR (plan->maxOver (150, 1000.0 / std::sqrt (2.0), 1000.0), 1.0, 1e-6);
    CHECK_NEAR (plan->maxOver (150, 2000.0, 4000.0), 0.0, 1e-6);

    // Values above 1 are clipped, faint ones dropped like the sine plan's silence.
    MaskSettings settings;
    auto canvas = bandCanvas (900.0, 1100.0, 0, Canvas::width, 1.5f);
    canvas.at (10, 10) = 0.0005f;
    const auto clipped = buildMaskPlan (canvas, settings);
    CHECK_NEAR (clipped->valueAt (0, 1000.0), 1.0, 1e-6);
    CHECK_NEAR (clipped->column (10)[Canvas::height - 1 - 10], 0.0, 0.0);

    // An empty canvas is an empty plan.
    CHECK (buildMaskPlan (Canvas(), settings)->empty);
}

ARTFREQ_TEST (maskPlanHaloWidensShapes)
{
    MaskSettings settings;
    settings.haloRows = 3;
    settings.haloColumns = 2;

    Canvas canvas;
    canvas.at (500, 500) = 0.7f;
    const auto plan = buildMaskPlan (canvas, settings);
    const auto row = Canvas::height - 1 - 500;

    CHECK_NEAR (plan->column (500)[row + 3], 0.7, 1e-6);
    CHECK_NEAR (plan->column (500)[row - 3], 0.7, 1e-6);
    CHECK_NEAR (plan->column (500)[row + 4], 0.0, 0.0);
    CHECK_NEAR (plan->column (502)[row + 3], 0.7, 1e-6);
    CHECK_NEAR (plan->column (503)[row], 0.0, 0.0);
    CHECK_NEAR (plan->column (498)[row], 0.7, 1e-6);
    CHECK (plan->isColumnUsed (498) && ! plan->isColumnUsed (497));
}

ARTFREQ_TEST (maskPlansTravelThroughTheExchange)
{
    MaskPlanExchange exchange;
    std::shared_ptr<MaskPlan> first (bandPlan (900.0, 1100.0).release());
    std::shared_ptr<MaskPlan> second (bandPlan (2000.0, 3000.0).release());
    std::weak_ptr<MaskPlan> firstAlive = first;

    exchange.publish (first);
    const auto* acquired = exchange.acquire();
    CHECK (acquired == first.get());
    exchange.reportInUse (acquired->version, 0);

    exchange.publish (second);
    first.reset();
    exchange.collectGarbage();
    CHECK (! firstAlive.expired());  // still in use on the audio thread

    const auto* newer = exchange.acquire();
    CHECK (newer == second.get());
    CHECK_GREATER (newer->version, acquired->version);
    exchange.reportInUse (newer->version, 0);
    exchange.collectGarbage();
    CHECK (firstAlive.expired());
    CHECK_NEAR (exchange.numAlive(), 1, 0);
}

ARTFREQ_TEST (carverIsTransparentWithNothingToCarve)
{
    const auto input = noise (48000);

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        CarveSettings settings;
        settings.mode = mode;

        // No plan, and an empty one: the delayed input, bit for bit.
        auto carver = preparedBaseline();
        const auto latency = carver->getLatencySamples();
        CHECK_NEAR (differenceFromDelayed (input, run (*carver, input, { 512 }, settings), latency), 0.0, 0.0);

        const auto empty = buildMaskPlan (Canvas(), MaskSettings());
        carver = preparedBaseline();
        carver->setPlan (empty.get());
        CHECK_NEAR (differenceFromDelayed (input, run (*carver, input, { 512 }, settings), latency), 0.0, 0.0);

        // A drawing, but the gate shut (outside the pass): also untouched.
        const auto plan = bandPlan (900.0, 1100.0);
        carver = preparedBaseline();
        carver->setPlan (plan.get());
        CHECK_NEAR (differenceFromDelayed (input, run (*carver, input, { 512 }, settings, 8.0 * rate, 1 << 30), latency), 0.0, 0.0);
    }

    // Through the transforms: a band drawn far from the sines carves nothing audible. Mask 0
    // gives the input back to float precision.
    const auto sinesIn = sines (48000, { { 200.0, 0.3f }, { 1000.0, 0.3f }, { 5000.0, 0.3f } });
    const auto high = bandPlan (18000.0, 20000.0);
    auto carver = preparedBaseline();
    carver->setPlan (high.get());
    const auto out = run (*carver, sinesIn, { 512 }, CarveSettings());
    CHECK_LESS (differenceFromDelayed (sinesIn, out, carver->getLatencySamples()), 1.0e-5);
}

ARTFREQ_TEST (carverCarvesASineInsideTheBand)
{
    const auto input = sines (96000, { { 1000.0, 0.5f }, { 4000.0, 0.5f } });
    const auto plan = bandPlan (800.0, 1250.0);

    for (const auto depth : { -60.0f, -30.0f })
    {
        auto carver = preparedBaseline();
        carver->setPlan (plan.get());
        CarveSettings settings;
        settings.depthDb = depth;
        const auto out = run (*carver, input, { 512 }, settings);

        const auto from = carver->getLatencySamples() + 8192, to = 96000;
        const auto inside = levelDb (out, 1000.0, from, to) - levelDb (input, 1000.0, from, to);
        const auto outside = levelDb (out, 4000.0, from, to) - levelDb (input, 4000.0, from, to);

        CHECK_LESS (inside, depth + 3.0f);
        CHECK_GREATER (inside, depth - 3.0f);
        CHECK_NEAR (outside, 0.0, 0.1);
    }
}

ARTFREQ_TEST (carverStencilKeepsOnlyTheDrawing)
{
    const auto input = sines (96000, { { 1000.0, 0.5f }, { 4000.0, 0.5f } });
    const auto plan = bandPlan (800.0, 1250.0);
    auto carver = preparedBaseline();
    carver->setPlan (plan.get());

    CarveSettings settings;
    settings.mode = CarveMode::stencil;
    const auto out = run (*carver, input, { 512 }, settings);

    const auto from = carver->getLatencySamples() + 8192, to = 96000;
    CHECK_NEAR (levelDb (out, 1000.0, from, to) - levelDb (input, 1000.0, from, to), 0.0, 0.1);
    CHECK_LESS (levelDb (out, 4000.0, from, to) - levelDb (input, 4000.0, from, to), -57.0);
}

ARTFREQ_TEST (carverReportsItsLatency)
{
    const auto plan = bandPlan (18000.0, 20000.0);  // makes every frame go through the transforms

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        auto carver = preparedBaseline();
        carver->setPlan (plan.get());
        const auto latency = carver->getLatencySamples();
        CHECK_NEAR (latency, 2048, 0);

        Stereo impulse (2, std::vector<float> (16384, 0.0f));
        impulse[0][3000] = impulse[1][3000] = 1.0f;

        const auto peakAt = [] (const Stereo& audio)
        {
            const auto peak = std::max_element (audio[0].begin(), audio[0].end(),
                                                [] (float a, float b) { return std::abs (a) < std::abs (b); });
            return static_cast<int> (peak - audio[0].begin());
        };

        CarveSettings settings;
        settings.mode = mode;
        CHECK_NEAR (peakAt (run (*carver, impulse, { 512 }, settings)), 3000 + latency, 0);

        // The same with another depth, and with no plan (no transforms at all) after a reset.
        settings.depthDb = -12.0f;
        carver->reset();
        CHECK_NEAR (peakAt (run (*carver, impulse, { 512 }, settings)), 3000 + latency, 0);
        carver->setPlan (nullptr);
        carver->reset();
        CHECK_NEAR (carver->getLatencySamples(), latency, 0);
        CHECK_NEAR (peakAt (run (*carver, impulse, { 512 }, settings)), 3000 + latency, 0);
    }

    // About 43 ms at any rate.
    auto fast = preparedBaseline (96000.0);
    CHECK_NEAR (fast->getLatencySamples(), 4096, 0);
}

ARTFREQ_TEST (carverIsBlockSizeIndependent)
{
    const auto input = noise (3 * 48000, 11);
    const auto plan = bandPlan (300.0, 3000.0, 200, 700);

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        CarveSettings settings;
        settings.mode = mode;

        auto steady = preparedBaseline();
        steady->setPlan (plan.get());
        const auto reference = run (*steady, input, { 512 }, settings, 2.0 * rate, 20000);

        // Random sizes, also longer than the prepared block (worked through in pieces).
        juce::Random random (3);
        std::vector<int> sizes;
        for (int i = 0; i < 400; ++i)
            sizes.push_back (1 + random.nextInt (2048));

        auto varied = preparedBaseline();
        varied->setPlan (plan.get());
        const auto other = run (*varied, input, sizes, settings, 2.0 * rate, 20000);

        auto largest = 0.0f;
        for (size_t channel = 0; channel < 2; ++channel)
            for (size_t i = 0; i < input[0].size(); ++i)
                largest = std::max (largest, std::abs (reference[channel][i] - other[channel][i]));

        CHECK_LESS (largest, 1.0e-6);

        // ... and it did carve something.
        CHECK_GREATER (differenceFromDelayed (input, reference, steady->getLatencySamples()), 0.01);
    }
}

ARTFREQ_TEST (carverGateOpensWithThePass)
{
    // The gate opens at 1 s: before it (and the frames reaching into it) the audio is untouched;
    // a second into the pass a sine in the band is carved.
    const auto input = sines (96000, { { 1000.0, 0.5f } });
    const auto plan = bandPlan (800.0, 1250.0);
    auto carver = preparedBaseline();
    carver->setPlan (plan.get());

    const auto out = run (*carver, input, { 256 }, CarveSettings(), 8.0 * rate, 48000);
    const auto latency = carver->getLatencySamples();

    Stereo early (2), late (2);
    for (size_t channel = 0; channel < 2; ++channel)
    {
        early[channel].assign (out[channel].begin(), out[channel].begin() + 48000 - 2048 + latency);
        late[channel].assign (input[channel].begin(), input[channel].begin() + 48000 - 2048);
    }

    CHECK_NEAR (differenceFromDelayed (late, early, latency), 0.0, 0.0);
    CHECK_LESS (levelDb (out, 1000.0, 48000 + latency + 4096, 96000) - levelDb (input, 1000.0, 48000 + latency + 4096, 96000), -57.0);
}

// ---- engine 1: the tuned STFT ------------------------------------------------------------------

ARTFREQ_TEST (tunedCarverIsTransparentWithNothingToCarve)
{
    const auto input = noise (48000);

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        CarveSettings settings;
        settings.mode = mode;

        // No plan, an empty one, and a drawing with the gate shut: the delayed input, bit for bit.
        auto carver = preparedTuned();
        const auto latency = carver->getLatencySamples();
        CHECK_NEAR (differenceFromDelayed (input, run (*carver, input, { 512 }, settings), latency), 0.0, 0.0);

        const auto empty = buildMaskPlan (Canvas(), MaskSettings());
        carver = preparedTuned();
        carver->setPlan (empty.get());
        CHECK_NEAR (differenceFromDelayed (input, run (*carver, input, { 512 }, settings), latency), 0.0, 0.0);

        const auto plan = bandPlan (900.0, 1100.0);
        carver = preparedTuned();
        carver->setPlan (plan.get());
        CHECK_NEAR (differenceFromDelayed (input, run (*carver, input, { 512 }, settings, 8.0 * rate, 1 << 30), latency), 0.0, 0.0);
    }

    // Through the transforms, at the rates where the frame is a power of 2 and where it is
    // zero-padded (44.1 kHz), and for a window pair whose products do not add up to a constant:
    // where the mask is 0 the input comes back to float precision.
    const auto sinesIn = sines (48000, { { 200.0, 0.3f }, { 1000.0, 0.3f }, { 5000.0, 0.3f } });
    const auto high = bandPlan (18000.0, 20000.0);

    for (const auto sampleRate : { 48000.0, 44100.0 })
    {
        auto carver = preparedTuned (sampleRate);
        carver->setPlan (high.get());
        const auto out = run (*carver, sinesIn, { 512 }, CarveSettings());
        CHECK_LESS (differenceFromDelayed (sinesIn, out, carver->getLatencySamples()), 1.0e-5);
    }

    TunedCarver::Options rippling;
    rippling.windowPower = 2.5f;
    TunedCarver odd (rippling);
    odd.prepare (rate, 512, 2);
    odd.setPlan (high.get());
    CHECK_LESS (differenceFromDelayed (sinesIn, run (odd, sinesIn, { 512 }, CarveSettings()), odd.getLatencySamples()), 1.0e-5);
}

ARTFREQ_TEST (tunedCarverCarvesASineInsideTheBand)
{
    const auto input = sines (96000, { { 1000.0, 0.5f }, { 4000.0, 0.5f } });
    const auto plan = bandPlan (800.0, 1250.0);

    for (const auto depth : { -60.0f, -30.0f })
    {
        auto carver = preparedTuned();
        carver->setPlan (plan.get());
        CarveSettings settings;
        settings.depthDb = depth;
        const auto out = run (*carver, input, { 512 }, settings);

        const auto from = carver->getLatencySamples() + 8192, to = 96000;
        const auto inside = levelDb (out, 1000.0, from, to) - levelDb (input, 1000.0, from, to);
        const auto outside = levelDb (out, 4000.0, from, to) - levelDb (input, 4000.0, from, to);

        CHECK_LESS (inside, depth + 3.0f);
        CHECK_GREATER (inside, depth - 3.0f);
        CHECK_NEAR (outside, 0.0, 0.1);
    }
}

ARTFREQ_TEST (tunedCarverCarvesTheLows)
{
    // What the longer frame is for: a sine at 70 Hz in a band of 60 - 85 Hz (half an octave,
    // about one of the baseline's bins) drops by nearly all of Depth (56 dB of 60; the baseline
    // manages 14), and one at 160 Hz, an octave above the band, keeps its level.
    const auto input = sines (4 * 48000, { { 70.0, 0.5f }, { 160.0, 0.5f } });
    const auto band = bandPlan (60.0, 85.0);
    const auto from = 4096 + 16384, to = 4 * 48000;

    auto tuned = preparedTuned();
    tuned->setPlan (band.get());
    const auto out = run (*tuned, input, { 512 }, CarveSettings());
    const auto carved = levelDb (out, 70.0, from, to) - levelDb (input, 70.0, from, to);
    CHECK_LESS (carved, -50.0);
    CHECK_NEAR (levelDb (out, 160.0, from, to) - levelDb (input, 160.0, from, to), 0.0, 0.2);

    // A stroke thinner than a bin (64 - 68 Hz) is carved by its share of the bins around it
    // (17 dB) rather than missed between their centres (the baseline leaves it untouched).
    const auto thin = bandPlan (64.0, 68.0);
    const auto thinInput = sines (4 * 48000, { { 66.0, 0.5f } });
    tuned = preparedTuned();
    tuned->setPlan (thin.get());
    const auto thinOut = run (*tuned, thinInput, { 512 }, CarveSettings());
    CHECK_LESS (levelDb (thinOut, 66.0, from, to) - levelDb (thinInput, 66.0, from, to), -12.0);
}

ARTFREQ_TEST (tunedCarverStencilKeepsOnlyTheDrawing)
{
    const auto input = sines (96000, { { 1000.0, 0.5f }, { 4000.0, 0.5f } });
    const auto plan = bandPlan (800.0, 1250.0);
    auto carver = preparedTuned();
    carver->setPlan (plan.get());

    CarveSettings settings;
    settings.mode = CarveMode::stencil;
    const auto out = run (*carver, input, { 512 }, settings);

    const auto from = carver->getLatencySamples() + 8192, to = 96000;
    CHECK_NEAR (levelDb (out, 1000.0, from, to) - levelDb (input, 1000.0, from, to), 0.0, 0.1);
    CHECK_LESS (levelDb (out, 4000.0, from, to) - levelDb (input, 4000.0, from, to), -57.0);
}

ARTFREQ_TEST (tunedCarverReportsItsLatency)
{
    const auto plan = bandPlan (18000.0, 20000.0);  // makes every frame go through the transforms

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        auto carver = preparedTuned();
        carver->setPlan (plan.get());
        const auto latency = carver->getLatencySamples();
        CHECK_NEAR (latency, 4096, 0);

        Stereo impulse (2, std::vector<float> (24000, 0.0f));
        impulse[0][7000] = impulse[1][7000] = 1.0f;

        const auto peakAt = [] (const Stereo& audio)
        {
            const auto peak = std::max_element (audio[0].begin(), audio[0].end(),
                                                [] (float a, float b) { return std::abs (a) < std::abs (b); });
            return static_cast<int> (peak - audio[0].begin());
        };

        CarveSettings settings;
        settings.mode = mode;
        CHECK_NEAR (peakAt (run (*carver, impulse, { 512 }, settings)), 7000 + latency, 0);

        // The same with another depth, and with no plan (no transforms at all) after a reset.
        settings.depthDb = -12.0f;
        carver->reset();
        CHECK_NEAR (peakAt (run (*carver, impulse, { 512 }, settings)), 7000 + latency, 0);
        carver->setPlan (nullptr);
        carver->reset();
        CHECK_NEAR (carver->getLatencySamples(), latency, 0);
        CHECK_NEAR (peakAt (run (*carver, impulse, { 512 }, settings)), 7000 + latency, 0);
    }

    // About 85 ms at any rate: a whole number of hops, the FFT the next power of 2.
    TunedCarver at44;
    at44.prepare (44100.0, 512, 2);
    CHECK_NEAR (at44.getLatencySamples(), 3764, 0);
    CHECK_NEAR (at44.getHopSize(), 941, 0);
    CHECK_NEAR (at44.getFftSize(), 4096, 0);

    auto fast = preparedTuned (96000.0);
    CHECK_NEAR (fast->getLatencySamples(), 8192, 0);
}

ARTFREQ_TEST (tunedCarverIsBlockSizeIndependent)
{
    const auto input = noise (3 * 48000, 11);
    const auto plan = bandPlan (300.0, 3000.0, 200, 700);

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        CarveSettings settings;
        settings.mode = mode;

        auto steady = preparedTuned();
        steady->setPlan (plan.get());
        const auto reference = runPass (*steady, input, { 512 }, settings, 20000, 120000, 2.0 * rate);

        // Random sizes, also longer than the prepared block (worked through in pieces).
        juce::Random random (3);
        std::vector<int> sizes;
        for (int i = 0; i < 400; ++i)
            sizes.push_back (1 + random.nextInt (2048));

        auto varied = preparedTuned();
        varied->setPlan (plan.get());
        const auto other = runPass (*varied, input, sizes, settings, 20000, 120000, 2.0 * rate);

        auto largest = 0.0f;
        for (size_t channel = 0; channel < 2; ++channel)
            for (size_t i = 0; i < input[0].size(); ++i)
                largest = std::max (largest, std::abs (reference[channel][i] - other[channel][i]));

        CHECK_NEAR (largest, 0.0, 0.0);

        // ... and it did carve something.
        CHECK_GREATER (differenceFromDelayed (input, reference, steady->getLatencySamples()), 0.01);
    }
}

ARTFREQ_TEST (tunedCarverGateWorksAtTheOutput)
{
    // A pass from 1 s to 2 s over a sine in the drawn band. The gate is applied per sample at the
    // output: before the pass and once the gate has faded out after it the output is the delayed
    // input bit for bit (in Stencil too, which no longer mutes a frame's length either side), and
    // from the end of the gate's 5 ms fade on the band is carved fully, right from the start of
    // the pass and up to its end.
    const auto input = sines (3 * 48000, { { 1000.0, 0.5f }, { 4000.0, 0.5f } });
    const auto plan = bandPlan (800.0, 1250.0);
    // The pass starts just after a hop starts (hops are 1024 samples at 48 kHz), the hardest
    // place for frames that look at the timeline once per hop.
    const auto passFrom = 47 * 1024 + 1, passTo = 96000;
    const auto fade = static_cast<int> (SpectralCarver::gateFadeSeconds * rate) + 2;

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        auto carver = preparedTuned();
        carver->setPlan (plan.get());
        CarveSettings settings;
        settings.mode = mode;

        const auto out = runPass (*carver, input, { 256 }, settings, passFrom, passTo);
        const auto latency = carver->getLatencySamples();

        auto before = 0.0f, after = 0.0f;

        for (size_t channel = 0; channel < 2; ++channel)
        {
            for (int i = latency; i < passFrom + latency; ++i)
                before = std::max (before, std::abs (out[channel][static_cast<size_t> (i)] - input[channel][static_cast<size_t> (i - latency)]));

            for (int i = passTo + fade + latency; i < static_cast<int> (out[channel].size()); ++i)
                after = std::max (after, std::abs (out[channel][static_cast<size_t> (i)] - input[channel][static_cast<size_t> (i - latency)]));
        }

        CHECK_NEAR (before, 0.0, 0.0);
        CHECK_NEAR (after, 0.0, 0.0);

        // The first 10 ms of the pass after the fade, and the last 10 ms before the gate closes.
        const auto carvedHz = mode == CarveMode::carve ? 1000.0 : 4000.0;
        const auto keptHz = mode == CarveMode::carve ? 4000.0 : 1000.0;
        const auto edge = 480;

        for (const auto start : { passFrom + fade, passTo - fade - edge })
        {
            const auto from = start + latency, to = from + edge;
            CHECK_LESS (levelDb (out, carvedHz, from, to) - levelDb (input, carvedHz, start, start + edge), -55.0);
            CHECK_NEAR (levelDb (out, keptHz, from, to) - levelDb (input, keptHz, start, start + edge), 0.0, 0.2);
        }
    }
}

ARTFREQ_TEST (maskPlanWideHaloMatchesTheDirectOne)
{
    // widenMaskPlan builds its window up from doubling shifts: it must give exactly what a
    // straightforward maximum over the window gives, for small and wide halos and at the edges.
    Canvas canvas;
    juce::Random random (5);

    for (int i = 0; i < 300; ++i)
        canvas.at (random.nextInt (Canvas::width), random.nextInt (Canvas::height)) = random.nextFloat();

    for (int x = 0; x < 6; ++x)
        canvas.at (x, 3) = 0.9f;   // at the corner

    const auto exact = buildMaskPlan (canvas, MaskSettings());

    for (const auto& [haloRows, haloColumns] : { std::pair<int, int> { 4, 8 }, { 1, 0 }, { 0, 37 }, { 7, 64 } })
    {
        const auto widened = widenMaskPlan (*exact, haloRows, haloColumns);
        MaskSettings settings;
        settings.haloRows = haloRows;
        settings.haloColumns = haloColumns;
        const auto built = buildMaskPlan (canvas, settings);

        auto mismatches = 0;

        for (int c = 0; c < Canvas::width; c += 3)
        {
            for (int r = 0; r < Canvas::height; r += 3)
            {
                auto expected = 0.0f;

                for (int dc = -haloColumns; dc <= haloColumns; ++dc)
                    for (int dr = -haloRows; dr <= haloRows; ++dr)
                        if (c + dc >= 0 && c + dc < Canvas::width && r + dr >= 0 && r + dr < Canvas::height)
                            expected = std::max (expected, exact->column (c + dc)[r + dr]);

                if (! juce::exactlyEqual (widened->column (c)[r], expected) || ! juce::exactlyEqual (built->column (c)[r], expected))
                    ++mismatches;
            }
        }

        CHECK_NEAR (mismatches, 0, 0);
        CHECK (widened->isColumnUsed (haloColumns) && ! widened->empty);
    }
}

ARTFREQ_TEST (tunedCarverReadsColumnEdgesAlike)
{
    // Every other column drawn, over a pass of a whole number of samples per column (375): many
    // of the points where frames read the drawing fall exactly on the edge between two columns,
    // where a host's position rounds either way depending on where its blocks start (here a few
    // ulps one way in one run, the other way in the other; the pass opens after its first
    // moment, where a loop's position would wrap round to its end). Both runs must read the
    // same columns (columnNudge).
    Canvas stripes;

    for (int x = 1; x < Canvas::width; x += 2)
        for (int y = 300; y < 700; ++y)
            stripes.at (x, y) = 1.0f;

    const auto plan = buildMaskPlan (stripes, MaskSettings());
    const auto input = noise (3 * 48000, 13);

    for (const auto mode : { CarveMode::carve, CarveMode::stencil })
    {
        CarveSettings settings;
        settings.mode = mode;

        auto early = preparedTuned();
        early->setPlan (plan.get());
        const auto reference = run (*early, input, { 512 }, settings, 8.0 * rate, 1000, -1.0e-15);

        auto late = preparedTuned();
        late->setPlan (plan.get());
        const auto other = run (*late, input, { 300, 700, 1 }, settings, 8.0 * rate, 1000, 1.0e-15);

        CHECK_LESS (largestDifference (reference, other), 1.0e-6);
        CHECK_GREATER (differenceFromDelayed (input, reference, early->getLatencySamples()), 0.01);
    }
}

ARTFREQ_TEST (tunedCarverReadsThinVerticalStrokes)
{
    // A one-beat drawing at 120 BPM (0.5 s, 23 samples a column) with a stroke 8 columns wide
    // (190 samples) over 1 - 4 kHz: a frame stands for a hop of 1024 samples, so read at one
    // moment per hop the stroke would be missed in most passes. Read at points 128 samples
    // apart it is carved in every pass, by about its share of the hop.
    const auto passSamples = 24000.0;
    const auto plan = bandPlan (1000.0, 4000.0, 500, 508);
    const auto input = noise (6 * 24000, 17);

    auto carver = preparedTuned();
    carver->setPlan (plan.get());
    CarveSettings settings;
    settings.depthDb = -60.0f;
    const auto out = run (*carver, input, { 512 }, settings, passSamples);
    const auto latency = carver->getLatencySamples();

    // The energy of 1.5 - 3 kHz around the stroke (a hop either side of it), in and out.
    const auto bandEnergy = [] (const Stereo& audio, int from, int to)
    {
        juce::dsp::FFT fft (11);
        std::vector<float> buffer (4096, 0.0f);
        double energy = 0.0;

        for (int start = from; start + 2048 <= to; start += 256)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);

            for (int i = 0; i < 2048; ++i)
                buffer[static_cast<size_t> (i)] = audio[0][static_cast<size_t> (start + i)]
                                                * static_cast<float> (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / 2048));

            fft.performFrequencyOnlyForwardTransform (buffer.data());

            for (int bin = 64; bin <= 128; ++bin)  // 1.5 - 3 kHz at 23.4 Hz a bin
                energy += static_cast<double> (buffer[static_cast<size_t> (bin)]) * buffer[static_cast<size_t> (bin)];
        }

        return energy;
    };

    for (int pass = 1; pass < 5; ++pass)
    {
        const auto strokeStart = static_cast<int> (pass * passSamples + 500.0 / Canvas::width * passSamples);
        const auto from = strokeStart - 1024, to = strokeStart + 190 + 1024;
        const auto drop = 10.0 * std::log10 (bandEnergy (out, from + latency, to + latency) / bandEnergy (input, from, to));
        CHECK_LESS (drop, -3.0);
    }
}

ARTFREQ_TEST (tunedCarverHaloFollowsTheMode)
{
    // Carve widens what it carves by settings.haloBins. A sine on the drawn band's top edge is
    // carved by about half of Depth with Carve's 1 bin and by all of it with Carve + Draw's 3;
    // one 3 bins (35 Hz) above the band is left alone with 1 bin and partly carved with 3. And
    // with holdLoudest a stroke that covers part of a frame's hop is carved by all of Depth in
    // that frame, not by its share (the frames either side, which do not reach it, still let
    // some of the tone through: an STFT spreads a frame's change over its whole length).
    const auto input = sines (3 * 48000, { { 1000.0, 0.5f }, { 1035.2, 0.5f } });
    const auto plan = bandPlan (900.0, 1000.0);
    const auto from = 4096 + 8192, to = 3 * 48000;

    const auto carvedBy = [&] (int haloBins, double hz)
    {
        auto carver = preparedTuned();
        carver->setPlan (plan.get());
        CarveSettings settings;
        settings.haloBins = haloBins;
        const auto out = run (*carver, input, { 512 }, settings);
        return levelDb (out, hz, from, to) - levelDb (input, hz, from, to);
    };

    CHECK_GREATER (carvedBy (1, 1000.0), -40.0);
    CHECK_LESS (carvedBy (3, 1000.0), -55.0);
    CHECK_GREATER (carvedBy (1, 1035.2), -1.0);
    CHECK_LESS (carvedBy (3, 1035.2), -5.0);

    // A stroke of 4 columns in a pass of 1024 x 48 samples: 192 samples of a 1024-sample hop,
    // between two frames' centres (which are hop starts).
    const auto stroke = bandPlan (800.0, 1250.0, 517, 521);
    const auto tone = sines (3 * 48000, { { 1000.0, 0.5f } });
    const auto passSamples = 1024.0 * 48.0;
    const auto strokeAt = static_cast<int> (passSamples * 517.0 / Canvas::width);

    const auto deepest = [&] (bool holdLoudest)
    {
        auto carver = preparedTuned();
        carver->setPlan (stroke.get());
        CarveSettings settings;
        settings.holdLoudest = holdLoudest;
        const auto out = run (*carver, tone, { 512 }, settings, passSamples);
        const auto latency = carver->getLatencySamples();
        return levelDb (out, 1000.0, strokeAt + latency - 256, strokeAt + latency + 448)
             - levelDb (tone, 1000.0, strokeAt - 256, strokeAt + 448);
    };

    const auto byShare = deepest (false);
    const auto byMost = deepest (true);
    CHECK_LESS (byShare, -2.0);
    CHECK_LESS (byMost, byShare - 3.0);
}

ARTFREQ_TEST (tunedCarverSpreadsTheDrawingOverTheHop)
{
    // A frame's transforms need the last sample of its hop, and the first sample played out
    // after them needs them, so they run in the call that completes the hop. Reading the drawing
    // for the next frame (its columns at the points across the hop, then the gains) is spread
    // over the calls before that one: with small blocks, the call that transforms a frame reads
    // no column of the drawing and no call reads more than one, at any rate. The points are
    // spaced in time, 8 a hop at any rate (the work does not grow with the rate), and the output
    // is the same however the work falls (tunedCarverIsBlockSizeIndependent).
    const auto plan = bandPlan (25.0, 19000.0);

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const auto mode : { CarveMode::carve, CarveMode::stencil })
        {
            auto carver = std::make_unique<TunedCarver>();
            carver->prepare (sampleRate, 512, 2);
            carver->setPlan (plan.get());
            CHECK_NEAR (carver->getPointsPerHop(), 8, 0);

            const auto block = 32;
            const auto passSamples = 0.5 * sampleRate;   // one beat at 120 BPM: a column every few points
            std::vector<float> left (static_cast<size_t> (block)), right (static_cast<size_t> (block));
            float* channels[2] = { left.data(), right.data() };
            juce::Random random (6);
            CarveSettings settings;
            settings.mode = mode;
            auto frames = 0, mostColumns = 0, columnsWhileTransforming = 0, columns = 0;
            carver->takeWork();

            for (int start = 0; start < static_cast<int> (sampleRate); start += block)
            {
                for (int i = 0; i < block; ++i)
                {
                    left[static_cast<size_t> (i)] = random.nextFloat() - 0.5f;
                    right[static_cast<size_t> (i)] = random.nextFloat() - 0.5f;
                }

                PlaySegment segment;
                segment.length = block;
                segment.playing = true;
                segment.fraction = std::fmod (start / passSamples, 1.0);
                segment.fractionPerSample = 1.0 / passSamples;
                carver->process (channels, 2, block, &segment, 1, settings);

                const auto work = carver->takeWork();
                frames += work.framesTransformed;
                columns += work.columnsRead;
                mostColumns = std::max (mostColumns, work.columnsRead);

                if (work.framesTransformed > 0)
                    columnsWhileTransforming += work.columnsRead;
            }

            // Every frame of the second read columns (the drawing covers every column), and none
            // of them in the call that transformed it.
            const auto hops = static_cast<int> (sampleRate) / carver->getHopSize();
            CHECK_GREATER (frames, hops - 6);
            CHECK_GREATER (columns, 4 * frames);
            CHECK_NEAR (columnsWhileTransforming, 0, 0);
            CHECK_NEAR (mostColumns, 1, 0);
        }
    }

    // Blocks as long as a hop or longer still do it all in one call, as before.
    auto whole = preparedTuned();
    whole->setPlan (plan.get());
    run (*whole, noise (8192), { 4096 }, CarveSettings(), 0.5 * rate);
    const auto work = dynamic_cast<TunedCarver&> (*whole).takeWork();
    CHECK_NEAR (work.framesTransformed, 8, 0);
}

ARTFREQ_TEST (tunedCarverFinishesAFrameWithThePlanItBegan)
{
    // A frame reads the drawing while its hop comes in, from the plan set when it began: a plan
    // set in the middle of the hop is used from the next frame on, and until then the engine
    // reports the earlier one as still in use (getFadingPlan), so the caller keeps it alive.
    const auto first = bandPlan (800.0, 1250.0);
    const auto second = bandPlan (3000.0, 5000.0);

    auto carver = preparedTuned();
    carver->setPlan (first.get());
    const auto hop = dynamic_cast<TunedCarver&> (*carver).getHopSize();
    run (*carver, noise (hop * 8 + 100, 3), { 512 }, CarveSettings());   // into a hop: its frame has begun
    CHECK (carver->getFadingPlan() == nullptr);

    carver->setPlan (second.get());
    CHECK (carver->getFadingPlan() == first.get());
    CHECK (carver->getPlan() == second.get());

    // The rest of the hop and a little of the next: that frame was transformed, and the next one
    // began with the new plan.
    run (*carver, noise (hop - 100 + 10, 4), { 512 }, CarveSettings());
    CHECK (carver->getFadingPlan() == nullptr);

    // A reset lets go of it too.
    carver->setPlan (first.get());
    CHECK (carver->getFadingPlan() == second.get());
    carver->reset();
    CHECK (carver->getFadingPlan() == nullptr);
}
