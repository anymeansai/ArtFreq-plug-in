// Turning objects, keeping their proportions while resizing (Shift), and the picture shapes:
// the document (save, load, angles), the compositor (an upright object draws exactly as before
// rotation; a turned one where it is turned), what follows the canvas (ArtFreq's plan, ArtFreq
// FX's masks, the fast and the measured previews), and the canvas and the inspector driven like
// a user would, in both plug-ins.
//
// The editor tests need JUCE's GUI classes: on Linux run them under a virtual display.

#include "ProcessorTestUtils.h"
#include "ReferenceDrawing.h"
#include "TestFramework.h"
#include "WaitFor.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/ArtPlan.h"
#include "core/Compositor.h"
#include "core/MaskPlan.h"
#include "ui/Actions.h"
#include "ui/CanvasAxes.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/MeasuredPreview.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <limits>
#include <memory>

using namespace artfreq;

namespace
{

using Variant = ArtFreqProcessor::Variant;

std::unique_ptr<ArtFreqProcessor> makeProcessor (Variant variant = Variant::instrument)
{
    auto processor = std::make_unique<ArtFreqProcessor> (variant);
    processor->setRateAndBufferSizeDetails (48000.0, 512);
    processor->prepareToPlay (48000.0, 512);
    return processor;
}

std::unique_ptr<ArtFreqEditor> makeEditor (ArtFreqProcessor& processor)
{
    return std::unique_ptr<ArtFreqEditor> (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));
}

juce::Component* findComponent (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findComponent (*child, id))
            return found;

    return nullptr;
}

template <typename Type>
Type* find (juce::Component& root, const juce::String& id)
{
    return dynamic_cast<Type*> (findComponent (root, id));
}

/** Whether the component with that id is there and it and everything it lies in are visible. */
bool isShownAnywhere (juce::Component& root, const juce::String& id)
{
    auto* component = findComponent (root, id);

    for (auto* c = component; c != nullptr && c != &root; c = c->getParentComponent())
        if (! c->isVisible())
            return false;

    return component != nullptr;
}

juce::MouseEvent mouseEventAt (juce::Component& c, juce::Point<float> position, juce::Point<float> downPosition,
                               juce::ModifierKeys mods, bool dragged = false)
{
    const auto time = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), position, mods, juce::MouseInputSource::defaultPressure,
             0.0f, 0.0f, 0.0f, 0.0f, &c, &c, time, downPosition, time, 1, dragged };
}

void clickOn (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {})
{
    c.mouseDown (mouseEventAt (c, at, at, mods.withFlags (juce::ModifierKeys::leftButtonModifier)));
    c.mouseUp (mouseEventAt (c, at, at, mods));
}

/** A drag in eight steps; `shiftUntil` steps (of 8) are made with Shift held. */
void dragOn (juce::Component& c, juce::Point<float> from, juce::Point<float> to, int shiftUntil = 0)
{
    const auto held = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    const auto shifted = held.withFlags (juce::ModifierKeys::shiftModifier);
    c.mouseDown (mouseEventAt (c, from, from, held));

    for (int i = 1; i <= 8; ++i)
        c.mouseDrag (mouseEventAt (c, from + (to - from) * (static_cast<float> (i) / 8.0f), from, i <= shiftUntil ? shifted : held, true));

    c.mouseUp (mouseEventAt (c, to, from, {}, true));
}

/** A picture of a plain white bar (Original mode: white is loud). */
ArtObject whiteBar (float start, float end, float top, float bottom, const FrequencyScale& scale)
{
    juce::Image image (juce::Image::ARGB, 64, 16, true);
    juce::Graphics (image).fillAll (juce::Colours::white);
    const auto hz = normSpanToHz (top, bottom, scale);
    auto object = ArtDocument::makeImageObject (prepareImportedImage (image), static_cast<float> (hz.low), static_cast<float> (hz.high));
    object.startTime = start;
    object.endTime = end;
    object.imageMode = ImageMode::original;
    object.cleanup = 0.0f;
    return object;
}

/** The bar the plan, masks and previews tests turn: across 0.3 to 0.7 of the length, 8 % of the
    canvas tall, in the middle. Turned 90 degrees it stands 0.17 to 0.83 tall, 0.48 to 0.52 wide. */
ArtObject middleBar (float rotation)
{
    auto bar = whiteBar (0.3f, 0.7f, 0.46f, 0.54f, FrequencyScale {});
    bar.rotation = rotation;
    return bar;
}

// Normalised places: only the turned bar covers the first, only the upright one the second.
const juce::Rectangle<float> onlyTurned { 0.49f, 0.25f, 0.02f, 0.1f };
const juce::Rectangle<float> onlyUpright { 0.33f, 0.49f, 0.07f, 0.02f };

float meanIn (const Canvas& canvas, juce::Rectangle<float> norm)
{
    const auto r = juce::Rectangle<float> (norm.getX() * Canvas::width, norm.getY() * Canvas::height,
                                           norm.getWidth() * Canvas::width, norm.getHeight() * Canvas::height).toNearestInt();
    double sum = 0.0;

    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX(); x < r.getRight(); ++x)
            sum += canvas.get (x, y);

    return static_cast<float> (sum / juce::jmax (1, r.getWidth() * r.getHeight()));
}

/** The mean brightness of a picture over a normalised part of `area` (in the picture's pixels). */
float meanBrightness (const juce::Image& image, juce::Rectangle<float> area, juce::Rectangle<float> norm)
{
    const auto r = juce::Rectangle<float> (area.getX() + norm.getX() * area.getWidth(), area.getY() + norm.getY() * area.getHeight(),
                                           norm.getWidth() * area.getWidth(), norm.getHeight() * area.getHeight()).toNearestInt();
    double sum = 0.0;
    int count = 0;

    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX(); x < r.getRight(); ++x, ++count)
            sum += image.getPixelAt (x, y).getBrightness();

    return count > 0 ? static_cast<float> (sum / count) : 0.0f;
}

/** The plan's loudest amplitude over a normalised part of the canvas. */
float planLevelIn (const ArtPlan& plan, juce::Rectangle<float> norm)
{
    float loudest = 0.0f;
    const auto firstColumn = juce::roundToInt (norm.getX() * plan.numColumns);
    const auto lastColumn = juce::roundToInt (norm.getRight() * plan.numColumns);

    for (int c = firstColumn; c < lastColumn; ++c)
        for (int line = 0; line < plan.numLines; ++line)
        {
            const auto height = 1.0f - plan.lineNorm[static_cast<size_t> (line)];   // 0 at the top, like the canvas

            if (height >= norm.getY() && height <= norm.getBottom())
                loudest = juce::jmax (loudest, plan.amplitude (c, line));
        }

    return loudest;
}

/** The mask's largest value over a normalised part of the canvas. */
float maskLevelIn (const MaskPlan& mask, juce::Rectangle<float> norm)
{
    float largest = 0.0f;

    for (int c = juce::roundToInt (norm.getX() * mask.numColumns); c < juce::roundToInt (norm.getRight() * mask.numColumns); ++c)
        for (int row = 0; row < mask.numRows; ++row)
        {
            const auto height = 1.0f - (static_cast<float> (row) + 0.5f) / static_cast<float> (mask.numRows);

            if (height >= norm.getY() && height <= norm.getBottom())
                largest = juce::jmax (largest, mask.column (c)[row]);
        }

    return largest;
}

/** The screen position (canvas coordinates) of a normalised canvas point. */
juce::Point<float> onScreen (ui::CanvasView& canvas, juce::Point<float> norm)
{
    const auto a = canvas.getDrawingArea();
    return { a.getX() + norm.x * a.getWidth(), a.getY() + norm.y * a.getHeight() };
}

std::array<juce::Point<float>, 4> cornersOnScreen (ui::CanvasView& canvas, const ArtObject& object, const FrequencyScale& scale,
                                                   float aspect)
{
    auto corners = objectCornersNorm (object, scale, aspect);

    for (auto& c : corners)
        c = onScreen (canvas, c);

    return corners;
}

