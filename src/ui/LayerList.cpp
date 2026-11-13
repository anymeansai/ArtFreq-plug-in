#include "LayerList.h"

#include "Actions.h"
#include "Theme.h"
#include "Widgets.h"

#include <cmath>

namespace freqart::ui
{
namespace
{
    // Logical pixels
    constexpr int inset = 4;               // the list's frame to its rows
    constexpr int separatorHeight = 9;     // between the objects and the paint layer: gap, line, gap
    constexpr int scrollBarWidth = 8;
    constexpr int settingsWidth = 22;      // the slots on the right of a row
    constexpr int eyeWidth = 26;
    constexpr int deleteWidth = 26;
    constexpr int clearWidth = 46;
    constexpr int iconSize = 16;
    constexpr int dragThreshold = 4;       // a press that moves less than this is a click
    constexpr int autoScrollMargin = 14;   // dragging a row this close to an edge scrolls
    constexpr int autoScrollStep = 6;
    constexpr int autoScrollInterval = 30; // ms
    constexpr float wheelPixels = 160.0f;  // per unit of wheel delta

    enum class Part
    {
        none,
        body,
        eye,
        remove,
        settings,
        paintBody,
        paintEye,
        paintClear
    };

    /** A path icon designed in the 24 box, drawn at `size` and snapped to whole pixels. */
    void drawIcon (juce::Graphics& g, const juce::Path& path, juce::Rectangle<int> slot, int size, juce::Colour colour)
    {
        const auto box = juce::Rectangle<int> (size, size).withCentre (slot.getCentre());
        g.setColour (colour);
        g.fillPath (path, juce::AffineTransform::scale (static_cast<float> (size) / 24.0f)
                              .translated (static_cast<float> (box.getX()), static_cast<float> (box.getY())));
    }

    /** The quiet background of a row's button under the mouse. */
    void drawButtonHover (juce::Graphics& g, juce::Rectangle<int> slot)
    {
        g.setColour (theme::raisedHover);
        g.fillRoundedRectangle (slot.toFloat().withSizeKeepingCentre (static_cast<float> (slot.getWidth()) - 2.0f, 22.0f), 4.0f);
    }
} // namespace

//==============================================================================
struct LayerList::Impl
{
    struct Row
    {
        juce::Uuid id;
        ArtObject::Type type = ArtObject::Type::text;
        juce::String name;
        bool visible = true;
    };

    struct Hit
    {
        int row = -1;   // an object row (0 = the front-most); -1 for the paint row or nothing
        Part part = Part::none;
    };

    /** A row's slots, from the left: type icon, name, and on the right delete (or Clear on the
        paint row), eye, settings chevron. The eye is in the same place on every row. */
    struct Parts
    {
        juce::Rectangle<int> icon, name, remove, eye, settings;
    };

    Impl (LayerList& ownerToUse, FreqArtProcessor& processorToUse, EditorState& stateToUse)
        : owner (ownerToUse), processor (processorToUse), state (stateToUse)
    {
    }

    // ---- geometry (the list's coordinates) -------------------------------------------------

    int numRows() const noexcept { return static_cast<int> (rows.size()); }
    int listedRows() const noexcept { return juce::jmax (1, numRows()); }   // an empty list shows a placeholder

    int contentHeight() const noexcept { return 2 * inset + listedRows() * rowHeight + separatorHeight + rowHeight; }
    bool scrolls() const { return contentHeight() > owner.getHeight(); }
    int maxScroll() const { return juce::jmax (0, contentHeight() - owner.getHeight()); }
    int rowsRight() const { return owner.getWidth() - inset - (scrolls() ? scrollBarWidth : 0); }

    juce::Rectangle<int> rowBounds (int row) const
    {
        return { inset, inset + row * rowHeight - scroll, rowsRight() - inset, rowHeight };
    }

    juce::Rectangle<int> paintRowBounds() const
    {
        return { inset, inset + listedRows() * rowHeight + separatorHeight - scroll, rowsRight() - inset, rowHeight };
    }

    static Parts partsOf (juce::Rectangle<int> r, bool paintRow)
    {
        Parts p;
        auto area = r;
        p.settings = area.removeFromRight (settingsWidth);
        p.eye = area.removeFromRight (eyeWidth);
        p.remove = area.removeFromRight (paintRow ? clearWidth : deleteWidth);
        area.removeFromLeft (8);
        p.icon = area.removeFromLeft (iconSize);
        area.removeFromLeft (8);
        p.name = area.withTrimmedRight (4);
        return p;
    }

