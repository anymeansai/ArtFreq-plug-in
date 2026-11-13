// The editor, driven like a user would (synthetic mouse, wheel and key events delivered straight
// to its components, no window): bounces, undo steps of wheel and arrow-key gestures, clicking
// and placing objects, the layer list, picking overlapping objects, the live view's span,
// pictures that cannot be read.
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
#include "ui/LayerList.h"
#include "ui/LiveView.h"
#include "ui/ShellExport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

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

void clickOn (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {})
{
    c.mouseDown (mouseEventAt (c, at, at, mods.withFlags (juce::ModifierKeys::leftButtonModifier)));
    c.mouseUp (mouseEventAt (c, at, at, mods));
}

juce::Point<float> centreOf (juce::Rectangle<int> r)
{
    return r.toFloat().getCentre();
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

/** Runs the message loop (the editor's timer) until `done` or the timeout; true if `done`. */
template <typename Predicate>
bool pumpUntil (Predicate&& done, int timeoutMilliseconds)
{
    const auto start = std::chrono::steady_clock::now();

    while (! done())
    {
        if (secondsSince (start) * 1000.0 > timeoutMilliseconds)
            return false;

        pump (10);
    }

    return true;
}

/** The left channel of a WAV file (empty if it cannot be read). */
std::vector<float> readWav (const juce::File& file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    juce::AudioBuffer<float> audio (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
    reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    return { audio.getReadPointer (0), audio.getReadPointer (0) + audio.getNumSamples() };
}

/** One pass of the processor's current plan, rendered here as the export panel would. */
std::vector<float> renderCurrentPlan (FreqArtProcessor& processor)
{
    const auto plan = processor.getCurrentPlan();

    if (plan == nullptr)
        return {};

    ExportSettings settings;
    settings.sampleRate = sampleRate;
    settings.seconds = processor.getLengthSeconds();
    settings.gain = processor.getOutputGain();

    const auto file = juce::File::createTempFile (".wav");
    juce::String error;
    const auto ok = renderPlanToWav (*plan, settings, file, error);
    auto audio = ok ? readWav (file) : std::vector<float>();
    file.deleteFile();
    return audio;
}

double largestDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size() || a.empty())
        return std::numeric_limits<double>::infinity();

    double largest = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
        largest = std::max (largest, static_cast<double> (std::abs (a[i] - b[i])));

    return largest;
}

double rmsOf (const std::vector<float>& x)
{
    double sum = 0.0;

    for (const auto v : x)
        sum += static_cast<double> (v) * static_cast<double> (v);

    return x.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (x.size()));
}

/** A fresh instance whose drawing is one word, with its editor open (and visible, as in a
    host: the export follows the drawing only while it can be seen). */
struct ExportRig
{
    explicit ExportRig (const juce::String& word)
        : processor (makeProcessor())
    {
        ArtDocument document;
        document.objects.push_back (ArtDocument::makeTextObject (word, 300.0f, 3000.0f));
        textId = document.objects.front().id;
        processor->setDocument (document);
        processor->rebuildPlanIfNeeded();

        editor = makeEditor (*processor);
        editor->setVisible (true);
        exporter = find<ui::shell::ExportPanel> (*editor, "export");
        bounce = find<juce::Button> (*editor, "bounce");
        chip = findComponent (*editor, "export-chip");
        save = find<juce::Button> (*editor, "export-save");
    }

    bool valid() const { return exporter != nullptr && bounce != nullptr && chip != nullptr && save != nullptr; }

    void setText (const juce::String& text)
    {
        processor->editDocument ([this, text] (ArtDocument& d) { d.find (textId)->text = text; });
    }

    bool chipShows (const juce::String& text) const   { return chip->isVisible() && chip->getTitle() == text; }

    std::unique_ptr<FreqArtProcessor> processor;
    std::unique_ptr<FreqArtEditor> editor;
    juce::Uuid textId;
    ui::shell::ExportPanel* exporter = nullptr;
    juce::Button* bounce = nullptr;
    juce::Component* chip = nullptr;
    juce::Button* save = nullptr;
};

