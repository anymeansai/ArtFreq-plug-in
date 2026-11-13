// Renders the editor to PNG files, without a host, and checks the shell along the way.
//
//   FreqArtSnapshot <output-dir> [pixel-scale]
//
// Writes
//
//   freqart-default.png          a fresh instance (the default drawing)
//   freqart-rich.png             text "HELLO" in the high band, a picture in the low / mid band,
//                                brush strokes, density 384
//   freqart-playing.png          the same while previewing (playhead, Preview on)
//   freqart-text-selected.png    the text object selected
//   freqart-image-selected.png   the picture selected
//   freqart-brush.png            the Brush tool active (chosen with the B key)
//   freqart-live.png             the live view, filled from the plan
//   freqart-settings.png         the settings panel open
//   freqart-settings-reset.png   the same, asking to confirm "Reset drawing"
//   freqart-help.png             the help panel open
//   freqart-export.png           after "Bounce WAV": the chip (the file's name) and "Save..."
//   freqart-export-updating.png  just after a change: the chip says "Updating..." until the new file
//   freqart-menu.png             the Length menu open
//   freqart-tooltip.png          a tooltip (Density's)
//   freqart-edit.png             typing a value into the Level knob
//   freqart-dragging.png         the picture being dragged (its frequency range shown)
//   freqart-painting.png         a brush stroke just painted, the brush cursor over it
//   freqart-empty.png            after Clear: the empty canvas and its hint
//   freqart-linear.png           the Linear analyser scale and a 1 bar length (beat labels)
//   freqart-75.png               window at 75 %, one image pixel per logical pixel
//   freqart-150.png              window at 150 %, one image pixel per logical pixel
//
// All but the last two are rendered at <pixel-scale> image pixels per logical pixel (default 2, as
// on a HiDPI display). Along the way it checks keyboard shortcuts, undo / redo, the bounce (a
// WAV file of the right length, named on the chip, which says "Updating..." after a change until
// a new file takes its place), that the window size is remembered, and drives the editor with
// synthetic mouse events (select, move, resize, nudge, paint, erase, the inspector, the toolbar),
// checking what each does to the drawing and that undo takes it back. It exits with 1 if a file
// cannot be written or a check fails. At the end it puts the window on the (virtual) desktop to
// check the live view's own timer: the analyser on while shown, scrolling, off when hidden.
//
// On Linux run it under a virtual display:  xvfb-run -a FreqArtSnapshot out

#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/FrequencyScale.h"
#include "ui/Actions.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/LiveView.h"
#include "ui/ShellExport.h"
#include "ui/ShellToolbar.h"
#include "ui/Widgets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

namespace
{

bool failed = false;
int checksRun = 0;

void check (bool condition, const juce::String& what)
{
    ++checksRun;

    if (! condition)
    {
        std::cerr << "CHECK FAILED: " << what << std::endl;
        failed = true;
    }
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}

void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

void setParameter (freqart::FreqArtProcessor& processor, const char* id, float plainValue)
{
    if (auto* parameter = processor.getParameters().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

juce::Component* findComponentWithId (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findComponentWithId (*child, id))
            return found;

    return nullptr;
}

template <typename Type>
Type* find (juce::Component& root, const juce::String& id)
{
    auto* c = dynamic_cast<Type*> (findComponentWithId (root, id));
    check (c != nullptr, "component '" + id + "' exists");
    return c;
}

void setWindowScale (freqart::FreqArtEditor& editor, float scale)
{
    editor.setSize (juce::roundToInt (static_cast<float> (freqart::FreqArtEditor::baseWidth) * scale),
                    juce::roundToInt (static_cast<float> (freqart::FreqArtEditor::baseHeight) * scale));
}

/** A stand-in for an imported picture: a shaded disc and a frame on a white background, the
    kind of logo Auto turns into its negative. */
juce::Image makeTestPicture()
{
    juce::Image image (juce::Image::ARGB, 480, 300, true);
    juce::Graphics g (image);
    g.fillAll (juce::Colours::white);

    g.setColour (juce::Colours::black);
    g.drawRect (image.getBounds().reduced (12), 10);

    const auto disc = juce::Rectangle<float> (150.0f, 40.0f, 220.0f, 220.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, disc.getTopLeft(),
                                             juce::Colour (0xffc8c8c8), disc.getBottomRight(), false));
    g.fillEllipse (disc);

    g.setColour (juce::Colours::black);
    g.fillRect (50, 90, 60, 120);
    return image;
}

/** The "rich" drawing: text in the highs, a picture in the low / mid band, brush strokes. */
void makeRichDrawing (freqart::FreqArtProcessor& processor, juce::Uuid& textId, juce::Uuid& imageId)
{
    using namespace freqart;

    const auto scale = processor.getScale();
    ArtDocument document;

    const auto high = bandRange (Band::high, scale);
    auto text = ArtDocument::makeTextObject ("HELLO", static_cast<float> (high.low), static_cast<float> (high.high));
    text.startTime = 0.04f;
    text.endTime = 0.50f;
    textId = text.id;
    document.objects.push_back (text);

    auto picture = ArtDocument::makeImageObject (prepareImportedImage (makeTestPicture()), 70.0f, 2200.0f);
    picture.startTime = 0.56f;
    picture.endTime = 0.96f;
    imageId = picture.id;
    document.objects.push_back (picture);

    // A wave painted under the text, and a soft dab.
    juce::Point<float> last { 40.0f, 330.0f };

    for (int i = 1; i <= 48; ++i)
    {
        const auto x = 40.0f + static_cast<float> (i) * 10.0f;
        const juce::Point<float> next { x, 330.0f + 40.0f * std::sin (static_cast<float> (i) * 0.35f) };

        BrushStroke stroke;
        stroke.from = last;
        stroke.to = next;
        stroke.radiusX = stroke.radiusY = 5.0f;
        stroke.softness = 0.3f;
        stroke.strength = 0.9f;
        document.paintStroke (stroke);
        last = next;
    }

    BrushStroke dab;
    dab.from = dab.to = { 300.0f, 440.0f };
    dab.radiusX = dab.radiusY = 26.0f;
    dab.softness = 1.0f;
    dab.strength = 0.8f;
    document.paintStroke (dab);

    processor.setDocument (document);
    setParameter (processor, freqart::params::id::density, 384.0f);
}

/** Runs the processor for a while with Preview on, as a host would, so there is a playhead. */
void playFor (freqart::FreqArtProcessor& processor, double seconds)
{
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const auto blocks = static_cast<int> (seconds * 48000.0 / blockSize);

    for (int i = 0; i < blocks; ++i)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
    }
}

