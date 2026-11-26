#include "Widgets.h"

#include <cmath>
#include <vector>

namespace artfreq::ui
{
namespace
{
    constexpr float iconStroke = 2.0f;   // in the 24 box: 1.33 px at 16 px, 1.5 px at 18 px

    /** An outline icon: the SVG path stroked with round caps and joins, ready to fill. */
    juce::Path stroked (const char* svg, float thickness = iconStroke)
    {
        juce::Path result;
        juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (result, juce::Drawable::parseSVGPath (svg));
        return result;
    }

    /** A solid icon whose corners are softened by a thin stroke around it. */
    juce::Path solid (const char* svg, float cornerStroke = 2.0f)
    {
        auto shape = juce::Drawable::parseSVGPath (svg);
        auto result = shape;
        juce::Path outline;
        juce::PathStrokeType (cornerStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (outline, shape);
        result.addPath (outline);
        result.setUsingNonZeroWinding (true);
        return result;
    }

    juce::Font segmentFont (float size, bool selected)
    {
        return fonts::ui (size, selected ? fonts::UiWeight::semiBold : fonts::UiWeight::medium);
    }

    float stringWidth (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }

    constexpr float minSegmentPadding = 12.0f;   // text to segment edge, both sides together

    /** The type size for a row of segments: the control size, stepping down (to 11 px) when long
        labels would otherwise touch their neighbours. */
    float segmentTextSize (const juce::StringArray& options, float width, int height)
    {
        const auto preferred = height >= 26 ? theme::controlSize : 12.0f;

        for (auto size = preferred; size > 11.0f; size -= 1.0f)
        {
            auto total = 0.0f;

            for (const auto& option : options)
                total += stringWidth (segmentFont (size, true), option) + minSegmentPadding;

            if (total <= width)
                return size;
        }

        return 11.0f;
    }

    /** Whether the labels have their padding at `size`. */
    bool segmentsFit (const juce::StringArray& options, float width, float size)
    {
        auto total = 0.0f;

        for (const auto& option : options)
            total += stringWidth (segmentFont (size, true), option) + minSegmentPadding;

        return total <= width;
    }
} // namespace

//==============================================================================
SegmentedControl::SegmentedControl (juce::StringArray initialOptions)
{
    setOptions (initialOptions);
    setWantsKeyboardFocus (false);
}

void SegmentedControl::setOptions (const juce::StringArray& newOptions)
{
    options = newOptions;

    if (selected >= options.size())
        selected = -1;

    hovered = -1;
    repaint();
}

void SegmentedControl::setOptionLabel (int index, const juce::String& label)
{
    if (juce::isPositiveAndBelow (index, options.size()) && options[index] != label)
    {
        options.set (index, label);
        repaint();
    }
}

void SegmentedControl::setShortLabels (const juce::StringArray& labels)
{
    shortLabels = labels;
    repaint();
}

const juce::StringArray& SegmentedControl::getShownLabels() const
{
    const auto width = static_cast<float> (getWidth()) - 4.0f;
    return shortLabels.size() == options.size() && ! segmentsFit (options, width, 11.0f) ? shortLabels : options;
}

bool SegmentedControl::labelsFit() const
{
    const auto width = static_cast<float> (getWidth()) - 4.0f;
    const auto& labels = getShownLabels();
    return segmentsFit (labels, width, segmentTextSize (labels, width, getHeight()));
}

void SegmentedControl::setSelectedIndex (int index, juce::NotificationType notification)
{
    if (! juce::isPositiveAndBelow (index, options.size()))
        index = -1;

    if (index == selected)
        return;

    selected = index;
    repaint();

    if (notification != juce::dontSendNotification && onChange != nullptr)
        onChange (selected);
}

namespace
{
    /** Segment rectangles: each wide enough for its text, the spare width shared out evenly, so
        "Transport | MIDI" does not squeeze "Transport" while short labels stay even. */
    std::vector<juce::Rectangle<float>> segmentRects (const juce::StringArray& options, juce::Rectangle<float> area, int height)
    {
        std::vector<juce::Rectangle<float>> rects;
        const auto count = options.size();

        if (count == 0)
            return rects;

        const auto font = segmentFont (segmentTextSize (options, area.getWidth(), height), true);
        std::vector<float> widths;
        auto total = 0.0f;

        for (const auto& option : options)
        {
            widths.push_back (stringWidth (font, option));
            total += widths.back();
        }

        const auto spare = area.getWidth() - total;
        auto x = area.getX();

        for (int i = 0; i < count; ++i)
        {
            const auto w = spare > 0.0f ? widths[static_cast<size_t> (i)] + spare / static_cast<float> (count)
                                        : area.getWidth() / static_cast<float> (count);
            const auto right = i == count - 1 ? area.getRight() : std::round (x + w);
            rects.push_back ({ x, area.getY(), right - x, area.getHeight() });
            x = right;
        }

        return rects;
    }
} // namespace

int SegmentedControl::indexAt (juce::Point<float> position) const
{
    const auto rects = segmentRects (getShownLabels(), getLocalBounds().toFloat().reduced (2.0f), getHeight());

    for (size_t i = 0; i < rects.size(); ++i)
        if (position.x >= rects[i].getX() && position.x < rects[i].getRight())
            return static_cast<int> (i);

    return -1;
}

void SegmentedControl::paint (juce::Graphics& g)
{
    const auto enabled = isEnabled();
    const auto outer = getLocalBounds().toFloat();

    g.setColour (theme::raised);
    g.fillRoundedRectangle (outer, theme::controlCorner);
    g.setColour (theme::line);
    g.drawRoundedRectangle (outer.reduced (0.5f), theme::controlCorner, 1.0f);

    const auto& labels = getShownLabels();
    const auto rects = segmentRects (labels, outer.reduced (2.0f), getHeight());
    const auto textSize = segmentTextSize (labels, outer.getWidth() - 4.0f, getHeight());
    const auto innerCorner = theme::controlCorner - 2.0f;

    for (size_t i = 0; i < rects.size(); ++i)
    {
        const auto index = static_cast<int> (i);
        const auto isSelected = index == selected;
        const auto isHovered = enabled && index == hovered && ! isSelected;
        const auto r = rects[i];

        if (isSelected)
        {
            const auto colour = enabled ? theme::accent : theme::textFaint;
            g.setColour (colour.withAlpha (0.22f));
            g.fillRoundedRectangle (r, innerCorner);
            g.setColour (colour.withAlpha (0.75f));
            g.drawRoundedRectangle (r.reduced (0.5f), innerCorner, 1.0f);
        }
        else if (isHovered)
        {
            g.setColour (theme::raisedHover);
            g.fillRoundedRectangle (r, innerCorner);
        }

        // A short divider between two quiet neighbours.
        if (i + 1 < rects.size())
        {
            const auto next = index + 1;
            const auto quiet = [&] (int k) { return k != selected && ! (enabled && k == hovered); };

            if (quiet (index) && quiet (next))
            {
                g.setColour (theme::line);
                g.fillRect (juce::Rectangle<float> (std::round (r.getRight()) - 0.5f, r.getY() + r.getHeight() * 0.28f,
                                                    1.0f, r.getHeight() * 0.44f));
            }
        }

        g.setFont (segmentFont (textSize, isSelected));
        g.setColour (! enabled ? theme::textFaint : isSelected || isHovered ? theme::text : theme::textDim);
        g.drawText (labels[index], r, juce::Justification::centred, true);
    }
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.position);

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isPopupMenu())
        return;

