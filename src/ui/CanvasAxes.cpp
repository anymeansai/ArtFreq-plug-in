#include "CanvasAxes.h"

#include "Parameters.h"
#include "Theme.h"

#include <cmath>
#include <limits>

namespace freqart::ui::axes
{
namespace
{
    constexpr float labelHeight = 10.5f;      // logical px (still legible at 75 %)
    constexpr float labelGap = 6.0f;          // between a label and the drawing
    constexpr float minBarLabelSpacing = 30.0f;
    constexpr float minSubLabelSpacing = 44.0f;
    constexpr float minSubLineSpacing = 8.0f;
    constexpr float tickLength = 3.0f;

    /** The grid frequencies for a scale whose ends came through a float parameter (20 kHz reads
        back as 19999.998 Hz): the ends are rounded to 0.01 Hz first, so the round frequency at
        an edge keeps its line and label. */
    std::vector<double> gridFor (const FrequencyScale& scale)
    {
        auto rounded = scale;
        rounded.minHz = std::round (scale.minHz * 100.0) / 100.0;
        rounded.maxHz = std::round (scale.maxHz * 100.0) / 100.0;
        return gridFrequencies (rounded);
    }
} // namespace

juce::Font labelFont()
{
    return fonts::mono (labelHeight);
}

juce::Colour labelColour()
{
    // Between textFaint and textDim: read at a glance without competing with the drawing.
    return theme::textFaint.interpolatedWith (theme::textDim, 0.45f);
}

juce::String compactHz (double hz)
{
    if (! std::isfinite (hz) || hz <= 0.0)
        return "0";

    if (std::round (hz) < 1000.0)
        return juce::String (juce::roundToInt (hz));

    // One decimal at most, dropped when it is zero: "1k", "2.5k", "16k".
    const auto tenths = juce::roundToInt (hz / 100.0);
    return tenths % 10 == 0 ? juce::String (tenths / 10) + "k"
                            : juce::String (tenths / 10) + "." + juce::String (tenths % 10) + "k";
}

float yForHz (double hz, const FrequencyScale& scale, juce::Rectangle<float> area) noexcept
{
    const auto norm = scale.toNorm (hz);
    return area.getY() + static_cast<float> (1.0 - norm) * area.getHeight();
}

void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> area, const FrequencyScale& scale, juce::Colour colour)
{
    g.setColour (colour);

    for (const auto hz : gridFor (scale))
    {
        // Snapped to whole pixels so the lines stay crisp and evenly faint.
        const auto y = std::floor (yForHz (hz, scale, area));

        if (y > area.getY() && y < area.getBottom() - 1.0f)
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
    }
}

void drawFrequencyLabels (juce::Graphics& g, juce::Rectangle<float> axis, juce::Rectangle<float> area,
                          const FrequencyScale& scale)
{
    g.setFont (labelFont());

    auto lastTop = std::numeric_limits<float>::max();  // labels go from the bottom up
    const auto grid = gridFor (scale);

    for (auto it = grid.begin(); it != grid.end(); ++it)
    {
        const auto y = yForHz (*it, scale, area);
        const auto box = juce::Rectangle<float> (axis.getX(), y - labelHeight * 0.5f - 1.0f,
                                                 axis.getWidth() - labelGap, labelHeight + 2.0f);

        // Labels may reach a little past the drawing (into its top margin and the time axis
        // strip) so the edge frequencies, 20 Hz and 20 kHz, are labelled too.
        if (box.getBottom() > lastTop - 2.0f || box.getY() < axis.getY() - static_cast<float> (topMargin)
            || box.getCentreY() > axis.getBottom() + 0.5f || box.getCentreY() < axis.getY() - 0.5f)
            continue;

        g.setColour (labelColour());
        g.drawText (compactHz (*it), box, juce::Justification::centredRight, false);

        g.setColour (theme::line);
        g.fillRect (axis.getRight() - tickLength, std::floor (y), tickLength, 1.0f);
        lastTop = box.getY();
    }

}

std::vector<TimeMark> timeMarks (int lengthIndex, float width)
{
    std::vector<TimeMark> marks;

    const auto quarters = params::lengthInQuarterNotes (lengthIndex, 4.0);

    if (! (quarters > 0.0) || ! (width > 0.0f))
        return marks;

    // A bar of 4/4 and its beats, or (under a bar) a beat and its sixteenths.
    const auto underABar = quarters < 4.0;
    const auto major = underABar ? 1.0 : 4.0;
    const auto minor = underABar ? 0.25 : 1.0;
    const auto numMajor = juce::jmax (1, juce::roundToInt (quarters / major));
    const auto minorPerMajor = juce::roundToInt (major / minor);
    const auto majorSpacing = width / static_cast<float> (numMajor);
    const auto minorSpacing = majorSpacing / static_cast<float> (minorPerMajor);

    // Label every bar, or every 2nd, 4th, ... bar when they would crowd.
    auto labelEvery = 1;
    while (static_cast<float> (labelEvery) * majorSpacing < minBarLabelSpacing && labelEvery < numMajor)
        labelEvery *= 2;

    const auto showMinor = minorSpacing >= minSubLineSpacing;
    const auto labelMinor = numMajor <= 2 && minorSpacing >= minSubLabelSpacing;

    for (int m = 0; m < numMajor; ++m)
    {
        // When bars are thinned out, only the labelled ones get the stronger line.
        TimeMark mark;
        mark.fraction = static_cast<double> (m) / static_cast<double> (numMajor);
        mark.major = m % labelEvery == 0;

        if (mark.major)
            mark.label = juce::String (m + 1);

        marks.push_back (mark);

        if (! showMinor)
            continue;

        for (int k = 1; k < minorPerMajor; ++k)
        {
            TimeMark sub;
            sub.fraction = (static_cast<double> (m) + static_cast<double> (k) / static_cast<double> (minorPerMajor))
                         / static_cast<double> (numMajor);

            if (labelMinor)
                sub.label = juce::String (m + 1) + "." + juce::String (k + 1);

            marks.push_back (sub);
        }
    }

    return marks;
}

void drawTimeGrid (juce::Graphics& g, juce::Rectangle<float> area, const std::vector<TimeMark>& marks,
                   juce::Colour majorColour, juce::Colour minorColour)
{
    for (const auto& mark : marks)
    {
        if (mark.fraction <= 0.0)
            continue;  // the start is the drawing's edge

        const auto x = std::floor (area.getX() + static_cast<float> (mark.fraction) * area.getWidth());
        g.setColour (mark.major ? majorColour : minorColour);
        g.fillRect (x, area.getY(), 1.0f, area.getHeight());
    }
}

void drawTimeLabels (juce::Graphics& g, juce::Rectangle<float> axis, juce::Rectangle<float> area,
                     const std::vector<TimeMark>& marks)
{
    g.setFont (labelFont());

    for (const auto& mark : marks)
    {
        const auto x = std::floor (area.getX() + static_cast<float> (mark.fraction) * area.getWidth());

        g.setColour (theme::line);
        g.fillRect (x, axis.getY(), 1.0f, mark.major ? tickLength + 2.0f : tickLength);

        if (mark.label.isEmpty())
            continue;

        const auto box = juce::Rectangle<float> (x + 4.0f, axis.getY() + 2.0f, 48.0f, axis.getHeight() - 4.0f);

        if (box.getX() + juce::GlyphArrangement::getStringWidth (labelFont(), mark.label) > area.getRight())
            continue;

        g.setColour (mark.major ? labelColour() : theme::textFaint);
        g.drawText (mark.label, box, juce::Justification::centredLeft, false);
    }
}

} // namespace freqart::ui::axes
