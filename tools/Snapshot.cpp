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
//   freqart-layers.png           the layer list: five objects, one hidden, one selected from the
//                                list, the mouse over another row
//   freqart-layers-hidden.png    the hidden object selected from the list (its outline on the canvas)
//   freqart-layers-drag.png      a row being dragged to another place in the list
//   freqart-layers-menu.png      an object's right-click menu on the canvas
//   freqart-lows-brush.png       Lows (Smooth) at the foot of the panel, under the brush settings
//   freqart-lows-stripes.png     the rich drawing with Lows on Stripes (the layers PNGs are Smooth)
//   freqart-linear.png           the Linear analyser scale and a 1 bar length (beat labels)
//   freqart-repeat.png           Transport with Repeat on ("Repeats every 4 bars from bar 1")
//   freqart-midi-repeat.png      MIDI note with Repeat on ("Loops while a note is held")
//   freqart-start-bar.png        a single pass from bar 9: the start bar field, bars 9 to 12 on
//                                the canvas
//   freqart-start-bar-edit.png   typing a start bar
//   freqart-75.png               window at 75 %, one image pixel per logical pixel
//   freqart-100.png              window at 100 %, one image pixel per logical pixel
//   freqart-150.png              window at 150 %, one image pixel per logical pixel
//   freqart-200.png              window at 200 %, one image pixel per logical pixel
//
// and for FreqArt FX (the effect variant: the same editor with Mode and Depth in the header)
//
//   freqart-fx-default.png       a fresh FreqArt FX (Carve: the default drawing dark in the song)
//   freqart-fx-carve.png         the rich drawing in Carve: dark shapes on the stand-in song, the
//                                export saying "Bounce the track in your DAW"
//   freqart-fx-carve-draw.png    Carve + Draw: the sines bright in a hole a little wider than them
//   freqart-fx-stencil.png       Stencil: the drawing filled with the song, the rest dark
//   freqart-fx-draw.png          Draw: the sines on top of the song, Depth greyed, Bounce WAV back
//   freqart-fx-shallow.png       Carve at a Depth of -24 dB, typed into the field
//   freqart-fx-live.png          the live view: a stand-in song carved by the real processor
//   freqart-fx-help.png          FreqArt FX's help panel
//   freqart-fx-tooltip.png       the Mode control's tooltip
//   freqart-fx-75.png, -100.png, -200.png   window sizes, one image pixel per logical pixel
//
// All but the last two are rendered at <pixel-scale> image pixels per logical pixel (default 2, as
// on a HiDPI display). Along the way it checks keyboard shortcuts, undo / redo, the bounce (a
// WAV file of the right length, named on the chip, which says "Updating..." after a change until
// a new file takes its place), that the window size is remembered, and drives the editor with
// synthetic mouse events (select, move, resize, nudge, paint, erase, the inspector, the toolbar,
// the footer's Trigger, Repeat, start bar and Preview, the layer list, picking overlapping
// objects, Lows), checking what each does to the drawing or the settings and that undo takes it back.
// It exits with 1 if a file cannot be written or a check fails. At the end it puts the window on
// the (virtual) desktop to check the live view's own timer: the analyser on while shown,
// scrolling, off when hidden.
//
// For FreqArt FX it checks the header's layout at every size, Mode and Depth driven with the
// mouse and the keyboard (and what they grey and show), the export's hint, Preview and a carve
// through the processor.
//
// On Linux run it under a virtual display:  xvfb-run -a FreqArtSnapshot out

#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/FrequencyScale.h"
#include "ui/Actions.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/CanvasAxes.h"
#include "ui/LayerList.h"
#include "ui/LiveView.h"
#include "ui/ShellExport.h"
#include "ui/ShellFooter.h"
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
    picture.fileName = "logo.png";
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

