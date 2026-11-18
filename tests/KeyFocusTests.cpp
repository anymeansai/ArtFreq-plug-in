// The keyboard focus (ui/KeyFocus.h): a key the editor has no use for must come back unused, so
// the DAW gets it (its play key, the space bar mostly), and a text box may have the focus only
// while it is typed in.
//
// The editor is in a real window here, with the keyboard, and is driven through the window's
// peer, the way the OS drives it: mouse events through handleMouseEvent (so JUCE gives the focus
// to what is clicked, as in a host), keys through handleKeyUpOrDown and handleKeyPress, whose
// results are what macOS goes by when it passes a key on to the host's window.
//
// That needs a display that gives a window the keyboard and the mouse when asked: a virtual
// display on Linux does (xvfb-run -a); a build machine's macOS session does not reliably, so
// these tests run on Linux only (the code they test is the same on both).

#include "LicenseTestSupport.h"
#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/KeyFocus.h"
#include "ui/LayerList.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <memory>

using namespace artfreq;
using testutil::getParameter;
using testutil::setParameter;

namespace
{

bool canDriveAWindow()
{
   #if JUCE_LINUX || JUCE_BSD
    return true;
   #else
    std::printf ("    (not run here: needs a window with the keyboard, see the top of KeyFocusTests.cpp)\n");
    return false;
   #endif
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

const juce::KeyPress space (juce::KeyPress::spaceKey, juce::ModifierKeys(), ' ');
const juce::KeyPress returnKey (juce::KeyPress::returnKey, juce::ModifierKeys(), '\r');
const juce::KeyPress escapeKey (juce::KeyPress::escapeKey, juce::ModifierKeys(), 27);
const juce::KeyPress tabKey (juce::KeyPress::tabKey, juce::ModifierKeys(), '\t');

/** An instance with a text object in the highs (bars 1 and 2), its editor in a window that has
    the keyboard. */
struct Rig
{

    explicit Rig (ArtFreqProcessor::Variant variant = ArtFreqProcessor::Variant::instrument)
        : processor (std::make_unique<ArtFreqProcessor> (variant))
    {
        processor->setRateAndBufferSizeDetails (48000.0, 512);
        processor->prepareToPlay (48000.0, 512);

        ArtDocument document;
        auto text = ArtDocument::makeTextObject ("HELLO", 3500.0f, 16000.0f);
        text.startTime = 0.04f;
        text.endTime = 0.5f;
        textId = text.id;
        document.objects.push_back (text);
        processor->setDocument (document);
        processor->rebuildPlanIfNeeded();

        editor.reset (dynamic_cast<ArtFreqEditor*> (processor->createEditor()));

        if (editor == nullptr)
            return;

        time += 5000;
        editor->addToDesktop (0);
        editor->setVisible (true);
        pump (100);

        // As when the user clicks into the plug-in's window for the first time.
        editor->grabKeyboardFocus();
        pump (50);

        // The display tells the window that it has the keyboard in a message, and JUCE then
        // gives the focus to the editor. On X11 that message can be read early (with the answer
        // to "has it the keyboard?") and then lie unseen until something more arrives from the
        // display: it would turn up after the test's first click, and take the focus from what
        // was clicked. So it is waited for here: the focus is put on the canvas, and comes back
        // to the editor when the message is dealt with (the pointer moving and the window
        // repainting keep the display talking). If it was dealt with at once, the wait is in vain.
        if (auto* canvasView = findComponent (*editor, "canvas"))
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

    ~Rig()
    {
        if (editor != nullptr)
            editor->removeFromDesktop();

        pump (20);
    }

    juce::ComponentPeer* peer() const   { return editor != nullptr ? editor->getPeer() : nullptr; }

    /** The window has the keyboard, and something in it the focus. */
    bool valid() const
    {
        return peer() != nullptr && peer()->isFocused() && juce::Component::getCurrentlyFocusedComponent() != nullptr;
    }

    juce::Component& get (const juce::String& id) const
    {
        auto* found = findComponent (*editor, id);
        jassert (found != nullptr);
        return found != nullptr ? *found : *editor;
    }

    juce::Component* focused() const   { return juce::Component::getCurrentlyFocusedComponent(); }
    bool hasFocus (const juce::String& id) const   { return focused() == findComponent (*editor, id); }

    // ---- the mouse, through the window --------------------------------------------------------
    void mouse (juce::Component& c, juce::Point<float> at, bool down, juce::ModifierKeys mods = {})
    {
        const auto inWindow = editor->getLocalPoint (&c, at);
        const auto buttons = down ? mods.withFlags (juce::ModifierKeys::leftButtonModifier) : mods;

        // The display's pointer goes along: components ask it where the mouse is (a button
        // that loses the focus while it is held down checks that the mouse is still over it).
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (editor->localPointToGlobal (inWindow));

        peer()->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, inWindow, buttons,
                                  juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time);
    }

    /** A click (clicks are a second apart: never a double-click by accident). */
    void click (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {})
    {
        time += 1000;
        mouse (c, at, true, mods);
        time += 30;
        mouse (c, at, false, mods);
    }

    void click (const juce::String& id)   { click (get (id), get (id).getLocalBounds().toFloat().getCentre()); }

    void doubleClick (juce::Component& c, juce::Point<float> at)
    {
        click (c, at);
        time += 120 - 1000;
        click (c, at);
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
    }

    // ---- the keyboard, through the window ------------------------------------------------------
    /** Whether a text box is being typed in: macOS then sends the keys there, as text, before
        anything else gets to see them. (A number typed into a field, a knob or a bar is not: its
        keys come as key presses, so the space bar can still go on; see editing().) */
    bool typing() const   { return peer()->findCurrentTextInputTarget() != nullptr; }

    /** Whether a text box has the focus: the object's text, or a number being typed. */
    bool editing() const   { return ui::isTextBox (focused()); }

    /** Presses a key. Returns whether it was used, so that the DAW would not get it: by a text
        box that is being typed in, by any component on the way from the focus up, or by a modal
        component (which swallows every key). */
    bool uses (const juce::KeyPress& key)
    {
        const auto wasTyping = typing();
        auto used = peer()->handleKeyUpOrDown (true);
        used = peer()->handleKeyPress (key) || used;
        used = peer()->handleKeyUpOrDown (false) || used;
        used = used || wasTyping || juce::Component::getCurrentlyModalComponent() != nullptr;

        pump (20);   // (a text box acts on Return and Esc a moment later)
        return used;
    }

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

        pump (20);   // (a text box reports its changes a moment later)
    }

    juce::String text() const
    {
        const auto* object = processor->getDocument().find (textId);
        return object != nullptr ? object->text : juce::String ("(gone)");
    }

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

    ui::CanvasView& canvas() const   { return *dynamic_cast<ui::CanvasView*> (findComponent (*editor, "canvas")); }

    std::unique_ptr<ArtFreqProcessor> processor;
    std::unique_ptr<ArtFreqEditor> editor;
    juce::Uuid textId;

    // The time of the last mouse event. It only moves forwards, from one rig to the next too:
    // JUCE's mouse remembers the last clicks, and takes a click before them for a double-click.
    juce::int64& time = clock();

    static juce::int64& clock()
    {
        static auto now = juce::Time::currentTimeMillis();
        return now;
    }
};

} // namespace