    const auto index = indexAt (e.position);

    if (index >= 0)
        setSelectedIndex (index, juce::sendNotificationSync);
}

//==============================================================================
namespace icons
{
    juce::Path select()
    {
        // A pointer, tip at the top left.
        return stroked ("M6 3.5 L6 18.5 L10.1 14.6 L12.9 20.6 L15.6 19.4 L12.9 13.5 L18.5 13.5 Z");
    }

    juce::Path brush()
    {
        // A round brush: the handle at 45 degrees and a bristle tip with a curl.
        auto path = stroked ("M20.5 3.5 L11.5 12.5");
        path.addPath (stroked ("M10.5 13.5 C8 13.5 6.5 15.2 6.5 17.3 C6.5 19.2 5.2 20.1 3.5 20.5 "
                               "C4.6 21.2 6 21.5 7.5 21.5 C10.5 21.5 12.5 19.5 12.5 17 C12.5 15 11.8 13.5 10.5 13.5 Z"));
        path.addPath (stroked ("M9.5 11.5 L13.5 15.5"));
        return path;
    }

    juce::Path eraser()
    {
        // An eraser block at 45 degrees, its rubber end split off, on a base line.
        return stroked ("M8.5 20 L4.3 15.8 A1.8 1.8 0 0 1 4.3 13.2 L13.2 4.3 A1.8 1.8 0 0 1 15.8 4.3 "
                        "L20.7 9.2 A1.8 1.8 0 0 1 20.7 11.8 L12.5 20 Z "
                        "M8.8 8.8 L16.2 16.2 M12.5 20 L20.5 20");
    }

