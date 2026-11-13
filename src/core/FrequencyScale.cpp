#include "FrequencyScale.h"

#include <algorithm>
#include <cmath>

namespace freqart
{

namespace
{

// Below this the log scale would head for -inf: frequencies at or below 0 Hz are treated as this.
constexpr double tinyHz = 1.0e-6;

bool isUsable (const FrequencyScale& scale) noexcept
{
    return std::isfinite (scale.minHz) && std::isfinite (scale.maxHz)
        && scale.minHz > 0.0 && scale.maxHz > scale.minHz;
}

/** Position of hz on the axis before normalising (log Hz, Hz or mel). */
double axisValue (ScaleType type, double hz) noexcept
{
    switch (type)
    {
        case ScaleType::linear: return hz;
        case ScaleType::mel:    return hzToMel (hz);
        case ScaleType::log:    break;
    }

    return std::log (std::max (hz, tinyHz));
}

double fromAxisValue (ScaleType type, double value) noexcept
{
    switch (type)
    {
        case ScaleType::linear: return value;
        case ScaleType::mel:    return melToHz (value);
        case ScaleType::log:    break;
    }

    return std::exp (value);
}

/** Steps of 1, 2, 2.5 and 5 times a power of ten: consecutive steps differ by at most 2x, so one
    of them always gives between 5 and 10 grid lines. */
std::vector<double> linearGrid (double minHz, double maxHz)
{
    const auto span = maxHz - minHz;
    std::vector<double> result;

    if (! (span > 0.0))
        return result;

    const double mantissas[] = { 1.0, 2.0, 2.5, 5.0 };
    auto decade = std::pow (10.0, std::floor (std::log10 (span / 10.0)) - 1.0);

    for (int attempt = 0; attempt < 32; ++attempt, decade *= 10.0)
    {
        for (const auto mantissa : mantissas)
        {
            const auto step = mantissa * decade;
            const auto first = std::ceil (minHz / step - 1.0e-9);
            const auto last = std::floor (maxHz / step + 1.0e-9);

            if (last - first + 1.0 <= 10.0)
            {
                for (auto k = first; k <= last; k += 1.0)
                    if (k * step > 0.0)
                        result.push_back (k * step);

                return result;
            }
        }
    }

    return result;
}

/** 1-2-5 frequencies inside the range, added roundest first (powers of ten, then 2s, then 5s)
    and skipped when they would sit closer than minGap (in canvas height) to one already chosen,
    so the dense end of a mel axis keeps its most useful labels. */
std::vector<double> roundGrid (const FrequencyScale& scale, double minGap)
{
    std::vector<double> chosen;
    std::vector<double> chosenNorm;

    const auto firstDecade = static_cast<int> (std::floor (std::log10 (scale.minHz))) - 1;
    const auto lastDecade = static_cast<int> (std::ceil (std::log10 (scale.maxHz)));

    for (const auto mantissa : { 1.0, 2.0, 5.0 })
    {
        for (int decade = firstDecade; decade <= lastDecade; ++decade)
        {
            const auto hz = mantissa * std::pow (10.0, decade);

            if (hz < scale.minHz * (1.0 - 1.0e-9) || hz > scale.maxHz * (1.0 + 1.0e-9))
                continue;

            const auto norm = scale.toNorm (hz);
            const auto crowded = std::any_of (chosenNorm.begin(), chosenNorm.end(),
                                              [norm, minGap] (double other) { return std::abs (other - norm) < minGap; });

            if (! crowded)
            {
                chosen.push_back (hz);
                chosenNorm.push_back (norm);
            }
        }
    }

    std::sort (chosen.begin(), chosen.end());
    return chosen;
}

} // namespace

double hzToMel (double hz) noexcept
{
    // Mirrored below 0 Hz so the mapping stays monotonic and invertible everywhere.
    if (hz < 0.0)
        return -hzToMel (-hz);

    return 2595.0 * std::log10 (1.0 + hz / 700.0);
}

double melToHz (double mel) noexcept
{
    if (mel < 0.0)
        return -melToHz (-mel);

    return 700.0 * (std::pow (10.0, mel / 2595.0) - 1.0);
}

double FrequencyScale::toNorm (double hz) const noexcept
{
    if (! isUsable (*this))
        return sanitised().toNorm (hz);

    const auto bottom = axisValue (type, minHz);
    const auto top = axisValue (type, maxHz);
    return (axisValue (type, hz) - bottom) / (top - bottom);
}

double FrequencyScale::toHz (double norm) const noexcept
{
    if (! isUsable (*this))
        return sanitised().toHz (norm);

    const auto bottom = axisValue (type, minHz);
    const auto top = axisValue (type, maxHz);
    return fromAxisValue (type, bottom + norm * (top - bottom));
}

FrequencyScale FrequencyScale::sanitised() const noexcept
{
    FrequencyScale result;

    const auto typeIndex = static_cast<int> (type);
    result.type = (typeIndex >= 0 && typeIndex <= 2) ? type : ScaleType::log;

    auto low = std::isfinite (minHz) ? minHz : 20.0;
    auto high = std::isfinite (maxHz) ? maxHz : 20000.0;

    if (high < low)
        std::swap (low, high);

    // Absurd values are bounded so the result is always finite and usable.
    low = std::clamp (low, 1.0, 1.0e6);
    high = std::min (high, 1.0e7);

    // Less than half an octave is not a usable spectrogram: widen to an octave.
    if (! (high > low * 1.5))
        high = low * 2.0;

    result.minHz = low;
    result.maxHz = high;
    return result;
}

HzRange bandRange (Band band, const FrequencyScale& scale) noexcept
{
    const auto s = scale.sanitised();

    double low = 0.0, high = 0.0;

    switch (band)
    {
        case Band::low:  low = 40.0;   high = 250.0;   break;
        case Band::mid:  low = 300.0;  high = 3000.0;  break;
        case Band::high: low = 3500.0; high = 16000.0; break;
        case Band::full:
        default:
            return { s.toHz (0.02), s.toHz (0.98) };
    }

    // Clip to the visible range. A band that falls (almost) entirely outside it becomes a strip
    // at the nearest edge, so "Low" on a 1 kHz - 20 kHz analyser still means "at the bottom".
    constexpr double minSpan = 0.08;
    auto bottom = std::clamp (s.toNorm (low), 0.0, 1.0);
    auto top = std::clamp (s.toNorm (high), 0.0, 1.0);

    if (top - bottom < minSpan)
    {
        const auto centre = std::clamp ((bottom + top) * 0.5, minSpan * 0.5, 1.0 - minSpan * 0.5);
        bottom = centre - minSpan * 0.5;
        top = centre + minSpan * 0.5;
        return { s.toHz (bottom), s.toHz (top) };
    }

    return { std::max (low, s.minHz), std::min (high, s.maxHz) };
}

juce::String formatHz (double hz)
{
    if (! std::isfinite (hz) || hz <= 0.0)
        return "0 Hz";

    if (hz < 10.0 && std::abs (hz - std::round (hz)) > 0.05)
        return juce::String (hz, 1) + " Hz";

    if (std::round (hz) < 1000.0)
        return juce::String (static_cast<int> (std::round (hz))) + " Hz";

    // One decimal at most, dropped when it is zero: "1 kHz", "2.5 kHz", "16 kHz".
    const auto tenths = std::round (hz / 100.0);
    const auto whole = static_cast<juce::int64> (tenths) / 10;
    const auto fraction = static_cast<juce::int64> (tenths) % 10;

    if (fraction == 0)
        return juce::String (whole) + " kHz";

    return juce::String (whole) + "." + juce::String (fraction) + " kHz";
}

std::vector<double> gridFrequencies (const FrequencyScale& scale)
{
    const auto s = scale.sanitised();

    if (s.type == ScaleType::linear)
        return linearGrid (s.minHz, s.maxHz);

    // The log axis spaces 1-2-5 steps evenly, so nothing is thinned in practice; the mel axis
    // squeezes the lows together and needs the gap.
    auto result = roundGrid (s, s.type == ScaleType::mel ? 0.045 : 0.03);

    // A narrow range may hold fewer than three round 1-2-5 values: use even steps instead.
    if (result.size() < 3)
        return linearGrid (s.minHz, s.maxHz);

    return result;
}

} // namespace freqart
