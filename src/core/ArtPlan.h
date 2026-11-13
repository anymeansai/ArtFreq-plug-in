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

/** How the crowded low end is played (on a log scale, below about 2 kHz, where lines must keep
    minLineSpacingHz apart and so are few). */
enum class LowEnd
{
    stripes,  // separate lines: an analyser with a long FFT shows them as stripes with dark gaps
              // (the only look in FreqArt 0.1 and 0.2, kept as an effect)
    smooth    // the rising comb (see ArtPlan::combLines): shapes stay filled at every FFT size
};

struct PlanSettings
{
    FrequencyScale scale;
    int density = 256;              // lines across the full canvas height, 16..1024
    float contrastDb = 40.0f;       // dB between a value of 1 and a value just above 0
    float slopeDbPerOctave = 0.0f;  // tilt of the user's analyser, compensated (pivot 1 kHz)
    float minLineSpacingHz = 25.0f; // closer lines beat audibly and visibly: they are merged
    float maxFrequencyHz = 20000.0f;// lines above this are dropped (kept below Nyquist by the engine too)

    // Plain lines unless asked: the processor's "lowEnd" parameter (Smooth for new instances)
    // sets this.
    LowEnd lowEnd = LowEnd::stripes;
};

/** LowEnd::smooth, the rising comb.

    Why: in the merged lows the lines stand 25 Hz apart, which an analyser with a long FFT
    resolves into stripes with dark gaps; closer lines would beat. Instead, the lows are played
    by a comb of sines combSpacingHz apart that glide upwards together at combSlopeHzPerSecond,
    each wrapping round from the top of the comb to the bottom, where it is silent. Every height
    of the band is passed by a sine every spacing / slope seconds (0.1 s), so analyser windows of
    every usual length (4096 to 32768 samples at 48 kHz) find energy at every height: a short
    window blurs neighbouring sines together, a long one sees each sine smeared along its glide.
    As every sine glides at the same speed, they never cross or crowd, and their unrelated
    starting phases keep the passes from adding up in a pattern that repeats.

    Each gliding sine takes its loudness from the drawing at the height it has reached: the comb's
    lines in the plan are a grid of that loudness, finer than the sines (so edges stay where they
    were drawn), read with linear interpolation between grid lines and between columns. */
constexpr float combSpacingHz = 20.0f;
constexpr float combSlopeHzPerSecond = 200.0f;
constexpr float combGridSpacingHz = 5.0f;
constexpr float combEdgeFadeHz = 15.0f;   // the glide runs this far past each end of the grid, fading in and out
constexpr float combTopLimitHz = 2000.0f; // the comb never reaches above this: the highs keep their lines
constexpr int combMaxSines = 255;

// The edges of what is drawn. A sine gliding into a shape would turn on as fast as it crosses one
// grid cell (25 ms), and so quick an onset splashes energy tens of Hz below the shape (a faint
// curtain under it, a haze inside holes such as an O's); leaving it at the top does the same
// above. So, in each column, the outermost grid line of a run of drawn ones plays at
// combEdgeTaper[0] of its level and the next one at combEdgeTaper[1]: a raised cosine over three
// cells, inside the shape, along which the sines fade in and out over 75 ms. Runs shorter than
// combEdgeTaperMinCells (25 Hz) keep their level, so thin strokes stay as bright; the ends of the
// grid fade over combEdgeFadeHz beyond it instead. Measured with FreqArtLab against hard edges:
// 1.3 dB less leakage in the lows (1.2 dB for text), fidelity +0.01 (text +0.03), fill the same.
constexpr float combEdgeTaper[] { 0.5f, 0.933f };
constexpr int combEdgeTaperMinCells = 5;

