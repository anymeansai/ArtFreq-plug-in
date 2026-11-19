// Licensing in the plug-ins: the activation screen over a locked plug-in's window and the Licence
// card in its settings, driven through the editor against a fake server (src/license's tests
// cover the licence code itself); and instances following each other's activation.
//
// These need JUCE's GUI classes, so on Linux run them under a virtual display (xvfb-run -a).
// The editor follows the licence and the settings on its timers and listeners, and requests come
// back on threads: a slow machine (the macOS runner's Intel half, under Rosetta) does all of that
// much later than a fast one, so checks of it wait for it (CHECK_EVENTUALLY, WaitFor.h).

#include "LicenseTestSupport.h"
#include "ProcessorTestUtils.h"
#include "TestFramework.h"
#include "WaitFor.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "license/LicenseKey.h"
#include "license/LicenseStore.h"
#include "ui/Editor.h"
#include "ui/Licensing.h"
#include "ui/ShellPanels.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

using namespace artfreq;
using namespace licensetest;
using Variant = ArtFreqProcessor::Variant;

namespace
{

juce::Component* findComponent (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findComponent (*child, id))
            return found;

    return nullptr;
}

/** Shown: visible, and so is everything it is in (the editor is on no screen here). */
bool isShown (juce::Component& root, const juce::String& id)
{
    auto* component = findComponent (root, id);

    for (auto* c = component; c != nullptr && c != &root; c = c->getParentComponent())
        if (! c->isVisible())
            return false;

    return component != nullptr;
}

const juce::KeyPress returnKey (juce::KeyPress::returnKey, juce::ModifierKeys(), '\r');

/** A plug-in instance and its editor, in whatever licensing environment is the default when it
    is made (make a ScopedLockedLicensing first). */
struct Instance
{
    explicit Instance (Variant variant)
        : processor (std::make_unique<ArtFreqProcessor> (variant))
    {
        processor->setPlayConfigDetails (variant == Variant::effect ? 2 : 0, 2, 48000.0, 512);
        processor->prepareToPlay (48000.0, 512);
        openEditor();
    }

    void openEditor()
    {
        editor.reset (dynamic_cast<ArtFreqEditor*> (processor->createEditor()));
        pump (40);
    }

    ui::ActivationScreen& screen() const   { return *dynamic_cast<ui::ActivationScreen*> (findComponent (*editor, "activation")); }
    ui::KeyTextBox& field() const          { return *dynamic_cast<ui::KeyTextBox*> (findComponent (*editor, "licence-key")); }
    juce::Button& button (const juce::String& id) const   { return *dynamic_cast<juce::Button*> (findComponent (*editor, id)); }
    juce::Component& content() const       { return *findComponent (*editor, "content"); }
    ui::LicenceCard& card() const          { return *dynamic_cast<ui::LicenceCard*> (findComponent (*editor, "licence-card")); }

    juce::String message() const           { return screen().getMessageText().getMessage(); }
    bool screenShown() const               { return isShown (*editor, "activation"); }

    /** Waits for the request to come back (results arrive on the message thread). */
    bool settle (const std::function<bool()>& done) const   { return pumpUntil (done, testwait::timeoutMs); }

    std::unique_ptr<ArtFreqProcessor> processor;
    std::unique_ptr<ArtFreqEditor> editor;
};

juce::String formatted (const char* key)
{
    return anymeans::license::LicenseKey::format (key);
}

} // namespace