    Hit hitTest (juce::Point<int> p) const
    {
        if (p.x < inset || p.x >= rowsRight() || p.y < 0 || p.y >= owner.getHeight())
            return {};

        if (const auto r = paintRowBounds(); r.contains (p))
        {
            const auto parts = partsOf (r, true);
            return { -1, parts.eye.contains (p) ? Part::paintEye : parts.remove.contains (p) ? Part::paintClear : Part::paintBody };
        }

        for (int i = 0; i < numRows(); ++i)
        {
            const auto r = rowBounds (i);

            if (! r.contains (p))
                continue;

            const auto parts = partsOf (r, false);
            return { i, parts.settings.contains (p) ? Part::settings
                      : parts.eye.contains (p)      ? Part::eye
                      : parts.remove.contains (p)   ? Part::remove
                                                    : Part::body };
        }

        return {};
    }

    int rowOf (const juce::Uuid& id) const
    {
        if (id.isNull())
            return -1;

        for (int i = 0; i < numRows(); ++i)
            if (rows[static_cast<size_t> (i)].id == id)
                return i;

        return -1;
    }

    /** Where a dragged row would go: the gap above row `gap` (numRows() = below the last). */
    int gapAt (int y) const
    {
        const auto contentY = static_cast<double> (y + scroll - inset);
        return juce::jlimit (0, numRows(), static_cast<int> (std::round (contentY / rowHeight)));
    }

    // ---- scrolling -------------------------------------------------------------------------

    void setScroll (int newScroll)
    {
        newScroll = juce::jlimit (0, maxScroll(), newScroll);

        if (newScroll != scroll)
        {
            scroll = newScroll;
            owner.repaint();
        }

        updateScrollBar();
    }

    void updateScrollBar()
    {
        scroll = juce::jlimit (0, maxScroll(), scroll);
        scrollBar.setVisible (scrolls());
        scrollBar.setRangeLimits (0.0, static_cast<double> (contentHeight()), juce::dontSendNotification);
        scrollBar.setCurrentRange (static_cast<double> (scroll), static_cast<double> (owner.getHeight()), juce::dontSendNotification);
    }

    void scrollToShow (int row)
    {
        if (row < 0 || owner.getHeight() <= 0)
            return;

        const auto top = row * rowHeight;                   // the row's place in the content, less the inset
        const auto bottom = top + rowHeight + 2 * inset;

        if (top < scroll)
            setScroll (top);
        else if (bottom > scroll + owner.getHeight())
            setScroll (bottom - owner.getHeight());
    }

    // ---- keeping in step with the document -------------------------------------------------

    void refresh()
    {
        const auto& document = processor.getDocument();
        std::vector<Row> newRows;
        newRows.reserve (document.objects.size());

        for (auto it = document.objects.rbegin(); it != document.objects.rend(); ++it)
            newRows.push_back ({ it->id, it->type, actions::layerName (*it), it->visible });

        rows = std::move (newRows);
        painted = document.hasPaint();
        paintShown = document.paintVisible;

        // A drag whose object has gone (undone, deleted from the canvas) ends.
        if (dragging && rowOf (dragId) < 0)
            endDrag();

        if (hover.row >= numRows())
            hover = {};

        updateScrollBar();

        // A new selection (from the canvas, the keyboard, a new object) scrolls into view.
        if (state.getSelected() != shownSelection)
        {
            shownSelection = state.getSelected();
            scrollToShow (rowOf (shownSelection));
        }

        owner.repaint();
    }

    // ---- actions ---------------------------------------------------------------------------

    void select (int row)
    {
        if (! juce::isPositiveAndBelow (row, numRows()))
            return;

        // The handles show only with the Select tool.
        state.setTool (Tool::select);
        state.selectInLayerList (rows[static_cast<size_t> (row)].id);
        scrollToShow (row);
    }

    void openSettings (int row)
    {
        if (! juce::isPositiveAndBelow (row, numRows()))
            return;

        state.setTool (Tool::select);
        state.setSelected (rows[static_cast<size_t> (row)].id);
    }

