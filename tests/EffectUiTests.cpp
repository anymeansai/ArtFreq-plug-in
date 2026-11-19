// ArtFreq FX's editor: a small window of its own (720 x 440), not the instrument's with parts
// greyed out. One top bar (Mode, Depth, undo / redo, Live, help, settings), the canvas, one
// bottom bar (the tools, + Text, + Image, Layers, what will happen in words, Length, Repeat).
// What a mode or a tool has no use for is not there: Depth in Draw, the sines' settings outside
// the Draw modes, the brush's settings without the brush. The inspector is a drawer over the
// canvas; Trigger, Start and Tempo are in the settings; there is no Preview and no Export.
// Nothing is greyed, not even in an object's right-click menu. And the canvas shows what the
// analyser will show in each mode.
//
// These need JUCE's GUI classes, so on Linux run them under a virtual display (xvfb-run -a).
//
// The editor follows the parameters, the drawing and the editor's own state on its timers and
// change messages, which a slow machine (the macOS runner's Intel half, under Rosetta) runs much
// later than a fast one: a check of what the editor shows after a change waits for it
// (CHECK_EVENTUALLY, WaitFor.h), never for a fixed time.

#include "ProcessorTestUtils.h"
#include "TestFramework.h"
#include "WaitFor.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "ui/Actions.h"
#include "ui/CanvasPlanImage.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/EffectPreview.h"
#include "ui/EffectShell.h"
#include "ui/Inspector.h"
#include "ui/ShellControls.h"
#include "ui/ShellPanels.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace artfreq;
using testutil::setParameter;

namespace
{

std::unique_ptr<ArtFreqProcessor> makeProcessor (ArtFreqProcessor::Variant variant)
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

/** The colour-map index of a pixel of an image drawn with the spectrogram colours. */
int levelOf (const juce::Image& image, int x, int y)
{
    const auto pixel = image.getPixelAt (x, y);
    const auto& table = ui::theme::spectrumTable();

    for (int i = 0; i < 256; ++i)
    {
        const auto c = juce::Colour (table[static_cast<size_t> (i)].getUnpremultiplied());

        if (c.getRed() == pixel.getRed() && c.getGreen() == pixel.getGreen() && c.getBlue() == pixel.getBlue())
            return i;
    }

    return -1;
}

/** Whether a component is shown: visible, and so is everything it is in (the editor itself is
    on no screen in these tests). */
bool isShown (const juce::Component* component, const juce::Component& root)
{
    for (auto* c = component; c != nullptr && c != &root; c = c->getParentComponent())
        if (! c->isVisible())
            return false;

    return component != nullptr;
}

bool isShown (juce::Component& root, const juce::String& id)
{
    return isShown (findComponent (root, id), root);
}

/** The ids of everything that is shown but disabled. */
void collectDisabled (juce::Component& component, juce::StringArray& ids)
{
    if (! component.isVisible())
        return;

    if (! component.isEnabled())
    {
        ids.add (component.getComponentID().isNotEmpty() ? component.getComponentID() : component.getName());
        return;
    }

    for (auto* child : component.getChildren())
        collectDisabled (*child, ids);
}

/** The visible children of a component: none overlaps another, all lie inside it. */
bool childrenAreApart (juce::Component& parent)
{
    std::vector<juce::Rectangle<int>> shown;

    for (auto* child : parent.getChildren())
        if (child->isVisible() && ! child->getBounds().isEmpty())
            shown.push_back (child->getBounds());

    for (size_t i = 0; i < shown.size(); ++i)
    {
        if (! parent.getLocalBounds().contains (shown[i]))
            return false;

        for (size_t j = i + 1; j < shown.size(); ++j)
            if (shown[i].intersects (shown[j]))
                return false;
    }

    return true;
}

juce::String hintOf (ArtFreqEditor& editor)
{
    auto* bar = dynamic_cast<ui::shell::EffectBottomBar*> (findComponent (editor, "footer"));
    return bar != nullptr ? bar->getShownHint() : juce::String ("(no bottom bar)");
}

} // namespace