//==============================================================================
ARTFREQ_TEST (uiLicensingTheKeyFieldShowsAnyFormAsAKey)
{
    // Typed or pasted in any form: in groups of five, capitals, O as 0 and I or L as 1 (as the
    // licence code reads them), at most a key's length; a key pasted with other words is found.
    CHECK (ui::formatTypedKey ("y1f2fjvxbxwcnvbvczkj") == "Y1F2F-JVXBX-WCNVB-VCZKJ");
    CHECK (ui::formatTypedKey ("  Y1F2F JVXBX\tWCNVB-VCZKJ \n") == "Y1F2F-JVXBX-WCNVB-VCZKJ");
    CHECK (ui::formatTypedKey ("y1f2f") == "Y1F2F");
    CHECK (ui::formatTypedKey ("y1f2fj") == "Y1F2F-J");
    CHECK (ui::formatTypedKey ("5vbnqo5o5rhw9poa28oqw") == "5VBNQ-0505R-HW9P0-A280Q");   // O as 0, 21 characters cut to 20
    CHECK (ui::formatTypedKey ("Your key: Y1F2F-JVXBX-WCNVB-VCZKJ. Keep it safe!") == "Y1F2F-JVXBX-WCNVB-VCZKJ");
    CHECK (ui::formatTypedKey (juce::String::fromUTF8 ("Key \xe2\x80\x93 y1f2f\xe2\x80\x93jvxbx\xe2\x80\x93wcnvb\xe2\x80\x93vczkj"))
           == "Y1F2F-JVXBX-WCNVB-VCZKJ");                                                       // typographic dashes from a mail
    CHECK (ui::formatTypedKey ("") == "");

    // A key with a typo, pasted with the email's words: the key as typed, not the words' first
    // twenty characters ("Y0UR1-1CENS-..."), so the typo is there to be seen.
    CHECK (ui::formatTypedKey ("Your license key: y1f2f-jvxbx-wcnvb-vczkk") == "Y1F2F-JVXBX-WCNVB-VCZKK");
    CHECK (ui::formatTypedKey ("Key Y1F2F JVXBX WCNVB VCZKK") == "Y1F2F-JVXBX-WCNVB-VCZKK");
    CHECK (ui::countKeyCharacters ("Y1F2F-JV") == 7);
    CHECK (ui::caretAfterKeyCharacters (0) == 0 && ui::caretAfterKeyCharacters (5) == 5 && ui::caretAfterKeyCharacters (6) == 7
           && ui::caretAfterKeyCharacters (20) == 23);

    // Typed key by key into the field: the dashes come as the groups do, the caret after the last
    // character; a deleted character is taken out of its group.
    const juce::ScopedJuceInitialiser_GUI gui;
    ui::KeyTextBox field;
    field.setSize (300, 34);

    for (const auto c : juce::String ("y1f2fjvx"))
        field.keyPressed (juce::KeyPress (static_cast<int> (c), juce::ModifierKeys(), c));

    CHECK (field.getText() == "Y1F2F-JVX");
    CHECK (field.getCaretPosition() == 9);

    field.setCaretPosition (6);   // before the J
    field.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
    CHECK (field.getText() == "Y1F2F-JVX");   // the dash: the groups stay as they are
    field.setCaretPosition (7);
    field.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
    CHECK (field.getText() == "Y1F2F-VX");
    CHECK (field.getCaretPosition() == 6);

    // The space bar types nothing (it is the DAW's): unused.
    CHECK (! field.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey, juce::ModifierKeys(), ' ')));
    CHECK (field.getText() == "Y1F2F-VX");
}

ARTFREQ_TEST (uiLicensingTheActivationScreenCoversALockedPlugIn)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const ScopedLockedLicensing licensing;

    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        Instance instance (variant);
        auto& editor = *instance.editor;

        // Shown at once (nothing was sent anywhere to know it), over everything but the top bar,
        // which shows only the name, help and settings.
        CHECK (instance.screenShown());
        CHECK (licensing.server->numSent() == 0);
        CHECK (instance.screen().getY() == findComponent (editor, "header")->getBottom());
        CHECK (isShown (editor, "help") && isShown (editor, "settings"));
        CHECK (! isShown (editor, "undo") && ! isShown (editor, "redo"));
        CHECK (! isShown (editor, variant == Variant::effect ? "effect-controls" : "view-switch"));

        if (variant == Variant::effect)
            CHECK (! isShown (editor, "live"));

        // The drawing is out of reach: a click on the canvas lands on the screen, and the
        // drawing's keys are not the editor's (they go on to the DAW).
        auto& canvas = *findComponent (editor, "canvas");
        auto& content = instance.content();
        auto* hit = content.getComponentAt (content.getLocalPoint (&canvas, canvas.getLocalBounds().getCentre()));
        CHECK (hit == &instance.screen() || instance.screen().isParentOf (hit));

        CHECK (! content.keyPressed (juce::KeyPress ('b', juce::ModifierKeys(), 'b')));
        CHECK (! content.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
        CHECK (! content.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
        CHECK (! content.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey, juce::ModifierKeys(), ' ')));
        CHECK (! dynamic_cast<juce::Button*> (findComponent (editor, "tool-brush"))->getToggleState());

        // Return with nothing typed is not the screen's either.
        CHECK (! content.keyPressed (returnKey));

        // Help and settings open over it; Esc closes them.
        editor.showHelp (true);
        CHECK (isShown (editor, "help-panel"));
        CHECK (content.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK (! isShown (editor, "help-panel") && instance.screenShown());
    }

    // An activated plug-in shows none of it.
    CHECK (licensing.activatePro());
    Instance activated (Variant::effect);
    CHECK (! activated.screenShown() && isShown (*activated.editor, "effect-controls"));
}