    /** Deletes a row's object; the keyboard's Delete then selects its neighbour, so a run of
        presses keeps clearing the list. */
    void deleteRow (int row, bool selectNeighbour)
    {
        if (! juce::isPositiveAndBelow (row, numRows()))
            return;

        const auto id = rows[static_cast<size_t> (row)].id;
        const auto neighbour = row + 1 < numRows() ? rows[static_cast<size_t> (row + 1)].id
                             : row > 0             ? rows[static_cast<size_t> (row - 1)].id
                                                   : juce::Uuid::null();

        actions::deleteObject (processor, state, id);

        if (selectNeighbour && ! neighbour.isNull())
            state.selectInLayerList (neighbour);
    }

    void endDrag()
    {
        dragging = false;
        dragId = juce::Uuid::null();
        owner.beginDragAutoRepeat (0);
        owner.repaint();
    }

    // ---- painting --------------------------------------------------------------------------

    void paint (juce::Graphics& g)
    {
        const auto bounds = owner.getLocalBounds().toFloat();
        const auto focused = owner.hasKeyboardFocus (false);

        // A well, darker than the panel, so the list reads as one box that scrolls.
        g.setColour (theme::bg);
        g.fillRoundedRectangle (bounds, theme::controlCorner);

        {
            juce::Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (owner.getLocalBounds().reduced (1));

            const auto lifted = dragging ? rowOf (dragId) : -1;

            for (int i = 0; i < numRows(); ++i)
                if (g.clipRegionIntersects (rowBounds (i)))
                    paintObjectRow (g, i, rowBounds (i), focused, i == lifted);

            if (rows.empty())
            {
                const auto parts = partsOf (rowBounds (0), false);
                g.setColour (theme::textFaint);
                g.setFont (fonts::ui (12.5f));
                g.drawText ("No text or pictures yet", parts.name.withRight (rowsRight() - 4), juce::Justification::centredLeft, true);
            }

            // The paint layer is underneath everything and never moves: a hairline sets it apart.
            const auto separatorY = inset + listedRows() * rowHeight - scroll + separatorHeight / 2;
            g.setColour (theme::line);
            g.fillRect (inset + 6, separatorY, rowsRight() - inset - 12, 1);

            paintPaintRow (g);

            if (dragging)
                paintDrag (g, focused);
        }

        g.setColour (focused ? theme::accent.withAlpha (0.45f) : theme::line);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::controlCorner, 1.0f);
    }

    void paintObjectRow (juce::Graphics& g, int index, juce::Rectangle<int> r, bool focused, bool lifted)
    {
        const auto& row = rows[static_cast<size_t> (index)];
        const auto selected = row.id == state.getSelected();
        const auto hovered = hover.row == index && ! dragging;
        const auto box = r.toFloat().reduced (0.0f, 1.0f);

        // While dragged, its place is held by a faint copy (the row itself follows the mouse).
        const juce::Graphics::ScopedSaveState saved (g);

        if (lifted)
            g.setOpacity (0.3f);

        if (selected)
        {
            g.setColour (theme::accent.withAlpha (0.16f));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (theme::accent.withAlpha (focused ? 0.9f : 0.55f));
            g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);
        }
        else if (hovered)
        {
            g.setColour (theme::raised);
            g.fillRoundedRectangle (box, 5.0f);
        }

