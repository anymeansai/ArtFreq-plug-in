// Renders the editor to PNG files, without a host, and checks the shell along the way.
//
//   ArtFreqSnapshot <output-dir> [pixel-scale]
//
// Writes
//
//   artfreq-default.png          a fresh instance (the default drawing)
//   artfreq-rich.png             text "HELLO" in the high band, a picture in the low / mid band,
//                                brush strokes, density 384
//   artfreq-playing.png          the same while previewing (playhead, Preview on)
//   artfreq-text-selected.png    the text object selected
//   artfreq-image-selected.png   the picture selected
//   artfreq-brush.png            the Brush tool active (chosen with the B key)
//   artfreq-live.png             the live view, filled from the plan
//   artfreq-settings.png         the settings panel open
//   artfreq-settings-reset.png   the same, asking to confirm "Reset drawing"
//   artfreq-help.png             the help panel open
//   artfreq-export.png           after "Bounce WAV": the chip (the file's name) and "Save..."
//   artfreq-export-updating.png  just after a change: the chip says "Updating..." until the new file
//   artfreq-menu.png             the Length menu open
//   artfreq-tooltip.png          a tooltip (Density's)
//   artfreq-edit.png             typing a value into the Level knob
//   artfreq-dragging.png         the picture being dragged (its frequency range shown)
//   artfreq-painting.png         a brush stroke just painted, the brush cursor over it
//   artfreq-empty.png            after Clear: the empty canvas and its hint
//   artfreq-layers.png           the layer list: five objects, one hidden, one selected from the
//                                list, the mouse over another row
//   artfreq-layers-hidden.png    the hidden object selected from the list (its outline on the canvas)
//   artfreq-layers-drag.png      a row being dragged to another place in the list
//   artfreq-layers-menu.png      an object's right-click menu on the canvas
//   artfreq-lows-brush.png       Lows (Smooth) at the foot of the panel, under the brush settings
//   artfreq-lows-stripes.png     the rich drawing with Lows on Stripes (the layers PNGs are Smooth)
//   artfreq-linear.png           the Linear analyser scale and a 1 bar length (beat labels)
//   artfreq-repeat.png           Transport with Repeat on ("Repeats every 4 bars")
//   artfreq-on-play.png          the default: "Plays once when you press play", bars 1 to 4 on the axis
//   artfreq-start-menu.png       the start choice's menu (When you press play / From a bar of the song)
//   artfreq-footer-*.png         the footer alone, On play and At bar, at 75, 100, 150 and 200 %
//   artfreq-midi-repeat.png      MIDI note with Repeat on ("Loops while a note is held")
//   artfreq-start-bar.png        a single pass from bar 9: the start bar field, bars 9 to 12 on
//                                the canvas
//   artfreq-start-bar-edit.png   typing a start bar
//   artfreq-75.png               window at 75 %, one image pixel per logical pixel
//   artfreq-100.png              window at 100 %, one image pixel per logical pixel
//   artfreq-150.png              window at 150 %, one image pixel per logical pixel
//   artfreq-200.png              window at 200 %, one image pixel per logical pixel
//
// and for ArtFreq FX (the effect variant: a small window of its own, 720 x 440)
//
//   artfreq-fx-default.png        a fresh ArtFreq FX (Carve: the default drawing dark in the song)
//   artfreq-fx-wide-selected.png  its "ArtFreq" (4 - 96 %) selected: the canvas beside the drawer
//   artfreq-fx-new-text.png       + Text, "HELLO WORLD" typed: all of it in view while it is typed
//   artfreq-fx-carve.png          the rich drawing in Carve: dark shapes on the stand-in song
//   artfreq-fx-carve-draw.png     Carve + Draw: the sines bright in a hole a little wider than
//                                 them; the Sound strip under the top bar
//   artfreq-fx-stencil.png        Stencil: the drawing filled with the song, the rest dark
//   artfreq-fx-draw.png           Draw: the sines on top of the song; no Depth, the Sound strip
//   artfreq-fx-shallow.png        Carve at a Depth of -24 dB, typed into the field
//   artfreq-fx-tooltip.png        the Mode control's tooltip
//   artfreq-fx-text-selected.png  a text selected: its settings in the drawer, on the right
//   artfreq-fx-menu.png           its right-click menu (only what would change something)
//   artfreq-fx-image-selected.png a picture on the right selected: the drawer on the left
//   artfreq-fx-layers.png         the Layers button: the layer list in the drawer
//   artfreq-fx-brush.png          the Brush tool: its settings in a strip over the bottom bar
//   artfreq-fx-draw-brush.png     Draw, the eraser and the layer list: both strips and the drawer
//   artfreq-fx-repeat.png         Repeat on ("Repeats every 4 bars")
//   artfreq-fx-length-menu.png    the Length menu, open over the bottom bar
//   artfreq-fx-settings.png       the settings: the analyser match, the window, Trigger, Start, Tempo
//   artfreq-fx-settings-timing.png  the same with At bar 9 and a typed tempo
//   artfreq-fx-live.png           the live view: a stand-in song carved by the real processor
//   artfreq-fx-help.png           ArtFreq FX's help panel
//   artfreq-fx-empty.png          nothing drawn: the stand-in song and the canvas's hint
//   artfreq-fx-75.png, -100.png, -150.png, -200.png   window sizes (Carve + Draw, a text's
//                                 settings), one image pixel per logical pixel
//
// All but the window sizes are rendered at <pixel-scale> image pixels per logical pixel (default
// 2, as on a HiDPI display). Along the way it checks keyboard shortcuts, undo / redo, the bounce (a
// WAV file of the right length, named on the chip, which says "Updating..." after a change until
// a new file takes its place), that the window size is remembered, and drives the editor with
// synthetic mouse events (select, move, resize, nudge, paint, erase, the inspector, the toolbar,
// the footer's Trigger, Repeat, start bar and Preview, the layer list, picking overlapping
// objects, Lows), checking what each does to the drawing or the settings and that undo takes it back.
// It exits with 1 if a file cannot be written or a check fails. At the end it puts the window on
// the (virtual) desktop to check the live view's own timer: the analyser on while shown,
// scrolling, off when hidden.
//
// For ArtFreq FX it checks, in every state and at every size, that the bars, the strips and the
// canvas share the window, that nothing overlaps or is cut off and that nothing shown is greyed;
// and, driven with the mouse, Mode and Depth and what each mode shows, the Sound strip, the
// drawer (an object's settings, the layer list, the side it opens on, closing it), the brush,
// the bottom bar's line, the timing in the settings, a size saved with the old large window,
// and a carve through the processor.
//
// Last, for both plug-ins, it proves that the DAW's play key gets through: with the editor's
// window on the (virtual) desktop and the keyboard in it, it clicks its way through every place
// the focus can be (through the window's peer, as the OS does), presses the space bar through
// ComponentPeer::handleKeyUpOrDown and handleKeyPress each time, and checks that it comes back
// unused, and that it is typed where text is being typed (see checkKeys; on Linux only, where a
// virtual display gives the window the keyboard). It writes
//
//   artfreq-keys-typing.png      the text box while it is typed in (focus ring, caret, the hint)
//   artfreq-keys-add-text.png    just after Add text: the new object's text box has the focus
//   artfreq-fx-drawer-left.png   ArtFreq FX: a text on the right clicked, its settings on the left
//   artfreq-fx-typing.png        ArtFreq FX: the drawer's text box while it is typed in
//
// And, for both plug-ins (the instrument's as artfreq-*, ArtFreq FX's as artfreq-fx-*), a plug-in
// that is not activated, against a fake server (every other state here is rendered activated):
//
//   artfreq-activate.png              the activation screen over the dimmed window
//   artfreq-activate-75.png ... -200.png   the same at 75, 100, 150 and 200 %, one image pixel
//                                     per logical pixel
//   artfreq-activate-typing.png       part of a key typed in lower case, shown in groups of five
//   artfreq-activate-busy.png         the request on its way (the server does not answer)
//   artfreq-activate-error.png        a 409: the server's words, with the support address
//   artfreq-activate-success.png      activated in this window: what is unlocked, and Continue
//   artfreq-activate-deactivated.png  deactivated from the settings: locked again, and why
//   artfreq-licence.png               the settings' Licence card, activated (and -75, -200)
//   artfreq-licence-confirm.png       Deactivate... asking first
//   artfreq-licence-error.png         a deactivation that failed (no connection): still activated
//   artfreq-licence-locked.png        not activated: the card offers the activation screen
//   artfreq-licence-two-keys-confirm.png  an ArtFreq and an ArtFreq Pro key: what deactivating
//                                     the Pro key locks and what stays
//   artfreq-licence-two-keys.png      ... done: still activated (ArtFreq key), the card says so
//                                     (ArtFreq FX: artfreq-fx-activate-two-keys.png, locked)
//   artfreq-activate-unfinished.png   a deactivation that didn't finish (the project closed):
//   artfreq-licence-unfinished.png    locked, and the card offers Deactivate to finish it
//
// checking in each that the screen's card fits the window with room around it, that nothing in it
// overlaps or is cut off, that nothing shown is greyed, that the top bar shows only the name, help
// and settings while locked, and that the settings with the card fit the window.
//
// On Linux run it under a virtual display:  xvfb-run -a ArtFreqSnapshot out
//
// The editor follows a change on its timers and change messages, which a slow or busy machine
// runs much later than a fast one: a check of what it shows after a change waits for it
// (checkSoon, tests/WaitFor.h), and every pump also deals with whatever is already posted to the
// message loop, so a picture or a check after it sees the editor caught up.
//
// With ARTFREQ_SNAPSHOT_STATES=1 in the environment it also writes, beside each PNG, the
// processor's state (<name>.state) and where the canvas's drawing lies in the PNG (<name>.json),
// for ArtFreqTruthLab, which plays each state through the processor and puts what an analyser
// shows of the real output beside the canvas.

#include "LicenseTestSupport.h"
#include "WaitFor.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/FrequencyScale.h"
#include "ui/Actions.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/CanvasAxes.h"
#include "ui/EffectShell.h"
#include "ui/Inspector.h"
#include "ui/KeyFocus.h"
#include "ui/Licensing.h"
#include "ui/LayerList.h"
#include "ui/LiveView.h"
#include "ui/ShellExport.h"
#include "ui/ShellFooter.h"
#include "ui/ShellToolbar.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>

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

/** Runs the message loop for a while, then until all that was posted to it by then is dealt
    with (change messages, posted calls: on a slow machine more than fits in a few milliseconds). */
void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    testwait::pumpPosted();
}

/** check (condition), once the message loop has run until it holds (the editor's timers and
    change messages come late on a slow machine), or for testwait::timeoutMs. */
template <typename Condition>
void checkSoon (Condition&& condition, const juce::String& what)
{
    check (testwait::pumpUntil (std::forward<Condition> (condition)), what);
}

juce::Component* findComponentWithId (juce::Component& root, const juce::String& id);

/** Waits until ArtFreq FX's window shows what its processor's mode has a use for: it follows the
    mode on its 30 Hz timer, which a slow machine runs late. (The checks that follow say what is
    shown; this only waits for the window to have caught up.) */
void waitForTheMode (juce::Component& editor, artfreq::ArtFreqProcessor& processor)
{
    testwait::pumpUntil ([&]
    {
        const auto mode = processor.getEffectMode();
        auto* sound = findComponentWithId (editor, "sound-strip");
        auto* depth = findComponentWithId (editor, "depth");
        return sound != nullptr && depth != nullptr && sound->isVisible() == artfreq::playsSines (mode)
            && depth->isVisible() == artfreq::carvesAudio (mode);
    });
}

void setParameter (artfreq::ArtFreqProcessor& processor, const char* id, float plainValue)
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

void setWindowScale (artfreq::ArtFreqEditor& editor, float scale)
{
    editor.setSize (juce::roundToInt (static_cast<float> (editor.getBaseWidth()) * scale),
                    juce::roundToInt (static_cast<float> (editor.getBaseHeight()) * scale));
}

/** With the environment variable ARTFREQ_SNAPSHOT_STATES set (to anything), each PNG gets two
    files beside it: <name>.state, the processor's state as a host saves it, and <name>.json, the
    plug-in and where the canvas's drawing lies in the PNG (image pixels). ArtFreqTruthLab plays
    those states through the processor and compares what an analyser shows with the canvas. */
void saveStateBeside (const juce::File& png, artfreq::ArtFreqProcessor& processor, juce::Component& editor, float scale)
{
    if (juce::SystemStats::getEnvironmentVariable ("ARTFREQ_SNAPSHOT_STATES", {}).isEmpty())
        return;

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    png.withFileExtension ("state").replaceWithData (state.getData(), state.getSize());

    auto* object = new juce::DynamicObject();
    object->setProperty ("plugin", processor.isEffect() ? "effect" : "instrument");
    object->setProperty ("pixelScale", scale);

    if (auto* canvas = dynamic_cast<artfreq::ui::CanvasView*> (findComponentWithId (editor, "canvas")))
    {
        if (canvas->isShowing() || canvas->isVisible())
        {
            const auto area = editor.getLocalArea (canvas, canvas->getDrawingArea()) * scale;
            object->setProperty ("plotX", area.getX());
            object->setProperty ("plotY", area.getY());
            object->setProperty ("plotWidth", area.getWidth());
            object->setProperty ("plotHeight", area.getHeight());
        }
    }

    png.withFileExtension ("json").replaceWithText (juce::JSON::toString (juce::var (object)));
}

/** The canvas shows the measured preview (the real sound, as an analyser with the plug-in's
    Settings shows it) once the drawing has stayed still for a moment: the screenshots show that,
    as a user sees it, except those caught in the middle of a drag or a brush stroke, which show
    the fast preview and "Updating...". Checks that the canvas then says which it shows. */
