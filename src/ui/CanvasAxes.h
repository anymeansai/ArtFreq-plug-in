#pragma once

// The frequency and time axes shared by the canvas and the live view, so both read alike.

#include "core/FrequencyScale.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace artfreq::ui::axes
{

/** Space around the drawing inside the canvas and the live view, besides the axes: room for the
    top frequency label, and a little air on the right. */
inline constexpr int topMargin = 10;
inline constexpr int rightMargin = 12;

/** The plotting area of a view with a frequency axis `axisWidth` wide on the left and a time
    axis `axisHeight` tall at the bottom. */
inline juce::Rectangle<int> plotArea (juce::Rectangle<int> bounds, int axisWidth, int axisHeight) noexcept
{
    return bounds.withTrimmedLeft (axisWidth).withTrimmedBottom (axisHeight)
                 .withTrimmedTop (topMargin).withTrimmedRight (rightMargin);
}

/** A compact Hz label for a narrow axis: "20", "500", "1k", "2.5k", "20k". */
juce::String compactHz (double hz);

/** Height of a frequency on `area` (y grows downwards; area's top is scale.maxHz). */
float yForHz (double hz, const FrequencyScale& scale, juce::Rectangle<float> area) noexcept;

/** Faint horizontal lines at the scale's grid frequencies. */
void drawFrequencyGrid (juce::Graphics&, juce::Rectangle<float> area, const FrequencyScale&, juce::Colour colour);

/** The grid frequencies' labels, right-aligned in `axis` (the strip left of `area`), centred on
    their lines; labels that would overlap the previous one or leave the strip are skipped. */
void drawFrequencyLabels (juce::Graphics&, juce::Rectangle<float> axis, juce::Rectangle<float> area,
                          const FrequencyScale&);

/** A vertical line on the time axis. */
struct TimeMark
{
    double fraction = 0.0;  // 0..1 across the drawing
    bool major = false;     // bar (or beat, under a bar)
    juce::String label;     // empty: no label
};

/** Bar and beat lines for a length choice (see params::lengthNames) in a time signature (as the
    host gives it; anything not above 0 reads as 4/4), thinned for the width: bars labelled "1",
    "2", ... (every 2nd/4th/... when crowded), the signature's beats (numerator of them, each a
    1/denominator note) as "1.2", "1.3", ... when there is room; under a bar, quarter-note beats
    and sixteenths instead. A length that is not a whole number of bars ends on a short bar.

    With a `firstBar` (above 0) the drawing sits at that bar of the song, and the labels are the
    song's bars: firstBar, firstBar + 1, ..., with beats as "12.2"; under a bar, the quarter-note
    beats are labelled as the beats of bar firstBar they fall on ("12", "12.2", ...). */
std::vector<TimeMark> timeMarks (int lengthIndex, int numerator, int denominator, float width, int firstBar = 0);

/** The marks as faint vertical lines over `area`. */
void drawTimeGrid (juce::Graphics&, juce::Rectangle<float> area, const std::vector<TimeMark>&,
                   juce::Colour majorColour, juce::Colour minorColour);

/** The marks' labels and ticks in `axis` (the strip under `area`). */
void drawTimeLabels (juce::Graphics&, juce::Rectangle<float> axis, juce::Rectangle<float> area,
                     const std::vector<TimeMark>&);

/** The font of axis labels (DM Mono, small). */
juce::Font labelFont();

/** The colour of axis labels. */
juce::Colour labelColour();

} // namespace artfreq::ui::axes