        paintRowContent (g, row, r, selected, hovered);
    }

    void paintRowContent (juce::Graphics& g, const Row& row, juce::Rectangle<int> r, bool selected, bool hovered)
    {
        const auto parts = partsOf (r, false);
        const auto isText = row.type == ArtObject::Type::text;

        drawIcon (g, isText ? icons::text() : icons::image(), parts.icon, iconSize,
                  selected ? theme::accent.brighter (0.35f) : row.visible ? theme::textDim : theme::textFaint);

        g.setFont (fonts::ui (theme::controlSize, selected ? fonts::UiWeight::medium : fonts::UiWeight::regular));
        g.setColour (row.visible ? theme::text : theme::textFaint);
        g.drawText (row.name, parts.name, juce::Justification::centredLeft, true);

        const auto hot = [&] (Part part) { return hovered && hover.part == part; };

        // Delete only while the mouse is over the row: it is there when wanted, quiet otherwise.
        if (hovered)
        {
            if (hot (Part::remove))
                drawButtonHover (g, parts.remove);

            drawIcon (g, icons::trash(), parts.remove, iconSize, hot (Part::remove) ? theme::danger : theme::textDim);
        }

        // The eye: open and quiet when shown; struck through, and brighter, when hidden, so hidden
        // layers stand out in the list.
        if (hot (Part::eye))
            drawButtonHover (g, parts.eye);

        drawIcon (g, row.visible ? icons::eye() : icons::eyeOff(), parts.eye, iconSize,
                  hot (Part::eye) ? theme::text : row.visible ? (hovered || selected ? theme::textDim : theme::textFaint)
                                                              : theme::textDim);

        if (hot (Part::settings))
            drawButtonHover (g, parts.settings);

        drawIcon (g, icons::chevronRight(), parts.settings, 14,
                  hot (Part::settings) ? theme::text : hovered || selected ? theme::textDim : theme::textFaint);
    }

    void paintPaintRow (juce::Graphics& g)
    {
        const auto r = paintRowBounds();
        const auto parts = partsOf (r, true);
        const auto hovered = hover.row < 0 && hover.part != Part::none && ! dragging;
        const auto shows = painted && paintShown;

        drawIcon (g, icons::brush(), parts.icon, iconSize, shows ? theme::textDim : theme::textFaint);

        const auto nameFont = fonts::ui (theme::controlSize);
        g.setFont (nameFont);
        g.setColour (shows ? theme::text : theme::textFaint);
        g.drawText ("Paint", parts.name, juce::Justification::centredLeft, false);

        if (! painted)
        {
            const auto x = parts.name.getX() + static_cast<int> (std::ceil (juce::GlyphArrangement::getStringWidth (nameFont, "Paint"))) + 8;
            g.setFont (fonts::ui (11.5f));
            g.drawText ("empty", parts.name.withLeft (x), juce::Justification::centredLeft, true);
        }

        // Clear: a small text button, there only when there is paint to clear.
        if (painted)
        {
            const auto hot = hover.part == Part::paintClear;
            const auto button = parts.remove.toFloat().withSizeKeepingCentre (static_cast<float> (parts.remove.getWidth()) - 6.0f, 20.0f);
            g.setColour (hot ? theme::raisedHover : theme::raised);
            g.fillRoundedRectangle (button, 4.0f);
            g.setColour (theme::line);
            g.drawRoundedRectangle (button.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (hot ? theme::text : theme::textDim);
            g.setFont (fonts::ui (12.0f, fonts::UiWeight::medium));
            g.drawText ("Clear", button, juce::Justification::centred, false);
        }

        // The eye works once there is paint (or while the layer is hidden, to show it again).
        if (painted || ! paintShown)
        {
            const auto hot = hover.part == Part::paintEye;

            if (hot)
                drawButtonHover (g, parts.eye);

            drawIcon (g, paintShown ? icons::eye() : icons::eyeOff(), parts.eye, iconSize,
                      hot ? theme::text : ! paintShown ? theme::textDim : hovered ? theme::textDim : theme::textFaint);
        }
    }

    void paintDrag (juce::Graphics& g, bool focused)
    {
        const auto index = rowOf (dragId);

        if (index < 0)
            return;

        // Where it would land: a line in the gap, unless that is where it already is.
        const auto gap = gapAt (dragMouseY);

        if (gap != index && gap != index + 1)
        {
            const auto y = inset + gap * rowHeight - scroll;
            g.setColour (theme::accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (inset + 2), static_cast<float> (y) - 1.0f,
                                                            static_cast<float> (rowsRight() - inset - 4), 2.0f), 1.0f);
            g.fillEllipse (static_cast<float> (inset), static_cast<float> (y) - 3.0f, 6.0f, 6.0f);
        }

        // The row itself, lifted, under the mouse.
        auto r = rowBounds (index).withY (dragMouseY - dragGrabOffset);
        r.setY (juce::jlimit (0, juce::jmax (0, owner.getHeight() - rowHeight), r.getY()));
        const auto box = r.toFloat().reduced (0.0f, 1.0f);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (box.translated (0.0f, 2.0f), 5.0f);
        g.setColour (theme::raisedHover);
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (theme::accent.withAlpha (focused ? 0.9f : 0.7f));
        g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);
        paintRowContent (g, rows[static_cast<size_t> (index)], r, true, false);
    }

    // ---- state -----------------------------------------------------------------------------

    LayerList& owner;
    FreqArtProcessor& processor;
    EditorState& state;
    juce::ScrollBar scrollBar { true };

    std::vector<Row> rows;   // front first
    bool painted = false;
    bool paintShown = true;
    juce::Uuid shownSelection = juce::Uuid::null();

    int scroll = 0;
    Hit hover;

    // A press on a row, which becomes a drag once it has moved a little.
    int pressRow = -1;
    int pressY = 0;
    bool dragging = false;
    juce::Uuid dragId = juce::Uuid::null();
    int dragMouseY = 0;
    int dragGrabOffset = 0;
};