/** An editor showing one object, selected, its canvas laid out. */
struct OneObject
{
    explicit OneObject (Variant variant, const ArtObject& object)
        : processor (makeProcessor (variant))
    {
        ArtDocument document;
        document.objects.push_back (object);
        id = object.id;
        processor->setDocument (document);
        editor = makeEditor (*processor);
        canvas = find<ui::CanvasView> (*editor, "canvas");
        editor->selectObject (id);
        settle();
    }

    /** Lets the editor follow the selection (ArtFreq FX's drawer may move the canvas aside). */
    void settle()
    {
        testwait::pumpUntil ([] { return false; }, 150);
    }

    const ArtObject& object() const { return *processor->getDocument().find (id); }
    FrequencyScale scale() const { return processor->getScale(); }
    float aspect() const { return processor->getDisplayAspect(); }
    float shape() const { return displayAspectOf (object(), scale(), aspect()); }   // in display space
    juce::Point<float> centreOnScreen() const { return onScreen (*canvas, objectRectNorm (object(), scale()).getCentre()); }
    std::array<juce::Point<float>, 4> corners() const { return cornersOnScreen (*canvas, object(), scale(), aspect()); }

    std::unique_ptr<ArtFreqProcessor> processor;
    std::unique_ptr<ArtFreqEditor> editor;
    ui::CanvasView* canvas = nullptr;
    juce::Uuid id;
};

const char* nameOf (Variant variant) { return variant == Variant::instrument ? "ArtFreq" : "ArtFreq FX"; }

/** An object's box as the canvas shows it, from its corner handles (top-left, top-right,
    bottom-left, as it is turned): its two sides from the top-left corner, in screen pixels. */
struct BoxOnScreen
{
    juce::Point<float> top, side;

    float width() const { return top.getDistanceFromOrigin(); }
    float height() const { return side.getDistanceFromOrigin(); }

    /** The cosine of the angle at the corner: 0 for a right angle. */
    float cosine() const { return (top.x * side.x + top.y * side.y) / juce::jmax (1.0e-6f, width() * height()); }
};

BoxOnScreen boxOnScreen (ui::CanvasView& canvas, const juce::Uuid& id)
{
    const auto handles = canvas.getHandleCentres (id);
    return { handles[2] - handles[0], handles[6] - handles[0] };
}

/** The drawing area's width less what its height would give at `aspect`, in pixels. */
float aspectError (juce::Rectangle<float> drawing, float aspect)
{
    return drawing.getWidth() - drawing.getHeight() * aspect;
}

} // namespace

//==============================================================================
ARTFREQ_TEST (rotationAnglesAreNormalised)
{
    CHECK_NEAR (ArtDocument::normaliseRotation (0.0f), 0.0f, 0.0f);
    CHECK_NEAR (ArtDocument::normaliseRotation (180.0f), 180.0f, 0.0f);
    CHECK_NEAR (ArtDocument::normaliseRotation (-180.0f), -180.0f, 0.0f);
    CHECK_NEAR (ArtDocument::normaliseRotation (37.5f), 37.5f, 0.0f);
    CHECK_NEAR (ArtDocument::normaliseRotation (190.0f), -170.0f, 1.0e-4f);
    CHECK_NEAR (ArtDocument::normaliseRotation (-190.0f), 170.0f, 1.0e-4f);
    CHECK_NEAR (ArtDocument::normaliseRotation (540.0f), 180.0f, 1.0e-4f);
    CHECK_NEAR (ArtDocument::normaliseRotation (725.0f), 5.0f, 1.0e-3f);
    CHECK_NEAR (ArtDocument::normaliseRotation (-360.0f), 0.0f, 1.0e-4f);
    CHECK_NEAR (ArtDocument::normaliseRotation (std::numeric_limits<float>::quiet_NaN()), 0.0f, 0.0f);
    CHECK_NEAR (ArtDocument::normaliseRotation (std::numeric_limits<float>::infinity()), 0.0f, 0.0f);

    // Each plug-in's display aspect is its canvas's, which tests below check against its editor.
    CHECK_NEAR (ArtDocument::displayAspectFor (false), 764.0f / 462.0f, 1.0e-6f);
    CHECK_NEAR (ArtDocument::displayAspectFor (true), 652.0f / 300.0f, 1.0e-6f);
    CHECK_NEAR (makeProcessor (Variant::instrument)->getDisplayAspect(), ArtDocument::instrumentDisplayAspect, 0.0f);
    CHECK_NEAR (makeProcessor (Variant::effect)->getDisplayAspect(), ArtDocument::effectDisplayAspect, 0.0f);
}

ARTFREQ_TEST (rotationIsSavedAndOldDrawingsLoadUpright)
{
    auto document = referenceDrawing();
    document.objects[0].rotation = 37.5f;
    document.objects[2].rotation = -90.0f;

    const auto tree = document.toValueTree();
    const auto loaded = ArtDocument::fromValueTree (tree);
    CHECK (loaded.objects.size() == document.objects.size());

    if (loaded.objects.size() != document.objects.size())
        return;

    for (size_t i = 0; i < loaded.objects.size(); ++i)
        CHECK_NEAR (loaded.objects[i].rotation, document.objects[i].rotation, 1.0e-6f);

    // Upright objects are saved exactly as before rotation (no property at all)...
    CHECK (tree.getChild (0).hasProperty ("rotation"));
    CHECK (! tree.getChild (1).hasProperty ("rotation"));

    // ... and a drawing saved before rotation (none anywhere) loads upright.
    auto old = referenceDrawing().toValueTree();

    for (auto child : old)
        CHECK (! child.hasProperty ("rotation"));

    for (const auto& object : ArtDocument::fromValueTree (old).objects)
        CHECK_NEAR (object.rotation, 0.0f, 0.0f);

    // Out-of-range and broken angles are brought into -180..180 (or ignored).
    auto odd = referenceDrawing().toValueTree();
    odd.getChild (0).setProperty ("rotation", 450.0, nullptr);
    odd.getChild (1).setProperty ("rotation", "-200", nullptr);
    odd.getChild (2).setProperty ("rotation", "sideways", nullptr);
    const auto oddLoaded = ArtDocument::fromValueTree (odd);
    CHECK_NEAR (oddLoaded.objects[0].rotation, 90.0f, 1.0e-4f);
    CHECK_NEAR (oddLoaded.objects[1].rotation, 160.0f, 1.0e-4f);
    CHECK_NEAR (oddLoaded.objects[2].rotation, 0.0f, 0.0f);

    // Copies (undo steps) keep it.
    DocumentHistory history;
    auto edited = document;
    history.push (edited);
    edited.objects[0].rotation = 10.0f;
    CHECK (history.undo (edited));
    CHECK_NEAR (edited.objects[0].rotation, 37.5f, 0.0f);
}

ARTFREQ_TEST (uprightObjectsDrawExactlyAsBeforeRotation)
{
    // The hashes were taken from the compositor at the commit before rotation (212708c), with
    // this same drawing (ReferenceDrawing.h): text, outline text, pictures, objects partly off
    // the canvas. Float results are only promised bit for bit on the platform they were taken
    // on, so elsewhere the canvas is compared with the same drawing turned by a full turn.
    Compositor compositor;
    Canvas canvas;
    compositor.render (referenceDrawing(), FrequencyScale {}, canvas);
    const auto logHash = hashOf (canvas);

    Canvas linear;
    compositor.render (referenceDrawing(), FrequencyScale { ScaleType::linear, 20.0, 20000.0 }, linear);

   #if JUCE_LINUX && defined (__x86_64__)
    CHECK_MSG (logHash == 0x946546047c058f3dull, "log scale: " << std::hex << logHash);
    CHECK_MSG (hashOf (linear) == 0x4e621802b9a759ceull, "linear scale: " << std::hex << hashOf (linear));
   #endif

    // A whole turn is upright again (normalised to 0): the same pixels.
    auto turnedRound = referenceDrawing();

    for (auto& object : turnedRound.objects)
        object.rotation = 360.0f;

    Canvas again;
    compositor.render (turnedRound, FrequencyScale {}, again);
    CHECK (hashOf (again) == logHash);

    // Any other angle changes the picture.
    turnedRound.objects[1].rotation = 0.5f;
    compositor.render (turnedRound, FrequencyScale {}, again);
    CHECK (hashOf (again) != logHash);
}