//==============================================================================
ARTFREQ_TEST (uiKeysTheSpaceBarIsTheDawsAfterAnyClick)
{
    // Whatever was clicked last (the canvas, a button, a chip, a knob, a number field, a row of
    // the list), the space bar comes back unused: nothing took it, nothing is typing, nothing is
    // modal. (ArtFreq FX's window, which has its own layout, is the next test.)
    if (! canDriveAWindow())
        return;

    {
        Rig rig;
        CHECK (rig.valid());

        if (! rig.valid())
            return;

        CHECK (! rig.uses (space));   // as the window opens

        rig.click (rig.canvas(), rig.onEmptyCanvas());
        CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));

        // Buttons, chips, segmented controls (Redo is disabled, and BPM follows the host: clicks
        // on them do nothing, and still count).
        for (const auto* id : { "tool-brush", "tool-select", "repeat", "trigger", "view-switch", "redo", "bpm", "preview",
                                "lows" })
        {
            rig.click (id);
            pump (20);
            CHECK (! rig.uses (space));
            CHECK (rig.focused() != nullptr && ! dynamic_cast<juce::Button*> (rig.focused()));

            if (rig.uses (space))
                std::printf ("    after a click on %s\n", id);

            // (The click did what it does: the focus moving away mid-click is no obstacle.)
            if (juce::String (id) == "tool-brush")
                CHECK (dynamic_cast<juce::Button&> (rig.get (id)).getToggleState());

            if (juce::String (id) == "repeat")
                CHECK_NEAR (getParameter (*rig.processor, params::id::repeat), 1.0f, 1.0e-4);
        }

        // A menu (Length's, the start choice's) has the keys while it is open; once it is
        // closed nothing is modal, and the focus is not left on the box that opened it.
        for (const auto* id : { "length", "start-choice" })
        {
            setParameter (*rig.processor, params::id::repeat, 0.0f);
            setParameter (*rig.processor, params::id::trigger, 0.0f);
            pump (60);
            rig.click (id);
            pump (30);
            juce::PopupMenu::dismissAllActiveMenus();
            pump (60);
            CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
            CHECK (! rig.uses (space) && ! rig.uses (returnKey) && ! rig.uses (juce::KeyPress (juce::KeyPress::downKey)));
            CHECK (rig.focused() != nullptr && ! dynamic_cast<juce::ComboBox*> (rig.focused())
                   && ! dynamic_cast<juce::Button*> (rig.focused()));
        }

        // A knob turned, a field dragged.
        auto& density = rig.get ("density");
        rig.drag (density, { 38.0f, 40.0f }, { 38.0f, 20.0f });
        CHECK (! rig.uses (space));

        rig.click ("view-switch");   // (back to the canvas, whichever half was clicked)
        rig.editor->showLiveView (false);

        // A row of the layer list: the list has the focus, and no use for the space bar.
        auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));
        CHECK (list != nullptr && list->isShowing());

        if (list != nullptr)
        {
            rig.click (*list, list->getRowBounds (0).toFloat().getCentre().withX (60.0f));
            CHECK (rig.hasFocus ("layer-list") && ! rig.uses (space));
        }

        // The keys the editor does use are still its own.
        rig.click (rig.canvas(), rig.onEmptyCanvas());
        CHECK (rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
        CHECK (rig.uses (juce::KeyPress ('v', juce::ModifierKeys(), 'v')));
        CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));   // nothing selected: not ours
        CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::leftKey)));
        CHECK (! rig.uses (returnKey));
        CHECK (! rig.uses (juce::KeyPress ('a', juce::ModifierKeys(), 'a')));

        rig.click (rig.canvas(), rig.onText());
        CHECK (rig.editor->getSelectedObject() == rig.textId);
        CHECK (rig.uses (juce::KeyPress (juce::KeyPress::rightKey)));
        CHECK (rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));
        CHECK (rig.text() == "(gone)");
        CHECK (rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
        CHECK (rig.text() == "HELLO");
    }
}

