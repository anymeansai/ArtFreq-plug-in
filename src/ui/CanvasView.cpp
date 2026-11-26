#include "CanvasView.h"

#include "Actions.h"
#include "CanvasAxes.h"
#include "CanvasPlanImage.h"
#include "EffectPreview.h"
#include "FastPreview.h"
#include "KeyFocus.h"
#include "MeasuredPreview.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Compositor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace artfreq::ui
{
namespace
{
    constexpr int framesPerSecond = 30;

    // Logical pixels
    constexpr float handleSize = 7.0f;
    constexpr float handleHitRadius = 6.0f;
    constexpr float rotateStem = 16.0f;        // the rotate handle: a knob this far out from the top edge
    constexpr float rotateKnobRadius = 4.5f;
    constexpr float rotateHitRadius = 7.0f;
    static_assert (CanvasView::rotateHandleReach >= rotateStem + rotateHitRadius, "the rotate handle's reach");
    constexpr float rotateSnapDegrees = 15.0f; // Shift while turning
    constexpr float rotateKeyDegrees = 15.0f;  // [ and ] (Shift: 1 degree)
    constexpr float objectHitSlop = 3.0f;      // thin objects are still easy to click
    constexpr float dragThreshold = 3.0f;      // a click that moves less than this is not a drag
    constexpr float minObjectPixels = 10.0f;   // resizing stops at this size
    constexpr float playheadWidth = 2.0f;
    constexpr float playheadCapWidth = 9.0f;   // the small triangle on top of the playhead
    constexpr float playheadCapHeight = 6.0f;

    constexpr double nudgeSessionGap = 1.0;    // seconds: nudges closer together are one undo step

    // The cross-fades between the fast and the measured preview: the measured one fades in
    // slowly enough to be seen as an update, not a flicker; the fast one takes over quickly, as
    // it follows the mouse.
    constexpr double fadeToMeasuredSeconds = 0.3;
    constexpr double fadeToFastSeconds = 0.12;

    // The first measured preview waits until the canvas has been on screen this long.
    constexpr double openingQuietSeconds = 0.5;

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }


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
        return actions::isPictureFile (juce::File (path));
    }

    bool isArrowKeyDown()
    {
        return juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::leftKey)
            || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::rightKey)
            || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::upKey)
            || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::downKey);
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

    /** The resize cursor for a handle of a turned object: the handle's direction from the
        object's centre (its edges in the object's own frame), turned, to the nearest of the four
        resize cursors' directions. */
    juce::MouseCursor cursorForTurnedEdges (int edges, float degrees)
    {
        const auto dx = (edges & rightEdge) != 0 ? 1.0f : (edges & leftEdge) != 0 ? -1.0f : 0.0f;
        const auto dy = (edges & bottomEdge) != 0 ? 1.0f : (edges & topEdge) != 0 ? -1.0f : 0.0f;
        auto angle = juce::radiansToDegrees (std::atan2 (dy, dx)) + degrees;   // clockwise, y down
        angle = std::fmod (std::fmod (angle, 180.0f) + 180.0f, 180.0f);          // a line: 0..180

        if (angle < 22.5f || angle >= 157.5f) return juce::MouseCursor::LeftRightResizeCursor;
        if (angle < 67.5f)                    return juce::MouseCursor::TopLeftCornerResizeCursor;    // down-right
        if (angle < 112.5f)                   return juce::MouseCursor::UpDownResizeCursor;
        return juce::MouseCursor::TopRightCornerResizeCursor;                                          // down-left
    }

    juce::Path polygonOf (const std::array<juce::Point<float>, 4>& corners)
    {
        juce::Path path;
        path.startNewSubPath (corners[0]);

        for (size_t i = 1; i < corners.size(); ++i)
            path.lineTo (corners[i]);

        path.closeSubPath();
        return path;
    }

    void drawDashedPolygon (juce::Graphics& g, const std::array<juce::Point<float>, 4>& corners, float dash)
    {
        const float pattern[] { dash, dash };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, polygonOf (corners), pattern, 2);
        g.fillPath (dashed);
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
    enum class Shown { fast, measured };

    Impl (CanvasView& ownerToUse, ArtFreqProcessor& processorToUse, EditorState& stateToUse)
        : owner (ownerToUse), processor (processorToUse), state (stateToUse),
          lengthParameter (processor.getParameters().getRawParameterValue (params::id::length))
    {
        plan = processor.getCurrentPlan();
        scale = processor.getScale();
        lengthIndex = currentLengthIndex();
        timeSignature = processor.getTimeSignature();
        firstBar = processor.getSinglePassStartBar();
        effectMode = processor.getEffectMode();
        depthDb = previewDepthDb (processor.getDepthDb());
        outputGain = previewGain (processor.getOutputGain());
        view = processor.getAnalyserView();
        slope = processor.getPlanSettings().slopeDbPerOctave;
        sampleRate = processor.getCurrentSampleRate();
        passSeconds = previewSeconds (processor.getLengthSeconds());
        shownStatus = statusText();
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

    // ---- rotated objects -------------------------------------------------------------------
    // An object's placement is its unrotated box; on screen it is turned about the box's
    // centre in display space (rotationTransform). Everything the mouse touches (the outline,
    // the handles, what a click picks) is the turned box.

    static bool isUpright (const ArtObject& object) noexcept
    {
        return juce::exactlyEqual (ArtDocument::normaliseRotation (object.rotation), 0.0f);
    }

    /** How much wider than tall a pixel of this view is in display space (see rotationTransform). */
    float screenXScale() const
    {
        const auto a = area();
        return ArtDocument::displayAspect * juce::jmax (1.0f, a.getHeight()) / juce::jmax (1.0f, a.getWidth());
    }

    /** The turn of an object's box on screen (the identity for an upright one). */
    juce::AffineTransform screenTurn (const ArtObject& object) const
    {
        return rotationTransform (screenRectOf (object), object.rotation, screenXScale());
    }

    std::array<juce::Point<float>, 4> screenCorners (const ArtObject& object) const
    {
        const auto r = screenRectOf (object);
        const auto t = screenTurn (object);
        return { r.getTopLeft().transformedBy (t), r.getTopRight().transformedBy (t),
                 r.getBottomRight().transformedBy (t), r.getBottomLeft().transformedBy (t) };
    }

    /** The smallest upright rectangle around an object's turned box on screen. */
    juce::Rectangle<float> screenBoundsOf (const ArtObject& object) const
    {
        if (isUpright (object))
            return screenRectOf (object);

        const auto corners = screenCorners (object);
        return juce::Rectangle<float>::findAreaContainingPoints (corners.data(), static_cast<int> (corners.size()));
    }

    /** Whether a point is on an object: inside its turned box, or within objectHitSlop of it. */
    bool isOnObject (const ArtObject& object, juce::Point<float> p) const
    {
        if (isUpright (object))
            return screenRectOf (object).expanded (objectHitSlop).contains (p);

        return screenRectOf (object).expanded (objectHitSlop).contains (p.transformedBy (screenTurn (object).inverted()));
    }

    std::array<juce::Point<float>, 8> turnedHandleCentres (const ArtObject& object) const
    {
        auto centres = handleCentres (screenRectOf (object));

        if (! isUpright (object))
            for (auto& c : centres)
                c = c.transformedBy (screenTurn (object));

        return centres;
    }

    /** The rotate handle: where its stem starts (the middle of an edge) and its knob's centre.
        Above the top edge (as the object is turned), unless that is off the view: then below
        the bottom edge, or else just inside the top edge. */
    struct RotateHandle
    {
        juce::Point<float> stem, knob;
    };

    RotateHandle rotateHandleOf (const ArtObject& object) const
    {
        const auto r = screenRectOf (object);
        const auto t = screenTurn (object);
        const auto centre = r.getCentre();
        const auto top = juce::Point<float> (r.getCentreX(), r.getY()).transformedBy (t);
        const auto bottom = juce::Point<float> (r.getCentreX(), r.getBottom()).transformedBy (t);
        const auto up = top - centre;
        const auto length = up.getDistanceFromOrigin();
        const auto direction = length > 0.5f ? up / length : juce::Point<float> (0.0f, -1.0f);
        const auto room = owner.getLocalBounds().toFloat().reduced (rotateKnobRadius + 1.0f);

        for (const auto& handle : { RotateHandle { top, top + direction * rotateStem },
                                    RotateHandle { bottom, bottom - direction * rotateStem } })
            if (room.contains (handle.knob))
                return handle;

        return { top, top - direction * rotateStem };
    }

    /** Whether a point is on the selected object's rotate handle (the Select tool only). */
    bool isOnRotateHandle (juce::Point<float> p) const
    {
        if (state.getTool() != Tool::select)
            return false;

        const auto* object = selectedObject();
        return object != nullptr && rotateHandleOf (*object).knob.getDistanceFrom (p) <= rotateHitRadius;
    }

    /** The angle (degrees, clockwise from straight up) from the dragged box's centre to a point,
        in display space. */
    float angleFromCentre (juce::Point<float> p) const
    {
        const auto n = toNorm (p);
        const auto c = dragStartRect.getCentre();
        const auto dx = (n.x - c.x) * ArtDocument::displayAspect;
        const auto dy = n.y - c.y;
        return juce::radiansToDegrees (std::atan2 (dx, -dy));
    }

    /** The cursor over a handle of the selected object. */
    juce::MouseCursor cursorForHandle (const ArtObject& object, int handle) const
    {
        const auto edges = handleEdges[static_cast<size_t> (handle)];
        return isUpright (object) ? cursorForEdges (edges) : cursorForTurnedEdges (edges, object.rotation);
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

    /** The top-most visible object under a point (objects later in the list are in front).
        Hidden objects cannot be clicked: they are picked in the layer list. */
    const ArtObject* objectAt (juce::Point<float> p) const
    {
        const auto& objects = processor.getDocument().objects;

        for (auto it = objects.rbegin(); it != objects.rend(); ++it)
            if (it->visible && isOnObject (*it, p))
                return &*it;

        return nullptr;
    }

    /** What a click at a point takes hold of: the selected object if the point is on it (so an
        object partly under another can be moved and edited once selected, and a hidden one
        picked in the layer list can be moved), else objectAt. */
    const ArtObject* objectToGrab (juce::Point<float> p) const
    {
        if (const auto* selected = selectedObject(); selected != nullptr && state.getTool() == Tool::select
                                                     && isOnObject (*selected, p))
            return selected;

        return objectAt (p);
    }

    /** The object under a point after the selected one, front to back, round to the front again
        (the front-most if the selection is not there): what Alt-click, or clicking again on the
        same spot, picks. Only visible objects, and the selection. */
    const ArtObject* nextObjectBelow (juce::Point<float> p) const
    {
        const auto& objects = processor.getDocument().objects;
        const auto selectedId = state.getSelected();
        std::vector<const ArtObject*> stack;

        for (auto it = objects.rbegin(); it != objects.rend(); ++it)
            if ((it->visible || it->id == selectedId) && isOnObject (*it, p))
                stack.push_back (&*it);

        if (stack.empty())
            return nullptr;

        const auto current = std::find_if (stack.begin(), stack.end(), [&selectedId] (const ArtObject* o) { return o->id == selectedId; });

        if (current == stack.end() || std::next (current) == stack.end())
            return stack.front();

        return *std::next (current);
    }

    /** The handle of the selected object under a point, or -1. */
    int handleAt (juce::Point<float> p) const
    {
        if (state.getTool() != Tool::select)
            return -1;

        if (const auto* object = selectedObject())
        {
            const auto centres = turnedHandleCentres (*object);

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
        dragStartRotation = ArtDocument::normaliseRotation (object.rotation);
        dragStartBounds = objectBoundsNorm (object, scale);
    }

    bool dragStartUpright() const noexcept { return juce::exactlyEqual (dragStartRotation, 0.0f); }

    /** Where the dragged rectangle may go: the visible canvas, or as far out as it already was
        (a turned object: its turned box). */
    juce::Rectangle<float> dragBounds() const
    {
        return juce::Rectangle<float> (0.0f, 0.0f, 1.0f, 1.0f).getUnion (dragStartUpright() ? dragStartRect : dragStartBounds);
    }

    juce::Rectangle<float> movedRect (juce::Point<float> deltaNorm) const
    {
        const auto bounds = dragBounds();

        if (! dragStartUpright())
        {
            // The turned box stays in bounds; the placement moves with it.
            auto box = dragStartBounds.translated (deltaNorm.x, deltaNorm.y);
            box.setPosition (clampTo (box.getX(), bounds.getX(), bounds.getRight() - box.getWidth()),
                             clampTo (box.getY(), bounds.getY(), bounds.getBottom() - box.getHeight()));
            return dragStartRect.translated (box.getX() - dragStartBounds.getX(), box.getY() - dragStartBounds.getY());
        }

        auto r = dragStartRect.translated (deltaNorm.x, deltaNorm.y);
        r.setPosition (clampTo (r.getX(), bounds.getX(), bounds.getRight() - r.getWidth()),
                       clampTo (r.getY(), bounds.getY(), bounds.getBottom() - r.getHeight()));
        return r;
    }

    /** The placement for a resize by a mouse movement (normalised). Shift (`keepShape`) keeps
        the box's proportions: a corner scales it from the opposite corner, following the axis
        the mouse moved along more; an edge scales it about the middle of the opposite edge,
        both sides following. */
    juce::Rectangle<float> resizedRect (juce::Point<float> deltaNorm, bool keepShape = false) const
    {
        if (keepShape || ! dragStartUpright())
            return resizedInOwnFrame (deltaNorm, keepShape);

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

    /** A resize in the box's own frame, in display units (x times displayAspect), so a turned
        box's edges move along its own sides: the mouse's movement is turned back into that
        frame, the edges are moved there, and the box's new centre is turned out again, so the
        opposite edge or corner stays where it is on screen. Also an upright box's resize with
        Shift (keeping its shape), which then stays inside the canvas (or as far out as it was)
        by scaling less. */
    juce::Rectangle<float> resizedInOwnFrame (juce::Point<float> deltaNorm, bool keepShape) const
    {
        constexpr auto aspect = ArtDocument::displayAspect;
        const auto a = area();
        const auto radians = juce::degreesToRadians (dragStartRotation);
        const auto cosine = std::cos (radians), sine = std::sin (radians);

        // The mouse's movement in the box's frame.
        const auto dx = deltaNorm.x * aspect, dy = deltaNorm.y;
        const auto du = dx * cosine + dy * sine;
        const auto dv = -dx * sine + dy * cosine;

        const auto width = juce::jmax (1.0e-6f, dragStartRect.getWidth() * aspect);
        const auto height = juce::jmax (1.0e-6f, dragStartRect.getHeight());
        const auto minWidth = minObjectPixels / juce::jmax (1.0f, a.getWidth()) * aspect;
        const auto minHeight = minObjectPixels / juce::jmax (1.0f, a.getHeight());
        const auto horizontal = (dragEdges & (leftEdge | rightEdge)) != 0;
        const auto vertical = (dragEdges & (topEdge | bottomEdge)) != 0;
        const auto minScale = juce::jmax (minWidth / width, minHeight / height);

        // Free: each dragged edge follows the mouse. Keeping the shape: a scale of the box.
        auto scaleBy = 1.0f;

        if (keepShape)
        {
            const auto widthScale = (width + ((dragEdges & leftEdge) != 0 ? -du : du)) / width;
            const auto heightScale = (height + ((dragEdges & topEdge) != 0 ? -dv : dv)) / height;
            scaleBy = horizontal && vertical ? (std::abs (du) >= std::abs (dv) ? widthScale : heightScale)
                    : horizontal ? widthScale : heightScale;
            scaleBy = juce::jmax (minScale, scaleBy);
        }

        // The box for a scale (keeping the shape) in the box's frame, centred on 0, 0 at the start.
        const auto frameFor = [&] (float s)
        {
            auto left = -width * 0.5f, right = width * 0.5f, top = -height * 0.5f, bottom = height * 0.5f;

            if (! keepShape)
            {
                if ((dragEdges & leftEdge) != 0)   left = juce::jmin (left + du, right - minWidth);
                if ((dragEdges & rightEdge) != 0)  right = juce::jmax (right + du, left + minWidth);
                if ((dragEdges & topEdge) != 0)    top = juce::jmin (top + dv, bottom - minHeight);
                if ((dragEdges & bottomEdge) != 0) bottom = juce::jmax (bottom + dv, top + minHeight);
                return juce::Rectangle<float> (left, top, right - left, bottom - top);
            }

            // The edges dragged move; the opposite ones stay; an edge's own sides scale about its
            // middle (an edge handle: both sides follow).
            if (horizontal)
            {
                if ((dragEdges & leftEdge) != 0) left = right - width * s;
                else                             right = left + width * s;
            }
            else
            {
                left = -width * s * 0.5f;
                right = width * s * 0.5f;
            }

            if (vertical)
            {
                if ((dragEdges & topEdge) != 0) top = bottom - height * s;
                else                            bottom = top + height * s;
            }
            else
            {
                top = -height * s * 0.5f;
                bottom = height * s * 0.5f;
            }

            return juce::Rectangle<float> (left, top, right - left, bottom - top);
        };

        // Back onto the canvas: the frame's centre turned out from the start box's centre.
        const auto toCanvas = [&] (juce::Rectangle<float> frame)
        {
            const auto cu = frame.getCentreX(), cv = frame.getCentreY();
            const auto centre = dragStartRect.getCentre();
            const auto cx = centre.x + (cu * cosine - cv * sine) / aspect;
            const auto cy = centre.y + (cu * sine + cv * cosine);
            return juce::Rectangle<float> (frame.getWidth() / aspect, frame.getHeight()).withCentre ({ cx, cy });
        };

        if (dragStartUpright())
        {
            // Keeping the shape of an upright box. The edges that stay keep their exact values.
            const auto uprightRect = [&] (juce::Rectangle<float> frame)
            {
                const auto& start = dragStartRect;
                const auto w = frame.getWidth() / aspect, h = frame.getHeight();
                const auto left = ! horizontal ? start.getCentreX() - w * 0.5f
                                : (dragEdges & leftEdge) != 0 ? start.getRight() - w : start.getX();
                const auto top = ! vertical ? start.getCentreY() - h * 0.5f
                               : (dragEdges & topEdge) != 0 ? start.getBottom() - h : start.getY();
                return juce::Rectangle<float> (left, top, w, h);
            };

            // As large as fits in bounds (scaling is monotonic about the point that stays, so
            // halving the step finds it).
            const auto bounds = dragBounds().expanded (1.0e-6f);
            auto r = uprightRect (frameFor (scaleBy));

            if (! bounds.contains (r))
            {
                auto low = minScale, high = scaleBy;

                for (int i = 0; i < 24; ++i)
                {
                    const auto middle = (low + high) * 0.5f;

                    if (bounds.contains (uprightRect (frameFor (middle))))
                        low = middle;
                    else
                        high = middle;
                }

                r = uprightRect (frameFor (low));
            }

            return r;
        }

        auto r = toCanvas (frameFor (scaleBy));

        // A turned box: its centre stays on the canvas (or as far out as it was).
        const auto bounds = dragBounds();
        return r.withCentre ({ juce::jlimit (bounds.getX(), bounds.getRight(), r.getCentreX()),
                               juce::jlimit (bounds.getY(), bounds.getBottom(), r.getCentreY()) });
    }

    /** Writes a new rotation for the dragged (or nudged) object. */
    void applyRotation (float degrees, const juce::String& key)
    {
        const auto id = dragObject;
        const auto turn = ArtDocument::normaliseRotation (degrees);

        processor.editDocument ([id, turn] (ArtDocument& document)
        {
            if (auto* object = document.find (id))
                object->rotation = turn;
        }, key);
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

        // The first stroke on an empty layer tags it with the scale it is painted on; later
        // strokes paint into the layer's own scale, whatever the canvas shows now.
        const auto& document = processor.getDocument();
        strokeScale = document.hasPaint() ? document.paintScale.sanitised() : scale;
        strokeTo (position);
    }

    /** Canvas height (0 = top, 1 = bottom; beyond for points off the canvas) to paint-layer
        rows. The layer's rows run evenly over the scale it was painted on, so the height goes
        through its frequency: paint lands at the frequency under the cursor, where the
        compositor draws it back. */
    double toPaintY (double normY) const
    {
        return canvasToPaintY (normY, scale, strokeScale);
    }

    juce::Point<float> toPaintLayer (juce::Point<float> screen) const
    {
        // Pixel (x, y) of the paint layer is measured at its centre, so normX * width puts a
        // dab exactly where the cursor is.
        const auto n = toNorm (screen);
        return { n.x * static_cast<float> (ArtDocument::paintWidth), static_cast<float> (toPaintY (static_cast<double> (n.y))) };
    }

    /** The paint-layer radius in y of a dab `radius` screen pixels round: half the paint rows
        its height on screen covers, so it stays round on screen where the canvas scale
        stretches or squeezes the paint's. */
    float paintRadiusY (juce::Point<float> screen, float radius) const
    {
        const auto height = juce::jmax (1.0f, area().getHeight());

        if (strokeScale == scale)
            return radius / height * static_cast<float> (ArtDocument::paintHeight);

        const auto y = static_cast<double> (toNorm (screen).y);
        const auto r = static_cast<double> (radius / height);
        return static_cast<float> (0.5 * std::abs (toPaintY (y + r) - toPaintY (y - r)));
    }

    void strokeTo (juce::Point<float> position)
    {
        const auto a = area();
        const auto radius = state.getBrushSize() * 0.5f;

        BrushStroke stroke;
        stroke.from = toPaintLayer (lastStrokePoint);
        stroke.to = toPaintLayer (position);
        stroke.radiusX = radius / juce::jmax (1.0f, a.getWidth()) * static_cast<float> (ArtDocument::paintWidth);
        stroke.radiusY = paintRadiusY (position, radius);
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
        const auto paintScale = strokeScale;

        processor.editDocument ([&] (ArtDocument& document)
        {
            const auto size = static_cast<size_t> (ArtDocument::paintWidth * ArtDocument::paintHeight);

            if (document.paint.size() != size)
                document.paint.assign (size, 0);

            document.paintScale = paintScale;

            // Painting on a hidden paint layer shows it again: a stroke that cannot be seen or
            // heard would look like a broken brush.
            document.paintVisible = true;

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
            owner.setMouseCursor (cursorForHandle (*selectedObject(), handle));
            return;
        }

        if (isOnRotateHandle (p))
        {
            setHovered ({});
            owner.setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
            return;
        }

        const auto* object = objectToGrab (p);
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
        // The length, the host's time signature and where in the song the drawing plays change
        // no plan, and a scale change shows before the new plan arrives.
        const auto newLength = currentLengthIndex();
        const auto newScale = processor.getScale();
        const auto newSignature = processor.getTimeSignature();
        const auto newFirstBar = processor.getSinglePassStartBar();

        // What the previews show also depends on Level, the length in seconds (the analyser's
        // window blurs a shorter pass more), the analyser's settings and the sample rate; and in
        // ArtFreq FX on the mode and the depth.
        const auto newMode = processor.getEffectMode();
        const auto newDepth = previewDepthDb (processor.getDepthDb());
        const auto newGain = previewGain (processor.getOutputGain());
        const auto newView = processor.getAnalyserView();
        const auto newSlope = processor.getPlanSettings().slopeDbPerOctave;
        const auto newRate = processor.getCurrentSampleRate();
        const auto newPass = previewSeconds (processor.getLengthSeconds());

        if (newLength != lengthIndex || newScale != scale || newSignature != timeSignature || newFirstBar != firstBar
            || newMode != effectMode || ! juce::exactlyEqual (newDepth, depthDb) || ! juce::exactlyEqual (newGain, outputGain)
            || newView != view || ! juce::exactlyEqual (newSlope, slope) || ! juce::exactlyEqual (newRate, sampleRate)
            || ! juce::exactlyEqual (newPass, passSeconds))
        {
            lengthIndex = newLength;
            scale = newScale;
            timeSignature = newSignature;
            firstBar = newFirstBar;
            effectMode = newMode;
            depthDb = newDepth;
            outputGain = newGain;
            view = newView;
            slope = newSlope;
            sampleRate = newRate;
            passSeconds = newPass;
            owner.repaint();
        }
    }

    // ---- the previews ----------------------------------------------------------------------
    //
    // The fast preview (CanvasPlanImage, EffectPreviewImage) is drawn from the plan in a few ms
    // and follows every drag. The measured preview (MeasuredPreview) is the real sound, rendered
    // and analysed in the background a moment after the last change. The canvas shows:
    //
    //  - the measured preview when it is of what is drawn and set now;
    //  - the last measured preview while an update is on its way that changes only the picture's
    //    size, the colours, the take of the user's song (ArtFreq FX) or the instrument's Level
    //    where that only moves the analysis by as many dB (measuredStillShows): it is then still
    //    the right picture, and the update takes a moment;
    //  - otherwise the fast preview, which follows any change within a frame: before the first
    //    measurement, and from any change to the drawing or to what is heard or how it is
    //    analysed (painting, dragging, typing, undo, a mode, a knob turned by the mouse, the
    //    wheel, the keyboard or the host) until the new measurement is in.
    //
    // A new editor window starts with what the last one measured, when it is still of the drawing
    // (the processor keeps it: PreviewMemory). Between the pictures the canvas cross-fades. The
    // selection, the handles, the outlines and the brush cursor are drawn on top as always.

    AnalyserModel analyserModel() const
    {
        AnalyserModel model;
        model.scale = scale;
        model.slopeDbPerOctave = slope;
        model.view = view;
        model.sampleRate = sampleRate;
        model.passSeconds = passSeconds;
        model.outputGain = outputGain;
        return model;
    }

    /** What the fast preview is to show at a size (image pixels). */
    FastRequest makeFastRequest (int width, int height) const
    {
        FastRequest request;
        request.effect = processor.isEffect();
        request.plan = plan;
        request.model = analyserModel();
        request.width = width;
        request.height = height;

        if (request.effect)
        {
            // ArtFreq FX: over the song, as the mode will show it.
            request.settings.mode = effectMode;
            request.settings.depthDb = depthDb;
            request.settings.view = view;
            request.settings.sampleRate = sampleRate;
            request.settings.passSeconds = passSeconds;
            request.masks = processor.getCurrentMasks();
            request.song = fastSong;
        }

        // The last measurement, shown where the drawing did not change near since.
        request.measured = fastMeasured;

        if (fastMeasured != nullptr)
            request.measuredRequest = measuredRequest;

        return request;
    }

    /** The fast preview at a size (image pixels): made on its own thread, the newest there is. */
    juce::Image fastPicture (int width, int height)
    {
        return fast.get (makeFastRequest (width, height), fastAtOnce);
    }

    MeasureRequest makeRequest (int width, int height) const
    {
        MeasureRequest request;
        request.effect = processor.isEffect();
        request.plan = plan;
        request.masks = request.effect ? processor.getCurrentMasks() : nullptr;
        request.mode = effectMode;
        request.depthDb = depthDb;
        request.outputGain = outputGain;

        // ArtFreq FX's Carve and Stencil play no sines: neither the plan nor Level changes their
        // sound (Density, Contrast and Lows rebuild the plan, not the masks), so a change to them
        // renders nothing again.
        if (request.effect && ! playsSines (effectMode))
        {
            request.plan = nullptr;
            request.outputGain = 1.0f;
        }
        request.passSeconds = passSeconds;
        request.sampleRate = sampleRate;

        // ArtFreq FX: the user's own song, once a pass of it has played.
        if (request.effect)
            if (auto capture = processor.getSongCapture(); capture != nullptr && capture->getSerial() != 0)
            {
                request.songSerial = capture->getSerial();
                request.song = std::move (capture);
            }

        request.scale = scale;
        request.slopeDbPerOctave = slope;
        request.view = view;
        request.width = width;
        request.height = height;
        request.documentRevision = processor.getDocumentRevision();
        return request;
    }

    /** Shows a finished measurement (cross-fading to it if it is to be seen and `fade`). */
    void installMeasurement (std::unique_ptr<MeasuredPreview::Result> result, bool fade)
    {
        const auto& r = result->request;
        juce::Image image (juce::Image::RGB, juce::jmax (1, r.width), juce::jmax (1, r.height), false);

        if (result->pixels.size() == static_cast<size_t> (r.width) * static_cast<size_t> (r.height))
        {
            juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < r.height; ++y)
                writePixelRow (data, y, result->pixels.data() + static_cast<size_t> (y) * static_cast<size_t> (r.width), r.width);
        }

        // A newer measurement while one is on screen: cross-fade from it.
        if (fade && shown == Shown::measured && measuredImage.isValid())
            startFade (Shown::measured, false, measuredImage, fadeToMeasuredSeconds);
        else if (! fade)
            fading = false;

        measuredImage = image;
        measuredRequest = r;
        measuredAnalysis = result->analysis;
        hasMeasured = true;
        lastMeasureMilliseconds = result->milliseconds;
        updateMeasuredSong();

        // Kept by the processor for the next editor window.
        auto memory = std::make_shared<PreviewMemory>();
        memory->request = r;
        memory->analysis = measuredAnalysis;
        memory->image = measuredImage;
        processor.getPreviewMemory() = std::move (memory);

        owner.repaint (area().getSmallestIntegerContainer());
    }

    /** A new canvas takes over what the last editor window measured (the processor keeps it):
        shown at once if it is still of the drawing and settings (at another size, stretched,
        until the new size is in), and the background render starts from it. */
    void recallMeasurement (const MeasureRequest& request)
    {
        const auto memory = processor.getPreviewMemory();

        if (memory == nullptr || memory->analysis == nullptr || ! memory->image.isValid())
            return;

        measured.adopt (memory->request, memory->analysis);

        if (! memory->request.sameExcept (request, MeasureRequest::colours | MeasureRequest::size | MeasureRequest::songTake))
            return;

        measuredImage = memory->image;
        measuredRequest = memory->request;
        measuredAnalysis = memory->analysis;
        hasMeasured = true;
        shown = Shown::measured;
        fading = false;
        fadeFromImage = {};
        updateMeasuredSong();
        owner.repaint (area().getSmallestIntegerContainer());
    }

    /** Whether the measured picture on screen still shows the sound asked for now, though not
        exactly: only the picture's size, the colours, the take of the user's song (the same song,
        another pass of it) or, where the new analysis is the old one moved by as many dB, the
        instrument's Level differ. It then stays until the new one is in (a moment). Any other
        change (the drawing, mode, Depth, Density, Contrast, Lows, Length, tempo, the analyser's
        FFT, Level where the clipper or the song is involved) goes to the fast preview at once:
        it is current within a frame, whether the change came from the mouse, the wheel, the
        keyboard or the host. */
    bool measuredStillShows (const MeasureRequest& request) const
    {
        if (! hasMeasured)
            return false;

        if (measuredRequest.sameExcept (request, MeasureRequest::colours | MeasureRequest::size | MeasureRequest::songTake))
            return true;

        auto sameSize = request;
        sameSize.width = measuredRequest.width;
        sameSize.height = measuredRequest.height;
        return measuredAnalysis != nullptr && canRelevel (*measuredAnalysis, measuredRequest, sameSize);
    }

    /** The fast preview borrows from the last measurement made with the scale, slope, FFT, rate
        and length it draws now: ArtFreq FX the song as analysed (instead of the stand-in's
        texture), and, at the same height, the picture itself where the drawing did not change
        near since (FastPreview::makePatch). */
    void updateMeasuredSong()
    {
        const auto& r = measuredRequest;
        const auto compatible = hasMeasured && measuredAnalysis != nullptr && r.scale == scale && r.view.fftSize == view.fftSize
                             && juce::exactlyEqual (r.sampleRate, sampleRate) && juce::exactlyEqual (r.passSeconds, passSeconds)
                             && juce::exactlyEqual (r.slopeDbPerOctave, slope);

        fastMeasured = compatible && r.height == pictureHeight ? measuredAnalysis : nullptr;

        if (processor.isEffect())
            fastSong = compatible ? measuredAnalysis : nullptr;
    }

    void startFade (Shown target, bool fromFast, juce::Image fromImage, double seconds)
    {
        fadeFromFast = fromFast;
        fadeFromImage = std::move (fromImage);
        fadeStart = nowSeconds();
        fadeSeconds = seconds;
        fading = true;
        shown = target;
    }

    float fadeProgress() const
    {
        return fading ? static_cast<float> (juce::jlimit (0.0, 1.0, (nowSeconds() - fadeStart) / fadeSeconds)) : 1.0f;
    }

    /** From the timer: asks for a measurement of what is shown, takes one that is in, and decides
        what the canvas shows. */
    void updateMeasured()
    {
        if (pictureWidth <= 0 || pictureHeight <= 0)
            return;   // not painted yet

        const auto request = makeRequest (pictureWidth, pictureHeight);
        lastRequest = request;

        // Only while the canvas can be seen, and has been for a moment (an editor opened and
        // closed at once, as host scans and validators do, renders nothing); not before the plan
        // is rebuilt (the request would name the new drawing with the old plan).
        const auto now = nowSeconds();
        const auto showing = owner.isShowing();

        if (! showing)
            shownSince = -1.0;
        else if (shownSince < 0.0)
            shownSince = now;

        const auto settled = showing && now - shownSince >= openingQuietSeconds;

        if (! recalled)
        {
            recalled = true;
            recallMeasurement (request);
        }

        // ArtFreq FX keeps the song coming in from now on, for the next measurement to carve.
        if (settled)
            processor.enableSongCapture();

        measured.update (request, settled && ! processor.isPlanPending());

        if (auto result = measured.takeResult())
            installMeasurement (std::move (result), true);

        updateMeasuredSong();

        auto target = shown;

        if (hasMeasured && measuredRequest == request)
            target = Shown::measured;
        else if (shown == Shown::measured && ! measuredStillShows (request))
            target = Shown::fast;

        // The fast preview is asked for here too, not only when painted: its thread starts on a
        // change at once, and the picture is in by the time the canvas next paints.
        if (target == Shown::fast || fading)
            fast.get (makeFastRequest (pictureWidth, pictureHeight), fastAtOnce);

        if (target != shown)
        {
            if (target == Shown::measured)
                startFade (Shown::measured, true, {}, fadeToMeasuredSeconds);
            else
                startFade (Shown::fast, false, measuredImage, fadeToFastSeconds);

            owner.repaint (area().getSmallestIntegerContainer());
        }

        if (fading)
        {
            if (fadeProgress() >= 1.0f)
            {
                fading = false;
                fadeFromImage = {};
            }

            owner.repaint (area().getSmallestIntegerContainer());
        }

        const auto text = statusText();

        if (text != shownStatus)
        {
            // The whole time axis: the bar numbers make room for the line, or take it back.
            shownStatus = text;
            const auto a = area();
            owner.repaint (juce::Rectangle<float> (0.0f, a.getBottom(), static_cast<float> (owner.getWidth()), static_cast<float> (axisHeight))
                               .getSmallestIntegerContainer());
        }
    }

    bool measurementIsCurrent() const
    {
        return hasMeasured && shown == Shown::measured && measuredRequest == lastRequest;
    }

    /** ArtFreq FX: the picture shown carves the user's own song (else the stand-in). */
    bool showsUsersSong() const
    {
        return hasMeasured && measuredAnalysis != nullptr && measuredAnalysis->songSerial != 0;
    }

    juce::String statusText() const
    {
        if (measurementIsCurrent())
        {
            if (! processor.isEffect())
                return "Preview of the real sound";

            return showsUsersSong() ? "Real output, on your track's last pass" : "Real output, on a stand-in song";
        }

        return "Updating" + juce::String::fromUTF8 ("\xe2\x80\xa6");
    }

    /** Where the status line goes: the right end of the time axis, under the drawing. */
    juce::Rectangle<float> statusArea() const
    {
        const auto a = area();
        const auto width = juce::jmin (a.getWidth() * 0.5f, 220.0f);
        return { a.getRight() - width, a.getBottom() + 3.0f, width, static_cast<float> (axisHeight) - 5.0f };
    }

    /** The status line as it fits after the time labels: in full where there is room, else in
        fewer words ("Real sound"; "Real output, stand-in song", "Stand-in song"), so it never
        hides a bar number. */
    juce::String displayedStatus() const
    {
        if (shownStatus.isEmpty())
            return {};

        const auto a = area();
        const auto font = fonts::ui (11.0f);
        auto labelsEnd = a.getX();

        for (const auto& mark : axes::timeMarks (lengthIndex, timeSignature.numerator, timeSignature.denominator, a.getWidth(), firstBar))
        {
            if (mark.label.isEmpty())
                continue;

            const auto end = std::floor (a.getX() + static_cast<float> (mark.fraction) * a.getWidth()) + 4.0f
                           + juce::GlyphArrangement::getStringWidth (axes::labelFont(), mark.label);

            if (end <= a.getRight())
                labelsEnd = std::max (labelsEnd, end);
        }

        const auto room = a.getRight() - labelsEnd - 12.0f;

        if (shownStatus.startsWith ("Updating"))
            return shownStatus;

        const auto candidates = ! processor.isEffect() ? juce::StringArray { shownStatus, "Real sound" }
                              : showsUsersSong()       ? juce::StringArray { shownStatus, "Real output, your track", "Your track" }
                                                       : juce::StringArray { shownStatus, "Real output, stand-in song", "Stand-in song" };

        for (const auto& text : candidates)
            if (juce::GlyphArrangement::getStringWidth (font, text) <= room)
                return text;

        return candidates[candidates.size() - 1];
    }

    void paintStatus (juce::Graphics& g)
    {
        const auto text = displayedStatus();

        if (text.isEmpty())
            return;

        g.setFont (fonts::ui (11.0f));
        g.setColour (measurementIsCurrent() ? theme::textFaint : theme::textDim);
        g.drawText (text, statusArea(), juce::Justification::centredRight, true);
    }

    /** Where the time labels must stop, so they never run into the status line (when even its
        fewest words do not fit after them). */
    float statusStart() const
    {
        const auto text = displayedStatus();

        if (text.isEmpty())
            return 1.0e9f;

        return statusArea().getRight() - juce::GlyphArrangement::getStringWidth (fonts::ui (11.0f), text) - 10.0f;
    }

    void paintPicture (juce::Graphics& g, juce::Rectangle<float> a, int width, int height)
    {
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);

        const auto showFast = shown == Shown::fast || ! measuredImage.isValid();
        const auto current = showFast ? fastPicture (width, height) : measuredImage;

        if (fading && fadeProgress() < 1.0f)
        {
            const auto from = fadeFromFast || ! fadeFromImage.isValid() ? fastPicture (width, height) : fadeFromImage;
            g.drawImage (from, a);
            g.setOpacity (fadeProgress());
            g.drawImage (current, a);
            g.setOpacity (1.0f);
            return;
        }

        g.drawImage (current, a);
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
            pictureWidth = juce::jmax (1, juce::roundToInt (a.getWidth() * pixelScale));
            pictureHeight = juce::jmax (1, juce::roundToInt (a.getHeight() * pixelScale));
            paintPicture (g, a, pictureWidth, pictureHeight);

            const auto marks = axes::timeMarks (lengthIndex, timeSignature.numerator, timeSignature.denominator, a.getWidth(), firstBar);
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
        paintStatus (g);
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
            if (object.id == selectedId || ! object.visible)
                continue;

            const auto isHovered = object.id == hovered && state.getTool() == Tool::select;
            g.setColour (juce::Colours::white.withAlpha (isHovered ? 0.55f : 0.22f));

            if (isUpright (object))
                drawDashedRectangle (g, screenRectOf (object), 4.0f);
            else
                drawDashedPolygon (g, screenCorners (object), 4.0f);
        }

        if (const auto* object = selectedObject())
        {
            const auto r = screenRectOf (*object);
            const auto upright = isUpright (*object);

            if (object->visible)
            {
                g.setColour (theme::accent);

                if (upright)
                    g.drawRect (juce::Rectangle<float> (std::floor (r.getX()), std::floor (r.getY()),
                                                        std::floor (r.getRight()) - std::floor (r.getX()),
                                                        std::floor (r.getBottom()) - std::floor (r.getY())), 1.0f);
                else
                    g.strokePath (polygonOf (screenCorners (*object)), juce::PathStrokeType (1.0f));
            }
            else
            {
                // A hidden object (picked in the layer list) is silent and not drawn: a faint
                // dashed outline and a tag say where it is, so it can still be moved.
                g.setColour (theme::accent.withAlpha (0.6f));

                if (upright)
                    drawDashedRectangle (g, r, 3.0f);
                else
                    drawDashedPolygon (g, screenCorners (*object), 3.0f);

                if (dragMode != DragMode::move && dragMode != DragMode::resize && dragMode != DragMode::rotate)
                    paintHiddenTag (g, screenBoundsOf (*object));
            }

            if (dragMode == DragMode::move || dragMode == DragMode::resize)
                paintPlacementTag (g, *object, screenBoundsOf (*object));
            else if (dragMode == DragMode::rotate)
                paintRotationTag (g, *object, screenBoundsOf (*object));
        }
    }

    /** "Hidden", just inside the top-left corner of a hidden selected object (clear of its
        handle), kept on the canvas. */
    void paintHiddenTag (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto font = fonts::ui (11.0f, fonts::UiWeight::medium);
        const auto text = juce::String ("Hidden");
        const auto width = 6.0f + 12.0f + 4.0f + juce::GlyphArrangement::getStringWidth (font, text) + 8.0f;
        const auto a = area();
        auto tag = juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 8.0f, width, 18.0f);
        tag.setPosition (clampTo (tag.getX(), a.getX() + 2.0f, a.getRight() - width - 2.0f),
                         clampTo (tag.getY(), a.getY() + 2.0f, a.getBottom() - 20.0f));
        tag = tag.withPosition (std::round (tag.getX()), std::round (tag.getY()));

        g.setColour (theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (theme::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (tag.reduced (0.5f), 4.0f, 1.0f);

        g.setColour (theme::textDim);
        g.fillPath (icons::eyeOff(), juce::AffineTransform::scale (12.0f / 24.0f).translated (tag.getX() + 6.0f, tag.getY() + 3.0f));
        g.setFont (font);
        g.drawText (text, tag.withTrimmedLeft (22.0f), juce::Justification::centredLeft, false);
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

    /** While turning: the angle. */
    void paintRotationTag (juce::Graphics& g, const ArtObject& object, juce::Rectangle<float> r)
    {
        const auto text = juce::String (juce::roundToInt (ArtDocument::normaliseRotation (object.rotation)))
                        + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        const auto font = fonts::mono (11.0f);
        const auto width = juce::GlyphArrangement::getStringWidth (font, text) + 12.0f;
        const auto a = area();
        auto tag = juce::Rectangle<float> (r.getCentreX() - width * 0.5f, r.getBottom() + 8.0f, width, 18.0f);

        if (tag.getBottom() > a.getBottom() - 2.0f)
            tag.setY (r.getCentreY() - 9.0f);  // no room below: in the middle

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
            // The rotate handle: a knob on a short stem off the top edge.
            const auto handle = rotateHandleOf (*object);
            g.setColour (theme::accent);
            g.drawLine ({ handle.stem, handle.knob }, 1.0f);
            const auto knob = juce::Rectangle<float> (rotateKnobRadius * 2.0f, rotateKnobRadius * 2.0f).withCentre (handle.knob);
            g.setColour (theme::text);
            g.fillEllipse (knob);
            g.setColour (theme::accent);
            g.drawEllipse (knob, 1.0f);

            for (const auto c : turnedHandleCentres (*object))
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
            axes::drawTimeLabels (g, timeAxis, a, axes::timeMarks (lengthIndex, timeSignature.numerator,
                                                                    timeSignature.denominator, a.getWidth(), firstBar),
                                  statusStart());
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
    ArtFreqProcessor& processor;
    EditorState& state;
    std::atomic<float>* lengthParameter = nullptr;

    // The fast preview, made on its own thread; the canvas repaints when a new picture is in.
    FastPreview fast { [this] { owner.repaint (area().getSmallestIntegerContainer()); } };
    std::shared_ptr<const MeasuredSpectrogram> fastSong, fastMeasured;   // see updateMeasuredSong
    bool fastAtOnce = false;              // the snapshot tool: made while painting
    std::shared_ptr<const ArtPlan> plan;  // kept alive while it is shown
    FrequencyScale scale;
    EffectMode effectMode = EffectMode::carve;
    float depthDb = 0.0f;
    float outputGain = 1.0f;              // Level, as a gain
    AnalyserView view;                    // the user's analyser (Settings)
    float slope = 0.0f;
    double sampleRate = 48000.0;
    double passSeconds = 8.0;

    // The measured preview
    MeasuredPreview measured;
    juce::Image measuredImage;
    MeasureRequest measuredRequest, lastRequest;
    std::shared_ptr<const MeasuredSpectrogram> measuredAnalysis;
    bool hasMeasured = false;
    bool recalled = false;   // the processor's memory of the last window's measurement was looked at
    double lastMeasureMilliseconds = 0.0;
    int pictureWidth = 0, pictureHeight = 0;   // image pixels, as last painted

    Shown shown = Shown::fast;
    bool fading = false;
    bool fadeFromFast = true;
    juce::Image fadeFromImage;
    double fadeStart = 0.0, fadeSeconds = fadeToMeasuredSeconds;
    juce::String shownStatus;
    double shownSince = -1.0;   // when the canvas came on screen (seconds), -1 while it is not
    int lengthIndex = params::defaultLengthIndex;
    juce::AudioPlayHead::TimeSignature timeSignature;  // the host's, for the bar and beat grid
    int firstBar = 0;   // the song's bar the drawing plays at (a single Transport pass), else 0

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
        pendingRotate,  // ... on the rotate handle
        move,
        resize,
        rotate,
        paint
    };

    DragMode dragMode = DragMode::none;
    juce::Uuid dragObject = juce::Uuid::null();
    juce::Rectangle<float> dragStartRect;   // normalised
    float dragStartTime = 0.0f, dragEndTime = 1.0f;
    HzRange dragStartHz;
    float dragStartRotation = 0.0f;         // degrees
    juce::Rectangle<float> dragStartBounds; // normalised: the turned box's bounding box
    float dragStartAngle = 0.0f;            // rotating: the mouse's angle round the centre at the start
    juce::Point<float> dragStartMouse;
    int dragEdges = 0;
    juce::String dragKey;
    int editCounter = 0;

    std::vector<uint8_t> strokeBefore;
    ArtDocument strokeMask;
    FrequencyScale strokeScale;  // the scale the stroke's paint layer is painted on
    bool strokeErase = false;
    float strokeStrength = 1.0f;
    float strokeSoftness = 0.5f;
    juce::String strokeKey;
    juce::Point<float> lastStrokePoint;

    // Clicking again on the same spot, without moving the mouse in between, picks the next
    // object down (see nextObjectBelow): where the last plain click was, and what it selected.
    bool hasLastClick = false;
    juce::Point<float> lastClickPoint;
    juce::Uuid lastClickSelection = juce::Uuid::null();
    bool cycleOnRelease = false;
    bool typeOnRelease = false;   // a double-click on text: its text box gets the focus on the release

    // [ and ] turns: presses closer together than nudgeSessionGap are one undo step.
    double lastTurnKeyTime = -1.0e9;
    int turnKeySession = 0;
    juce::Uuid turnKeyObject = juce::Uuid::null();

    // Arrow-key nudges: one undo step per gesture (see keyPressed).
    double lastNudgeTime = -1.0e9;
    int nudgeSession = 0;
    bool nudging = false;
    juce::Uuid nudgeObject = juce::Uuid::null();
};

//==============================================================================
CanvasView::CanvasView (ArtFreqProcessor& processor, EditorState& state)
    : impl (std::make_unique<Impl> (*this, processor, state))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setKeepsKeyFocus (*this);   // Delete, the arrows and Esc act on the selection from here
    setTitle ("Canvas");
    setDescription ("The drawing: time across, frequency up. Click an object to select it; Alt-click, or click "
                    "again, for the one underneath; right-click for its menu. Drag the round handle above the "
                    "selection to turn it (Shift: 15 degree steps; [ and ] too); Shift-drag a corner to keep the "
                    "proportions.");

    processor.addChangeListener (this);
    state.addChangeListener (this);
    startTimerHz (framesPerSecond);
}

CanvasView::~CanvasView()
{
    stopTimer();
    impl->processor.removeChangeListener (this);
    impl->state.removeChangeListener (this);

    // The processor outlives the editor: an edit made in the next one must not join this one's
    // last undo step (the keys are counted from 1 again there).
    impl->processor.endCoalescing();
}

juce::Rectangle<float> CanvasView::getDrawingArea() const
{
    return axes::plotArea (getLocalBounds(), axisWidth, axisHeight).toFloat();
}

juce::Rectangle<float> CanvasView::getObjectBounds (const juce::Uuid& id) const
{
    const auto* object = impl->processor.getDocument().find (id);

    if (object == nullptr)
        return {};

    auto bounds = impl->screenBoundsOf (*object).expanded (handleReach);

    // The selected object's rotate handle too (it is drawn only with the Select tool).
    if (id == impl->state.getSelected() && impl->state.getTool() == Tool::select)
        bounds = bounds.getUnion (juce::Rectangle<float> (rotateHitRadius * 2.0f, rotateHitRadius * 2.0f)
                                      .withCentre (impl->rotateHandleOf (*object).knob));

    return bounds;
}

juce::Point<float> CanvasView::getRotateHandle (const juce::Uuid& id) const
{
    const auto* object = impl->processor.getDocument().find (id);
    return object != nullptr ? impl->rotateHandleOf (*object).knob : juce::Point<float>();
}

std::array<juce::Point<float>, 8> CanvasView::getHandleCentres (const juce::Uuid& id) const
{
    const auto* object = impl->processor.getDocument().find (id);
    return object != nullptr ? impl->turnedHandleCentres (*object) : std::array<juce::Point<float>, 8> {};
}

void CanvasView::paint (juce::Graphics& g)
{
    impl->paint (g);
}

void CanvasView::resized()
{
    repaint();
}

//==============================================================================
void CanvasView::mouseMove (const juce::MouseEvent& e)
{
    // Moving away ends "clicking again on the same spot".
    if (impl->hasLastClick && e.position.getDistanceFrom (impl->lastClickPoint) >= dragThreshold)
        impl->hasLastClick = false;

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
    d.nudging = false;
    d.cycleOnRelease = false;
    d.typeOnRelease = false;

    // Right-click on an object: select it and offer Bring to front, Send to back, Hide, Delete.
    if (e.mods.isPopupMenu())
    {
        d.hasLastClick = false;

        if (! d.brushActive())
        {
            if (const auto* object = d.objectToGrab (e.position))
            {
                const auto id = object->id;
                d.state.setSelected (id);
                actions::showObjectMenu (d.processor, d.state, id, *this, e.position.toInt());
            }
        }

        return;
    }

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

    if (d.isOnRotateHandle (e.position))
    {
        d.rememberDragStart (*d.selectedObject());
        d.dragStartAngle = d.angleFromCentre (e.position);
        d.dragMode = Impl::DragMode::pendingRotate;
        return;
    }

    // Alt (Option) - click picks the next object down under the mouse, so one hidden behind
    // another can be reached; so does a second click on the same spot (on its release, so a
    // press and drag still moves the selection).
    const auto altClick = e.mods.isAltDown();

    if (const auto* object = altClick ? d.nextObjectBelow (e.position) : d.objectToGrab (e.position))
    {
        const auto clickedAgain = ! altClick && d.hasLastClick && object->id == d.state.getSelected()
                               && object->id == d.lastClickSelection
                               && e.position.getDistanceFrom (d.lastClickPoint) < dragThreshold;

        d.state.setSelected (object->id);
        d.rememberDragStart (*object);
        d.dragMode = Impl::DragMode::pendingMove;
        d.cycleOnRelease = clickedAgain;

        // Double-clicking text goes straight to typing: on the release, as every click that
        // gives a text box the focus does (a press anywhere else is what ends typing).
        if (e.getNumberOfClicks() >= 2 && object->type == ArtObject::Type::text && d.state.focusTextField != nullptr)
        {
            d.dragMode = Impl::DragMode::none;
            d.cycleOnRelease = false;
            d.typeOnRelease = true;
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

    if (d.dragMode == Impl::DragMode::pendingMove || d.dragMode == Impl::DragMode::pendingResize
        || d.dragMode == Impl::DragMode::pendingRotate)
    {
        if (e.position.getDistanceFrom (d.dragStartMouse) < dragThreshold)
            return;

        d.dragMode = d.dragMode == Impl::DragMode::pendingMove ? Impl::DragMode::move
                   : d.dragMode == Impl::DragMode::pendingResize ? Impl::DragMode::resize
                                                                 : Impl::DragMode::rotate;
        d.dragKey = "canvas-drag-" + juce::String (++d.editCounter);

        if (const auto* object = d.processor.getDocument().find (d.dragObject))
            setMouseCursor (d.dragMode == Impl::DragMode::move ? juce::MouseCursor::DraggingHandCursor
                          : d.dragMode == Impl::DragMode::rotate ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                                                 : (Impl::isUpright (*object) ? cursorForEdges (d.dragEdges)
                                                                                              : cursorForTurnedEdges (d.dragEdges, object->rotation)));
    }

    if (d.dragMode != Impl::DragMode::move && d.dragMode != Impl::DragMode::resize && d.dragMode != Impl::DragMode::rotate)
        return;

    if (d.processor.getDocument().find (d.dragObject) == nullptr)
    {
        d.dragMode = Impl::DragMode::none;  // undone or deleted meanwhile
        return;
    }

    // Turning: by the angle the mouse has gone round the box's centre (in display space), so
    // grabbing the handle a little off its middle does not jump; Shift snaps to 15 degrees.
    if (d.dragMode == Impl::DragMode::rotate)
    {
        auto degrees = ArtDocument::normaliseRotation (d.dragStartRotation + d.angleFromCentre (e.position) - d.dragStartAngle);

        if (e.mods.isShiftDown())
            degrees = ArtDocument::normaliseRotation (std::round (degrees / rotateSnapDegrees) * rotateSnapDegrees);

        d.applyRotation (degrees, d.dragKey);
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
    // Shift while resizing keeps the proportions (letting go of it frees them again).
    const auto r = d.dragMode == Impl::DragMode::move ? d.movedRect (deltaNorm) : d.resizedRect (deltaNorm, e.mods.isShiftDown());
    d.applyPlacement (r, d.dragKey);
}

void CanvasView::mouseUp (const juce::MouseEvent& e)
{
    auto& d = *impl;
    const auto wasEditing = d.dragMode == Impl::DragMode::move || d.dragMode == Impl::DragMode::resize
                         || d.dragMode == Impl::DragMode::rotate || d.dragMode == Impl::DragMode::paint;
    const auto clickedObject = d.dragMode == Impl::DragMode::pendingMove;   // pressed on one, never moved
    d.dragMode = Impl::DragMode::none;

    if (clickedObject && d.cycleOnRelease)
        if (const auto* next = d.nextObjectBelow (d.dragStartMouse))
            d.state.setSelected (next->id);

    d.cycleOnRelease = false;
    d.hasLastClick = clickedObject;
    d.lastClickPoint = d.dragStartMouse;
    d.lastClickSelection = d.state.getSelected();

    if (std::exchange (d.typeOnRelease, false) && d.state.focusTextField != nullptr)
        d.state.focusTextField();

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

    // [ and ] turn the selection 15 degrees anticlockwise and clockwise (Shift: 1 degree, the
    // keys then typing { and } on most layouts).
    const auto character = key.getTextCharacter();
    const auto turn = character == '[' || character == '{' ? -1 : character == ']' || character == '}' ? 1 : 0;

    if (turn != 0)
    {
        const auto* object = d.selectedObject();

        if (object == nullptr || d.dragMode != Impl::DragMode::none)
            return object != nullptr;

        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

        if (object->id != d.turnKeyObject || now - d.lastTurnKeyTime > nudgeSessionGap)
            ++d.turnKeySession;

        d.turnKeyObject = object->id;
        d.lastTurnKeyTime = now;
        d.dragObject = object->id;
        d.applyRotation (object->rotation + static_cast<float> (turn) * (mods.isShiftDown() ? 1.0f : rotateKeyDegrees),
                         "canvas-turn-" + juce::String (d.turnKeySession));
        return true;
    }

    const auto left = code == juce::KeyPress::leftKey, right = code == juce::KeyPress::rightKey;
    const auto up = code == juce::KeyPress::upKey, down = code == juce::KeyPress::downKey;

    if (! (left || right || up || down))
        return false;

    const auto* object = d.selectedObject();

    if (object == nullptr || d.dragMode != Impl::DragMode::none)
        return object != nullptr;

    // One screen pixel per press (ten with Shift).
    const auto step = mods.isShiftDown() ? 10.0f : 1.0f;
    const auto a = getDrawingArea();
    const auto deltaNorm = juce::Point<float> ((right ? step : left ? -step : 0.0f) / juce::jmax (1.0f, a.getWidth()),
                                               (down ? step : up ? -step : 0.0f) / juce::jmax (1.0f, a.getHeight()));

    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // A nudge gesture (an arrow held down, repeating) is one undo step. It ends when the arrows
    // are let go (keyStateChanged), on a click, on another object or after a pause; any other
    // edit in between also starts a new step, as its own key ends the coalescing.
    if (! d.nudging || object->id != d.nudgeObject || now - d.lastNudgeTime > nudgeSessionGap)
        ++d.nudgeSession;

    d.nudging = true;
    d.nudgeObject = object->id;
    d.lastNudgeTime = now;
    d.scale = d.processor.getScale();
    d.rememberDragStart (*object);
    d.applyPlacement (d.movedRect (deltaNorm), "canvas-nudge-" + juce::String (d.nudgeSession));
    return true;
}

bool CanvasView::keyStateChanged (bool)
{
    // Letting go of the arrows ends the nudge gesture: the next press is another undo step.
    if (impl->nudging && ! isArrowKeyDown())
        impl->nudging = false;

    return false;
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

    juce::File unreadable;

    for (const auto& file : files)
    {
        if (! isImageFile (file))
            continue;

        if (actions::addImageFile (d.processor, d.state, juce::File (file), position))
            return;  // one picture per drop: several would land on top of each other

        if (unreadable == juce::File())
            unreadable = juce::File (file);
    }

    if (unreadable != juce::File())
        actions::reportUnreadablePicture (d.state, unreadable);
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

    // The tool, the brush or the selection changed. Another selection ends a nudge gesture.
    if (d.state.getSelected() != d.nudgeObject)
        d.nudging = false;

    const auto mouse = getMouseXYRelative().toFloat();
    d.updateHover (mouse, isMouseOver());
    repaint();
}

void CanvasView::timerCallback()
{
    impl->updatePlayhead();
    impl->pollParameters();
    impl->updateMeasured();
}

CanvasView::PreviewStatus CanvasView::getPreviewStatus() const
{
    const auto& d = *impl;
    PreviewStatus status;
    status.measuredShown = d.shown == Impl::Shown::measured && d.measuredImage.isValid();
    status.current = d.measurementIsCurrent();
    status.fading = d.fading;
    status.fastCurrent = d.fast.isCurrent();
    status.text = d.shownStatus.isNotEmpty() ? d.shownStatus : d.statusText();
    return status;
}

void CanvasView::setFastPreviewMadeWhilePainting (bool shouldMakeWhilePainting)
{
    impl->fastAtOnce = shouldMakeWhilePainting;
    repaint();
}

int CanvasView::getFastPreviewsMadeInBackground() const noexcept
{
    return impl->fast.getBackgroundPictures();
}

void CanvasView::settleMeasuredPreview (float pixelScale)
{
    auto& d = *impl;
    d.pollParameters();
    d.processorChanged();

    const auto a = getDrawingArea();
    d.pictureWidth = juce::jmax (1, juce::roundToInt (a.getWidth() * pixelScale));
    d.pictureHeight = juce::jmax (1, juce::roundToInt (a.getHeight() * pixelScale));
    d.lastRequest = d.makeRequest (d.pictureWidth, d.pictureHeight);

    if (auto result = MeasuredPreview::renderNow (d.lastRequest))
    {
        d.installMeasurement (std::move (result), false);
        d.shown = Impl::Shown::measured;
        d.fading = false;
        d.fadeFromImage = {};
    }

    d.shownStatus = d.statusText();
    repaint();
}

} // namespace artfreq::ui
