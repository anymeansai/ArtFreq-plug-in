// The editor, driven like a user would (synthetic mouse, wheel and key events delivered straight
// to its components, no window): bounces, undo steps of wheel and arrow-key gestures, clicking
// and placing objects, the live view's span, pictures that cannot be read.
//
// These need JUCE's GUI classes, so on Linux run them under a virtual display (xvfb-run -a).

#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "dsp/WavExporter.h"
#include "ui/Actions.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/LiveView.h"
#include "ui/ShellExport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chrono>
#include <cmath>
#include <memory>

using namespace freqart;
using testutil::setParameter;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

std::unique_ptr<FreqArtProcessor> makeProcessor()
{
    auto processor = std::make_unique<FreqArtProcessor>();
    processor->setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);
    return processor;
}

std::unique_ptr<FreqArtEditor> makeEditor (FreqArtProcessor& processor)
{
    return std::unique_ptr<FreqArtEditor> (dynamic_cast<FreqArtEditor*> (processor.createEditor()));
}

void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
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

juce::MouseEvent mouseEventAt (juce::Component& c, juce::Point<float> position, juce::Point<float> downPosition,
                               juce::ModifierKeys mods, juce::Time time = juce::Time::getCurrentTime(), bool dragged = false)
{
    return { juce::Desktop::getInstance().getMainMouseSource(), position, mods, juce::MouseInputSource::defaultPressure,
             0.0f, 0.0f, 0.0f, 0.0f, &c, &c, time, downPosition, time, 1, dragged };
}