//==============================================================================
LayerList::LayerList (FreqArtProcessor& processor, EditorState& state)
    : impl (std::make_unique<Impl> (*this, processor, state))
{
    setWantsKeyboardFocus (true);
    setTitle ("Layers");
    setDescription ("Every text and picture, front first, and the paint layer. Up and Down move the selection.");

    impl->scrollBar.setAutoHide (false);
    impl->scrollBar.addListener (this);
    addChildComponent (impl->scrollBar);

    impl->refresh();
}

LayerList::~LayerList()
{
    impl->scrollBar.removeListener (this);
}

void LayerList::refresh()
{
    impl->refresh();
}

int LayerList::getContentHeight() const
{
    return impl->contentHeight();
}

int LayerList::getNumObjectRows() const noexcept
{
    return impl->numRows();
}

juce::Rectangle<int> LayerList::getRowBounds (int row) const      { return impl->rowBounds (row); }
juce::Rectangle<int> LayerList::getEyeBounds (int row) const      { return Impl::partsOf (impl->rowBounds (row), false).eye; }
juce::Rectangle<int> LayerList::getDeleteBounds (int row) const   { return Impl::partsOf (impl->rowBounds (row), false).remove; }
juce::Rectangle<int> LayerList::getSettingsBounds (int row) const { return Impl::partsOf (impl->rowBounds (row), false).settings; }
juce::Rectangle<int> LayerList::getPaintRowBounds() const         { return impl->paintRowBounds(); }
juce::Rectangle<int> LayerList::getPaintEyeBounds() const         { return Impl::partsOf (impl->paintRowBounds(), true).eye; }
juce::Rectangle<int> LayerList::getPaintClearBounds() const       { return Impl::partsOf (impl->paintRowBounds(), true).remove; }

void LayerList::paint (juce::Graphics& g)
{
    impl->paint (g);
}

void LayerList::resized()
{
    impl->scrollBar.setBounds (getWidth() - scrollBarWidth - 2, 3, scrollBarWidth, juce::jmax (0, getHeight() - 6));
    impl->updateScrollBar();
    impl->scrollToShow (impl->rowOf (impl->state.getSelected()));
}

//==============================================================================
void LayerList::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = impl->hitTest (e.getPosition());

    if (hit.row != impl->hover.row || hit.part != impl->hover.part)
    {
        impl->hover = hit;
        repaint();
    }
}

void LayerList::mouseExit (const juce::MouseEvent&)
{
    if (impl->hover.part != Part::none)
    {
        impl->hover = {};
        repaint();
    }
}

void LayerList::mouseDown (const juce::MouseEvent& e)
{
    auto& d = *impl;

    // (Only on screen: the snapshot tool sends mouse events to an editor that is not.)
    if (isShowing())
        grabKeyboardFocus();

    d.pressRow = -1;
    const auto hit = d.hitTest (e.getPosition());
    d.hover = hit;
    repaint();

    if (e.mods.isPopupMenu())
    {
        if (hit.row >= 0)
        {
            d.select (hit.row);
            actions::showObjectMenu (d.processor, d.state, d.rows[static_cast<size_t> (hit.row)].id, *this, e.getPosition());
        }

        return;
    }

    const auto& document = d.processor.getDocument();

    switch (hit.part)
    {
        case Part::eye:
        {
            const auto& row = d.rows[static_cast<size_t> (hit.row)];
            actions::setObjectVisible (d.processor, row.id, ! row.visible);
            break;
        }

        case Part::remove:     d.deleteRow (hit.row, false); break;
        case Part::settings:   d.openSettings (hit.row); break;
        case Part::paintEye:   if (document.hasPaint() || ! document.paintVisible) actions::setPaintVisible (d.processor, ! document.paintVisible); break;
        case Part::paintClear: actions::clearPaint (d.processor); break;

        case Part::body:
            if (e.getNumberOfClicks() >= 2)
            {
                d.openSettings (hit.row);
                break;
            }

            d.select (hit.row);
            d.pressRow = hit.row;
            d.pressY = e.getPosition().y;
            break;

        case Part::paintBody:
        case Part::none:
            break;
    }
}