ARTFREQ_TEST (aTurnedBarDrawsWhereItIsTurned)
{
  for (const auto aspect : { ArtDocument::instrumentDisplayAspect, ArtDocument::effectDisplayAspect })
  {
    Compositor compositor (aspect);
    CHECK_NEAR (compositor.getDisplayAspect(), aspect, 0.0f);

    for (const auto text : { false, true })
    {
        ArtDocument upright, turned;
        auto bar = middleBar (0.0f);

        if (text)
        {
            // A hyphen: text is stretched to fill its box, so it is a bar too.
            bar = ArtDocument::makeTextObject ("-", bar.lowHz, bar.highHz);
            bar.startTime = 0.3f;
            bar.endTime = 0.7f;
        }

        upright.objects.push_back (bar);
        bar.rotation = 90.0f;
        turned.objects.push_back (bar);

        Canvas a, b;
        compositor.render (upright, FrequencyScale {}, a);
        compositor.render (turned, FrequencyScale {}, b);

        CHECK_MSG (meanIn (a, onlyUpright) > 0.3f && meanIn (a, onlyTurned) < 0.001f, (text ? "text" : "picture"));
        CHECK_MSG (meanIn (b, onlyTurned) > 0.3f && meanIn (b, onlyUpright) < 0.001f, (text ? "text" : "picture"));

        // Turned in display space: it stands 0.4 * aspect of the canvas tall, centred.
        if (! text)
        {
            int top = Canvas::height, bottom = -1;

            for (int y = 0; y < Canvas::height; ++y)
                if (b.get (Canvas::width / 2, y) > 0.5f)
                {
                    top = juce::jmin (top, y);
                    bottom = juce::jmax (bottom, y);
                }

            const auto tall = static_cast<float> (bottom - top + 1) / Canvas::height;
            CHECK_NEAR (tall, 0.4f * aspect, 0.01f);
            CHECK_NEAR ((top + bottom + 1) * 0.5f / Canvas::height, 0.5f, 0.005f);

            int left = Canvas::width, right = -1;

            for (int x = 0; x < Canvas::width; ++x)
                if (b.get (x, Canvas::height / 2) > 0.5f)
                {
                    left = juce::jmin (left, x);
                    right = juce::jmax (right, x);
                }

            CHECK_NEAR (static_cast<float> (right - left + 1) / Canvas::width, 0.08f / aspect, 0.004f);

            // Anti-aliased edges: values between silent and loud along them, none above the top.
            float largest = 0.0f;

            for (auto v : b.values)
                largest = juce::jmax (largest, v);

            CHECK (largest <= 1.0f);
        }
    }

    // Turned off the canvas: clipped, nothing outside the canvas is touched (no crash), and a
    // half-turn of an outline text draws.
    ArtDocument edge;
    auto text = ArtDocument::makeTextObject ("Edge", 8000.0f, 30000.0f);
    text.startTime = 0.7f;
    text.endTime = 1.2f;
    text.rotation = 33.0f;
    text.textStyle = TextStyle::outline;
    edge.objects.push_back (text);
    auto picture = middleBar (-120.0f);
    picture.startTime = -0.3f;
    picture.endTime = 0.2f;
    edge.objects.push_back (picture);
    Canvas c;
    compositor.render (edge, FrequencyScale {}, c);
    CHECK (meanIn (c, { 0.75f, 0.0f, 0.25f, 0.3f }) > 0.0f);
    CHECK (meanIn (c, { 0.0f, 0.0f, 0.2f, 1.0f }) > 0.0f);
  }
}

ARTFREQ_TEST (aSquareTurnsIntoASquareDiamondInEachPlugIn)
{
    // A picture square on each plug-in's canvas (0.3 of its height tall, as wide on screen),
    // turned 45 degrees by that plug-in's compositor: on that canvas it is a square diamond. Its
    // extents and corners are measured in canvas cells, scaled to the screen by the plug-in's
    // aspect (a cell is aspect / Canvas::width of the height wide on screen).
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        auto processor = makeProcessor (variant);
        const auto aspect = processor->getDisplayAspect();
        const auto halfWidth = 0.15f / aspect;
        auto square = whiteBar (0.5f - halfWidth, 0.5f + halfWidth, 0.35f, 0.65f, FrequencyScale {});
        CHECK_NEAR (displayAspectOf (square, FrequencyScale {}, aspect), 1.0f, 0.001f);
        square.rotation = 45.0f;

        ArtDocument document;
        document.objects.push_back (square);
        Compositor compositor (aspect);   // as the processor's (thePlanAndTheMasksFollowTheTurn)
        Canvas canvas;
        compositor.render (document, FrequencyScale {}, canvas);

        const auto lit = [&canvas] (int x, int y) { return canvas.get (x, y) > 0.5f; };
        const auto xOnScreen = [aspect] (float cells) { return cells / static_cast<float> (Canvas::width) * aspect; };
        const auto yOnScreen = [] (float cells) { return cells / static_cast<float> (Canvas::height); };

        // Across the middle row and down the middle column: as wide on screen as tall, the
        // square's diagonal.
        int left = Canvas::width, right = -1, top = Canvas::height, bottom = -1;

        for (int x = 0; x < Canvas::width; ++x)
            if (lit (x, Canvas::height / 2)) { left = juce::jmin (left, x); right = juce::jmax (right, x); }

        for (int y = 0; y < Canvas::height; ++y)
            if (lit (Canvas::width / 2, y)) { top = juce::jmin (top, y); bottom = juce::jmax (bottom, y); }

        const auto across = xOnScreen (static_cast<float> (right - left + 1));
        const auto down = yOnScreen (static_cast<float> (bottom - top + 1));
        const auto diagonal = 0.3f * std::sqrt (2.0f);
        CHECK_MSG (std::abs (across - diagonal) < 0.01f && std::abs (down - diagonal) < 0.01f,
                   nameOf (variant) << ": " << across << " across, " << down << " down, wanted " << diagonal);

        // Its four corners (the middle of the lit cells in the top and bottom rows and the
        // outermost columns) on screen: the sides between them meet at right angles.
        const auto rowMiddle = [&] (int y)
        {
            float sum = 0.0f; int count = 0;

            for (int x = 0; x < Canvas::width; ++x)
                if (lit (x, y)) { sum += static_cast<float> (x) + 0.5f; ++count; }

            return juce::Point<float> (xOnScreen (sum / static_cast<float> (juce::jmax (1, count))), yOnScreen (static_cast<float> (y) + 0.5f));
        };

        const auto columnMiddle = [&] (int x)
        {
            float sum = 0.0f; int count = 0;

            for (int y = 0; y < Canvas::height; ++y)
                if (lit (x, y)) { sum += static_cast<float> (y) + 0.5f; ++count; }

            return juce::Point<float> (xOnScreen (static_cast<float> (x) + 0.5f), yOnScreen (sum / static_cast<float> (juce::jmax (1, count))));
        };

        int firstRow = -1, lastRow = -1, firstColumn = -1, lastColumn = -1;

        for (int y = 0; y < Canvas::height; ++y)
            for (int x = 0; x < Canvas::width; ++x)
                if (lit (x, y))
                {
                    if (firstRow < 0) firstRow = y;
                    lastRow = y;
                    firstColumn = firstColumn < 0 ? x : juce::jmin (firstColumn, x);
                    lastColumn = juce::jmax (lastColumn, x);
                }

        const auto upper = rowMiddle (firstRow), lower = rowMiddle (lastRow);
        const auto leftmost = columnMiddle (firstColumn), rightmost = columnMiddle (lastColumn);
        const auto a = rightmost - upper, b = leftmost - upper, c = rightmost - lower, d = leftmost - lower;
        const auto cosineAt = [] (juce::Point<float> u, juce::Point<float> v)
        {
            return (u.x * v.x + u.y * v.y) / juce::jmax (1.0e-6f, u.getDistanceFromOrigin() * v.getDistanceFromOrigin());
        };

        CHECK_MSG (std::abs (cosineAt (a, b)) < 0.03f && std::abs (cosineAt (c, d)) < 0.03f,
                   nameOf (variant) << ": corners " << cosineAt (a, b) << ", " << cosineAt (c, d));
        CHECK_MSG (std::abs (a.getDistanceFromOrigin() / b.getDistanceFromOrigin() - 1.0f) < 0.03f,
                   nameOf (variant) << ": sides " << a.getDistanceFromOrigin() << ", " << b.getDistanceFromOrigin());

        // The other plug-in's turn would not be square here: the aspect is the plug-in's own.
        Compositor other (variant == Variant::instrument ? ArtDocument::effectDisplayAspect : ArtDocument::instrumentDisplayAspect);
        Canvas wrong;
        other.render (document, FrequencyScale {}, wrong);
        int wrongLeft = Canvas::width, wrongRight = -1;

        for (int x = 0; x < Canvas::width; ++x)
            if (wrong.get (x, Canvas::height / 2) > 0.5f) { wrongLeft = juce::jmin (wrongLeft, x); wrongRight = juce::jmax (wrongRight, x); }

        int wrongTop = Canvas::height, wrongBottom = -1;

        for (int y = 0; y < Canvas::height; ++y)
            if (wrong.get (Canvas::width / 2, y) > 0.5f) { wrongTop = juce::jmin (wrongTop, y); wrongBottom = juce::jmax (wrongBottom, y); }

        CHECK (juce::jmax (std::abs (xOnScreen (static_cast<float> (wrongRight - wrongLeft + 1)) - diagonal),
                           std::abs (yOnScreen (static_cast<float> (wrongBottom - wrongTop + 1)) - diagonal)) > 0.03f);
    }
}