ARTFREQ_TEST (uiLicensingActivatingWithAGoodKey)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        const ScopedLockedLicensing licensing;
        auto& server = *licensing.server;
        Instance instance (variant);
        const auto instrument = variant == Variant::instrument;

        // The instrument sends a key as ArtFreq's first; the server says a Pro key is for
        // another product, and it is sent again as ArtFreq Pro's. ArtFreq FX sends it as Pro's.
        if (instrument)
            server.reply (activateProAsArtFreq);

        server.reply (activateProFresh);

        // Pasted as the email shows it, lower case: activated with Return.
        instance.field().setKey ("y1f2f-jvxbx-wcnvb-vczkj");
        CHECK (instance.field().getText() == formatted (keyPro));
        CHECK (instance.field().keyPressed (returnKey));
        CHECK (instance.screen().isBusy() || server.numSent() > 0);

        CHECK (instance.settle ([&] { return instance.screen().isShowingSuccess(); }));
        CHECK (instance.processor->isLicensed());
        CHECK (instance.message().contains ("ArtFreq and ArtFreq FX"));
        CHECK (instance.screenShown());

        const auto sent = server.sent();
        CHECK (sent.size() == (instrument ? 2u : 1u));

        if (! sent.empty())
        {
            CHECK (sent.back().path == "/api/activate");
            CHECK (sent.back().json.getProperty ("key", {}).toString() == formatted (keyPro));
            CHECK (sent.back().json.getProperty ("productId", {}).toString() == anymeans::license::PRODUCT_ID_ARTFREQ_PRO);
            CHECK (sent.back().json.getProperty ("machineId", {}).toString() == machineHere);
            CHECK (! sent.back().onMessageThread);
        }

        if (instrument && sent.size() == 2)
            CHECK (sent.front().json.getProperty ("productId", {}).toString() == anymeans::license::PRODUCT_ID_ARTFREQ);

        // Continue (Return): the screen goes, the window is whole again.
        CHECK (instance.content().keyPressed (returnKey));
        CHECK_EVENTUALLY (! instance.screenShown() && isShown (*instance.editor, instrument ? "view-switch" : "effect-controls"));

        // Saved for every AnyMeans plug-in on this computer: a new instance is activated at once,
        // without the network.
        const auto before = server.numSent();
        Instance another (variant);
        CHECK (another.processor->isLicensed() && ! another.screenShown());
        CHECK (server.numSent() == before);
    }
}

