#pragma once

// How frequencies are laid out from the bottom to the top of a spectrogram.
//
// FreqArt draws on a canvas that is a picture of the user's spectrogram: x is time across the
// drawing's length, y is frequency on the same scale as the analyser the art will be seen on
// (MiniMeters and most analysers default to a logarithmic axis). Everything that converts
// between a height on the canvas and a frequency in Hz goes through FrequencyScale, so the
// canvas, the plan that makes the sound and the live view always agree.

#include <juce_core/juce_core.h>

#include <vector>

namespace freqart
{

enum class ScaleType
{
    log = 0,     // equal height per octave (MiniMeters, Pro-Q, SPAN, most analysers)
    linear = 1,  // equal height per Hz
    mel = 2      // perceptual: roughly linear below 700 Hz, logarithmic above
};

/** Mel conversion (O'Shaughnessy): mel = 2595 * log10 (1 + hz / 700). */
double hzToMel (double hz) noexcept;
double melToHz (double mel) noexcept;

struct FrequencyScale
{
    ScaleType type = ScaleType::log;
    double minHz = 20.0;     // bottom edge of the canvas
    double maxHz = 20000.0;  // top edge of the canvas

    /** 0 at minHz (bottom), 1 at maxHz (top). Not clamped: frequencies outside the range give
        values outside [0, 1]. Frequencies at or below 0 Hz map to a large negative value on
        the log scale rather than -inf. */
    double toNorm (double hz) const noexcept;

    /** The inverse of toNorm. Not clamped. */
    double toHz (double norm) const noexcept;

    /** minHz and maxHz forced into a usable order and range (minHz >= 1 Hz, maxHz > minHz * 1.5). */
    FrequencyScale sanitised() const noexcept;

    bool operator== (const FrequencyScale& other) const noexcept
    {
        return type == other.type && juce::exactlyEqual (minHz, other.minHz) && juce::exactlyEqual (maxHz, other.maxHz);
    }

    bool operator!= (const FrequencyScale& other) const noexcept { return ! operator== (other); }
};

/** The quick placements offered for text and images. */
enum class Band
{
    low = 0,   // 40 - 250 Hz
    mid = 1,   // 300 - 3000 Hz
    high = 2,  // 3.5 - 16 kHz
    full = 3   // the whole visible range, less a small margin at each end
};

struct HzRange
{
    double low = 0.0;
    double high = 0.0;
};

/** The frequency range of a band, clipped to the visible range of the scale (with a margin of
    2 % of the canvas height at each end for Band::full). */
HzRange bandRange (Band band, const FrequencyScale& scale) noexcept;

/** Short label for a frequency: "20 Hz", "250 Hz", "1 kHz", "2.5 kHz", "16 kHz". */
juce::String formatHz (double hz);

/** Frequencies worth a grid line and label on the given scale, in ascending order, all inside
    the visible range. Log: 20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k. Linear: a round step
    giving 5 - 10 lines. Mel: like log but thinned so labels do not crowd. */
std::vector<double> gridFrequencies (const FrequencyScale& scale);

} // namespace freqart
