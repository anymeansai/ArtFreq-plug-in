#pragma once

// The plan: the canvas translated into a bank of sine waves.
//
// Each "line" is one sine wave at a fixed frequency; it shows up in a spectrogram as a
// horizontal line at that height. Density is how many lines share the canvas height: few lines
// look like scan lines, many lines merge into solid shapes. A line's loudness over time comes
// from the canvas row(s) it covers, column by column.
//
// Plans are immutable once built and are shared between the message thread (which builds them
// and shows them) and the audio thread (which plays them), see PlanExchange.

#include "Compositor.h"
#include "FrequencyScale.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace freqart
{

/** RMS the loudest moment of every plan is normalised to: -12 dBFS before the output level. */
constexpr float planTargetRms = 0.25f;

struct PlanSettings
{
    FrequencyScale scale;
    int density = 256;              // lines across the full canvas height, 16..1024
    float contrastDb = 40.0f;       // dB between a value of 1 and a value just above 0
    float slopeDbPerOctave = 0.0f;  // tilt of the user's analyser, compensated (pivot 1 kHz)
    float minLineSpacingHz = 5.0f;  // closer lines beat audibly and visibly: they are merged
    float maxFrequencyHz = 20000.0f;// lines above this are dropped (kept below Nyquist by the engine too)
};

struct ArtPlan
{
    uint64_t version = 0;  // set by the publisher; strictly increasing per processor

    int numLines = 0;
    int numColumns = 0;   // always Canvas::width for a built plan

    std::vector<float> frequencies;  // numLines, ascending, Hz
    std::vector<float> lineNorm;     // numLines, height of each line on the scale (0 = bottom, 1 = top)

    // Linear peak amplitude of each line's sine at each column, column-major:
    // amplitudes[column * numLines + line]. Includes contrast, slope compensation and
    // normalisation (the loudest column has an RMS of planTargetRms when all its sines play).
    std::vector<float> amplitudes;

    // For each column c, the lines whose amplitude is non-zero at column c OR at column
    // (c + 1) % numColumns (the two ends of the interpolation from c to c + 1):
    // activeLines[activeStart[c] .. activeStart[c + 1]).
    std::vector<uint32_t> activeStart;  // numColumns + 1
    std::vector<uint16_t> activeLines;

    uint64_t layoutHash = 0;  // hash of `frequencies`; equal hashes mean the same lines
    bool silent = true;       // every amplitude is 0

    float amplitude (int column, int line) const noexcept
    {
        return amplitudes[static_cast<size_t> (column) * static_cast<size_t> (numLines) + static_cast<size_t> (line)];
    }
};

/** Builds a plan from a canvas.

    Lines: density lines are spaced evenly in canvas height, line i (0 = bottom) at
    norm (i + 0.5) / density, frequency scale.toHz (norm). Going up from the bottom, a line closer
    than minLineSpacingHz to the last kept line is merged into it (its canvas span joins the kept
    line's span); lines below 10 Hz or above maxFrequencyHz are dropped.

    Values: a line's value at a column is the MAXIMUM of the canvas over the rows its span covers
    (so thin strokes never fall between lines), where a line's span runs halfway to its kept
    neighbours.

    Loudness: value v (0..1) becomes amplitude 10^((v - 1) * contrastDb / 20) for v > 0.001, else
    0; times the slope compensation 10^(-slope * log2 (f / 1000) / 20); then every amplitude is
    scaled by one factor so that the loudest column's RMS, sqrt (sum (a^2) / 2), is
    planTargetRms. A canvas with nothing on it gives a silent plan with the lines still laid out. */
std::unique_ptr<ArtPlan> buildPlan (const Canvas& canvas, const PlanSettings& settings);

} // namespace freqart