ARTFREQ_TEST (thePlanAndTheMasksFollowTheTurn)
{
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        for (const auto rotation : { 0.0f, 90.0f })
        {
            auto processor = makeProcessor (variant);
            ArtDocument document;
            document.objects.push_back (middleBar (rotation));
            processor->setDocument (document);
            processor->rebuildPlanIfNeeded();

            const auto plan = processor->getCurrentPlan();
            CHECK (plan != nullptr);

            // Turned, the bar stands 0.4 * the plug-in's display aspect tall: from 0.17 to 0.83
            // of the canvas in ArtFreq, from 0.07 to 0.93 in ArtFreq FX, so only ArtFreq FX's
            // reaches this high (its processor turns it for its own canvas).
            const juce::Rectangle<float> onlyEffectTurned { 0.49f, 0.09f, 0.02f, 0.05f };

            if (plan != nullptr)
            {
                const auto turnedLevel = planLevelIn (*plan, onlyTurned);
                const auto uprightLevel = planLevelIn (*plan, onlyUpright);
                CHECK_MSG (rotation > 0.0f ? turnedLevel > 0.0f && uprightLevel <= turnedLevel * 0.01f
                                           : uprightLevel > 0.0f && turnedLevel <= uprightLevel * 0.01f,
                           nameOf (variant) << ", plan, " << rotation << " degrees: " << turnedLevel << " / " << uprightLevel);

                if (rotation > 0.0f)
                {
                    const auto high = planLevelIn (*plan, onlyEffectTurned);
                    CHECK_MSG (variant == Variant::effect ? high > turnedLevel * 0.5f : high <= turnedLevel * 0.01f,
                               nameOf (variant) << ", plan, the turned bar's top: " << high << " / " << turnedLevel);
                }
            }

            if (variant == Variant::effect)
            {
                const auto masks = processor->getCurrentMasks();
                CHECK (masks != nullptr && masks->exact != nullptr);

                if (masks != nullptr && masks->exact != nullptr)
                {
                    const auto turnedLevel = maskLevelIn (*masks->exact, onlyTurned);
                    const auto uprightLevel = maskLevelIn (*masks->exact, onlyUpright);
                    CHECK_MSG (rotation > 0.0f ? turnedLevel > 0.5f && uprightLevel <= 0.0f : uprightLevel > 0.5f && turnedLevel <= 0.0f,
                               "masks, " << rotation << " degrees: " << turnedLevel << " / " << uprightLevel);

                    if (rotation > 0.0f)
                        CHECK (maskLevelIn (*masks->exact, onlyEffectTurned) > 0.5f);
                }
            }
        }
    }
}

ARTFREQ_TEST (thePreviewsShowTheTurn)
{
    // The fast preview (the canvas's picture while nothing is measured) and the measured one
    // (the real sound, analysed) of the turned bar: ArtFreq's are bright where the bar is,
    // ArtFreq FX's (Carve) dark where it carves the song.
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        float fast[2] {}, fastElsewhere[2] {}, measured[2] {}, measuredElsewhere[2] {};

        for (int turned = 0; turned < 2; ++turned)
        {
            auto bar = middleBar (turned != 0 ? 90.0f : 0.0f);
            OneObject setup (variant, bar);
            setup.editor->selectObject (juce::Uuid::null());   // no handles over the picture
            setup.processor->rebuildPlanIfNeeded();
            setup.canvas->setFastPreviewMadeWhilePainting (true);
            setup.settle();

            const auto shot = setup.canvas->createComponentSnapshot (setup.canvas->getLocalBounds(), true, 1.0f);
            const auto area = setup.canvas->getDrawingArea();
            fast[turned] = meanBrightness (shot, area, onlyTurned);
            fastElsewhere[turned] = meanBrightness (shot, area, onlyUpright);

            const auto request = ui::MeasureRequest::forProcessor (*setup.processor, 320, 200);
            const auto result = ui::MeasuredPreview::renderNow (request);
            CHECK (result != nullptr);

            if (result != nullptr)
            {
                juce::Image picture (juce::Image::ARGB, request.width, request.height, true);

                for (int y = 0; y < request.height; ++y)
                    for (int x = 0; x < request.width; ++x)
                        picture.setPixelAt (x, y, juce::Colour (result->pixels[static_cast<size_t> (y * request.width + x)].getUnpremultiplied()));

                measured[turned] = meanBrightness (picture, picture.getBounds().toFloat(), onlyTurned);
                measuredElsewhere[turned] = meanBrightness (picture, picture.getBounds().toFloat(), onlyUpright);
            }
        }

        // Where only the turned bar is: brighter (ArtFreq) or darker (ArtFreq FX) once turned;
        // where only the upright one is, the other way round.
        const auto sign = variant == Variant::instrument ? 1.0f : -1.0f;
        CHECK_MSG (sign * (fast[1] - fast[0]) > 0.05f && sign * (fastElsewhere[0] - fastElsewhere[1]) > 0.05f,
                   nameOf (variant) << ", fast preview: " << fast[0] << " -> " << fast[1] << ", " << fastElsewhere[0] << " -> " << fastElsewhere[1]);
        CHECK_MSG (sign * (measured[1] - measured[0]) > 0.05f && sign * (measuredElsewhere[0] - measuredElsewhere[1]) > 0.05f,
                   nameOf (variant) << ", measured preview: " << measured[0] << " -> " << measured[1] << ", " << measuredElsewhere[0] << " -> " << measuredElsewhere[1]);
    }
}