ARTFREQ_TEST (uiLicensingEveryFailureSaysWhyAndLeavesItLocked)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const ScopedLockedLicensing licensing;
    auto& server = *licensing.server;
    Instance effect (Variant::effect);

    const auto attempt = [&] (const juce::String& key)
    {
        // Done when the screen shows a new message, or the same one again for a request that
        // went to the server (two keys can be refused in the same words).
        const auto before = effect.message();
        const auto sentBefore = server.numSent();
        effect.field().setKey (key);
        effect.button ("activate").onClick();
        return effect.settle ([&]
        {
            return ! effect.screen().isBusy() && effect.message().isNotEmpty()
                && (effect.message() != before || server.numSent() > sentBefore);
        });
    };

    // A typo: caught on the spot, nothing sent.
    CHECK (attempt ("Y1F2F-JVXBX-WCNVB-VCZKK"));
    CHECK (effect.message().contains ("typos"));
    CHECK (server.numSent() == 0);

    // Every refusal names the support address: a genuine key can be refused too (a mix-up with
    // the order), and "check for typos" is then no help.
    const juce::String address (anymeans::license::SUPPORT_EMAIL);

    // An unknown key (403): the server's words.
    server.reply (activateUnknownKey);
    CHECK (attempt (keyUnknown));
    CHECK (effect.message().startsWith ("That license key isn't valid"));
    CHECK (effect.message().contains (address));

    // A key for ArtFreq alone, in ArtFreq FX (403 wrong_product): ArtFreq FX's own words.
    server.reply (activateArtFreqAsPro);
    CHECK (attempt (keyArtFreq));
    CHECK (effect.message().startsWith (anymeans::license::artFreqEffectLicensing().wrongProductMessage));
    CHECK (effect.message().contains (address));

    // A key for another AnyMeans plug-in altogether (Creme).
    server.reply (activateCremeAsPro);
    CHECK (attempt (keyCreme));
    CHECK (effect.message().startsWith ("That key isn't for ArtFreq FX.") && effect.message().contains (address));

    // Two computers already (409): the server's words, with the support address, which a
    // double-click selects whole (the text box would stop at '@' and '.').
    server.reply (activateLimit);
    CHECK (attempt (keyFull));
    CHECK (effect.message().contains (address));

    auto& text = const_cast<ui::SelectableText&> (effect.screen().getMessageText());
    const auto start = effect.message().indexOf (address);
    const auto at = text.getTextBounds ({ start + 9, start + 10 }).getBounds().getCentre();   // on "anymeansaudio"
    CHECK (text.isReadOnly());
    text.mouseDoubleClick (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), {},
                                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                             juce::MouseInputSource::defaultTiltY, &text, &text, juce::Time::getCurrentTime(),
                                             at.toFloat(), juce::Time::getCurrentTime(), 2, false));
    CHECK (text.getHighlightedText() == address);

    // No answer: "Couldn't reach the server", and trying again is always possible.
    server.noConnection();
    CHECK (attempt (keyPro));
    CHECK (effect.message() == juce::String::fromUTF8 ("Couldn't reach the server \xe2\x80\x94 check your connection and try again."));
    CHECK (! effect.field().isReadOnly() && effect.button ("activate").isVisible());

    // The server without its signing key: "ok", but nothing to keep: not activated.
    server.reply (activateUnsigned);
    CHECK (attempt (keyPro));
    CHECK (effect.message().contains ("couldn't be confirmed"));

    // A Cloudflare error page.
    server.reply (cloudflare502);
    CHECK (attempt (keyPro));
    CHECK (effect.message().contains ("code 502") && effect.message().contains (address));

    CHECK (! effect.processor->isLicensed() && effect.screenShown());
    CHECK (anymeans::license::LicenseStore (licensing.folder.folder).read().activations.empty());

    // And then it works.
    server.reply (activateProFresh);
    CHECK (attempt (keyPro));
    CHECK (effect.processor->isLicensed() && effect.screen().isShowingSuccess());
}

