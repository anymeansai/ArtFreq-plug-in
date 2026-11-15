#pragma once

// Plans built by hand, and the signal measurements the engine, export and analyser tests share.
//
// The plans here do not come from buildPlan: they are filled in directly (lines, amplitudes, the
// active lists with their wrap rule, the layout hash), so the audio tests depend only on the
// ArtPlan data contract and stay meaningful whatever the plan builder does.

#include "core/ArtPlan.h"
#include "dsp/ArtEngine.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace test
{

constexpr double twoPi = 6.283185307179586476925286766559;

inline double toDb (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

/** FNV-1a over the frequencies' bits: equal lines, equal hash. */
inline uint64_t hashFrequencies (const std::vector<float>& frequencies)
{
    uint64_t hash = 1469598103934665603ull;

    for (auto frequency : frequencies)
    {
        uint32_t bits = 0;
        std::memcpy (&bits, &frequency, sizeof (bits));

        for (int byte = 0; byte < 4; ++byte)
        {
            hash ^= (bits >> (8 * byte)) & 0xffu;
            hash *= 1099511628211ull;
        }
    }

    return hash;
}

/** Fills in the derived parts of a plan whose lines and amplitudes are set: the active lists
    (a line is active for column c if it is non-zero at c or at (c + 1) % numColumns), the
    layout hash and the silent flag. */
inline void finishPlan (artfreq::ArtPlan& plan)
{
    const auto lines = static_cast<size_t> (plan.numLines);
    const auto columns = static_cast<size_t> (plan.numColumns);
    const auto nonZero = [&] (size_t column, size_t line) { return std::abs (plan.amplitudes[column * lines + line]) > 0.0f; };

    plan.activeStart.assign (columns + 1, 0);
    plan.activeLines.clear();
    plan.silent = true;

    for (size_t column = 0; column < columns; ++column)
    {
        plan.activeStart[column] = static_cast<uint32_t> (plan.activeLines.size());
        const auto next = (column + 1) % columns;

        for (size_t line = 0; line < lines; ++line)
        {
            if (nonZero (column, line))
                plan.silent = false;

            if (nonZero (column, line) || nonZero (next, line))
                plan.activeLines.push_back (static_cast<uint16_t> (line));
        }
    }

    plan.activeStart[columns] = static_cast<uint32_t> (plan.activeLines.size());
    plan.layoutHash = hashFrequencies (plan.frequencies);
}

/** A plan with the given lines (ascending Hz) whose amplitude at (column, line) comes from
    `amplitude`. */
inline std::shared_ptr<artfreq::ArtPlan> makePlan (const std::vector<float>& frequencies,
                                                   const std::function<float (int column, int line)>& amplitude,
                                                   int numColumns = artfreq::Canvas::width)
{
    auto plan = std::make_shared<artfreq::ArtPlan>();
    plan->numLines = static_cast<int> (frequencies.size());
    plan->numColumns = numColumns;
    plan->frequencies = frequencies;
    plan->lineNorm.resize (frequencies.size());

    for (size_t line = 0; line < frequencies.size(); ++line)
        plan->lineNorm[line] = (static_cast<float> (line) + 0.5f) / static_cast<float> (frequencies.size());

    plan->amplitudes.resize (frequencies.size() * static_cast<size_t> (numColumns));

    for (int column = 0; column < numColumns; ++column)
        for (int line = 0; line < plan->numLines; ++line)
            plan->amplitudes[static_cast<size_t> (column) * frequencies.size() + static_cast<size_t> (line)] = amplitude (column, line);

    finishPlan (*plan);
    return plan;
}

/** Every line at the same amplitude in every column. */
inline std::shared_ptr<artfreq::ArtPlan> makeConstantPlan (const std::vector<float>& frequencies, float amplitude,
                                                           int numColumns = artfreq::Canvas::width)
{
    return makePlan (frequencies, [amplitude] (int, int) { return amplitude; }, numColumns);
}

/** `count` lines spaced evenly in log frequency from lowHz to highHz. */
inline std::vector<float> logLines (int count, double lowHz, double highHz)
{
    std::vector<float> frequencies;

    for (int i = 0; i < count; ++i)
    {
        const auto norm = (i + 0.5) / count;
        frequencies.push_back (static_cast<float> (lowHz * std::pow (highHz / lowHz, norm)));
    }

    return frequencies;
}

inline artfreq::PlaySegment segment (int start, int length, bool playing, double fraction, double perSample, bool jump = false)
{
    artfreq::PlaySegment s;
    s.start = start;
    s.length = length;
    s.playing = playing;
    s.fraction = fraction;
    s.fractionPerSample = perSample;
    s.jump = jump;
    return s;
}

/** Plays the engine (gate on) from `fraction` for numSamples in blocks, the position moving by
    `perSample`; the first block is a jump if `jumpFirst`. Returns the output. */
inline std::vector<float> play (artfreq::ArtEngine& engine, int numSamples, double fraction, double perSample,
                                int blockSize = 512, bool jumpFirst = false)
{
    std::vector<float> out (static_cast<size_t> (numSamples));

    for (int done = 0; done < numSamples; done += blockSize)
    {
        const auto length = std::min (blockSize, numSamples - done);
        const auto s = segment (0, length, true, fraction + done * perSample, perSample, jumpFirst && done == 0);
        engine.render (out.data() + done, length, &s, 1);
    }

    return out;
}

/** Renders with the gate closed (fading out and then silence). */
inline std::vector<float> stop (artfreq::ArtEngine& engine, int numSamples, double fraction = 0.0)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    const auto s = segment (0, numSamples, false, fraction, 0.0);
    engine.render (out.data(), numSamples, &s, 1);
    return out;
}

inline double rms (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    to = to == 0 ? x.size() : std::min (to, x.size());
    double sum = 0.0;

    for (auto i = from; i < to; ++i)
        sum += static_cast<double> (x[i]) * x[i];

    return to > from ? std::sqrt (sum / static_cast<double> (to - from)) : 0.0;
}

inline double peak (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    to = to == 0 ? x.size() : std::min (to, x.size());
    double largest = 0.0;

    for (auto i = from; i < to; ++i)
        largest = std::max (largest, static_cast<double> (std::abs (x[i])));

    return largest;
}

/** The largest difference between neighbouring samples: a click shows up as a step. */
inline double largestStep (const std::vector<float>& x, size_t from = 1, size_t to = 0)
{
    to = to == 0 ? x.size() : std::min (to, x.size());
    double largest = 0.0;

    for (auto i = std::max (from, static_cast<size_t> (1)); i < to; ++i)
        largest = std::max (largest, static_cast<double> (std::abs (x[i] - x[i - 1])));

    return largest;
}

/** The largest step a sum of sines of these frequencies and peak amplitudes can take. */
inline double sineStepBound (const std::vector<float>& frequencies, const std::vector<float>& amplitudes, double sampleRate)
{
    double bound = 0.0;

    for (size_t i = 0; i < frequencies.size(); ++i)
        bound += 2.0 * amplitudes[i] * std::sin (0.5 * twoPi * std::min (0.25, frequencies[i] / sampleRate));

    return bound;
}

/** Magnitude spectrum (Hann window) of x[from .. from + 2^order). */
struct Spectrum
{
    std::vector<double> magnitudes;  // 2^order / 2 + 1 bins, scaled so a sine centred on a bin reads its peak amplitude
    double binHz = 0.0;
    double powerScale = 0.0;         // turns the root of a sum of squared magnitudes into a peak amplitude

    /** The peak amplitude of a sine at `hz`, from the power in the bins around it (so it does
        not depend on where between two bins the sine falls). */
    double levelAt (double hz, int bins = 3) const
    {
        const auto centre = static_cast<int> (std::lround (hz / binHz));
        double power = 0.0;

        for (int b = std::max (0, centre - bins); b <= std::min (static_cast<int> (magnitudes.size()) - 1, centre + bins); ++b)
            power += magnitudes[static_cast<size_t> (b)] * magnitudes[static_cast<size_t> (b)];

        return std::sqrt (power) * powerScale;
    }

    /** The strongest bin's frequency, refined by a parabola through it and its neighbours. */
    double peakHz() const
    {
        size_t best = 1;

        for (size_t b = 1; b + 1 < magnitudes.size(); ++b)
            if (magnitudes[b] > magnitudes[best])
                best = b;

        const auto a = std::log (std::max (magnitudes[best - 1], 1.0e-30));
        const auto b = std::log (std::max (magnitudes[best], 1.0e-30));
        const auto c = std::log (std::max (magnitudes[best + 1], 1.0e-30));
        const auto denominator = a - 2.0 * b + c;
        const auto offset = std::abs (denominator) > 1.0e-30 ? 0.5 * (a - c) / denominator : 0.0;
        return (static_cast<double> (best) + offset) * binHz;
    }
};

inline Spectrum spectrum (const std::vector<float>& x, size_t from, int order, double sampleRate)
{
    const auto size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> work (static_cast<size_t> (2 * size), 0.0f);
    double windowSum = 0.0, windowSquares = 0.0;

    for (int i = 0; i < size; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (twoPi * i / size);
        const auto index = from + static_cast<size_t> (i);
        work[static_cast<size_t> (i)] = index < x.size() ? static_cast<float> (w * x[index]) : 0.0f;
        windowSum += w;
        windowSquares += w * w;
    }

    fft.performFrequencyOnlyForwardTransform (work.data(), true);

    Spectrum result;
    result.binHz = sampleRate / size;
    result.magnitudes.resize (static_cast<size_t> (size / 2 + 1));

    // Parseval: the positive-frequency bins of a sine of peak A hold A^2 N sum (w^2) / 4.
    result.powerScale = windowSum / std::sqrt (size * windowSquares);

    for (size_t b = 0; b < result.magnitudes.size(); ++b)
        result.magnitudes[b] = 2.0 * work[b] / windowSum;

    return result;
}

} // namespace test
