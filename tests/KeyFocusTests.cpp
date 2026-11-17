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

#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
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
        anything else gets to see them. */
    bool typing() const   { return peer()->findCurrentTextInputTarget() != nullptr; }

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
    // modal. In both plug-ins.
    if (! canDriveAWindow())
        return;

    for (const auto variant : { ArtFreqProcessor::Variant::instrument, ArtFreqProcessor::Variant::effect })
    {
        Rig rig (variant);
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

        if (variant == ArtFreqProcessor::Variant::effect)
        {
            rig.click ("mode");
            CHECK (! rig.uses (space));

            auto& depth = rig.get ("depth");
            rig.drag (depth, { 30.0f, 14.0f }, { 30.0f, 4.0f });
            CHECK (! rig.uses (space));
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
    CHECK (rig.typing());
    rig.type ("12");
    CHECK (rig.uses (returnKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 12.0f, 1.0e-4);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space) && ! rig.uses (returnKey));

    rig.click ("start-bar");
    pump (20);
    rig.type ("40");
    CHECK (rig.uses (escapeKey));
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 12.0f, 1.0e-4);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space));

    rig.click ("start-bar");
    pump (20);
    rig.type ("7");
    rig.click ("repeat");
    pump (20);
    CHECK_NEAR (getParameter (*rig.processor, params::id::startBar), 7.0f, 1.0e-4);
    CHECK (! rig.typing() && ! rig.uses (space));
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
    CHECK (rig.typing() && juce::Component::getCurrentlyModalComponent() != nullptr);
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

    // A bar's value in the inspector (Intensity): a click to type, Return sets it. JUCE's own
    // label closes it; the focus comes home a moment later.
    rig.click (rig.canvas(), rig.onText());
    pump (50);
    auto& intensity = rig.get ("inspector-intensity");
    CHECK (intensity.isShowing());
    rig.click ("inspector-intensity");
    pump (20);
    CHECK (rig.typing());
    rig.type ("50");
    CHECK (rig.uses (returnKey));
    pump (50);
    CHECK_NEAR (rig.processor->getDocument().find (rig.textId)->intensity, 0.5f, 1.0e-3);
    CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
    CHECK (rig.hasFocus ("canvas") && ! rig.uses (space) && ! rig.uses (returnKey));
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
    CHECK (rig.typing());
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