ARTFREQ_TEST (uiLicensingBusyAndClosingTheWindowDuringARequest)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const ScopedLockedLicensing licensing;
    auto& server = *licensing.server;
    Instance instrument (Variant::instrument);

    // The server does not answer: busy, the key can't be changed, a second press sends nothing.
    server.hang();
    instrument.field().setKey (keyArtFreq);
    instrument.button ("activate").onClick();
    CHECK (server.hanging.wait (testwait::timeoutMs));
    CHECK (instrument.screen().isBusy());
    CHECK (instrument.field().isReadOnly());
    CHECK (instrument.button ("activate").getButtonText().startsWith ("Checking your key"));
    instrument.button ("activate").onClick();
    CHECK (instrument.content().keyPressed (returnKey));
    pump (50);
    CHECK (server.numSent() == 1);

    // The window closes: the request stops (its answer would go nowhere), still locked.
    instrument.editor.reset();
    CHECK (pumpUntil ([&] { return server.cancelsSeen.load() == 1; }));
    CHECK (! instrument.processor->getLicense().isBusy());
    CHECK (! instrument.processor->isLicensed());
    pump (100);

    // Open again: the screen, ready to try again.
    instrument.openEditor();
    CHECK (instrument.screenShown() && ! instrument.screen().isBusy() && ! instrument.field().isReadOnly());

    server.reply (activateArtFreqFresh);
    instrument.field().setKey (keyArtFreq);
    instrument.button ("activate").onClick();
    CHECK (instrument.settle ([&] { return instrument.screen().isShowingSuccess(); }));
    CHECK (instrument.processor->isLicensed());
    CHECK (instrument.message().contains ("unlocked for ArtFreq."));

    // A deactivation, though, is not stopped by closing the window: stopped after the server had
    // freed this computer's place, it would leave the computer activated with its place free. The
    // processor finishes it, and the plug-in is locked.
    instrument.content().keyPressed (returnKey);   // Continue
    instrument.editor->showSettings (true);
    CHECK_EVENTUALLY (isShown (*instrument.editor, "deactivate"));
    server.replyAfter (deactivateArtFreq, 1500);   // (still on its way when the window closes, even on a slow machine)
    instrument.button ("deactivate").onClick();
    instrument.button ("deactivate-confirm").onClick();
    CHECK (server.started.wait (testwait::timeoutMs));

    instrument.editor.reset();
    CHECK (instrument.processor->getLicense().getRequestInProgress() == anymeans::license::LicenseManager::Request::deactivating);
    CHECK (pumpUntil ([&] { return ! instrument.processor->isLicensed(); }));
    CHECK (server.cancelsSeen.load() == 1);
    CHECK (anymeans::license::LicenseStore (licensing.folder.folder).read().activations.empty());

    instrument.openEditor();
    CHECK (instrument.screenShown() && ! instrument.screen().isBusy());
}

ARTFREQ_TEST (uiLicensingTheLicenceCardAndDeactivating)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        const ScopedLockedLicensing licensing;
        auto& server = *licensing.server;
        CHECK (licensing.activatePro());

        Instance instance (variant);
        auto& editor = *instance.editor;
        CHECK (instance.processor->isLicensed() && ! instance.screenShown());

        editor.showSettings (true);
        CHECK_EVENTUALLY (isShown (editor, "licence-card") && isShown (editor, "deactivate"));

        auto* panel = dynamic_cast<ui::shell::SettingsPanel*> (findComponent (editor, "settings-panel"));
        CHECK (panel != nullptr);

        // The panel fits the window in every state of the card (ArtFreq FX's is small).
        const auto fits = [&]
        {
            return panel != nullptr && panel->getY() >= 0 && panel->getBottom() <= editor.getBaseHeight() - 6
                && panel->getLocalBounds().contains (instance.card().getBounds());
        };

        CHECK (fits());

        // Deactivate asks first, saying what will be locked and that the key can then be used
        // elsewhere. Cancel takes the question back.
        instance.button ("deactivate").onClick();
        CHECK (instance.card().isConfirming() && isShown (editor, "deactivate-confirm") && isShown (editor, "deactivate-cancel"));
        CHECK (instance.processor->getLicense().getDeactivationWarning().contains ("ArtFreq and ArtFreq FX will both be locked"));
        CHECK (fits());
        instance.button ("deactivate-cancel").onClick();
        CHECK (! instance.card().isConfirming() && isShown (editor, "deactivate"));
        CHECK (server.numSent() == 0);

        // It fails (no connection, then a server error): nothing changes, the card says why.
        for (const auto noConnection : { true, false })
        {
            if (noConnection)
                server.noConnection();
            else
                server.reply (cloudflare500);

            instance.button ("deactivate").onClick();
            instance.button ("deactivate-confirm").onClick();
            CHECK (instance.settle ([&] { return instance.card().getMessageText().getMessage().isNotEmpty(); }));
            CHECK (instance.card().getMessageText().getMessage().contains ("This computer stays activated."));
            CHECK (instance.processor->isLicensed() && ! instance.screenShown() && isShown (editor, "settings-panel"));
            CHECK (anymeans::license::LicenseStore (licensing.folder.folder).read().activations.size() == 1);
            CHECK (isShown (editor, "deactivate"));
            CHECK (fits());
        }

        // It goes through: locked, the panel away, the activation screen with the result.
        server.reply (deactivateArtFreq);
        instance.button ("deactivate").onClick();
        instance.button ("deactivate-confirm").onClick();
        CHECK (instance.settle ([&] { return ! instance.processor->isLicensed(); }));
        CHECK (instance.settle ([&] { return instance.screenShown() && ! isShown (editor, "settings-panel"); }));
        CHECK (instance.message().startsWith ("Deactivated. Your key can now be activated on another computer."));
        CHECK (anymeans::license::LicenseStore (licensing.folder.folder).read().activations.empty());

        const auto sent = server.sent();
        CHECK (! sent.empty() && sent.back().path == "/api/deactivate"
               && sent.back().json.getProperty ("key", {}).toString() == formatted (keyPro));

        // Locked, the card offers the activation screen.
        editor.showSettings (true);
        CHECK_EVENTUALLY (isShown (editor, "licence-activate") && ! isShown (editor, "deactivate"));
        CHECK (fits());
        instance.button ("licence-activate").onClick();
        CHECK_EVENTUALLY (! isShown (editor, "settings-panel") && instance.screenShown());
    }
}