//==============================================================================
ARTFREQ_TEST (shapePresetsReshapeAboutTheCentre)
{
  // For each plug-in's canvas: a shape is the box's proportions on that plug-in's screen.
  for (const auto aspect : { ArtDocument::instrumentDisplayAspect, ArtDocument::effectDisplayAspect })
  {
    const FrequencyScale scale;
    auto picture = whiteBar (0.3f, 0.6f, 0.4f, 0.6f, scale);   // a 64 x 16 picture: Original is 4:1
    const auto before = objectRectNorm (picture, scale);
    const auto area = before.getWidth() * aspect * before.getHeight();

    for (int i = 0; i < numShapePresets; ++i)
    {
        const auto preset = static_cast<ShapePreset> (i);
        auto shaped = picture;
        applyShapeAspect (shaped, shapeAspect (preset, shaped), scale, aspect);
        const auto r = objectRectNorm (shaped, scale);

        CHECK_NEAR (displayAspectOf (shaped, scale, aspect) / shapeAspect (preset, shaped), 1.0f, 0.01f);
        CHECK_NEAR (r.getWidth() * aspect / r.getHeight(), shapeAspect (preset, shaped), 0.01f * shapeAspect (preset, shaped));
        CHECK_NEAR (r.getCentreX(), before.getCentreX(), 1.0e-4f);
        CHECK_NEAR (r.getCentreY(), before.getCentreY(), 1.0e-4f);
        CHECK_NEAR (r.getWidth() * aspect * r.getHeight() / area, 1.0f, 0.01f);
        CHECK (matchingShapePreset (shaped, scale, aspect) == i || (i > 0 && matchingShapePreset (shaped, scale, aspect) == 0));
    }

    CHECK_NEAR (shapeAspect (ShapePreset::original, picture), 4.0f, 1.0e-6f);
    CHECK_NEAR (shapeAspect (ShapePreset::sixteenByNine, picture), 16.0f / 9.0f, 1.0e-6f);
    CHECK (matchingShapePreset (picture, scale, aspect) == -1);   // stretched: none

    // Near the canvas's edge: shrunk about its centre to stay inside, then moved in.
    auto edge = whiteBar (0.9f, 0.99f, 0.02f, 0.3f, scale);
    applyShapeAspect (edge, 16.0f / 9.0f, scale, aspect);
    const auto inside = objectRectNorm (edge, scale);
    CHECK (inside.getX() >= -1.0e-5f && inside.getRight() <= 1.0f + 1.0e-5f);
    CHECK (inside.getY() >= -1.0e-5f && inside.getBottom() <= 1.0f + 1.0e-5f);
    CHECK_NEAR (displayAspectOf (edge, scale, aspect), 16.0f / 9.0f, 0.02f);

    // Larger than the canvas allows: as large as fits.
    auto huge = whiteBar (0.0f, 1.0f, 0.0f, 1.0f, scale);
    applyShapeAspect (huge, 9.0f / 16.0f, scale, aspect);
    const auto fitted = objectRectNorm (huge, scale);
    CHECK_NEAR (fitted.getHeight(), 1.0f, 1.0e-4f);
    CHECK_NEAR (displayAspectOf (huge, scale, aspect), 9.0f / 16.0f, 0.01f);
  }

    // A box square on one plug-in's canvas is not on the other's.
    auto square = whiteBar (0.3f, 0.6f, 0.4f, 0.6f, FrequencyScale {});
    applyShapeAspect (square, 1.0f, FrequencyScale {}, ArtDocument::effectDisplayAspect);
    CHECK (matchingShapePreset (square, FrequencyScale {}, ArtDocument::effectDisplayAspect) == static_cast<int> (ShapePreset::square));
    CHECK (matchingShapePreset (square, FrequencyScale {}, ArtDocument::instrumentDisplayAspect) != static_cast<int> (ShapePreset::square));
}

ARTFREQ_TEST (theShapeMenuReshapesAPictureInBothPlugIns)
{
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        OneObject setup (variant, whiteBar (0.3f, 0.6f, 0.4f, 0.6f, FrequencyScale {}));
        auto* shape = find<juce::ComboBox> (*setup.editor, "inspector-shape");
        CHECK (shape != nullptr);

        if (shape == nullptr)
            continue;

        CHECK_EVENTUALLY (shape->isVisible());
        CHECK_MSG (shape->getText() == "Free", nameOf (variant) << ": " << shape->getText());

        const auto before = objectRectNorm (setup.object(), setup.scale());
        shape->setSelectedId (3, juce::sendNotificationSync);   // 1:1
        CHECK_NEAR (setup.shape(), 1.0f, 0.01f);
        CHECK_NEAR (objectRectNorm (setup.object(), setup.scale()).getCentreX(), before.getCentreX(), 1.0e-4f);
        CHECK_EVENTUALLY (shape->getText() == "1:1");

        // Square on the screen: the box the canvas shows, measured in pixels (whatever room the
        // drawer leaves it).
        const auto square = boxOnScreen (*setup.canvas, setup.id);
        CHECK_MSG (std::abs (square.width() - square.height()) < 1.0f,
                   nameOf (variant) << ", 1:1 on screen: " << square.width() << " x " << square.height());

        shape->setSelectedId (6, juce::sendNotificationSync);   // 16:9
        CHECK_EVENTUALLY (shape->getText() == "16:9");
        const auto wide = boxOnScreen (*setup.canvas, setup.id);
        CHECK_MSG (std::abs (wide.width() - wide.height() * 16.0f / 9.0f) < 1.0f,
                   nameOf (variant) << ", 16:9 on screen: " << wide.width() << " x " << wide.height());
        CHECK (setup.processor->undo());
        CHECK_EVENTUALLY (shape->getText() == "1:1");

        shape->setSelectedId (2, juce::sendNotificationSync);   // Original: 4:1
        CHECK_NEAR (setup.shape(), 4.0f, 0.04f);
        CHECK_EVENTUALLY (shape->getText() == "Original");

        // One undo step each.
        CHECK (setup.processor->undo());
        CHECK_NEAR (setup.shape(), 1.0f, 0.01f);
        CHECK (setup.processor->undo());
        CHECK_NEAR (objectRectNorm (setup.object(), setup.scale()).getWidth(), before.getWidth(), 1.0e-6f);
        CHECK_EVENTUALLY (shape->getText() == "Free");
    }
}

ARTFREQ_TEST (aDroppedPictureKeepsItsProportions)
{
  for (const auto variant : { Variant::instrument, Variant::effect })
  {
    auto processor = makeProcessor (variant);
    processor->setDocument (ArtDocument());
    auto editor = makeEditor (*processor);
    auto* canvas = find<ui::CanvasView> (*editor, "canvas");
    CHECK (canvas != nullptr);

    if (canvas == nullptr)
        return;

    const auto file = juce::File::createTempFile (".png");
    {
        juce::Image image (juce::Image::ARGB, 160, 90, true);
        juce::Graphics (image).fillAll (juce::Colours::white);
        juce::FileOutputStream stream (file);
        CHECK (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream));
    }

    const auto at = canvas->getDrawingArea().getCentre().toInt();
    canvas->filesDropped ({ file.getFullPathName() }, at.x, at.y);
    CHECK (processor->getDocument().objects.size() == 1);

    if (processor->getDocument().objects.size() == 1)
    {
        const auto& picture = processor->getDocument().objects.front();
        CHECK_NEAR (displayAspectOf (picture, processor->getScale(), processor->getDisplayAspect()), 16.0f / 9.0f, 0.02f);
        CHECK (ui::actions::bandOf (picture, processor->getScale()) >= 0);   // still exactly on its band
        CHECK_NEAR ((picture.startTime + picture.endTime) * 0.5f, 0.5f, 0.01f);

        // 16:9 on the screen too.
        const auto box = boxOnScreen (*canvas, picture.id);
        CHECK_MSG (std::abs (box.width() / box.height() - 16.0f / 9.0f) * box.height() < 1.5f,
                   nameOf (variant) << ": " << box.width() << " x " << box.height());
    }

    file.deleteFile();
  }
}

//==============================================================================
ARTFREQ_TEST (theFittedCanvasKeepsItsShapeInAnyRoom)
{
    // The drawing area as large as fits, at the aspect asked for, centred, with the axes round it.
    for (const auto aspect : { ArtDocument::instrumentDisplayAspect, ArtDocument::effectDisplayAspect })
        for (const auto room : { juce::Rectangle<int> (2, 8, 708, 332), juce::Rectangle<int> (250, 8, 460, 332),
                                 juce::Rectangle<int> (2, 40, 708, 300), juce::Rectangle<int> (0, 0, 333, 1000),
                                 juce::Rectangle<int> (4, 12, 820, 494) })
        {
            const auto bounds = ui::CanvasView::fittedBounds (room, aspect);
            const auto drawing = ui::axes::plotArea (bounds, ui::CanvasView::axisWidth, ui::CanvasView::axisHeight).toFloat();
            const auto most = ui::axes::plotArea (room, ui::CanvasView::axisWidth, ui::CanvasView::axisHeight);
            CHECK_MSG (std::abs (aspectError (drawing, aspect)) <= 0.5f + 1.0e-3f, room.toString() << ": " << drawing.toString());
            CHECK (room.contains (bounds));
            CHECK_MSG (drawing.getWidth() >= static_cast<float> (most.getWidth()) - aspect - 0.5f
                           || drawing.getHeight() >= static_cast<float> (most.getHeight()) - 1.0f,
                       room.toString() << ": " << drawing.toString() << " in " << most.toString());
            CHECK (std::abs ((bounds.getCentreX() - room.getCentreX())) <= 1 && std::abs (bounds.getCentreY() - room.getCentreY()) <= 1);
        }

    // Each plug-in's own room at 100 % is exactly its aspect: the measured canvases.
    CHECK (ui::CanvasView::fittedBounds ({ 2, 8, 708, 332 }, ArtDocument::effectDisplayAspect) == juce::Rectangle<int> (2, 8, 708, 332));
    CHECK (ui::CanvasView::fittedBounds ({ 4, 12, 820, 494 }, ArtDocument::instrumentDisplayAspect) == juce::Rectangle<int> (4, 12, 820, 494));
}

