// Spectral carving: the mask plan built from the canvas, its exchange, the baseline engine
// (transparency, carving and stencilling a band, the latency, the gate, block sizes) and the
// tuned engine (engine 1: the same, plus thin low strokes and the gate at the output).

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

using namespace freqart;

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
    `gateFrom`. */
Stereo run (SpectralCarver& carver, const Stereo& audio, const std::vector<int>& blocks, const CarveSettings& settings,
            double passSamples = 8.0 * rate, int gateFrom = 0)
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
            segment.fraction = std::fmod (from / passSamples, 1.0);
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

FREQART_TEST (maskPlanFollowsTheCanvas)
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

FREQART_TEST (maskPlanHaloWidensShapes)
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

FREQART_TEST (maskPlansTravelThroughTheExchange)
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

FREQART_TEST (carverIsTransparentWithNothingToCarve)
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

FREQART_TEST (carverCarvesASineInsideTheBand)
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

FREQART_TEST (carverStencilKeepsOnlyTheDrawing)
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

FREQART_TEST (carverReportsItsLatency)
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

FREQART_TEST (carverIsBlockSizeIndependent)
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

FREQART_TEST (carverGateOpensWithThePass)
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

FREQART_TEST (tunedCarverIsTransparentWithNothingToCarve)
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

FREQART_TEST (tunedCarverCarvesASineInsideTheBand)
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

FREQART_TEST (tunedCarverCarvesTheLows)
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

FREQART_TEST (tunedCarverStencilKeepsOnlyTheDrawing)
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

FREQART_TEST (tunedCarverReportsItsLatency)
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

FREQART_TEST (tunedCarverIsBlockSizeIndependent)
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

FREQART_TEST (tunedCarverGateWorksAtTheOutput)
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
