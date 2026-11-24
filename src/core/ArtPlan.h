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

namespace artfreq
{

/** RMS the loudest moment of every plan is normalised to: -12 dBFS before the output level. */
constexpr float planTargetRms = 0.25f;

/** How the crowded low end is played (on a log scale, below about 2 kHz, where lines must keep
    minLineSpacingHz apart and so are few). */
enum class LowEnd
{
    stripes,  // separate lines: an analyser with a long FFT shows them as stripes with dark gaps
              // (the only look in ArtFreq 0.1 and 0.2, kept as an effect)
    smooth    // the standing comb (see ArtPlan::combLines): shapes stay filled at every FFT size
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

    // How long one pass of the drawing takes, in seconds, and whether it leads back into its
    // start (Repeat): what the fades in time are worked out for (see fadeCycles). 0: not known,
    // every line changes from column to column as drawn.
    double passSeconds = 0.0;
    bool repeats = true;
};

/** LowEnd::smooth, the standing comb.

    Why: in the merged lows the lines stand 25 Hz apart, which an analyser with a long FFT
    resolves into stripes with dark gaps. Closer lines fill the gaps, but lines that close beat
    against each other, and with unrelated phases the beats add up to a band that pulses. So the
    lows are played by steady lines on a fixed grid (every multiple of combFineSpacingHz below
    combFineTopHz, every multiple of combCoarseSpacingHz above it), with starting phases that
    make each stretch of the grid one sine sweep per beat period: line k (its frequency over the
    spacing) starts at -pi k^2 / combPhaseLines. Seen through an analyser's window of any usual
    length, a band of such lines keeps an even level, as if a comb of sines glided up through it
    (which is what the lines add up to); ArtFreq 0.3 to 0.6 played that comb as gliding sines.

    Why not gliding sines any more: a gliding sine has to be turned on and off as it crosses a
    shape, and that switching sprays energy far outside the shape: a haze about 40 dB down under
    every low shape, streaks under its edges (the customer: "a drip from the image into the lower
    frequencies"). A steady line that is not inside the drawing is simply silent: nothing is
    played below the lowest drawn frequency or above the highest.

    The lines' phases only work as a pattern if every line keeps its place in it, so the engine
    starts each comb line at the phase it would have had if it had played since the engine's
    reset (ArtPlan::combPhases; see ArtEngine). */
constexpr float combFineSpacingHz = 5.0f;     // fills an analyser of 16384 points, and nearly one of 32768
constexpr float combCoarseSpacingHz = 10.0f;  // ... where a display's rows are taller than the lines are apart
constexpr float combFineTopHz = 500.0f;
constexpr float combTopLimitHz = 2000.0f;     // the comb never reaches above this: the highs keep their lines
constexpr float combPhaseLines = 2.618034f;   // lines the sweep passes in one beat period, the golden ratio
                                              // squared: the level stays even through windows of 4096
                                              // to 32768 points, and no two beats ever line up, so the
                                              // sum never peaks (whole numbers and halves, 2, 3.5, 4,
                                              // make it a train of clicks 5 to 10 dB taller; measured)

// Thin strokes. A few of the comb's lines on their own (a stroke a row or two tall, an outline,
// thin lettering) are too few for the sweep to even out. Two lines alone beat as one pulse, five
// or ten times a second (8 to 14 dB on an analyser of 4096 or 8192 points, a tremolo to the ear):
// where, in a column, drawn rows join two lines and no third, the louder of the two plays for
// both, steadily. Three lines play, the outer two at combThinEdge of their level (that shimmers by
// 4 dB at 4096 points and 3 dB at 8192; all three at full level, by 6 dB). The narrowest runs
// played as they are (four or five lines) shimmer by about 6 dB at 4096 points and 3 dB at 8192;
// from six lines on, as any shape does.
constexpr float combThinEdge = 0.5f;

// Nothing is played below this: lower lines are dropped (nothing so low is heard or shown).
constexpr float lowestPlayedHz = 10.0f;

// Lines closer than an analyser's bins add up in each bin, so the closer the comb's lines, the
// quieter each is played: combGain times the square root of (its spacing / combGainSpacingHz).
// That is the loudness the gliding comb had (sines 20 Hz apart, 3.5 dB louder than a line): as
// bright as the lines above it at an FFT size of 8192, about 3 dB brighter at 4096, about 3 dB
// darker (but filled) at 16384 and 32768.
constexpr float combGain = 1.5f;
constexpr float combGainSpacingHz = 20.0f;

/** Fades in time. A sine turned on or off between two columns (a few ms) is, for that moment, a
    click: its energy spreads over about as many Hz as one over the time it took, and a little of
    it much further. At 100 Hz that is an octave of "legs" under every edge of a low shape; under
    text in the mids, faint streaks down to the bottom of an analyser's picture. So each line's
    loudness is smoothed along time with a raised cosine as long as fadeCycles of its own sine:
    the lower the line, the gentler it comes and goes, and its energy stays within about a sixth
    of its frequency (12 cycles, measured with ArtFreqLab --drip: 8 leave legs 15 dB stronger, 20
    gain little more and soften the letters of low text). No shorter than shortestFadeSeconds
    where the columns are short enough to shape it (shortestFadeColumns of them at most: a short
    drawing keeps its edges; 40 ms takes the streaks under mid text down by 4 dB below it and 11
    dB above, and is still shorter than any analyser's window), no longer than longestFadeSeconds
    and a sixteenth of the pass. With Repeat the smoothing runs round from the last column into
    the first; without, the pass is surrounded by silence and the lines also reach silence at its
    two ends. */
constexpr float fadeCycles = 12.0f;
constexpr float shortestFadeSeconds = 0.04f;
constexpr float shortestFadeColumns = 5.0f;
constexpr float longestFadeSeconds = 0.25f;
constexpr float longestFadeOfPass = 1.0f / 16.0f;

struct ArtPlan
{
    uint64_t version = 0;  // set by the publisher; strictly increasing per processor