ARTFREQ_TEST (eachCanvasHasItsPlugInsAspectInEveryState)
{
    // The drawing area, in the window's own pixels, against the plug-in's display aspect: within
    // a pixel, in every state of the window and at every size. (Content scales uniformly, so the
    // drawing area is measured both in the canvas's coordinates and in the window's.)
    const auto measure = [] (ArtFreqEditor& editor, ui::CanvasView& canvas, float aspect, const juce::String& state)
    {
        const auto drawing = canvas.getDrawingArea();
        const auto inWindow = editor.getLocalArea (&canvas, drawing);
        CHECK_MSG (std::abs (aspectError (drawing, aspect)) <= 1.0f && std::abs (aspectError (inWindow, aspect)) <= 1.0f,
                   state << ": " << drawing.toString() << " (" << inWindow.toString() << " in the window), wanted "
                         << aspect);
        CHECK_MSG (editor.getLocalBounds().toFloat().expanded (0.5f).contains (inWindow), state << ": in the window");

        // The live view lies exactly where the canvas does.
        if (auto* live = findComponent (editor, "live-view"))
            CHECK_MSG (live->getBounds() == canvas.getBounds(), state << ": live view " << live->getBounds().toString());
    };

    // ---- ArtFreq: its canvas has the same room whatever is shown --------------------------------
    {
        auto processor = makeProcessor (Variant::instrument);
        const auto aspect = processor->getDisplayAspect();
        auto editor = makeEditor (*processor);
        auto* canvas = find<ui::CanvasView> (*editor, "canvas");
        CHECK (canvas != nullptr);

        if (canvas == nullptr)
            return;

        CHECK_EVENTUALLY (canvas->getDrawingArea().getWidth() > 0.0f);
        const auto home = canvas->getBounds();
        CHECK_MSG (canvas->getDrawingArea().toNearestInt().getWidth() == 764 && canvas->getDrawingArea().toNearestInt().getHeight() == 462,
                   "ArtFreq's drawing area " << canvas->getDrawingArea().toString());
        measure (*editor, *canvas, aspect, "ArtFreq, a new window");

        // (The inspector shows the layer list while nothing is selected.)
        CHECK_EVENTUALLY (editor->isLayerListShown());
        measure (*editor, *canvas, aspect, "ArtFreq, the layer list");
        const auto id = processor->getDocument().objects.front().id;
        editor->selectObject (id);
        CHECK_EVENTUALLY (editor->getSelectedObject() == id && ! editor->isLayerListShown());
        measure (*editor, *canvas, aspect, "ArtFreq, an object selected");
        editor->showSettings (true);
        CHECK_EVENTUALLY (isShownAnywhere (*editor, "settings-panel"));
        measure (*editor, *canvas, aspect, "ArtFreq, the settings");
        editor->showSettings (false);
        editor->showHelp (true);
        measure (*editor, *canvas, aspect, "ArtFreq, the help");
        editor->showHelp (false);
        editor->showLiveView (true);
        CHECK_EVENTUALLY (isShownAnywhere (*editor, "live-view"));
        measure (*editor, *canvas, aspect, "ArtFreq, the live view");
        editor->showLiveView (false);
        CHECK (canvas->getBounds() == home);

        for (const auto scale : { 0.75f, 1.5f, 2.0f })
        {
            editor->setSize (juce::roundToInt (ArtFreqEditor::baseWidth * scale), juce::roundToInt (ArtFreqEditor::baseHeight * scale));
            measure (*editor, *canvas, aspect, "ArtFreq at " + juce::String (scale * 100.0f) + " %");
            CHECK (canvas->getBounds() == home);
        }
    }

    // ---- ArtFreq FX: fitted into whatever room the strips and the drawer leave ----------------
    {
        auto processor = makeProcessor (Variant::effect);
        const auto aspect = processor->getDisplayAspect();

        // A narrow text on the left, a narrow picture on the right, and a wide text.
        ArtDocument document;
        auto left = ArtDocument::makeTextObject ("Left", 1000.0f, 4000.0f);
        left.startTime = 0.08f;
        left.endTime = 0.28f;
        auto right = whiteBar (0.7f, 0.9f, 0.3f, 0.5f, FrequencyScale {});
        auto wide = ArtDocument::makeTextObject ("Wide", 200.0f, 800.0f);
        wide.startTime = 0.05f;
        wide.endTime = 0.95f;

        for (const auto& object : { left, right, wide })
            document.objects.push_back (object);

        processor->setDocument (document);
        auto editor = makeEditor (*processor);
        auto* canvas = find<ui::CanvasView> (*editor, "canvas");
        auto* drawer = findComponent (*editor, "inspector");
        CHECK (canvas != nullptr && drawer != nullptr);

        if (canvas == nullptr || drawer == nullptr)
            return;

        CHECK_EVENTUALLY (canvas->getDrawingArea().getWidth() > 0.0f);

        // The aspect is Carve's canvas, as it is at 100 %: 652 x 300.
        CHECK_MSG (canvas->getDrawingArea().toNearestInt().getWidth() == 652 && canvas->getDrawingArea().toNearestInt().getHeight() == 300,
                   "ArtFreq FX's drawing area in Carve " << canvas->getDrawingArea().toString());

        for (const auto mode : { EffectMode::carve, EffectMode::stencil, EffectMode::carveDraw, EffectMode::draw })
        {
            testutil::setParameter (*processor, params::id::mode, static_cast<float> (mode));
            const auto sound = playsSines (mode);
            CHECK_EVENTUALLY (isShownAnywhere (*editor, "sound-strip") == sound);
            const auto name = "ArtFreq FX, mode " + juce::String (static_cast<int> (mode));

            editor->selectObject (juce::Uuid::null());
            CHECK_EVENTUALLY (! editor->isDrawerOpen());
            measure (*editor, *canvas, aspect, name + ", the drawer closed");
            const auto full = canvas->getBounds();

            // Under the Sound strip: less wide, centred in the window; else the whole width.
            if (sound)
                CHECK_MSG (canvas->getWidth() < 700 && std::abs (canvas->getBounds().getCentreX() - (2 + 708 / 2)) <= 1,
                           name << ": " << canvas->getBounds().toString());
            else
                CHECK_MSG (canvas->getBounds() == juce::Rectangle<int> (2, 52, 708, 332), name << ": " << canvas->getBounds().toString());

            // A narrow object on the left: the drawer opens on the right, over the canvas, which
            // stays as it is.
            editor->selectObject (left.id);
            CHECK_EVENTUALLY (editor->isDrawerOpen() && ! editor->isDrawerOnLeft());
            measure (*editor, *canvas, aspect, name + ", the drawer on the right");
            CHECK (canvas->getBounds() == full);

            // ... and one on the right: on the left.
            editor->selectObject (right.id);
            CHECK_EVENTUALLY (editor->isDrawerOpen() && editor->isDrawerOnLeft());
            measure (*editor, *canvas, aspect, name + ", the drawer on the left");
            CHECK (canvas->getBounds() == full);

            // A wide one: the canvas beside the drawer, smaller, at the same aspect.
            editor->selectObject (wide.id);
            CHECK_EVENTUALLY (editor->isDrawerOpen() && canvas->getWidth() < full.getWidth() - 100);
            measure (*editor, *canvas, aspect, name + ", the canvas beside the drawer");
            CHECK (canvas->getRight() <= drawer->getX() || canvas->getX() >= drawer->getRight());
            CHECK (canvas->getHeight() >= 200);

            // The layer list over the canvas: back to its whole room.
            editor->showLayers (true);
            CHECK_EVENTUALLY (editor->isLayerListShown() && canvas->getBounds() == full);
            measure (*editor, *canvas, aspect, name + ", the layer list");
            editor->showLayers (false);
            editor->selectObject (juce::Uuid::null());
            CHECK_EVENTUALLY (! editor->isDrawerOpen() && canvas->getBounds() == full);
        }

        // The brush (its strip over the canvas's foot) and the live view.
        testutil::setParameter (*processor, params::id::mode, static_cast<float> (EffectMode::carve));
        CHECK_EVENTUALLY (! isShownAnywhere (*editor, "sound-strip"));
        const auto carve = canvas->getBounds();

        if (auto* brush = find<juce::Button> (*editor, "tool-brush"))
        {
            brush->triggerClick();
            CHECK_EVENTUALLY (isShownAnywhere (*editor, "brush-strip"));
            measure (*editor, *canvas, aspect, "ArtFreq FX, the brush");
            CHECK (canvas->getBounds() == carve);
        }

        editor->showLiveView (true);
        CHECK_EVENTUALLY (isShownAnywhere (*editor, "live-view"));
        measure (*editor, *canvas, aspect, "ArtFreq FX, the live view");
        editor->showLiveView (false);

        for (const auto scale : { 0.75f, 2.0f })
        {
            editor->setSize (juce::roundToInt (ArtFreqEditor::effectBaseWidth * scale), juce::roundToInt (ArtFreqEditor::effectBaseHeight * scale));
            measure (*editor, *canvas, aspect, "ArtFreq FX at " + juce::String (scale * 100.0f) + " %");
            editor->selectObject (wide.id);
            CHECK_EVENTUALLY (editor->isDrawerOpen() && canvas->getWidth() < carve.getWidth() - 100);
            measure (*editor, *canvas, aspect, "ArtFreq FX at " + juce::String (scale * 100.0f) + " %, beside the drawer");
            editor->selectObject (juce::Uuid::null());
            CHECK_EVENTUALLY (! editor->isDrawerOpen());
        }
    }
}