ARTFREQ_TEST (uiKeysEffectTheSpaceBarIsTheDawsAfterAnyClick)
{
    // The same in ArtFreq FX's own window: its two bars, the strips that come and go, the drawer
    // and the settings that hold its timing.
    if (! canDriveAWindow())
        return;

    Rig rig (ArtFreqProcessor::Variant::effect);
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    const auto spaceIsFree = [&] (const char* after)
    {
        const auto used = rig.uses (space);
        CHECK (! used);
        CHECK (rig.focused() != nullptr && ! dynamic_cast<juce::Button*> (rig.focused()));

        if (used)
            std::printf ("    after %s\n", after);
    };

    CHECK (! rig.uses (space));   // as the window opens

    rig.click (rig.canvas(), rig.onEmptyCanvas());
    pump (40);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));

    // The bars: each control only clicked, and their backgrounds.
    for (const auto* id : { "tool-brush", "brush-strip", "tool-select", "live", "repeat", "mode", "header", "footer", "layers",
                            "inspector", "inspector-close" })
    {
        auto& target = rig.get (id);
        const auto at = juce::String (id) == "brush-strip" ? juce::Point<float> (680.0f, 16.0f)
                      : juce::String (id) == "header"      ? juce::Point<float> (565.0f, 38.0f)
                      : juce::String (id) == "footer"      ? juce::Point<float> (400.0f, 4.0f)
                      : juce::String (id) == "inspector"   ? juce::Point<float> (120.0f, static_cast<float> (target.getHeight()) - 30.0f)
                                                           : target.getLocalBounds().toFloat().getCentre();
        rig.click (target, at);
        pump (20);
        spaceIsFree (id);

        // (The click did what it does.)
        if (juce::String (id) == "tool-brush")
            CHECK (dynamic_cast<juce::Button&> (rig.get (id)).getToggleState() && rig.get ("brush-strip").isVisible());

        if (juce::String (id) == "repeat")
            CHECK_NEAR (getParameter (*rig.processor, params::id::repeat), 1.0f, 1.0e-4);

        if (juce::String (id) == "layers")
            CHECK (rig.editor->isLayerListShown());

        if (juce::String (id) == "inspector-close")
            CHECK (! rig.editor->isDrawerOpen());

        rig.editor->showLiveView (false);
        setParameter (*rig.processor, params::id::repeat, 0.0f);
        setParameter (*rig.processor, params::id::mode, 0.0f);
        pump (40);
    }

    // Length's menu has the keys while it is open; closed, nothing is modal or left on the box.
    rig.click ("length");
    pump (30);
    juce::PopupMenu::dismissAllActiveMenus();
    pump (60);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
    CHECK (! rig.uses (space) && ! rig.uses (returnKey) && ! rig.uses (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (rig.focused() != nullptr && ! dynamic_cast<juce::ComboBox*> (rig.focused()));

    // Depth dragged; in a Draw mode the Sound strip: Lows clicked, Level dragged, then typed.
    rig.drag (rig.get ("depth"), { 30.0f, 14.0f }, { 30.0f, 4.0f });
    pump (40);
    spaceIsFree ("dragging Depth");

    setParameter (*rig.processor, params::id::mode, 3.0f);
    pump (80);
    rig.click ("lows");
    pump (40);
    spaceIsFree ("a click on Lows");
    rig.drag (rig.get ("level"), { 30.0f, 12.0f }, { 30.0f, 4.0f });
    pump (40);
    spaceIsFree ("dragging Level");

    // A single click on a field starts typing in it, and the space bar, which has no place in a
    // number, ends the typing (what was typed is set) and goes on to the DAW: a click on a value
    // and then the space bar plays the DAW, as everywhere else. Return sets a value and is kept.
    for (const auto* id : { "level", "density", "contrast" })
    {
        rig.click (id);
        pump (40);
        CHECK (rig.editing() && ! rig.typing());
        CHECK (! rig.uses (space));
        CHECK (! rig.editing() && rig.hasFocus ("canvas") && ! rig.uses (space));
    }

    rig.click ("level");
    pump (40);
    rig.type ("-12");
    CHECK (! rig.uses (space));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -12.0f, 1.0e-3);
    CHECK (! rig.editing() && rig.hasFocus ("canvas"));

    rig.click ("level");
    pump (40);
    rig.type ("-9");
    pump (40);
    CHECK (rig.uses (returnKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -9.0f, 1.0e-3);
    CHECK (! rig.editing() && rig.hasFocus ("canvas") && ! rig.uses (space));

    // The strip going while one of its values is typed (automation changes the mode) leaves no
    // text box behind.
    rig.click ("density");
    pump (40);
    CHECK (rig.editing());
    setParameter (*rig.processor, params::id::mode, 0.0f);
    pump (120);
    CHECK (! rig.get ("sound-strip").isVisible() && ! rig.editing());

    rig.click ("depth");
    pump (40);
    CHECK (rig.editing() && ! rig.uses (space));
    CHECK (! rig.editing() && rig.hasFocus ("canvas"));
    spaceIsFree ("the Sound strip went while a value was typed");

    // The settings: the timing's controls clicked, the start bar typed; the focus stays with the
    // panel (Esc closes it), and comes back to the canvas when it closes.
    rig.click ("settings");
    pump (40);
    CHECK (rig.hasFocus ("settings-panel") && ! rig.uses (space));

    for (const auto* id : { "tempo-source", "start-at" })
    {
        auto& control = rig.get (id);
        rig.click (control, { static_cast<float> (control.getWidth()) * 0.75f, 14.0f });
        pump (40);
        CHECK (rig.hasFocus ("settings-panel") && ! rig.uses (space));
    }

    CHECK (rig.get ("start-bar").isShowing() && rig.get ("bpm").isShowing());
    rig.click ("start-bar");
    pump (40);
    CHECK (rig.editing());
    rig.type ("12");
    pump (40);
    CHECK (rig.uses (returnKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 12.0f, 1.0e-4);
    CHECK (! rig.editing() && rig.hasFocus ("settings-panel") && ! rig.uses (space));

    rig.click ("bpm");
    pump (40);
    CHECK (rig.editing() && ! rig.uses (space));
    CHECK (! rig.editing() && rig.hasFocus ("settings-panel"));
    CHECK (rig.uses (escapeKey) && ! rig.get ("settings-panel").isVisible() && rig.hasFocus ("canvas") && ! rig.uses (space));

    // The live view (no canvas to hold the focus).
    rig.click ("live");
    pump (40);
    spaceIsFree ("a click on Live");
    rig.click ("live");
    pump (40);
    CHECK (rig.get ("canvas").isShowing());

    // The layer list in the drawer: a row has the focus; Esc deselects, then closes the list, and
    // then is no key of ours.
    rig.click ("layers");
    pump (40);
    auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));
    CHECK (list != nullptr && list->isShowing());

    if (list != nullptr)
    {
        rig.click (*list, list->getRowBounds (0).toFloat().getCentre().withX (60.0f));
        pump (40);
        CHECK (rig.hasFocus ("layer-list") && rig.editor->getSelectedObject() == rig.textId && ! rig.uses (space));
        CHECK (rig.uses (escapeKey) && rig.editor->getSelectedObject().isNull() && rig.editor->isLayerListShown());
        CHECK (rig.uses (escapeKey) && ! rig.editor->isDrawerOpen() && rig.hasFocus ("canvas"));
        CHECK (! rig.uses (escapeKey) && ! rig.uses (space));
    }

    // The keys the editor does use are still its own.
    rig.click (rig.canvas(), rig.onEmptyCanvas());
    pump (40);
    CHECK (rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')) && rig.get ("brush-strip").isVisible());
    CHECK (rig.uses (juce::KeyPress ('v', juce::ModifierKeys(), 'v')) && ! rig.get ("brush-strip").isVisible());
    CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));   // nothing selected: not ours
    CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (! rig.uses (returnKey));
    CHECK (! rig.uses (tabKey));

    rig.click (rig.canvas(), rig.onText());
    pump (40);
    CHECK (rig.editor->getSelectedObject() == rig.textId && rig.editor->isDrawerOpen());
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (rig.text() == "(gone)" && ! rig.editor->isDrawerOpen());
    CHECK (rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
    CHECK (rig.text() == "HELLO");
}