ARTFREQ_TEST (uiEffectHasASmallWindowOfItsOwn)
{
    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);
    auto editor = makeEditor (*effect);
    pump (60);

    // 720 x 440 at 100 %, against the instrument's 1120 x 720.
    CHECK (editor->getWidth() == 720 && editor->getHeight() == 440);
    CHECK (editor->getBaseWidth() == ArtFreqEditor::effectBaseWidth && editor->getBaseHeight() == ArtFreqEditor::effectBaseHeight);

    auto* top = findComponent (*editor, "header");
    auto* bottom = findComponent (*editor, "footer");
    auto* controls = findComponent (*editor, "effect-controls");
    auto* mode = dynamic_cast<ui::SegmentedControl*> (findComponent (*editor, "mode"));
    auto* depth = findComponent (*editor, "depth");
    auto* canvas = findComponent (*editor, "canvas");

    CHECK (top != nullptr && bottom != nullptr && controls != nullptr && mode != nullptr && depth != nullptr && canvas != nullptr);

    if (top == nullptr || bottom == nullptr || controls == nullptr || mode == nullptr || depth == nullptr || canvas == nullptr)
        return;

    // The bars' own parts: Mode and Depth in the top bar with Live, help and settings; the tools,
    // + Text, + Image, Layers, Length and Repeat in the bottom bar. Nothing overlaps.
    CHECK (controls->getParentComponent() == top && dynamic_cast<ui::shell::EffectTopBar*> (top) != nullptr);
    CHECK (dynamic_cast<ui::shell::EffectBottomBar*> (bottom) != nullptr);
    CHECK (childrenAreApart (*top) && childrenAreApart (*controls) && childrenAreApart (*bottom));

    for (const auto* id : { "mode", "depth", "live", "help", "settings", "tool-select", "tool-brush", "tool-erase", "add-text",
                            "add-image", "layers", "length", "repeat" })
        CHECK (isShown (*editor, id));

    // The canvas has the whole middle: no panel beside it.
    CHECK_GREATER (canvas->getWidth(), 700);
    CHECK (canvas->getY() >= top->getBottom() && canvas->getBottom() <= bottom->getY());
    CHECK_GREATER (canvas->getHeight(), 330);

    // What the instrument's window has and this one does not: Preview, Export, Clear, the toolbar,
    // Trigger and Tempo in a footer, an inspector beside the canvas.
    for (const auto* id : { "preview", "export", "bounce", "clear", "toolbar", "view-switch", "trigger", "tempo-source", "bpm",
                            "start-choice", "inspector", "sound-strip", "brush-strip", "undo", "redo" })
        CHECK (! isShown (*editor, id));

    // Choosing a mode sets the parameter; the parameter (automation) moves the control.
    CHECK (mode->getSelectedIndex() == 0);
    mode->setSelectedIndex (2, juce::sendNotificationSync);
    CHECK (effect->getEffectMode() == EffectMode::stencil);
    setParameter (*effect, params::id::mode, 3.0f);
    CHECK_EVENTUALLY (mode->getSelectedIndex() == 3);

    // The instrument's window is as it was, and has none of this.
    auto instrument = makeProcessor (ArtFreqProcessor::Variant::instrument);
    auto instrumentEditor = makeEditor (*instrument);
    pump (60);
    CHECK (instrumentEditor->getWidth() == ArtFreqEditor::baseWidth && instrumentEditor->getHeight() == ArtFreqEditor::baseHeight);
    CHECK (findComponent (*instrumentEditor, "effect-controls") == nullptr);
    CHECK (findComponent (*instrumentEditor, "mode") == nullptr);
    CHECK (findComponent (*instrumentEditor, "sound-strip") == nullptr);
    CHECK (isShown (*instrumentEditor, "preview") && isShown (*instrumentEditor, "export") && isShown (*instrumentEditor, "inspector"));
    CHECK (! instrumentEditor->isDrawerOpen());
}

ARTFREQ_TEST (uiEffectShowsOnlyWhatAppliesAndGreysNothing)
{
    // Depth is there in the modes that carve; the sines' Level, Density, Contrast and Lows in the
    // modes that play them; the brush's settings while the brush or the eraser is the tool. What
    // does not apply is not shown, and nothing that is shown is disabled.
    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);
    auto editor = makeEditor (*effect);
    pump (80);

    auto* canvas = findComponent (*editor, "canvas");

    if (canvas == nullptr)
    {
        CHECK (false);
        return;
    }

    const auto nothingGreyed = [&]
    {
        juce::StringArray ids;
        collectDisabled (*editor, ids);
        return ids.isEmpty();
    };

    const auto fullHeight = canvas->getHeight();

    for (const auto& [mode, carves, sines] : { std::tuple<float, bool, bool> { 0.0f, true, false }, { 1.0f, true, true },
                                               { 2.0f, true, false }, { 3.0f, false, true } })
    {
        // The editor follows the mode on its timer: wait until it shows the mode's controls (each
        // mode shows something the one before did not), then check them one by one.
        const auto shows = [&, carves = carves, sines = sines]
        {
            auto all = isShown (*editor, "depth") == carves && isShown (*editor, "sound-strip") == sines
                    && canvas->getHeight() == fullHeight - (sines ? ui::shell::effectMetrics::stripHeight : 0);

            for (const auto* id : { "level", "density", "contrast", "lows" })
                all = all && isShown (*editor, id) == sines;

            return all;
        };

        setParameter (*effect, params::id::mode, mode);
        CHECK_EVENTUALLY (shows());

        CHECK (isShown (*editor, "depth") == carves);
        CHECK (isShown (*editor, "sound-strip") == sines);

        for (const auto* id : { "level", "density", "contrast", "lows" })
            CHECK (isShown (*editor, id) == sines);

        // The canvas makes room for the strip rather than lying under it.
        CHECK (canvas->getHeight() == fullHeight - (sines ? ui::shell::effectMetrics::stripHeight : 0));
        CHECK (nothingGreyed());

        if (auto* strip = findComponent (*editor, "sound-strip"); strip != nullptr && sines)
            CHECK (childrenAreApart (*strip) && strip->getBottom() <= canvas->getY());
    }

    setParameter (*effect, params::id::mode, 0.0f);
    CHECK_EVENTUALLY (! isShown (*editor, "sound-strip") && canvas->getHeight() == fullHeight);

    // The brush.
    auto* brushTool = dynamic_cast<juce::Button*> (findComponent (*editor, "tool-brush"));
    auto* selectTool = dynamic_cast<juce::Button*> (findComponent (*editor, "tool-select"));
    auto* strip = findComponent (*editor, "brush-strip");

    if (brushTool == nullptr || selectTool == nullptr || strip == nullptr)
    {
        CHECK (false);
        return;
    }

    CHECK (! strip->isVisible());
    brushTool->onClick();
    CHECK_EVENTUALLY (strip->isVisible() && isShown (*editor, "brush-size") && isShown (*editor, "brush-softness") && isShown (*editor, "brush-strength"));
    // It lies over the canvas's time axis, clear of the drawing: choosing a tool never resizes
    // the canvas, so the drawing does not move under the brush.
    auto* canvasView = dynamic_cast<ui::CanvasView*> (canvas);
    CHECK (childrenAreApart (*strip) && canvasView != nullptr
           && static_cast<float> (strip->getY()) >= static_cast<float> (canvas->getY()) + canvasView->getDrawingArea().getBottom());
    CHECK (canvas->getHeight() == fullHeight);
    CHECK (nothingGreyed());

    // The live view has no use for the tools or the brush's settings: they are not there.
    editor->showLiveView (true);
    CHECK_EVENTUALLY (! strip->isVisible() && ! isShown (*editor, "tool-brush") && ! isShown (*editor, "tool-select") && isShown (*editor, "add-text"));
    CHECK (nothingGreyed());
    editor->showLiveView (false);
    CHECK_EVENTUALLY (strip->isVisible() && isShown (*editor, "tool-brush"));

    selectTool->onClick();
    CHECK_EVENTUALLY (! strip->isVisible() && canvas->getHeight() == fullHeight);

    // Undo and redo are there while they have something to do.
    CHECK (! isShown (*editor, "undo") && ! isShown (*editor, "redo"));
    effect->editDocument ([] (ArtDocument& document) { document.objects.clear(); });
    CHECK_EVENTUALLY (isShown (*editor, "undo") && ! isShown (*editor, "redo"));
    effect->undo();
    CHECK_EVENTUALLY (! isShown (*editor, "undo") && isShown (*editor, "redo"));
    CHECK (nothingGreyed());
}