    juce::Path text()
    {
        return stroked ("M5 7 L5 4.5 L19 4.5 L19 7 M12 4.5 L12 19.5 M9 19.5 L15 19.5");
    }

    juce::Path image()
    {
        return stroked ("M6 4 L18 4 A2 2 0 0 1 20 6 L20 18 A2 2 0 0 1 18 20 L6 20 A2 2 0 0 1 4 18 L4 6 A2 2 0 0 1 6 4 Z "
                        "M4.5 17 L9.5 12 L13.5 16 L15.5 14 L19.5 18 "
                        "M17 8.5 A1.5 1.5 0 1 1 14 8.5 A1.5 1.5 0 1 1 17 8.5 Z");
    }

    juce::Path undo()
    {
        return stroked ("M9 13.5 L4.5 9 L9 4.5 M4.5 9 L14.5 9 A5.5 5.5 0 0 1 14.5 20 L11 20");
    }

    juce::Path redo()
    {
        return stroked ("M15 13.5 L19.5 9 L15 4.5 M19.5 9 L9.5 9 A5.5 5.5 0 0 0 9.5 20 L13 20");
    }

    juce::Path help()
    {
        auto path = stroked ("M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 Z "
                             "M9.3 9.3 A2.8 2.8 0 0 1 14.75 10 C14.75 11.9 12 12.4 12 14.3");
        path.addEllipse (10.85f, 16.4f, 2.3f, 2.3f);
        return path;
    }

    juce::Path gear()
    {
        // Eight teeth: the outline alternates between the tooth tips and the root circle,
        // with slanted flanks so the teeth read as a cog at small sizes.
        constexpr int teeth = 8;
        constexpr float outer = 9.6f, inner = 7.0f;
        constexpr float tipHalf = 0.17f, rootHalf = 0.28f;   // radians
        const auto step = juce::MathConstants<float>::twoPi / static_cast<float> (teeth);
        const juce::Point<float> c (12.0f, 12.0f);

        juce::Path outline;

        for (int i = 0; i < teeth; ++i)
        {
            const auto a = static_cast<float> (i) * step;
            const auto p = [&] (float angle, float radius) { return c.getPointOnCircumference (radius, angle); };

            if (i == 0)
                outline.startNewSubPath (p (a - rootHalf, inner));
            else
                outline.lineTo (p (a - rootHalf, inner));

            outline.lineTo (p (a - tipHalf, outer));
            outline.lineTo (p (a + tipHalf, outer));
            outline.lineTo (p (a + rootHalf, inner));
            outline.addCentredArc (c.x, c.y, inner, inner, 0.0f, a + rootHalf, a + step - rootHalf);
        }

        outline.closeSubPath();
        outline.addEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);

        juce::Path result;
        juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
            .createStrokedPath (result, outline);
        return result;
    }

    juce::Path trash()
    {
        return stroked ("M4 6.5 L20 6.5 M9 6.5 L9 4.5 A1 1 0 0 1 10 3.5 L14 3.5 A1 1 0 0 1 15 4.5 L15 6.5 "
                        "M6 6.5 L7 19 A2 2 0 0 0 9 21 L15 21 A2 2 0 0 0 17 19 L18 6.5 "
                        "M10 10.5 L10 17 M14 10.5 L14 17");
    }

    juce::Path play()
    {
        return solid ("M8 5.2 L8 18.8 L19 12 Z");
    }

    juce::Path stop()
    {
        return solid ("M7 7 L17 7 L17 17 L7 17 Z", 2.5f);
    }

    juce::Path close()
    {
        return stroked ("M6.5 6.5 L17.5 17.5 M17.5 6.5 L6.5 17.5");
    }

    juce::Path download()
    {
        return stroked ("M12 4 L12 15 M7.5 10.5 L12 15 L16.5 10.5 M5 19.5 L19 19.5");
    }

    juce::Path clear()
    {
        return stroked ("M6 4 L18 4 A2 2 0 0 1 20 6 L20 18 A2 2 0 0 1 18 20 L6 20 A2 2 0 0 1 4 18 L4 6 A2 2 0 0 1 6 4 Z "
                        "M9.25 9.25 L14.75 14.75 M14.75 9.25 L9.25 14.75");
    }

    // An almond with its pupil: shown. The same, struck through: hidden.
    constexpr const char* eyeShape = "M2.5 12 C4.8 7.6 8.2 5.5 12 5.5 C15.8 5.5 19.2 7.6 21.5 12 "
                                     "C19.2 16.4 15.8 18.5 12 18.5 C8.2 18.5 4.8 16.4 2.5 12 Z "
                                     "M15 12 A3 3 0 1 1 9 12 A3 3 0 1 1 15 12 Z";

    juce::Path eye()
    {
        return stroked (eyeShape);
    }

    juce::Path eyeOff()
    {
        auto path = stroked (eyeShape);
        path.addPath (stroked ("M4 4 L20 20"));
        return path;
    }

    juce::Path chevronLeft()
    {
        return stroked ("M14.5 6 L8.5 12 L14.5 18");
    }

    juce::Path chevronRight()
    {
        return stroked ("M9.5 6 L15.5 12 L9.5 18");
    }

    juce::Path layers()
    {
        // Three sheets, one over the other.
        return stroked ("M12 3.5 L21 8.5 L12 13.5 L3 8.5 Z M3.5 12.5 L12 17.2 L20.5 12.5 M3.5 16.3 L12 21 L20.5 16.3");
    }
} // namespace icons