void clickOn (juce::Component& c, juce::Point<float> at)
{
    c.mouseDown (mouseEventAt (c, at, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
    c.mouseUp (mouseEventAt (c, at, at, {}));
}

void dragOn (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
{
    const auto held = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    c.mouseDown (mouseEventAt (c, from, from, held));

    for (int i = 1; i <= 8; ++i)
        c.mouseDrag (mouseEventAt (c, from + (to - from) * (static_cast<float> (i) / 8.0f), from, held, juce::Time::getCurrentTime(), true));

    c.mouseUp (mouseEventAt (c, to, from, {}, juce::Time::getCurrentTime(), true));
}

/** Wheel steps as a trackpad sends them: small, each with its own time (a slider ignores a
    repeated one). */
void wheel (juce::Component& c, int steps, float deltaY = -0.02f)
{
    static juce::int64 lastTime = juce::Time::currentTimeMillis();

    for (int i = 0; i < steps; ++i)
    {
        lastTime = juce::jmax (lastTime + 1, juce::Time::currentTimeMillis());
        const auto centre = c.getLocalBounds().getCentre().toFloat();
        juce::MouseWheelDetails details {};
        details.deltaY = deltaY;
        details.isSmooth = true;
        c.mouseWheelMove (mouseEventAt (c, centre, centre, {}, juce::Time (lastTime)), details);
    }
}

/** Where an object is on the canvas, in the canvas's coordinates. */
juce::Rectangle<float> onCanvas (ui::CanvasView& canvas, const ArtObject& object, const FrequencyScale& scale)
{
    const auto a = canvas.getDrawingArea();
    const auto n = objectRectNorm (object, scale);
    return { a.getX() + n.getX() * a.getWidth(), a.getY() + n.getY() * a.getHeight(),
             n.getWidth() * a.getWidth(), n.getHeight() * a.getHeight() };
}

ArtObject textIn (Band band, float start, float end, const juce::String& text)
{
    const auto range = bandRange (band, FrequencyScale {});
    auto object = ArtDocument::makeTextObject (text, static_cast<float> (range.low), static_cast<float> (range.high));
    object.startTime = start;
    object.endTime = end;
    return object;
}

bool overlaps (const ArtObject& a, const ArtObject& b, const FrequencyScale& scale)
{
    const auto r = objectRectNorm (a, scale).getIntersection (objectRectNorm (b, scale));
    return r.getWidth() > 1.0e-4f && r.getHeight() > 1.0e-4f;
}

bool samePlacement (const ArtObject& a, const ArtObject& b)
{
    return juce::approximatelyEqual (a.startTime, b.startTime) && juce::approximatelyEqual (a.endTime, b.endTime)
        && juce::approximatelyEqual (a.lowHz, b.lowHz) && juce::approximatelyEqual (a.highHz, b.highHz);
}

double secondsSince (std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
}

} // namespace

//==============================================================================
FREQART_TEST (uiEveryBounceHasItsOwnFile)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processorA = makeProcessor();
    auto processorB = makeProcessor();
    setParameter (*processorB, params::id::level, -30.0f);

    auto editorA = makeEditor (*processorA);
    auto editorB = makeEditor (*processorB);
    auto* exportA = find<ui::shell::ExportPanel> (*editorA, "export");
    auto* exportB = find<ui::shell::ExportPanel> (*editorB, "export");
    CHECK (exportA != nullptr && exportB != nullptr);

    if (exportA == nullptr || exportB == nullptr)
        return;

    exportA->startBounce();
    CHECK (exportA->waitForBounce (60000) && exportA->hasCurrentFile());
    const auto fileA = exportA->getBounceFile();
    const auto bytesA = fileA.getSize();
    const auto writtenA = fileA.getLastModificationTime();

    // Another instance's bounce never touches this one's file, so its chip still hands out what
    // was drawn here.
    exportB->startBounce();
    CHECK (exportB->waitForBounce (60000) && exportB->hasCurrentFile());
    const auto fileB = exportB->getBounceFile();
    exportA->refresh();

    CHECK (fileA != fileB);
    CHECK (fileA.getParentDirectory() == ui::shell::ExportPanel::getBounceFolder() && fileA.getFileName().startsWith ("FreqArt-"));
    CHECK (exportA->hasCurrentFile() && fileA.existsAsFile());
    CHECK (fileA.getSize() == bytesA && fileA.getLastModificationTime() == writtenA);

    // A new bounce (after a change) is a new file; the one before, never dragged out, goes.
    setParameter (*processorA, params::id::density, 200.0f);
    processorA->rebuildPlanIfNeeded();
    exportA->refresh();
    CHECK (! exportA->hasCurrentFile());
    exportA->startBounce();
    CHECK (exportA->waitForBounce (60000) && exportA->hasCurrentFile());
    const auto fileA2 = exportA->getBounceFile();
    CHECK (fileA2 != fileA && fileA2.existsAsFile() && ! fileA.exists());
    CHECK (fileB.existsAsFile());

    // Closing an editor takes its bounce with it.
    editorA.reset();
    CHECK (! fileA2.exists() && fileB.existsAsFile());
    editorB.reset();
    CHECK (! fileB.exists());
}

FREQART_TEST (uiOldBouncesAreSweptAway)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto folder = ui::shell::ExportPanel::getBounceFolder();
    folder.createDirectory();

    const auto stale = folder.getChildFile ("FreqArt-uiTestStale.wav");
    const auto fresh = folder.getChildFile ("FreqArt-uiTestFresh.wav");
    const auto other = folder.getChildFile ("uiTestOther.wav");

    for (const auto& file : { stale, fresh, other })
        file.replaceWithText ("x");

    const auto twoDaysAgo = juce::Time::getCurrentTime() - juce::RelativeTime::days (2.0);
    stale.setLastModificationTime (twoDaysAgo);
    other.setLastModificationTime (twoDaysAgo);

    auto processor = makeProcessor();
    auto editor = makeEditor (*processor);

    CHECK (! stale.exists());
    CHECK (fresh.existsAsFile());
    CHECK (other.existsAsFile());   // only FreqArt's own bounces

    fresh.deleteFile();
    other.deleteFile();
}

FREQART_TEST (uiClosingDuringABounceDoesNotWaitForIt)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();

    // Several minutes of a solid drawing: a render that takes a while.
    setParameter (*processor, params::id::tempoSource, 1.0f);
    setParameter (*processor, params::id::bpm, 20.0f);
    setParameter (*processor, params::id::length, 7.0f);   // 32 bars: 384 s
    setParameter (*processor, params::id::density, 512.0f);
    processor->editDocument ([] (ArtDocument& document)
    {
        document.paint.assign (static_cast<size_t> (ArtDocument::paintWidth * ArtDocument::paintHeight), 200);
    });
    processor->rebuildPlanIfNeeded();

    const auto plan = processor->getCurrentPlan();
    CHECK (plan != nullptr && std::abs (processor->getLengthSeconds() - 384.0) < 1.0e-6);

    if (plan == nullptr)
        return;

    // How long the whole render takes here: an eighth of it timed (the same plan, to a small
    // file), times eight.
    ExportSettings settings;
    settings.sampleRate = sampleRate;
    settings.seconds = processor->getLengthSeconds() / 8.0;
    settings.gain = processor->getOutputGain();
    settings.numChannels = 1;
    settings.bitDepth = 16;
    const auto scratch = juce::File::createTempFile (".wav");
    juce::String error;
    const auto renderStart = std::chrono::steady_clock::now();
    CHECK (renderPlanToWav (*plan, settings, scratch, error));
    const auto fullRender = secondsSince (renderStart) * 8.0;
    scratch.deleteFile();

    auto editor = makeEditor (*processor);
    auto* exporter = find<ui::shell::ExportPanel> (*editor, "export");
    CHECK (exporter != nullptr);

    if (exporter == nullptr)
        return;

    const auto bounces = [] { return ui::shell::ExportPanel::getBounceFolder().getNumberOfChildFiles (juce::File::findFiles, "FreqArt-*.wav"); };
    const auto before = bounces();
    exporter->startBounce();
    CHECK (exporter->isBouncing());
    pump (50);

    const auto closeStart = std::chrono::steady_clock::now();
    editor.reset();
    const auto closing = secondsSince (closeStart);

    CHECK_MSG (closing < fullRender * 0.25, "closing took " << closing << " s, the render " << fullRender << " s");
    CHECK (bounces() == before);   // nothing left behind, not even the temporary file
}