void clickOn (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {})
{
    c.mouseDown (mouseEventAt (c, at, at, mods.withFlags (juce::ModifierKeys::leftButtonModifier)));
    c.mouseUp (mouseEventAt (c, at, at, mods));
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

/** The footer's timing controls, as a person would use them: Trigger, Repeat, the start bar
    field (drag, type, Enter, Esc) and what the hint line under them says; Preview following
    Repeat; the canvas numbering the song's bars. Puts the settings back as they were. */
void checkFooter (freqart::FreqArtEditor& editor, freqart::FreqArtProcessor& processor,
                  const std::function<void (const juce::String&, bool)>& save)
{
    using namespace freqart;

    auto* footer = find<ui::shell::Footer> (editor, "footer");
    auto* trigger = find<ui::SegmentedControl> (editor, "trigger");
    auto* repeat = find<juce::Button> (editor, "repeat");
    auto* startBar = find<ui::shell::NumberField> (editor, "start-bar");
    auto* preview = find<juce::Button> (editor, "preview");

    if (footer == nullptr || trigger == nullptr || repeat == nullptr || startBar == nullptr || preview == nullptr)
        return;

    const auto value = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load(); };
    const auto settle = [&] { pump (60); footer->refresh(); };
    const auto middleOf = [] (juce::Component& c, float across) { return juce::Point<float> (static_cast<float> (c.getWidth()) * across,
                                                                                               static_cast<float> (c.getHeight()) * 0.5f); };
    settle();

    // ---- the defaults: a single pass from bar 1 ----------------------------------------------
    check (trigger->getOptions() == juce::StringArray { "Transport", "MIDI" }, "Trigger offers Transport and MIDI (no Loop)");
    check (! repeat->getToggleState() && startBar->isVisible() && footer->getTriggerHint() == "Plays once from bar 1",
           "by default the drawing plays once from bar 1, and the footer says so, with the bar as a field");

    // ---- the layout: in line, inside the footer, nothing overlapping ------------------------------
    {
        juce::Array<juce::Component*> row;

        for (const auto* id : { "density", "contrast", "level", "length", "trigger", "repeat", "tempo-source", "bpm", "preview", "export" })
            row.add (findComponentWithId (*footer, id));

        auto apart = ! row.contains (nullptr);

        for (int i = 0; apart && i < row.size(); ++i)
            for (int j = i + 1; j < row.size(); ++j)
                apart = apart && ! row[i]->getBounds().intersects (row[j]->getBounds());

        check (apart, "the footer's controls do not overlap");

        if (apart)
        {
            check (trigger->getY() == repeat->getY() && trigger->getHeight() == repeat->getHeight()
                   && repeat->getRight() <= findComponentWithId (*footer, "tempo-source")->getX() - 16,
                   "Repeat sits in line with Trigger, clear of Tempo");
            check (findComponentWithId (*footer, "bpm")->getRight() <= footer->getWidth() - 280 - 24,
                   "the timing controls end a margin before the Preview column");
        }

        check (startBar->getX() > trigger->getX() + 90 && startBar->getRight() <= repeat->getRight()
               && startBar->getY() > trigger->getBottom() + 6 && startBar->getBottom() < footer->getHeight() - 8,
               "the start bar field sits in the hint line under Trigger, after its words");
    }

    // ---- the start bar: drag, type, Enter, Esc ---------------------------------------------------
    dragOn (*startBar, middleOf (*startBar, 0.5f), middleOf (*startBar, 0.5f) + juce::Point<float> (0.0f, -32.0f));
    check (juce::approximatelyEqual (value (params::id::startBar), 9.0f), "dragging the start bar up 32 px adds 8 bars ("
                                                                          + juce::String (value (params::id::startBar)) + ")");
    settle();
    check (footer->getTriggerHint() == "Plays once from bar 9" && processor.getSinglePassStartBar() == 9,
           "... and the hint says where it now plays");
    save ("start-bar", true);

    {
        const auto marks = ui::axes::timeMarks (params::defaultLengthIndex, 4, 4, 600.0f, processor.getSinglePassStartBar());
        juce::StringArray labels;

        for (const auto& mark : marks)
            if (mark.label.isNotEmpty())
                labels.add (mark.label);

        check (labels == juce::StringArray { "9", "10", "11", "12" }, "the canvas numbers the song's bars: 9 to 12 ("
                                                                       + labels.joinIntoString (" ") + ")");
    }

    const auto typeInto = [&] (const juce::String& text, const juce::KeyPress& key, const juce::String& snapshot)
    {
        clickOn (*startBar, middleOf (*startBar, 0.5f));   // a single click opens the field
        juce::TextEditor* field = nullptr;

        for (auto* child : startBar->getChildren())
            if (auto* t = dynamic_cast<juce::TextEditor*> (child))
                field = t;

        check (field != nullptr && startBar->isEditing(), "a click on the start bar opens it for typing");

        if (field == nullptr)
            return;

        field->setText (text, true);

        if (snapshot.isNotEmpty())
            save (snapshot, false);

        field->keyPressed (key);
        pump (30);
    };

    typeInto ("17", juce::KeyPress (juce::KeyPress::returnKey), "start-bar-edit");
    check (! startBar->isEditing() && juce::approximatelyEqual (value (params::id::startBar), 17.0f), "typing 17 and Enter sets bar 17");
    typeInto ("50", juce::KeyPress (juce::KeyPress::escapeKey), {});
    check (! startBar->isEditing() && juce::approximatelyEqual (value (params::id::startBar), 17.0f), "Esc leaves it as it was");
    typeInto ("5000", juce::KeyPress (juce::KeyPress::returnKey), {});
    check (juce::approximatelyEqual (value (params::id::startBar), 999.0f), "a bar past 999 is 999");
    typeInto ("0", juce::KeyPress (juce::KeyPress::returnKey), {});
    check (juce::approximatelyEqual (value (params::id::startBar), 1.0f), "bar 0 is bar 1");

    // ---- Repeat and Trigger ----------------------------------------------------------------------
    clickOn (*repeat, middleOf (*repeat, 0.5f));
    settle();
    check (value (params::id::repeat) > 0.5f && repeat->getToggleState() && ! startBar->isVisible()
           && footer->getTriggerHint() == "Repeats every 4 bars from bar 1" && processor.getSinglePassStartBar() == 0,
           "Repeat loops the drawing every Length from bar 1 (no start bar to set then)");
    save ("repeat", true);

    clickOn (*trigger, middleOf (*trigger, 0.85f));
    settle();
    check (juce::approximatelyEqual (value (params::id::trigger), 1.0f) && footer->getTriggerHint() == "Loops while a note is held"
           && ! startBar->isVisible(), "MIDI with Repeat loops while a note is held");
    save ("midi-repeat", true);

    clickOn (*repeat, middleOf (*repeat, 0.5f));
    settle();
    check (value (params::id::repeat) < 0.5f && footer->getTriggerHint() == "Each note plays it once" && ! startBar->isVisible(),
           "MIDI without Repeat plays it once per note");

    clickOn (*trigger, middleOf (*trigger, 0.2f));
    settle();
    check (juce::approximatelyEqual (value (params::id::trigger), 0.0f) && startBar->isVisible()
           && footer->getTriggerHint() == "Plays once from bar 1", "back to Transport: once from bar 1, the field back");

    // Automation (or the host) changing Repeat moves the switch too.
    setParameter (processor, params::id::repeat, 1.0f);
    settle();
    check (repeat->getToggleState() && ! startBar->isVisible(), "the Repeat switch follows the parameter");
    setParameter (processor, params::id::repeat, 0.0f);
    settle();

    // ---- Preview follows Repeat: once, then the button turns itself off; or on a loop ------------
    setParameter (processor, params::id::length, 2.0f);   // 1 bar: 2 s
    clickOn (*preview, middleOf (*preview, 0.5f));
    playFor (processor, 1.0);
    settle();
    check (processor.isPreviewing() && preview->getToggleState(), "Preview plays");
    playFor (processor, 1.3);
    settle();
    check (! processor.isPreviewing() && ! preview->getToggleState(), "without Repeat, Preview stops by itself after one pass");

    setParameter (processor, params::id::repeat, 1.0f);
    clickOn (*preview, middleOf (*preview, 0.5f));
    playFor (processor, 4.5);
    settle();
    check (processor.isPreviewing() && preview->getToggleState() && processor.isSounding(), "with Repeat, Preview goes round");
    clickOn (*preview, middleOf (*preview, 0.5f));
    playFor (processor, 0.1);
    settle();
    check (! processor.isPreviewing() && ! preview->getToggleState(), "... until it is pressed again");

    setParameter (processor, params::id::repeat, 0.0f);
    setParameter (processor, params::id::length, static_cast<float> (params::defaultLengthIndex));
    settle();
}