    int numLines = 0;
    int numColumns = 0;   // always Canvas::width for a built plan

    std::vector<float> frequencies;  // numLines, Hz: ascending
    std::vector<float> lineNorm;     // numLines, height of each line on the scale (0 = bottom, 1 = top)

    // Linear peak amplitude of each line's sine at each column, column-major:
    // amplitudes[column * numLines + line]. Includes contrast, slope compensation, the fades in
    // time and normalisation (the loudest column has an RMS of planTargetRms when all its sines
    // play).
    std::vector<float> amplitudes;

    // For each column c, the lines whose amplitude is non-zero at column c OR at column
    // (c + 1) % numColumns (the two ends of the interpolation from c to c + 1):
    // activeLines[activeStart[c] .. activeStart[c + 1]).
    std::vector<uint32_t> activeStart;  // numColumns + 1
    std::vector<uint16_t> activeLines;

    uint64_t layoutHash = 0;  // hash of `frequencies` (and the comb's size); equal hashes mean the same lines
    bool silent = true;       // every amplitude is 0

    // The standing comb (LowEnd::smooth; 0 lines otherwise): lines [0, combLines) are its lines,
    // sines like any other but for their phases: line l is played as
    //     sin (2 pi frequencies[l] t + combPhases[l])
    // for t seconds since the engine's reset, whenever it starts to sound.
    int combLines = 0;
    std::vector<float> combPhases;   // combLines, radians

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

    The lows: the lowest lines, up to the last one that stands closer than twice the minimum
    spacing to the line below (that is, the merged and crowded lows) and below combTopLimitHz.
    LowEnd::smooth replaces them by the standing comb's lines over the same band of the canvas
    (from the bottom of the lowest line's span to the top of the last replaced line's span); the
    lines above keep their frequencies and spans.

    Values: a line's value at a column is the MAXIMUM of the canvas over the rows its span covers
    (so thin strokes never fall between lines), where a line's span runs halfway (in canvas
    height, from the final lineNorm values) to its neighbours: every row plays on its nearest
    line, and a row on the boundary on both. The lowest line's span starts at the bottom of its
    own cell and the highest line's ends at the top of the last cell merged into it, so rows of
    dropped lines (below 10 Hz, above maxFrequencyHz) play nowhere.

    Never below the drawing: in the lows (both ways of playing them), where lines are far apart
    for their height, the nearest line may lie well below or above what is drawn (a shape whose
    lowest edge is at 60 Hz would also sound at 47 Hz). So there a line sounds only if the
    drawing covers its own frequency: in each column, each run of drawn rows is played by the
    lines whose own row lies inside it (each at the maximum over the part of the run in its
    span). A run with no line inside it (a thin stroke between two lines) is played by the line
    nearest its middle, at the run's maximum, so that nothing drawn disappears.

    Loudness: value v (0..1) becomes amplitude 10^((v - 1) * contrastDb / 20) for v > 0.001, else
    0; times the slope compensation 10^(-slope * log2 (f / 1000) / 20), and for the comb's lines
    their gain (see combGain); then the fades in time (see fadeCycles; none unless
    settings.passSeconds is given); then every amplitude is scaled by one factor so that the
    loudest column's RMS, sqrt (sum (a^2) / 2), is planTargetRms. A canvas with nothing on it
    gives a silent plan with the lines still laid out. */
std::unique_ptr<ArtPlan> buildPlan (const Canvas& canvas, const PlanSettings& settings);

} // namespace artfreq
