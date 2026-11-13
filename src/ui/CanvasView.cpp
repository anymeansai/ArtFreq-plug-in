#include "CanvasView.h"

#include "Actions.h"
#include "CanvasAxes.h"
#include "CanvasPlanImage.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Compositor.h"

#include <array>
#include <cmath>

namespace freqart::ui
{
namespace
{
    constexpr int framesPerSecond = 30;

    // Logical pixels
    constexpr float handleSize = 7.0f;
    constexpr float handleHitRadius = 6.0f;
    constexpr float objectHitSlop = 3.0f;      // thin objects are still easy to click
    constexpr float dragThreshold = 3.0f;      // a click that moves less than this is not a drag
    constexpr float minObjectPixels = 10.0f;   // resizing stops at this size
    constexpr float playheadWidth = 2.0f;
    constexpr float playheadCapWidth = 9.0f;   // the small triangle on top of the playhead
    constexpr float playheadCapHeight = 6.0f;

    constexpr double nudgeSessionGap = 1.0;    // seconds: nudges closer together are one undo step

    enum Edge
    {
        leftEdge = 1,
        rightEdge = 2,
        topEdge = 4,
        bottomEdge = 8
    };

    // Handles clockwise from the top-left corner.
    constexpr std::array<int, 8> handleEdges { leftEdge | topEdge, topEdge, rightEdge | topEdge, rightEdge,
                                               rightEdge | bottomEdge, bottomEdge, leftEdge | bottomEdge, leftEdge };

    bool isImageFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("png;jpg;jpeg;gif");
    }

    /** jlimit that tolerates an empty range (the result is then `low`). */
    float clampTo (float value, float low, float high) noexcept
    {
        return low >= high ? low : juce::jlimit (low, high, value);
    }

    juce::MouseCursor cursorForEdges (int edges)
    {
        switch (edges)
        {
            case leftEdge | topEdge:     return juce::MouseCursor::TopLeftCornerResizeCursor;
            case rightEdge | topEdge:    return juce::MouseCursor::TopRightCornerResizeCursor;
            case leftEdge | bottomEdge:  return juce::MouseCursor::BottomLeftCornerResizeCursor;
            case rightEdge | bottomEdge: return juce::MouseCursor::BottomRightCornerResizeCursor;
            case leftEdge:
            case rightEdge:              return juce::MouseCursor::LeftRightResizeCursor;
            case topEdge:
            case bottomEdge:             return juce::MouseCursor::UpDownResizeCursor;
            default:                     return juce::MouseCursor::NormalCursor;
        }
    }

    void drawDashedRectangle (juce::Graphics& g, juce::Rectangle<float> r, float dash)
    {
        // Centred on pixel centres so the 1 px dashes stay crisp.
        const auto x0 = std::floor (r.getX()) + 0.5f, x1 = std::floor (r.getRight()) - 0.5f;
        const auto y0 = std::floor (r.getY()) + 0.5f, y1 = std::floor (r.getBottom()) - 0.5f;
        const float pattern[] { dash, dash };

        g.drawDashedLine ({ x0, y0, x1, y0 }, pattern, 2, 1.0f);
        g.drawDashedLine ({ x1, y0, x1, y1 }, pattern, 2, 1.0f);
        g.drawDashedLine ({ x1, y1, x0, y1 }, pattern, 2, 1.0f);
        g.drawDashedLine ({ x0, y1, x0, y0 }, pattern, 2, 1.0f);
    }
} // namespace

//==============================================================================
struct CanvasView::Impl
{
    Impl (CanvasView& ownerToUse, FreqArtProcessor& processorToUse, EditorState& stateToUse)
        : owner (ownerToUse), processor (processorToUse), state (stateToUse),
          lengthParameter (processor.getParameters().getRawParameterValue (params::id::length))
    {
        plan = processor.getCurrentPlan();
        scale = processor.getScale();
        lengthIndex = currentLengthIndex();
    }

    // ---- geometry ------------------------------------------------------------------------

    juce::Rectangle<float> area() const { return owner.getDrawingArea(); }

    /** Screen position to canvas fractions (x time 0..1, y 0 top .. 1 bottom). */
    juce::Point<float> toNorm (juce::Point<float> p) const
    {
        const auto a = area();
        return { (p.x - a.getX()) / juce::jmax (1.0f, a.getWidth()), (p.y - a.getY()) / juce::jmax (1.0f, a.getHeight()) };
    }

    juce::Rectangle<float> toScreen (juce::Rectangle<float> norm) const
    {
        const auto a = area();
        return { a.getX() + norm.getX() * a.getWidth(), a.getY() + norm.getY() * a.getHeight(),
                 norm.getWidth() * a.getWidth(), norm.getHeight() * a.getHeight() };
    }