/** The layer list and picking overlapping objects, with synthetic events: selecting from the
    list (no undo step), the keyboard, hiding, reordering (which decides what a click on the
    canvas picks), deleting, clicking again and Alt-clicking on the canvas, the right-click menu,
    adding at the front. Everything is undone again, so the drawing ends as it began. */
void checkLayers (freqart::FreqArtEditor& editor, freqart::FreqArtProcessor& processor,
                  const juce::Uuid& textId, const juce::Uuid& imageId,
                  const std::function<void (const juce::String&, bool)>& save)
{
    using namespace freqart;

    auto* canvas = find<ui::CanvasView> (editor, "canvas");
    auto* list = find<ui::LayerList> (editor, "layer-list");

    if (canvas == nullptr || list == nullptr)
        return;

    const auto start = processor.getDocument();
    const auto scale = processor.getScale();
    const auto shown = [&] (const juce::String& id)
    {
        auto* c = findComponentWithId (editor, id);
        return c != nullptr && c->isVisible();
    };
    const auto row = [&] (int index) { return list->getRowBounds (index).toFloat().getCentre(); };
    const auto selected = [&] { return editor.getSelectedObject(); };
    const auto frontId = [&] { return processor.getDocument().objects.back().id; };
    const auto visible = [&] (const juce::Uuid& id)
    {
        const auto* o = processor.getDocument().find (id);
        return o != nullptr && o->visible;
    };

    // Five objects. Back to front: the rich drawing's text and picture, a line of text in the
    // lows, "WORLD" in the mids, and a picture with a long name over the end of "HELLO".
    auto document = start;
    const auto low = bandRange (Band::low, scale), mid = bandRange (Band::mid, scale), high = bandRange (Band::high, scale);

    auto bass = ArtDocument::makeTextObject ("Bass drop\nsecond line", static_cast<float> (low.low), static_cast<float> (low.high));
    bass.startTime = 0.04f;
    bass.endTime = 0.40f;
    auto world = ArtDocument::makeTextObject ("WORLD", static_cast<float> (mid.low), static_cast<float> (mid.high));
    world.startTime = 0.10f;
    world.endTime = 0.45f;
    auto sunset = ArtDocument::makeImageObject (prepareImportedImage (makeTestPicture()), static_cast<float> (high.low), static_cast<float> (high.high));
    sunset.fileName = "sunset-over-the-harbour-at-dusk.jpeg";
    sunset.startTime = 0.36f;
    sunset.endTime = 0.62f;

    for (const auto& object : { bass, world, sunset })
        document.objects.push_back (object);

    processor.setDocument (document);
    editor.selectObject (juce::Uuid::null());
    pump (40);

    check (shown ("layer-list") && list->getNumObjectRows() == 5, "with nothing selected the inspector lists the layers");

    // ---- selecting from the list --------------------------------------------------------------------
    // Rows are front first: sunset, WORLD, Bass drop, logo.png, HELLO.
    clickOn (*list, row (4));
    pump (20);
    check (selected() == textId && shown ("layer-list") && ! shown ("inspector-band"),
           "clicking a row selects its object and keeps the list");

    list->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    list->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    check (selected() == bass.id, "Up moves the selection up the list");
    list->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    check (selected() == imageId, "Down moves it down");

    list->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    pump (20);
    check (shown ("inspector-mode") && ! shown ("layer-list") && shown ("inspector-layers"),
           "Return opens the selected object's settings, with a way back to the list");

    if (auto* back = find<juce::Button> (editor, "inspector-layers"))
    {
        clickOn (*back, back->getLocalBounds().getCentre().toFloat());
        pump (20);
        check (shown ("layer-list") && selected() == imageId, "\"Layers\" goes back to the list, the selection kept");
    }

    // Selecting is no undo step: the next undo takes back the five objects.
    check (processor.undo() && processor.getDocument().objects.size() == 2, "selecting from the list is no undo step");
    processor.redo();
    pump (20);

    // ---- hiding -------------------------------------------------------------------------------------
    clickOn (*list, list->getEyeBounds (1).toFloat().getCentre());
    pump (20);
    check (! visible (world.id) && visible (sunset.id) && visible (textId), "the eye hides an object");
    check (processor.undo() && visible (world.id), "hiding is one undo step");
    processor.redo();
    pump (20);

    {
        // A hidden object is silent: the drawing sounds as if it were not there at all.
        auto without = processor.getDocument();
        auto shownAgain = processor.getDocument();
        without.remove (world.id);

        if (auto* w = shownAgain.find (world.id))
            w->visible = true;

        Compositor compositor;
        Canvas withHidden, withoutIt, withShown;
        compositor.render (processor.getDocument(), scale, withHidden);
        compositor.render (without, scale, withoutIt);
        compositor.render (shownAgain, scale, withShown);
        check (withHidden.values == withoutIt.values && withShown.values != withHidden.values,
               "a hidden object is silent (the canvas is as if it were not there)");
    }

    // The picture: WORLD hidden, HELLO selected from the list, the mouse over "Bass drop".
    clickOn (*list, row (4));
    list->mouseMove (mouseEventAt (*list, row (2), row (2), {}));
    save ("layers", true);
    list->mouseExit (mouseEventAt (*list, { -5.0f, -5.0f }, row (2), {}));

    clickOn (*list, row (1));
    save ("layers-hidden", true);
    check (selected() == world.id && ! visible (world.id), "a hidden object can be selected from the list");

    // ---- order decides what a click on the canvas picks --------------------------------------------
    const auto overlap = onCanvas (*canvas, start.objects.front(), scale).getIntersection (onCanvas (*canvas, sunset, scale)).getCentre();
    editor.selectObject (juce::Uuid::null());
    pump (20);
    canvas->mouseMove (mouseEventAt (*canvas, overlap + juce::Point<float> (0.0f, 50.0f), overlap, {}));
    clickOn (*canvas, overlap);
    check (selected() == sunset.id, "a click where objects overlap picks the front-most");
    clickOn (*canvas, overlap);
    check (selected() == textId, "clicking again on the same spot picks the one underneath");
    clickOn (*canvas, overlap);
    check (selected() == sunset.id, "... and again goes round to the front");
    clickOn (*canvas, overlap, juce::ModifierKeys::altModifier);
    check (selected() == textId, "Alt-click picks the one underneath");

    editor.selectObject (juce::Uuid::null());
    pump (20);
    dragOn (*list, row (4), list->getRowBounds (0).toFloat().getTopLeft() + juce::Point<float> (60.0f, 3.0f), {}, false);
    save ("layers-drag", false);
    list->mouseUp (mouseEventAt (*list, list->getRowBounds (0).toFloat().getTopLeft() + juce::Point<float> (60.0f, 3.0f), row (4), {}, 1, true));
    check (frontId() == textId, "dragging a row to the top brings its object to the front");

    editor.selectObject (juce::Uuid::null());
    canvas->mouseMove (mouseEventAt (*canvas, overlap + juce::Point<float> (0.0f, 50.0f), overlap, {}));
    clickOn (*canvas, overlap);
    check (selected() == textId, "... where a click on the canvas now picks it");
    processor.undo();
    check (frontId() == sunset.id, "reordering is one undo step");
    pump (20);

    // ---- the right-click menu ------------------------------------------------------------------------
    const auto rightClick = [&]
    {
        canvas->mouseMove (mouseEventAt (*canvas, overlap + juce::Point<float> (0.0f, 50.0f), overlap, {}));
        canvas->mouseDown (mouseEventAt (*canvas, overlap, overlap, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
    };

    editor.selectObject (juce::Uuid::null());
    rightClick();
    check (selected() == sunset.id, "a right-click selects the object under the mouse");
    canvas->mouseUp (mouseEventAt (*canvas, overlap, overlap, {}));
    juce::PopupMenu::dismissAllActiveMenus();
    pump (30);

    // Drawn at once (off screen, an open menu closes itself at its first timer tick), with the
    // inspector already showing the selection.
    rightClick();
    save ("layers-menu", false);

    if (auto* contentComponent = findComponentWithId (editor, "content"))
    {
        juce::Component* menuWindow = nullptr;

        for (auto* child : contentComponent->getChildren())
            if (child->getName() == "menu" && child->isVisible())
                menuWindow = child;

        const auto click = contentComponent->getLocalPoint (canvas, overlap);
        check (menuWindow != nullptr && std::abs (static_cast<float> (menuWindow->getX()) - click.x) < 8.0f
                                     && std::abs (static_cast<float> (menuWindow->getY()) - click.y) < 8.0f,
               "the menu opens at the mouse");
    }

    canvas->mouseUp (mouseEventAt (*canvas, overlap, overlap, {}));
    check (juce::PopupMenu::dismissAllActiveMenus(), "... and opens its menu");
    pump (30);

    ui::EditorState scratch;
    ui::actions::performObjectMenuItem (processor, scratch, sunset.id, ui::actions::menuItems::sendToBack);
    check (processor.getDocument().objects.front().id == sunset.id, "Send to back");
    processor.undo();
    ui::actions::performObjectMenuItem (processor, scratch, sunset.id, ui::actions::menuItems::hide);
    check (! visible (sunset.id), "Hide");
    processor.undo();

    // ---- deleting, adding --------------------------------------------------------------------------
    editor.selectObject (juce::Uuid::null());
    pump (20);
    list->mouseMove (mouseEventAt (*list, row (2), row (2), {}));
    clickOn (*list, list->getDeleteBounds (2).toFloat().getCentre());
    check (processor.getDocument().find (bass.id) == nullptr && processor.getDocument().objects.size() == 4,
           "a row's delete button removes its object");
    processor.undo();
    check (processor.getDocument().objects[2].id == bass.id, "... and undo puts it back in its place");
    pump (20);

    if (auto* addText = find<juce::Button> (editor, "inspector-add-text"))
    {
        addText->onClick();
        pump (20);
        check (processor.getDocument().objects.size() == 6 && selected() == frontId() && shown ("inspector-text"),
               "a new object goes in front, selected, its settings shown");
        processor.undo();
        pump (20);
    }

    // Back to the drawing as it was: WORLD shown again, then the five objects taken away.
    processor.undo();
    check (visible (world.id), "(WORLD shown again)");
    processor.undo();
    editor.selectObject (juce::Uuid::null());
    pump (30);
    check (sameDrawing (processor.getDocument(), start), "after the layer checks the drawing is back where it began");
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

/** Lows (Smooth | Stripes) at the foot of the layer list: where it sits (with the keys and with
    the brush settings), what it does to the plan and the canvas, and that it makes way for an
    object's settings. */
void checkLows (freqart::FreqArtEditor& editor, freqart::FreqArtProcessor& processor, juce::Component& content,
                const juce::Uuid& imageId, const std::function<void (const juce::String&, bool)>& save)
{
    using namespace freqart;

    auto* lows = find<ui::SegmentedControl> (editor, "lows");
    auto* canvas = find<ui::CanvasView> (editor, "canvas");
    auto* inspector = lows != nullptr ? lows->getParentComponent() : nullptr;

    if (lows == nullptr || canvas == nullptr || inspector == nullptr)
        return;

    const auto value = [&] { return processor.getParameters().getRawParameterValue (params::id::lowEnd)->load(); };
    const auto planHasComb = [&]
    {
        processor.rebuildPlanIfNeeded();
        const auto plan = processor.getCurrentPlan();
        return plan != nullptr && plan->combLines > 0;
    };
    const auto at = [lows] (float across) { return juce::Point<float> (static_cast<float> (lows->getWidth()) * across,
                                                                       static_cast<float> (lows->getHeight()) * 0.5f); };

    // At the foot of the panel, inside its margins, below everything else shown there.
    const auto laidOut = [&]
    {
        auto ok = lows->isVisible() && lows->getBottom() == inspector->getHeight() - 16 && lows->getRight() == inspector->getWidth() - 16
               && lows->getWidth() >= 150 && lows->getHeight() == 28;

        for (auto* child : inspector->getChildren())
            if (child != lows && child->isVisible())
                ok = ok && child->getBottom() <= lows->getY() - 12;

        return ok;
    };

    editor.selectObject (juce::Uuid::null());
    pump (40);

    check (lows->getOptions() == juce::StringArray { "Smooth", "Stripes" } && lows->getSelectedIndex() == 0
           && juce::exactlyEqual (value(), 0.0f) && planHasComb(),
           "Lows, under the layer list, is on Smooth in a new instance: the plan has the rising comb");

    if (auto* tip = dynamic_cast<juce::TooltipClient*> (lows))
        check (tip->getTooltip().contains ("Smooth") && tip->getTooltip().contains ("Stripes"), "Lows explains both in its tooltip");
    else
        check (false, "Lows has a tooltip");

    check (laidOut(), "Lows sits at the foot of the layer list, clear of the list and its hint");

    content.keyPressed (juce::KeyPress ('b', juce::ModifierKeys(), 'b'));
    pump (30);
    check (laidOut() && findComponentWithId (*inspector, "inspector-strength") != nullptr
               && findComponentWithId (*inspector, "inspector-strength")->isVisible(),
           "... and clear of the brush settings");
    save ("lows-brush", true);
    content.keyPressed (juce::KeyPress ('v', juce::ModifierKeys(), 'v'));
    pump (30);

    // Stripes: lines, which the canvas shows as scan lines in the lows.
    const auto smoothCanvas = canvas->createComponentSnapshot (canvas->getLocalBounds());
    clickOn (*lows, at (0.75f));
    pump (60);
    check (juce::exactlyEqual (value(), 1.0f) && lows->getSelectedIndex() == 1 && ! planHasComb(),
           "clicking Stripes plays the lows as separate lines");
    pump (60);
    const auto stripesCanvas = canvas->createComponentSnapshot (canvas->getLocalBounds());
    check (countDifferentPixels (smoothCanvas, stripesCanvas) > 2000, "... and the canvas shows them as stripes");
    save ("lows-stripes", true);

    // An object's settings take the panel; Lows comes back with the list.
    editor.selectObject (imageId);
    pump (30);
    check (! lows->isVisible(), "selecting an object shows its settings in Lows' place");
    editor.selectObject (juce::Uuid::null());
    pump (30);
    check (laidOut() && lows->getSelectedIndex() == 1, "... and Lows is back, as it was, with the list");

    // The parameter, changed elsewhere (a host, a loaded state), moves the control too.
    setParameter (processor, params::id::lowEnd, 0.0f);
    pump (60);
    check (lows->getSelectedIndex() == 0 && planHasComb(), "Lows follows the parameter back to Smooth");
    pump (60);
    check (countDifferentPixels (smoothCanvas, canvas->createComponentSnapshot (canvas->getLocalBounds())) == 0,
           "... and the canvas shows the filled band again");
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

//==============================================================================
// FreqArt FX

/** A host transport for the effect's offline runs. */
struct SnapshotPlayHead final : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

/** A stand-in for a song, stereo at 48 kHz: pink noise, a kick on every beat at 120 BPM, a hat on
    the off-beats and held chords. */
std::vector<std::vector<float>> makeSong (double seconds)
{
    const auto total = static_cast<int> (seconds * 48000.0);
    std::vector<std::vector<float>> song (2, std::vector<float> (static_cast<size_t> (total), 0.0f));
    juce::Random random (11);
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    constexpr double chords[4][3] { { 220.0, 261.6, 329.6 }, { 174.6, 220.0, 261.6 }, { 196.0, 246.9, 293.7 }, { 164.8, 207.7, 246.9 } };

    for (int i = 0; i < total; ++i)
    {
        const auto t = i / 48000.0;
        const auto white = random.nextFloat() * 2.0f - 1.0f;

        // Pink noise (Paul Kellet's economy filter).
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        auto x = 0.012f * (b0 + b1 + b2 + white * 0.1848f);

        const auto beat = t * 2.0;
        const auto sinceBeat = (beat - std::floor (beat)) / 2.0;
        const auto sinceOffBeat = (beat + 0.5 - std::floor (beat + 0.5)) / 2.0;
        const auto kickPhase = juce::MathConstants<double>::twoPi * (45.0 * sinceBeat + 75.0 * (1.0 - std::exp (-sinceBeat * 30.0)) / 30.0);
        x += static_cast<float> (0.2 * std::exp (-sinceBeat * 9.0) * std::sin (kickPhase));
        x += 0.06f * white * static_cast<float> (std::exp (-sinceOffBeat * 60.0));

        for (const auto hz : chords[static_cast<int> (t / 2.0) % 4])
            x += static_cast<float> (0.01 * std::sin (juce::MathConstants<double>::twoPi * hz * t));

        song[0][static_cast<size_t> (i)] = x;
        song[1][static_cast<size_t> (i)] = x;
    }

    return song;
}

/** Plays `song` through an effect processor from the song's start, the DAW playing at 120 BPM;
    returns its output as one channel. */
std::vector<float> carveSong (freqart::FreqArtProcessor& effect, const std::vector<std::vector<float>>& song)
{
    SnapshotPlayHead head;
    effect.setPlayHead (&head);

    constexpr int blockSize = 512;
    const auto total = static_cast<int> (song[0].size());
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> out;
    out.reserve (song[0].size());

    for (int start = 0; start < total; start += blockSize)
    {
        const auto length = juce::jmin (blockSize, total - start);
        buffer.setSize (2, length, false, false, true);

        for (int channel = 0; channel < 2; ++channel)
            buffer.copyFrom (channel, 0, song[static_cast<size_t> (channel)].data() + start, length);

        const auto seconds = start / 48000.0;
        head.info = {};
        head.info.setIsPlaying (true);
        head.info.setBpm (120.0);
        head.info.setPpqPosition (seconds * 2.0);
        head.info.setTimeInSeconds (seconds);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });

        effect.processBlock (buffer, midi);

        for (int i = 0; i < length; ++i)
            out.push_back (0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)));
    }

    effect.setPlayHead (nullptr);
    return out;
}