void LayerList::mouseDrag (const juce::MouseEvent& e)
{
    auto& d = *impl;
    const auto y = e.getPosition().y;

    if (! d.dragging)
    {
        if (! juce::isPositiveAndBelow (d.pressRow, d.numRows()) || std::abs (y - d.pressY) < dragThreshold || d.numRows() < 2)
            return;

        d.dragging = true;
        d.dragId = d.rows[static_cast<size_t> (d.pressRow)].id;
        d.dragGrabOffset = d.pressY - d.rowBounds (d.pressRow).getY();
        d.hover = {};

        // Repeated drag events while the mouse is held still near an edge, to keep scrolling.
        beginDragAutoRepeat (autoScrollInterval);
    }

    d.dragMouseY = y;

    if (y < autoScrollMargin)
        d.setScroll (d.scroll - autoScrollStep);
    else if (y > getHeight() - autoScrollMargin)
        d.setScroll (d.scroll + autoScrollStep);

    repaint();
}

void LayerList::mouseUp (const juce::MouseEvent& e)
{
    auto& d = *impl;
    d.pressRow = -1;

    if (! d.dragging)
        return;

    const auto from = d.rowOf (d.dragId);
    const auto gap = d.gapAt (e.getPosition().y);
    const auto id = d.dragId;
    d.endDrag();

    if (from < 0)
        return;

    // The row's place once it has left its own: rows below the gap move up one.
    const auto to = gap > from ? gap - 1 : gap;

    if (to != from)
        actions::moveObject (d.processor, id, d.numRows() - 1 - to);

    d.hover = d.hitTest (e.getPosition());
}

void LayerList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! impl->scrolls())
    {
        juce::Component::mouseWheelMove (e, wheel);
        return;
    }

    impl->setScroll (impl->scroll - juce::roundToInt (wheel.deltaY * wheelPixels));
}

bool LayerList::keyPressed (const juce::KeyPress& key)
{
    auto& d = *impl;
    const auto mods = key.getModifiers();

    if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown())
        return false;   // shortcuts (undo, ...) belong to the editor

    const auto code = key.getKeyCode();
    const auto current = d.rowOf (d.state.getSelected());

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        if (d.numRows() == 0)
            return false;

        const auto down = code == juce::KeyPress::downKey;
        d.select (current < 0 ? (down ? 0 : d.numRows() - 1)
                              : juce::jlimit (0, d.numRows() - 1, current + (down ? 1 : -1)));
        return true;
    }

    if (current < 0)
        return false;

    if (code == juce::KeyPress::returnKey)
    {
        d.openSettings (current);
        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        d.deleteRow (current, true);
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        d.state.selectInLayerList (juce::Uuid::null());
        return true;
    }

    return false;
}

void LayerList::focusGained (FocusChangeType)
{
    repaint();
}

void LayerList::focusLost (FocusChangeType)
{
    repaint();
}

juce::String LayerList::getTooltip()
{
    const auto& d = *impl;
    const auto hidden = juce::isPositiveAndBelow (d.hover.row, d.numRows()) && ! d.rows[static_cast<size_t> (d.hover.row)].visible;

    switch (d.hover.part)
    {
        case Part::body:       return "Click to select, double-click for its settings, drag to reorder";
        case Part::eye:        return hidden ? "Show (it sounds again)" : "Hide (silent, but kept in the drawing)";
        case Part::remove:     return "Delete";
        case Part::settings:   return "Settings";
        case Part::paintEye:   return d.paintShown ? "Hide the paint layer" : "Show the paint layer";
        case Part::paintClear: return "Clear the paint (text and pictures stay)";
        case Part::paintBody:
        case Part::none:       break;
    }

    return {};
}

void LayerList::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    const auto newScroll = juce::jlimit (0, impl->maxScroll(), juce::roundToInt (newRangeStart));

    if (newScroll != impl->scroll)
    {
        impl->scroll = newScroll;
        repaint();
    }
}

} // namespace freqart::ui