void settleCanvas (juce::Component& editor, float scale, const juce::String& name)
{
    auto* canvas = dynamic_cast<artfreq::ui::CanvasView*> (findComponentWithId (editor, "canvas"));

    if (canvas == nullptr || ! canvas->isVisible())
        return;

    // The picture taken shows the fast preview of what is drawn now (in the plug-in it is made
    // on its own thread and comes in a frame later).
    canvas->setFastPreviewMadeWhilePainting (true);

    if (name.contains ("dragging") || name.contains ("painting"))
    {
        check (! canvas->getPreviewStatus().current && canvas->getPreviewStatus().text.startsWith ("Updating"),
               name + ": mid-drag the canvas shows the fast preview and says it is updating");
        return;
    }

    canvas->settleMeasuredPreview (scale);
    const auto status = canvas->getPreviewStatus();
    check (status.measuredShown && status.current && ! status.fading, name + ": the canvas shows the measured preview");
    const auto* processor = dynamic_cast<artfreq::ArtFreqProcessor*> (dynamic_cast<juce::AudioProcessorEditor&> (editor).getAudioProcessor());
    const auto effect = processor != nullptr && processor->isEffect();
    // ArtFreq FX carves the song that played once a pass has (else a stand-in), and says which.
    const auto usersSong = effect && processor->getSongCapture() != nullptr && processor->getSongCapture()->getSerial() != 0;
    check (status.text == (! effect ? "Preview of the real sound" : usersSong ? "Real output, on your track's last pass" : "Real output, on a stand-in song"),
           name + ": the canvas says it shows the real sound (" + status.text + ")");
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
void makeRichDrawing (artfreq::ArtFreqProcessor& processor, juce::Uuid& textId, juce::Uuid& imageId)
{
    using namespace artfreq;

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
    setParameter (processor, artfreq::params::id::density, 384.0f);
}

/** Runs the processor for a while with Preview on, as a host would, so there is a playhead. */
void playFor (artfreq::ArtFreqProcessor& processor, double seconds)
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
juce::Rectangle<float> onCanvas (artfreq::ui::CanvasView& canvas, const artfreq::ArtObject& object,
                                 const artfreq::FrequencyScale& scale)
{
    const auto a = canvas.getDrawingArea();
    const auto n = artfreq::objectRectNorm (object, scale);
    return { a.getX() + n.getX() * a.getWidth(), a.getY() + n.getY() * a.getHeight(),
             n.getWidth() * a.getWidth(), n.getHeight() * a.getHeight() };
}

/** The paint layer's value under a point of the canvas. */
int paintAt (const artfreq::ArtDocument& document, artfreq::ui::CanvasView& canvas, juce::Point<float> p)
{
    const auto a = canvas.getDrawingArea();
    const auto x = juce::jlimit (0, artfreq::ArtDocument::paintWidth - 1,
                                 static_cast<int> ((p.x - a.getX()) / a.getWidth() * artfreq::ArtDocument::paintWidth));
    const auto y = juce::jlimit (0, artfreq::ArtDocument::paintHeight - 1,
                                 static_cast<int> ((p.y - a.getY()) / a.getHeight() * artfreq::ArtDocument::paintHeight));

    if (document.paint.empty())
        return 0;

    return document.paint[static_cast<size_t> (y * artfreq::ArtDocument::paintWidth + x)];
}

bool samePlacement (const artfreq::ArtObject& a, const artfreq::ArtObject& b)
{
    return juce::exactlyEqual (a.startTime, b.startTime) && juce::exactlyEqual (a.endTime, b.endTime)
        && juce::exactlyEqual (a.lowHz, b.lowHz) && juce::exactlyEqual (a.highHz, b.highHz);
}

/** True if two documents hold the same drawing (objects in order, their settings, the paint). */
bool sameDrawing (const artfreq::ArtDocument& a, const artfreq::ArtDocument& b)
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
void checkInteractions (artfreq::ArtFreqEditor& editor, artfreq::ArtFreqProcessor& processor, juce::Component& content,
                        const juce::Uuid& textId, const juce::Uuid& imageId,
                        const std::function<void (const juce::String&, bool)>& save)
{
    using namespace artfreq;

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
    checkSoon ([&] { return shown ("inspector-text") && shown ("inspector-band"); },
               "clicking the text selects it (the inspector shows it)");

    clickOn (*canvas, { a.getRight() - 6.0f, a.getY() + 6.0f });
    pump (20);
    checkSoon ([&] { return ! shown ("inspector-band"); },
               "clicking empty canvas deselects");

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
    // (A held arrow repeats every few tens of milliseconds, and its undo step ends after a second's
    // pause: the two presses go in back to back, as the message loop between them can take longer
    // than that on a slow machine.)
    editor.selectObject (textId);
    content.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
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
        checkSoon ([&] { return object (textId).text == text0.text + " WORLD!"; },
                   "typing changes the text on the canvas");
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
        checkSoon ([&] { return processor.getDocument().objects.size() == count + 1 && shown ("inspector-text"); },
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
        checkSoon ([&] { return processor.getDocument().isEmpty() && ! clear->isEnabled(); },
                   "Clear empties the drawing and greys itself out");
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
        checkSoon ([&] { return processor.isPreviewing(); },
                   "the Preview button starts the preview");
        clickOn (*preview, preview->getLocalBounds().getCentre().toFloat());
        pump (20);
        checkSoon ([&] { return ! processor.isPreviewing(); },
                   "... and stops it");
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

/** The canvas's bar labels for the default length, as the processor's settings number them. */
juce::String canvasBarLabels (const artfreq::ArtFreqProcessor& processor)
{
    using namespace artfreq;
    juce::StringArray labels;

    for (const auto& mark : ui::axes::timeMarks (params::defaultLengthIndex, 4, 4, 600.0f, processor.getSinglePassStartBar()))
        if (mark.label.isNotEmpty())
            labels.add (mark.label);

    return labels.joinIntoString (" ");
}

/** The footer's timing controls, as a person would use them: Trigger, Repeat, where a single
    pass starts (the choice in the hint line, its menu, and the start bar field: drag, type,
    Enter, Esc) and what the hint line under them says; Preview following Repeat; the canvas
    numbering the drawing's own bars, or the song's. Puts the settings back as they were. */
void checkFooter (artfreq::ArtFreqEditor& editor, artfreq::ArtFreqProcessor& processor,
                  const std::function<void (const juce::String&, bool)>& save)
{
    using namespace artfreq;

    auto* footer = find<ui::shell::Footer> (editor, "footer");
    auto* trigger = find<ui::SegmentedControl> (editor, "trigger");
    auto* repeat = find<juce::Button> (editor, "repeat");
    auto* startChoice = find<ui::shell::StartChoice> (editor, "start-choice");
    auto* startBar = find<ui::shell::NumberField> (editor, "start-bar");
    auto* preview = find<juce::Button> (editor, "preview");
    auto* tempo = findComponentWithId (editor, "tempo-source");

    if (footer == nullptr || trigger == nullptr || repeat == nullptr || startChoice == nullptr || startBar == nullptr
        || preview == nullptr || tempo == nullptr)
        return;

    const auto value = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load(); };
    const auto settle = [&] { pump (60); footer->refresh(); };
    const auto middleOf = [] (juce::Component& c, float across) { return juce::Point<float> (static_cast<float> (c.getWidth()) * across,
                                                                                               static_cast<float> (c.getHeight()) * 0.5f); };
    settle();

    // ---- the defaults: a single pass from where the DAW starts playing ---------------------------
    const auto onPlay = [&] { return juce::roundToInt (value (params::id::startAt)) == params::onPlayStartIndex; };

    check (trigger->getOptions() == juce::StringArray { "Transport", "MIDI" }, "Trigger offers Transport and MIDI (no Loop)");
    check (! repeat->getToggleState() && onPlay() && footer->getTriggerHint() == "Plays once when you press play"
           && startChoice->isVisible() && startChoice->getWords() == "when you press play" && ! startBar->isVisible(),
           "by default the drawing plays once when the DAW starts playing, and the footer says so (no bar to set)");
    check (processor.getSinglePassStartBar() == 0 && canvasBarLabels (processor) == "1 2 3 4",
           "... and the canvas numbers the drawing's own bars, 1 to 4 (" + canvasBarLabels (processor) + ")");
    save ("on-play", true);

    // The words of the choice carry on from the hint's, on its line, and end before Tempo's hint.
    const auto inHintLine = [&] (juce::Component& c)
    {
        return c.getY() > trigger->getBottom() + 6 && c.getBottom() < footer->getHeight() - 8
            && c.getX() > trigger->getX() + 40 && c.getRight() <= repeat->getRight() + 8 && c.getRight() < tempo->getX();
    };

    check (inHintLine (*startChoice), "the start choice sits in the hint line under Trigger, after \"Plays once\" ("
                                      + startChoice->getBounds().toString() + ", Repeat ends at " + juce::String (repeat->getRight()) + ")");

    // ---- the start choice: a menu of the two, the current one ticked -------------------------------
    {
        const auto ticked = [&]
        {
            juce::StringArray items;
            const auto menu = footer->startMenu();

            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                items.add ((it.getItem().isTicked ? "+" : "-") + it.getItem().text);

            return items.joinIntoString (" | ");
        };

        check (ticked() == "+When you press play | -From a bar of the song", "the start choice offers the two, On play ticked: " + ticked());

        // Drawn at once (off screen, an open menu closes itself at its first timer tick).
        clickOn (*startChoice, middleOf (*startChoice, 0.5f));
        save ("start-menu", false);

        auto menuShown = false;

        if (auto* content = findComponentWithId (editor, "content"))
            for (auto* child : content->getChildren())
                menuShown = menuShown || (child->getName() == "menu" && child->isVisible());

        check (menuShown, "a click on the start choice opens its menu");
        juce::PopupMenu::dismissAllActiveMenus();
        pump (30);

        // What its second item does.
        footer->setStartAt (StartAt::bar);
        settle();
        check (! onPlay() && footer->getTriggerHint() == "Plays once from bar 1" && startChoice->getWords() == "from bar"
               && startBar->isVisible() && processor.getSinglePassStartBar() == 1,
               "From a bar of the song: once from bar 1, with the bar as a field");
        check (ticked() == "-When you press play | +From a bar of the song", "... and the menu ticks it: " + ticked());
        check (inHintLine (*startChoice) && inHintLine (*startBar) && startBar->getX() >= startChoice->getRight()
               && startBar->getX() <= startChoice->getRight() + 6
               && startBar->getY() == startChoice->getY() && startBar->getHeight() == startChoice->getHeight(),
               "the start bar field follows the choice's words, on the same line");
    }

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

    check (canvasBarLabels (processor) == "9 10 11 12", "the canvas numbers the song's bars: 9 to 12 ("
                                                         + canvasBarLabels (processor) + ")");

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
    check (value (params::id::repeat) > 0.5f && repeat->getToggleState() && ! startBar->isVisible() && ! startChoice->isVisible()
           && footer->getTriggerHint() == "Repeats every 4 bars" && processor.getSinglePassStartBar() == 0,
           "Repeat loops the drawing every Length (nothing to choose about its start then)");
    save ("repeat", true);

    clickOn (*trigger, middleOf (*trigger, 0.85f));
    settle();
    check (juce::approximatelyEqual (value (params::id::trigger), 1.0f) && footer->getTriggerHint() == "Loops while a note is held"
           && ! startBar->isVisible() && ! startChoice->isVisible(), "MIDI with Repeat loops while a note is held");
    save ("midi-repeat", true);

    clickOn (*repeat, middleOf (*repeat, 0.5f));
    settle();
    check (value (params::id::repeat) < 0.5f && footer->getTriggerHint() == "Each note plays it once" && ! startBar->isVisible()
           && ! startChoice->isVisible(), "MIDI without Repeat plays it once per note");

    clickOn (*trigger, middleOf (*trigger, 0.2f));
    settle();
    check (juce::approximatelyEqual (value (params::id::trigger), 0.0f) && startBar->isVisible() && startChoice->isVisible()
           && footer->getTriggerHint() == "Plays once from bar 1", "back to Transport: once from bar 1, the choice and the field back");

    // Back to On play: the bar is kept for next time, but no longer shown or used.
    setParameter (processor, params::id::startBar, 9.0f);
    footer->setStartAt (StartAt::play);
    settle();
    check (onPlay() && footer->getTriggerHint() == "Plays once when you press play" && ! startBar->isVisible()
           && processor.getSinglePassStartBar() == 0 && canvasBarLabels (processor) == "1 2 3 4",
           "When you press play: the field goes, and the canvas numbers its own bars again");
    footer->setStartAt (StartAt::bar);
    settle();
    check (footer->getTriggerHint() == "Plays once from bar 9", "... and At bar finds the bar it had");
    setParameter (processor, params::id::startBar, 1.0f);
    footer->setStartAt (StartAt::play);
    settle();

    // Automation (or the host) changing Repeat moves the switch too.
    setParameter (processor, params::id::repeat, 1.0f);
    settle();
    check (repeat->getToggleState() && ! startChoice->isVisible(), "the Repeat switch follows the parameter");
    setParameter (processor, params::id::repeat, 0.0f);
    settle();

    // ... and the host changing where the pass starts moves the choice and the field.
    setParameter (processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    settle();
    check (startChoice->getWords() == "from bar" && startBar->isVisible(), "the start choice follows the parameter");
    setParameter (processor, params::id::startAt, static_cast<float> (params::onPlayStartIndex));
    settle();
    check (startChoice->getWords() == "when you press play" && ! startBar->isVisible(), "... both ways");

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
void checkLayers (artfreq::ArtFreqEditor& editor, artfreq::ArtFreqProcessor& processor,
                  const juce::Uuid& textId, const juce::Uuid& imageId,
                  const std::function<void (const juce::String&, bool)>& save)
{
    using namespace artfreq;

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

    checkSoon ([&] { return shown ("layer-list") && list->getNumObjectRows() == 5; },
               "with nothing selected the inspector lists the layers");

    // ---- selecting from the list --------------------------------------------------------------------
    // Rows are front first: sunset, WORLD, Bass drop, logo.png, HELLO.
    clickOn (*list, row (4));
    pump (20);
    checkSoon ([&] { return selected() == textId && shown ("layer-list") && ! shown ("inspector-band"); },
               "clicking a row selects its object and keeps the list");

    list->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    list->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    check (selected() == bass.id, "Up moves the selection up the list");
    list->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    check (selected() == imageId, "Down moves it down");

    list->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    pump (20);
    checkSoon ([&] { return shown ("inspector-mode") && ! shown ("layer-list") && shown ("inspector-layers"); },
               "Return opens the selected object's settings, with a way back to the list");

    if (auto* back = find<juce::Button> (editor, "inspector-layers"))
    {
        clickOn (*back, back->getLocalBounds().getCentre().toFloat());
        pump (20);
        checkSoon ([&] { return shown ("layer-list") && selected() == imageId; },
                   "\"Layers\" goes back to the list, the selection kept");
    }

    // Selecting is no undo step: the next undo takes back the five objects.
    check (processor.undo() && processor.getDocument().objects.size() == 2, "selecting from the list is no undo step");
    processor.redo();
    pump (20);

    // ---- hiding -------------------------------------------------------------------------------------
    clickOn (*list, list->getEyeBounds (1).toFloat().getCentre());
    pump (20);
    checkSoon ([&] { return ! visible (world.id) && visible (sunset.id) && visible (textId); },
               "the eye hides an object");
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
        checkSoon ([&] { return processor.getDocument().objects.size() == 6 && selected() == frontId() && shown ("inspector-text"); },
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
    checkSoon ([&] { return sameDrawing (processor.getDocument(), start); },
               "after the layer checks the drawing is back where it began");
}

/** The colour-map level (0..255) of every pixel, by the nearest colour of the map. */
std::vector<int> colourLevels (const juce::Image& image)
{
    const auto& table = artfreq::ui::theme::spectrumTable();
    std::map<uint32_t, int> cache;
    std::vector<int> levels;
    levels.reserve (static_cast<size_t> (image.getWidth()) * static_cast<size_t> (image.getHeight()));

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto c = image.getPixelAt (x, y);
            const auto key = c.getARGB() & 0xffffffu;
            auto found = cache.find (key);

            if (found == cache.end())
            {
                auto best = 0, bestDistance = std::numeric_limits<int>::max();

                for (int i = 0; i < 256; ++i)
                {
                    const auto t = juce::Colour (table[static_cast<size_t> (i)].getUnpremultiplied());
                    const auto dr = t.getRed() - c.getRed(), dg = t.getGreen() - c.getGreen(), db = t.getBlue() - c.getBlue();

                    if (dr * dr + dg * dg + db * db < bestDistance)
                    {
                        bestDistance = dr * dr + dg * dg + db * db;
                        best = i;
                    }
                }

                found = cache.emplace (key, best).first;
            }

            levels.push_back (found->second);
        }

    return levels;
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
void checkLows (artfreq::ArtFreqEditor& editor, artfreq::ArtFreqProcessor& processor, juce::Component& content,
                const juce::Uuid& imageId, const std::function<void (const juce::String&, bool)>& save)
{
    using namespace artfreq;

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

    checkSoon ([&] { return lows->getOptions() == juce::StringArray { "Smooth", "Stripes" } && lows->getSelectedIndex() == 0
                    && juce::exactlyEqual (value(), 0.0f) && planHasComb(); },
               "Lows, under the layer list, is on Smooth in a new instance: the plan has the rising comb");

    if (auto* tip = dynamic_cast<juce::TooltipClient*> (lows))
        check (tip->getTooltip().contains ("Smooth") && tip->getTooltip().contains ("Stripes"), "Lows explains both in its tooltip");
    else
        check (false, "Lows has a tooltip");

    check (laidOut(), "Lows sits at the foot of the layer list, clear of the list and its hint");

    content.keyPressed (juce::KeyPress ('b', juce::ModifierKeys(), 'b'));
    pump (30);
    checkSoon ([&] { return laidOut() && findComponentWithId (*inspector, "inspector-strength") != nullptr
                    && findComponentWithId (*inspector, "inspector-strength")->isVisible(); },
               "... and clear of the brush settings");
    save ("lows-brush", true);
    content.keyPressed (juce::KeyPress ('v', juce::ModifierKeys(), 'v'));
    pump (30);

    // Stripes: lines, which the canvas shows as scan lines in the lows (once the measured preview
    // of the new sound is in: settled here rather than waited for).
    canvas->settleMeasuredPreview (1.0f);
    const auto smoothCanvas = canvas->createComponentSnapshot (canvas->getLocalBounds());
    clickOn (*lows, at (0.75f));
    pump (60);
    checkSoon ([&] { return juce::exactlyEqual (value(), 1.0f) && lows->getSelectedIndex() == 1 && ! planHasComb(); },
               "clicking Stripes plays the lows as separate lines");   // (the plan is rebuilt on the processor's timer)
    pump (60);
    canvas->settleMeasuredPreview (1.0f);
    const auto stripesCanvas = canvas->createComponentSnapshot (canvas->getLocalBounds());
    check (countDifferentPixels (smoothCanvas, stripesCanvas) > 2000, "... and the canvas shows them as stripes");
    save ("lows-stripes", true);

    // An object's settings take the panel; Lows comes back with the list.
    editor.selectObject (imageId);
    pump (30);
    checkSoon ([&] { return ! lows->isVisible(); },
               "selecting an object shows its settings in Lows' place");
    editor.selectObject (juce::Uuid::null());
    pump (30);
    checkSoon ([&] { return laidOut() && lows->getSelectedIndex() == 1; },
               "... and Lows is back, as it was, with the list");

    // The parameter, changed elsewhere (a host, a loaded state), moves the control too.
    setParameter (processor, params::id::lowEnd, 0.0f);
    pump (60);
    checkSoon ([&] { return lows->getSelectedIndex() == 0 && planHasComb(); }, "Lows follows the parameter back to Smooth");
    pump (60);
    canvas->settleMeasuredPreview (1.0f);
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
// ArtFreq FX

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
std::vector<float> carveSong (artfreq::ArtFreqProcessor& effect, const std::vector<std::vector<float>>& song)
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

/** A showing component that is disabled, by name (ArtFreq FX shows nothing it would have to
    grey: what does not apply is not there). */
void collectDisabled (juce::Component& component, juce::StringArray& names)
{
    if (! component.isVisible())
        return;

    if (! component.isEnabled())
    {
        names.add (component.getComponentID().isNotEmpty() ? component.getComponentID()
                 : component.getName().isNotEmpty() ? component.getName() : juce::String ("(unnamed)"));
        return;
    }

    for (auto* child : component.getChildren())
        collectDisabled (*child, names);
}

juce::String nameOf (const juce::Component& component)
{
    return component.getComponentID().isNotEmpty() ? component.getComponentID()
         : component.getName().isNotEmpty() ? component.getName() : juce::String ("(unnamed)");
}

/** The visible children of `parent` lie inside it and none overlaps another. */
void checkNoOverlap (juce::Component& parent, const juce::String& state)
{
    std::vector<juce::Component*> shown;

    for (auto* child : parent.getChildren())
        if (child->isVisible() && ! child->getBounds().isEmpty())
            shown.push_back (child);

    juce::StringArray problems;

    for (size_t i = 0; i < shown.size(); ++i)
    {
        if (! parent.getLocalBounds().contains (shown[i]->getBounds()))
            problems.add (nameOf (*shown[i]) + " leaves " + nameOf (parent));

        for (size_t j = i + 1; j < shown.size(); ++j)
            if (shown[i]->getBounds().intersects (shown[j]->getBounds()))
                problems.add (nameOf (*shown[i]) + " overlaps " + nameOf (*shown[j]));
    }

    check (problems.isEmpty(), "ArtFreq FX, " + state + ": in " + nameOf (parent) + " nothing overlaps or is cut off"
                                   + (problems.isEmpty() ? juce::String() : " (" + problems.joinIntoString ("; ") + ")"));
}

/** ArtFreq FX's window holds together in the state it is in: the bars, the strips that are
    there and the canvas tile the window without overlapping, the drawer stays over the canvas,
    inside every bar, strip and the drawer nothing overlaps, the line in the bottom bar fits, and
    nothing that is shown is disabled. */
void checkEffectLayout (artfreq::ArtFreqEditor& editor, const juce::String& state)
{
    using namespace artfreq;

    auto* content = findComponentWithId (editor, "content");
    auto* top = findComponentWithId (editor, "header");
    auto* bottom = dynamic_cast<ui::shell::EffectBottomBar*> (findComponentWithId (editor, "footer"));
    auto* sound = findComponentWithId (editor, "sound-strip");
    auto* brush = findComponentWithId (editor, "brush-strip");
    auto* controls = findComponentWithId (editor, "effect-controls");
    auto* canvas = findComponentWithId (editor, "canvas");
    auto* live = findComponentWithId (editor, "live-view");
    auto* drawer = findComponentWithId (editor, "inspector");

    if (content == nullptr || top == nullptr || bottom == nullptr || sound == nullptr || brush == nullptr || controls == nullptr
        || canvas == nullptr || live == nullptr || drawer == nullptr)
    {
        check (false, "ArtFreq FX, " + state + ": the window's parts exist");
        return;
    }

    // From top to bottom, without a gap or an overlap. (The Brush strip lies over the canvas's
    // foot: see below.)
    auto* view = canvas->isVisible() ? canvas : live;
    auto y = 0;
    juce::StringArray problems;

    for (auto* part : { top, sound, view, static_cast<juce::Component*> (bottom) })
    {
        if (! part->isVisible())
            continue;

        // (The canvas keeps a few pixels of air around it.)
        const auto air = part == view || y == view->getBottom() ? 16 : 0;

        if (part->getY() < y || part->getY() > y + air || part->getX() < 0 || part->getRight() > content->getWidth())
            problems.add (nameOf (*part) + " at " + part->getBounds().toString());

        y = part->getBottom();
    }

    check (problems.isEmpty() && bottom->getBottom() == content->getHeight() && view->getHeight() >= 200,
           "ArtFreq FX, " + state + ": the bars, the strips and the canvas share the window"
               + (problems.isEmpty() ? juce::String() : " (" + problems.joinIntoString ("; ") + ")"));

    auto* canvasView = dynamic_cast<ui::CanvasView*> (canvas);

    // The Brush strip over the canvas's time axis, from edge to edge, on the bottom bar, clear of
    // the drawing (so a tool never resizes the canvas).
    if (brush->isVisible() && canvasView != nullptr)
    {
        const auto drawing = canvasView->getDrawingArea() + canvas->getPosition().toFloat();
        check (canvas->isVisible() && brush->getBottom() == bottom->getY() && brush->getX() == 0 && brush->getRight() == content->getWidth()
                   && static_cast<float> (brush->getY()) >= drawing.getBottom() && canvas->getBottom() <= bottom->getY(),
               "ArtFreq FX, " + state + ": the Brush strip lies over the canvas's time axis, clear of the drawing");
    }

    // An object's settings in the drawer: the object and its handles are in view, none of them
    // under the drawer (a wide one moves the canvas aside).
    if (drawer->isVisible() && ! editor.isLayerListShown() && ! editor.getSelectedObject().isNull() && canvas->isVisible()
        && canvasView != nullptr)
    {
        const auto object = canvasView->getObjectBounds (editor.getSelectedObject()) + canvas->getPosition().toFloat();
        const auto drawing = canvasView->getDrawingArea() + canvas->getPosition().toFloat();
        const auto reach = ui::CanvasView::handleReach + 1.0f;
        check (! object.isEmpty() && ! object.intersects (drawer->getBounds().toFloat())
                   && object.getX() >= drawing.getX() - reach && object.getRight() <= drawing.getRight() + reach,
               "ArtFreq FX, " + state + ": the selected object and its handles are in view, clear of the drawer (object "
                   + object.toNearestInt().toString() + ", drawer " + drawer->getBounds().toString() + ")");
    }

    for (auto* part : { top, controls, sound, brush, static_cast<juce::Component*> (bottom) })
        if (part->isVisible())
            checkNoOverlap (*part, state);

    // The line that says what will happen: whole, and clear of the buttons beside it.
    const auto hint = bottom->getHintBounds();
    auto hintClear = true;

    for (auto* child : bottom->getChildren())
        hintClear = hintClear && ! (child->isVisible() && child->getBounds().intersects (hint));

    check (hintClear && bottom->getShownHint().isNotEmpty()
               && juce::GlyphArrangement::getStringWidth (fonts::ui (11.0f), bottom->getShownHint()) <= static_cast<float> (hint.getWidth()),
           "ArtFreq FX, " + state + ": \"" + bottom->getShownHint() + "\" fits in the bottom bar");

    if (drawer->isVisible())
    {
        check (drawer->getY() >= (sound->isVisible() ? sound->getBottom() : top->getBottom())
                   && drawer->getBottom() <= (brush->isVisible() ? brush->getY() : bottom->getY())
                   && drawer->getX() >= 0 && drawer->getRight() <= content->getWidth()
                   && drawer->getHeight() >= ui::Inspector::compactMinimumHeight,
               "ArtFreq FX, " + state + ": the drawer lies over the canvas, between the bars, with room for its tallest content");
        checkNoOverlap (*drawer, state);
    }

    juce::StringArray disabled;
    collectDisabled (editor, disabled);
    check (disabled.isEmpty(), "ArtFreq FX, " + state + ": nothing that is shown is greyed"
                                   + (disabled.isEmpty() ? juce::String() : " (" + disabled.joinIntoString (", ") + ")"));
}

/** ArtFreq FX's editor: a small window of its own. The top bar's Mode and Depth, what each mode
    shows (and that nothing is greyed), the canvas in each mode, the strips, the drawer (an
    object's settings, the layer list, which side it opens on), the bottom bar's line, the
    settings that hold the timing, the live view of a carved song, the help, the window sizes. */
void checkEffect (const juce::File& outputDir, float pixelScale)
{
    using namespace artfreq;

    ArtFreqProcessor processor (ArtFreqProcessor::Variant::effect);
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    check (processor.getName() == "ArtFreq FX" && processor.isEffect(), "ArtFreq FX is the effect");
    check (processor.getLatencySamples() == 4096, "ArtFreq FX reports its latency");

    const std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));

    if (editor == nullptr)
    {
        check (false, "ArtFreq FX makes an ArtFreqEditor");
        return;
    }

    const auto save = [&] (const juce::String& name, float scale, bool settle = true)
    {
        processor.rebuildPlanIfNeeded();

        if (settle)
        {
            pump (60);
            settleCanvas (*editor, scale, name);
        }

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = outputDir.getChildFile ("artfreq-fx-" + name + ".png");

        if (writePng (image, file))
            std::cout << file.getFullPathName() << std::endl;
        else
            check (false, "write " + file.getFullPathName());

        saveStateBeside (file, processor, *editor, scale);
    };

    pump (150);

    checkSoon ([&] { return editor->getWidth() == ArtFreqEditor::effectBaseWidth && editor->getHeight() == ArtFreqEditor::effectBaseHeight
                    && editor->getBaseWidth() == 720 && editor->getBaseHeight() == 440; },
               "a new ArtFreq FX opens at 720 x 440");

    auto* mode = find<ui::SegmentedControl> (*editor, "mode");
    auto* depth = find<ui::shell::NumberField> (*editor, "depth");
    auto* top = findComponentWithId (*editor, "header");
    auto* bottom = find<ui::shell::EffectBottomBar> (*editor, "footer");
    auto* sound = findComponentWithId (*editor, "sound-strip");
    auto* brush = findComponentWithId (*editor, "brush-strip");
    auto* drawer = findComponentWithId (*editor, "inspector");
    auto* canvas = find<ui::CanvasView> (*editor, "canvas");
    auto* list = find<ui::LayerList> (*editor, "layer-list");
    auto* undo = findComponentWithId (*editor, "undo");
    auto* redo = findComponentWithId (*editor, "redo");
    auto* live = find<juce::Button> (*editor, "live");
    auto* layers = find<juce::Button> (*editor, "layers");
    auto* repeat = find<juce::Button> (*editor, "repeat");
    auto* content = findComponentWithId (*editor, "content");

    if (mode == nullptr || depth == nullptr || top == nullptr || bottom == nullptr || sound == nullptr || brush == nullptr
        || drawer == nullptr || canvas == nullptr || list == nullptr || undo == nullptr || redo == nullptr || live == nullptr
        || layers == nullptr || repeat == nullptr || content == nullptr)
        return;

    // (Shown: visible, and so is everything it is in. The editor itself is on no screen here.)
    const auto shown = [&] (const juce::String& id)
    {
        auto* c = findComponentWithId (*editor, id);

        for (auto* p = c; p != nullptr && p != editor.get(); p = p->getParentComponent())
            if (! p->isVisible())
                return false;

        return c != nullptr;
    };
    const auto scale = processor.getScale();
    const auto choice = [&] (const char* id) { return juce::roundToInt (processor.getParameters().getRawParameterValue (id)->load()); };

    // ---- a fresh window: Carve, and only what Carve has a use for ---------------------------------
    check (mode->getSelectedIndex() == 0 && processor.getEffectMode() == EffectMode::carve, "a new ArtFreq FX carves");
    check (shown ("depth") && ! sound->isVisible() && ! brush->isVisible() && ! drawer->isVisible(),
           "Carve with the Select tool: Depth, and neither strip nor the drawer");
    check (! undo->isVisible() && ! redo->isVisible(), "nothing to undo or redo yet: neither button is there");
    check (bottom->getShownHint() == "Carves once when you press play", "the bottom bar says \"Carves once when you press play\"");

    // What the instrument's window has and this one leaves out.
    for (const auto* id : { "preview", "export", "bounce", "clear", "trigger", "tempo-source", "start-choice", "view-switch",
                            "toolbar", "inspector-add-text", "inspector-size" })
        check (! shown (id), juce::String ("ArtFreq FX's window has no ") + id);

    checkEffectLayout (*editor, "a new window");
    save ("default", pixelScale);

    // ---- wide objects: the canvas moves aside for the drawer, so all of them stays in view -------
    {
        const auto canvasWidth = canvas->getWidth();
        const auto defaultText = processor.getDocument().objects.front().id;
        editor->selectObject (defaultText);
        pump (60);
        checkSoon ([&] { return editor->isDrawerOpen() && canvas->getWidth() < canvasWidth - 200 && canvas->getRight() <= drawer->getX(); },
                   "the default drawing's \"ArtFreq\" (4 - 96 %) selected: the canvas lies beside the drawer, not under it");
        checkEffectLayout (*editor, "the default text selected");
        save ("wide-selected", pixelScale);

        // + Text: across the length in a free band, typed into at once. What is typed stays in view.
        if (auto* add = find<juce::Button> (*editor, "add-text"))
            add->triggerClick();

        pump (80);

        if (auto* field = find<juce::TextEditor> (*editor, "inspector-text"))
        {
            field->selectAll();
            field->insertTextAtCaret ("HELLO WORLD");
        }

        pump (80);
        const auto* added = processor.getDocument().find (editor->getSelectedObject());
        check (added != nullptr && added->text == "HELLO WORLD" && added->endTime - added->startTime > 0.8f
                   && canvas->getRight() <= drawer->getX(),
               "+ Text, typed: the new text across the length, all of it beside the drawer ("
                   + (added != nullptr ? added->text + " " + juce::String (added->startTime) + " - " + juce::String (added->endTime) : juce::String ("none"))
                   + ", canvas " + canvas->getBounds().toString() + ", drawer " + drawer->getBounds().toString() + ")");
        checkEffectLayout (*editor, "new text typed");
        save ("new-text", pixelScale);

        // Deselected: the canvas has the whole width again.
        editor->selectObject (juce::Uuid::null());
        pump (60);
        checkSoon ([&] { return ! editor->isDrawerOpen() && canvas->getWidth() == canvasWidth; },
                   "deselected, the canvas has the window's width again");
    }

    juce::Uuid textId, imageId;
    makeRichDrawing (processor, textId, imageId);
    pump (100);
    checkEffectLayout (*editor, "Carve");
    save ("carve", pixelScale);

    // ---- Mode, clicked: the segments sit at about these fractions of its width ----------------------
    const auto clickMode = [&] (float fraction)
    {
        clickOn (*mode, { static_cast<float> (mode->getWidth()) * fraction, static_cast<float> (mode->getHeight()) * 0.5f });
        pump (80);
        waitForTheMode (*editor, processor);
    };

    const auto canvasHeightInCarve = canvas->getHeight();

    clickMode (0.38f);
    check (processor.getEffectMode() == EffectMode::carveDraw && mode->getSelectedIndex() == 1, "clicking Carve + Draw chooses it");
    check (shown ("depth") && sound->isVisible() && shown ("level") && shown ("density") && shown ("contrast") && shown ("lows"),
           "Carve + Draw: Depth, and the Sound strip with Level, Density, Contrast and Lows");
    check (canvas->getHeight() == canvasHeightInCarve - sound->getHeight(), "... the canvas makes room for the strip");
    check (bottom->getShownHint() == "Draws once when you press play", "... and the bottom bar says \"Draws once when you press play\"");
    checkEffectLayout (*editor, "Carve + Draw");
    save ("carve-draw", pixelScale);

    // The Sound strip's fields are the sines' parameters.
    if (auto* level = find<ui::shell::NumberField> (*editor, "level"))
    {
        const auto before = processor.getParameters().getRawParameterValue (params::id::level)->load();
        dragOn (*level, { 30.0f, 12.0f }, { 30.0f, 32.0f });
        check (processor.getParameters().getRawParameterValue (params::id::level)->load() < before - 2.0f, "dragging Level down turns the sines down");
        setParameter (processor, params::id::level, before);
    }

    if (auto* lows = find<ui::SegmentedControl> (*editor, "lows"))
    {
        clickOn (*lows, { static_cast<float> (lows->getWidth()) * 0.75f, 12.0f });
        check (choice (params::id::lowEnd) == params::stripesLowEndIndex, "clicking Stripes chooses it");
        clickOn (*lows, { static_cast<float> (lows->getWidth()) * 0.25f, 12.0f });
        check (choice (params::id::lowEnd) == params::smoothLowEndIndex, "... and Smooth, Smooth");
    }

    clickMode (0.7f);
    check (processor.getEffectMode() == EffectMode::stencil, "clicking Stencil chooses it");
    check (shown ("depth") && ! sound->isVisible() && ! shown ("level"), "Stencil: Depth, and no Sound strip");
    check (bottom->getShownHint() == "Stencils once when you press play", "... \"Stencils once when you press play\"");
    checkEffectLayout (*editor, "Stencil");
    save ("stencil", pixelScale);

    clickMode (0.92f);
    check (processor.getEffectMode() == EffectMode::draw, "clicking Draw chooses it");
    check (! depth->isVisible() && sound->isVisible(), "Draw carves nothing: Depth is not there, the Sound strip is");
    checkEffectLayout (*editor, "Draw");
    save ("draw", pixelScale);

    // The canvas shows each mode differently (and not as the instrument does).
    clickMode (0.08f);
    check (processor.getEffectMode() == EffectMode::carve, "clicking Carve chooses it");

    {
        canvas->settleMeasuredPreview (1.0f);
        const auto carved = canvas->createComponentSnapshot (canvas->getLocalBounds());
        setParameter (processor, params::id::mode, 2.0f);
        pump (80);
        canvas->settleMeasuredPreview (1.0f);
        const auto stencilled = canvas->createComponentSnapshot (canvas->getLocalBounds());
        check (countDifferentPixels (carved, stencilled) > 20000, "the canvas shows Carve and Stencil differently");
        setParameter (processor, params::id::mode, 0.0f);
        pump (80);
    }

    // Depth: dragged up it carves less, typed it takes the value.
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
    checkSoon ([&] { return std::abs (processor.getDepthDb() + 24.0f) < 0.05f; },
               "a typed Depth is taken: " + juce::String (processor.getDepthDb()));
    save ("shallow", pixelScale);
    setParameter (processor, params::id::depth, params::defaultDepthDb);
    pump (60);

    // The Mode control explains the modes.
    if (auto* tips = find<juce::TooltipWindow> (*editor, "tooltips"))
    {
        if (auto* client = dynamic_cast<juce::TooltipClient*> (mode))
        {
            check (client->getTooltip().contains ("Stencil"), "the Mode control's tooltip explains the modes");

            // Drawn at once: the tip window's own timer puts away a tip that no mouse is on, and
            // would do so while the picture waits for the window to settle.
            tips->displayTip (mode->localPointToGlobal (juce::Point<int> (mode->getWidth() / 2, mode->getHeight())), client->getTooltip());
            check (tips->isVisible() && content->getLocalBounds().contains (tips->getBounds()),
                   "... and it opens inside the window");
            save ("tooltip", pixelScale, false);
            tips->hideTip();
        }
    }

    // Every control says what it is: a label of its own, or a tooltip.
    for (const auto* id : { "mode", "depth", "live", "help", "settings", "tool-select", "tool-brush", "tool-erase", "add-text",
                            "add-image", "layers", "length", "repeat", "level", "density", "contrast", "lows" })
    {
        auto* client = dynamic_cast<juce::TooltipClient*> (findComponentWithId (*editor, id));
        check (client != nullptr && client->getTooltip().isNotEmpty(), juce::String (id) + " has a tooltip");
    }

    // ---- the drawer: an object's settings --------------------------------------------------------------
    const auto objectAt = [&] (const juce::Uuid& id) { return onCanvas (*canvas, *processor.getDocument().find (id), scale); };

    // The text is on the left of the canvas: its settings open on the right.
    clickOn (*canvas, objectAt (textId).getCentre());
    pump (60);
    checkSoon ([&] { return editor->getSelectedObject() == textId && editor->isDrawerOpen() && ! editor->isLayerListShown() && ! editor->isDrawerOnLeft()
                    && shown ("inspector-text") && shown ("inspector-band") && shown ("inspector-close"); },
               "a click on the text opens its settings in a drawer on the right");
    check (drawer->getX() > juce::roundToInt (objectAt (textId).getRight()) + canvas->getX(), "... clear of the text itself");
    check (! layers->getToggleState(), "... the Layers button is not lit (the drawer shows an object)");
    checkEffectLayout (*editor, "a text object selected");
    save ("text-selected", pixelScale);

    // Its settings edit it, as in the instrument.
    if (auto* band = find<ui::SegmentedControl> (*editor, "inspector-band"))
    {
        const auto highLow = processor.getDocument().find (textId)->lowHz;
        clickOn (*band, { static_cast<float> (band->getWidth()) * 0.38f, 13.0f });
        pump (40);
        checkSoon ([&] { return ui::actions::bandOf (*processor.getDocument().find (textId), scale) == static_cast<int> (Band::mid)
                        && processor.getDocument().find (textId)->lowHz < highLow; },
                   "the drawer's Band moves the text to the mid band");
        processor.undo();
        pump (40);
    }

    if (auto* field = find<juce::TextEditor> (*editor, "inspector-text"))
    {
        field->setText ("HELLO YOU", true);
        pump (40);
        checkSoon ([&] { return processor.getDocument().find (textId)->text == "HELLO YOU"; },
                   "typing in the drawer's text box changes the text");
        processor.undo();
        pump (40);
        checkSoon ([&] { return processor.getDocument().find (textId)->text == "HELLO"; },
                   "... and undo takes it back");
    }

    check (undo->isVisible() || redo->isVisible(), "after an edit, undo or redo is there");

    // Its right-click menu opens inside the small window. (Drawn at once: off screen, an open
    // menu closes itself at its first timer tick.)
    {
        const auto at = objectAt (textId).getCentre();
        canvas->mouseDown (mouseEventAt (*canvas, at, at, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
        save ("menu", pixelScale, false);
        canvas->mouseUp (mouseEventAt (*canvas, at, at, {}));
        check (juce::PopupMenu::dismissAllActiveMenus() && editor->getSelectedObject() == textId,
               "a right-click on the text opens its menu, and it stays selected");
        pump (40);
    }

    // The picture is on the right: its settings would cover it there, so they open on the left.
    clickOn (*canvas, objectAt (imageId).getCentre());
    pump (60);
    checkSoon ([&] { return editor->getSelectedObject() == imageId && editor->isDrawerOpen() && editor->isDrawerOnLeft()
                    && shown ("inspector-mode") && shown ("inspector-replace"); },
               "a click on the picture (on the right of the canvas) opens its settings on the left");
    check (drawer->getRight() < juce::roundToInt (objectAt (imageId).getX()) + canvas->getX(), "... clear of the picture");
    checkEffectLayout (*editor, "a picture selected");
    save ("image-selected", pixelScale);

    // The close button puts the drawer away and keeps the selection (and its handles).
    if (auto* close = find<juce::Button> (*editor, "inspector-close"))
    {
        close->onClick();
        pump (40);
        checkSoon ([&] { return ! editor->isDrawerOpen() && editor->getSelectedObject() == imageId; },
                   "the drawer's close button closes it; the picture stays selected");
    }

    // Esc deselects; with nothing selected the drawer is gone.
    editor->selectObject (textId);
    pump (40);
    checkSoon ([&] { return editor->isDrawerOpen() && ! editor->isDrawerOnLeft(); },
               "selecting the text again opens its settings (on the right)");
    canvas->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    pump (40);
    checkSoon ([&] { return editor->getSelectedObject().isNull() && ! editor->isDrawerOpen(); },
               "Esc deselects, and the drawer goes");

    // ---- the drawer: the layer list ------------------------------------------------------------------------
    layers->onClick();
    pump (60);
    checkSoon ([&] { return editor->isDrawerOpen() && editor->isLayerListShown() && layers->getToggleState() && shown ("layer-list")
                    && list->getNumObjectRows() == 2 && ! editor->isDrawerOnLeft(); },
               "the Layers button opens the layer list in the drawer, and is lit");
    checkEffectLayout (*editor, "the layer list");

    clickOn (*list, list->getRowBounds (1).toFloat().getCentre().withX (60.0f));
    pump (40);
    checkSoon ([&] { return editor->getSelectedObject() == textId && editor->isLayerListShown(); },
               "a click on a row selects its object; the list stays");
    save ("layers", pixelScale);

    clickOn (*list, list->getEyeBounds (0).toFloat().getCentre());
    pump (40);
    checkSoon ([&] { return ! processor.getDocument().find (imageId)->visible; },
               "a row's eye hides its object");
    processor.undo();
    pump (40);

    clickOn (*list, list->getSettingsBounds (1).toFloat().getCentre());
    pump (40);
    checkSoon ([&] { return editor->isDrawerOpen() && ! editor->isLayerListShown() && shown ("inspector-band"); },
               "a row's chevron opens the object's settings");

    if (auto* back = find<juce::Button> (*editor, "inspector-layers"))
    {
        back->onClick();
        pump (40);
        checkSoon ([&] { return editor->isLayerListShown(); },
                   "\"Layers\" in the settings goes back to the list");
    }

    // An object's right-click menu greys nothing either: an item that would change nothing (the
    // front-most object brought to the front) is not in it.
    {
        const auto itemsOf = [&] (const juce::Uuid& id)
        {
            const auto menu = ui::actions::objectMenu (processor, id);   // (the iterator only refers to it)
            juce::StringArray names;

            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (! it.getItem().isSeparator)
                    names.add (it.getItem().text + (it.getItem().isEnabled ? "" : " (greyed)"));

            return names.joinIntoString (", ");
        };

        check (itemsOf (imageId) == "Send to back, Hide, Delete" && itemsOf (textId) == "Bring to front, Hide, Delete",
               "an object's menu lists only what would change something (" + itemsOf (imageId) + " / " + itemsOf (textId) + ")");
    }

    layers->onClick();
    pump (40);
    checkSoon ([&] { return ! editor->isDrawerOpen() && ! layers->getToggleState(); },
               "the Layers button closes the list again");
    editor->selectObject (juce::Uuid::null());
    pump (40);

    // ---- the brush: its settings appear with it ----------------------------------------------------------
    if (auto* brushTool = find<juce::Button> (*editor, "tool-brush"))
    {
        const auto heightBefore = canvas->getHeight();
        brushTool->onClick();
        pump (60);
        checkSoon ([&] { return brush->isVisible() && shown ("brush-size") && shown ("brush-softness") && shown ("brush-strength"); },
                   "the Brush tool brings its settings: Size, Soft, Strength, in a strip over the bottom bar");
        check (canvas->getHeight() == heightBefore && brush->getBottom() == bottom->getY()
                   && static_cast<float> (brush->getY()) >= canvas->getY() + canvas->getDrawingArea().getBottom(),
               "... over the canvas's time axis: the canvas keeps its size, the drawing stays clear");
        checkEffectLayout (*editor, "the brush");

        // It paints.
        const auto area = canvas->getDrawingArea();
        const auto from = juce::Point<float> (area.getX() + 0.55f * area.getWidth(), area.getY() + 0.12f * area.getHeight());
        const auto to = from.translated (0.3f * area.getWidth(), 0.05f * area.getHeight());
        const auto painted = paintAt (processor.getDocument(), *canvas, (from + to) * 0.5f);
        dragOn (*canvas, from, to);
        pump (60);
        checkSoon ([&] { return paintAt (processor.getDocument(), *canvas, (from + to) * 0.5f) > painted; },
                   "the brush paints on the canvas");
        save ("brush", pixelScale);

        if (auto* eraser = find<juce::Button> (*editor, "tool-erase"))
        {
            eraser->onClick();
            pump (40);
            checkSoon ([&] { return brush->isVisible(); },
                       "the Eraser keeps the strip");
            dragOn (*canvas, from, to);
            pump (60);
            checkSoon ([&] { return paintAt (processor.getDocument(), *canvas, (from + to) * 0.5f) <= painted; },
                       "the eraser takes the stroke away");
        }

        // Both strips at once (Draw, painting), with the drawer open: the least room there is.
        setParameter (processor, params::id::mode, 3.0f);
        pump (80);
        waitForTheMode (*editor, processor);
        editor->showLayers (true);
        pump (60);
        checkSoon ([&] { return sound->isVisible() && brush->isVisible() && editor->isDrawerOpen(); },
                   "Draw while painting, the layer list open: both strips and the drawer");
        checkEffectLayout (*editor, "Draw, the eraser and the layer list");
        save ("draw-brush", pixelScale);
        editor->showLayers (false);
        setParameter (processor, params::id::mode, 0.0f);
        pump (80);
        waitForTheMode (*editor, processor);

        if (auto* select = find<juce::Button> (*editor, "tool-select"))
        {
            select->onClick();
            pump (60);
            checkSoon ([&] { return ! brush->isVisible() && canvas->getHeight() == heightBefore; },
                       "back on Select the brush's settings are gone");
        }
    }

    // ---- the bottom bar: Length, Repeat and the line that says what will happen ---------------------------
    repeat->triggerClick();
    pump (80);
    checkSoon ([&] { return choice (params::id::repeat) == 1 && bottom->getShownHint() == "Repeats every 4 bars"; },
               "Repeat on: \"Repeats every 4 bars\"");
    checkEffectLayout (*editor, "Repeat on");
    save ("repeat", pixelScale);
    repeat->triggerClick();
    pump (80);
    check (choice (params::id::repeat) == 0 && bottom->getShownHint() == "Carves once when you press play", "... and off again");

    if (auto* length = find<juce::ComboBox> (*editor, "length"))
    {
        // Its menu opens upwards, inside the small window. (Drawn at once, as the object's menu.)
        length->showPopup();
        save ("length-menu", pixelScale, false);
        check (juce::PopupMenu::dismissAllActiveMenus(), "a click on Length opens its menu");
        pump (60);

        length->setSelectedId (6, juce::sendNotificationSync);   // 8 bars
        pump (60);
        checkSoon ([&] { return choice (params::id::length) == 5; },
                   "the Length box sets the length");
        length->setSelectedId (params::defaultLengthIndex + 1, juce::sendNotificationSync);
        pump (60);
    }

    // ---- Settings: the analyser, and what the instrument keeps in its footer ---------------------------------
    editor->showSettings (true);
    pump (80);

    auto* settings = findComponentWithId (*editor, "settings-panel");

    if (settings != nullptr)
    {
        check (shown ("settings-panel") && content->getLocalBounds().reduced (8).contains (settings->getBounds()),
               "the settings fit in the window with room around them");
        checkNoOverlap (*settings, "the settings");
        check (shown ("scale") && shown ("range-low") && shown ("slope") && shown ("window-size") && shown ("reset-drawing")
                   && shown ("trigger") && shown ("start-at") && shown ("tempo-source"),
               "the settings hold the analyser match, the window size, Trigger, Start and Tempo");
        check (! shown ("start-bar") && ! shown ("bpm"), "On play has no bar to set, and the DAW's tempo no field (nothing is greyed)");
        save ("settings", pixelScale);

        // Start: at a bar of the song.
        if (auto* startAt = find<ui::SegmentedControl> (*editor, "start-at"))
        {
            clickOn (*startAt, { static_cast<float> (startAt->getWidth()) * 0.75f, 14.0f });
            pump (80);
            checkSoon ([&] { return choice (params::id::startAt) == params::atBarStartIndex && shown ("start-bar"); },
                       "Start: At bar shows the bar's field");
            setParameter (processor, params::id::startBar, 9.0f);
            pump (80);
            checkSoon ([&] { return bottom->getShownHint() == "Carves once from bar 9" && canvasBarLabels (processor) == "9 10 11 12"; },
                       "... the bottom bar says \"Carves once from bar 9\" and the canvas numbers the song's bars");
        }

        // Tempo: typed in.
        if (auto* tempo = find<ui::SegmentedControl> (*editor, "tempo-source"))
        {
            clickOn (*tempo, { static_cast<float> (tempo->getWidth()) * 0.75f, 14.0f });
            pump (80);
            checkSoon ([&] { return choice (params::id::tempoSource) == 1 && shown ("bpm"); },
                       "Tempo: Manual shows the BPM field");
            checkNoOverlap (*settings, "the settings, At bar and a typed tempo");
            save ("settings-timing", pixelScale);
            clickOn (*tempo, { static_cast<float> (tempo->getWidth()) * 0.25f, 14.0f });
            pump (80);
            checkSoon ([&] { return choice (params::id::tempoSource) == 0 && ! shown ("bpm"); },
                       "... Host hides it again");
        }

        // Trigger: MIDI notes.
        if (auto* trigger = find<ui::SegmentedControl> (*editor, "trigger"))
        {
            clickOn (*trigger, { static_cast<float> (trigger->getWidth()) * 0.8f, 14.0f });
            pump (80);
            checkSoon ([&] { return choice (params::id::trigger) == 1 && ! shown ("start-at") && ! shown ("start-bar"); },
                       "Trigger: MIDI has no start to choose (a note starts it)");
            check (bottom->getShownHint() == "Each note carves it once", "... the bottom bar says \"Each note carves it once\"");
            clickOn (*trigger, { static_cast<float> (trigger->getWidth()) * 0.2f, 14.0f });
            pump (80);
        }

        setParameter (processor, params::id::startBar, 1.0f);
        setParameter (processor, params::id::startAt, static_cast<float> (params::onPlayStartIndex));
        pump (80);
        check (bottom->getShownHint() == "Carves once when you press play", "back to the defaults");

        juce::StringArray disabled;
        collectDisabled (*editor, disabled);
        check (disabled.isEmpty(), "ArtFreq FX, the settings: nothing that is shown is greyed (" + disabled.joinIntoString (", ") + ")");
    }

    editor->showSettings (false);
    pump (40);

    // ---- the live view: a stand-in song through a second ArtFreq FX with this one's state -----------------
    editor->showLiveView (true);
    pump (40);
    checkSoon ([&] { return live->getToggleState() && ! shown ("tool-select") && ! shown ("tool-brush") && shown ("add-text") && shown ("layers"); },
               "the live view: Live is lit, and the tools (which have nothing to act on) are not there");
    checkEffectLayout (*editor, "the live view");

    if (auto* liveView = find<ui::LiveView> (*editor, "live-view"))
    {
        ArtFreqProcessor second (ArtFreqProcessor::Variant::effect);
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        second.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        second.setPlayConfigDetails (2, 2, 48000.0, 512);
        second.prepareToPlay (48000.0, 512);
        second.rebuildPlanIfNeeded();

        const auto seconds = liveView->getVisibleSeconds();
        const auto song = makeSong (seconds);
        const auto carved = carveSong (second, song);
        check (second.getDocument().objects.size() == processor.getDocument().objects.size(), "the state carries the drawing");

        // The pass (the first 8 s) is carved: the output differs from the song played late.
        auto changed = 0.0f;

        for (size_t i = 4096 + 48000; i < 4096 + 6 * 48000 && i < carved.size(); ++i)
            changed = std::max (changed, std::abs (carved[i] - song[0][i - 4096]));

        check (changed > 0.05f, "the processor carves the song from where the DAW starts playing");
        liveView->fillFromAudioForSnapshot (carved, 48000.0);
    }

    save ("live", pixelScale);
    live->onClick();
    pump (40);
    checkSoon ([&] { return ! live->getToggleState() && shown ("canvas") && shown ("tool-select"); },
               "Live again goes back to the canvas and the tools");

    // ---- the help ------------------------------------------------------------------------------------------------
    editor->showHelp (true);
    pump (40);

    if (auto* help = findComponentWithId (*editor, "help-panel"))
        check (shown ("help-panel") && content->getLocalBounds().reduced (12).contains (help->getBounds()),
               "the help fits in the window with room around it");

    save ("help", pixelScale);
    editor->showHelp (false);

    // ---- an empty drawing ----------------------------------------------------------------------------------------
    {
        const auto drawing = processor.getDocument();
        processor.setDocument (ArtDocument());
        pump (80);
        checkEffectLayout (*editor, "nothing drawn");
        save ("empty", pixelScale);
        processor.setDocument (drawing);
        pump (80);
    }

    // ---- window sizes: every state holds together from 75 to 200 % ---------------------------------------------
    const auto atEveryState = [&] (const juce::String& size)
    {
        // Carve; then Carve + Draw with a text's settings; then Draw, painting, the layer list.
        checkEffectLayout (*editor, size + ", Carve");

        setParameter (processor, params::id::mode, 1.0f);
        editor->selectObject (textId);
        pump (80);
        waitForTheMode (*editor, processor);
        checkEffectLayout (*editor, size + ", Carve + Draw with a text's settings");
        save (size, 1.0f);

        setParameter (processor, params::id::mode, 3.0f);
        editor->selectObject (imageId);
        pump (80);
        waitForTheMode (*editor, processor);
        checkEffectLayout (*editor, size + ", Draw with a picture's settings");

        editor->selectObject (juce::Uuid::null());
        editor->showLayers (true);

        if (auto* brushTool = find<juce::Button> (*editor, "tool-brush"))
            brushTool->onClick();

        pump (80);
        checkEffectLayout (*editor, size + ", Draw, painting, the layer list");

        if (auto* select = find<juce::Button> (*editor, "tool-select"))
            select->onClick();

        editor->showLayers (false);
        setParameter (processor, params::id::mode, 0.0f);
        pump (80);
        waitForTheMode (*editor, processor);

        editor->showSettings (true);
        pump (40);

        if (settings != nullptr)
            check (editor->getLocalBounds().contains (editor->getLocalArea (settings, settings->getLocalBounds())),
                   "ArtFreq FX, " + size + ": the settings fit in the window");

        editor->showSettings (false);
        pump (40);
    };

    for (const auto& [windowScale, name] : { std::pair<float, const char*> { 1.0f, "100" }, { 2.0f, "200" }, { 1.5f, "150" }, { 0.75f, "75" } })
    {
        setWindowScale (*editor, windowScale);
        pump (60);
        checkSoon ([&] { return editor->getWidth() == juce::roundToInt (720.0f * windowScale) && editor->getHeight() == juce::roundToInt (440.0f * windowScale); },
                   "ArtFreq FX's window at " + juce::String (name) + " % is " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()));
        atEveryState (name);
    }

    // The size is remembered, and a window that is opened again has it.
    setWindowScale (*editor, 1.5f);
    pump (60);
    checkSoon ([&] { return processor.getEditorWidth() == 1080 && processor.getEditorHeight() == 660; },
               "the window size is remembered");

    {
        const std::unique_ptr<juce::AudioProcessorEditor> again (processor.createEditor());
        check (again != nullptr && again->getWidth() == 1080 && again->getHeight() == 660, "a reopened ArtFreq FX has the remembered size");
    }

    // A project saved by 0.4, whose ArtFreq FX had the instrument's large window: the size it
    // saved is read as that window's scale, so 100 % stays 100 % (720 x 440, not 1120 x 720).
    check (std::abs (ArtFreqEditor::scaleForSavedSize (true, 1120, 720) - 1.0f) < 1.0e-4f
               && std::abs (ArtFreqEditor::scaleForSavedSize (true, 1680, 1080) - 1.5f) < 1.0e-4f
               && std::abs (ArtFreqEditor::scaleForSavedSize (true, 840, 540) - 0.75f) < 1.0e-4f
               && std::abs (ArtFreqEditor::scaleForSavedSize (true, 2240, 1440) - 2.0f) < 1.0e-4f
               && std::abs (ArtFreqEditor::scaleForSavedSize (true, 1080, 660) - 1.5f) < 1.0e-4f
               && std::abs (ArtFreqEditor::scaleForSavedSize (true, 0, 0) - 1.0f) < 1.0e-4f
               && std::abs (ArtFreqEditor::scaleForSavedSize (false, 1680, 1080) - 1.5f) < 1.0e-4f,
           "a size saved with the old large window opens at the same scale of the small one");

    processor.setEditorSize (1120, 720);

    {
        const std::unique_ptr<juce::AudioProcessorEditor> old (processor.createEditor());
        check (old != nullptr && old->getWidth() == 720 && old->getHeight() == 440,
               "an ArtFreq FX saved at 0.4's 1120 x 720 opens at 720 x 440");
    }

    processor.releaseResources();
}

/** ArtFreq FX once a pass of the user's song has played: the canvas carves that song (here a
    bass track: a kick, a bass line and a little low-mid body, nothing high), not the stand-in, so
    the carve shows where the song has something to carve and nowhere else, and says so. */
void checkEffectOnTheUsersSong (const juce::File& outputDir, float pixelScale)
{
    using namespace artfreq;

    ArtFreqProcessor processor (ArtFreqProcessor::Variant::effect);
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    juce::Uuid textId, imageId;
    makeRichDrawing (processor, textId, imageId);
    processor.rebuildPlanIfNeeded();

    const std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));

    if (editor == nullptr)
    {
        check (false, "ArtFreq FX makes an ArtFreqEditor");
        return;
    }

    editor->setVisible (true);
    pump (150);

    auto* canvas = dynamic_cast<ui::CanvasView*> (findComponentWithId (*editor, "canvas"));

    if (canvas == nullptr)
    {
        check (false, "ArtFreq FX has a canvas");
        return;
    }

    const auto standIn = [&]
    {
        canvas->settleMeasuredPreview (pixelScale);
        return canvas->createComponentSnapshot (canvas->getLocalBounds(), true, 1.0f);
    }();

    // One pass of the bass track, as a DAW plays it into the effect (the editor has been open,
    // so the effect keeps what comes in).
    processor.enableSongCapture();

    struct PlayHead final : public juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    } head;

    processor.setPlayHead (&head);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto passSamples = static_cast<int64_t> (std::llround (processor.getLengthSeconds() * 48000.0));
    double kick = 0.0, bass = 0.0, body = 0.0;

    for (int64_t done = 0; done < passSamples + 24000; done += 512)
    {
        const auto t = static_cast<double> (done) / 48000.0;
        head.info = {};
        head.info.setIsPlaying (true);
        head.info.setBpm (120.0);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        head.info.setPpqPosition (t * 2.0);
        head.info.setTimeInSeconds (t);

        for (int i = 0; i < 512; ++i)
        {
            const auto n = done + i;
            const auto sinceBeat = std::fmod (static_cast<double> (n), 24000.0) / 48000.0;
            kick += juce::MathConstants<double>::twoPi * (50.0 + 90.0 * std::exp (-sinceBeat * 30.0)) / 48000.0;
            bass += juce::MathConstants<double>::twoPi * ((n / 48000) % 2 == 0 ? 55.0 : 41.2) / 48000.0;
            body += juce::MathConstants<double>::twoPi * 330.0 / 48000.0;
            const auto x = 0.5 * std::exp (-sinceBeat * 10.0) * std::sin (kick) + 0.2 * std::sin (bass) + 0.01 * std::sin (body);
            buffer.setSample (0, i, static_cast<float> (x));
            buffer.setSample (1, i, static_cast<float> (x));
        }

        midi.clear();
        processor.processBlock (buffer, midi);
    }

    head.info.setIsPlaying (false);
    buffer.clear();
    processor.processBlock (buffer, midi);
    processor.setPlayHead (nullptr);

    check (processor.getSongCapture() != nullptr && processor.getSongCapture()->getSerial() != 0,
           "ArtFreq FX keeps the song that played");

    pump (60);
    settleCanvas (*editor, pixelScale, "fx-carve-your-track");
    const auto ownSong = canvas->createComponentSnapshot (canvas->getLocalBounds(), true, 1.0f);
    check (countDifferentPixels (standIn, ownSong) > 20000, "the canvas carves the song that played, not the stand-in");

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, pixelScale);
    const auto file = outputDir.getChildFile ("artfreq-fx-carve-your-track.png");

    if (writePng (image, file))
        std::cout << file.getFullPathName() << std::endl;
    else
        check (false, "write " + file.getFullPathName());

    saveStateBeside (file, processor, *editor, pixelScale);
    processor.releaseResources();
}

//==============================================================================
// Licensing: the activation screen of a plug-in that is not activated, in each of its states, and
// the Licence card in the settings, for both plug-ins, through a fake server (nothing here
// reaches the real one). Everything else in this tool runs activated (see main).

/** The activation screen holds together: its card inside the screen, under the top bar, with
    room around it; inside the card nothing overlaps or leaves it; nothing shown is greyed; the
    top bar shows only the name, help and settings. */
void checkActivationLayout (artfreq::ArtFreqEditor& editor, const juce::String& state)
{
    using namespace artfreq;

    auto* screen = dynamic_cast<ui::ActivationScreen*> (findComponentWithId (editor, "activation"));
    auto* top = findComponentWithId (editor, "header");

    if (screen == nullptr || top == nullptr)
    {
        check (false, state + ": the activation screen exists");
        return;
    }

    const auto card = screen->getCardBounds();
    check (screen->isVisible() && screen->getY() == top->getBottom() && screen->getBottom() == screen->getParentHeight(),
           state + ": the activation screen covers the window under the top bar");
    check (screen->getLocalBounds().reduced (8).contains (card),
           state + ": its card fits, with room around it (card " + card.toString() + " in " + screen->getLocalBounds().toString() + ")");

    juce::StringArray problems;
    std::vector<juce::Component*> shown;

    for (auto* child : screen->getChildren())
        if (child->isVisible())
            shown.push_back (child);

    for (size_t i = 0; i < shown.size(); ++i)
    {
        if (! card.reduced (12).contains (shown[i]->getBounds()))
            problems.add (nameOf (*shown[i]) + " leaves the card");

        for (size_t j = i + 1; j < shown.size(); ++j)
            if (shown[i]->getBounds().intersects (shown[j]->getBounds()))
                problems.add (nameOf (*shown[i]) + " overlaps " + nameOf (*shown[j]));
    }

    check (problems.isEmpty(), state + ": in the card nothing overlaps or is cut off"
                                   + (problems.isEmpty() ? juce::String() : " (" + problems.joinIntoString ("; ") + ")"));

    // The message is whole: the text box shows all of its lines.
    const auto& message = screen->getMessageText();
    check (message.getMessage().isEmpty() || message.getHeightFor (message.getWidth()) <= message.getHeight(),
           state + ": the message fits its place");

    juce::StringArray disabled;
    collectDisabled (editor, disabled);
    check (disabled.isEmpty(), state + ": nothing that is shown is greyed"
                                   + (disabled.isEmpty() ? juce::String() : " (" + disabled.joinIntoString (", ") + ")"));

    // The top bar while locked: the name, help and settings (nothing to edit with). (After an
    // activation, while the screen says so, it is whole again.)
    if (screen->isShowingSuccess())
        return;

    juce::StringArray inTopBar;

    for (auto* child : top->getChildren())
        if (child->isVisible())
            inTopBar.add (nameOf (*child));

    inTopBar.sort (true);
    check (inTopBar.joinIntoString (",") == "help,settings", state + ": the top bar shows only help and settings (" + inTopBar.joinIntoString (",") + ")");
}

void checkLicensing (const juce::File& outputDir, float pixelScale)
{
    using namespace artfreq;
    using Variant = ArtFreqProcessor::Variant;

    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        const auto effect = variant == Variant::effect;
        const juce::String prefix (effect ? "artfreq-fx-" : "artfreq-");
        const juce::String who (effect ? "ArtFreq FX" : "ArtFreq");

        // Not activated: a folder of its own with nothing in it, and a fake server.
        const licensetest::ScopedLockedLicensing licensing;
        auto& server = *licensing.server;

        ArtFreqProcessor processor (variant);
        processor.setPlayConfigDetails (effect ? 2 : 0, 2, 48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        check (! processor.isLicensed(), who + " is locked with nothing activated");

        const std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));

        if (editor == nullptr)
        {
            check (false, who + " makes an editor");
            continue;
        }

        const auto save = [&] (const juce::String& name, float scale)
        {
            pump (60);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
            const auto file = outputDir.getChildFile (prefix + name + ".png");

            if (writePng (image, file))
                std::cout << file.getFullPathName() << std::endl;
            else
                check (false, "write " + file.getFullPathName());
        };

        pump (150);

        auto* screen = find<ui::ActivationScreen> (*editor, "activation");
        auto* field = find<ui::KeyTextBox> (*editor, "licence-key");
        auto* activate = find<juce::Button> (*editor, "activate");

        if (screen == nullptr || field == nullptr || activate == nullptr)
            continue;

        const auto message = [&] { return screen->getMessageText().getMessage(); };
        const auto settle = [&] (const std::function<bool()>& done) { return testwait::pumpUntil (done); };

        // ---- the screen, empty -------------------------------------------------------------------
        check (screen->isVisible(), who + ": the activation screen is shown on first run");
        checkActivationLayout (*editor, who + ", empty");
        save ("activate", pixelScale);

        for (const auto scale : { 0.75f, 1.0f, 1.5f, 2.0f })
        {
            setWindowScale (*editor, scale);
            pump (40);
            checkActivationLayout (*editor, who + " at " + juce::String (juce::roundToInt (scale * 100.0f)) + " %");
            save ("activate-" + juce::String (juce::roundToInt (scale * 100.0f)), 1.0f);
        }

        setWindowScale (*editor, 1.0f);
        pump (40);

        // ---- typing: any form, shown in groups of five ---------------------------------------------
        field->setKey ("y1f2f jvxbx wcnvb");
        check (field->getText() == "Y1F2F-JVXBX-WCNVB", who + ": the key shows in groups of five as it is typed (" + field->getText() + ")");
        save ("activate-typing", pixelScale);

        field->setKey ("Your key: y1f2f-jvxbx-wcnvb-vczkj (keep it safe)");
        check (field->getText() == "Y1F2F-JVXBX-WCNVB-VCZKJ", who + ": a key pasted among other words is found (" + field->getText() + ")");

        // ---- a typo: caught without the server ------------------------------------------------------
        field->setKey ("Y1F2F-JVXBX-WCNVB-VCZKK");
        activate->onClick();
        check (settle ([&] { return message().isNotEmpty(); }) && message().contains ("typos") && server.numSent() == 0,
               who + ": a typo is caught without asking the server (" + message() + ")");

        // ---- busy: the request on its way ------------------------------------------------------------
        field->setKey (licensetest::keyPro);
        server.hang();
        activate->onClick();
        check (server.hanging.wait (testwait::timeoutMs) && screen->isBusy() && field->isReadOnly(), who + ": busy while the request is on its way");
        checkActivationLayout (*editor, who + ", busy");
        save ("activate-busy", pixelScale);

        // Closing the window stops it (here: as the editor does when it goes).
        processor.getLicense().cancelRequest();
        check (settle ([&] { return ! screen->isBusy(); }) && ! field->isReadOnly(), who + ": a stopped request ends the busy state");

        // ---- the server says no: its words, the support address in them -----------------------------
        server.reply (licensetest::activateLimit);
        activate->onClick();
        check (settle ([&] { return message().isNotEmpty(); }) && message().contains (anymeans::license::SUPPORT_EMAIL),
               who + ": a 409 shows the server's text with the support address (" + message() + ")");
        checkActivationLayout (*editor, who + ", error");
        save ("activate-error", pixelScale);

        server.noConnection();
        activate->onClick();
        check (settle ([&] { return message().contains ("reach the server"); }), who + ": no connection says so (" + message() + ")");
        check (! processor.isLicensed() && screen->isVisible(), who + ": still locked after the failures");

        // ---- activated -------------------------------------------------------------------------------
        // The instrument tries an ArtFreq key first: the server says a Pro key is for another
        // product, and it is sent again as ArtFreq Pro's.
        if (! effect)
            server.reply (licensetest::activateProAsArtFreq);

        server.reply (licensetest::activateProFresh);
        activate->onClick();
        check (settle ([&] { return screen->isShowingSuccess(); }) && processor.isLicensed() && screen->isVisible(),
               who + ": activated, and the screen says so (" + message() + ")");
        check (message().contains ("ArtFreq and ArtFreq FX"), who + ": the success names what is unlocked");
        checkActivationLayout (*editor, who + ", success");
        save ("activate-success", pixelScale);

        activate->onClick();   // Continue
        pump (40);
        checkSoon ([&] { return ! screen->isVisible(); },
                   who + ": Continue puts the screen away");
        check (findComponentWithId (*editor, effect ? "effect-controls" : "view-switch")->isVisible(), who + ": the top bar is whole again");

        // ---- the Licence card ----------------------------------------------------------------------
        auto* card = find<ui::LicenceCard> (*editor, "licence-card");
        auto* panel = findComponentWithId (*editor, "settings-panel");

        if (card == nullptr || panel == nullptr)
            continue;

        const auto panelFits = [&] (const juce::String& state)
        {
            check (panel->getBounds().getBottom() <= editor->getBaseHeight() - 6 && panel->getY() >= 0
                       && panel->getLocalBounds().contains (card->getBounds()),
                   who + ", " + state + ": the settings with the Licence card fit the window (" + panel->getBounds().toString() + ")");
        };

        editor->showSettings (true);
        pump (40);
        checkSoon ([&] { return find<juce::Button> (*editor, "deactivate") != nullptr && findComponentWithId (*editor, "deactivate")->isVisible(); },
                   who + ": activated, the card offers Deactivate");
        panelFits ("activated");
        save ("licence", pixelScale);

        for (const auto scale : { 0.75f, 2.0f })
        {
            setWindowScale (*editor, scale);
            pump (40);
            save ("licence-" + juce::String (juce::roundToInt (scale * 100.0f)), 1.0f);
        }

        setWindowScale (*editor, 1.0f);
        pump (40);

        find<juce::Button> (*editor, "deactivate")->onClick();
        pump (30);
        checkSoon ([&] { return card->isConfirming() && findComponentWithId (*editor, "deactivate-confirm")->isVisible(); },
                   who + ": Deactivate asks first");
        panelFits ("confirming");
        save ("licence-confirm", pixelScale);

        // It fails: everything stays as it was, and the card says why.
        server.noConnection();
        find<juce::Button> (*editor, "deactivate-confirm")->onClick();
        check (settle ([&] { return card->getMessageText().getMessage().isNotEmpty(); }) && processor.isLicensed(),
               who + ": a deactivation that fails keeps the activation (" + card->getMessageText().getMessage() + ")");
        panelFits ("failed");
        save ("licence-error", pixelScale);

        // It goes through: locked again, and the activation screen says so.
        server.reply (licensetest::deactivateArtFreq);
        find<juce::Button> (*editor, "deactivate")->onClick();
        find<juce::Button> (*editor, "deactivate-confirm")->onClick();
        check (settle ([&] { return ! processor.isLicensed() && screen->isVisible(); }) && ! panel->isVisible()
                   && message().contains ("another computer"),
               who + ": deactivated, the activation screen is back with the result (" + message() + ")");
        save ("activate-deactivated", pixelScale);

        // Not activated, the card offers the activation screen, and no Reset drawing (the drawing
        // is out of reach).
        editor->showSettings (true);
        pump (40);
        checkSoon ([&] { return findComponentWithId (*editor, "licence-activate")->isVisible(); }, who + ": not activated, the card offers Activate...");
        check (! findComponentWithId (*editor, "reset-drawing")->isVisible(), who + ": locked, the settings offer no Reset drawing");
        panelFits ("not activated");
        save ("licence-locked", pixelScale);
        editor->showSettings (false);
        pump (30);

        // ---- two keys: ArtFreq, then ArtFreq Pro -------------------------------------------------------
        // Deactivating frees the Pro key: ArtFreq FX locks, ArtFreq stays activated by its own key.
        // The instrument's card says so (still activated, the settings stay); ArtFreq FX's
        // activation screen does.
        check (licensing.activateArtFreqThenPro(), who + ": two keys stored");
        processor.getLicense().reload();
        pump (40);
        editor->showSettings (true);
        pump (40);
        testwait::pumpUntil ([&] { return findComponentWithId (*editor, "deactivate")->isVisible(); });
        find<juce::Button> (*editor, "deactivate")->onClick();
        pump (30);
        checkSoon ([&] { return processor.getLicense().getDeactivationWarning().contains ("ArtFreq stays activated with your ArtFreq key"); },
                   who + ", two keys: the question says what stays (" + processor.getLicense().getDeactivationWarning() + ")");
        panelFits ("two keys, confirming");
        save ("licence-two-keys-confirm", pixelScale);

        server.reply (licensetest::deactivateArtFreq);
        find<juce::Button> (*editor, "deactivate-confirm")->onClick();

        if (effect)
        {
            check (settle ([&] { return ! processor.isLicensed() && screen->isVisible(); })
                       && message().contains ("ArtFreq stays activated with your ArtFreq key"),
                   who + ", two keys: locked, and the screen says what stays (" + message() + ")");
            save ("activate-two-keys", pixelScale);
        }
        else
        {
            check (settle ([&] { return card->getMessageText().getMessage().isNotEmpty(); }) && processor.isLicensed()
                       && panel->isVisible() && ! screen->isVisible()
                       && card->getMessageText().getMessage().contains ("ArtFreq stays activated with your ArtFreq key"),
                   who + ", two keys: still activated, and the card says what went (" + card->getMessageText().getMessage() + ")");
            panelFits ("two keys, deactivated one");
            save ("licence-two-keys", pixelScale);
        }

        editor->showSettings (false);
        check (licensing.deactivate(), who + ": every key removed");
        processor.getLicense().reload();
        pump (40);

        // ---- a deactivation that didn't finish ---------------------------------------------------------
        // (The project closed while it ran.) Locked; the screen says why, and the card offers
        // Deactivate to finish it beside Activate....
        auto cutOff = licensetest::storedActivationFrom (licensetest::activateProFresh, licensetest::keyPro);
        cutOff.deactivatingSince = "2021-01-01T00:00:00.000Z";
        check (anymeans::license::LicenseStore (licensing.folder.folder).put (cutOff), who + ": a cut-off deactivation stored");
        processor.getLicense().reload();
        pump (60);
        checkSoon ([&] { return ! processor.isLicensed() && screen->isVisible() && processor.getLicense().getInterruptedDeactivation().has_value(); },
                   who + ": a deactivation that didn't finish leaves it locked");
        checkActivationLayout (*editor, who + ", unfinished");
        save ("activate-unfinished", pixelScale);

        editor->showSettings (true);
        pump (40);
        checkSoon ([&] { return findComponentWithId (*editor, "deactivate-confirm")->isVisible() && findComponentWithId (*editor, "licence-activate")->isVisible(); },
                   who + ": the card offers Deactivate to finish it, and Activate...");
        panelFits ("unfinished deactivation");
        save ("licence-unfinished", pixelScale);
        editor->showSettings (false);
        pump (30);
    }
}