//==============================================================================
IconButton::IconButton (const juce::String& name, juce::Path iconPath, const juce::String& labelText)
    : juce::Button (name), icon (std::move (iconPath)), label (labelText)
{
    setTitle (name);
}

void IconButton::setIcon (juce::Path newIcon)
{
    icon = std::move (newIcon);
    repaint();
}

void IconButton::setLabel (const juce::String& newLabel)
{
    if (label != newLabel)
    {
        label = newLabel;
        repaint();
    }
}

void IconButton::setOnColour (juce::Colour colour)
{
    onColour = colour;
    repaint();
}

void IconButton::setFlat (bool shouldBeFlat)
{
    if (flat != shouldBeFlat)
    {
        flat = shouldBeFlat;
        repaint();
    }
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto enabled = isEnabled();
    const auto on = getToggleState();
    const auto bounds = getLocalBounds().toFloat();
    const auto corner = juce::jmin (theme::controlCorner, bounds.getHeight() * 0.5f);

    highlighted = highlighted && enabled;
    down = down && enabled;

    // Background and outline.
    if (on)
    {
        const auto colour = enabled ? onColour : theme::textFaint;
        g.setColour (colour.withAlpha (down ? 0.28f : highlighted ? 0.22f : 0.16f));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (colour.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
    }
    else if (down || highlighted || ! flat)
    {
        g.setColour (down ? theme::raised.darker (0.3f) : highlighted ? theme::raisedHover : theme::raised);
        g.fillRoundedRectangle (bounds, corner);

        if (! flat)
        {
            g.setColour (theme::line);
            g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
        }
    }

    const auto contentColour = ! enabled ? theme::textFaint
                             : on ? onColour.brighter (0.25f)
                             : (highlighted || down || ! flat) ? theme::text
                             : theme::textDim;

    const auto iconSize = getHeight() <= 28 ? 16 : 18;
    const auto font = fonts::ui (theme::controlSize, fonts::UiWeight::medium);

    if (label.isEmpty())
    {
        const auto box = juce::Rectangle<float> (static_cast<float> (iconSize), static_cast<float> (iconSize))
                             .withCentre (bounds.getCentre());
        const auto snapped = box.withPosition (std::round (box.getX()), std::round (box.getY()));
        g.setColour (contentColour);
        g.fillPath (icon, juce::AffineTransform::scale (static_cast<float> (iconSize) / 24.0f)
                              .translated (snapped.getX(), snapped.getY()));
        return;
    }

    // Icon then label, centred together.
    const auto labelWidth = std::ceil (juce::GlyphArrangement::getStringWidth (font, label));
    const auto total = static_cast<float> (iconSize) + 6.0f + labelWidth;
    const auto left = std::round ((bounds.getWidth() - total) * 0.5f);
    const auto iconTop = std::round ((bounds.getHeight() - static_cast<float> (iconSize)) * 0.5f);

    g.setColour (contentColour);
    g.fillPath (icon, juce::AffineTransform::scale (static_cast<float> (iconSize) / 24.0f).translated (left, iconTop));

    g.setFont (font);
    g.drawText (label, juce::Rectangle<float> (left + static_cast<float> (iconSize) + 6.0f, 0.0f,
                                               labelWidth + 2.0f, bounds.getHeight()),
                juce::Justification::centredLeft, false);
}

} // namespace artfreq::ui