/** FreqArt FX's editor: the header's Mode and Depth, what they show and grey, the canvas in each
    mode, the export's hint, the live view of a carved song, the help, the window sizes. */
void checkEffect (const juce::File& outputDir, float pixelScale)
{
    using namespace freqart;

    FreqArtProcessor processor (FreqArtProcessor::Variant::effect);
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    check (processor.getName() == "FreqArt FX" && processor.isEffect(), "FreqArt FX is the effect");
    check (processor.getLatencySamples() == 4096, "FreqArt FX reports its latency");

    const std::unique_ptr<FreqArtEditor> editor (dynamic_cast<FreqArtEditor*> (processor.createEditor()));

    if (editor == nullptr)
    {
        check (false, "FreqArt FX makes a FreqArtEditor");
        return;
    }

    const auto save = [&] (const juce::String& name, float scale, bool settle = true)
    {
        processor.rebuildPlanIfNeeded();

        if (settle)
            pump (60);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = outputDir.getChildFile ("freqart-fx-" + name + ".png");

        if (writePng (image, file))
            std::cout << file.getFullPathName() << std::endl;
        else
            check (false, "write " + file.getFullPathName());
    };

    pump (150);

    auto* mode = find<ui::SegmentedControl> (*editor, "mode");
    auto* depth = find<ui::shell::NumberField> (*editor, "depth");
    auto* header = findComponentWithId (*editor, "header");
    auto* view = findComponentWithId (*editor, "view-switch");
    auto* controls = findComponentWithId (*editor, "effect-controls");
    auto* undo = findComponentWithId (*editor, "undo");
    auto* exporter = find<ui::shell::ExportPanel> (*editor, "export");
    auto* bounce = find<juce::Button> (*editor, "bounce");

    if (mode == nullptr || depth == nullptr || header == nullptr || view == nullptr || controls == nullptr || undo == nullptr
        || exporter == nullptr || bounce == nullptr)
        return;

    // The header: Canvas | Live beside the wordmark, Mode and Depth after it, clear of undo.
    check (controls->getParentComponent() == header && controls->isVisible(), "Mode and Depth are in the header");
    check (view->getRight() < controls->getX() && controls->getRight() < undo->getX()
               && mode->getRight() <= controls->getWidth() && depth->getRight() <= controls->getWidth(),
           "the header's controls do not overlap");
    check (mode->getSelectedIndex() == 0 && processor.getEffectMode() == EffectMode::carve, "a new FreqArt FX carves");
    check (depth->isEnabled(), "Depth can be changed in Carve");

    for (auto* id : { "density", "contrast", "level" })
        if (auto* knob = findComponentWithId (*editor, id))
            check (! knob->isEnabled(), juce::String (id) + " is greyed in Carve (it shapes the sines only)");

    check (exporter->isAskingForDawBounce() && ! bounce->isVisible(), "Carve: the export asks for a bounce in the DAW");
    save ("default", pixelScale);

    juce::Uuid textId, imageId;
    makeRichDrawing (processor, textId, imageId);
    pump (100);
    save ("carve", pixelScale);

    // Mode, clicked: the segments sit at about these fractions of its width.
    const auto clickMode = [&] (float fraction)
    {
        clickOn (*mode, { mode->getWidth() * fraction, mode->getHeight() * 0.5f });
        pump (60);
    };

    clickMode (0.39f);
    check (processor.getEffectMode() == EffectMode::carveDraw && mode->getSelectedIndex() == 1, "clicking Carve + Draw chooses it");
    check (depth->isEnabled() && ! exporter->isAskingForDawBounce() && bounce->isVisible(), "Carve + Draw: Depth on, Bounce WAV back");

    for (auto* id : { "density", "contrast", "level" })
        if (auto* knob = findComponentWithId (*editor, id))
            check (knob->isEnabled(), juce::String (id) + " can be changed in Carve + Draw");

    save ("carve-draw", pixelScale);

    clickMode (0.69f);
    check (processor.getEffectMode() == EffectMode::stencil, "clicking Stencil chooses it");
    check (exporter->isAskingForDawBounce(), "Stencil: the export asks for a bounce in the DAW");
    save ("stencil", pixelScale);

    clickMode (0.9f);
    check (processor.getEffectMode() == EffectMode::draw, "clicking Draw chooses it");
    check (! depth->isEnabled(), "Depth is greyed in Draw, which carves nothing");
    check (bounce->isVisible() && bounce->isEnabled(), "Draw: Bounce WAV bounces the sines");
    save ("draw", pixelScale);

    // The canvas shows each mode differently (and not as the instrument does).
    clickMode (0.1f);
    check (processor.getEffectMode() == EffectMode::carve, "clicking Carve chooses it");
    auto* canvas = findComponentWithId (*editor, "canvas");

    if (canvas != nullptr)
    {
        const auto carved = canvas->createComponentSnapshot (canvas->getLocalBounds());
        setParameter (processor, params::id::mode, 2.0f);
        pump (80);
        const auto stencilled = canvas->createComponentSnapshot (canvas->getLocalBounds());
        check (countDifferentPixels (carved, stencilled) > 20000, "the canvas shows Carve and Stencil differently");
        setParameter (processor, params::id::mode, 0.0f);
        pump (80);
    }

    // Depth: dragged up it carves less, typed it takes the value, Esc leaves it.
    const auto before = processor.getDepthDb();
    dragOn (*depth, { 30.0f, 14.0f }, { 30.0f, -10.0f });
    check (processor.getDepthDb() > before + 5.0f, "dragging Depth up carves less deeply");

    depth->showEditor();
    pump (30);

    if (auto* field = dynamic_cast<juce::TextEditor*> (depth->getChildComponent (0)))
    {
        field->setText ("-24", juce::dontSendNotification);
        field->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    }

    pump (60);
    check (std::abs (processor.getDepthDb() + 24.0f) < 0.05f, "a typed Depth is taken: " + juce::String (processor.getDepthDb()));
    save ("shallow", pixelScale);
    setParameter (processor, params::id::depth, params::defaultDepthDb);
    pump (60);

    // The Mode control explains the modes.
    if (auto* tips = find<juce::TooltipWindow> (*editor, "tooltips"))
    {
        if (auto* client = dynamic_cast<juce::TooltipClient*> (mode))
        {
            check (client->getTooltip().contains ("Stencil"), "the Mode control's tooltip explains the modes");
            tips->displayTip (mode->localPointToGlobal (juce::Point<int> (mode->getWidth() / 2, mode->getHeight())), client->getTooltip());
            save ("tooltip", pixelScale);
            tips->hideTip();
        }
    }

    // Preview runs the pass on the audio coming in, with the DAW stopped.
    {
        processor.setPreview (true);
        const auto song = makeSong (1.0);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        auto changed = 0.0f;

        for (int start = 0; start + 512 <= 48000; start += 512)
        {
            for (int channel = 0; channel < 2; ++channel)
                buffer.copyFrom (channel, 0, song[static_cast<size_t> (channel)].data() + start, 512);

            processor.processBlock (buffer, midi);

            if (start >= 4096 + 8192)
                for (int i = 0; i < 512; ++i)
                    changed = std::max (changed, std::abs (buffer.getSample (0, i) - song[0][static_cast<size_t> (start + i - 4096)]));
        }

        check (processor.isSounding() && changed > 0.01f, "Preview carves the audio coming in");
        processor.setPreview (false);

        // The host carries on: the pass stops, and with it the playhead.
        for (int i = 0; i < 8; ++i)
            processor.processBlock (buffer, midi);

        check (! processor.isSounding(), "Preview off stops the pass");
        pump (60);
    }

    // The live view: a stand-in song through a second FreqArt FX with this one's state.
    editor->showLiveView (true);

    if (auto* live = find<ui::LiveView> (*editor, "live-view"))
    {
        FreqArtProcessor second (FreqArtProcessor::Variant::effect);
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        second.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        second.setPlayConfigDetails (2, 2, 48000.0, 512);
        second.prepareToPlay (48000.0, 512);
        second.rebuildPlanIfNeeded();

        const auto seconds = live->getVisibleSeconds();
        const auto song = makeSong (seconds);
        const auto carved = carveSong (second, song);
        check (second.getDocument().objects.size() == processor.getDocument().objects.size(), "the state carries the drawing");

        // The pass (the first 8 s) is carved: the output differs from the song played late.
        auto changed = 0.0f;

        for (size_t i = 4096 + 48000; i < 4096 + 6 * 48000 && i < carved.size(); ++i)
            changed = std::max (changed, std::abs (carved[i] - song[0][i - 4096]));

        check (changed > 0.05f, "the processor carves the song");
        live->fillFromAudioForSnapshot (carved, 48000.0);
    }

    save ("live", pixelScale);
    editor->showLiveView (false);

    editor->showHelp (true);
    save ("help", pixelScale);
    editor->showHelp (false);

    // Window sizes: the header holds together from 75 to 200 %.
    for (const auto& [scale, name] : { std::pair<float, const char*> { 1.0f, "100" }, { 2.0f, "200" }, { 0.75f, "75" } })
    {
        setWindowScale (*editor, scale);
        pump (60);
        check (view->getRight() < controls->getX() && controls->getRight() < undo->getX(),
               "the header's controls do not overlap at " + juce::String (name) + " %");
        save (name, 1.0f);
    }

    processor.releaseResources();
}

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
        checkFooter (*editor, processor, [&] (const juce::String& name, bool settle) { save (name, pixelScale, settle); });
        checkLayers (*editor, processor, textId, imageId,
                     [&] (const juce::String& name, bool settle) { save (name, pixelScale, settle); });
        checkLows (*editor, processor, *content, imageId,
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
        save ("100", 1.0f);

        setWindowScale (*editor, 2.0f);
        pump (60);
        save ("200", 1.0f);

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

    // ---- FreqArt FX: the same editor on the effect ----------------------------------------------
    checkEffect (outputDir, pixelScale);
    pump (50);

    if (failed)
        std::cerr << "Some checks failed" << std::endl;
    else
        std::cout << checksRun << " checks passed" << std::endl;

    return failed ? 1 : 0;
}