//==============================================================================
// The keyboard, the way the OS delivers it: through the window's peer.
//
// A plug-in cannot start the DAW's transport: it can only leave the DAW's play key (the space
// bar, mostly) alone. On macOS JUCE hands a key on to the host's window when nothing used it
// (juce_NSViewComponentPeer_mac.mm: keyDown -> sendEventToInputContextOrComponent, else
// [super keyDown:]). "Used" is: a text box that is being typed in took it (the peer's text input
// target gets all typing), or ComponentPeer::handleKeyUpOrDown or handleKeyPress said so (the
// focused component, its parents and their key listeners), or a component is modal (JUCE then
// swallows every key). The same calls are made here, on an editor in a window that has the
// keyboard, with the clicks sent through the peer as well, so that JUCE moves the focus as it
// does in a host.
struct KeyRig
{
    explicit KeyRig (artfreq::ArtFreqProcessor::Variant variant) : processor (variant)
    {
        using namespace artfreq;

        processor.setRateAndBufferSizeDetails (48000.0, 512);
        processor.prepareToPlay (48000.0, 512);

        ArtDocument document;
        auto text = ArtDocument::makeTextObject ("HELLO", 3500.0f, 16000.0f);
        text.startTime = 0.04f;
        text.endTime = 0.5f;
        textId = text.id;
        document.objects.push_back (text);
        processor.setDocument (document);
        processor.rebuildPlanIfNeeded();

        editor.reset (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));