const juce::String updatingText = "Updating" + juce::String::fromUTF8 ("\xe2\x80\xa6");

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
    // Both named after the default drawing's text ("FreqArt") and the time (in the same second
    // the second one gets a number).
    CHECK (fileA.getParentDirectory() == ui::shell::ExportPanel::getBounceFolder() && fileA.getFileName().startsWith ("FreqArt "));
    CHECK (fileB.getFileName().startsWith ("FreqArt "));
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

    const auto bounces = [] { return ui::shell::ExportPanel::getBounceFolder().getNumberOfChildFiles (juce::File::findFiles, "FreqArt*.wav"); };
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

FREQART_TEST (uiExportNeedsOneClick)
{
    // Nothing renders unasked: before the first "Bounce WAV", changes start no render.
    const juce::ScopedJuceInitialiser_GUI gui;
    ExportRig rig ("HELLO");
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    rig.setText ("WORLD");
    setParameter (*rig.processor, params::id::level, -12.0f);
    pump (2 * ui::shell::ExportPanel::updateDelayMilliseconds);
    CHECK (! rig.exporter->isFollowing() && ! rig.exporter->isBouncing());
    CHECK (rig.exporter->getBounceFile() == juce::File() && rig.bounce->isVisible() && ! rig.chip->isVisible());
}

FREQART_TEST (uiExportAlwaysHandsOutWhatIsDrawn)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ExportRig rig ("HELLO");
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    auto& exporter = *rig.exporter;
    auto& processor = *rig.processor;

    // The first bounce, asked for with the button.
    rig.bounce->onClick();
    CHECK (exporter.isBouncing() && exporter.isFollowing());
    CHECK (exporter.waitForBounce (60000) && exporter.hasCurrentFile());
    const auto first = exporter.getBounceFile();
    const auto firstAudio = readWav (first);
    CHECK (first.getFileName().startsWith ("FreqArt HELLO ") && first.getFileName().endsWith (".wav"));
    CHECK (rig.chipShows (first.getFileName()) && rig.save->isEnabled() && exporter.getFileToHandOut() == first);
    CHECK (largestDifference (firstAudio, renderCurrentPlan (processor)) < 1.0e-6);

    // An edit: from that moment on the old file is not handed out, even before the processor
    // has rebuilt the plan (0.1.1 kept offering it until then, and a drag in that moment took
    // the old drawing).
    rig.setText ("WORLD");
    CHECK (processor.isPlanPending());
    CHECK (exporter.getFileToHandOut() == juce::File() && ! exporter.hasCurrentFile());
    CHECK (rig.chipShows (updatingText) && ! rig.save->isEnabled());

    // The new render waits for the drawing to settle, then comes by itself.
    processor.rebuildPlanIfNeeded();
    exporter.refresh();
    CHECK (! exporter.isBouncing() && rig.chipShows (updatingText));
    CHECK (pumpUntil ([&] { return exporter.isBouncing() || exporter.hasCurrentFile(); }, 5000));
    CHECK (pumpUntil ([&] { return exporter.hasCurrentFile(); }, 60000));

    const auto second = exporter.getBounceFile();
    const auto secondAudio = readWav (second);
    CHECK (second != first && second.getFileName().startsWith ("FreqArt WORLD "));
    CHECK (rig.chipShows (second.getFileName()) && exporter.getFileToHandOut() == second);
    CHECK (! first.exists());   // superseded and never dragged out: gone
    CHECK_MSG (largestDifference (secondAudio, renderCurrentPlan (processor)) < 1.0e-6, "the new file is the new drawing");
    CHECK_MSG (largestDifference (secondAudio, firstAudio) > 0.01, "... and not the old one");

    // The level: a new file, as loud as the new level.
    setParameter (processor, params::id::level, -18.0f);
    CHECK (! exporter.hasCurrentFile());
    CHECK (pumpUntil ([&] { return exporter.hasCurrentFile(); }, 60000));
    const auto third = exporter.getBounceFile();
    const auto thirdAudio = readWav (third);
    CHECK (third != second && ! second.exists());
    CHECK_NEAR (rmsOf (thirdAudio) / rmsOf (secondAudio), juce::Decibels::decibelsToGain (-12.0), 0.01);
    CHECK (largestDifference (thirdAudio, renderCurrentPlan (processor)) < 1.0e-6);

    // The tempo (and so the length of a pass): a new file of the new length.
    setParameter (processor, params::id::tempoSource, 1.0f);
    setParameter (processor, params::id::bpm, 150.0f);
    CHECK (pumpUntil ([&] { return exporter.hasCurrentFile() && exporter.getBounceFile() != third; }, 60000));
    const auto fourth = exporter.getBounceFile();
    CHECK (readWav (fourth).size() == static_cast<size_t> (std::llround (processor.getLengthSeconds() * sampleRate)));
    CHECK (std::abs (processor.getLengthSeconds() - 6.4) < 1.0e-9);   // 4 bars at 150 BPM

    // The length: likewise.
    setParameter (processor, params::id::length, 3.0f);   // 2 bars
    CHECK (pumpUntil ([&] { return exporter.hasCurrentFile() && exporter.getBounceFile() != fourth; }, 60000));
    CHECK (readWav (exporter.getBounceFile()).size() == static_cast<size_t> (std::llround (3.2 * sampleRate)));

    // An empty drawing: nothing to hand out until something is drawn again.
    const auto fifth = exporter.getBounceFile();
    rig.setText ("");
    CHECK (pumpUntil ([&] { return rig.chipShows ("Nothing drawn"); }, 5000));
    CHECK (exporter.getFileToHandOut() == juce::File() && ! exporter.isBouncing());
    rig.setText ("AGAIN");
    CHECK (pumpUntil ([&] { return exporter.hasCurrentFile(); }, 60000));
    CHECK (exporter.getBounceFile().getFileName().startsWith ("FreqArt AGAIN ") && ! fifth.exists());

    // Closing takes the last file with it.
    const auto last = exporter.getBounceFile();
    rig.editor.reset();
    CHECK (! last.exists());
}