ARTFREQ_TEST (uiKeysEffectDrawerKeepsClearOfTheMouse)
{
    // ArtFreq FX's drawer lies over the canvas, so it must not get in the way of the mouse: it
    // opens away from a click (a double-click's second click still lands on the text), selecting
    // is not typing, its close button keeps the selection, a click on the selected object brings
    // it back, and it moves aside when an object is dragged under it.
    if (! canDriveAWindow())
        return;

    Rig rig (ArtFreqProcessor::Variant::effect);
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    // A second text, on the right of the canvas (the rig's "HELLO" is on the left).
    juce::Uuid rightId;
    rig.processor->editDocument ([&rightId] (ArtDocument& document)
    {
        auto text = ArtDocument::makeTextObject ("RIGHT", 300.0f, 1500.0f);
        text.startTime = 0.62f;
        text.endTime = 0.96f;
        rightId = text.id;
        document.objects.push_back (text);
    });
    rig.processor->rebuildPlanIfNeeded();
    pump (80);

    const auto area = rig.canvas().getDrawingArea();
    const auto onRight = juce::Point<float> (area.getX() + 0.9f * area.getWidth(), area.getY() + 0.5f * area.getHeight());
    const auto rightText = [&]
    {
        const auto* object = rig.processor->getDocument().find (rightId);
        return object != nullptr ? object->text : juce::String ("(gone)");
    };
    auto& textBox = rig.get ("inspector-text");
    auto& drawer = rig.get ("inspector");

    // A click on the text on the left: its settings on the right; the focus stays with the canvas.
    rig.click (rig.canvas(), rig.onText());
    pump (40);
    CHECK (rig.editor->getSelectedObject() == rig.textId && rig.editor->isDrawerOpen() && ! rig.editor->isDrawerOnLeft());
    CHECK (textBox.isShowing() && ! textBox.hasKeyboardFocus (true) && rig.hasFocus ("canvas") && ! rig.uses (space));
    CHECK (rig.text() == "HELLO");

    // Its close button: closed, still selected, the canvas has the keys.
    rig.click ("inspector-close");
    pump (40);
    CHECK (! rig.editor->isDrawerOpen() && rig.editor->getSelectedObject() == rig.textId && rig.hasFocus ("canvas") && ! rig.uses (space));

    // A click on the selected object opens it again.
    rig.click (rig.canvas(), rig.onText());
    pump (40);
    CHECK (rig.editor->isDrawerOpen() && rig.editor->getSelectedObject() == rig.textId);

    // Esc: nothing selected, no drawer.
    CHECK (rig.uses (escapeKey));
    pump (40);
    CHECK (rig.editor->getSelectedObject().isNull() && ! rig.editor->isDrawerOpen());

    // The text on the right, where the drawer would open: it opens on the left instead, clear of
    // the mouse and of the text.
    rig.click (rig.canvas(), onRight);
    pump (40);
    CHECK (rig.editor->getSelectedObject() == rightId && rig.editor->isDrawerOpen() && rig.editor->isDrawerOnLeft());
    CHECK (! drawer.getBounds().contains (rig.editor->getLocalPoint (&rig.canvas(), onRight).roundToInt()));
    CHECK (rig.uses (escapeKey));
    pump (40);

    // So a double-click there selects the text and goes to typing (its second click was not
    // caught by a drawer that had opened under the mouse).
    rig.doubleClick (rig.canvas(), onRight);
    pump (40);
    CHECK (rig.editor->getSelectedObject() == rightId && textBox.hasKeyboardFocus (true) && rig.typing());
    rig.type ("PLAY IT");
    pump (40);
    CHECK (rightText() == "PLAY IT");
    CHECK (rig.uses (space) && rightText() == "PLAY IT ");   // while typing, the space bar types
    CHECK (rig.uses (returnKey) && ! rig.typing() && rig.hasFocus ("canvas") && ! rig.uses (space));

    // Dragged to the left, under its drawer: let go, the drawer moves to the other side.
    rig.drag (rig.canvas(), onRight, { area.getX() + 0.12f * area.getWidth(), onRight.y });
    pump (40);
    const auto* moved = rig.processor->getDocument().find (rightId);
    CHECK (moved != nullptr && moved->startTime < 0.2f);
    CHECK (rig.editor->isDrawerOpen() && ! rig.editor->isDrawerOnLeft());
    CHECK (! rig.uses (space));

    // + Text: the drawer opens on the new text, with the focus in its text box.
    CHECK (rig.uses (escapeKey));
    pump (40);
    const auto before = rig.processor->getDocument().objects.size();
    rig.click ("add-text");
    pump (60);
    CHECK (rig.processor->getDocument().objects.size() == before + 1 && rig.editor->isDrawerOpen());
    CHECK (textBox.isShowing() && textBox.hasKeyboardFocus (true) && rig.typing());
    CHECK (rig.uses (returnKey) && rig.hasFocus ("canvas") && ! rig.uses (space));
}