        if (editor == nullptr)
            return;

        editor->addToDesktop (0);
        editor->setVisible (true);
        pump (150);
        testwait::pumpUntil ([this] { return editor->isShowing(); });

        // As when the user clicks into the plug-in's window for the first time: the display gives
        // the window the keyboard (a slow machine takes its time), and JUCE gives the editor the focus.
        editor->grabKeyboardFocus();
        pump (50);
        testwait::pumpUntil ([this] { return hasKeyboard(); });

        // The display tells the window that it has the keyboard in a message, and JUCE then gives
        // the focus to the editor. On X11 that message can lie unseen until something more
        // arrives from the display, and would then take the focus from what was clicked first.
        // So it is waited for: the focus is put on the canvas, and comes back to the editor when
        // the message is dealt with (the pointer moving and the window repainting keep the
        // display talking). If it was dealt with at once, the wait is in vain.
        if (auto* canvasView = findComponentWithId (*editor, "canvas"))
        {
            canvasView->grabKeyboardFocus();

            for (int rounds = 0; rounds < 24 && juce::Component::getCurrentlyFocusedComponent() == canvasView; ++rounds)
            {
                const auto corner = 6.0f + static_cast<float> (rounds % 2) * 6.0f;
                juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (editor->localPointToGlobal (juce::Point<float> (corner, corner)));
                editor->repaint();
                pump (15);
            }

            editor->grabKeyboardFocus();
            pump (20);
        }
    }

    ~KeyRig()
    {
        juce::PopupMenu::dismissAllActiveMenus();

        if (editor != nullptr)
            editor->removeFromDesktop();

        pump (30);
        editor.reset();
        pump (30);
        processor.releaseResources();
    }

    juce::ComponentPeer* peer() const   { return editor != nullptr ? editor->getPeer() : nullptr; }

    /** The window is on a desktop and has the keyboard. */
    bool hasKeyboard() const
    {
        return peer() != nullptr && peer()->isFocused() && juce::Component::getCurrentlyFocusedComponent() != nullptr;
    }

    juce::Component& get (const juce::String& id) const
    {
        auto* found = findComponentWithId (*editor, id);
        check (found != nullptr, "component '" + id + "' exists");
        return found != nullptr ? *found : *editor;
    }

    bool hasFocus (const juce::String& id) const
    {
        return juce::Component::getCurrentlyFocusedComponent() == findComponentWithId (*editor, id);
    }

    /** What has the focus, for the messages. */
    juce::String focusName() const
    {
        const auto* focused = juce::Component::getCurrentlyFocusedComponent();
        return focused == nullptr ? "nothing"
             : focused == editor.get() ? "the editor"
             : focused->getComponentID().isNotEmpty() ? focused->getComponentID()
             : focused->getName().isNotEmpty() ? focused->getName() : "a component without a name";
    }

    // ---- the mouse, through the window ----------------------------------------------------------
    void mouse (juce::Component& c, juce::Point<float> at, bool down)
    {
        const auto inWindow = editor->getLocalPoint (&c, at);
        const auto buttons = down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys();

        // The display's pointer goes along: components ask it where the mouse is.
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (editor->localPointToGlobal (inWindow));
        peer()->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, inWindow, buttons,
                                  juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time);
    }

    /** A click (clicks are a second apart: never a double-click by accident). */
    void click (juce::Component& c, juce::Point<float> at)
    {
        time += 1000;
        mouse (c, at, true);
        time += 30;
        mouse (c, at, false);
        pump (40);
    }

    void click (const juce::String& id)   { click (get (id), get (id).getLocalBounds().toFloat().getCentre()); }

    void doubleClick (juce::Component& c, juce::Point<float> at)
    {
        time += 1000;
        mouse (c, at, true);
        time += 30;
        mouse (c, at, false);
        time += 120;
        mouse (c, at, true);
        time += 30;
        mouse (c, at, false);
        pump (60);
    }

    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
    {
        time += 1000;
        mouse (c, from, true);

        for (int i = 1; i <= 4; ++i)
        {
            time += 10;
            mouse (c, from + (to - from) * (static_cast<float> (i) / 4.0f), true);
        }

        time += 10;
        mouse (c, to, false);
        pump (40);
    }

    // ---- the keyboard, through the window -------------------------------------------------------
    /** Whether a text box is being typed in: macOS then sends the keys there, as text, before
        anything else gets to see them. (A number typed into a field, a knob or a bar is not: its
        keys come as key presses, so the space bar can still go on; see editing().) */
    bool typing() const   { return peer()->findCurrentTextInputTarget() != nullptr; }

    /** Whether a text box has the focus: the object's text, or a number being typed. */
    bool editing() const   { return artfreq::ui::isTextBox (juce::Component::getCurrentlyFocusedComponent()); }

    /** Presses a key and lets it go. Returns whether it was used, so that the DAW would not get
        it. */
    bool uses (const juce::KeyPress& key)
    {
        const auto wasTyping = typing();
        auto used = peer()->handleKeyUpOrDown (true);
        used = peer()->handleKeyPress (key) || used;
        used = peer()->handleKeyUpOrDown (false) || used;
        used = used || wasTyping || juce::Component::getCurrentlyModalComponent() != nullptr;

        pump (30);   // (a text box acts on Return and Esc a moment later)
        return used;
    }

    bool usesSpace()   { return uses (juce::KeyPress (juce::KeyPress::spaceKey, juce::ModifierKeys(), ' ')); }

    /** Types into whatever has the focus, key by key. */
    void type (const juce::String& text)
    {
        for (const auto character : text)
        {
            peer()->handleKeyUpOrDown (true);
            peer()->handleKeyPress (juce::KeyPress (character == ' ' ? juce::KeyPress::spaceKey : static_cast<int> (character),
                                                    juce::ModifierKeys(), character));
            peer()->handleKeyUpOrDown (false);
        }

        pump (30);   // (a text box reports its changes a moment later)
    }

    juce::String text (const juce::Uuid& id) const
    {
        const auto* object = processor.getDocument().find (id);
        return object != nullptr ? object->text : juce::String ("(gone)");
    }

    artfreq::ui::CanvasView& canvas() const   { return *dynamic_cast<artfreq::ui::CanvasView*> (findComponentWithId (*editor, "canvas")); }

    /** A point on the text object, and one on empty canvas (bar 4, in the lows). */
    juce::Point<float> onText() const
    {
        const auto a = canvas().getDrawingArea();
        return { a.getX() + 0.25f * a.getWidth(), a.getY() + 0.12f * a.getHeight() };
    }

    juce::Point<float> onEmptyCanvas() const
    {
        const auto a = canvas().getDrawingArea();
        return { a.getX() + 0.85f * a.getWidth(), a.getY() + 0.8f * a.getHeight() };
    }

    artfreq::ArtFreqProcessor processor;
    std::unique_ptr<artfreq::ArtFreqEditor> editor;
    juce::Uuid textId;
    juce::int64 time = juce::Time::currentTimeMillis() + 60000;   // of the last mouse event: only ever forwards
};