ARTFREQ_TEST (uiLicensingInstancesFollowEachOther)
{
    // An instrument and an effect open in one host, both locked. A Pro key entered in the
    // instrument's window unlocks both: the effect follows the shared file by itself (no reload
    // here), and its screen simply goes. Deactivating in the effect's settings locks both.
    const juce::ScopedJuceInitialiser_GUI gui;
    const ScopedLockedLicensing licensing;
    auto& server = *licensing.server;

    Instance instrument (Variant::instrument);
    Instance effect (Variant::effect);
    CHECK (instrument.screenShown() && effect.screenShown());

    server.reply (activateProAsArtFreq);
    server.reply (activateProFresh);
    instrument.field().setKey (keyPro);
    instrument.button ("activate").onClick();

    CHECK (pumpUntil ([&] { return effect.processor->isLicensed() && ! effect.screenShown(); }));
    CHECK (instrument.processor->isLicensed() && instrument.screen().isShowingSuccess());
    CHECK (effect.message().isEmpty());

    server.reply (deactivateArtFreq);
    effect.editor->showSettings (true);
    CHECK_EVENTUALLY (isShown (*effect.editor, "deactivate"));
    effect.button ("deactivate").onClick();
    effect.button ("deactivate-confirm").onClick();

    CHECK (pumpUntil ([&] { return ! instrument.processor->isLicensed() && ! effect.processor->isLicensed(); }));
    CHECK (pumpUntil ([&] { return instrument.screenShown() && effect.screenShown(); }));
    CHECK (! instrument.screen().isShowingSuccess());

    // An ArtFreq key (the instrument alone): the instrument unlocks, ArtFreq FX does not.
    server.reply (activateArtFreqFresh);
    instrument.field().setKey (keyArtFreq);
    instrument.button ("activate").onClick();
    CHECK (instrument.settle ([&] { return instrument.processor->isLicensed(); }));
    pump (200);
    CHECK (! effect.processor->isLicensed() && effect.screenShown());
}

ARTFREQ_TEST (uiLicensingStateAutomationAndScansWorkWhileLocked)
{
    // A locked plug-in is still a whole plug-in to the host: its state saves and loads, its
    // parameters move, its latency and tail are reported, and making one (a host's scan) sends
    // nothing anywhere.
    const juce::ScopedJuceInitialiser_GUI gui;
    const ScopedLockedLicensing licensing;

    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        auto processor = std::make_unique<ArtFreqProcessor> (variant);
        CHECK (! processor->isLicensed());
        CHECK (processor->getLatencySamples() == (variant == Variant::effect ? 4096 : 0));

        testutil::setParameter (*processor, params::id::length, 4.0f);
        testutil::setParameter (*processor, params::id::level, -12.0f);

        juce::MemoryBlock state;
        processor->getStateInformation (state);

        auto loaded = std::make_unique<ArtFreqProcessor> (variant);
        loaded->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        pump (20);
        CHECK_NEAR (testutil::getParameter (*loaded, params::id::length), 4.0f, 1.0e-4);
        CHECK_NEAR (testutil::getParameter (*loaded, params::id::level), -12.0f, 1.0e-3);
        CHECK (loaded->getDocument().objects.size() == processor->getDocument().objects.size());
    }

    CHECK (licensing.server->numSent() == 0);
}