FREQART_TEST (uiExportStopsRendersThatAreNoLongerWanted)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ExportRig rig ("LONG");
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    auto& exporter = *rig.exporter;
    auto& processor = *rig.processor;

    // Minutes of a solid drawing: renders that take a while.
    setParameter (processor, params::id::tempoSource, 1.0f);
    setParameter (processor, params::id::bpm, 20.0f);
    setParameter (processor, params::id::length, 7.0f);   // 32 bars: 384 s
    setParameter (processor, params::id::density, 512.0f);
    processor.editDocument ([] (ArtDocument& document)
    {
        document.paint.assign (static_cast<size_t> (ArtDocument::paintWidth * ArtDocument::paintHeight), 200);
    });

    rig.bounce->onClick();
    const auto firstRender = exporter.getRenderingFile();
    CHECK (exporter.isBouncing() && firstRender.existsAsFile());   // the name is reserved at once
    pump (50);

    // A change while it renders: it is stopped at once and leaves nothing behind.
    setParameter (processor, params::id::level, -10.0f);
    exporter.refresh();
    CHECK (! exporter.isBouncing());
    CHECK (pumpUntil ([&] { return ! firstRender.exists(); }, 2000));

    // ... and the new one starts once the change has settled.
    CHECK (pumpUntil ([&] { return exporter.isBouncing(); }, 5000));
    // (In the same second it may take the name the stopped one had: that file never existed.)
    const auto secondRender = exporter.getRenderingFile();
    CHECK (secondRender.existsAsFile());
    pump (50);

    // Closing during it does not wait for it, and leaves nothing behind.
    const auto closeStart = std::chrono::steady_clock::now();
    rig.editor.reset();
    const auto closing = secondsSince (closeStart);
    CHECK_MSG (closing < 0.5, "closing took " << closing << " s");
    CHECK (! secondRender.exists() && ! firstRender.exists());
}