ARTFREQ_TEST (uiKeysSelectingTextDoesNotStartTyping)
{
    if (! canDriveAWindow())
        return;

    Rig rig;
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    auto& textBox = rig.get ("inspector-text");

    // A click on a text object selects it and shows its settings, text box included: the focus
    // stays with the canvas, and the next keys are not typed into the text.
    rig.click (rig.canvas(), rig.onText());
    pump (50);
    CHECK (rig.editor->getSelectedObject() == rig.textId);
    CHECK (textBox.isShowing() && ! textBox.hasKeyboardFocus (true));
    CHECK (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));
    rig.type ("x y");
    CHECK (rig.text() == "HELLO");

    // The same from the layer list: a click on its row, then Return (or a double-click) for its
    // settings. The list goes, and the focus with it: to the canvas, not into the text box.
    rig.click (rig.canvas(), rig.onEmptyCanvas());
    pump (50);
    auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));
    CHECK (list != nullptr && list->isShowing());

    if (list == nullptr)
        return;

    const auto row = list->getRowBounds (0).toFloat().getCentre().withX (60.0f);
    rig.click (*list, row);
    pump (50);
    CHECK (rig.hasFocus ("layer-list") && rig.editor->isLayerListShown());
    CHECK (rig.uses (returnKey));   // the list's own key: opens the settings
    pump (50);
    CHECK (! rig.editor->isLayerListShown() && textBox.isShowing());
    CHECK (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space) && ! rig.uses (returnKey));

    rig.click (rig.canvas(), rig.onEmptyCanvas());
    pump (50);
    rig.doubleClick (*list, row);
    pump (50);
    CHECK (! rig.editor->isLayerListShown() && textBox.isShowing() && ! textBox.hasKeyboardFocus (true));
    CHECK (! rig.uses (space));
    CHECK (rig.text() == "HELLO");
}

