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
//
// Much of what a click or a key does comes a moment later: the focus moved home, a text box
// closed, a value set, the drawer or a strip shown (on the editor's timers, change messages and
// calls posted to the message loop). A slow machine does all of that much later than a fast one,
// so each check of it waits for it (CHECK_EVENTUALLY, WaitFor.h), and so does each step that needs
// it done (typing waits for the field to be typed in, a key for the focus to have left a button
// that would take it). Whether a key was used is known at once, and is checked at once.

#include "LicenseTestSupport.h"
#include "ProcessorTestUtils.h"
#include "TestFramework.h"
#include "WaitFor.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/KeyFocus.h"
#include "ui/LayerList.h"
#include "ui/Licensing.h"

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
        testwait::pumpUntil ([this] { return editor->isShowing(); });

        // As when the user clicks into the plug-in's window for the first time: the display gives
        // the window the keyboard (a slow machine takes its time), and JUCE gives the editor the focus.
        editor->grabKeyboardFocus();
        pump (50);
        testwait::pumpUntil ([this] { return valid(); });

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

    /** The focus is somewhere, and not on a button (which would take the space bar): where the
        editor puts it a moment after a click. */
    bool focusIsOffButtons() const   { return focused() != nullptr && dynamic_cast<juce::Button*> (focused()) == nullptr; }

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
        testwait::pumpPosted();   // (what the step before posted is dealt with first, as it would be by then)
        const auto wasTyping = typing();
        auto used = peer()->handleKeyUpOrDown (true);
        used = peer()->handleKeyPress (key) || used;
        used = peer()->handleKeyUpOrDown (false) || used;
        used = used || wasTyping || juce::Component::getCurrentlyModalComponent() != nullptr;

        pump (20);   // (a text box acts on Return and Esc a moment later)
        testwait::pumpPosted();
        return used;
    }

    /** Types into whatever has the focus, key by key. What the click before set in motion (the
        selection's change message, which shows the object's text in its box) is dealt with first:
        a person types that much later, and on a slow machine a fixed wait is not enough. */
    void type (const juce::String& text)
    {
        testwait::pumpPosted();

        for (const auto character : text)
        {
            peer()->handleKeyUpOrDown (true);
            peer()->handleKeyPress (juce::KeyPress (character == ' ' ? juce::KeyPress::spaceKey : static_cast<int> (character),
                                                    juce::ModifierKeys(), character));
            peer()->handleKeyUpOrDown (false);
        }

        pump (20);   // (a text box reports its changes a moment later)
        testwait::pumpPosted();
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
        CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
        CHECK (! rig.uses (space));

        // Buttons, chips, segmented controls (Redo is disabled, and BPM follows the host: clicks
        // on them do nothing, and still count).
        for (const auto* id : { "tool-brush", "tool-select", "repeat", "trigger", "view-switch", "redo", "bpm", "preview",
                                "lows" })
        {
            rig.click (id);
            CHECK_EVENTUALLY (rig.focusIsOffButtons());
            const auto used = rig.uses (space);
            CHECK (! used);

            if (used)
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
            CHECK_EVENTUALLY (rig.get (id).isShowing());
            rig.click (id);
            CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() != nullptr);   // (the menu opens a moment later)
            juce::PopupMenu::dismissAllActiveMenus();
            CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && rig.focused() != nullptr
                              && dynamic_cast<juce::ComboBox*> (rig.focused()) == nullptr && rig.focusIsOffButtons());
            CHECK (! rig.uses (space) && ! rig.uses (returnKey) && ! rig.uses (juce::KeyPress (juce::KeyPress::downKey)));
            CHECK (rig.focused() != nullptr && ! dynamic_cast<juce::ComboBox*> (rig.focused())
                   && ! dynamic_cast<juce::Button*> (rig.focused()));
        }

        // A knob turned, a field dragged.
        auto& density = rig.get ("density");
        rig.drag (density, { 38.0f, 40.0f }, { 38.0f, 20.0f });
        CHECK_EVENTUALLY (rig.focusIsOffButtons());
        CHECK (! rig.uses (space));

        rig.click ("view-switch");   // (back to the canvas, whichever half was clicked)
        rig.editor->showLiveView (false);

        // A row of the layer list: the list has the focus, and no use for the space bar.
        auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));
        CHECK_EVENTUALLY (list != nullptr && list->isShowing());

        if (list != nullptr)
        {
            rig.click (*list, list->getRowBounds (0).toFloat().getCentre().withX (60.0f));
            CHECK_EVENTUALLY (rig.hasFocus ("layer-list"));
            CHECK (! rig.uses (space));
        }

        // The keys the editor does use are still its own.
        rig.click (rig.canvas(), rig.onEmptyCanvas());
        CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
        CHECK (rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
        CHECK (rig.uses (juce::KeyPress ('v', juce::ModifierKeys(), 'v')));
        CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));   // nothing selected: not ours
        CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::leftKey)));
        CHECK (! rig.uses (returnKey));
        CHECK (! rig.uses (juce::KeyPress ('a', juce::ModifierKeys(), 'a')));

        rig.click (rig.canvas(), rig.onText());
        CHECK_EVENTUALLY (rig.editor->getSelectedObject() == rig.textId && rig.hasFocus ("canvas"));
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

    // (The focus leaves a clicked button a moment later; then the space bar is pressed.)
    const auto spaceIsFree = [&] (const char* after)
    {
        CHECK_EVENTUALLY (rig.focusIsOffButtons());
        const auto used = rig.uses (space);
        CHECK (! used);
        CHECK (rig.focused() != nullptr && ! dynamic_cast<juce::Button*> (rig.focused()));

        if (used)
            std::printf ("    after %s\n", after);
    };

    CHECK (! rig.uses (space));   // as the window opens

    rig.click (rig.canvas(), rig.onEmptyCanvas());
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // The bars: each control only clicked, and their backgrounds.
    for (const auto* id : { "tool-brush", "brush-strip", "tool-select", "live", "repeat", "mode", "header", "footer", "layers",
                            "inspector", "inspector-close" })
    {
        auto& target = rig.get (id);
        CHECK_EVENTUALLY (target.isShowing());
        const auto at = juce::String (id) == "brush-strip" ? juce::Point<float> (680.0f, 16.0f)
                      : juce::String (id) == "header"      ? juce::Point<float> (565.0f, 38.0f)
                      : juce::String (id) == "footer"      ? juce::Point<float> (400.0f, 4.0f)
                      : juce::String (id) == "inspector"   ? juce::Point<float> (120.0f, static_cast<float> (target.getHeight()) - 30.0f)
                                                           : target.getLocalBounds().toFloat().getCentre();
        rig.click (target, at);
        spaceIsFree (id);

        // (The click did what it does.)
        if (juce::String (id) == "tool-brush")
            CHECK_EVENTUALLY (dynamic_cast<juce::Button&> (rig.get (id)).getToggleState() && rig.get ("brush-strip").isVisible());

        if (juce::String (id) == "repeat")
            CHECK_NEAR (getParameter (*rig.processor, params::id::repeat), 1.0f, 1.0e-4);

        if (juce::String (id) == "layers")
            CHECK_EVENTUALLY (rig.editor->isLayerListShown());

        if (juce::String (id) == "inspector-close")
            CHECK_EVENTUALLY (! rig.editor->isDrawerOpen());

        rig.editor->showLiveView (false);
        setParameter (*rig.processor, params::id::repeat, 0.0f);
        setParameter (*rig.processor, params::id::mode, 0.0f);
        CHECK_EVENTUALLY (rig.get ("canvas").isShowing() && ! rig.get ("sound-strip").isVisible());
    }

    // Length's menu has the keys while it is open; closed, nothing is modal or left on the box.
    rig.click ("length");
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() != nullptr);   // (the menu opens a moment later)
    juce::PopupMenu::dismissAllActiveMenus();
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && rig.focused() != nullptr
                      && dynamic_cast<juce::ComboBox*> (rig.focused()) == nullptr);
    CHECK (! rig.uses (space) && ! rig.uses (returnKey) && ! rig.uses (juce::KeyPress (juce::KeyPress::downKey)));
    CHECK (rig.focused() != nullptr && ! dynamic_cast<juce::ComboBox*> (rig.focused()));

    // Depth dragged; in a Draw mode the Sound strip: Lows clicked, Level dragged, then typed.
    rig.drag (rig.get ("depth"), { 30.0f, 14.0f }, { 30.0f, 4.0f });
    spaceIsFree ("dragging Depth");

    setParameter (*rig.processor, params::id::mode, 3.0f);
    CHECK_EVENTUALLY (rig.get ("lows").isShowing() && rig.get ("level").isShowing());
    rig.click ("lows");
    spaceIsFree ("a click on Lows");
    rig.drag (rig.get ("level"), { 30.0f, 12.0f }, { 30.0f, 4.0f });
    spaceIsFree ("dragging Level");

    // A single click on a field starts typing in it, and the space bar, which has no place in a
    // number, ends the typing (what was typed is set) and goes on to the DAW: a click on a value
    // and then the space bar plays the DAW, as everywhere else. Return sets a value and is kept.
    for (const auto* id : { "level", "density", "contrast" })
    {
        rig.click (id);
        CHECK_EVENTUALLY (rig.editing() && ! rig.typing());
        CHECK (! rig.uses (space));
        CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("canvas"));
        CHECK (! rig.uses (space));
    }

    rig.click ("level");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("-12");
    CHECK (! rig.uses (space));
    CHECK_EVENTUALLY (std::abs (getParameter (*rig.processor, params::id::level) + 12.0f) < 1.0e-3f);
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -12.0f, 1.0e-3);
    CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("canvas"));

    rig.click ("level");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("-9");
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (std::abs (getParameter (*rig.processor, params::id::level) + 9.0f) < 1.0e-3f);
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -9.0f, 1.0e-3);
    CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // The strip going while one of its values is typed (automation changes the mode) leaves no
    // text box behind.
    rig.click ("density");
    CHECK_EVENTUALLY (rig.editing());
    setParameter (*rig.processor, params::id::mode, 0.0f);
    CHECK_EVENTUALLY (! rig.get ("sound-strip").isVisible() && ! rig.editing());

    rig.click ("depth");
    CHECK_EVENTUALLY (rig.editing());
    CHECK (! rig.uses (space));
    CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("canvas"));
    spaceIsFree ("the Sound strip went while a value was typed");

    // The settings: the timing's controls clicked, the start bar typed; the focus stays with the
    // panel (Esc closes it), and comes back to the canvas when it closes.
    rig.click ("settings");
    CHECK_EVENTUALLY (rig.hasFocus ("settings-panel"));
    CHECK (! rig.uses (space));

    for (const auto* id : { "tempo-source", "start-at" })
    {
        auto& control = rig.get (id);
        rig.click (control, { static_cast<float> (control.getWidth()) * 0.75f, 14.0f });
        CHECK_EVENTUALLY (rig.hasFocus ("settings-panel"));
        CHECK (! rig.uses (space));
    }

    CHECK_EVENTUALLY (rig.get ("start-bar").isShowing() && rig.get ("bpm").isShowing());
    rig.click ("start-bar");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("12");
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (std::abs (getParameter (*rig.processor, params::id::startBar) - 12.0f) < 1.0e-4f);
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 12.0f, 1.0e-4);
    CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("settings-panel"));
    CHECK (! rig.uses (space));

    rig.click ("bpm");
    CHECK_EVENTUALLY (rig.editing());
    CHECK (! rig.uses (space));
    CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("settings-panel"));
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (! rig.get ("settings-panel").isVisible() && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // The live view (no canvas to hold the focus).
    rig.click ("live");
    CHECK_EVENTUALLY (rig.get ("live-view").isShowing());
    spaceIsFree ("a click on Live");
    rig.click ("live");
    CHECK_EVENTUALLY (rig.get ("canvas").isShowing());

    // The layer list in the drawer: a row has the focus; Esc deselects, then closes the list, and
    // then is no key of ours.
    rig.click ("layers");
    auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));
    CHECK_EVENTUALLY (list != nullptr && list->isShowing());

    if (list != nullptr)
    {
        rig.click (*list, list->getRowBounds (0).toFloat().getCentre().withX (60.0f));
        CHECK_EVENTUALLY (rig.hasFocus ("layer-list") && rig.editor->getSelectedObject() == rig.textId);
        CHECK (! rig.uses (space));
        CHECK (rig.uses (escapeKey));
        CHECK_EVENTUALLY (rig.editor->getSelectedObject().isNull() && rig.editor->isLayerListShown());
        CHECK (rig.uses (escapeKey));
        CHECK_EVENTUALLY (! rig.editor->isDrawerOpen() && rig.hasFocus ("canvas"));
        CHECK (! rig.uses (escapeKey) && ! rig.uses (space));
    }

    // The keys the editor does use are still its own.
    rig.click (rig.canvas(), rig.onEmptyCanvas());
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
    CHECK_EVENTUALLY (rig.get ("brush-strip").isVisible());
    CHECK (rig.uses (juce::KeyPress ('v', juce::ModifierKeys(), 'v')));
    CHECK_EVENTUALLY (! rig.get ("brush-strip").isVisible());
    CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));   // nothing selected: not ours
    CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (! rig.uses (returnKey));
    CHECK (! rig.uses (tabKey));

    rig.click (rig.canvas(), rig.onText());
    CHECK_EVENTUALLY (rig.editor->getSelectedObject() == rig.textId && rig.editor->isDrawerOpen() && rig.hasFocus ("canvas"));
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (rig.text() == "(gone)");
    CHECK_EVENTUALLY (! rig.editor->isDrawerOpen());
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
    CHECK_EVENTUALLY (rig.editor->getSelectedObject() == rig.textId && rig.editor->isDrawerOpen() && ! rig.editor->isDrawerOnLeft()
                      && textBox.isShowing());
    pump (40);   // (and a moment more, for the focus to go where it should not)
    CHECK (textBox.isShowing() && ! textBox.hasKeyboardFocus (true) && rig.hasFocus ("canvas") && ! rig.uses (space));
    CHECK (rig.text() == "HELLO");

    // Its close button: closed, still selected, the canvas has the keys.
    rig.click ("inspector-close");
    CHECK_EVENTUALLY (! rig.editor->isDrawerOpen() && rig.editor->getSelectedObject() == rig.textId && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // A click on the selected object opens it again.
    rig.click (rig.canvas(), rig.onText());
    CHECK_EVENTUALLY (rig.editor->isDrawerOpen() && rig.editor->getSelectedObject() == rig.textId);

    // Esc: nothing selected, no drawer.
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (rig.editor->getSelectedObject().isNull() && ! rig.editor->isDrawerOpen());

    // The text on the right, where the drawer would open: it opens on the left instead, clear of
    // the mouse and of the text.
    rig.click (rig.canvas(), onRight);
    CHECK_EVENTUALLY (rig.editor->getSelectedObject() == rightId && rig.editor->isDrawerOpen() && rig.editor->isDrawerOnLeft());
    CHECK (! drawer.getBounds().contains (rig.editor->getLocalPoint (&rig.canvas(), onRight).roundToInt()));
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (rig.editor->getSelectedObject().isNull() && ! rig.editor->isDrawerOpen());

    // So a double-click there selects the text and goes to typing (its second click was not
    // caught by a drawer that had opened under the mouse).
    rig.doubleClick (rig.canvas(), onRight);
    CHECK_EVENTUALLY (rig.editor->getSelectedObject() == rightId && textBox.hasKeyboardFocus (true) && rig.typing());
    rig.type ("PLAY IT");
    CHECK_EVENTUALLY (rightText() == "PLAY IT");
    CHECK (rig.uses (space));   // while typing, the space bar types
    CHECK_EVENTUALLY (rightText() == "PLAY IT ");
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (! rig.typing() && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // Dragged to the left, under its drawer: let go, the drawer moves to the other side.
    rig.drag (rig.canvas(), onRight, { area.getX() + 0.12f * area.getWidth(), onRight.y });
    const auto* moved = rig.processor->getDocument().find (rightId);
    CHECK (moved != nullptr && moved->startTime < 0.2f);
    CHECK_EVENTUALLY (rig.editor->isDrawerOpen() && ! rig.editor->isDrawerOnLeft());
    CHECK (! rig.uses (space));

    // + Text: the drawer opens on the new text, with the focus in its text box.
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (! rig.editor->isDrawerOpen());
    const auto before = rig.processor->getDocument().objects.size();
    rig.click ("add-text");
    CHECK_EVENTUALLY (rig.processor->getDocument().objects.size() == before + 1 && rig.editor->isDrawerOpen());
    CHECK_EVENTUALLY (textBox.isShowing() && textBox.hasKeyboardFocus (true) && rig.typing());
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));
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
    CHECK_EVENTUALLY (rig.editor->getSelectedObject() == rig.textId && textBox.isShowing());
    pump (50);   // (and a moment more, for the focus to go where it should not)
    CHECK (textBox.isShowing() && ! textBox.hasKeyboardFocus (true));
    CHECK (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));
    rig.type ("x y");
    CHECK (rig.text() == "HELLO");

    // The same from the layer list: a click on its row, then Return (or a double-click) for its
    // settings. The list goes, and the focus with it: to the canvas, not into the text box.
    rig.click (rig.canvas(), rig.onEmptyCanvas());
    auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));
    CHECK_EVENTUALLY (list != nullptr && list->isShowing());

    if (list == nullptr)
        return;

    const auto row = list->getRowBounds (0).toFloat().getCentre().withX (60.0f);
    rig.click (*list, row);
    CHECK_EVENTUALLY (rig.hasFocus ("layer-list") && rig.editor->isLayerListShown());
    CHECK (rig.uses (returnKey));   // the list's own key: opens the settings
    CHECK_EVENTUALLY (! rig.editor->isLayerListShown() && textBox.isShowing() && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space) && ! rig.uses (returnKey));

    rig.click (rig.canvas(), rig.onEmptyCanvas());
    CHECK_EVENTUALLY (list->isShowing());
    rig.doubleClick (*list, row);
    CHECK_EVENTUALLY (! rig.editor->isLayerListShown() && textBox.isShowing());
    pump (50);   // (and a moment more, for the focus to go where it should not)
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
    CHECK_EVENTUALLY (textBox.hasKeyboardFocus (true) && rig.typing());
    rig.type ("PLAY IT");
    CHECK_EVENTUALLY (rig.text() == "PLAY IT");

    // Return ends the typing: the text stays, the focus is the canvas's, the space bar the DAW's.
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (rig.hasFocus ("canvas") && ! rig.typing());
    CHECK (! rig.uses (space));
    CHECK (rig.text() == "PLAY IT");

    // Esc does the same, keeping what was typed (one undo step takes the typing back).
    rig.doubleClick (rig.canvas(), rig.onText());
    CHECK_EVENTUALLY (textBox.hasKeyboardFocus (true));
    rig.type ("NOW");
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));
    CHECK_EVENTUALLY (rig.text() == "NOW");
    CHECK (rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
    CHECK_EVENTUALLY (rig.text() == "PLAY IT");

    // Shift+Return (or Alt+Return) is a new line, and the typing goes on.
    rig.click (textBox, { 20.0f, 12.0f });
    CHECK_EVENTUALLY (textBox.hasKeyboardFocus (true));
    rig.type ("A");
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::shiftModifier, '\r')));
    rig.type ("B");
    CHECK (rig.uses (juce::KeyPress (juce::KeyPress::returnKey, juce::ModifierKeys::altModifier, '\r')));
    rig.type ("C");
    CHECK (textBox.hasKeyboardFocus (true));
    CHECK_EVENTUALLY (rig.text().contains ("A\nB\nC"));

    // Tab ends it too (and is used: it did something).
    CHECK (rig.uses (tabKey));
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // A click anywhere else ends it: on a control that takes no focus (a knob, a chip, a
    // segmented control, the inspector's background), on a button, on the canvas.
    for (const auto* elsewhere : { "density", "repeat", "trigger", "inspector", "inspector-fill", "canvas", "toolbar" })
    {
        rig.click (textBox, { 20.0f, 12.0f });
        CHECK_EVENTUALLY (textBox.hasKeyboardFocus (true));

        auto& target = rig.get (elsewhere);
        const auto at = juce::String (elsewhere) == "canvas"    ? rig.onEmptyCanvas()
                      : juce::String (elsewhere) == "inspector" ? juce::Point<float> (140.0f, static_cast<float> (target.getHeight()) - 20.0f)
                      : juce::String (elsewhere) == "toolbar"   ? juce::Point<float> (650.0f, 22.0f)
                                                                : target.getLocalBounds().toFloat().getCentre();
        rig.click (target, at);
        CHECK_EVENTUALLY (! textBox.hasKeyboardFocus (true) && ! rig.typing()
                          && (rig.focused() == nullptr || ! dynamic_cast<juce::Button*> (rig.focused())));
        CHECK (! rig.uses (space));

        if (textBox.hasKeyboardFocus (true))
            std::printf ("    after a click on %s\n", elsewhere);

        rig.editor->selectObject (rig.textId);
        CHECK_EVENTUALLY (textBox.isShowing());
    }

    setParameter (*rig.processor, params::id::repeat, 0.0f);

    // Add text: the new object's text box has the focus, ready for typing; Return gives it back.
    const auto before = rig.processor->getDocument().objects.size();
    rig.click ("add-text");
    CHECK_EVENTUALLY (rig.processor->getDocument().objects.size() == before + 1);
    CHECK_EVENTUALLY (textBox.isShowing() && textBox.hasKeyboardFocus (true) && rig.typing());
    rig.type ("NEW ONE");
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));
    const auto newText = [&]
    {
        const auto* object = rig.processor->getDocument().find (rig.editor->getSelectedObject());
        return object != nullptr ? object->text : juce::String ("(none)");
    };
    CHECK_EVENTUALLY (newText() == "NEW ONE");

    // The object deleted while its text box is typed in (from the host's side, say): the text
    // box goes, and the focus does not stay behind on some button.
    rig.click (textBox, { 20.0f, 12.0f });
    CHECK_EVENTUALLY (textBox.hasKeyboardFocus (true));
    rig.processor->setDocument (ArtDocument());
    CHECK_EVENTUALLY (! textBox.isShowing() && (rig.focused() == nullptr || ! dynamic_cast<juce::Button*> (rig.focused())));
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

    const auto parameterIs = [&] (const char* id, float value, float tolerance)
    {
        return std::abs (getParameter (*rig.processor, id) - value) <= tolerance;
    };

    // The start bar field (At bar): a click to type, Return sets, Esc cancels, a click elsewhere
    // sets; each time the focus goes back to the canvas.
    setParameter (*rig.processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    auto& startBar = rig.get ("start-bar");
    CHECK_EVENTUALLY (startBar.isShowing());

    rig.click ("start-bar");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("12");
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (parameterIs (params::id::startBar, 12.0f, 1.0e-4f));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 12.0f, 1.0e-4);
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space) && ! rig.uses (returnKey));

    // The space bar sets what was typed, as Return does, and goes on to the DAW.
    rig.click ("start-bar");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("9");
    CHECK (! rig.uses (space));
    CHECK_EVENTUALLY (parameterIs (params::id::startBar, 9.0f, 1.0e-4f));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 9.0f, 1.0e-4);
    CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("canvas"));

    rig.click ("start-bar");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("40");
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("canvas"));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 9.0f, 1.0e-4);
    CHECK (! rig.uses (space));

    rig.click ("start-bar");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("7");
    rig.click ("repeat");
    CHECK_EVENTUALLY (parameterIs (params::id::startBar, 7.0f, 1.0e-4f));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 7.0f, 1.0e-4);
    CHECK_EVENTUALLY (! rig.editing() && rig.focusIsOffButtons());
    CHECK (! rig.uses (space));
    setParameter (*rig.processor, params::id::repeat, 0.0f);
    CHECK_EVENTUALLY (startBar.isShowing());

    rig.click ("start-bar");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("3");
    CHECK (rig.uses (tabKey));
    CHECK_EVENTUALLY (parameterIs (params::id::startBar, 3.0f, 1.0e-4f));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 3.0f, 1.0e-4);
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // A knob's value: a double-click to type (its label is modal meanwhile), Return sets it,
    // and nothing stays modal or focused behind it.
    auto& level = rig.get ("level");
    rig.doubleClick (level, { 38.0f, 72.0f });
    CHECK_EVENTUALLY (rig.editing() && juce::Component::getCurrentlyModalComponent() != nullptr);
    rig.type ("-12");
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (parameterIs (params::id::level, -12.0f, 1.0e-3f));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -12.0f, 1.0e-3);
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    rig.doubleClick (level, { 38.0f, 72.0f });
    CHECK_EVENTUALLY (rig.editing() && juce::Component::getCurrentlyModalComponent() != nullptr);
    rig.type ("-30");
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas"));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -12.0f, 1.0e-3);
    CHECK (! rig.uses (space));

    rig.doubleClick (level, { 38.0f, 72.0f });
    CHECK_EVENTUALLY (rig.editing() && juce::Component::getCurrentlyModalComponent() != nullptr);
    rig.type ("-20");
    CHECK (rig.uses (tabKey));
    CHECK_EVENTUALLY (parameterIs (params::id::level, -20.0f, 1.0e-3f));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -20.0f, 1.0e-3);
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space));

    // The space bar sets it and goes on to the DAW: the label stops being modal at once, so
    // macOS passes the key on.
    rig.doubleClick (level, { 38.0f, 72.0f });
    CHECK_EVENTUALLY (rig.editing() && juce::Component::getCurrentlyModalComponent() != nullptr);
    rig.type ("-24");
    CHECK (! rig.uses (space));
    CHECK_EVENTUALLY (parameterIs (params::id::level, -24.0f, 1.0e-3f));
    CHECK_NEAR (getParameter (*rig.processor, params::id::level), -24.0f, 1.0e-3);
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas") && ! rig.editing());

    // A bar's value in the inspector (Intensity): a click to type, Return sets it. JUCE's own
    // label closes it; the focus comes home a moment later.
    rig.click (rig.canvas(), rig.onText());
    auto& intensity = rig.get ("inspector-intensity");
    CHECK_EVENTUALLY (intensity.isShowing());
    rig.click ("inspector-intensity");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("50");
    CHECK (rig.uses (returnKey));
    const auto intensityIs = [&] (float value)
    {
        const auto* object = rig.processor->getDocument().find (rig.textId);
        return object != nullptr && std::abs (object->intensity - value) <= 1.0e-3f;
    };
    CHECK_EVENTUALLY (intensityIs (0.5f));
    CHECK_NEAR (rig.processor->getDocument().find (rig.textId)->intensity, 0.5f, 1.0e-3);
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && rig.hasFocus ("canvas"));
    CHECK (! rig.uses (space) && ! rig.uses (returnKey));

    // ... and a click on it, then the space bar: set, and the DAW's.
    rig.click ("inspector-intensity");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("70");
    CHECK (! rig.uses (space));
    CHECK_EVENTUALLY (intensityIs (0.7f));
    CHECK_NEAR (rig.processor->getDocument().find (rig.textId)->intensity, 0.7f, 1.0e-3);
    CHECK_EVENTUALLY (juce::Component::getCurrentlyModalComponent() == nullptr && ! rig.editing() && rig.hasFocus ("canvas"));
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
        CHECK_EVENTUALLY (rig.editor->getSelectedObject() == rig.textId
                          && (variant == ArtFreqProcessor::Variant::instrument || rig.editor->isDrawerOpen()));

        rig.editor->showLiveView (true);
        CHECK_EVENTUALLY (rig.get ("live-view").isShowing() && rig.editor->getSelectedObject() == rig.textId);

        const auto before = rig.processor->getDocument().find (rig.textId)->startTime;
        CHECK (rig.uses (juce::KeyPress (juce::KeyPress::rightKey)));
        CHECK_GREATER (rig.processor->getDocument().find (rig.textId)->startTime, before);
        CHECK (rig.uses (juce::KeyPress (juce::KeyPress::deleteKey)));
        CHECK (rig.text() == "(gone)");
        CHECK_EVENTUALLY (rig.editor->getSelectedObject().isNull());

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
    CHECK_EVENTUALLY (rig.hasFocus ("canvas"));
    CHECK (! rig.uses (tabKey));
    pump (50);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));
    CHECK (! rig.uses (juce::KeyPress (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, '\t')));
    pump (50);
    CHECK (rig.hasFocus ("canvas"));

    auto* list = dynamic_cast<ui::LayerList*> (findComponent (*rig.editor, "layer-list"));

    if (list != nullptr)
    {
        CHECK_EVENTUALLY (list->isShowing());
        rig.click (*list, list->getRowBounds (0).toFloat().getCentre().withX (60.0f));
        CHECK_EVENTUALLY (rig.hasFocus ("layer-list"));
        CHECK (! rig.uses (tabKey));
        pump (50);
        CHECK (rig.hasFocus ("layer-list"));
        CHECK (rig.uses (escapeKey));   // the list's own: deselects
        CHECK_EVENTUALLY (rig.editor->getSelectedObject().isNull());
    }

    // A panel has the keys while it is open: Esc closes it, the rest is the DAW's; closed, the
    // canvas has the focus again.
    for (const auto* button : { "settings", "help" })
    {
        const auto panelId = juce::String (button) + "-panel";
        rig.click (button);
        CHECK_EVENTUALLY (rig.get (panelId).isShowing() && rig.hasFocus (panelId));
        CHECK (! rig.uses (space) && ! rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
        CHECK (! rig.uses (tabKey));
        pump (50);
        CHECK (rig.hasFocus (panelId));
        CHECK (rig.uses (escapeKey));
        CHECK_EVENTUALLY (! rig.get (panelId).isVisible() && rig.hasFocus ("canvas"));
        CHECK (! rig.uses (space));
    }

    // In the settings, a field typed into and set: the focus is the panel's again.
    rig.click ("settings");
    CHECK_EVENTUALLY (rig.get ("settings-panel").isShowing() && rig.get ("range-low").isShowing());
    rig.click ("range-low");
    CHECK_EVENTUALLY (rig.editing());
    rig.type ("30");
    CHECK (rig.uses (returnKey));
    CHECK_EVENTUALLY (std::abs (getParameter (*rig.processor, params::id::rangeLow) - 30.0f) <= 1.0e-3f);
    CHECK_NEAR (getParameter (*rig.processor, params::id::rangeLow), 30.0f, 1.0e-3);
    CHECK_EVENTUALLY (rig.hasFocus ("settings-panel"));
    CHECK (! rig.uses (space));
    CHECK (rig.uses (escapeKey));
    CHECK_EVENTUALLY (! rig.get ("settings-panel").isVisible());

    // The live view: no canvas to hold the focus, and still nothing takes the space bar.
    rig.editor->showLiveView (true);
    CHECK_EVENTUALLY (rig.get ("live-view").isShowing());
    CHECK (! rig.uses (space) && ! rig.uses (returnKey));
    rig.click ("repeat");
    CHECK_EVENTUALLY (rig.focused() == nullptr || ! dynamic_cast<juce::Button*> (rig.focused()));
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
        CHECK_EVENTUALLY (rig.hasFocus ("activation") && ! rig.editing());
        CHECK (! rig.uses (space));
        CHECK (! rig.uses (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
        CHECK (! rig.uses (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z')));
        CHECK (! rig.uses (returnKey));   // nothing typed
        CHECK (rig.hasFocus ("activation"));

        // A click on Activate with nothing typed: says what to do; the focus stays home.
        rig.click ("activate");
        CHECK_EVENTUALLY (! rig.editing() && rig.focusIsOffButtons());
        CHECK (! rig.uses (space));

        // Into the field: typing, as key presses, formatted.
        rig.click ("licence-key");
        CHECK_EVENTUALLY (rig.editing() && ! rig.typing());
        rig.type ("y1f2fjvxbx");
        CHECK_EVENTUALLY (field.getText() == "Y1F2F-JVXBX");

        // The space bar: the typing ends, the DAW plays, what was typed stays.
        CHECK (! rig.uses (space));
        CHECK_EVENTUALLY (! rig.editing() && rig.hasFocus ("activation"));
        CHECK (field.getText() == "Y1F2F-JVXBX");

        // Back in, the rest, and Return: sent; the focus back home.
        rig.click ("licence-key");
        CHECK_EVENTUALLY (rig.editing());
        field.moveCaretToEnd();
        rig.type ("wcnvbvczkj");
        CHECK_EVENTUALLY (field.getText() == "Y1F2F-JVXBX-WCNVB-VCZKJ");

        if (variant == ArtFreqProcessor::Variant::instrument)
            licensing.server->reply (licensetest::activateProAsArtFreq);

        licensing.server->reply (licensetest::activateProFresh);
        CHECK (rig.uses (returnKey));
        CHECK_EVENTUALLY (! rig.editing());
        CHECK (licensetest::pumpUntil ([&] { return rig.processor->isLicensed(); }));

        // Return on the success: continue, and the canvas has the focus again.
        CHECK_EVENTUALLY (screen.isShowing() && dynamic_cast<ui::ActivationScreen&> (screen).isShowingSuccess());
        CHECK (rig.uses (returnKey));
        CHECK_EVENTUALLY (! screen.isVisible() && rig.hasFocus ("canvas"));
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
        CHECK_EVENTUALLY (rig.editing());
        field.setText (held, false);
        field.setCaretPosition (caret < 0 ? held.length() : caret);
        juce::SystemClipboard::copyTextToClipboard (clipboard);
        CHECK (rig.uses (paste));
        return field.getText();
    };

    // The typo was refused ("check for typos"); the right key pasted after it, the caret at the end.
    rig.click ("licence-key");
    CHECK_EVENTUALLY (rig.editing());
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