FREQART_TEST (uiExportSavesTheCurrentFile)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    ExportRig rig ("SAVE");
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    auto& exporter = *rig.exporter;
    const auto folder = juce::File::createTempFile ("");
    folder.createDirectory();

    rig.bounce->onClick();
    CHECK (exporter.waitForBounce (60000) && exporter.hasCurrentFile());

    // While the file is current: copied at once.
    const auto now = folder.getChildFile ("now.wav");
    exporter.saveTo (now);
    CHECK (now.existsAsFile() && now.hasIdenticalContentTo (exporter.getBounceFile()));

    // Chosen while an update is on its way: the update is what gets saved.
    rig.setText ("SAVED LATER");
    const auto later = folder.getChildFile ("later.wav");
    exporter.saveTo (later);
    CHECK (! later.exists());
    CHECK (pumpUntil ([&] { return exporter.hasCurrentFile(); }, 60000));
    CHECK (later.existsAsFile() && later.hasIdenticalContentTo (exporter.getBounceFile()));
    CHECK (! later.hasIdenticalContentTo (now) && exporter.getLastError().isEmpty());

    folder.deleteRecursively();
}

FREQART_TEST (uiExportWaitsWhileHidden)
{
    // Renders start by themselves only while the export can be seen.
    const juce::ScopedJuceInitialiser_GUI gui;
    ExportRig rig ("HIDDEN");
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    rig.bounce->onClick();
    CHECK (rig.exporter->waitForBounce (60000) && rig.exporter->hasCurrentFile());

    rig.editor->setVisible (false);
    rig.setText ("CHANGED");
    pump (2 * ui::shell::ExportPanel::updateDelayMilliseconds);
    CHECK (! rig.exporter->isBouncing() && ! rig.exporter->hasCurrentFile());

    rig.editor->setVisible (true);
    CHECK (pumpUntil ([&] { return rig.exporter->hasCurrentFile(); }, 60000));
    CHECK (rig.exporter->getBounceFile().getFileName().startsWith ("FreqArt CHANGED "));
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

FREQART_TEST (uiLayerListSelectsHidesReordersAndDeletes)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();

    // Back to front: A, B, C. The list shows them front first: C, B, A.
    ArtDocument document;
    document.objects.push_back (textIn (Band::low, 0.10f, 0.50f, "A"));
    document.objects.push_back (textIn (Band::mid, 0.10f, 0.50f, "\n  B first line\nB second line"));
    document.objects.push_back (textIn (Band::high, 0.10f, 0.50f, "C"));
    processor->setDocument (document);
    const auto a = document.objects[0], b = document.objects[1], c = document.objects[2];

    auto editor = makeEditor (*processor);
    auto* list = find<ui::LayerList> (*editor, "layer-list");
    CHECK (list != nullptr && list->isVisible());

    if (list == nullptr)
        return;

    const auto order = [&]
    {
        juce::String result;

        for (const auto& o : processor->getDocument().objects)
            result << o.text.trim().substring (0, 1);

        return result;
    };
    const auto object = [&] (const ArtObject& o) { return processor->getDocument().find (o.id); };

    CHECK (list->getNumObjectRows() == 3);
    CHECK (ui::actions::layerName (b) == "B first line");

    // Clicking a row selects its object and keeps the list; it is no undo step.
    clickOn (*list, centreOf (list->getRowBounds (1)));
    pump (20);
    CHECK (editor->getSelectedObject() == b.id && editor->isLayerListShown() && list->isVisible());
    CHECK (processor->undo() && processor->getDocument().objects.size() == 1);   // straight back to the default drawing
    CHECK (processor->redo() && order() == "ABC");
    pump (20);

    // Up and Down walk the list (front first) and stop at its ends.
    list->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    CHECK (editor->getSelectedObject() == a.id);
    list->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    CHECK (editor->getSelectedObject() == a.id);

    for (int i = 0; i < 3; ++i)
        list->keyPressed (juce::KeyPress (juce::KeyPress::upKey));

    CHECK (editor->getSelectedObject() == c.id && editor->isLayerListShown());

    // Return opens the settings; "< Layers" goes back to the list, the selection kept.
    list->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    pump (20);
    auto* field = find<juce::TextEditor> (*editor, "inspector-text");
    CHECK (! editor->isLayerListShown() && field != nullptr && field->isVisible() && field->getText() == "C");
    CHECK (! list->isVisible());

    if (auto* back = find<juce::Button> (*editor, "inspector-layers"))
    {
        CHECK (back->isVisible());
        back->onClick();
        pump (20);
    }

    CHECK (editor->isLayerListShown() && list->isVisible() && editor->getSelectedObject() == c.id);

    // A double-click on a row, or its chevron, opens the settings too.
    list->mouseDown (mouseEventAt (*list, centreOf (list->getRowBounds (2)), centreOf (list->getRowBounds (2)),
                                   juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
    list->mouseUp (mouseEventAt (*list, centreOf (list->getRowBounds (2)), centreOf (list->getRowBounds (2)), {}));
    juce::MouseEvent second (juce::Desktop::getInstance().getMainMouseSource(), centreOf (list->getRowBounds (2)),
                             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), juce::MouseInputSource::defaultPressure,
                             0.0f, 0.0f, 0.0f, 0.0f, list, list, juce::Time::getCurrentTime(), centreOf (list->getRowBounds (2)),
                             juce::Time::getCurrentTime(), 2, false);
    list->mouseDown (second);
    pump (20);
    CHECK (editor->getSelectedObject() == a.id && ! editor->isLayerListShown());
    editor->selectObject (juce::Uuid::null());
    pump (20);
    clickOn (*list, centreOf (list->getSettingsBounds (0)));
    pump (20);
    CHECK (editor->getSelectedObject() == c.id && ! editor->isLayerListShown());
    editor->selectObject (juce::Uuid::null());
    pump (20);

    // The eye hides an object (one undo step) and shows it again.
    clickOn (*list, centreOf (list->getEyeBounds (1)));
    CHECK (! object (b)->visible && object (a)->visible && object (c)->visible);
    CHECK (processor->undo() && object (b)->visible);
    CHECK (processor->redo() && ! object (b)->visible);
    pump (20);
    clickOn (*list, centreOf (list->getEyeBounds (1)));
    CHECK (object (b)->visible);
    CHECK (processor->undo() && ! object (b)->visible && processor->undo() && object (b)->visible);
    pump (20);

    // Dragging a row reorders (one undo step): A from the back to the front.
    dragOn (*list, centreOf (list->getRowBounds (2)), list->getRowBounds (0).toFloat().getTopLeft() + juce::Point<float> (40.0f, 3.0f));
    CHECK_MSG (order() == "BCA", "order " << order());
    CHECK (processor->undo() && order() == "ABC");
    pump (20);

    // ... and C from the front to between A and B.
    dragOn (*list, centreOf (list->getRowBounds (0)), list->getRowBounds (2).toFloat().getTopLeft() + juce::Point<float> (40.0f, 2.0f));
    CHECK_MSG (order() == "ACB", "order " << order());
    CHECK (processor->undo() && order() == "ABC");
    pump (20);

    // A drag that lands where it started changes nothing (the step undone above is still there
    // to redo: no edit was made).
    dragOn (*list, centreOf (list->getRowBounds (1)), centreOf (list->getRowBounds (1)) + juce::Point<float> (0.0f, 8.0f));
    CHECK (order() == "ABC" && processor->canRedo());

    // The delete button removes the row's object (one undo step, back in its place).
    clickOn (*list, centreOf (list->getDeleteBounds (0)));
    CHECK (order() == "AB");
    CHECK (processor->undo() && order() == "ABC");
    pump (20);

    // Delete with the list focused removes the selection and selects its neighbour.
    clickOn (*list, centreOf (list->getRowBounds (1)));
    list->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    pump (20);
    CHECK (order() == "AC" && editor->getSelectedObject() == a.id && editor->isLayerListShown());
    CHECK (processor->undo() && order() == "ABC");
    pump (20);

    // Esc deselects.
    clickOn (*list, centreOf (list->getRowBounds (1)));
    list->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK (editor->getSelectedObject().isNull());

    // The paint layer's row: the eye hides the paint, Clear empties it; one undo step each.
    processor->editDocument ([] (ArtDocument& d)
    {
        d.paintStroke ({ { 100.0f, 100.0f }, { 400.0f, 120.0f }, 10.0f, 10.0f, 0.0f, 1.0f, false });
    });
    pump (20);
    clickOn (*list, centreOf (list->getPaintEyeBounds()));
    CHECK (! processor->getDocument().paintVisible && processor->getDocument().hasPaint());
    clickOn (*list, centreOf (list->getPaintClearBounds()));
    CHECK (! processor->getDocument().hasPaint() && processor->getDocument().paintVisible && order() == "ABC");
    CHECK (processor->undo() && processor->getDocument().hasPaint() && ! processor->getDocument().paintVisible);
    CHECK (processor->undo() && processor->getDocument().paintVisible);
}

FREQART_TEST (uiLayerListScrollsToTheSelection)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();

    ArtDocument document;

    for (int i = 0; i < 24; ++i)
        document.objects.push_back (textIn (Band::mid, 0.01f * static_cast<float> (i), 0.5f, "Layer " + juce::String (i)));

    processor->setDocument (document);
    auto editor = makeEditor (*processor);
    auto* list = find<ui::LayerList> (*editor, "layer-list");
    CHECK (list != nullptr);

    if (list == nullptr)
        return;

    const auto inView = [&] (juce::Rectangle<int> r) { return list->getLocalBounds().contains (r); };

    // More rows than room: the list scrolls, starting at the top.
    CHECK (list->getContentHeight() > list->getHeight());
    CHECK (inView (list->getRowBounds (0)) && ! inView (list->getRowBounds (23)) && ! inView (list->getPaintRowBounds()));

    // Up with nothing selected picks the last row (the back-most object), and brings it into view.
    list->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
    pump (20);
    CHECK (editor->getSelectedObject() == document.objects.front().id);
    CHECK (inView (list->getRowBounds (23)) && ! inView (list->getRowBounds (0)));

    // The wheel scrolls back up, and down to the paint layer.
    wheel (*list, 60, 0.1f);
    CHECK (inView (list->getRowBounds (0)));
    wheel (*list, 60, -0.1f);
    CHECK (inView (list->getPaintRowBounds()));

    // A selection made on the canvas (or a new object) is scrolled to as well.
    editor->selectObject (document.objects.back().id);
    pump (20);
    CHECK (inView (list->getRowBounds (0)));
}