ARTFREQ_TEST (uiEffectObjectMenuGreysNothing)
{
    // The instrument's right-click menu greys Bring to front and Send to back where they would
    // change nothing. ArtFreq FX greys nothing, there either: such an item is not in the menu.
    const auto itemsOf = [] (ArtFreqProcessor& processor, const juce::Uuid& id)
    {
        const auto menu = ui::actions::objectMenu (processor, id);   // (the iterator only refers to it)
        juce::StringArray names;

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            names.add (it.getItem().isSeparator ? juce::String ("-") : it.getItem().text + (it.getItem().isEnabled ? "" : " (off)"));

        return names.joinIntoString (", ");
    };

    const auto drawing = [] (int count)
    {
        ArtDocument document;

        for (int i = 0; i < count; ++i)
            document.objects.push_back (ArtDocument::makeTextObject ("T" + juce::String (i), 300.0f, 3000.0f));

        return document;
    };

    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);
    auto instrument = makeProcessor (ArtFreqProcessor::Variant::instrument);

    // One object (as a new ArtFreq FX has): nothing to reorder, so the menu is Hide and Delete.
    effect->setDocument (drawing (1));
    instrument->setDocument (effect->getDocument());
    const auto only = effect->getDocument().objects.front().id;
    CHECK_MSG (itemsOf (*effect, only) == "Hide, Delete", itemsOf (*effect, only).toStdString());
    CHECK_MSG (itemsOf (*instrument, only) == "Bring to front (off), Send to back (off), -, Hide, Delete",
               itemsOf (*instrument, only).toStdString());

    // Three: the one at the back can only come forward, the one in front only go back, the one
    // between them both.
    effect->setDocument (drawing (3));
    const auto& objects = effect->getDocument().objects;
    CHECK_MSG (itemsOf (*effect, objects[0].id) == "Bring to front, -, Hide, Delete", itemsOf (*effect, objects[0].id).toStdString());
    CHECK_MSG (itemsOf (*effect, objects[1].id) == "Bring to front, Send to back, -, Hide, Delete",
               itemsOf (*effect, objects[1].id).toStdString());
    CHECK_MSG (itemsOf (*effect, objects[2].id) == "Send to back, -, Hide, Delete", itemsOf (*effect, objects[2].id).toStdString());

    // What is there does what it says.
    ui::EditorState state;
    const auto back = objects[0].id;
    ui::actions::performObjectMenuItem (*effect, state, back, ui::actions::menuItems::bringToFront);
    CHECK (effect->getDocument().objects.back().id == back);
    CHECK_MSG (itemsOf (*effect, back) == "Send to back, -, Hide, Delete", itemsOf (*effect, back).toStdString());
}