ARTFREQ_TEST (uiKeysTheTextBoxHasTheFocusOnlyWhileTyping)
{
    if (! canDriveAWindow())
        return;

    Rig rig;
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    auto& textBox = rig.get ("inspector-text");

    // A double-click on the text: its text box has the focus (everything selected), and typing
    // goes into it, spaces too.
    rig.doubleClick (rig.canvas(), rig.onText());
    pump (50);
    CHECK (textBox.hasKeyboardFocus (true) && rig.typing());
    rig.type ("PLAY IT");
    CHECK (rig.text() == "PLAY IT");

    // Return ends the typing: the text stays, the focus is the canvas's, the space bar the DAW's.
    CHECK (rig.uses (returnKey));
    CHECK (rig.hasFocus ("canvas") && ! rig.typing());
    CHECK (! rig.uses (space));
    CHECK (rig.text() == "PLAY IT");

    // Esc does the same, keeping what was typed (one undo step takes the typing back).
    rig.doubleClick (rig.canvas(), rig.onText());
    pump (50);
    CHECK (textBox.hasKeyboardFocus (true));
    rig.type ("NOW");
    CHECK (rig.uses (escapeKey));
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));
    CHECK (rig.text() == "NOW");
    CHECK (rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
    CHECK (rig.text() == "PLAY IT");

    // Shift+Return (or Alt+Return) is a new line, and the typing goes on.
    rig.click (textBox, { 20.0f, 12.0f });
    pump (20);
    CHECK (textBox.hasKeyboardFocus (true));
    rig.type ("A");
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, '\r')));
    rig.type ("B");
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::altModifier, '\r')));
    rig.type ("C");
    CHECK (textBox.hasKeyboardFocus (true));
    CHECK (rig.text().contains ("A\nB\nC"));

    // Tab ends it too (and is used: it did something).
    CHECK (rig.uses (tabKey));
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));

    // A click anywhere else ends it: on a control that takes no focus (a knob, a chip, a
    // segmented control, the inspector's background), on a button, on the canvas.
    for (const auto* elsewhere : { "density", "repeat", "trigger", "inspector", "inspector-fill", "canvas", "toolbar" })
    {
        rig.click (textBox, { 20.0f, 12.0f });
        pump (20);
        CHECK (textBox.hasKeyboardFocus (true));

        auto& target = rig.get (elsewhere);
        const auto at = juce::String (elsewhere) == "canvas"    ? rig.onEmptyCanvas()
                      : juce::String (elsewhere) == "inspector" ? juce::Point<float> (140.0f, static_cast<float> (target.getHeight()) - 20.0f)
                      : juce::String (elsewhere) == "toolbar"   ? juce::Point<float> (650.0f, 22.0f)
                                                                : target.getLocalBounds().toFloat().getCentre();
        rig.click (target, at);
        pump (20);
        CHECK (! textBox.hasKeyboardFocus (true) && ! rig.typing());
        CHECK (! rig.uses (space));

        if (textBox.hasKeyboardFocus (true))
            std::printf ("    after a click on %s\n", elsewhere);

        rig.editor->selectObject (rig.textId);
        pump (50);
    }

    setParameter (*rig.processor, params::id::repeat, 0.0f);

    // Add text: the new object's text box has the focus, ready for typing; Return gives it back.
    const auto before = rig.processor->getDocument().objects.size();
    rig.click ("add-text");
    pump (50);
    CHECK (rig.processor->getDocument().objects.size() == before + 1);
    CHECK (textBox.isShowing() && textBox.hasKeyboardFocus (true) && rig.typing());
    rig.type ("NEW ONE");
    CHECK (rig.uses (returnKey));
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));
    CHECK (rig.processor->getDocument().find (rig.editor->getSelectedObject())->text == "NEW ONE");

    // The object deleted while its text box is typed in (from the host's side, say): the text
    // box goes, and the focus does not stay behind on some button.
    rig.click (textBox, { 20.0f, 12.0f });
    pump (20);
    CHECK (textBox.hasKeyboardFocus (true));
    rig.processor->setDocument (ArtDocument());
    pump (100);
    CHECK (! textBox.isShowing());
    CHECK (! rig.uses (space) && ! rig.uses (returnKey));
    CHECK (rig.focused() == nullptr || ! dynamic_cast<juce::Button*> (rig.focused()));
}

ARTFREQ_TEST (uiKeysNumberFieldsGiveTheFocusBack)
{
    if (! canDriveAWindow())
        return;

    Rig rig;
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    // The start bar field (At bar): a click to type, Return sets, Esc cancels, a click elsewhere
    // sets; each time the focus goes back to the canvas.
    setParameter (*rig.processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    pump (100);
    auto& startBar = rig.get ("start-bar");
    CHECK (startBar.isShowing());

    rig.click ("start-bar");
    pump (20);
    CHECK (rig.editing());
    rig.type ("12");
    CHECK (rig.uses (returnKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 12.0f, 1.0e-4);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space) && ! rig.uses (returnKey));

    // The space bar sets what was typed, as Return does, and goes on to the DAW.
    rig.click ("start-bar");
    pump (20);
    rig.type ("9");
    CHECK (! rig.uses (space));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 9.0f, 1.0e-4);
    CHECK (! rig.editing() && rig.hasFocus ("canvas"));

    rig.click ("start-bar");
    pump (20);
    rig.type ("40");
    CHECK (rig.uses (escapeKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 9.0f, 1.0e-4);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));

    rig.click ("start-bar");
    pump (20);
    rig.type ("7");
    rig.click ("repeat");
    pump (20);
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 7.0f, 1.0e-4);
    CHECK (! rig.editing() && ! rig.uses (space));
    setParameter (*rig.processor, params::id::repeat, 0.0f);
    pump (50);

    rig.click ("start-bar");
    pump (20);
    rig.type ("3");
    CHECK (rig.uses (tabKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 3.0f, 1.0e-4);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));

    // A knob's value: a double-click to type (its label is modal meanwhile), Return sets it,
    // and nothing stays modal or focused behind it.
    auto& level = rig.get ("level");
    rig.doubleClick (level, { 38.0f, 72.0f });
    pump (20);
    CHECK (rig.editing() && juce::Component::getCurrentlyModalComponent() != nullptr);
    rig.type ("-12");
    CHECK (rig.uses (returnKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -12.0f, 1.0e-3);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));

    rig.doubleClick (level, { 38.0f, 72.0f });
    pump (20);
    rig.type ("-30");
    CHECK (rig.uses (escapeKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -12.0f, 1.0e-3);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas") && ! rig.uses (space));

    rig.doubleClick (level, { 38.0f, 72.0f });
    pump (20);
    rig.type ("-20");
    CHECK (rig.uses (tabKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -20.0f, 1.0e-3);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas") && ! rig.uses (space));

    // The space bar sets it and goes on to the DAW: the label stops being modal at once, so
    // macOS passes the key on.
    rig.doubleClick (level, { 38.0f, 72.0f });
    pump (20);
    rig.type ("-24");
    CHECK (! rig.uses (space));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -24.0f, 1.0e-3);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas") && ! rig.editing());

    // A bar's value in the inspector (Intensity): a click to type, Return sets it. JUCE's own
    // label closes it; the focus comes home a moment later.
    rig.click (rig.canvas(), rig.onText());
    pump (50);
    auto& intensity = rig.get ("inspector-intensity");
    CHECK (intensity.isShowing());
    rig.click ("inspector-intensity");
    pump (20);
    CHECK (rig.editing());
    rig.type ("50");
    CHECK (rig.uses (returnKey));
    pump (50);
    CHECK_NEAR (rig.processor->getDocument().find (rig.textId)->intensity, 0.5f, 1.0e-3);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space) && ! rig.uses (returnKey));

    // ... and a click on it, then the space bar: set, and the DAW's.
    rig.click ("inspector-intensity");
    pump (20);
    rig.type ("70");
    CHECK (! rig.uses (space));
    pump (50);
    CHECK_NEAR (rig.processor->getDocument().find (rig.textId)->intensity, 0.7f, 1.0e-3);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr && ! rig.editing() && rig.hasFocus ("canvas"));
}