FREQART_TEST (uiClickingAgainPicksTheObjectUnderneath)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    auto processor = makeProcessor();

    // Back to front: A, B, and C, hidden, over both.
    ArtDocument document;
    document.objects.push_back (textIn (Band::mid, 0.10f, 0.60f, "A"));
    document.objects.push_back (textIn (Band::mid, 0.40f, 0.90f, "B"));
    document.objects.push_back (textIn (Band::mid, 0.30f, 0.70f, "C"));
    document.objects.back().visible = false;
    processor->setDocument (document);
    const auto a = document.objects[0], b = document.objects[1], c = document.objects[2];

    auto editor = makeEditor (*processor);
    auto* canvas = find<ui::CanvasView> (*editor, "canvas");
    CHECK (canvas != nullptr);

    if (canvas == nullptr)
        return;

    const auto scale = processor->getScale();
    const auto shared = onCanvas (*canvas, a, scale).getIntersection (onCanvas (*canvas, b, scale)).getCentre();
    const auto selected = [&] { return editor->getSelectedObject(); };

    // The front-most visible object (the hidden one cannot be clicked), then, clicking again on
    // the same spot, the one underneath, and round again.
    clickOn (*canvas, shared);
    CHECK (selected() == b.id);
    clickOn (*canvas, shared);
    CHECK (selected() == a.id);
    clickOn (*canvas, shared);
    CHECK (selected() == b.id);

    // Moving the mouse away in between makes it a fresh click: the selection stays.
    canvas->mouseMove (mouseEventAt (*canvas, shared + juce::Point<float> (0.0f, 40.0f), shared, {}));
    canvas->mouseMove (mouseEventAt (*canvas, shared, shared, {}));
    clickOn (*canvas, shared);
    CHECK (selected() == b.id);

    // Alt (Option) - click picks the next one down at once.
    canvas->mouseMove (mouseEventAt (*canvas, shared + juce::Point<float> (0.0f, 40.0f), shared, {}));
    clickOn (*canvas, shared, juce::ModifierKeys::altModifier);
    CHECK (selected() == a.id);
    clickOn (*canvas, shared, juce::ModifierKeys::altModifier);
    CHECK (selected() == b.id);

    // A press and drag on the selection moves it, and picks nothing else.
    clickOn (*canvas, shared, juce::ModifierKeys::altModifier);
    CHECK (selected() == a.id);
    dragOn (*canvas, shared, shared + juce::Point<float> (-30.0f, 0.0f));
    CHECK (selected() == a.id && processor->getDocument().find (a.id)->startTime < a.startTime);
    CHECK (samePlacement (*processor->getDocument().find (b.id), b));
    CHECK (processor->undo());

    // A hidden object picked in the layer list can be moved on the canvas, where it shows
    // only as an outline.
    if (auto* list = find<ui::LayerList> (*editor, "layer-list"))
    {
        editor->selectObject (juce::Uuid::null());
        pump (20);
        clickOn (*list, centreOf (list->getRowBounds (0)));
        CHECK (selected() == c.id);
        dragOn (*canvas, shared, shared + juce::Point<float> (0.0f, 30.0f));
        CHECK (selected() == c.id && ! samePlacement (*processor->getDocument().find (c.id), c));
        CHECK (! processor->getDocument().find (c.id)->visible);
        CHECK (processor->undo());
    }

    // Right-click selects the object under the mouse and opens its menu.
    editor->selectObject (juce::Uuid::null());
    pump (20);
    const auto right = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
    canvas->mouseDown (mouseEventAt (*canvas, shared, shared, right));
    canvas->mouseUp (mouseEventAt (*canvas, shared, shared, {}));
    CHECK (selected() == b.id);
    CHECK (juce::PopupMenu::dismissAllActiveMenus());
    pump (20);

    // The menu: Bring to front, Send to back (greyed where it would change nothing), Hide or
    // Show, Delete; each one undo step.
    namespace items = ui::actions::menuItems;
    const auto menu = ui::actions::objectMenu (*processor, b.id);
    juce::StringArray names;

    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (! it.getItem().isSeparator)
            names.add (it.getItem().text + (it.getItem().isEnabled ? "" : " (off)"));

    CHECK_MSG (names.joinIntoString (", ") == "Bring to front, Send to back, Hide, Delete", names.joinIntoString (", ").toStdString());

    ui::EditorState state;
    ui::actions::performObjectMenuItem (*processor, state, a.id, items::bringToFront);
    CHECK (processor->getDocument().objects.back().id == a.id);
    CHECK (processor->undo() && processor->getDocument().objects.front().id == a.id);
    ui::actions::performObjectMenuItem (*processor, state, c.id, items::sendToBack);
    CHECK (processor->getDocument().objects.front().id == c.id);
    CHECK (processor->undo() && processor->getDocument().objects.back().id == c.id);
    ui::actions::performObjectMenuItem (*processor, state, c.id, items::show);
    CHECK (processor->getDocument().find (c.id)->visible);
    CHECK (processor->undo() && ! processor->getDocument().find (c.id)->visible);
    state.setSelected (a.id);
    ui::actions::performObjectMenuItem (*processor, state, a.id, items::remove);
    CHECK (processor->getDocument().find (a.id) == nullptr && ! state.hasSelection());
    CHECK (processor->undo() && processor->getDocument().find (a.id) != nullptr);

    const auto hiddenMenu = ui::actions::objectMenu (*processor, c.id);
    auto showsShow = false;

    for (juce::PopupMenu::MenuItemIterator it (hiddenMenu); it.next();)
        showsShow = showsShow || it.getItem().text == "Show";

    CHECK (showsShow);
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