ARTFREQ_TEST (uiEffectBottomBarSaysWhatWillHappen)
{
    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);
    auto editor = makeEditor (*effect);
    pump (80);

    const auto says = [&] (const juce::String& expected)
    {
        pump (80);
        return hintOf (*editor) == expected;
    };

    // The defaults: press play and it carves, wherever the DAW starts.
    CHECK (says ("Carves once when you press play"));

    // The verb is the mode's.
    setParameter (*effect, params::id::mode, 1.0f);
    CHECK (says ("Draws once when you press play"));
    setParameter (*effect, params::id::mode, 2.0f);
    CHECK (says ("Stencils once when you press play"));
    setParameter (*effect, params::id::mode, 3.0f);
    CHECK (says ("Draws once when you press play"));
    setParameter (*effect, params::id::mode, 0.0f);

    // While the DAW plays over a silent stretch of the track, the pass waits for its audio, and
    // the line says so. Under a DAW loop shorter than the drawing, it says how much of the drawing
    // each round shows (the same part every round).
    {
        testutil::FakePlayHead head;
        effect->setPlayHead (&head);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        const auto block = [&] (bool playing, bool sound, double loopEnd)
        {
            head.set (playing, 120.0, 32.0, 16.0);

            if (loopEnd > 0.0)
            {
                head.info.setIsLooping (true);
                head.info.setLoopPoints (juce::AudioPlayHead::LoopPoints { 32.0, loopEnd });
            }

            buffer.clear();

            if (sound)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (0, i, 0.25f * std::sin (0.1f * static_cast<float> (i)));

            effect->processBlock (buffer, midi);
        };

        block (true, false, 0.0);
        CHECK (says ("Waiting for audio to carve"));
        setParameter (*effect, params::id::mode, 2.0f);
        block (true, false, 0.0);
        CHECK (says ("Waiting for audio to stencil"));
        CHECK (ui::shell::effectHint (*effect, true) == "Waiting for audio");
        setParameter (*effect, params::id::mode, 0.0f);

        block (true, true, 40.0);
        CHECK (says ("Your loop shows 2 of 4 bars"));
        CHECK (ui::shell::effectHint (*effect, true) == "Loop shows 2 of 4 bars");
        block (true, true, 38.0);
        CHECK (says ("Your loop shows 1.5 of 4 bars"));
        setParameter (*effect, params::id::repeat, 1.0f);
        CHECK (says ("Your loop shows 1.5 of 4 bars"));
        setParameter (*effect, params::id::repeat, 0.0f);
        block (true, true, 48.0);
        CHECK (says ("Carves once when you press play"));   // a loop of the whole drawing
        block (true, true, 0.0);
        CHECK (says ("Carves once when you press play"));
        block (false, false, 0.0);
        CHECK (says ("Carves once when you press play"));
        effect->setPlayHead (nullptr);
    }

    setParameter (*effect, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    setParameter (*effect, params::id::startBar, 9.0f);
    CHECK (says ("Carves once from bar 9"));

    setParameter (*effect, params::id::repeat, 1.0f);
    CHECK (says ("Repeats every 4 bars"));
    setParameter (*effect, params::id::length, 2.0f);
    CHECK (says ("Repeats every 1 bar"));

    setParameter (*effect, params::id::trigger, 1.0f);
    CHECK (says ("Loops while a note is held"));
    setParameter (*effect, params::id::repeat, 0.0f);
    CHECK (says ("Each note carves it once"));

    // The line is whole in the bar: every sentence fits (else the bar says it in fewer words).
    auto* bar = dynamic_cast<ui::shell::EffectBottomBar*> (findComponent (*editor, "footer"));

    if (bar != nullptr)
        for (const auto mode : { 0.0f, 1.0f, 2.0f, 3.0f })
            for (const auto trigger : { 0.0f, 1.0f })
                for (const auto repeat : { 0.0f, 1.0f })
                    for (const auto startAt : { 0.0f, 1.0f })
                    {
                        setParameter (*effect, params::id::mode, mode);
                        setParameter (*effect, params::id::trigger, trigger);
                        setParameter (*effect, params::id::repeat, repeat);
                        setParameter (*effect, params::id::startAt, startAt);
                        setParameter (*effect, params::id::startBar, 999.0f);
                        setParameter (*effect, params::id::length, 7.0f);
                        pump (10);

                        const auto shown = bar->getShownHint();
                        CHECK (shown == ui::shell::effectHint (*effect));
                        CHECK (juce::GlyphArrangement::getStringWidth (fonts::ui (11.0f), shown) <= static_cast<float> (bar->getHintBounds().getWidth()));
                    }

    // The shorter forms exist for a narrower bar.
    setParameter (*effect, params::id::mode, 0.0f);
    setParameter (*effect, params::id::trigger, 0.0f);
    setParameter (*effect, params::id::repeat, 0.0f);
    setParameter (*effect, params::id::startAt, 0.0f);
    CHECK (ui::shell::effectHint (*effect, true) == "Carves once on play");
}