    juce::Rectangle<float> screenRectOf (const ArtObject& object) const
    {
        return toScreen (objectRectNorm (object, scale));
    }

    static std::array<juce::Point<float>, 8> handleCentres (juce::Rectangle<float> r)
    {
        const auto cx = r.getCentreX(), cy = r.getCentreY();
        return { { { r.getX(), r.getY() }, { cx, r.getY() }, { r.getRight(), r.getY() }, { r.getRight(), cy },
                   { r.getRight(), r.getBottom() }, { cx, r.getBottom() }, { r.getX(), r.getBottom() }, { r.getX(), cy } } };
    }

    int currentLengthIndex() const
    {
        return lengthParameter != nullptr ? juce::roundToInt (lengthParameter->load()) : params::defaultLengthIndex;
    }

    // ---- the document --------------------------------------------------------------------

    const ArtObject* selectedObject() const
    {
        return state.hasSelection() ? processor.getDocument().find (state.getSelected()) : nullptr;
    }

    /** The top-most object under a point (objects later in the list are drawn on top). */
    const ArtObject* objectAt (juce::Point<float> p) const
    {
        const auto& objects = processor.getDocument().objects;

        for (auto it = objects.rbegin(); it != objects.rend(); ++it)
            if (screenRectOf (*it).expanded (objectHitSlop).contains (p))
                return &*it;

        return nullptr;
    }

    /** The handle of the selected object under a point, or -1. */
    int handleAt (juce::Point<float> p) const
    {
        if (state.getTool() != Tool::select)
            return -1;

        if (const auto* object = selectedObject())
        {
            const auto centres = handleCentres (screenRectOf (*object));

            // Corners first: where a small object's handles overlap, corners are more useful.
            for (int i : { 0, 2, 4, 6, 1, 3, 5, 7 })
            {
                const auto c = centres[static_cast<size_t> (i)];

                if (std::abs (p.x - c.x) <= handleHitRadius && std::abs (p.y - c.y) <= handleHitRadius)
                    return i;
            }
        }

        return -1;
    }

    /** Writes a new placement for the dragged object. Each edge that has not moved keeps its
        exact value (no round trip through the scale), so an object dragged sideways stays exactly
        on its band, and resizing one edge never nudges the others. */
    void applyPlacement (juce::Rectangle<float> r, const juce::String& key)
    {
        const auto id = dragObject;
        const auto moved = [] (float a, float b) { return ! juce::exactlyEqual (a, b); };
        const auto hz = normSpanToHz (r.getY(), r.getBottom(), scale);

        const auto start = moved (r.getX(), dragStartRect.getX()) ? r.getX() : dragStartTime;
        const auto end = moved (r.getRight(), dragStartRect.getRight()) ? r.getRight() : dragEndTime;
        const auto high = moved (r.getY(), dragStartRect.getY()) ? hz.high : dragStartHz.high;
        const auto low = moved (r.getBottom(), dragStartRect.getBottom()) ? hz.low : dragStartHz.low;

        processor.editDocument ([id, start, end, low, high] (ArtDocument& document)
        {
            if (auto* object = document.find (id))
            {
                object->startTime = start;
                object->endTime = end;
                object->lowHz = static_cast<float> (low);
                object->highHz = static_cast<float> (high);
            }
        }, key);
    }

    void rememberDragStart (const ArtObject& object)
    {
        dragObject = object.id;
        dragStartRect = objectRectNorm (object, scale);
        dragStartTime = object.startTime;
        dragEndTime = object.endTime;
        dragStartHz = { static_cast<double> (object.lowHz), static_cast<double> (object.highHz) };
    }

    /** Where the dragged rectangle may go: the visible canvas, or as far out as it already was. */
    juce::Rectangle<float> dragBounds() const
    {
        return juce::Rectangle<float> (0.0f, 0.0f, 1.0f, 1.0f).getUnion (dragStartRect);
    }

    juce::Rectangle<float> movedRect (juce::Point<float> deltaNorm) const
    {
        const auto bounds = dragBounds();
        auto r = dragStartRect.translated (deltaNorm.x, deltaNorm.y);
        r.setPosition (clampTo (r.getX(), bounds.getX(), bounds.getRight() - r.getWidth()),
                       clampTo (r.getY(), bounds.getY(), bounds.getBottom() - r.getHeight()));
        return r;
    }