//==============================================================================
// Synthetic mouse input, delivered straight to a component (no window is needed).

juce::MouseEvent mouseEventAt (juce::Component& c, juce::Point<float> position, juce::Point<float> downPosition,
                               juce::ModifierKeys mods, int clicks = 1, bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), position, mods, juce::MouseInputSource::defaultPressure,
             0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPosition, now, clicks, dragged };
}

void clickOn (juce::Component& c, juce::Point<float> at)
{
    c.mouseDown (mouseEventAt (c, at, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
    c.mouseUp (mouseEventAt (c, at, at, {}));
}

/** Presses at `from`, moves to `to` in steps (as a real drag arrives) and lets go, unless
    `release` is false. */
void dragOn (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {},
             bool release = true)
{
    const auto held = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    c.mouseDown (mouseEventAt (c, from, from, held));

    constexpr int steps = 12;

    for (int i = 1; i <= steps; ++i)
        c.mouseDrag (mouseEventAt (c, from + (to - from) * (static_cast<float> (i) / steps), from, held, 1, true));

    if (release)
        c.mouseUp (mouseEventAt (c, to, from, mods, 1, true));
}

/** Where an object is on the canvas, in the canvas's coordinates. */
juce::Rectangle<float> onCanvas (freqart::ui::CanvasView& canvas, const freqart::ArtObject& object,
                                 const freqart::FrequencyScale& scale)
{
    const auto a = canvas.getDrawingArea();
    const auto n = freqart::objectRectNorm (object, scale);
    return { a.getX() + n.getX() * a.getWidth(), a.getY() + n.getY() * a.getHeight(),
             n.getWidth() * a.getWidth(), n.getHeight() * a.getHeight() };
}

/** The paint layer's value under a point of the canvas. */
int paintAt (const freqart::ArtDocument& document, freqart::ui::CanvasView& canvas, juce::Point<float> p)
{
    const auto a = canvas.getDrawingArea();
    const auto x = juce::jlimit (0, freqart::ArtDocument::paintWidth - 1,
                                 static_cast<int> ((p.x - a.getX()) / a.getWidth() * freqart::ArtDocument::paintWidth));
    const auto y = juce::jlimit (0, freqart::ArtDocument::paintHeight - 1,
                                 static_cast<int> ((p.y - a.getY()) / a.getHeight() * freqart::ArtDocument::paintHeight));

    if (document.paint.empty())
        return 0;

    return document.paint[static_cast<size_t> (y * freqart::ArtDocument::paintWidth + x)];
}

bool samePlacement (const freqart::ArtObject& a, const freqart::ArtObject& b)
{
    return juce::exactlyEqual (a.startTime, b.startTime) && juce::exactlyEqual (a.endTime, b.endTime)
        && juce::exactlyEqual (a.lowHz, b.lowHz) && juce::exactlyEqual (a.highHz, b.highHz);
}

/** True if two documents hold the same drawing (objects in order, their settings, the paint). */
bool sameDrawing (const freqart::ArtDocument& a, const freqart::ArtDocument& b)
{
    if (a.objects.size() != b.objects.size() || a.hasPaint() != b.hasPaint() || (a.hasPaint() && a.paint != b.paint))
        return false;

    for (size_t i = 0; i < a.objects.size(); ++i)
    {
        const auto& x = a.objects[i];
        const auto& y = b.objects[i];

        if (x.id != y.id || ! samePlacement (x, y) || x.text != y.text || ! juce::exactlyEqual (x.intensity, y.intensity)
            || x.fontIndex != y.fontIndex || x.textStyle != y.textStyle || x.imageMode != y.imageMode)
            return false;
    }

    return true;
}

/** Uses the editor as a person would, with synthetic mouse and key events, and checks what each
    gesture does to the drawing. Everything is undone again, so the drawing ends as it began. */
void checkInteractions (freqart::FreqArtEditor& editor, freqart::FreqArtProcessor& processor, juce::Component& content,
                        const juce::Uuid& textId, const juce::Uuid& imageId,
                        const std::function<void (const juce::String&, bool)>& save)
{
    using namespace freqart;

    auto* canvas = find<ui::CanvasView> (editor, "canvas");
    auto* toolbar = find<ui::shell::Toolbar> (editor, "toolbar");

    if (canvas == nullptr || toolbar == nullptr)
        return;

    const auto start = processor.getDocument();
    const auto scale = processor.getScale();
    const auto a = canvas->getDrawingArea();
    const auto w = a.getWidth();

    const auto key = [&] (const juce::KeyPress& k) { content.keyPressed (k); pump (20); };
    const auto undo = [&] { key (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)); };
    const auto redo = [&] { key (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)); };
    const auto shown = [&] (const juce::String& id)
    {
        auto* c = findComponentWithId (editor, id);
        return c != nullptr && c->isVisible();
    };
    const auto object = [&] (const juce::Uuid& id)
    {
        const auto* o = processor.getDocument().find (id);
        return o != nullptr ? *o : ArtObject();
    };

    const auto text0 = object (textId);
    const auto textRect = onCanvas (*canvas, text0, scale);

    // ---- selecting ------------------------------------------------------------------------------
    editor.selectObject (juce::Uuid::null());
    pump (20);
    clickOn (*canvas, textRect.getCentre());
    pump (20);
    check (shown ("inspector-text") && shown ("inspector-band"), "clicking the text selects it (the inspector shows it)");

    clickOn (*canvas, { a.getRight() - 6.0f, a.getY() + 6.0f });
    pump (20);
    check (! shown ("inspector-band"), "clicking empty canvas deselects");

    // ---- moving -------------------------------------------------------------------------------
    dragOn (*canvas, textRect.getCentre(), textRect.getCentre() + juce::Point<float> (60.0f, 0.0f));
    {
        const auto t = object (textId);
        check (std::abs ((t.startTime - text0.startTime) - 60.0f / w) < 1.0e-4f
               && std::abs ((t.endTime - text0.endTime) - 60.0f / w) < 1.0e-4f, "dragging the text moves it in time");
        check (juce::exactlyEqual (t.lowHz, text0.lowHz) && juce::exactlyEqual (t.highHz, text0.highHz),
               "a sideways drag keeps the frequencies exactly");
    }
    undo();
    check (samePlacement (object (textId), text0), "a drag is one undo step");

    dragOn (*canvas, textRect.getCentre(), textRect.getCentre() + juce::Point<float> (70.0f, 24.0f), juce::ModifierKeys::shiftModifier);
    check (object (textId).startTime > text0.startTime && juce::exactlyEqual (object (textId).lowHz, text0.lowHz),
           "Shift keeps a drag on one axis");
    undo();

    dragOn (*canvas, textRect.getCentre(), textRect.getCentre() + juce::Point<float> (0.0f, 40.0f));
    {
        const auto r = onCanvas (*canvas, object (textId), scale);
        check (std::abs (r.getY() - (textRect.getY() + 40.0f)) < 0.5f && std::abs (r.getHeight() - textRect.getHeight()) < 0.5f
               && juce::exactlyEqual (object (textId).startTime, text0.startTime), "a vertical drag moves it down the scale");
    }
    undo();

    // ---- resizing -----------------------------------------------------------------------------
    editor.selectObject (textId);
    pump (20);
    {
        const auto handle = juce::Point<float> (textRect.getRight(), textRect.getCentreY());
        dragOn (*canvas, handle, handle + juce::Point<float> (-80.0f, 15.0f));
        const auto t = object (textId);
        check (std::abs ((t.endTime - text0.endTime) + 80.0f / w) < 1.0e-4f && juce::exactlyEqual (t.startTime, text0.startTime)
               && juce::exactlyEqual (t.lowHz, text0.lowHz) && juce::exactlyEqual (t.highHz, text0.highHz),
               "the right-hand handle moves the end only");
        undo();
        check (samePlacement (object (textId), text0), "a resize is one undo step");
    }

    // ---- nudging (keys sent to the window, as from any focused control) ----------------------------
    editor.selectObject (textId);
    key (juce::KeyPress (juce::KeyPress::rightKey));
    key (juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0));
    check (std::abs ((object (textId).startTime - text0.startTime) - 11.0f / w) < 1.0e-4f, "arrows nudge 1 px, Shift+arrow 10 px");
    undo();
    check (samePlacement (object (textId), text0), "a held arrow (its repeats) is one undo step");

    // ---- a drag in progress shows the frequency range -------------------------------------------
    {
        const auto image0 = object (imageId);
        const auto from = onCanvas (*canvas, image0, scale).getCentre();
        const auto to = from + juce::Point<float> (-40.0f, -60.0f);
        dragOn (*canvas, from, to, {}, false);
        save ("dragging", true);
        canvas->mouseUp (mouseEventAt (*canvas, to, from, {}, 1, true));
        check (object (imageId).startTime < image0.startTime && object (imageId).highHz > image0.highHz, "dragging the picture moves it");
        undo();
        check (samePlacement (object (imageId), image0), "... and undo puts it back");
    }

    // ---- the inspector ----------------------------------------------------------------------------
    editor.selectObject (textId);
    pump (20);

    if (auto* band = find<ui::SegmentedControl> (editor, "inspector-band"))
    {
        check (band->getSelectedIndex() == static_cast<int> (Band::high), "the inspector shows the text on the High band");
        clickOn (*band, { static_cast<float> (band->getWidth()) / 8.0f, static_cast<float> (band->getHeight()) * 0.5f });
        pump (20);
        const auto t = object (textId);
        const auto low = bandRange (Band::low, processor.getScale());
        check (std::abs (t.lowHz - low.low) < 0.01 && std::abs (t.highHz - low.high) < 0.01
               && juce::exactlyEqual (t.startTime, text0.startTime), "Band: Low moves the text into the lows");
        check (band->getSelectedIndex() == static_cast<int> (Band::low), "... and shows Low as chosen");
        undo();
        check (samePlacement (object (textId), text0) && band->getSelectedIndex() == static_cast<int> (Band::high),
               "undo moves it back (and the inspector follows)");
    }

    if (auto* intensity = find<juce::Slider> (editor, "inspector-intensity"))
    {
        intensity->setValue (40.0, juce::sendNotificationSync);
        check (std::abs (object (textId).intensity - 0.4f) < 1.0e-4f, "the Intensity bar sets the intensity");
        undo();
        check (std::abs (intensity->getValue() - 100.0) < 1.0e-6 && juce::exactlyEqual (object (textId).intensity, text0.intensity),
               "undo puts the intensity and its bar back");
    }

    if (auto* field = find<juce::TextEditor> (editor, "inspector-text"))
    {
        check (field->getText() == text0.text, "the text field shows the text");
        field->moveCaretToEnd();
        field->insertTextAtCaret (" WORLD");
        pump (30);
        field->insertTextAtCaret ("!");
        pump (30);
        check (object (textId).text == text0.text + " WORLD!", "typing changes the text on the canvas");
        undo();
        check (object (textId).text == text0.text && field->getText() == text0.text, "typing is one undo step, and the field follows undo");
    }

    if (auto* fill = find<juce::Button> (editor, "inspector-fill"))
    {
        fill->onClick();
        check (juce::approximatelyEqual (object (textId).startTime, 0.04f) && juce::approximatelyEqual (object (textId).endTime, 0.96f)
               && juce::exactlyEqual (object (textId).lowHz, text0.lowHz), "Fill length stretches the text across the length");
        undo();
    }

    if (auto* font = find<juce::ComboBox> (editor, "inspector-font"))
    {
        font->setSelectedId (3, juce::sendNotificationSync);
        check (object (textId).fontIndex == 2, "the Font menu changes the text's font");
        undo();
        check (object (textId).fontIndex == text0.fontIndex && font->getSelectedId() == text0.fontIndex + 1, "... and undo changes it back");
    }

    if (auto* style = find<ui::SegmentedControl> (editor, "inspector-style"))
    {
        clickOn (*style, { static_cast<float> (style->getWidth()) * 0.75f, static_cast<float> (style->getHeight()) * 0.5f });
        check (object (textId).textStyle == TextStyle::outline, "Style: Outline draws the text as outlines");
        undo();
    }

    editor.selectObject (imageId);
    pump (20);

    if (auto* modeControl = find<ui::SegmentedControl> (editor, "inspector-mode"))
    {
        check (shown ("inspector-mode") && ! shown ("inspector-text"), "selecting the picture shows the picture's settings");
        clickOn (*modeControl, { static_cast<float> (modeControl->getWidth()) * 0.875f, static_cast<float> (modeControl->getHeight()) * 0.5f });
        check (object (imageId).imageMode == ImageMode::outline, "Mode: Outline draws the picture's edges");
        undo();
        check (object (imageId).imageMode == ImageMode::automatic, "... and undo goes back to Auto");
    }

    // ---- dropping a picture file on the canvas -----------------------------------------------------
    {
        const auto file = juce::File::createTempFile (".png");
        writePng (makeTestPicture(), file);
        const juce::StringArray files { file.getFullPathName() };
        const auto count = processor.getDocument().objects.size();
        const auto at = juce::Point<int> (juce::roundToInt (a.getX() + w * 0.3f), juce::roundToInt (a.getY() + a.getHeight() * 0.85f));

        check (canvas->isInterestedInFileDrag (files), "the canvas takes picture files");
        canvas->fileDragEnter (files, at.x, at.y);
        canvas->filesDropped (files, at.x, at.y);
        pump (20);

        const auto& objects = processor.getDocument().objects;
        check (objects.size() == count + 1 && objects.back().type == ArtObject::Type::image
               && ui::actions::bandOf (objects.back(), scale) == static_cast<int> (Band::low) && shown ("inspector-mode"),
               "a dropped picture lands in the band under the mouse, selected");
        undo();
        check (processor.getDocument().objects.size() == count, "... and undo removes it");
        file.deleteFile();
    }

    // ---- painting and erasing -------------------------------------------------------------------
    editor.selectObject (juce::Uuid::null());
    key (juce::KeyPress ('b', {}, 'b'));
    toolbar->brushSize.getSlider().setValue (24.0, juce::sendNotificationSync);
    toolbar->brushSoftness.getSlider().setValue (0.0, juce::sendNotificationSync);
    toolbar->brushStrength.getSlider().setValue (100.0, juce::sendNotificationSync);
    {
        // Low down on the right, where the drawing has nothing.
        const auto from = juce::Point<float> (a.getX() + w * 0.60f, a.getBottom() - a.getHeight() * 0.05f);
        const auto to = from + juce::Point<float> (w * 0.25f, -a.getHeight() * 0.03f);
        const auto middle = (from + to) * 0.5f;
        check (paintAt (start, *canvas, middle) == 0, "(the stroke's place starts empty)");

        dragOn (*canvas, from, to);
        check (paintAt (processor.getDocument(), *canvas, middle) == 255, "a Brush stroke paints along its path");
        check (paintAt (processor.getDocument(), *canvas, middle + juce::Point<float> (0.0f, -9.0f)) == 255
               && paintAt (processor.getDocument(), *canvas, middle + juce::Point<float> (0.0f, -16.0f)) == 0,
               "... as wide as the brush on screen (24 px)");

        // Drawn at once: the message loop would notice that the real mouse is elsewhere.
        processor.rebuildPlanIfNeeded();
        pump (60);
        canvas->mouseMove (mouseEventAt (*canvas, to, to, {}));
        save ("painting", false);
        canvas->mouseExit (mouseEventAt (*canvas, { -10.0f, -10.0f }, to, {}));

        // Erase at half strength over the same path halves what is there.
        key (juce::KeyPress ('e', {}, 'e'));
        toolbar->brushStrength.getSlider().setValue (50.0, juce::sendNotificationSync);
        dragOn (*canvas, from, to);
        const auto halved = paintAt (processor.getDocument(), *canvas, middle);
        check (halved > 110 && halved < 145, "Erase at 50 % strength halves the paint (" + juce::String (halved) + ")");

        undo();
        check (paintAt (processor.getDocument(), *canvas, middle) == 255, "an erase stroke is one undo step");
        undo();
        check (sameDrawing (processor.getDocument(), start), "a brush stroke is one undo step");
        redo();
        check (paintAt (processor.getDocument(), *canvas, middle) == 255, "redo paints it again");
        undo();

        toolbar->brushStrength.getSlider().setValue (100.0, juce::sendNotificationSync);
        toolbar->brushSoftness.getSlider().setValue (50.0, juce::sendNotificationSync);
        toolbar->brushSize.getSlider().setValue (16.0, juce::sendNotificationSync);
        key (juce::KeyPress ('v', {}, 'v'));
    }

    // ---- the toolbar and the header -------------------------------------------------------------
    if (auto* addText = find<juce::Button> (editor, "add-text"))
    {
        const auto count = processor.getDocument().objects.size();
        addText->onClick();
        pump (20);
        check (processor.getDocument().objects.size() == count + 1 && shown ("inspector-text"),
               "Add text adds a text object and selects it");
        key (juce::KeyPress (juce::KeyPress::deleteKey));
        check (processor.getDocument().objects.size() == count, "... which Delete removes");
        undo();
        undo();
    }

    if (auto* clear = find<juce::Button> (editor, "clear"))
    {
        clear->onClick();
        pump (30);
        check (processor.getDocument().isEmpty() && ! clear->isEnabled(), "Clear empties the drawing and greys itself out");
        save ("empty", true);

        if (auto* undoButton = find<juce::Button> (editor, "undo"))
        {
            undoButton->onClick();
            pump (30);
        }

        check (sameDrawing (processor.getDocument(), start) && clear->isEnabled(), "the Undo button brings the drawing back");
    }

    // ---- the header's view switch, the footer's Preview -------------------------------------------
    if (auto* view = find<ui::SegmentedControl> (editor, "view-switch"))
    {
        clickOn (*view, { static_cast<float> (view->getWidth()) * 0.75f, static_cast<float> (view->getHeight()) * 0.5f });
        check (shown ("live-view") && ! shown ("canvas") && ! toolbar->brushTool.isEnabled() && toolbar->addTextButton.isEnabled(),
               "Live shows the live view (drawing tools off, adding still on)");
        clickOn (*view, { static_cast<float> (view->getWidth()) * 0.25f, static_cast<float> (view->getHeight()) * 0.5f });
        check (shown ("canvas") && ! shown ("live-view") && toolbar->brushTool.isEnabled(), "Canvas goes back to the canvas");
    }

    if (auto* preview = find<juce::Button> (editor, "preview"))
    {
        clickOn (*preview, preview->getLocalBounds().getCentre().toFloat());
        pump (20);
        check (processor.isPreviewing(), "the Preview button starts the preview");
        clickOn (*preview, preview->getLocalBounds().getCentre().toFloat());
        pump (20);
        check (! processor.isPreviewing(), "... and stops it");
    }

    // ---- the analyser scale (Settings) and the length --------------------------------------------
    if (auto* scaleControl = find<ui::SegmentedControl> (editor, "scale"))
    {
        clickOn (*scaleControl, { static_cast<float> (scaleControl->getWidth()) * 0.5f, static_cast<float> (scaleControl->getHeight()) * 0.5f });
        setParameter (processor, params::id::length, 2.0f);  // 1 bar
        pump (40);
        check (processor.getScale().type == ScaleType::linear, "Settings: Linear switches the analyser scale");
        save ("linear", true);

        clickOn (*scaleControl, { 4.0f, static_cast<float> (scaleControl->getHeight()) * 0.5f });
        setParameter (processor, params::id::length, static_cast<float> (params::defaultLengthIndex));
        pump (40);
        check (processor.getScale().type == ScaleType::log, "... and Log switches it back");
    }

    check (sameDrawing (processor.getDocument(), start), "after all that, undo has taken the drawing back to where it began");
    editor.selectObject (juce::Uuid::null());
    pump (30);
}