ARTFREQ_TEST (uiEffectDrawerOpensForLayersAndObjects)
{
    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);

    // A text on the left of the canvas and one on the right.
    ArtDocument document;
    auto left = ArtDocument::makeTextObject ("LEFT", 3500.0f, 16000.0f);
    left.startTime = 0.04f;
    left.endTime = 0.4f;
    auto right = ArtDocument::makeTextObject ("RIGHT", 300.0f, 1500.0f);
    right.startTime = 0.62f;
    right.endTime = 0.96f;
    document.objects.push_back (left);
    document.objects.push_back (right);
    effect->setDocument (document);

    auto editor = makeEditor (*effect);
    pump (80);

    auto* drawer = findComponent (*editor, "inspector");
    auto* canvas = findComponent (*editor, "canvas");
    auto* layers = dynamic_cast<juce::Button*> (findComponent (*editor, "layers"));
    auto* close = dynamic_cast<juce::Button*> (findComponent (*editor, "inspector-close"));

    if (drawer == nullptr || canvas == nullptr || layers == nullptr || close == nullptr)
    {
        CHECK (false);
        return;
    }

    // Closed until it is asked for.
    CHECK (! editor->isDrawerOpen() && ! drawer->isVisible() && ! editor->isLayerListShown());

    // An object selected: its settings, over the right of the canvas, the object in view.
    editor->selectObject (left.id);
    CHECK_EVENTUALLY (editor->isDrawerOpen() && drawer->isVisible() && ! editor->isLayerListShown() && ! editor->isDrawerOnLeft()
                      && isShown (*editor, "inspector-text"));
    CHECK (drawer->getWidth() == ui::Inspector::compactWidth && drawer->getRight() == 720 && drawer->getHeight() >= ui::Inspector::compactMinimumHeight);
    CHECK (isShown (*editor, "inspector-text") && isShown (*editor, "inspector-band") && isShown (*editor, "inspector-intensity"));
    CHECK (childrenAreApart (*drawer));
    CHECK (! layers->getToggleState());

    // The canvas keeps its size: the drawer lies over it.
    CHECK_GREATER (canvas->getWidth(), 700);

    // The object on the right: its settings would cover it there, so they open on the left.
    editor->selectObject (right.id);
    CHECK_EVENTUALLY (editor->isDrawerOpen() && editor->isDrawerOnLeft() && drawer->getX() == 0);
    CHECK (childrenAreApart (*drawer));

    // Deselect: gone (and it opens on the right again next time).
    editor->selectObject (juce::Uuid::null());
    CHECK_EVENTUALLY (! editor->isDrawerOpen() && ! editor->isDrawerOnLeft());

    // The Layers button: the list, and the button lit; again: closed.
    layers->onClick();
    CHECK_EVENTUALLY (editor->isDrawerOpen() && editor->isLayerListShown() && layers->getToggleState() && isShown (*editor, "layer-list"));
    CHECK (! isShown (*editor, "inspector-add-text") && ! isShown (*editor, "lows") && ! isShown (*editor, "inspector-size"));
    CHECK (childrenAreApart (*drawer));
    layers->onClick();
    CHECK_EVENTUALLY (! editor->isDrawerOpen() && ! layers->getToggleState());

    // Its own close button closes it whatever it shows, and keeps the selection.
    editor->selectObject (left.id);
    CHECK_EVENTUALLY (editor->isDrawerOpen());
    close->onClick();
    CHECK_EVENTUALLY (! editor->isDrawerOpen() && editor->getSelectedObject() == left.id);

    // Another object selected opens it again.
    editor->selectObject (right.id);
    CHECK_EVENTUALLY (editor->isDrawerOpen());

    // The object deleted: nothing selected, nothing to show.
    effect->editDocument ([] (ArtDocument& d) { d.objects.clear(); });
    CHECK_EVENTUALLY (editor->getSelectedObject().isNull() && ! editor->isDrawerOpen());

    // The drawer is open through both strips at once and still has room for its tallest content.
    editor->showLayers (true);
    setParameter (*effect, params::id::mode, 3.0f);

    if (auto* brush = dynamic_cast<juce::Button*> (findComponent (*editor, "tool-brush")))
        brush->onClick();

    CHECK_EVENTUALLY (isShown (*editor, "sound-strip") && isShown (*editor, "brush-strip") && editor->isDrawerOpen());
    CHECK (drawer->getHeight() >= ui::Inspector::compactMinimumHeight);
}

ARTFREQ_TEST (uiEffectDrawerKeepsWideObjectsInView)
{
    // An object wider than the room beside the drawer (a new text runs from 4 to 96 % of the
    // length; so do Fill length and a new window's "ArtFreq"): its settings open beside the
    // canvas, which moves aside, so all of it and its handles stay in view. Over the canvas, the
    // drawer hid the right third of it (the end of the text being typed, its right handles).
    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);

    ArtDocument document;
    auto wide = ArtDocument::makeTextObject ("HELLO WORLD", 300.0f, 3000.0f);
    wide.startTime = 0.04f;
    wide.endTime = 0.96f;
    auto narrow = ArtDocument::makeTextObject ("HI", 3500.0f, 16000.0f);
    narrow.startTime = 0.1f;
    narrow.endTime = 0.4f;
    document.objects.push_back (wide);
    document.objects.push_back (narrow);
    effect->setDocument (document);

    auto editor = makeEditor (*effect);
    pump (80);

    auto* drawer = findComponent (*editor, "inspector");
    auto* canvas = dynamic_cast<ui::CanvasView*> (findComponent (*editor, "canvas"));
    auto* fill = dynamic_cast<juce::Button*> (findComponent (*editor, "inspector-fill"));

    if (drawer == nullptr || canvas == nullptr || fill == nullptr)
    {
        CHECK (false);
        return;
    }

    const auto fullWidth = canvas->getWidth();

    const auto inView = [&] (const juce::Uuid& id)
    {
        const auto object = canvas->getObjectBounds (id) + canvas->getPosition().toFloat();
        const auto area = canvas->getDrawingArea() + canvas->getPosition().toFloat();
        return ! object.isEmpty() && ! object.intersects (drawer->getBounds().toFloat())
            && object.getX() >= area.getX() - 6.0f && object.getRight() <= area.getRight() + 6.0f;
    };

    // The wide one: the canvas beside the drawer, the whole object in view.
    editor->selectObject (wide.id);
    CHECK_EVENTUALLY (editor->isDrawerOpen() && canvas->getWidth() < fullWidth - 200);
    CHECK (canvas->getRight() <= drawer->getX() || canvas->getX() >= drawer->getRight());
    CHECK (inView (wide.id));

    // A narrow one: the drawer over the canvas again (its drawing does not move for it).
    editor->selectObject (narrow.id);
    CHECK_EVENTUALLY (editor->isDrawerOpen() && canvas->getWidth() == fullWidth);
    CHECK (inView (narrow.id));

    // Fill length makes it too wide for either side: the canvas moves aside, under the mouse's
    // feet as it were, but the drawer (where the mouse is) stays where it was.
    const auto drawerBefore = drawer->getBounds();
    fill->triggerClick();
    CHECK_EVENTUALLY (effect->getDocument().find (narrow.id)->endTime > 0.9f && canvas->getWidth() < fullWidth - 200);
    CHECK (drawer->getBounds() == drawerBefore && inView (narrow.id));

    // The settings closed (Esc, the close button, deselecting): the canvas has the width again.
    editor->selectObject (juce::Uuid::null());
    CHECK_EVENTUALLY (! editor->isDrawerOpen() && canvas->getWidth() == fullWidth);

    // The layer list lies over the canvas, whatever is selected in it.
    editor->selectObject (wide.id);
    CHECK_EVENTUALLY (editor->isDrawerOpen() && canvas->getWidth() < fullWidth - 200);
    editor->showLayers (true);
    CHECK_EVENTUALLY (editor->isLayerListShown() && canvas->getWidth() == fullWidth);
}

