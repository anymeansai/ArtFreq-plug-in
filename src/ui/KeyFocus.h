#pragma once

// Where the keyboard focus lives in the editor.
//
// A plug-in cannot press the DAW's play button: it can only leave the key alone. A key the
// focused component (and its parents) did not use goes on to the host's window; one a component
// used, the host never sees. A focused text box takes all typing, and a modal component every
// key. So the focus is kept where the editor's own few keys are handled (the canvas; the layer
// list while it is being used; a panel while it is open), and a text box has it only while the
// user is typing: Return, Esc, Tab or a click anywhere else hand it back. See docs/DESIGN.md,
// "Keys and the keyboard focus".

#include <juce_gui_basics/juce_gui_basics.h>

namespace artfreq::ui
{

/** The part of the editor that takes the keyboard focus back when nobody is typing: the
    editor's content. */
class KeyFocusHome
{
public:
    virtual ~KeyFocusHome() = default;

    /** Gives the focus to where the editor's own keys are handled: the panel that is open, else
        the canvas. Only while this window has the keyboard: never taken from another window. */
    virtual void takeKeyFocusBack() = 0;
};

/** Hands the keyboard focus back from a text box that is done with (`from`, or a child of it)
    to the editor: to the KeyFocusHome among its parents. Without one (a component on its own, in
    a test) the focus is simply let go. */
inline void handKeyFocusBack (juce::Component& from)
{
    for (auto* parent = from.getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
    {
        if (auto* home = dynamic_cast<KeyFocusHome*> (parent))
        {
            home->takeKeyFocusBack();
            return;
        }
    }

    from.giveAwayKeyboardFocus();
}

/** Marks a component that handles keys of its own while it has the focus (the canvas, the layer
    list, an open panel): a click on it leaves the focus there. A click on anything else (a
    button, a knob, a menu) sends the focus back to the KeyFocusHome, so a control that was only
    clicked never keeps the keyboard. */
inline void setKeepsKeyFocus (juce::Component& component)
{
    component.getProperties().set ("keepsKeyFocus", true);
}

inline bool keepsKeyFocus (const juce::Component& component)
{
    return component.getProperties().contains ("keepsKeyFocus");
}

/** Whether a component is a text box: where typing goes, the space bar included. */
inline bool isTextBox (const juce::Component* component)
{
    return dynamic_cast<const juce::TextEditor*> (component) != nullptr;
}

} // namespace artfreq::ui