ARTFREQ_TEST (uiLicensingTwoKeysDeactivatingTheNewerKeepsTheOlder)
{
    // A buyer of ArtFreq who later bought ArtFreq Pro (for ArtFreq FX) has both keys activated.
    // Deactivate in the instrument frees the Pro key, which is the one in use: the question says
    // ArtFreq FX will be locked and ArtFreq stays, and afterwards the instrument is still
    // activated (by the ArtFreq key), so the settings stay open and the card says what went and
    // what stays; ArtFreq FX, open beside it, locks and shows the activation screen.
    const juce::ScopedJuceInitialiser_GUI gui;
    const ScopedLockedLicensing licensing;
    auto& server = *licensing.server;
    CHECK (licensing.activateArtFreqThenPro());

    Instance instrument (Variant::instrument);
    Instance effect (Variant::effect);
    CHECK (instrument.processor->isLicensed() && effect.processor->isLicensed());

    auto& editor = *instrument.editor;
    editor.showSettings (true);
    CHECK_EVENTUALLY (isShown (editor, "deactivate"));

    instrument.button ("deactivate").onClick();
    const auto warning = instrument.processor->getLicense().getDeactivationWarning();
    CHECK (warning.contains ("ArtFreq FX will be locked") && warning.contains ("ArtFreq stays activated with your ArtFreq key"));

    server.reply (deactivateArtFreq);
    instrument.button ("deactivate-confirm").onClick();
    CHECK (instrument.settle ([&] { return instrument.card().getMessageText().getMessage().isNotEmpty(); }));

    const auto said = instrument.card().getMessageText().getMessage();
    CHECK_MSG (said.startsWith ("Deactivated. Your ArtFreq Pro key can now be activated on another computer.")
               && said.endsWith ("ArtFreq FX is now locked on this computer; ArtFreq stays activated with your ArtFreq key."), said);
    CHECK (instrument.processor->isLicensed() && ! instrument.screenShown());
    CHECK (isShown (editor, "settings-panel") && isShown (editor, "licence-message") && isShown (editor, "deactivate"));
    CHECK (instrument.processor->getLicense().getActivation()->productId == anymeans::license::PRODUCT_ID_ARTFREQ);

    CHECK (pumpUntil ([&] { return ! effect.processor->isLicensed() && effect.screenShown(); }));

    // The ArtFreq key next: now the instrument locks too, and the activation screen says so.
    server.reply (deactivateArtFreq);
    instrument.button ("deactivate").onClick();
    CHECK (instrument.processor->getLicense().getDeactivationWarning().contains ("ArtFreq will be locked"));
    instrument.button ("deactivate-confirm").onClick();
    CHECK (instrument.settle ([&] { return ! instrument.processor->isLicensed(); }));
    // The editor follows the lock on its own timer: on a slow machine (the macOS runner's Intel
    // half, under Rosetta) that comes a moment after the processor's flag, so wait for it.
    CHECK (instrument.settle ([&] { return instrument.screenShown() && ! isShown (editor, "settings-panel"); }));
    CHECK (instrument.settle ([&] { return instrument.message().endsWith ("ArtFreq is now locked on this computer."); }));
}