bool canDriveAWindow()
{
   #if JUCE_LINUX || JUCE_BSD
    return true;
   #else
    return false;
   #endif
}

/** The DAW's play key works while the plug-in's window has the focus: in every place the focus
    can be, the space bar comes back unused; where text is being typed it is typed, and Return or
    Esc hand the keyboard back. */
void checkKeys (const juce::File& outputDir, float pixelScale)
{
    using namespace artfreq;

    const juce::String name ("ArtFreq");

    // This needs a display that gives a window the keyboard and moves the pointer when asked. A
    // virtual display on Linux does; a build machine's macOS session does not reliably (and on a
    // desk the pointer is the user's). The code that is checked is the same on both.
    if (! canDriveAWindow())
    {
        std::cout << "(" << name << "'s keys are checked on Linux only, under a virtual display)" << std::endl;
        return;
    }

    KeyRig rig (ArtFreqProcessor::Variant::instrument);

    if (rig.editor == nullptr || ! rig.hasKeyboard())
    {
        std::cout << "(no display that gives a window the keyboard: " << name << "'s keys were not checked)" << std::endl;
        return;
    }

    auto& processor = rig.processor;
    auto& editor = *rig.editor;
    const auto returnKey = juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys(), '\r');
    const auto escapeKey = juce::KeyPress (juce::KeyPress::escapeKey, juce::ModifierKeys(), 27);

    const auto saveInspector = [&] (const juce::String& fileName)
    {
        auto& inspector = rig.get ("inspector");
        const auto image = editor.createComponentSnapshot (editor.getLocalArea (&inspector, inspector.getLocalBounds()), true, pixelScale);
        const auto file = outputDir.getChildFile (fileName);

        if (writePng (image, file))
            std::cout << file.getFullPathName() << std::endl;
        else
            check (false, "write " + file.getFullPathName());
    };

    /** In this state the space bar is the DAW's: nothing used it, nothing is typing or modal. */
    const auto spaceIsTheDaws = [&] (const juce::String& state)
    {
        const auto typing = rig.typing();
        const auto modal = juce::Component::getCurrentlyModalComponent() != nullptr;
        const auto focus = rig.focusName();
        const auto used = rig.usesSpace();

        check (! used && ! typing && ! modal,
               name + ": the space bar comes back unused " + state + " (the focus is on " + focus
                   + (typing ? ", a text box is being typed in" : "") + (modal ? ", something is modal" : "") + ")");
    };

    // ---- every place the focus can be ---------------------------------------------------------------
    spaceIsTheDaws ("as the window opens");

    rig.click (rig.canvas(), rig.onEmptyCanvas());
    check (rig.hasFocus ("canvas"), name + ": a click on the canvas gives it the focus");
    spaceIsTheDaws ("after a click on the canvas");

    // The layer list: a row clicked (the list handles Up, Down, Return, Delete and Esc itself).
    if (auto* list = dynamic_cast<ui::LayerList*> (findComponentWithId (editor, "layer-list")); list != nullptr && list->isShowing())
    {
        rig.click (*list, list->getRowBounds (0).toFloat().getCentre().withX (60.0f));
        check (rig.hasFocus ("layer-list"), name + ": a click on a row of the layer list gives the list the focus");
        spaceIsTheDaws ("with the layer list focused");
    }
    else
    {
        check (false, name + ": the layer list is shown when nothing is selected");
    }

    // Buttons, chips, segmented controls and number fields that are only clicked: none keeps
    // the focus (JUCE gives a clicked button the focus, and Space or Return would press it again).
    const std::pair<const char*, const char*> clicked[] = {
        { "tool-brush", "a tool button" }, { "tool-select", "a tool button" }, { "undo", "Undo" }, { "redo", "Redo (disabled)" },
        { "view-switch", "Canvas | Live" }, { "trigger", "Trigger (a segmented control)" }, { "repeat", "Repeat (a chip)" },
        { "tempo-source", "Tempo" }, { "bpm", "BPM (following the host: not editable)" }, { "preview", "Preview" },
        { "lows", "Lows" }, { "toolbar", "the toolbar's background" }, { "header", "the header's background" },
        { "footer", "the footer's background" }, { "inspector", "the inspector's background" } };

    for (const auto& [id, what] : clicked)
    {
        auto& target = rig.get (id);
        const auto at = juce::String (id) == "toolbar" ? juce::Point<float> (650.0f, 22.0f)
                      : juce::String (id) == "header"  ? juce::Point<float> (330.0f, 20.0f)
                      : juce::String (id) == "footer"  ? juce::Point<float> (6.0f, 6.0f)
                      : juce::String (id) == "inspector" ? juce::Point<float> (140.0f, static_cast<float> (target.getHeight()) - 90.0f)
                                                         : target.getLocalBounds().toFloat().getCentre();
        rig.click (target, at);
        spaceIsTheDaws (juce::String ("after a click on ") + what);

        const auto* focused = juce::Component::getCurrentlyFocusedComponent();
        check (focused != nullptr && dynamic_cast<const juce::Button*> (focused) == nullptr,
               name + ": a click on " + what + " does not leave the focus on a button (it is on " + rig.focusName() + ")");

        // Back to how it was: the canvas, Transport without Repeat, the DAW's tempo, no preview.
        editor.showLiveView (false);
        setParameter (processor, params::id::trigger, 0.0f);
        setParameter (processor, params::id::repeat, 0.0f);
        setParameter (processor, params::id::tempoSource, 0.0f);
        processor.setPreview (false);
        pump (40);
    }

    // Menus: the keys are the menu's while it is open; closed, nothing stays modal and the box
    // that opened it has not kept the focus (the arrows and Return would be its own).
    for (const auto* id : { "length", "start-choice" })
    {
        rig.click (id);
        check (juce::Component::getCurrentlyModalComponent() != nullptr, name + ": a click on " + juce::String (id) + " opens its menu");
        juce::PopupMenu::dismissAllActiveMenus();
        pump (80);
        spaceIsTheDaws (juce::String ("after ") + id + "'s menu has closed");
        check (! rig.uses (returnKey) && ! rig.uses (juce::KeyPress (juce::KeyPress::downKey)),
               name + ": Return and Down are not kept by " + id + " after its menu");
    }

    // A knob turned, a number field dragged.
    rig.drag (rig.get ("density"), { 38.0f, 40.0f }, { 38.0f, 20.0f });
    spaceIsTheDaws ("after turning a knob");

    setParameter (processor, params::id::tempoSource, 1.0f);
    pump (60);
    rig.drag (rig.get ("bpm"), { 30.0f, 14.0f }, { 30.0f, 2.0f });
    spaceIsTheDaws ("after dragging a number field");
    setParameter (processor, params::id::tempoSource, 0.0f);
    pump (40);

    // The panels: the keys are the panel's while it is open (Esc closes it), and it has no use
    // for the space bar; closed, the canvas has the focus again.
    for (const auto* button : { "help", "settings" })
    {
        const auto panel = juce::String (button) + "-panel";
        rig.click (button);
        check (rig.get (panel).isShowing() && rig.hasFocus (panel), name + ": the " + button + " panel has the focus while it is open");
        spaceIsTheDaws (juce::String ("with the ") + button + " panel open");
        check (rig.uses (escapeKey) && ! rig.get (panel).isVisible(), name + ": Esc closes the " + button + " panel");
        check (rig.hasFocus ("canvas"), name + ": ... and the focus is the canvas's again");
        spaceIsTheDaws (juce::String ("after the ") + button + " panel has closed");
    }

    // The live view: no canvas to hold the focus.
    editor.showLiveView (true);
    pump (60);
    spaceIsTheDaws ("in the live view");
    rig.click ("repeat");
    spaceIsTheDaws ("in the live view, after a click");
    setParameter (processor, params::id::repeat, 0.0f);
    editor.showLiveView (false);
    pump (60);

    // ---- a text object: selected is not typing ------------------------------------------------------
    auto& textBox = rig.get ("inspector-text");

    rig.click (rig.canvas(), rig.onText());
    check (editor.getSelectedObject() == rig.textId && textBox.isShowing() && ! textBox.hasKeyboardFocus (true)
           && rig.hasFocus ("canvas"),
           name + ": a click on a text object selects it, and the focus stays with the canvas, not in its text box");
    spaceIsTheDaws ("with a text object selected");
    check (rig.text (rig.textId) == "HELLO", name + ": ... and nothing was typed into the text");

    // ---- typing: the one place where the space bar types ---------------------------------------------
    rig.doubleClick (rig.canvas(), rig.onText());
    check (textBox.hasKeyboardFocus (true) && rig.typing(), name + ": a double-click on the text puts the focus in its text box");
    rig.type ("PLAY IT");
    check (rig.text (rig.textId) == "PLAY IT", name + ": typed text goes into the text box, the space too (\"" + rig.text (rig.textId) + "\")");
    check (rig.usesSpace() && rig.text (rig.textId) == "PLAY IT ", name + ": the space bar types a space while the text box is typed in");

    saveInspector ("artfreq-keys-typing.png");

    check (rig.uses (returnKey) && ! rig.typing() && rig.hasFocus ("canvas"),
           name + ": Return ends the typing and hands the focus back to the canvas");
    check (rig.text (rig.textId) == "PLAY IT ", name + ": ... keeping what was typed");
    spaceIsTheDaws ("after Return in the text box");

    rig.doubleClick (rig.canvas(), rig.onText());
    rig.type ("NOW");
    check (rig.uses (escapeKey) && ! rig.typing() && rig.hasFocus ("canvas") && rig.text (rig.textId) == "NOW",
           name + ": Esc ends the typing too");
    spaceIsTheDaws ("after Esc in the text box");

    rig.doubleClick (rig.canvas(), rig.onText());
    check (textBox.hasKeyboardFocus (true), name + ": (typing again)");
    rig.click ("density");
    check (! textBox.hasKeyboardFocus (true) && ! rig.typing(), name + ": a click anywhere else ends the typing");
    spaceIsTheDaws ("after clicking away from the text box");

    // Add text: typing is what comes next, so the new text box has the focus, and shows it.
    const auto objectsBefore = processor.getDocument().objects.size();
    rig.click ("add-text");
    pump (60);
    const auto newId = editor.getSelectedObject();
    check (processor.getDocument().objects.size() == objectsBefore + 1 && textBox.isShowing() && textBox.hasKeyboardFocus (true)
           && rig.typing(), name + ": Add text puts the focus in the new object's text box");

    saveInspector ("artfreq-keys-add-text.png");

    rig.type ("NEW ONE");
    check (rig.text (newId) == "NEW ONE", name + ": ... where typing replaces its text (\"" + rig.text (newId) + "\")");
    check (rig.uses (returnKey) && rig.hasFocus ("canvas"), name + ": ... and Return hands the focus back");
    spaceIsTheDaws ("after Add text, typing and Return");

    // ---- number fields ---------------------------------------------------------------------------
    if (auto* footer = dynamic_cast<ui::shell::Footer*> (findComponentWithId (editor, "footer")))
    {
        footer->setStartAt (StartAt::bar);
        pump (80);
        spaceIsTheDaws ("with the start bar field shown, not edited");

        rig.click ("start-bar");
        check (rig.editing() && ! rig.typing(), name + ": a click on the start bar field starts typing a number into it");
        rig.type ("9");
        check (! rig.usesSpace() && juce::roundToInt (processor.getParameters().getRawParameterValue (params::id::startBar)->load()) == 9,
               name + ": ... where the space bar sets the typed bar and is the DAW's (a number has no spaces)");
        check (! rig.editing() && rig.hasFocus ("canvas"), name + ": ... and the focus is the canvas's again");

        rig.click ("start-bar");
        rig.type ("12");
        check (rig.uses (returnKey) && juce::roundToInt (processor.getParameters().getRawParameterValue (params::id::startBar)->load()) == 12,
               name + ": Return sets the typed bar");
        check (! rig.editing() && rig.hasFocus ("canvas"), name + ": ... and hands the focus back to the canvas");
        spaceIsTheDaws ("after typing a start bar");

        rig.click ("start-bar");
        rig.type ("40");
        check (rig.uses (escapeKey) && juce::roundToInt (processor.getParameters().getRawParameterValue (params::id::startBar)->load()) == 12
               && rig.hasFocus ("canvas"), name + ": Esc leaves the bar as it was and hands the focus back");
        spaceIsTheDaws ("after Esc in the start bar field");

        setParameter (processor, params::id::startBar, 1.0f);
        footer->setStartAt (StartAt::play);
        pump (60);
    }

    // A knob's value, typed (its label is modal while it is edited).
    rig.doubleClick (rig.get ("level"), { 38.0f, 72.0f });
    check (rig.editing() && juce::Component::getCurrentlyModalComponent() != nullptr, name + ": a double-click on a knob's value types into it");
    rig.type ("-12");
    check (rig.uses (returnKey) && std::abs (processor.getParameters().getRawParameterValue (params::id::level)->load() + 12.0f) < 1.0e-3f,
           name + ": Return sets the typed level");
    check (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas"),
           name + ": ... nothing stays modal, and the focus is the canvas's");
    spaceIsTheDaws ("after typing a knob's value");

    // ---- the keys the editor does use are still its own ------------------------------------------------
    rig.click (rig.canvas(), rig.onEmptyCanvas());
    check (rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')) && rig.uses (juce::KeyPress ('v', juce::ModifierKeys(), 'v')),
           name + ": B and V choose a tool");
    check (! rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)) && ! rig.uses (juce::KeyPress (juce::KeyPress::leftKey))
           && ! rig.uses (returnKey) && ! rig.uses (juce::KeyPress ('a', juce::ModifierKeys(), 'a'))
           && ! rig.uses (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys(), '\t')),
           name + ": with nothing selected, Delete, the arrows, Return, Tab and other letters go on to the DAW too");
    pump (60);

    rig.click (rig.canvas(), rig.onText());
    check (rig.uses (juce::KeyPress (juce::KeyPress::rightKey)), name + ": an arrow nudges the selection");
    check (rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)) && rig.text (rig.textId) == "(gone)", name + ": Delete removes it");
    check (rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) && rig.text (rig.textId) == "NOW",
           name + ": Cmd+Z brings it back");
    spaceIsTheDaws ("at the end");
}