// A line's energy stays in one analyser bin at every FFT size; the comb's is spread along its
// glide, so the longer the analyser's window, the darker the comb looks next to a line of the same
// amplitude (about 3 dB per doubling). The comb is played this much louder (3.5 dB), which makes
// it as bright as the lines above it at an FFT size of 8192: about 3 dB brighter at 4096, 1 to 4
// dB darker at 16384 and 3 to 7 dB darker at 32768 (the deep lows darkest).
constexpr float combGain = 1.5f;

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

    // The rising comb (LowEnd::smooth; 0 lines otherwise). Lines [0, combLines) are not sines of
    // their own: they are the comb's loudness grid, frequencies[k] = combGridLowHz + k *
    // combGridHz, with amplitudes like any line's (so they show where the comb's energy goes).
    // The comb plays combSines sines over a glide from combGridLowHz - combEdgeHz to the last
    // grid line + combEdgeHz (length L): sine j at
    //     combGridLowHz - combEdgeHz + (j * L / combSines + combSlope * t) mod L
    // for t seconds since the engine's reset. Between grid lines it has the amplitude
    // interpolated between them; beyond the ends of the grid it fades linearly to silence at the
    // ends of the glide. The comb's lines are never in activeLines; combActive says in which
    // columns the comb can sound.
    int combLines = 0;
    float combGridLowHz = 0.0f;
    float combGridHz = 0.0f;
    float combEdgeHz = 0.0f;
    int combSines = 0;
    float combSlope = 0.0f;               // Hz per second
    std::vector<uint8_t> combActive;      // numColumns: a grid amplitude is non-zero at c or (c + 1) % numColumns

    float amplitude (int column, int line) const noexcept
    {
        return amplitudes[static_cast<size_t> (column) * static_cast<size_t> (numLines) + static_cast<size_t> (line)];
    }
};

/** Builds a plan from a canvas.

    Lines: density lines are spaced evenly in canvas height, line i (0 = bottom) at
    norm (i + 0.5) / density, frequency scale.toHz (norm). Going up from the bottom, a line closer
    than minLineSpacingHz to the last kept line is merged into it (it is not played; its rows go
    to the nearest kept line); lines below 10 Hz or above maxFrequencyHz are dropped. Lines
    closer than twice the minimum spacing to the line below are then moved by a fixed
    pseudo-random offset of up to a quarter of the minimum spacing either way (so crowded lines
    do not all beat in step), and their lineNorm follows.

    Values: a line's value at a column is the MAXIMUM of the canvas over the rows its span covers
    (so thin strokes never fall between lines), where a line's span runs halfway (in canvas
    height, from the final lineNorm values) to its kept neighbours: every row plays on its
    nearest line, and a row on the boundary on both. The lowest line's span starts at the bottom
    of its own cell and the highest line's ends at the top of the last cell merged into it, so
    rows of dropped lines (below 10 Hz, above maxFrequencyHz) play nowhere.

    Loudness: value v (0..1) becomes amplitude 10^((v - 1) * contrastDb / 20) for v > 0.001, else
    0; times the slope compensation 10^(-slope * log2 (f / 1000) / 20); then every amplitude is
    scaled by one factor so that the loudest column's RMS, sqrt (sum (a^2) / 2), is
    planTargetRms. A canvas with nothing on it gives a silent plan with the lines still laid out.

    LowEnd::smooth: the lowest lines, up to the last one that stands closer than twice the minimum
    spacing to the line below (that is, the merged and crowded lows) and below combTopLimitHz,
    are replaced by the rising comb's grid, evenly spaced over the same band of the canvas (from
    the bottom of the lowest line's span to the top of the last replaced line's span), about
    combGridSpacingHz apart. A grid line's value is the maximum of the canvas over its own cell,
    and its amplitude is played combGain louder, softened at the edges of runs of drawn grid lines
    (combEdgeTaper); the lines above keep their spans and values. In the normalisation the comb
    counts as its sines, each with the mean power of the grid lines it glides over. */
std::unique_ptr<ArtPlan> buildPlan (const Canvas& canvas, const PlanSettings& settings);

} // namespace freqart