ARTFREQ_TEST (uiEffectSettingsHoldTheTiming)
{
    // Trigger, Start and Tempo are rarely changed: ArtFreq FX keeps them in its settings, beside
    // the analyser match and the window size. Only what applies is shown.
    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);
    auto editor = makeEditor (*effect);
    pump (60);
    editor->showSettings (true);
    CHECK_EVENTUALLY (isShown (*editor, "settings-panel"));

    auto* panel = dynamic_cast<ui::shell::SettingsPanel*> (findComponent (*editor, "settings-panel"));
    auto* trigger = dynamic_cast<ui::SegmentedControl*> (findComponent (*editor, "trigger"));
    auto* startAt = dynamic_cast<ui::SegmentedControl*> (findComponent (*editor, "start-at"));
    auto* tempo = dynamic_cast<ui::SegmentedControl*> (findComponent (*editor, "tempo-source"));

    if (panel == nullptr || trigger == nullptr || startAt == nullptr || tempo == nullptr)
    {
        CHECK (false);
        return;
    }

    const auto choice = [&] (const char* id) { return juce::roundToInt (effect->getParameters().getRawParameterValue (id)->load()); };

    // The settings follow the parameters on the editor's 30 Hz timer: each check waits for what it
    // expects (each change shows or hides a field).
    CHECK (isShown (*editor, "scale") && isShown (*editor, "range-low") && isShown (*editor, "slope") && isShown (*editor, "window-size")
           && isShown (*editor, "reset-drawing") && isShown (*editor, "trigger") && isShown (*editor, "start-at") && isShown (*editor, "tempo-source"));
    CHECK_EVENTUALLY (! isShown (*editor, "start-bar") && ! isShown (*editor, "bpm"));
    CHECK (childrenAreApart (*panel));

    // Start: at a bar, with its field.
    startAt->setSelectedIndex (1, juce::sendNotificationSync);
    CHECK_EVENTUALLY (choice (params::id::startAt) == params::atBarStartIndex && isShown (*editor, "start-bar"));

    // Tempo: typed, with its field.
    tempo->setSelectedIndex (1, juce::sendNotificationSync);
    CHECK_EVENTUALLY (choice (params::id::tempoSource) == 1 && isShown (*editor, "bpm"));
    CHECK (childrenAreApart (*panel));
    tempo->setSelectedIndex (0, juce::sendNotificationSync);
    CHECK_EVENTUALLY (! isShown (*editor, "bpm"));

    // A single pass is the only one with a start to choose.
    setParameter (*effect, params::id::repeat, 1.0f);
    CHECK_EVENTUALLY (! isShown (*editor, "start-at") && ! isShown (*editor, "start-bar"));
    setParameter (*effect, params::id::repeat, 0.0f);
    trigger->setSelectedIndex (1, juce::sendNotificationSync);
    CHECK_EVENTUALLY (choice (params::id::trigger) == 1 && ! isShown (*editor, "start-at") && ! isShown (*editor, "start-bar"));
    trigger->setSelectedIndex (0, juce::sendNotificationSync);
    CHECK_EVENTUALLY (isShown (*editor, "start-at") && isShown (*editor, "start-bar"));

    juce::StringArray disabled;
    collectDisabled (*editor, disabled);
    CHECK (disabled.isEmpty());

    // The instrument's settings are the analyser, the window and the drawing, as before.
    auto instrument = makeProcessor (ArtFreqProcessor::Variant::instrument);
    ui::shell::SettingsPanel instrumentPanel (*instrument);
    CHECK (instrumentPanel.getPanelWidth() == ui::shell::SettingsPanel::width);
    CHECK (findComponent (instrumentPanel, "start-at") == nullptr && findComponent (instrumentPanel, "trigger") == nullptr);
}

ARTFREQ_TEST (uiEffectPanelsFitTheWindow)
{
    // Both help panels fit in their window with room around them (the editor centres them), and
    // ArtFreq FX's settings fit under its top bar.
    ui::shell::HelpPanel instrumentHelp (false), effectHelp (true);
    CHECK_LESS (instrumentHelp.getIdealHeight(), ArtFreqEditor::baseHeight - 24);
    CHECK_LESS (effectHelp.getIdealHeight(), ArtFreqEditor::effectBaseHeight - 24);
    CHECK_LESS (effectHelp.getPanelWidth(), ArtFreqEditor::effectBaseWidth - 24);
    CHECK (instrumentHelp.getPanelWidth() == ui::shell::HelpPanel::width);

    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);
    ui::shell::SettingsPanel settings (*effect);
    CHECK_LESS (settings.getPanelWidth(), ArtFreqEditor::effectBaseWidth - 24);
    CHECK_LESS (settings.getIdealHeight(), ArtFreqEditor::effectBaseHeight - ui::shell::effectMetrics::topBarHeight - 24);
}