/** The same for ArtFreq FX's own window: its bars, its strips, its drawer and its settings. And,
    with clicks that arrive as the OS sends them, what the drawer does around the mouse: it opens
    away from the click (so a double-click's second click still lands on the text), its close
    button keeps the selection, and a click on the selected object brings it back. */
void checkEffectKeys (const juce::File& outputDir, float pixelScale)
{
    using namespace artfreq;

    const juce::String name ("ArtFreq FX");

    if (! canDriveAWindow())
    {
        std::cout << "(" << name << "'s keys are checked on Linux only, under a virtual display)" << std::endl;
        return;
    }

    KeyRig rig (ArtFreqProcessor::Variant::effect);

    if (rig.editor == nullptr || ! rig.hasKeyboard())
    {
        std::cout << "(no display that gives a window the keyboard: " << name << "'s keys were not checked)" << std::endl;
        return;
    }

    auto& processor = rig.processor;
    auto& editor = *rig.editor;
    const auto returnKey = juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys(), '\r');
    const auto escapeKey = juce::KeyPress (juce::KeyPress::escapeKey, juce::ModifierKeys(), 27);
    const auto parameterValue = [&] (const char* id) { return processor.getParameters().getRawParameterValue (id)->load(); };

    /** In this state the space bar is the DAW's: nothing used it, nothing is typing or modal. */
    const auto spaceIsTheDaws = [&] (const juce::String& state)
    {
        const auto typing = rig.typing();
        const auto modal = juce::Component::getCurrentlyModalComponent() != nullptr;
        const auto focus = rig.focusName();
        const auto used = rig.usesSpace();

        check (! used && ! typing && ! modal,
               name + ": the space bar comes back unused " + state + " (the focus is on " + focus
                   + (typing ? ", a text box is being typed in" : "") + (modal ? ", something is modal" : "") + ")");
    };

    const auto notOnAButton = [&] (const juce::String& what)
    {
        const auto* focused = juce::Component::getCurrentlyFocusedComponent();
        check (focused != nullptr && dynamic_cast<const juce::Button*> (focused) == nullptr,
               name + ": a click on " + what + " does not leave the focus on a button (it is on " + rig.focusName() + ")");
    };

    const auto saveWindow = [&] (const juce::String& fileName)
    {
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, pixelScale);
        const auto file = outputDir.getChildFile (fileName);

        if (writePng (image, file))
            std::cout << file.getFullPathName() << std::endl;
        else
            check (false, "write " + file.getFullPathName());
    };

    // A second text, on the right of the canvas (the rig's "HELLO" is on the left).
    juce::Uuid rightId;
    processor.editDocument ([&rightId] (ArtDocument& document)
    {
        auto text = ArtDocument::makeTextObject ("RIGHT", 300.0f, 1500.0f);
        text.startTime = 0.62f;
        text.endTime = 0.96f;
        rightId = text.id;
        document.objects.push_back (text);
    }, {});
    processor.rebuildPlanIfNeeded();
    pump (80);

    const auto onRightText = [&]
    {
        const auto r = onCanvas (rig.canvas(), *processor.getDocument().find (rightId), processor.getScale());
        return juce::Point<float> (r.getX() + 0.8f * r.getWidth(), r.getCentreY());
    };

    // ---- every place the focus can be ---------------------------------------------------------------
    spaceIsTheDaws ("as the window opens");

    rig.click (rig.canvas(), rig.onEmptyCanvas());
    check (rig.hasFocus ("canvas"), name + ": a click on the canvas gives it the focus");
    spaceIsTheDaws ("after a click on the canvas");

    // The bars: buttons, chips, the segmented Mode and their backgrounds, each only clicked.
    const std::pair<const char*, const char*> clicked[] = {
        { "tool-brush", "a tool button" }, { "brush-strip", "the brush strip's background" }, { "tool-select", "a tool button" },
        { "live", "Live" }, { "repeat", "Repeat (a chip)" }, { "mode", "Mode (a segmented control)" },
        { "header", "the top bar's background" }, { "footer", "the bottom bar's background" },
        { "layers", "Layers" }, { "inspector", "the drawer's background" }, { "inspector-close", "the drawer's close button" } };

    for (const auto& [id, what] : clicked)
    {
        auto& target = rig.get (id);
        const auto at = juce::String (id) == "brush-strip" ? juce::Point<float> (680.0f, 16.0f)
                      : juce::String (id) == "header"      ? juce::Point<float> (565.0f, 38.0f)
                      : juce::String (id) == "footer"      ? juce::Point<float> (400.0f, 4.0f)
                      : juce::String (id) == "inspector"   ? juce::Point<float> (120.0f, static_cast<float> (target.getHeight()) - 30.0f)
                                                           : target.getLocalBounds().toFloat().getCentre();
        rig.click (target, at);
        spaceIsTheDaws (juce::String ("after a click on ") + what);
        notOnAButton (what);

        // Back to how it was, except the brush (its strip is clicked next) and the drawer (so are
        // its background and its close button).
        editor.showLiveView (false);
        setParameter (processor, params::id::repeat, 0.0f);
        setParameter (processor, params::id::mode, 0.0f);
        pump (40);
        waitForTheMode (editor, processor);
    }

    check (! editor.isDrawerOpen(), name + ": the drawer's close button closed it");

    // Undo and redo are there only while they have something to do.
    check (! rig.get ("undo").isVisible() || ! rig.get ("redo").isVisible(), name + ": (undo and redo are not both there)");
    rig.click (rig.canvas(), rig.onText());
    check (rig.uses (juce::KeyPress (juce::KeyPress::rightKey)), name + ": an arrow nudges the selection");
    pump (1200);   // (a held arrow's undo step ends after a pause)
    check (rig.get ("undo").isVisible(), name + ": after an edit, Undo is there");
    rig.click ("undo");
    spaceIsTheDaws ("after a click on Undo (which then has nothing more to undo, and goes)");
    notOnAButton ("Undo");
    check (rig.get ("redo").isVisible(), name + ": ... and Redo is there");
    rig.click ("redo");
    spaceIsTheDaws ("after a click on Redo");
    notOnAButton ("Redo");
    rig.uses (escapeKey);
    pump (40);
    checkSoon ([&] { return editor.getSelectedObject().isNull() && ! editor.isDrawerOpen(); },
               name + ": Esc deselects, and the drawer goes");
    check (! rig.uses (escapeKey), name + ": with nothing selected and no drawer, Esc goes on to the DAW");

    // The Length menu: the keys are the menu's while it is open; closed, nothing stays modal.
    rig.click ("length");
    check (juce::Component::getCurrentlyModalComponent() != nullptr, name + ": a click on Length opens its menu");
    juce::PopupMenu::dismissAllActiveMenus();
    pump (80);
    spaceIsTheDaws ("after Length's menu has closed");
    check (! rig.uses (returnKey) && ! rig.uses (juce::KeyPress (juce::KeyPress::downKey)),
           name + ": Return and Down are not kept by Length after its menu");

    // Depth, dragged; the Sound strip (Draw): Lows clicked, Level dragged and typed.
    rig.drag (rig.get ("depth"), { 30.0f, 14.0f }, { 30.0f, 4.0f });
    spaceIsTheDaws ("after dragging Depth");
    rig.click ("depth");
    check (rig.editing() && ! rig.usesSpace() && ! rig.editing() && rig.hasFocus ("canvas"),
           name + ": a click on Depth starts typing, and the space bar after it is the DAW's");

    setParameter (processor, params::id::mode, 3.0f);
    pump (80);
    waitForTheMode (editor, processor);
    rig.click ("lows");
    spaceIsTheDaws ("after a click on Lows");
    rig.click (rig.get ("sound-strip"), { 700.0f, 16.0f });
    spaceIsTheDaws ("after a click on the Sound strip's background");
    rig.drag (rig.get ("level"), { 30.0f, 12.0f }, { 30.0f, 4.0f });
    spaceIsTheDaws ("after dragging Level");

    // One click on a value and then the space bar: the DAW plays (the customer's "press the
    // normal play button ... it should play instead of me having to click out").
    for (const auto* id : { "level", "density", "contrast" })
    {
        rig.click (id);
        check (rig.editing() && ! rig.typing(), name + ": a click on " + juce::String (id) + " starts typing a number into it");
        check (! rig.usesSpace() && ! rig.editing() && rig.hasFocus ("canvas"),
               name + ": ... and the space bar after it is the DAW's, and ends the typing");
    }

    rig.click ("level");
    rig.type ("-12");
    check (! rig.usesSpace() && std::abs (parameterValue (params::id::level) + 12.0f) < 1.0e-3f,
           name + ": the space bar sets a typed level and is the DAW's");
    rig.click ("level");
    rig.type ("-9");
    check (rig.uses (returnKey) && std::abs (parameterValue (params::id::level) + 9.0f) < 1.0e-3f, name + ": Return sets the typed level");
    check (! rig.editing() && rig.hasFocus ("canvas"), name + ": ... and hands the focus back to the canvas");
    spaceIsTheDaws ("after typing a level");

    // A value being typed when its strip goes (automation changes the mode): nothing keeps the keys.
    rig.click ("density");
    check (rig.editing(), name + ": (typing a density)");
    setParameter (processor, params::id::mode, 0.0f);
    pump (120);
    checkSoon ([&] { return ! rig.get ("sound-strip").isVisible() && ! rig.editing(); },
               name + ": the Sound strip goes with the mode, and the typing with it");
    spaceIsTheDaws ("after the Sound strip went while a value was typed");
    setParameter (processor, params::id::lowEnd, 0.0f);
    setParameter (processor, params::id::level, -6.0f);

    // The panels: the keys are the panel's while it is open (Esc closes it), and it has no use
    // for the space bar; closed, the canvas has the focus again.
    for (const auto* button : { "help", "settings" })
    {
        const auto panel = juce::String (button) + "-panel";
        rig.click (button);
        check (rig.get (panel).isShowing() && rig.hasFocus (panel), name + ": the " + button + " panel has the focus while it is open");
        spaceIsTheDaws (juce::String ("with the ") + button + " panel open");
        check (rig.uses (escapeKey) && ! rig.get (panel).isVisible(), name + ": Esc closes the " + button + " panel");
        check (rig.hasFocus ("canvas"), name + ": ... and the focus is the canvas's again");
        spaceIsTheDaws (juce::String ("after the ") + button + " panel has closed");
    }

    // The timing in the settings: segmented controls clicked, the start bar typed.
    rig.click ("settings");

    for (const auto* id : { "trigger", "tempo-source", "start-at" })
    {
        rig.click (rig.get (id), { static_cast<float> (rig.get (id).getWidth()) * 0.75f, 14.0f });
        check (rig.hasFocus ("settings-panel"), name + ": a click on " + juce::String (id) + " in the settings leaves the focus with the panel");
        spaceIsTheDaws (juce::String ("after a click on ") + id + " in the settings");
        setParameter (processor, params::id::trigger, 0.0f);
        pump (60);
    }

    checkSoon ([&] { return juce::roundToInt (parameterValue (params::id::startAt)) == params::atBarStartIndex && rig.get ("start-bar").isShowing()
                            && rig.get ("bpm").isShowing(); }, name + ": (At bar and a typed tempo: their fields are there)");
    rig.click ("start-bar");
    check (rig.editing() && ! rig.typing(), name + ": a click on the start bar field starts typing a number into it");
    rig.type ("9");
    check (! rig.usesSpace() && juce::roundToInt (parameterValue (params::id::startBar)) == 9 && rig.hasFocus ("settings-panel"),
           name + ": ... where the space bar sets the typed bar, is the DAW's, and hands the focus back to the settings");
    rig.click ("start-bar");
    rig.type ("12");
    check (rig.uses (returnKey) && juce::roundToInt (parameterValue (params::id::startBar)) == 12, name + ": Return sets the typed bar");
    check (! rig.editing() && rig.hasFocus ("settings-panel"), name + ": ... and hands the focus back to the settings");
    spaceIsTheDaws ("after typing a start bar");
    rig.drag (rig.get ("bpm"), { 30.0f, 14.0f }, { 30.0f, 4.0f });
    spaceIsTheDaws ("after dragging the BPM field");
    check (rig.uses (escapeKey) && ! rig.get ("settings-panel").isVisible() && rig.hasFocus ("canvas"), name + ": Esc closes the settings");
    setParameter (processor, params::id::startBar, 1.0f);
    setParameter (processor, params::id::startAt, static_cast<float> (params::onPlayStartIndex));
    setParameter (processor, params::id::tempoSource, 0.0f);
    setParameter (processor, params::id::bpm, 120.0f);
    pump (60);

    // The live view: no canvas to hold the focus.
    rig.click ("live");
    spaceIsTheDaws ("in the live view");
    rig.click ("repeat");
    spaceIsTheDaws ("in the live view, after a click");
    setParameter (processor, params::id::repeat, 0.0f);
    rig.click ("live");
    pump (60);
    checkSoon ([&] { return rig.get ("canvas").isShowing(); }, name + ": Live again shows the canvas");

    // ---- the drawer: the layer list -------------------------------------------------------------------
    rig.click ("layers");
    auto* list = dynamic_cast<ui::LayerList*> (findComponentWithId (editor, "layer-list"));

    if (list != nullptr && list->isShowing())
    {
        rig.click (*list, list->getRowBounds (1).toFloat().getCentre().withX (60.0f));
        check (rig.hasFocus ("layer-list") && editor.getSelectedObject() == rig.textId && editor.isLayerListShown(),
               name + ": a click on a row of the layer list selects its object and gives the list the focus");
        spaceIsTheDaws ("with the layer list focused");
        check (rig.uses (escapeKey) && editor.getSelectedObject().isNull() && editor.isLayerListShown(), name + ": Esc deselects; the list stays");
        check (rig.uses (escapeKey) && ! editor.isDrawerOpen(), name + ": Esc again closes the list");
        check (rig.hasFocus ("canvas"), name + ": ... and the focus is the canvas's");
        check (! rig.uses (escapeKey), name + ": ... and a third Esc goes on to the DAW");
        spaceIsTheDaws ("after the layer list has closed");
    }
    else
    {
        check (false, name + ": the Layers button shows the layer list");
    }

    // ---- a text object: selected is not typing; the drawer keeps clear of the mouse -----------------------
    auto& textBox = rig.get ("inspector-text");

    rig.click (rig.canvas(), rig.onText());
    check (editor.getSelectedObject() == rig.textId && editor.isDrawerOpen() && ! editor.isDrawerOnLeft() && textBox.isShowing()
           && ! textBox.hasKeyboardFocus (true) && rig.hasFocus ("canvas"),
           name + ": a click on a text object selects it and opens its settings on the right; the focus stays with the canvas");
    spaceIsTheDaws ("with a text object selected");
    check (rig.text (rig.textId) == "HELLO", name + ": ... and nothing was typed into the text");

    // The close button: the drawer goes, the selection stays; a click on the object brings it back.
    rig.click ("inspector-close");
    check (! editor.isDrawerOpen() && editor.getSelectedObject() == rig.textId && rig.hasFocus ("canvas"),
           name + ": the drawer's close button closes it and keeps the selection");
    spaceIsTheDaws ("after closing the drawer");
    rig.drag (rig.canvas(), rig.onText(), rig.onText().translated (20.0f, 0.0f));
    check (! editor.isDrawerOpen(), name + ": dragging the object leaves the drawer closed");
    processor.undo();
    pump (60);
    rig.click (rig.canvas(), rig.onText());
    check (editor.isDrawerOpen() && editor.getSelectedObject() == rig.textId, name + ": a click on the selected object opens its settings again");

    // The text on the right (under the open drawer: Esc first): its settings open on the left, so
    // the double-click's second click lands on the text, not on a control of a drawer that
    // opened under the mouse.
    rig.uses (escapeKey);
    pump (40);
    rig.click (rig.canvas(), onRightText());
    check (editor.getSelectedObject() == rightId && editor.isDrawerOpen() && editor.isDrawerOnLeft(),
           name + ": a click on an object on the right opens its settings on the left");
    saveWindow ("artfreq-fx-drawer-left.png");
    rig.uses (escapeKey);
    pump (40);
    checkSoon ([&] { return ! editor.isDrawerOpen(); },
               name + ": (Esc: deselected, the drawer gone)");

    rig.doubleClick (rig.canvas(), onRightText());
    check (editor.getSelectedObject() == rightId && editor.isDrawerOnLeft() && textBox.hasKeyboardFocus (true) && rig.typing(),
           name + ": a double-click on the text on the right selects it and puts the focus in its text box");
    rig.type ("PLAY IT");
    check (rig.text (rightId) == "PLAY IT", name + ": typed text goes into the text box, the space too (\"" + rig.text (rightId) + "\")");
    check (rig.usesSpace() && rig.text (rightId) == "PLAY IT ", name + ": the space bar types a space while the text box is typed in");
    saveWindow ("artfreq-fx-typing.png");
    check (rig.uses (returnKey) && ! rig.typing() && rig.hasFocus ("canvas"),
           name + ": Return ends the typing and hands the focus back to the canvas");
    spaceIsTheDaws ("after Return in the text box");

    // Dragged to the left, under its drawer: the drawer moves aside when the object is let go.
    {
        const auto from = onRightText();
        const auto to = juce::Point<float> (rig.canvas().getDrawingArea().getX() + 0.12f * rig.canvas().getDrawingArea().getWidth(), from.y);
        rig.drag (rig.canvas(), from, to);
        const auto* moved = processor.getDocument().find (rightId);
        check (moved != nullptr && moved->startTime < 0.2f && editor.isDrawerOpen() && ! editor.isDrawerOnLeft(),
               name + ": dragged under the drawer, the object is uncovered: the drawer goes to the other side");
        processor.undo();
        pump (60);
    }

    rig.doubleClick (rig.canvas(), rig.onText());
    rig.type ("NOW");
    check (rig.uses (escapeKey) && ! rig.typing() && rig.hasFocus ("canvas") && rig.text (rig.textId) == "NOW",
           name + ": Esc ends the typing too");
    spaceIsTheDaws ("after Esc in the text box");

    rig.doubleClick (rig.canvas(), rig.onText());
    check (textBox.hasKeyboardFocus (true), name + ": (typing again)");
    rig.click ("mode");
    check (! textBox.hasKeyboardFocus (true) && ! rig.typing(), name + ": a click anywhere else ends the typing");
    spaceIsTheDaws ("after clicking away from the text box");
    setParameter (processor, params::id::mode, 0.0f);
    pump (40);

    // + Text: typing is what comes next, so the drawer opens on the new text and its box has the focus.
    rig.uses (escapeKey);
    pump (40);
    const auto objectsBefore = processor.getDocument().objects.size();
    rig.click ("add-text");
    pump (60);
    const auto newId = editor.getSelectedObject();
    check (processor.getDocument().objects.size() == objectsBefore + 1 && editor.isDrawerOpen() && textBox.isShowing()
           && textBox.hasKeyboardFocus (true) && rig.typing(), name + ": + Text opens the drawer on the new text, with the focus in its text box");
    rig.type ("NEW ONE");
    check (rig.text (newId) == "NEW ONE", name + ": ... where typing replaces its text (\"" + rig.text (newId) + "\")");
    check (rig.uses (returnKey) && rig.hasFocus ("canvas"), name + ": ... and Return hands the focus back");
    spaceIsTheDaws ("after + Text, typing and Return");

    // A bar in the drawer, typed.
    rig.doubleClick (rig.get ("inspector-intensity"), { 60.0f, 13.0f });
    pump (40);

    if (rig.typing())
    {
        rig.type ("50");
        check (rig.uses (returnKey) && juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas"),
               name + ": a value typed into the drawer's Intensity is set, and the focus goes back to the canvas");
        spaceIsTheDaws ("after typing an intensity");
    }

    // ---- the keys the editor does use are still its own ------------------------------------------------
    // (Between the two texts, above the new one in the lows: nothing is there.)
    rig.uses (escapeKey);
    pump (40);
    rig.click (rig.canvas(), { rig.canvas().getDrawingArea().getX() + 0.56f * rig.canvas().getDrawingArea().getWidth(),
                               rig.canvas().getDrawingArea().getY() + 0.45f * rig.canvas().getDrawingArea().getHeight() });
    check (editor.getSelectedObject().isNull(), name + ": (a click on empty canvas selects nothing)");
    const auto canvasBefore = rig.canvas().getBounds();
    check (rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')) && rig.get ("brush-strip").isVisible(),
           name + ": B chooses the brush, and its strip is there");
    check (rig.canvas().getBounds() == canvasBefore, name + ": ... over the canvas's time axis: the canvas keeps its size and place");
    check (rig.uses (juce::KeyPress ('v', juce::ModifierKeys(), 'v')) && ! rig.get ("brush-strip").isVisible()
               && rig.canvas().getBounds() == canvasBefore, name + ": V the Select tool again (the canvas as it was)");
    check (! rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)) && ! rig.uses (juce::KeyPress (juce::KeyPress::leftKey))
           && ! rig.uses (returnKey) && ! rig.uses (juce::KeyPress ('a', juce::ModifierKeys(), 'a'))
           && ! rig.uses (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys(), '\t')),
           name + ": with nothing selected, Delete, the arrows, Return, Tab and other letters go on to the DAW too");
    pump (60);

    rig.click (rig.canvas(), rig.onText());
    check (rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)) && rig.text (rig.textId) == "(gone)" && ! editor.isDrawerOpen(),
           name + ": Delete removes the selected object, and its drawer goes");
    check (rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) && rig.text (rig.textId) == "NOW",
           name + ": Cmd+Z brings it back");
    spaceIsTheDaws ("at the end");
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

    using namespace artfreq;

    // Every instance here is activated (a folder of the tool's own, a fake server), as a buyer
    // sees the plug-ins once activated; checkLicensing makes its own locked ones.
    licensetest::installRunnerLicensing();

    ArtFreqProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    {
        const std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));

        if (editor == nullptr)
        {
            std::cerr << "The processor made no ArtFreqEditor" << std::endl;
            return 1;
        }

        check (editor->getWidth() == ArtFreqEditor::baseWidth && editor->getHeight() == ArtFreqEditor::baseHeight,
               "a new editor opens at 1120 x 720");

        const auto save = [&] (const juce::String& name, float scale, bool settle = true)
        {
            processor.rebuildPlanIfNeeded();

            if (settle)
            {
                pump (60);
                settleCanvas (*editor, scale, name);
            }

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
            const auto file = outputDir.getChildFile ("artfreq-" + name + ".png");

            if (writePng (image, file))
                std::cout << file.getFullPathName() << std::endl;
            else
                check (false, "write " + file.getFullPathName());

            saveStateBeside (file, processor, *editor, scale);
        };

        auto* content = findComponentWithId (*editor, "content");
        check (content != nullptr, "the editor has its content");

        if (content == nullptr)
            return 1;

        // The footer's timing columns alone (Length to BPM, with their hint line), as a display of
        // twice the window's pixels shows them: On play and At bar 9, at the window's current size.
        const auto saveFooterStates = [&] (const juce::String& size)
        {
            auto* footer = dynamic_cast<ui::shell::Footer*> (findComponentWithId (*editor, "footer"));
            auto* length = findComponentWithId (*editor, "length");
            auto* bpm = findComponentWithId (*editor, "bpm");
            auto* startChoice = findComponentWithId (*editor, "start-choice");
            auto* startBar = findComponentWithId (*editor, "start-bar");

            if (footer == nullptr || length == nullptr || bpm == nullptr || startChoice == nullptr || startBar == nullptr)
                return;

            const auto columns = juce::Rectangle<int> (length->getX() - 24, 0, bpm->getRight() + 24 - (length->getX() - 24),
                                                       footer->getHeight());

            for (const auto atBar : { false, true })
            {
                setParameter (processor, params::id::startBar, atBar ? 9.0f : 1.0f);
                footer->setStartAt (atBar ? StartAt::bar : StartAt::play);
                pump (60);

                // In line at every size: the choice (and the field) on the hint's line, inside the
                // Trigger column's width, clear of Tempo's hint.
                check (startChoice->isVisible() && startBar->isVisible() == atBar
                       && (atBar ? startBar : startChoice)->getRight() <= findComponentWithId (*editor, "repeat")->getRight() + 8,
                       juce::String (atBar ? "At bar" : "On play") + ": the hint line fits under Trigger at " + size + " %");

                const auto area = editor->getLocalArea (footer, columns).getIntersection (editor->getLocalBounds());
                const auto image = editor->createComponentSnapshot (area, true, 2.0f);
                const auto file = outputDir.getChildFile ("artfreq-footer-" + juce::String (atBar ? "at-bar-" : "on-play-") + size + ".png");

                if (writePng (image, file))
                    std::cout << file.getFullPathName() << std::endl;
                else
                    check (false, "write " + file.getFullPathName());
            }

            setParameter (processor, params::id::startBar, 1.0f);
            footer->setStartAt (StartAt::play);
            pump (30);
        };

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
        checkSoon ([&] { return processor.isSounding(); },
                   "Preview makes the processor sound");
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
        // The canvas's measured picture of the drawing, for the live view's to agree with.
        juce::Image canvasPicture;

        if (auto* canvasView = find<ui::CanvasView> (*editor, "canvas"))
        {
            canvasView->settleMeasuredPreview (1.0f);
            canvasPicture = canvasView->createComponentSnapshot (canvasView->getDrawingArea().toNearestInt());
        }

        editor->showLiveView (true);

        if (auto* live = find<ui::LiveView> (*editor, "live-view"))
        {
            check (live->isVisible(), "the live view is shown");
            live->fillFromPlanForSnapshot (live->getVisibleSeconds());

            // The same sound looks the same in both: the same analysis and colours, so the brightest
            // and the typical drawn levels agree (to 4 dB; the views differ in time span and in
            // where their columns fall).
            const auto livePlot = ui::axes::plotArea (live->getLocalBounds(), ui::CanvasView::axisWidth, ui::CanvasView::axisHeight);
            const auto livePicture = live->createComponentSnapshot (livePlot);
            const auto canvasLevels = colourLevels (canvasPicture), liveLevels = colourLevels (livePicture);
            const auto top = [] (const std::vector<int>& levels, double p)
            {
                std::vector<int> lit;

                for (const auto level : levels)
                    if (level > 40)
                        lit.push_back (level);

                if (lit.empty())
                    return 0;

                std::sort (lit.begin(), lit.end());
                return lit[static_cast<size_t> (p * static_cast<double> (lit.size() - 1))];
            };

            std::cout << "  canvas / live view colour levels: 99th percentile " << top (canvasLevels, 0.99) << " / "
                      << top (liveLevels, 0.99) << ", median of the lit " << top (canvasLevels, 0.5) << " / " << top (liveLevels, 0.5)
                      << std::endl;
            check (std::abs (top (canvasLevels, 0.99) - top (liveLevels, 0.99)) <= 10
                       && std::abs (top (canvasLevels, 0.5) - top (liveLevels, 0.5)) <= 10,
                   "the live view shows the drawing's sound in the canvas's colours");
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
            check (first.getFileName().startsWith ("ArtFreq HELLO "), "the file is named after the drawing's text: " + first.getFileName());
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
        saveFooterStates ("100");

        setWindowScale (*editor, 2.0f);
        pump (60);
        save ("200", 1.0f);
        saveFooterStates ("200");

        setWindowScale (*editor, 1.5f);
        pump (60);
        checkSoon ([&] { return processor.getEditorWidth() == 1680 && processor.getEditorHeight() == 1080; },
                   "the window size is remembered");
        save ("150", 1.0f);
        saveFooterStates ("150");

        setWindowScale (*editor, 0.75f);
        pump (60);
        save ("75", 1.0f);
        saveFooterStates ("75");

        setWindowScale (*editor, 1.25f);
        pump (30);

        // ---- the live view in a real (virtual) window: its own timer turns the analyser on,
        // scrolls what the processor plays, and scrolls silence once the host stops calling.
        editor->addToDesktop (0);
        editor->setVisible (true);
        pump (200);
        testwait::pumpUntil ([&] { return editor->isShowing(); });

        if (auto* live = find<ui::LiveView> (*editor, "live-view"); live != nullptr && editor->isShowing())
        {
            editor->showLiveView (true);
            pump (100);
            checkSoon ([&] { return processor.getAnalyzer().isActive(); }, "the live view, on screen, turns the analyser on");

            processor.setPreview (true);

            for (int i = 0; i < 20; ++i)
            {
                playFor (processor, 0.05);
                pump (50);
            }

            processor.setPreview (false);
            playFor (processor, 0.1);
            pump (600);  // past the point where the view stops waiting for the host

            // (It scrolls on its own timer: on a slow machine the view may take longer than the
            // 400 ms a fast one needs to move this far.)
            const auto before = live->createComponentSnapshot (live->getLocalBounds());
            pump (400);
            checkSoon ([&] { return countDifferentPixels (before, live->createComponentSnapshot (live->getLocalBounds())) > 1000; },
                       "with the host silent, the live view keeps scrolling");

            editor->showLiveView (false);
            pump (100);
            checkSoon ([&] { return ! processor.getAnalyzer().isActive(); }, "hiding the live view turns the analyser off");
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

    // ---- ArtFreq FX: the same editor on the effect ----------------------------------------------
    checkEffect (outputDir, pixelScale);
    checkEffectOnTheUsersSong (outputDir, pixelScale);
    pump (50);

    // ---- the DAW's play key, in both plug-ins --------------------------------------------------------
    checkKeys (outputDir, pixelScale);
    checkEffectKeys (outputDir, pixelScale);
    pump (50);

    // ---- licensing: the activation screen and the Licence card, in both plug-ins ----------------
    checkLicensing (outputDir, pixelScale);
    pump (50);
    licensetest::removeRunnerLicensing();

    if (failed)
        std::cerr << "Some checks failed" << std::endl;
    else
        std::cout << checksRun << " checks passed" << std::endl;

    return failed ? 1 : 0;
}