//==============================================================================
FREQART_TEST (uiWheelGestureIsOneUndoStep)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();
    auto editor = makeEditor (*processor);
    const auto textId = processor->getDocument().objects.front().id;
    const auto intensityOf = [&] { return processor->getDocument().find (textId)->intensity; };

    // An earlier edit, which must still be there to undo afterwards.
    processor->editDocument ([textId] (ArtDocument& document) { document.find (textId)->fontIndex = 1; });

    editor->selectObject (textId);
    pump (30);
    auto* intensity = find<juce::Slider> (*editor, "inspector-intensity");
    CHECK (intensity != nullptr && intensity->isVisible());

    if (intensity == nullptr)
        return;

    // A long trackpad swipe: one undo step, which takes it all back.
    wheel (*intensity, 45);
    CHECK_NEAR (intensityOf(), 0.55f, 1.0e-4);
    CHECK (processor->undo());
    CHECK_NEAR (intensityOf(), 1.0f, 1.0e-6);
    CHECK (processor->getDocument().find (textId)->fontIndex == 1);
    CHECK (processor->undo() && processor->getDocument().find (textId)->fontIndex == 0);
    CHECK (processor->redo());
    pump (30);

    // Another edit in between starts a new step.
    wheel (*intensity, 3);
    processor->editDocument ([textId] (ArtDocument& document) { document.find (textId)->fontIndex = 2; });
    wheel (*intensity, 2);
    CHECK_NEAR (intensityOf(), 0.95f, 1.0e-4);
    CHECK (processor->undo());
    CHECK_NEAR (intensityOf(), 0.97f, 1.0e-4);
    CHECK (processor->getDocument().find (textId)->fontIndex == 2);
    CHECK (processor->undo() && processor->getDocument().find (textId)->fontIndex == 1);
    CHECK (processor->undo());
    CHECK_NEAR (intensityOf(), 1.0f, 1.0e-6);
    pump (30);

    // So does a pause.
    wheel (*intensity, 2);
    juce::Thread::sleep (1100);
    wheel (*intensity, 2);
    CHECK_NEAR (intensityOf(), 0.96f, 1.0e-4);
    CHECK (processor->undo());
    CHECK_NEAR (intensityOf(), 0.98f, 1.0e-4);
}

FREQART_TEST (uiNudgeGesturesAreSeparateUndoSteps)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();

    ArtDocument document;
    document.objects.push_back (textIn (Band::mid, 0.10f, 0.30f, "A"));
    document.objects.push_back (textIn (Band::high, 0.50f, 0.70f, "B"));
    processor->setDocument (document);
    const auto a = document.objects[0], b = document.objects[1];
    const auto startOf = [&] (const ArtObject& o)
    {
        const auto* found = processor->getDocument().find (o.id);
        return found != nullptr ? found->startTime : -1.0f;
    };
    const auto right = juce::KeyPress (juce::KeyPress::rightKey);

    // One editor nudges A and is closed; the next nudges B. Undo takes back B's nudge only.
    {
        auto editor = makeEditor (*processor);
        auto* content = findComponent (*editor, "content");
        editor->selectObject (a.id);
        content->keyPressed (right);
        content->keyPressed (right);
    }

    const auto movedA = startOf (a);
    CHECK (movedA > a.startTime);

    auto editor = makeEditor (*processor);
    auto* content = findComponent (*editor, "content");
    auto* canvas = find<ui::CanvasView> (*editor, "canvas");
    editor->selectObject (b.id);
    content->keyPressed (right);
    CHECK (startOf (b) > b.startTime);

    CHECK (processor->undo());
    CHECK (juce::exactlyEqual (startOf (b), b.startTime));
    CHECK (juce::exactlyEqual (startOf (a), movedA));
    CHECK (processor->undo());
    CHECK (juce::exactlyEqual (startOf (a), a.startTime));

    // Holding an arrow (its repeats) is one step; letting go ends it.
    editor->selectObject (a.id);
    content->keyPressed (right);
    content->keyPressed (right);
    const auto held = startOf (a);
    canvas->keyStateChanged (false);
    content->keyPressed (right);
    CHECK (startOf (a) > held);
    CHECK (processor->undo() && juce::exactlyEqual (startOf (a), held));
    CHECK (processor->undo() && juce::exactlyEqual (startOf (a), a.startTime));

    // Another object is another step.
    content->keyPressed (right);
    editor->selectObject (b.id);
    content->keyPressed (right);
    CHECK (processor->undo() && juce::exactlyEqual (startOf (b), b.startTime) && startOf (a) > a.startTime);
}