ARTFREQ_TEST (aTurnedObjectIsPickedByItsTurnedBox)
{
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        OneObject setup (variant, middleBar (90.0f));
        setup.editor->selectObject (juce::Uuid::null());
        setup.settle();

        // Inside the turned box, outside the unrotated one: picks it.
        clickOn (*setup.canvas, onScreen (*setup.canvas, onlyTurned.getCentre()));
        CHECK_MSG (setup.editor->getSelectedObject() == setup.id, nameOf (variant));

        // Inside the unrotated box only: nothing.
        setup.editor->selectObject (juce::Uuid::null());
        setup.settle();
        clickOn (*setup.canvas, onScreen (*setup.canvas, onlyUpright.getCentre()));
        CHECK_MSG (setup.editor->getSelectedObject().isNull(), nameOf (variant));

        // The handles sit on the turned box: the top-left one at its turned corner.
        setup.editor->selectObject (setup.id);
        setup.settle();
        const auto corners = setup.corners();
        const auto handles = setup.canvas->getHandleCentres (setup.id);
        CHECK (handles[0].getDistanceFrom (corners[0]) < 0.5f && handles[4].getDistanceFrom (corners[2]) < 0.5f);
        CHECK (setup.canvas->getObjectBounds (setup.id).contains (corners[1]));
    }
}

ARTFREQ_TEST (aTurnedObjectIsARectangleOnScreen)
{
    // Turned by any angle, an object's box on screen has right angles at its corners, and its
    // sides keep their lengths on screen: a turned rectangle, not a slanted parallelogram. In
    // ArtFreq FX also beside the drawer (a wide object) and under the Sound strip (Draw).
    for (const auto variant : { Variant::instrument, Variant::effect })
        for (const auto wide : { false, true })
        {
            auto bar = middleBar (0.0f);

            if (wide)
            {
                bar.startTime = 0.04f;
                bar.endTime = 0.96f;
            }

            OneObject setup (variant, bar);

            if (variant == Variant::effect && wide)
            {
                testutil::setParameter (*setup.processor, params::id::mode, static_cast<float> (EffectMode::draw));
                CHECK_EVENTUALLY (isShownAnywhere (*setup.editor, "sound-strip"));
            }

            CHECK_EVENTUALLY (setup.editor->isDrawerOpen() || variant == Variant::instrument);
            const auto upright = boxOnScreen (*setup.canvas, setup.id);

            for (const auto degrees : { 30.0f, -65.0f, 90.0f, 137.0f })
            {
                setup.processor->editDocument ([&setup, degrees] (ArtDocument& document) { document.find (setup.id)->rotation = degrees; });
                setup.settle();
                const auto box = boxOnScreen (*setup.canvas, setup.id);
                const auto turnedBy = juce::radiansToDegrees (std::atan2 (box.top.y, box.top.x));

                CHECK_MSG (std::abs (box.cosine()) < 0.005f,
                           nameOf (variant) << (wide ? ", wide" : "") << ", " << degrees << " degrees: cosine " << box.cosine());
                CHECK_MSG (std::abs (ArtDocument::normaliseRotation (turnedBy - degrees)) < 0.5f,
                           nameOf (variant) << ", " << degrees << " degrees: turned " << turnedBy << " on screen");

                // (The drawer may have moved the canvas since the upright box was measured: its
                // drawing keeps its aspect, so the box keeps its proportions.)
                CHECK_MSG (std::abs (box.width() / box.height() - upright.width() / upright.height()) < 0.02f * upright.width() / upright.height(),
                           nameOf (variant) << ", " << degrees << " degrees: " << box.width() << " x " << box.height()
                                            << ", upright " << upright.width() << " x " << upright.height());
            }
        }
}

ARTFREQ_TEST (theRotateHandleTurnsTheObject)
{
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        OneObject setup (variant, middleBar (0.0f));
        auto& canvas = *setup.canvas;

        // The knob is above the top edge's middle.
        const auto knob = canvas.getRotateHandle (setup.id);
        const auto box = onScreen (canvas, objectRectNorm (setup.object(), setup.scale()).getTopLeft());
        CHECK_MSG (knob.y < box.y - 10.0f && std::abs (knob.x - setup.centreOnScreen().x) < 0.5f, nameOf (variant));

        // Round to the right of the centre: a quarter turn clockwise, whatever the view's shape.
        dragOn (canvas, knob, setup.centreOnScreen() + juce::Point<float> (120.0f, 0.0f));
        CHECK_NEAR (setup.object().rotation, 90.0f, 0.5f);

        // One undo step for the whole drag.
        CHECK (setup.processor->undo());
        CHECK_NEAR (setup.object().rotation, 0.0f, 0.0f);

        // The handle follows the mouse on screen: dragged to a point 40 degrees round from
        // straight up (clockwise), on the screen, it turns the object 40 degrees, and the other
        // angles likewise (the canvas has the aspect the turn is worked out in). Shift snaps
        // 40 to 45.
        const auto toward = [&setup] (float degrees)
        {
            const auto radians = juce::degreesToRadians (degrees);
            return setup.centreOnScreen() + juce::Point<float> (std::sin (radians), -std::cos (radians)) * 80.0f;
        };

        for (const auto degrees : { -70.0f, 25.0f, 130.0f })
        {
            setup.settle();
            dragOn (canvas, canvas.getRotateHandle (setup.id), toward (degrees));
            CHECK_MSG (std::abs (setup.object().rotation - degrees) < 1.0f,
                       nameOf (variant) << ": dragged to " << degrees << " degrees on screen, turned " << setup.object().rotation);
            CHECK (setup.processor->undo());
        }

        setup.settle();
        dragOn (canvas, canvas.getRotateHandle (setup.id), toward (40.0f));
        CHECK_NEAR (setup.object().rotation, 40.0f, 0.5f);

        // ... and the knob is where the mouse let go of it, on the line from the centre.
        const auto knobAt = canvas.getRotateHandle (setup.id) - setup.centreOnScreen();
        CHECK_MSG (std::abs (juce::radiansToDegrees (std::atan2 (knobAt.x, -knobAt.y)) - 40.0f) < 1.0f,
                   nameOf (variant) << ": the knob at " << juce::radiansToDegrees (std::atan2 (knobAt.x, -knobAt.y)) << " degrees");
        CHECK (setup.processor->undo());
        setup.settle();

        dragOn (canvas, canvas.getRotateHandle (setup.id), toward (40.0f), 8);
        CHECK_NEAR (setup.object().rotation, 45.0f, 0.0f);

        // The turned handle follows: still a stem's length off the top edge, now turned.
        setup.settle();
        const auto turnedKnob = canvas.getRotateHandle (setup.id);
        CHECK (turnedKnob.x > setup.centreOnScreen().x + 5.0f && turnedKnob.y < setup.centreOnScreen().y);

        // [ and ] turn by 15 degrees (Shift: 1) while the canvas has the keys.
        CHECK (canvas.keyPressed (juce::KeyPress (']', {}, ']')));
        CHECK_NEAR (setup.object().rotation, 60.0f, 1.0e-4f);
        CHECK (canvas.keyPressed (juce::KeyPress ('[', juce::ModifierKeys::shiftModifier, '{')));
        CHECK_NEAR (setup.object().rotation, 59.0f, 1.0e-4f);
        CHECK (setup.processor->undo());   // the two presses were one step
        CHECK_NEAR (setup.object().rotation, 45.0f, 1.0e-4f);
    }
}