ARTFREQ_TEST (uiEffectWindowSizes)
{
    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);

    {
        auto editor = makeEditor (*effect);
        pump (40);

        // From 75 to 200 % of 720 x 440, the shape fixed; remembered.
        editor->setSize (1080, 660);
        CHECK_EVENTUALLY (effect->getEditorWidth() == 1080 && effect->getEditorHeight() == 660);

        auto* limits = editor->getConstrainer();
        CHECK (limits != nullptr && limits->getMinimumWidth() == 540 && limits->getMinimumHeight() == 330
               && limits->getMaximumWidth() == 1440 && limits->getMaximumHeight() == 880);
    }

    {
        auto again = makeEditor (*effect);
        CHECK (again->getWidth() == 1080 && again->getHeight() == 660);
    }

    // A size saved with 0.4's ArtFreq FX, which had the instrument's window (1120 x 720 at 100 %),
    // is that window's scale: 100 % stays 100 % of the small window, 150 % stays 150 %.
    for (const auto& [width, height, expectedWidth, expectedHeight] : { std::tuple<int, int, int, int> { 1120, 720, 720, 440 },
                                                                         { 1680, 1080, 1080, 660 },
                                                                         { 840, 540, 540, 330 },
                                                                         { 2240, 1440, 1440, 880 },
                                                                         { 1400, 900, 900, 550 } })
    {
        effect->setEditorSize (width, height);
        auto old = makeEditor (*effect);
        CHECK (old->getWidth() == expectedWidth && old->getHeight() == expectedHeight);
    }

    // Never resized: 100 %.
    effect->setEditorSize (0, 0);
    auto fresh = makeEditor (*effect);
    CHECK (fresh->getWidth() == 720 && fresh->getHeight() == 440);

    // The instrument reads a saved size as it always did.
    CHECK_NEAR (ArtFreqEditor::scaleForSavedSize (false, 1680, 1080), 1.5f, 1.0e-4f);
    CHECK_NEAR (ArtFreqEditor::scaleForSavedSize (false, 0, 0), 1.0f, 1.0e-4f);
    CHECK_NEAR (ArtFreqEditor::scaleForSavedSize (false, 400, 300), 0.75f, 1.0e-4f);
}

ARTFREQ_TEST (uiEffectCanvasFollowsLevel)
{
    // In the modes that play the sines the canvas shows them at their level against the
    // stand-in song: turning Level from 0 to -60 dB changes it (the drawing fades into the song,
    // or leaves Carve + Draw's hole dark). In Carve and Stencil Level plays no part.
    for (const auto& [mode, follows] : { std::pair<float, bool> { 3.0f, true }, { 1.0f, true }, { 0.0f, false }, { 2.0f, false } })
    {
        auto effect = makeProcessor (ArtFreqProcessor::Variant::effect);
        setParameter (*effect, params::id::mode, mode);
        effect->rebuildPlanIfNeeded();
        auto editor = makeEditor (*effect);
        editor->setVisible (true);
        pump (100);

        auto* canvas = dynamic_cast<ui::CanvasView*> (findComponent (*editor, "canvas"));

        if (canvas == nullptr)
        {
            CHECK (false);
            return;
        }

        // Painted once, the canvas knows the size of its picture (it asks for none before).
        canvas->createComponentSnapshot (canvas->getLocalBounds(), true, 1.0f);

        // The canvas takes Level in on its timer, and the fast preview's thread then makes the
        // picture for it: a slow machine does both well after any fixed wait. So each picture is
        // taken once both have happened: a new picture made since Level changed (Level is part of
        // what the fast preview is of in every mode, so one is made even where it looks the same),
        // and the snapshot showing the picture of what is set now, not the one before.
        const auto snapshotAt = [&] (float levelDb)
        {
            const auto madeBefore = canvas->getFastPreviewsMadeInBackground();
            setParameter (*effect, params::id::level, levelDb);
            juce::Image snapshot;

            CHECK (testwait::pumpUntil ([&]
            {
                if (canvas->getFastPreviewsMadeInBackground() == madeBefore)
                    return false;

                snapshot = canvas->createComponentSnapshot (canvas->getLocalBounds(), true, 1.0f);
                return canvas->getPreviewStatus().fastCurrent;
            }));

            return snapshot;
        };

        const auto loud = snapshotAt (0.0f), quiet = snapshotAt (-60.0f);
        auto changed = 0;

        for (int y = 0; y < loud.getHeight(); y += 2)
            for (int x = 0; x < loud.getWidth(); x += 2)
                changed += loud.getPixelAt (x, y) != quiet.getPixelAt (x, y) ? 1 : 0;

        if (follows)
            CHECK_GREATER (changed, 500);
        else
            CHECK_NEAR (changed, 0, 0);
    }
}