ARTFREQ_TEST (uiLicensingAProjectClosedDuringADeactivation)
{
    // Deactivate, and the project is closed (the processor goes) before the server's answer
    // comes: the server may have freed this computer, so it must not stay activated. Once the
    // request could have ended it is locked, the activation screen says why, and the Licence card
    // offers Deactivate to finish (or Activate... to use it here again).
    const juce::ScopedJuceInitialiser_GUI gui;
    ScopedLockedLicensing licensing;
    licensing.setRequestTimeout (2000);   // (the project closes while the answer is on its way, even on a slow machine)
    auto& server = *licensing.server;
    CHECK (licensing.activatePro());

    {
        Instance closing (Variant::instrument);
        closing.editor->showSettings (true);
        CHECK_EVENTUALLY (isShown (*closing.editor, "deactivate"));
        server.replyAfter (deactivateArtFreq, 1500);
        closing.button ("deactivate").onClick();
        closing.button ("deactivate-confirm").onClick();
        CHECK (server.started.wait (testwait::timeoutMs));
        closing.editor.reset();
        closing.processor.reset();
    }

    CHECK (server.cancelsSeen.load() == 1);

    // Opened again: locked (at once, or as soon as the request could have ended).
    Instance reopened (Variant::effect);
    CHECK (pumpUntil ([&] { return ! reopened.processor->isLicensed() && reopened.screenShown(); }));
    CHECK (reopened.processor->getLicense().getInterruptedDeactivation().has_value());

    auto& editor = *reopened.editor;
    editor.showSettings (true);
    CHECK_EVENTUALLY (isShown (editor, "deactivate-confirm") && isShown (editor, "licence-activate") && ! isShown (editor, "deactivate"));

    auto* panel = findComponent (editor, "settings-panel");
    CHECK (panel != nullptr && panel->getBottom() <= editor.getBaseHeight() - 6);

    // Finish it: the server had freed this computer already.
    server.reply (deactivateArtFreqAgain);
    reopened.button ("deactivate-confirm").onClick();
    CHECK (reopened.settle ([&] { return ! reopened.processor->getLicense().getInterruptedDeactivation().has_value()
                                         && ! reopened.processor->getLicense().isBusy(); }));
    CHECK (pumpUntil ([&] { return ! isShown (editor, "settings-panel"); }));
    CHECK (reopened.screenShown() && reopened.message().startsWith ("Deactivated here"));
    CHECK (anymeans::license::LicenseStore (licensing.folder.folder).read().activations.empty());
}

ARTFREQ_TEST (uiLicensingNoResetDrawingWhileLocked)
{
    // Locked, the drawing is out of reach and undo is hidden, so the settings (open for the
    // Licence card) offer no Reset drawing: the row is not there. Activated, it is back.
    const juce::ScopedJuceInitialiser_GUI gui;

    for (const auto variant : { Variant::instrument, Variant::effect })
    {
        const ScopedLockedLicensing licensing;
        Instance instance (variant);

        ArtDocument document;
        document.objects.push_back (ArtDocument::makeTextObject ("HELLO", 3500.0f, 16000.0f));
        instance.processor->setDocument (document);

        auto& editor = *instance.editor;
        editor.showSettings (true);

        // (The settings shown first, with their Licence card: only then does a row not shown say
        // anything.)
        auto* panel = dynamic_cast<ui::shell::SettingsPanel*> (findComponent (editor, "settings-panel"));
        CHECK_EVENTUALLY (panel != nullptr && isShown (editor, "settings-panel") && isShown (editor, "licence-card"));
        CHECK (! isShown (editor, "reset-drawing") && ! isShown (editor, "reset-confirm"));
        CHECK (panel != nullptr && ! panel->isDrawingResettable());

        // (Even asked directly, the editor keeps the drawing.)
        if (panel != nullptr && panel->onResetDrawing != nullptr)
            panel->onResetDrawing();

        CHECK (instance.processor->getDocument().objects.size() == 1
               && instance.processor->getDocument().objects.front().text == "HELLO");

        // Activated (here or anywhere): the row is back, and the panel still fits.
        CHECK (licensing.activatePro());
        CHECK (pumpUntil ([&] { return isShown (editor, "reset-drawing"); }));
        CHECK (panel != nullptr && panel->getY() >= 0 && panel->getBottom() <= editor.getBaseHeight() - 6);
        CHECK (panel != nullptr && panel->getLocalBounds().contains (instance.card().getBounds()));

        instance.button ("reset-drawing").onClick();
        instance.button ("reset-confirm").onClick();
        CHECK (instance.processor->getDocument().objects.front().text != "HELLO");
    }
}