    juce::Rectangle<float> resizedRect (juce::Point<float> deltaNorm) const
    {
        const auto a = area();
        const auto bounds = dragBounds();
        const auto minWidth = minObjectPixels / juce::jmax (1.0f, a.getWidth());
        const auto minHeight = minObjectPixels / juce::jmax (1.0f, a.getHeight());

        auto left = dragStartRect.getX(), right = dragStartRect.getRight();
        auto top = dragStartRect.getY(), bottom = dragStartRect.getBottom();

        if ((dragEdges & leftEdge) != 0)   left = clampTo (left + deltaNorm.x, bounds.getX(), right - minWidth);
        if ((dragEdges & rightEdge) != 0)  right = clampTo (right + deltaNorm.x, left + minWidth, bounds.getRight());
        if ((dragEdges & topEdge) != 0)    top = clampTo (top + deltaNorm.y, bounds.getY(), bottom - minHeight);
        if ((dragEdges & bottomEdge) != 0) bottom = clampTo (bottom + deltaNorm.y, top + minHeight, bounds.getBottom());

        return { left, top, right - left, bottom - top };
    }

    // ---- brush -----------------------------------------------------------------------------

    /** A stroke is drawn into its own mask (the strongest brush profile at each pixel so far) and
        the paint layer is recomputed from how it was before the stroke. So the segments of one
        stroke never compound: an erase is exactly as soft as its brush, however many mouse
        events it took. */
    void beginStroke (juce::Point<float> position)
    {
        const auto& paint = processor.getDocument().paint;
        const auto size = static_cast<size_t> (ArtDocument::paintWidth * ArtDocument::paintHeight);
        strokeBefore = paint.size() == size ? paint : std::vector<uint8_t> (size, 0);
        strokeMask.clearPaint();
        strokeErase = state.getTool() == Tool::erase;
        strokeStrength = state.getBrushStrength();
        strokeSoftness = state.getBrushSoftness();
        strokeKey = "canvas-stroke-" + juce::String (++editCounter);
        lastStrokePoint = position;
        strokeTo (position);
    }

    juce::Point<float> toPaintLayer (juce::Point<float> screen) const
    {
        // Pixel (x, y) of the paint layer is measured at its centre, so normX * width puts a
        // dab exactly where the cursor is.
        const auto n = toNorm (screen);
        return { n.x * static_cast<float> (ArtDocument::paintWidth), n.y * static_cast<float> (ArtDocument::paintHeight) };
    }

    void strokeTo (juce::Point<float> position)
    {
        const auto a = area();
        const auto radius = state.getBrushSize() * 0.5f;

        BrushStroke stroke;
        stroke.from = toPaintLayer (lastStrokePoint);
        stroke.to = toPaintLayer (position);
        stroke.radiusX = radius / juce::jmax (1.0f, a.getWidth()) * static_cast<float> (ArtDocument::paintWidth);
        stroke.radiusY = radius / juce::jmax (1.0f, a.getHeight()) * static_cast<float> (ArtDocument::paintHeight);
        stroke.softness = strokeSoftness;
        stroke.strength = 1.0f;
        stroke.erase = false;
        lastStrokePoint = position;

        strokeMask.paintStroke (stroke);

        // The pixels this segment can have touched.
        const auto left   = juce::jmax (0, static_cast<int> (std::floor (juce::jmin (stroke.from.x, stroke.to.x) - stroke.radiusX)) - 1);
        const auto right  = juce::jmin (ArtDocument::paintWidth - 1, static_cast<int> (std::ceil (juce::jmax (stroke.from.x, stroke.to.x) + stroke.radiusX)) + 1);
        const auto top    = juce::jmax (0, static_cast<int> (std::floor (juce::jmin (stroke.from.y, stroke.to.y) - stroke.radiusY)) - 1);
        const auto bottom = juce::jmin (ArtDocument::paintHeight - 1, static_cast<int> (std::ceil (juce::jmax (stroke.from.y, stroke.to.y) + stroke.radiusY)) + 1);

        if (left > right || top > bottom)
            return;

        const auto& mask = strokeMask.paint;
        const auto& before = strokeBefore;
        const auto erase = strokeErase;
        const auto strength = strokeStrength;

        processor.editDocument ([&] (ArtDocument& document)
        {
            const auto size = static_cast<size_t> (ArtDocument::paintWidth * ArtDocument::paintHeight);

            if (document.paint.size() != size)
                document.paint.assign (size, 0);

            for (int y = top; y <= bottom; ++y)
            {
                const auto row = static_cast<size_t> (y) * static_cast<size_t> (ArtDocument::paintWidth);

                for (int x = left; x <= right; ++x)
                {
                    const auto i = row + static_cast<size_t> (x);
                    // Strength scales both: how loud the brush paints, and how much Erase removes.
                    const auto m = juce::roundToInt (strength * static_cast<float> (mask[i]));
                    const auto b = static_cast<int> (before[i]);

                    document.paint[i] = static_cast<uint8_t> (erase ? (b * (255 - m) + 127) / 255 : juce::jmax (b, m));
                }
            }
        }, strokeKey);
    }