//==============================================================================
FREQART_TEST (uiClickInsideTheSelectionKeepsIt)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();

    // B lies partly over A.
    ArtDocument document;
    document.objects.push_back (textIn (Band::mid, 0.10f, 0.60f, "A"));
    document.objects.push_back (textIn (Band::mid, 0.40f, 0.90f, "B"));
    processor->setDocument (document);
    const auto a = document.objects[0], b = document.objects[1];

    auto editor = makeEditor (*processor);
    auto* canvas = find<ui::CanvasView> (*editor, "canvas");
    auto* inspector = findComponent (*editor, "inspector");
    CHECK (canvas != nullptr && inspector != nullptr);

    if (canvas == nullptr)
        return;

    const auto scale = processor->getScale();
    const auto shared = onCanvas (*canvas, a, scale).getIntersection (onCanvas (*canvas, b, scale)).getCentre();
    const auto selected = [&]
    {
        pump (20);
        auto* field = find<juce::TextEditor> (*editor, "inspector-text");
        return field != nullptr && field->isVisible() ? field->getText() : juce::String();
    };

    // Nothing selected: the top-most.
    clickOn (*canvas, shared);
    CHECK (selected() == "B");

    // A selected (clicked where B does not cover it): a click or a drag where they overlap
    // keeps A, and moves A.
    clickOn (*canvas, onCanvas (*canvas, a, scale).getCentre().withX (onCanvas (*canvas, a, scale).getX() + 10.0f));
    CHECK (selected() == "A");
    clickOn (*canvas, shared);
    CHECK (selected() == "A");
    dragOn (*canvas, shared, shared + juce::Point<float> (-30.0f, 0.0f));
    CHECK (processor->getDocument().find (a.id)->startTime < a.startTime);
    CHECK (samePlacement (*processor->getDocument().find (b.id), b));
    CHECK (selected() == "A");
}

FREQART_TEST (uiNewObjectsDoNotLandOnOthers)
{
    const FrequencyScale scale;

    // Half a band free: the new object goes there.
    {
        ArtDocument halves;

        for (const auto band : { Band::low, Band::mid, Band::high })
            halves.objects.push_back (textIn (band, 0.04f, 0.48f, "x"));

        const auto place = ui::actions::newObjectPlacement (halves, scale);
        CHECK (place.startTime > 0.5f && juce::approximatelyEqual (place.endTime, 0.96f));

        for (const auto& object : halves.objects)
            CHECK (! overlaps (place, object, scale));
    }

    // On the default drawing: the free bands first; once everything is taken, never exactly on
    // top of another object.
    ArtDocument document = ArtDocument::createDefault();

    for (int i = 0; i < 30; ++i)
    {
        const auto place = ui::actions::newObjectPlacement (document, scale);
        const auto r = objectRectNorm (place, scale);
        CHECK (r.getX() >= 0.0f && r.getRight() <= 1.0f && r.getY() >= 0.0f && r.getBottom() <= 1.0f && ! r.isEmpty());

        auto clear = true, distinct = true;

        for (const auto& object : document.objects)
        {
            clear = clear && ! overlaps (place, object, scale);
            distinct = distinct && ! samePlacement (place, object);
        }

        CHECK_MSG (distinct, "placement " << i << " lands exactly on another object");

        if (i < 2)   // the high and low bands are free
            CHECK_MSG (clear, "placement " << i << " overlaps an object");

        auto object = ArtDocument::makeTextObject ("x", place.lowHz, place.highHz);
        object.startTime = place.startTime;
        object.endTime = place.endTime;
        document.objects.push_back (object);
    }

    // On a fresh instance: "+ Add text" leaves the default word free to click.
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();
    auto editor = makeEditor (*processor);
    const auto defaultText = processor->getDocument().objects.front();
    auto* addText = find<juce::Button> (*editor, "add-text");
    auto* canvas = find<ui::CanvasView> (*editor, "canvas");
    CHECK (addText != nullptr && canvas != nullptr);

    if (addText == nullptr || canvas == nullptr)
        return;

    addText->onClick();
    pump (20);
    const auto& objects = processor->getDocument().objects;
    CHECK (objects.size() == 2 && ! overlaps (objects.back(), defaultText, processor->getScale()));

    editor->selectObject (juce::Uuid::null());
    clickOn (*canvas, onCanvas (*canvas, defaultText, processor->getScale()).getCentre());
    pump (20);
    auto* field = find<juce::TextEditor> (*editor, "inspector-text");
    CHECK (field != nullptr && field->getText() == "FreqArt");
}