int countDifferentPixels (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds())
        return std::numeric_limits<int>::max();

    auto count = 0;

    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                ++count;

    return count;
}

bool checkWav (const juce::File& file, double expectedSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return false;

    const auto seconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    std::cout << "bounce: " << file.getFullPathName() << ", " << seconds << " s, " << reader->numChannels << " ch, "
              << reader->bitsPerSample << " bit" << std::endl;
    return std::abs (seconds - expectedSeconds) < 0.01 && reader->numChannels == 2;
}

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_LINUX || JUCE_BSD
    // As a standalone app JUCE installs X error handlers that tolerate a virtual display without
    // a window manager (Xlib's own would end the process). No application object is created.
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return nullptr; };
   #endif

    const juce::ScopedJuceInitialiser_GUI gui;

    const auto outputDir = juce::File::getCurrentWorkingDirectory()
                               .getChildFile (argc > 1 ? juce::String (argv[1]) : juce::String ("snapshots"));
    const auto pixelScale = juce::jlimit (0.5f, 4.0f, argc > 2 ? juce::String (argv[2]).getFloatValue() : 2.0f);

    if (! outputDir.createDirectory())
    {
        std::cerr << "Cannot create " << outputDir.getFullPathName() << std::endl;
        return 1;
    }

    using namespace freqart;

    FreqArtProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    {
        const std::unique_ptr<FreqArtEditor> editor (dynamic_cast<FreqArtEditor*> (processor.createEditor()));

        if (editor == nullptr)
        {
            std::cerr << "The processor made no FreqArtEditor" << std::endl;
            return 1;
        }

        check (editor->getWidth() == FreqArtEditor::baseWidth && editor->getHeight() == FreqArtEditor::baseHeight,
               "a new editor opens at 1120 x 720");

        const auto save = [&] (const juce::String& name, float scale, bool settle = true)
        {
            processor.rebuildPlanIfNeeded();

            if (settle)
                pump (60);

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
            const auto file = outputDir.getChildFile ("freqart-" + name + ".png");

            if (writePng (image, file))
                std::cout << file.getFullPathName() << std::endl;
            else
                check (false, "write " + file.getFullPathName());
        };

        auto* content = findComponentWithId (*editor, "content");
        check (content != nullptr, "the editor has its content");

        if (content == nullptr)
            return 1;

        // Let attachments, the first plan and any async updates settle.
        pump (150);
        save ("default", pixelScale);

        // ---- the rich drawing -----------------------------------------------------------------
        juce::Uuid textId, imageId;
        makeRichDrawing (processor, textId, imageId);
        pump (100);
        save ("rich", pixelScale);

        processor.setPreview (true);
        playFor (processor, 2.2);
        pump (80);
        check (processor.isSounding(), "Preview makes the processor sound");
        save ("playing", pixelScale);
        processor.setPreview (false);
        playFor (processor, 0.1);
        pump (60);

        editor->selectObject (textId);
        save ("text-selected", pixelScale);

        editor->selectObject (imageId);
        save ("image-selected", pixelScale);

        editor->selectObject (juce::Uuid::null());

        // ---- shortcuts ------------------------------------------------------------------------
        content->keyPressed (juce::KeyPress ('b', juce::ModifierKeys(), 'b'));
        pump (30);

        if (auto* brush = find<juce::Button> (*editor, "tool-brush"))
            check (brush->getToggleState(), "B selects the Brush tool");

        save ("brush", pixelScale);

        content->keyPressed (juce::KeyPress ('e', juce::ModifierKeys(), 'e'));
        pump (30);

        if (auto* erase = find<juce::Button> (*editor, "tool-erase"))
            check (erase->getToggleState(), "E selects the Erase tool");

        content->keyPressed (juce::KeyPress ('v', juce::ModifierKeys(), 'v'));
        pump (30);

        if (auto* select = find<juce::Button> (*editor, "tool-select"))
            check (select->getToggleState(), "V selects the Select tool");

        // Undo takes the rich drawing back to the default one; redo restores it.
        check (processor.canUndo(), "the rich drawing can be undone");
        const auto objectsBefore = processor.getDocument().objects.size();
        content->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (processor.getDocument().objects.size() != objectsBefore && processor.canRedo(), "Cmd+Z undoes");
        pump (30);

        if (auto* redoButton = find<juce::Button> (*editor, "redo"))
            check (redoButton->isEnabled(), "Redo is enabled after an undo");

        content->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
        check (processor.getDocument().objects.size() == objectsBefore, "Cmd+Shift+Z redoes");

        // Delete removes the selection (from anywhere in the window); undo brings it back.
        editor->selectObject (textId);
        content->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        check (processor.getDocument().find (textId) == nullptr, "Delete removes the selected object");
        content->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (processor.getDocument().find (textId) != nullptr, "undo brings the deleted object back");
        editor->selectObject (juce::Uuid::null());
        pump (60);

        // ---- using the editor with the mouse ------------------------------------------------------
        checkInteractions (*editor, processor, *content, textId, imageId,
                           [&] (const juce::String& name, bool settle) { save (name, pixelScale, settle); });

        // ---- the look of standard controls: a menu, a tooltip, typing a value --------------------------
        if (auto* length = find<juce::ComboBox> (*editor, "length"))
        {
            // Off screen, an open menu closes itself at its first timer tick: draw it at once.
            length->showPopup();
            save ("menu", pixelScale, false);
            juce::PopupMenu::dismissAllActiveMenus();
            pump (60);
        }

        if (auto* tips = find<juce::TooltipWindow> (*editor, "tooltips"))
        {
            if (auto* density = dynamic_cast<juce::Slider*> (findComponentWithId (*editor, "density")))
            {
                tips->displayTip (density->localPointToGlobal (juce::Point<int> (38, 60)), density->getTooltip());
                save ("tooltip", pixelScale);
                tips->hideTip();
            }
        }

        if (auto* level = findComponentWithId (*editor, "level"))
        {
            for (auto* child : level->getChildren())
            {
                if (auto* valueBox = dynamic_cast<juce::Label*> (child))
                {
                    // (Off screen, JUCE asserts in debug builds that the field cannot take the
                    // keyboard focus; the field opens all the same.)
                    valueBox->showEditor();
                    check (valueBox->getCurrentTextEditor() != nullptr, "a knob's value can be typed");
                    save ("edit", pixelScale);
                    valueBox->hideEditor (true);
                }
            }
        }

        // ---- live view --------------------------------------------------------------------------
        editor->showLiveView (true);

        if (auto* live = find<ui::LiveView> (*editor, "live-view"))
        {
            check (live->isVisible(), "the live view is shown");
            live->fillFromPlanForSnapshot (live->getVisibleSeconds());
        }

        save ("live", pixelScale);
        editor->showLiveView (false);

        // ---- panels -------------------------------------------------------------------------------
        editor->showSettings (true);
        save ("settings", pixelScale);

        if (auto* reset = find<juce::Button> (*editor, "reset-drawing"))
        {
            reset->onClick();
            save ("settings-reset", pixelScale);
        }

        content->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* panel = findComponentWithId (*editor, "settings-panel"))
            check (! panel->isVisible(), "Esc closes the settings");

        editor->showHelp (true);
        save ("help", pixelScale);
        editor->showHelp (false);

        // ---- export -------------------------------------------------------------------------------
        if (auto* exporter = find<ui::shell::ExportPanel> (*editor, "export"))
        {
            auto* chip = findComponentWithId (*editor, "export-chip");
            auto* saveButton = find<juce::Button> (*editor, "export-save");
            const auto updating = "Updating" + juce::String::fromUTF8 ("\xe2\x80\xa6");
            const auto chipSays = [&] (const juce::String& text) { return chip != nullptr && chip->isVisible() && chip->getTitle() == text; };

            // Shown, as in a host: the export renders by itself only while it can be seen.
            editor->setVisible (true);

            if (auto* bounce = find<juce::Button> (*editor, "bounce"))
            {
                check (bounce->isEnabled(), "Bounce WAV is enabled for a drawing");
                bounce->onClick();
            }

            check (exporter->isBouncing(), "the bounce runs in the background");
            check (exporter->waitForBounce (60000), "the bounce finishes");
            check (exporter->hasCurrentFile(), "the bounce made a file: " + exporter->getLastError());

            const auto first = exporter->getBounceFile();
            check (checkWav (first, processor.getLengthSeconds()), "the bounce is one pass, stereo");
            check (first.getFileName().startsWith ("FreqArt HELLO "), "the file is named after the drawing's text: " + first.getFileName());
            check (chipSays (first.getFileName()) && saveButton != nullptr && saveButton->isEnabled(), "ready: the chip shows the file's name");
            save ("export", pixelScale);

            // A change: at once the chip says "Updating..." and hands nothing out, ...
            setParameter (processor, params::id::density, 200.0f);
            exporter->refresh();
            check (chipSays (updating) && exporter->getFileToHandOut() == juce::File() && saveButton != nullptr && ! saveButton->isEnabled(),
                   "updating: the chip says so, and cannot be dragged or saved");
            save ("export-updating", pixelScale);
            check (chipSays (updating) && ! exporter->hasCurrentFile(), "... while the drawing settles");

            // ... until the new file takes its place by itself, under a new name.
            for (int waited = 0; waited < 60000 && ! exporter->hasCurrentFile(); waited += 20)
                pump (20);

            const auto second = exporter->getBounceFile();
            check (exporter->hasCurrentFile() && second != first && checkWav (second, processor.getLengthSeconds()),
                   "the new drawing is rendered without a click");
            check (chipSays (second.getFileName()) && second.getFileName() != first.getFileName() && saveButton != nullptr && saveButton->isEnabled(),
                   "ready again: the chip shows the new name");
            check (! first.exists(), "the file it replaces (never dragged out) is gone");

            editor->setVisible (false);
        }

        // ---- window sizes -------------------------------------------------------------------------
        setWindowScale (*editor, 1.5f);
        pump (60);
        check (processor.getEditorWidth() == 1680 && processor.getEditorHeight() == 1080, "the window size is remembered");
        save ("150", 1.0f);

        setWindowScale (*editor, 0.75f);
        pump (60);
        save ("75", 1.0f);

        setWindowScale (*editor, 1.25f);
        pump (30);

        // ---- the live view in a real (virtual) window: its own timer turns the analyser on,
        // scrolls what the processor plays, and scrolls silence once the host stops calling.
        editor->addToDesktop (0);
        editor->setVisible (true);
        pump (200);

        if (auto* live = find<ui::LiveView> (*editor, "live-view"); live != nullptr && editor->isShowing())
        {
            editor->showLiveView (true);
            pump (100);
            check (processor.getAnalyzer().isActive(), "the live view, on screen, turns the analyser on");

            processor.setPreview (true);

            for (int i = 0; i < 20; ++i)
            {
                playFor (processor, 0.05);
                pump (50);
            }

            processor.setPreview (false);
            playFor (processor, 0.1);
            pump (600);  // past the point where the view stops waiting for the host

            const auto before = live->createComponentSnapshot (live->getLocalBounds());
            pump (400);
            const auto after = live->createComponentSnapshot (live->getLocalBounds());
            check (countDifferentPixels (before, after) > 1000, "with the host silent, the live view keeps scrolling");

            editor->showLiveView (false);
            pump (100);
            check (! processor.getAnalyzer().isActive(), "hiding the live view turns the analyser off");
        }
        else
        {
            std::cout << "(no desktop: the live view's own timer was not checked)" << std::endl;
        }

        editor->removeFromDesktop();
        pump (30);
    }

    // A new editor (the window closed and opened again) comes back at the remembered size.
    {
        const std::unique_ptr<juce::AudioProcessorEditor> again (processor.createEditor());
        check (again != nullptr && again->getWidth() == 1400 && again->getHeight() == 900,
               "a reopened editor has the remembered size");
    }

    // Deliver anything still queued before JUCE shuts down.
    pump (50);
    processor.releaseResources();

    if (failed)
        std::cerr << "Some checks failed" << std::endl;
    else
        std::cout << checksRun << " checks passed" << std::endl;

    return failed ? 1 : 0;
}