ARTFREQ_TEST (uiKeysDeleteActsOnTheSelectionInTheLiveView)
{
    // An object selected (its settings in the inspector, or in ArtFreq FX's drawer) stays
    // selected in the live view, and Delete and the arrows act on it there as on the canvas; they
    // must not go on to the DAW, which might delete whatever is selected there. With nothing
    // selected they are the DAW's.
    if (! canDriveAWindow())
        return;

    for (const auto variant : { ArtFreqProcessor::Variant::instrument, ArtFreqProcessor::Variant::effect })
    {
        Rig rig (variant);
        CHECK (rig.valid());

        if (! rig.valid())
            return;

        rig.click (rig.canvas(), rig.onText());
        pump (40);
        CHECK (rig.editor->getSelectedObject() == rig.textId);
        CHECK (variant == ArtFreqProcessor::Variant::instrument || rig.editor->isDrawerOpen());

        rig.editor->showLiveView (true);
        pump (40);
        CHECK (rig.get ("live-view").isShowing() && rig.editor->getSelectedObject() == rig.textId);

        const auto before = rig.processor->getDocument().find (rig.textId)->startTime;
        CHECK (rig.uses (juce::KeyPress (juce::KeyPress::rightKey)));
        CHECK_GREATER (rig.processor->getDocument().find (rig.textId)->startTime, before);
        CHECK (rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));
        CHECK (rig.text() == "(gone)" && rig.editor->getSelectedObject().isNull());

        CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)) && ! rig.uses (juce::KeyPress (juce::KeyPress::backspaceKey)));
        CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::leftKey)) && ! rig.uses (space));
        CHECK (rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) && rig.text() == "HELLO");
    }
}

ARTFREQ_TEST (uiKeysTabAndThePanels)
{
    if (! canDriveAWindow())
        return;

    Rig rig;
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    // Tab is the DAW's too: it comes back unused (JUCE would move the focus to the next control
    // and call it used), and the focus is where it was a moment later.
    rig.click (rig.canvas(), rig.onEmptyCanvas());
    CHECK (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (tabKey));
    pump (50);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));
    CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, '\t')));
    pump (50);
    CHECK (rig.hasFocus ("canvas"));

    auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));

    if (list != nullptr)
    {
        rig.click (*list, list->getRowBounds (0).toFloat().getCentre().withX (60.0f));
        CHECK (rig.hasFocus ("layer-list"));
        CHECK (! rig.uses (tabKey));
        pump (50);
        CHECK (rig.hasFocus ("layer-list"));
        CHECK (rig.uses (escapeKey));   // the list's own: deselects
    }

    // A panel has the keys while it is open: Esc closes it, the rest is the DAW's; closed, the
    // canvas has the focus again.
    for (const auto* button : { "settings", "help" })
    {
        const auto panelId = juce::String (button) + "-panel";
        rig.click (button);
        pump (50);
        CHECK (rig.get (panelId).isShowing() && rig.hasFocus (panelId));
        CHECK (! rig.uses (space) && ! rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
        CHECK (! rig.uses (tabKey));
        pump (50);
        CHECK (rig.hasFocus (panelId));
        CHECK (rig.uses (escapeKey));
        pump (20);
        CHECK (! rig.get (panelId).isVisible() && rig.hasFocus ("canvas") && ! rig.uses (space));
    }

    // In the settings, a field typed into and set: the focus is the panel's again.
    rig.click ("settings");
    pump (50);
    rig.click ("range-low");
    pump (20);
    CHECK (rig.editing());
    rig.type ("30");
    CHECK (rig.uses (returnKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::rangeLow), 30.0f, 1.0e-3);
    CHECK (rig.hasFocus ("settings-panel") && ! rig.uses (space));
    CHECK (rig.uses (escapeKey));

    // The live view: no canvas to hold the focus, and still nothing takes the space bar.
    rig.editor->showLiveView (true);
    pump (50);
    CHECK (! rig.uses (space) && ! rig.uses (returnKey));
    rig.click ("repeat");
    CHECK (! rig.uses (space));
    rig.editor->showLiveView (false);
}