//==============================================================================
FREQART_TEST (uiLiveViewShowsAWholePass)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();
    auto editor = makeEditor (*processor);
    auto* live = find<ui::LiveView> (*editor, "live-view");
    CHECK (live != nullptr);

    if (live == nullptr)
        return;

    // The default drawing (4 bars at 120 BPM, 8 s) fits, with room to spare.
    CHECK_NEAR (processor->getLengthSeconds(), 8.0, 1.0e-9);
    CHECK_NEAR (live->getVisibleSeconds(), 10.0, 1.0e-9);

    for (int index = 0; index < 8; ++index)
    {
        setParameter (*processor, params::id::length, static_cast<float> (index));
        const auto pass = processor->getLengthSeconds();
        const auto span = live->getVisibleSeconds();
        CHECK (span >= 4.0 && span <= 20.0 && std::abs (span - std::round (span)) < 1.0e-9);
        CHECK_MSG (span >= pass || span >= 20.0, "a pass of " << pass << " s in a view of " << span << " s");
    }
}

//==============================================================================
FREQART_TEST (uiPicturesThatCannotBeReadAreReported)
{
    CHECK (ui::actions::isPictureFile (juce::File ("/x/photo.PNG")));
    CHECK (ui::actions::isPictureFile (juce::File ("/x/photo.jpeg")));
    CHECK (! ui::actions::isPictureFile (juce::File ("/x/notes.txt")));
   #if JUCE_MAC
    // macOS reads these itself.
    CHECK (ui::actions::isPictureFile (juce::File ("/x/IMG_0001.HEIC")));
    CHECK (ui::actions::isPictureFile (juce::File ("/x/scan.tiff")));
    CHECK (ui::actions::isPictureFile (juce::File ("/x/logo.webp")));
   #endif

    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();
    auto editor = makeEditor (*processor);
    auto* canvas = find<ui::CanvasView> (*editor, "canvas");
    auto* notice = findComponent (*editor, "notice");
    CHECK (canvas != nullptr && notice != nullptr);

    if (canvas == nullptr || notice == nullptr)
        return;

    CHECK (! notice->isVisible());

    const auto broken = juce::File::createTempFile (".png");
    broken.replaceWithText ("not a picture");
    CHECK (! ui::actions::readPicture (broken).isValid());

    const auto count = processor->getDocument().objects.size();
    const auto at = canvas->getDrawingArea().getCentre().toInt();
    CHECK (canvas->isInterestedInFileDrag ({ broken.getFullPathName() }));
    canvas->filesDropped ({ broken.getFullPathName() }, at.x, at.y);
    CHECK (processor->getDocument().objects.size() == count);
    CHECK (notice->isVisible() && notice->getTitle().contains (broken.getFileName()));

    // A picture that reads is added.
    const auto good = juce::File::createTempFile (".png");
    {
        juce::Image image (juce::Image::ARGB, 40, 20, true);
        juce::Graphics (image).fillAll (juce::Colours::white);
        juce::FileOutputStream stream (good);
        CHECK (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream));
    }

    CHECK (ui::actions::readPicture (good).isValid());
    canvas->filesDropped ({ broken.getFullPathName(), good.getFullPathName() }, at.x, at.y);
    CHECK (processor->getDocument().objects.size() == count + 1);

    broken.deleteFile();
    good.deleteFile();
}