ARTFREQ_TEST (uiEffectCanvasShowsEachMode)
{
    // A band drawn in the middle of the canvas (columns 300 to 700), its sines' picture at a
    // full-scale sine's level there (0 dBFS before Level). The colours are levels over the
    // analyser's range (-100 to 0 dBFS by default), as the measured preview and the live view
    // show them; the stand-in song sits around -40 dBFS a cell at FFT 8192.
    Canvas canvas;

    for (int y = 400; y < 600; ++y)
        for (int x = 300; x < 700; ++x)
            canvas.at (x, y) = 1.0f;

    auto masks = std::make_shared<MaskPlanSet>();
    masks->exact = buildMaskPlan (canvas, MaskSettings());
    masks->halo = widenMaskPlan (*masks->exact, 4, 8);

    constexpr int width = 200, height = 100;
    std::vector<float> sines (static_cast<size_t> (width * height), -200.0f);

    for (int y = 40; y < 59; ++y)
        for (int x = 59; x < 137; ++x)
            sines[static_cast<size_t> (y * width + x)] = 0.0f;

    ui::EffectPreviewImage preview;
    const auto inside = juce::Point<int> (100, 50), outside = juce::Point<int> (100, 15);
    const std::shared_ptr<const MaskPlanSet> shared = masks;
    const FrequencyScale scale;
    const auto settingsFor = [] (EffectMode mode, float depthDb)
    {
        ui::EffectPreviewImage::Settings settings;
        settings.mode = mode;
        settings.depthDb = depthDb;
        return settings;
    };
    const auto picture = [&] (EffectMode mode, float depthDb, float levelDb, uint64_t key = 1)
    {
        return preview.get (sines, levelDb, key, settingsFor (mode, depthDb), shared, scale, width, height);
    };

    // Carve at -80 dB: the drawing is black, the song around it is not (also just before it,
    // where the carver's and the analyser's windows blur the edge only a little).
    const auto justBefore = juce::Point<int> (55, 50);   // a few pixels before the band
    auto image = picture (EffectMode::carve, -80.0f, 0.0f);
    CHECK (levelOf (image, inside.x, inside.y) == 0);
    CHECK_GREATER (levelOf (image, outside.x, outside.y), 80);
    CHECK_GREATER (levelOf (image, justBefore.x, justBefore.y), 100);

    // At -24 dB it is darker than the song there (24 dB: 61 levels) but not black.
    const auto song = levelOf (image, outside.x, outside.y);
    const auto songThere = levelOf (picture (EffectMode::draw, -80.0f, -200.0f, 7), inside.x, inside.y);
    image = picture (EffectMode::carve, -24.0f, 0.0f);
    CHECK_GREATER (levelOf (image, inside.x, inside.y), 40);
    CHECK_LESS (levelOf (image, inside.x, inside.y), songThere - 50);

    // Stencil: the other way round.
    image = picture (EffectMode::stencil, -80.0f, 0.0f);
    CHECK_GREATER (levelOf (image, inside.x, inside.y), 100);
    CHECK (levelOf (image, outside.x, outside.y) == 0);

    // Carve + Draw: the sines bright in the band, a darker edge just before it (the halo: 60 ms,
    // a pixel and a half here, so the windows blur it to about 10 dB), the song as it is further
    // out.
    image = picture (EffectMode::carveDraw, -80.0f, 0.0f);
    CHECK (levelOf (image, inside.x, inside.y) == 255);
    CHECK_LESS (levelOf (image, 58, 50), levelOf (image, 52, 50) - 15);
    CHECK (levelOf (image, outside.x, outside.y) == song);

    // Draw: the sines over the song, nothing carved.
    image = picture (EffectMode::draw, -80.0f, 0.0f);
    CHECK (levelOf (image, inside.x, inside.y) == 255);
    CHECK (levelOf (image, outside.x, outside.y) == song);

    // The sines at their level (Level included) against the song, their power added to the
    // song's: at -30 dBFS they show as -30 dBFS (a little brighter where the song is as loud);
    // at -60 they are lost in the song, and Carve + Draw shows its hole alone.
    const auto songInside = levelOf (picture (EffectMode::draw, -80.0f, -200.0f, 2), inside.x, inside.y);
    image = picture (EffectMode::draw, -80.0f, -30.0f, 3);
    CHECK (levelOf (image, inside.x, inside.y) >= colourLevelFor (-30.0f, -100.0f, 0.0f));
    CHECK (levelOf (image, inside.x, inside.y) <= colourLevelFor (-27.0f, -100.0f, 0.0f));
    image = picture (EffectMode::draw, -80.0f, -60.0f, 4);
    CHECK_NEAR (levelOf (image, inside.x, inside.y), songInside, 1);
    image = picture (EffectMode::carveDraw, -80.0f, -200.0f, 5);
    CHECK (levelOf (image, inside.x, inside.y) == 0);

    // Nothing drawn: every mode shows the song as it is (Stencil does not black it out).
    auto empty = std::make_shared<MaskPlanSet>();
    empty->exact = buildMaskPlan (Canvas(), MaskSettings());
    empty->halo = widenMaskPlan (*empty->exact, 4, 8);
    image = preview.get (std::vector<float> (static_cast<size_t> (width * height), -200.0f), 0.0f, 6,
                         settingsFor (EffectMode::stencil, -80.0f), empty, scale, width, height);
    CHECK (levelOf (image, outside.x, outside.y) == song);
    CHECK_GREATER (levelOf (image, inside.x, inside.y), 100);

    // Once the measured preview has analysed the stand-in, the fast preview draws that instead
    // of the level it was made with (here, a flat -50 dBFS).
    auto measured = std::make_shared<ui::MeasuredSpectrogram>();
    measured->frames = 4;
    measured->rows = 10;
    measured->output.assign (40, -16000);
    measured->input.assign (40, -5000);
    preview.setMeasuredSong (measured);
    image = preview.get (std::vector<float> (static_cast<size_t> (width * height), -200.0f), 0.0f, 6,
                         settingsFor (EffectMode::stencil, -80.0f), empty, scale, width, height);
    CHECK (levelOf (image, outside.x, outside.y) == colourLevelFor (-50.0f, -100.0f, 0.0f));
    preview.setMeasuredSong (nullptr);

    // The stand-in song falls towards the highs as a master does (4.5 dB an octave, give or take
    // its beat, chords and grain), and is never above full scale.
    CHECK_GREATER (ui::EffectPreviewImage::songDb (0.3, 100.0), ui::EffectPreviewImage::songDb (0.3, 8000.0) + 20.0f);
    CHECK_LESS (ui::EffectPreviewImage::songDb (0.0, 20.0), 0.001f);
}
