#pragma once

// Licensing in the editor (see docs/DESIGN.md, "Licensing"): the activation screen over the
// window while the plug-in is locked, and the Licence card in the settings. Both talk to the
// processor's LicenseManager (src/license/), which does the work: the key's checksum, the
// request on a thread of its own, the signed licence, the shared file, the messages.
//
// The network is touched only when the user presses Activate or Deactivate, never by opening
// the window. Nothing here is modal and nothing opens a window of its own.

#include "PluginProcessor.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace artfreq::ui
{

/** A key as the field shows it: what a key can hold (digits and capitals; O read as 0, I and L as
    1, as the licence code reads them), in groups of five ("8K3QX-7M2PD-R9T4W-NV6ZH"), at most 20
    characters. Pasted text that holds a key among other words ("Your key: 8k3qx 7m2pd ...") gives
    the key (or, if its checksum fails, what is in a key's shape there: the key with its typo);
    anything else is kept as typed, up to 20 characters, for the checksum to judge. */
juce::String formatTypedKey (const juce::String& typed);

/** The number of key characters in a piece of typed text (the dashes, spaces and such not counted). */
int countKeyCharacters (const juce::String& typed);

/** Where the caret goes in formatTypedKey's text after `keyCharacters` of the key. */
int caretAfterKeyCharacters (int keyCharacters);

//==============================================================================
/** The text box the key is typed or pasted into. It shows the key in groups of five as it is
    typed, whatever was typed or pasted (any case, with or without dashes or spaces); a key
    pasted over what it holds replaces it (a piece of one goes in at the caret). It has the
    keyboard focus only while it is typed in (see ui/KeyFocus.h): Return activates, Esc and Tab
    end the typing, and the space bar, which no key holds, ends it and goes on to the DAW. Its keys
    come to it as key presses, not through the system's text input (a key needs no input method,
    and on macOS that path takes every printable key before anything else sees it). */
class KeyTextBox final : public juce::TextEditor
{
public:
    KeyTextBox();

    /** Return: activate with what is typed. */
    std::function<void()> onActivate;

    /** Replaces the text with `typed`, formatted. */
    void setKey (const juce::String& typed);

    bool isTextInputActive() const override   { return false; }
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override      { return false; }
    void insertTextAtCaret (const juce::String& text) override;
    void performPopupMenuAction (int menuItemID) override;

private:
    void reformat();
};

//==============================================================================
/** A message the user can select and copy (the support address in it, say), in the look of
    ordinary text: no box, no caret. A double-click on the support address selects all of it.
    Read-only, so the space bar and every other key but copying pass on to the DAW. */
class SelectableText final : public juce::TextEditor
{
public:
    SelectableText();

    void setMessage (const juce::String& text, juce::Colour colour);
    const juce::String& getMessage() const noexcept   { return message; }

    /** The font (messageFont() unless set). */
    void setMessageFont (const juce::Font& font);

    /** The height the message takes at a width, and the height of one of its lines. */
    int getHeightFor (int width) const;
    int getLineHeight() const;

    bool isTextInputActive() const override   { return false; }
    bool keyStateChanged (bool) override      { return false; }
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static juce::Font messageFont();

private:
    juce::String message;
    juce::Font font { messageFont() };
};

//==============================================================================
/** A link in a line of small text: the store's page, in the browser. */
class LinkButton final : public juce::Button
{
public:
    LinkButton (const juce::String& buttonText, const juce::String& url);

    int getIdealWidth() const;
    const juce::String& getUrl() const noexcept   { return url; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    /** For tests: called instead of opening the browser. */
    std::function<void (const juce::String&)> onOpen;

    static juce::Font linkFont();

private:
    juce::String url;
};

//==============================================================================
/** The activation screen: over the window (below its top bar) while the plug-in is locked, the
    rest of the window dimmed behind it and out of reach. The emblem, "Activate ArtFreq", where
    the key is, the key field, Activate, the result in the licence code's own words (the support
    address selectable), and a link to the store. While the request runs Activate turns into its
    busy state; closing the window stops the request. Once this window has activated the plug-in
    it says so, and Continue (or Return, or Esc) puts it away; every other open instance simply
    unlocks. The editor shows and hides it (Content::updateLicensing). */
class ActivationScreen final : public juce::Component,
                               private anymeans::license::LicenseManager::Listener,
                               private juce::Timer
{
public:
    explicit ActivationScreen (ArtFreqProcessor& processor);
    ~ActivationScreen() override;

    /** Sends what is in the key field to the server (nothing while a request runs). */
    void activate();

    /** Puts the typing into the key field (Activate... in the settings, a test). */
    void focusKeyField();

    /** Shows a result that came from elsewhere: a deactivation in the settings, which locked the
        plug-in. */
    void showResult (const anymeans::license::LicenseManager::Result& result);

    /** Return and Esc, while the screen is the focus's home: Return activates (or continues), Esc
        continues after an activation. Anything else is not the screen's. */
    bool handleKey (const juce::KeyPress& key);

    /** Whether the screen has something to show although the plug-in is activated: the request
        it sent is still on its way, or its result (activated) is shown until Continue. */
    bool wantsToStay() const noexcept   { return waitingForResult || showingSuccess; }

    /** Called when wantsToStay() may have changed: a result came in, or Continue was pressed. */
    std::function<void()> onStateChanged;

    // For the snapshot tool and tests.
    KeyTextBox& getKeyField() noexcept               { return keyField; }
    const SelectableText& getMessageText() const     { return messageText; }
    juce::Rectangle<int> getCardBounds() const       { return card; }
    bool isShowingSuccess() const noexcept           { return showingSuccess; }
    bool isBusy() const noexcept;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    enum class MessageKind { none, info, error, success };

    void licenseStateChanged (anymeans::license::LicenseManager&) override;
    void timerCallback() override;
    void finished (const anymeans::license::LicenseManager::Result& result);
    void setMessage (const juce::String& text, MessageKind kind);
    juce::String getInfoText() const;
    void continuePressed();
    void update();

    ArtFreqProcessor& processor;
    anymeans::license::LicenseManager& license;
    const bool effect;

    KeyTextBox keyField;
    shell::ShellButton activateButton { "Activate", shell::ShellButton::Kind::primary };
    SelectableText messageText;
    LinkButton keyLink;
    LinkButton buyLink;

    juce::Rectangle<int> card, emblemArea, titleArea, subtitleArea, linksArea;
    MessageKind messageKind = MessageKind::none;
    bool waitingForResult = false;   // this screen sent the request that is on its way
    bool showingSuccess = false;
    bool showingUnfinished = false;  // locked by a deactivation that didn't finish (see getInfoText)
    float spinnerPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActivationScreen)
};

//==============================================================================
/** The Licence card in the settings: what is activated ("ArtFreq Pro: ArtFreq and ArtFreq FX"),
    the key (masked), "Activated on this computer", and Deactivate, which first says in one line
    what will be locked and that the key can then be used on another computer. A deactivation
    that fails keeps everything as it was and says why (the support address selectable); one
    after which another key keeps the plug-in activated says what went and what stays. Not
    activated, it says so and offers the activation screen, and, after a deactivation that was
    cut off (the project closed while it ran), Deactivate to finish it.

    `wide` (ArtFreq FX's two-column settings): one row across the panel, its caption on the left;
    otherwise a column under a caption the panel draws. */
class LicenceCard final : public juce::Component,
                          private anymeans::license::LicenseManager::Listener,
                          private juce::Timer
{
public:
    LicenceCard (ArtFreqProcessor& processor, bool wide);
    ~LicenceCard() override;

    /** The height the card needs at a width, in the state it is in. */
    int getIdealHeight (int width) const;

    /** Back to the plain card (when the panel is opened again): no question, no old message. */
    void reset();

    /** The card's height changed (a question, a message): the panel lays itself out again. */
    std::function<void()> onHeightChanged;

    /** Not activated: Activate... was pressed. The editor shows the activation screen. */
    std::function<void()> onActivateWanted;

    /** The deactivation went through and the plug-in is locked now. The editor puts the panel
        away and shows the result on the activation screen. (If another key keeps the plug-in
        activated, the card says so itself and this is not called.) */
    std::function<void (const anymeans::license::LicenseManager::Result&)> onDeactivated;

    // For the snapshot tool and tests.
    bool isConfirming() const noexcept   { return confirming; }
    const SelectableText& getMessageText() const   { return messageText; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void licenseStateChanged (anymeans::license::LicenseManager&) override;
    void timerCallback() override;
    void confirm (bool shouldConfirm);
    void deactivate();
    void update();
    bool isFinishingADeactivation() const;

    struct Layout;
    Layout layoutFor (int width) const;
    juce::Font sentenceFont() const;
    float sentenceSpacing() const;

    ArtFreqProcessor& processor;
    anymeans::license::LicenseManager& license;
    const bool wide;

    shell::ShellButton deactivateButton { juce::String::fromUTF8 ("Deactivate\xe2\x80\xa6") };
    shell::ShellButton cancelButton { "Cancel" };
    shell::ShellButton confirmButton { "Deactivate", shell::ShellButton::Kind::danger };
    shell::ShellButton activateButton { juce::String::fromUTF8 ("Activate\xe2\x80\xa6"), shell::ShellButton::Kind::primary };
    SelectableText messageText;

    bool confirming = false;
    bool waitingForResult = false;   // this card sent the deactivation that is on its way
    float spinnerPhase = 0.0f;
    int lastHeight = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenceCard)
};

} // namespace artfreq::ui