    juce::Rectangle<int> brushCursorBounds (juce::Point<float> p) const
    {
        const auto r = state.getBrushSize() * 0.5f + 3.0f;
        return juce::Rectangle<float> (p.x - r, p.y - r, r * 2.0f, r * 2.0f).getSmallestIntegerContainer();
    }

    bool brushActive() const { return state.getTool() == Tool::brush || state.getTool() == Tool::erase; }

    void setBrushCursor (bool visible, juce::Point<float> position)
    {
        if (visible == cursorVisible && (! visible || position == cursorPosition))
            return;

        if (cursorVisible)
            owner.repaint (brushCursorBounds (cursorPosition));

        cursorVisible = visible;
        cursorPosition = position;

        if (cursorVisible)
            owner.repaint (brushCursorBounds (cursorPosition));
    }

    // ---- hover, cursors --------------------------------------------------------------------

    void updateHover (juce::Point<float> p, bool inside)
    {
        const auto overDrawing = inside && area().contains (p);

        if (brushActive())
        {
            setBrushCursor (overDrawing, p);
            setHovered ({});
            owner.setMouseCursor (overDrawing ? juce::MouseCursor::NoCursor : juce::MouseCursor::NormalCursor);
            return;
        }

        setBrushCursor (false, p);

        if (! inside)
        {
            setHovered ({});
            owner.setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }

        if (const auto handle = handleAt (p); handle >= 0)
        {
            setHovered ({});
            owner.setMouseCursor (cursorForEdges (handleEdges[static_cast<size_t> (handle)]));
            return;
        }

        const auto* object = objectAt (p);
        setHovered (object != nullptr ? object->id : juce::Uuid::null());
        owner.setMouseCursor (object != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void setHovered (const juce::Uuid& id)
    {
        if (hovered != id)
        {
            hovered = id;
            owner.repaint();
        }
    }

    // ---- playhead --------------------------------------------------------------------------

    juce::Rectangle<int> playheadStrip (float x) const
    {
        const auto a = area();
        // The line plus its cap in the margin above the drawing.
        return juce::Rectangle<float> (x - playheadCapWidth, a.getY() - playheadCapHeight - 3.0f,
                                       playheadCapWidth * 2.0f, a.getHeight() + playheadCapHeight + 4.0f).getSmallestIntegerContainer();
    }

    float playheadXFor (double position) const
    {
        const auto a = area();
        return a.getX() + static_cast<float> (juce::jlimit (0.0, 1.0, position)) * a.getWidth();
    }

    void updatePlayhead()
    {
        const auto sounding = processor.isSounding();
        const auto x = playheadXFor (processor.getPlayPosition());

        if (sounding == playheadShown && (! sounding || std::abs (x - playheadX) < 0.25f))
            return;

        if (playheadShown)
            owner.repaint (playheadStrip (playheadX));

        playheadShown = sounding;
        playheadX = x;

        if (playheadShown)
            owner.repaint (playheadStrip (playheadX));
    }

    // ---- keeping up with the processor and the editor state --------------------------------

    void processorChanged()
    {
        plan = processor.getCurrentPlan();
        scale = processor.getScale();

        // An undo can remove the selected object.
        if (state.hasSelection() && processor.getDocument().find (state.getSelected()) == nullptr)
            state.setSelected (juce::Uuid::null());

        owner.repaint();
    }

    void pollParameters()
    {
        // The length changes no plan, and a scale change shows before the new plan arrives.
        const auto newLength = currentLengthIndex();
        const auto newScale = processor.getScale();

        if (newLength != lengthIndex || newScale != scale)
        {
            lengthIndex = newLength;
            scale = newScale;
            owner.repaint();
        }
    }

    // ---- painting --------------------------------------------------------------------------

    void paint (juce::Graphics& g)
    {
        const auto a = area();

        if (a.isEmpty())
        {
            g.fillAll (theme::bg);
            return;
        }

        // The background only around the drawing, which covers the rest.
        {
            const auto bounds = owner.getLocalBounds();
            const auto inner = a.getSmallestIntegerContainer();
            g.setColour (theme::bg);
            g.fillRect (bounds.withBottom (inner.getY()));
            g.fillRect (bounds.withTop (inner.getBottom()));
            g.fillRect (bounds.withTop (inner.getY()).withBottom (inner.getBottom()).withRight (inner.getX()));
            g.fillRect (bounds.withTop (inner.getY()).withBottom (inner.getBottom()).withLeft (inner.getRight()));
        }

        const auto drawingBounds = a.getSmallestIntegerContainer();

        if (g.clipRegionIntersects (drawingBounds))
        {
            const auto pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
            const auto picture = planImage.get (plan, scale,
                                                juce::roundToInt (a.getWidth() * pixelScale),
                                                juce::roundToInt (a.getHeight() * pixelScale), pixelScale);
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImage (picture, a);

            const auto marks = axes::timeMarks (lengthIndex, a.getWidth());
            axes::drawFrequencyGrid (g, a, scale, juce::Colours::white.withAlpha (0.06f));
            axes::drawTimeGrid (g, a, marks, juce::Colours::white.withAlpha (0.09f), juce::Colours::white.withAlpha (0.035f));

            {
                juce::Graphics::ScopedSaveState saved (g);
                g.reduceClipRegion (drawingBounds);
                paintObjects (g);
                paintEmptyHint (g, a);
            }

            if (playheadShown)
            {
                g.setColour (theme::accent2);
                g.fillRect (juce::Rectangle<float> (std::round (playheadX - playheadWidth * 0.5f), a.getY(), playheadWidth, a.getHeight()));
            }
        }

        g.setColour (theme::line);
        g.drawRect (a.expanded (1.0f), 1.0f);

        if (playheadShown)
            paintPlayheadCap (g, a);

        paintAxes (g, a);
        paintSelectionHandles (g);

        if (cursorVisible && brushActive())
            paintBrushCursor (g);

        if (dropHighlight)
            paintDropHighlight (g, a);
    }

    void paintObjects (juce::Graphics& g)
    {
        const auto& objects = processor.getDocument().objects;
        const auto selectedId = state.getSelected();

        for (const auto& object : objects)
        {
            if (object.id == selectedId)
                continue;

            const auto r = screenRectOf (object);
            const auto isHovered = object.id == hovered && state.getTool() == Tool::select;
            g.setColour (juce::Colours::white.withAlpha (isHovered ? 0.55f : 0.22f));
            drawDashedRectangle (g, r, 4.0f);
        }

        if (const auto* object = selectedObject())
        {
            const auto r = screenRectOf (*object);
            g.setColour (theme::accent);
            g.drawRect (juce::Rectangle<float> (std::floor (r.getX()), std::floor (r.getY()),
                                                std::floor (r.getRight()) - std::floor (r.getX()),
                                                std::floor (r.getBottom()) - std::floor (r.getY())), 1.0f);

            if (dragMode == DragMode::move || dragMode == DragMode::resize)
                paintPlacementTag (g, *object, r);
        }
    }

    /** A small triangle in the margin above the drawing, so the playhead reads at a glance even
        where it crosses something bright. */
    void paintPlayheadCap (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const auto x = std::round (playheadX - playheadWidth * 0.5f) + playheadWidth * 0.5f;
        const auto bottom = a.getY() - 1.0f;
        juce::Path cap;
        cap.addTriangle (x - playheadCapWidth * 0.5f, bottom - playheadCapHeight,
                         x + playheadCapWidth * 0.5f, bottom - playheadCapHeight,
                         x, bottom);
        g.setColour (theme::accent2);
        g.fillPath (cap);
    }

    /** While dragging: the frequency range, so it can be placed by ear and by number. */
    void paintPlacementTag (juce::Graphics& g, const ArtObject& object, juce::Rectangle<float> r)
    {
        const auto text = formatHz (object.lowHz) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 ")) + formatHz (object.highHz);
        const auto font = fonts::mono (11.0f);
        const auto width = juce::GlyphArrangement::getStringWidth (font, text) + 12.0f;
        const auto a = area();
        auto tag = juce::Rectangle<float> (r.getX(), r.getY() - 24.0f, width, 18.0f);

        if (tag.getY() < a.getY() + 2.0f)
            tag.setPosition (r.getX() + 12.0f, r.getY() + 8.0f);  // no room above: inside, clear of the handle

        tag.setX (clampTo (tag.getX(), a.getX() + 2.0f, a.getRight() - width - 2.0f));

        g.setColour (theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (theme::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (tag.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (font);
        g.drawText (text, tag, juce::Justification::centred, false);
    }

    void paintSelectionHandles (juce::Graphics& g)
    {
        if (state.getTool() != Tool::select)
            return;

        if (const auto* object = selectedObject())
        {
            for (const auto c : handleCentres (screenRectOf (*object)))
            {
                const auto h = juce::Rectangle<float> (handleSize, handleSize)
                                   .withCentre ({ std::floor (c.x) + 0.5f, std::floor (c.y) + 0.5f });
                g.setColour (theme::text);
                g.fillRect (h);
                g.setColour (theme::accent);
                g.drawRect (h, 1.0f);
            }
        }
    }

    void paintEmptyHint (juce::Graphics& g, juce::Rectangle<float> a)
    {
        if (! processor.getDocument().isEmpty())
            return;

        // The three ways to start, as icons over the hint, and what the canvas means under it.
        constexpr float iconSize = 22.0f, iconGap = 20.0f, lineGap = 8.0f;
        const auto centre = a.getCentre();
        const auto top = centre.y - 40.0f;
        const juce::Path paths[] { icons::text(), icons::image(), icons::brush() };
        auto x = centre.x - (3.0f * iconSize + 2.0f * iconGap) * 0.5f;

        // A quiet backing card, so the grid does not run through the words.
        const auto cardWidth = juce::jmin (a.getWidth() - 24.0f, 560.0f);
        const auto card = juce::Rectangle<float> (centre.x - cardWidth * 0.5f, top - 22.0f, cardWidth, iconSize + 14.0f + 44.0f + 44.0f);
        g.setColour (theme::spectrum (0.0f).withAlpha (0.9f));
        g.fillRoundedRectangle (card, theme::panelCorner);

        g.setColour (theme::textFaint);

        for (const auto& path : paths)
        {
            // The icons are designed in a 24 box: scale the box, not the path's own bounds, so
            // they keep their relative sizes and stroke weights.
            const auto box = juce::Rectangle<float> (x, top, iconSize, iconSize);
            g.fillPath (path, juce::AffineTransform::scale (iconSize / 24.0f).translated (box.getX(), box.getY()));
            x += iconSize + iconGap;
        }

        auto text = juce::Rectangle<float> (a.getX(), top + iconSize + 14.0f, a.getWidth(), 20.0f);
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (fonts::ui (14.0f, fonts::UiWeight::medium));
        g.drawText ("Add text, drop an image, or paint with the brush", text, juce::Justification::centred, false);

        text.translate (0.0f, 20.0f + lineGap * 0.5f);
        g.setColour (theme::textDim);
        g.setFont (fonts::ui (12.5f));
        g.drawText ("What you draw here is what your spectrogram will show: time across, frequency up.",
                    text, juce::Justification::centred, false);
    }

    void paintAxes (juce::Graphics& g, juce::Rectangle<float> a)
    {
        const auto frequencyAxis = juce::Rectangle<float> (0.0f, a.getY(), a.getX(), a.getHeight());
        const auto timeAxis = juce::Rectangle<float> (a.getX(), a.getBottom() + 1.0f, a.getWidth(), static_cast<float> (axisHeight) - 1.0f);

        if (g.clipRegionIntersects (frequencyAxis.expanded (0.0f, 8.0f).getSmallestIntegerContainer()))
            axes::drawFrequencyLabels (g, frequencyAxis, a, scale);

        if (g.clipRegionIntersects (timeAxis.getSmallestIntegerContainer()))
            axes::drawTimeLabels (g, timeAxis, a, axes::timeMarks (lengthIndex, a.getWidth()));
    }

    void paintBrushCursor (juce::Graphics& g)
    {
        const auto radius = state.getBrushSize() * 0.5f;
        const auto circle = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (cursorPosition);
        const auto erase = state.getTool() == Tool::erase;

        // A dark halo under a light ring: visible over silence and over the loudest colours.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawEllipse (circle.expanded (1.0f), 1.0f);

        if (erase)
        {
            juce::Path ring, dashed;
            ring.addEllipse (circle);
            const float pattern[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, ring, pattern, 2);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawEllipse (circle, 1.0f);
        }

        // The hard core of a soft brush.
        const auto core = radius * (1.0f - state.getBrushSoftness());

        if (core > 2.0f && core < radius - 2.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.drawEllipse (juce::Rectangle<float> (core * 2.0f, core * 2.0f).withCentre (cursorPosition), 1.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (2.0f, 2.0f).withCentre (cursorPosition));
    }

    void paintDropHighlight (juce::Graphics& g, juce::Rectangle<float> a)
    {
        g.setColour (theme::accent.withAlpha (0.12f));
        g.fillRect (a);
        g.setColour (theme::accent);
        g.drawRect (a, 2.0f);

        const auto font = fonts::ui (theme::controlSize, fonts::UiWeight::medium);
        const auto text = juce::String ("Drop to add the picture here");
        const auto pill = juce::Rectangle<float> (juce::GlyphArrangement::getStringWidth (font, text) + 28.0f, 32.0f)
                              .withCentre (a.getCentre());
        g.setColour (theme::panel.withAlpha (0.94f));
        g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (pill.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (font);
        g.drawText (text, pill, juce::Justification::centred, false);
    }

    // ---- state ---------------------------------------------------------------------------

    CanvasView& owner;
    FreqArtProcessor& processor;
    EditorState& state;
    std::atomic<float>* lengthParameter = nullptr;

    CanvasPlanImage planImage;
    std::shared_ptr<const ArtPlan> plan;  // kept alive while it is shown
    FrequencyScale scale;
    int lengthIndex = params::defaultLengthIndex;

    bool playheadShown = false;
    float playheadX = 0.0f;

    bool cursorVisible = false;
    juce::Point<float> cursorPosition;
    juce::Uuid hovered = juce::Uuid::null();
    bool dropHighlight = false;

    enum class DragMode
    {
        none,
        pendingMove,    // pressed on an object; becomes a move once the mouse has moved a little
        pendingResize,  // ... on a handle
        move,
        resize,
        paint
    };

    DragMode dragMode = DragMode::none;
    juce::Uuid dragObject = juce::Uuid::null();
    juce::Rectangle<float> dragStartRect;   // normalised
    float dragStartTime = 0.0f, dragEndTime = 1.0f;
    HzRange dragStartHz;
    juce::Point<float> dragStartMouse;
    int dragEdges = 0;
    juce::String dragKey;
    int editCounter = 0;

    std::vector<uint8_t> strokeBefore;
    ArtDocument strokeMask;
    bool strokeErase = false;
    float strokeStrength = 1.0f;
    float strokeSoftness = 0.5f;
    juce::String strokeKey;
    juce::Point<float> lastStrokePoint;

    double lastNudgeTime = -1.0e9;
    int nudgeSession = 0;
};

//==============================================================================
CanvasView::CanvasView (FreqArtProcessor& processor, EditorState& state)
    : impl (std::make_unique<Impl> (*this, processor, state))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setTitle ("Canvas");
    setDescription ("The drawing: time across, frequency up. Click an object to select it.");

    processor.addChangeListener (this);
    state.addChangeListener (this);
    startTimerHz (framesPerSecond);
}

CanvasView::~CanvasView()
{
    stopTimer();
    impl->processor.removeChangeListener (this);
    impl->state.removeChangeListener (this);
}

juce::Rectangle<float> CanvasView::getDrawingArea() const
{
    return axes::plotArea (getLocalBounds(), axisWidth, axisHeight).toFloat();
}

void CanvasView::paint (juce::Graphics& g)
{
    impl->paint (g);
}

void CanvasView::resized()
{
    impl->planImage.invalidate();
    repaint();
}

//==============================================================================
void CanvasView::mouseMove (const juce::MouseEvent& e)
{
    impl->updateHover (e.position, true);
}

void CanvasView::mouseExit (const juce::MouseEvent& e)
{
    impl->updateHover (e.position, false);
}

void CanvasView::mouseDown (const juce::MouseEvent& e)
{
    auto& d = *impl;

    // (Only on screen: the snapshot tool sends mouse events to an editor that is not.)
    if (getWantsKeyboardFocus() && isShowing())
        grabKeyboardFocus();

    d.dragMode = Impl::DragMode::none;
    d.dragStartMouse = e.position;
    d.scale = d.processor.getScale();  // what the drag converts with, from start to end
    ++d.nudgeSession;

    if (e.mods.isPopupMenu())
        return;

    if (d.brushActive())
    {
        if (d.area().contains (e.position))
        {
            d.dragMode = Impl::DragMode::paint;
            d.beginStroke (e.position);
        }

        return;
    }

    if (const auto handle = d.handleAt (e.position); handle >= 0)
    {
        d.rememberDragStart (*d.selectedObject());
        d.dragEdges = handleEdges[static_cast<size_t> (handle)];
        d.dragMode = Impl::DragMode::pendingResize;
        return;
    }

    if (const auto* object = d.objectAt (e.position))
    {
        d.state.setSelected (object->id);
        d.rememberDragStart (*object);
        d.dragMode = Impl::DragMode::pendingMove;

        // Double-clicking text goes straight to typing.
        if (e.getNumberOfClicks() >= 2 && object->type == ArtObject::Type::text && d.state.focusTextField != nullptr)
        {
            d.dragMode = Impl::DragMode::none;
            d.state.focusTextField();
        }

        return;
    }

    d.state.setSelected (juce::Uuid::null());
}

void CanvasView::mouseDrag (const juce::MouseEvent& e)
{
    auto& d = *impl;

    if (d.dragMode == Impl::DragMode::paint)
    {
        d.strokeTo (e.position);
        d.setBrushCursor (true, e.position);
        return;
    }

    if (d.dragMode == Impl::DragMode::pendingMove || d.dragMode == Impl::DragMode::pendingResize)
    {
        if (e.position.getDistanceFrom (d.dragStartMouse) < dragThreshold)
            return;

        d.dragMode = d.dragMode == Impl::DragMode::pendingMove ? Impl::DragMode::move : Impl::DragMode::resize;
        d.dragKey = "canvas-drag-" + juce::String (++d.editCounter);
        setMouseCursor (d.dragMode == Impl::DragMode::move ? juce::MouseCursor::DraggingHandCursor
                                                           : cursorForEdges (d.dragEdges));
    }

    if (d.dragMode != Impl::DragMode::move && d.dragMode != Impl::DragMode::resize)
        return;

    if (d.processor.getDocument().find (d.dragObject) == nullptr)
    {
        d.dragMode = Impl::DragMode::none;  // undone or deleted meanwhile
        return;
    }

    const auto a = getDrawingArea();
    auto delta = e.position - d.dragStartMouse;

    // Shift keeps a move on one axis: along time, or along frequency.
    if (d.dragMode == Impl::DragMode::move && e.mods.isShiftDown())
    {
        if (std::abs (delta.x) >= std::abs (delta.y))
            delta.y = 0.0f;
        else
            delta.x = 0.0f;
    }

    const auto deltaNorm = juce::Point<float> (delta.x / juce::jmax (1.0f, a.getWidth()), delta.y / juce::jmax (1.0f, a.getHeight()));
    const auto r = d.dragMode == Impl::DragMode::move ? d.movedRect (deltaNorm) : d.resizedRect (deltaNorm);
    d.applyPlacement (r, d.dragKey);
}

void CanvasView::mouseUp (const juce::MouseEvent& e)
{
    auto& d = *impl;
    const auto wasEditing = d.dragMode == Impl::DragMode::move || d.dragMode == Impl::DragMode::resize
                         || d.dragMode == Impl::DragMode::paint;
    d.dragMode = Impl::DragMode::none;

    if (wasEditing)
    {
        d.processor.endCoalescing();
        d.strokeBefore = {};
    }

    d.updateHover (e.position, isMouseOver());
    repaint();
}

bool CanvasView::keyPressed (const juce::KeyPress& key)
{
    auto& d = *impl;
    const auto mods = key.getModifiers();

    if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown())
        return false;  // shortcuts (undo, ...) belong to the editor

    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (! d.state.hasSelection())
            return false;

        actions::deleteSelected (d.processor, d.state);
        return true;
    }

    if (code == juce::KeyPress::escapeKey && d.state.hasSelection())
    {
        d.state.setSelected (juce::Uuid::null());
        return true;
    }

    const auto left = code == juce::KeyPress::leftKey, right = code == juce::KeyPress::rightKey;
    const auto up = code == juce::KeyPress::upKey, down = code == juce::KeyPress::downKey;

    if (! (left || right || up || down))
        return false;

    const auto* object = d.selectedObject();

    if (object == nullptr || d.dragMode != Impl::DragMode::none)
        return object != nullptr;

    // One screen pixel per press (ten with Shift); presses in quick succession are one undo step.
    const auto step = mods.isShiftDown() ? 10.0f : 1.0f;
    const auto a = getDrawingArea();
    const auto deltaNorm = juce::Point<float> ((right ? step : left ? -step : 0.0f) / juce::jmax (1.0f, a.getWidth()),
                                               (down ? step : up ? -step : 0.0f) / juce::jmax (1.0f, a.getHeight()));

    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    if (now - d.lastNudgeTime > nudgeSessionGap)
        ++d.nudgeSession;

    d.lastNudgeTime = now;
    d.scale = d.processor.getScale();
    d.rememberDragStart (*object);
    d.applyPlacement (d.movedRect (deltaNorm), "canvas-nudge-" + juce::String (d.nudgeSession));
    return true;
}

//==============================================================================
bool CanvasView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& file : files)
        if (isImageFile (file))
            return true;

    return false;
}

void CanvasView::fileDragEnter (const juce::StringArray&, int, int)
{
    impl->dropHighlight = true;
    repaint();
}

void CanvasView::fileDragExit (const juce::StringArray&)
{
    impl->dropHighlight = false;
    repaint();
}

void CanvasView::filesDropped (const juce::StringArray& files, int x, int y)
{
    auto& d = *impl;
    d.dropHighlight = false;
    repaint();

    const auto n = d.toNorm ({ static_cast<float> (x), static_cast<float> (y) });
    const auto position = juce::Point<float> (juce::jlimit (0.0f, 1.0f, n.x), juce::jlimit (0.0f, 1.0f, n.y));

    for (const auto& file : files)
        if (isImageFile (file) && actions::addImageFile (d.processor, d.state, juce::File (file), position))
            break;  // one picture per drop: several would land on top of each other
}

//==============================================================================
void CanvasView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    auto& d = *impl;

    if (source == &d.processor)
    {
        d.processorChanged();
        return;
    }

    // The tool, the brush or the selection changed.
    const auto mouse = getMouseXYRelative().toFloat();
    d.updateHover (mouse, isMouseOver());
    repaint();
}

void CanvasView::timerCallback()
{
    impl->updatePlayhead();
    impl->pollParameters();
}

} // namespace freqart::ui