ARTFREQ_TEST (uiKeysTheActivationScreen)
{
    // A locked plug-in's window: the activation screen is the focus's home. The space bar, and
    // the drawing's keys (which have nothing to act on), go on to the DAW; the key field takes
    // typing only while it is clicked into, as key presses (no system text input), shows it in
    // groups of five, and the space bar ends the typing and goes on too. Return in the field
    // activates; Return on the screen continues after the success.
    if (! canDriveAWindow())
        return;

    for (const auto variant : { ArtFreqProcessor::Variant::instrument, ArtFreqProcessor::Variant::effect })
    {
        const licensetest::ScopedLockedLicensing licensing;
        Rig rig (variant);
        CHECK (rig.valid());

        if (! rig.valid())
            continue;

        auto& screen = rig.get ("activation");
        auto& field = dynamic_cast<juce::TextEditor&> (rig.get ("licence-key"));
        CHECK (screen.isShowing());

        // A click on the dimmed window: the screen's, which has the focus.
        rig.click (screen, { 12.0f, 12.0f });
        CHECK (rig.hasFocus ("activation") && ! rig.editing());
        CHECK (! rig.uses (space));
        CHECK (! rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
        CHECK (! rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z')));
        CHECK (! rig.uses (returnKey));   // nothing typed
        CHECK (rig.hasFocus ("activation"));

        // A click on Activate with nothing typed: says what to do; the focus stays home.
        rig.click ("activate");
        pump (50);
        CHECK (! rig.editing() && ! rig.uses (space));

        // Into the field: typing, as key presses, formatted.
        rig.click ("licence-key");
        CHECK (rig.editing() && ! rig.typing());
        rig.type ("y1f2fjvxbx");
        CHECK (field.getText() == "Y1F2F-JVXBX");

        // The space bar: the typing ends, the DAW plays, what was typed stays.
        CHECK (! rig.uses (space));
        CHECK (! rig.editing() && rig.hasFocus ("activation"));
        CHECK (field.getText() == "Y1F2F-JVXBX");

        // Back in, the rest, and Return: sent; the focus back home.
        rig.click ("licence-key");
        field.moveCaretToEnd();
        rig.type ("wcnvbvczkj");
        CHECK (field.getText() == "Y1F2F-JVXBX-WCNVB-VCZKJ");

        if (variant == ArtFreqProcessor::Variant::instrument)
            licensing.server->reply (licensetest::activateProAsArtFreq);

        licensing.server->reply (licensetest::activateProFresh);
        CHECK (rig.uses (returnKey));
        CHECK (! rig.editing());
        CHECK (licensetest::pumpUntil ([&] { return rig.processor->isLicensed(); }));
        pump (50);

        // Return on the success: continue, and the canvas has the focus again.
        CHECK (screen.isShowing());
        CHECK (rig.uses (returnKey));
        pump (30);
        CHECK (! screen.isVisible() && rig.hasFocus ("canvas"));
        CHECK (! rig.uses (space));
    }
}

ARTFREQ_TEST (uiKeysPastingAKeyReplacesWhatTheFieldHolds)
{
    // After a failed attempt the field still holds the key that failed. The right key, copied
    // from the email and pasted (Cmd+V, wherever the caret is), must replace it, not go in
    // beside it: otherwise Activate sends the old key, or a mix of the two. A piece of a key that
    // completes what was typed goes in at the caret.
    if (! canDriveAWindow())
        return;

    const licensetest::ScopedLockedLicensing licensing;
    Rig rig (ArtFreqProcessor::Variant::effect);
    CHECK (rig.valid());

    if (! rig.valid())
        return;

    auto& field = dynamic_cast<juce::TextEditor&> (rig.get ("licence-key"));
    const juce::KeyPress paste ('v', juce::ModifierKeys::commandModifier, 0);
    const juce::String typo ("Y1F2F-JVXBX-WCNVB-VCZKK"), right ("Y1F2F-JVXBX-WCNVB-VCZKJ");

    const auto pasteInto = [&] (const juce::String& held, int caret, const juce::String& clipboard)
    {
        rig.click ("licence-key");
        field.setText (held, false);
        field.setCaretPosition (caret < 0 ? held.length() : caret);
        juce::SystemClipboard::copyTextToClipboard (clipboard);
        CHECK (rig.uses (paste));
        return field.getText();
    };

    // The typo was refused ("check for typos"); the right key pasted after it, the caret at the end.
    rig.click ("licence-key");
    rig.type (typo.removeCharacters ("-"));
    CHECK (rig.uses (returnKey));
    CHECK (licensetest::pumpUntil ([&] { return field.getText() == typo && ! rig.processor->getLicense().isBusy(); }));
    CHECK (pasteInto (typo, -1, right) == right);

    // The caret where the click left it, in the middle; the key with the email's words.
    CHECK (pasteInto (typo, 11, "Your license key: " + right) == right);

    // Part of the key typed, then the whole key pasted.
    CHECK (pasteInto ("Y1F2F-JV", -1, right) == right);

    // Into an empty field, a mistyped key with the email's words: the mistyped key, as it is.
    CHECK (pasteInto ({}, -1, "Your license key: y1f2f-jvxbx-wcnvb-vczkk") == typo);

    // The last group, pasted after the first three: it completes the key.
    CHECK (pasteInto ("Y1F2F-JVXBX-WCNVB-", -1, "VCZKJ") == right);

    // And what is pasted is what is sent (the typo was caught here, without the server).
    CHECK (licensing.server->numSent() == 0);

    licensing.server->reply (licensetest::activateProFresh);
    pasteInto (typo, 3, right);
    CHECK (rig.uses (returnKey));
    CHECK (licensetest::pumpUntil ([&] { return rig.processor->isLicensed(); }));
    CHECK (licensing.server->numSent() == 1
           && licensing.server->sent().front().json.getProperty ("key", {}).toString() == right);
}