ARTFREQ_TEST (resizingATurnedObjectKeepsTheOppositeCorner)
{
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        auto bar = middleBar (30.0f);
        bar.endTime = 0.55f;
        OneObject setup (variant, bar);
        auto& canvas = *setup.canvas;

        // (Positions are taken just before each drag: ArtFreq FX's drawer may have moved the
        // canvas after the last one.)
        auto before = setup.corners();
        const auto handles = canvas.getHandleCentres (setup.id);
        CHECK (handles[4].getDistanceFrom (before[2]) < 0.5f);

        // The bottom-right corner, pulled out: the top-left one stays put on screen.
        dragOn (canvas, handles[4], handles[4] + juce::Point<float> (40.0f, 25.0f));
        const auto after = setup.corners();
        CHECK_MSG (after[0].getDistanceFrom (before[0]) < 0.5f, nameOf (variant) << ": moved " << after[0].getDistanceFrom (before[0]));
        CHECK (after[2].getDistanceFrom (before[2]) > 20.0f);
        CHECK_NEAR (setup.object().rotation, 30.0f, 0.0f);

        // The left edge: the right edge's middle stays.
        setup.processor->undo();
        setup.settle();
        before = setup.corners();
        const auto rightMiddle = (before[1] + before[2]) * 0.5f;
        const auto left = canvas.getHandleCentres (setup.id)[7];
        dragOn (canvas, left, left + juce::Point<float> (-30.0f, 10.0f));
        const auto moved = setup.corners();
        CHECK (((moved[1] + moved[2]) * 0.5f).getDistanceFrom (rightMiddle) < 0.5f);
        CHECK (setup.object().endTime - setup.object().startTime > bar.endTime - bar.startTime);

        // Shift on a turned corner keeps the shape too.
        setup.processor->undo();
        setup.settle();
        before = setup.corners();
        const auto shape = setup.shape();
        const auto corner = canvas.getHandleCentres (setup.id)[4];
        dragOn (canvas, corner, corner + juce::Point<float> (30.0f, 30.0f), 8);
        CHECK_NEAR (setup.shape() / shape, 1.0f, 0.01f);
        CHECK (setup.corners()[0].getDistanceFrom (before[0]) < 0.5f);
    }
}

ARTFREQ_TEST (shiftKeepsProportionsWhileResizing)
{
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        OneObject setup (variant, whiteBar (0.3f, 0.5f, 0.4f, 0.6f, FrequencyScale {}));
        auto& canvas = *setup.canvas;
        const auto shape = setup.shape();
        const auto start = objectRectNorm (setup.object(), setup.scale());

        // A corner with Shift: the same shape, from the opposite corner, following the axis the
        // mouse moved along more.
        const auto onScreen = boxOnScreen (canvas, setup.id);
        auto handles = canvas.getHandleCentres (setup.id);
        dragOn (canvas, handles[4], handles[4] + juce::Point<float> (60.0f, 6.0f), 8);
        auto r = objectRectNorm (setup.object(), setup.scale());
        CHECK_NEAR (setup.shape() / shape, 1.0f, 0.005f);

        // The same proportions on screen, too.
        const auto grown = boxOnScreen (canvas, setup.id);
        CHECK_MSG (std::abs (grown.width() / grown.height() - onScreen.width() / onScreen.height()) * grown.height() < 1.0f,
                   nameOf (variant) << ": " << onScreen.width() << " x " << onScreen.height() << " -> " << grown.width() << " x " << grown.height());
        CHECK_NEAR (r.getX(), start.getX(), 1.0e-6f);
        CHECK_NEAR (r.getY(), start.getY(), 1.0e-5f);
        CHECK_NEAR (r.getWidth() * canvas.getDrawingArea().getWidth(), start.getWidth() * canvas.getDrawingArea().getWidth() + 60.0f, 0.5f);
        CHECK (setup.processor->undo());

        // Let go of Shift before the end: free again.
        setup.settle();
        handles = canvas.getHandleCentres (setup.id);
        dragOn (canvas, handles[4], handles[4] + juce::Point<float> (60.0f, 6.0f), 4);
        CHECK (std::abs (setup.shape() / shape - 1.0f) > 0.1f);
        CHECK (setup.processor->undo());

        // An edge with Shift: both sides follow, about the middle of that edge.
        setup.settle();
        handles = canvas.getHandleCentres (setup.id);
        dragOn (canvas, handles[3], handles[3] + juce::Point<float> (50.0f, 0.0f), 8);
        r = objectRectNorm (setup.object(), setup.scale());
        CHECK_NEAR (setup.shape() / shape, 1.0f, 0.005f);
        CHECK (r.getHeight() > start.getHeight() * 1.1f);
        CHECK_NEAR (r.getCentreY(), start.getCentreY(), 1.0e-5f);
        CHECK_NEAR (r.getX(), start.getX(), 1.0e-6f);

        // Moving with Shift still keeps to one axis.
        CHECK (setup.processor->undo());
        setup.settle();
        const auto from = setup.centreOnScreen();
        dragOn (canvas, from, from + juce::Point<float> (40.0f, 8.0f), 8);
        r = objectRectNorm (setup.object(), setup.scale());
        CHECK_NEAR (r.getY(), start.getY(), 1.0e-6f);
        CHECK (r.getX() > start.getX() + 0.01f);
    }
}

ARTFREQ_TEST (theRotationBarTurnsTheSelection)
{
    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        auto text = ArtDocument::makeTextObject ("Turn", 1000.0f, 4000.0f);
        OneObject setup (variant, text);
        auto* bar = find<juce::Slider> (*setup.editor, "inspector-rotation");
        CHECK (bar != nullptr);

        if (bar == nullptr)
            continue;

        CHECK_EVENTUALLY (bar->isVisible());
        CHECK_NEAR (bar->getMinimum(), -180.0, 0.0);
        CHECK_NEAR (bar->getMaximum(), 180.0, 0.0);
        CHECK (bar->getTextFromValue (30.0).startsWith ("30"));

        bar->setValue (-25.0, juce::sendNotificationSync);
        CHECK_NEAR (setup.object().rotation, -25.0f, 0.0f);

        // The bar follows the canvas's handle.
        setup.settle();
        dragOn (*setup.canvas, setup.canvas->getRotateHandle (setup.id), setup.centreOnScreen() + juce::Point<float> (100.0f, 0.0f));
        CHECK_EVENTUALLY (std::abs (bar->getValue() - 90.0) < 0.6);
        CHECK (setup.processor->undo());
        CHECK_EVENTUALLY (std::abs (bar->getValue() + 25.0) < 0.01);
    }
}
