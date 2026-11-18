#pragma once

#include "LicenseConfig.h"
#include "LicenseStore.h"
#include "LicenseTransport.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace anymeans::license
{

/** Licensing for one plug-in instance: whether this computer is activated for the plug-in,
    activating it with a key, and deactivating it.

    The state comes from the shared AnyMeans file (LicenseStore): an activation counts when its
    licence, signed by the server, checks out for a product the plug-in accepts, for its key and
    for this computer (SignedLicense). It is read when the manager is made and again whenever the
    file changes (checked about once a second), so a key entered in one plug-in, or in another
    instance, unlocks this one too without a restart. Activating needs the server once; after
    that nothing here touches the network.

    Threads: isActivated() and getActivatedFlag() may be called from any thread, the audio thread
    included (they read an atomic). Everything else belongs to the message thread, and so do the
    callbacks and listener calls. Requests run on a background thread of their own, one at a time.
*/
class LicenseManager : private juce::Timer
{
public:
    //==============================================================================
    /** What a manager works with. Each part left empty is the real thing; tests and tools replace
        parts of it (a folder of their own, a fake server, a fixed machine ID and the public key of
        a test key pair). Only code can do that: no file, setting, environment variable or
        command-line switch changes any of it.
    */
    struct Environment
    {
        juce::File sharedFolder;               // empty: LicenseStore::defaultDirectory(), the computer's
        juce::File userFolder;                 // the account's own, read too and written when the shared
                                               // one can't be. Empty: LicenseStore::userDirectory() if
                                               // sharedFolder is empty too, else none
        std::shared_ptr<Transport> transport;  // empty: HTTPS to SERVER_BASE_URL
        juce::String machineId;                // empty: MachineId::get()
        juce::String publicKeyHex;             // empty: SIGNING_PUBLIC_KEY_HEX
        int requestTimeoutMs = REQUEST_TIMEOUT_MS;
        int checkIntervalMs = 1000;            // how often the shared file is checked for changes
    };

    /** A manager for this plug-in, with the real environment (or the one set by
        setDefaultEnvironmentForTesting()).
    */
    explicit LicenseManager (PluginLicensing plugin);

    LicenseManager (PluginLicensing plugin, Environment environment);

    /** Stops a request in progress (its callback is then never called). */
    ~LicenseManager() override;

    /** For test code and tools only: the environment managers made without one get from now on,
        for test runners that create plug-in instances the ordinary way. std::nullopt goes back to
        the real one. Shipping plug-ins never call it.
    */
    static void setDefaultEnvironmentForTesting (std::optional<Environment> environment);

    //==============================================================================
    /** True if this computer is activated for this plug-in. Lock-free: any thread. */
    bool isActivated() const noexcept                           { return activated.load (std::memory_order_acquire); }

    /** The flag isActivated() reads, for code that wants to hold on to it. */
    const std::atomic<bool>& getActivatedFlag() const noexcept  { return activated; }

    /** The activation this plug-in uses on this computer. */
    struct Activation
    {
        juce::String productId;        // the product the key is for
        juce::String productName;      // "ArtFreq Pro"
        juce::String pluginsUnlocked;  // "ArtFreq and ArtFreq FX": what this activation unlocks here
        juce::String key;              // stored form, for deactivating; show maskedKey instead
        juce::String maskedKey;        // "8K3QX-*****-*****-NV6ZH", with bullets (U+2022)
        juce::String activatedAt;      // ISO 8601
        juce::String activatedIn;      // the plug-in it was entered in

        /** Of the plug-ins this activation unlocks, those another key activated on this computer
            keeps unlocked without it ("ArtFreq", when this is an ArtFreq Pro key and there is an
            ArtFreq key too), and that key's product ("ArtFreq"). Empty: none. */
        juce::StringArray pluginsKeptByAnotherKey;
        juce::String anotherKeyProduct;

        /** getInterruptedDeactivation(): when that deactivation was started. */
        juce::String deactivatingSince;
    };

    /** The activation in use, if the plug-in is activated. If the file holds several that apply
        (an ArtFreq key and an ArtFreq Pro key), the most recent.
    */
    const std::optional<Activation>& getActivation() const noexcept   { return current; }

    /** An activation whose deactivation was started on this computer and cut off before the
        server's answer was known (the project closed or the DAW quit meanwhile): the server may
        have freed this computer already, so it no longer counts, and deactivate() finishes the
        job. Activating again with the key (which the server answers with no extra computer, or
        with the place it freed) replaces it.
    */
    const std::optional<Activation>& getInterruptedDeactivation() const noexcept   { return interrupted; }

    enum class Request { none, activating, deactivating };

    /** The request in progress, if any. */
    Request getRequestInProgress() const noexcept   { return requestInProgress; }
    bool isBusy() const noexcept                    { return requestInProgress != Request::none; }

    /** One line for the interface: activated with which key, not activated, a deactivation that
        didn't finish, or what is happening. */
    juce::String getStatusText() const;

    /** What deactivating would do here, for a confirmation: which plug-ins it locks on this
        computer (deactivating an ArtFreq Pro activation locks ArtFreq and ArtFreq FX, since they
        share it), and which another key keeps unlocked. Empty when not activated.
    */
    juce::String getDeactivationWarning() const;

    //==============================================================================
    struct Result
    {
        enum class Outcome
        {
            activated,        // activated and saved on this computer
            deactivated,      // the activation is gone from this computer
            invalidKey,       // empty, or not a key (a typo): nothing was sent
            refused,          // the server said no: wrong key or product, or no computer left
            notConfirmed,     // the server said yes, but sent no licence or one that doesn't check out
            serverError,      // the server answered with an error or something unexpected
            noConnection,     // no answer: offline, or it took too long
            notSaved,         // the shared file couldn't be written
            notActivated,     // deactivate, with nothing to deactivate
            computerUnknown   // this computer has no ID to activate under
        };

        Outcome outcome = Outcome::serverError;
        juce::String message;         // ready to show, in full sentences
        int statusCode = 0;           // the HTTP status, when the server answered
        juce::String serverCode;      // the server's code ("activation_limit"), when it sent one
        juce::String productId;       // activated / deactivated: the product
        bool alreadyActive = false;   // activated: this computer already had this key (no extra computer used)
        int activationsUsed = -1;     // when the server said: computers now using the key
        int maxActivations = -1;      // ... and how many it allows

        bool succeeded() const noexcept   { return outcome == Outcome::activated || outcome == Outcome::deactivated; }
    };

    using Callback = std::function<void (const Result&)>;

    /** Activates this computer with a key as the user typed or pasted it (any case, with or without
        dashes and spaces). The key's checksum is checked first, so a typo is reported at once
        without asking the server. The callback comes on the message thread, after the state has
        been updated (and listeners told); it is never called from inside this function.

        Returns false, and does nothing, if a request is already in progress.
    */
    bool activate (const juce::String& typedKey, Callback onDone);

    /** Frees this computer's place on the server and removes the activation here (the way to move
        to a new computer). The activation is kept unless the server confirms (or no longer knows
        this computer for the key, and the activation is over two minutes old: a younger one may
        just not have reached every copy of the server's records yet): otherwise the key would
        stay taken on the server with no way to free it from here. Returns false, and does
        nothing, if a request is in progress.

        The activation is marked in the shared file before anything is sent, and the mark taken
        off again if the server answers that it did not free the computer, or can't be reached.
        If the request is cut off instead (the manager goes: the project closed, the DAW quit),
        the mark stays, so the activation no longer counts anywhere once the request could have
        ended (getInterruptedDeactivation()): the server may have freed this computer. Not
        activated, this finishes an interrupted deactivation.
    */
    bool deactivate (Callback onDone);

    /** Stops the request in progress, if any (when the editor closes, say); its callback is never
        called. What the server did before the request stopped stands: activating again later is
        free on a computer the server already counts.
    */
    void cancelRequest();

    /** Reads the shared file again now. The manager does this by itself when the file changes. */
    void reload();

    //==============================================================================
    struct Listener
    {
        virtual ~Listener() = default;

        /** The activation, or the request in progress, changed. Message thread. */
        virtual void licenseStateChanged (LicenseManager&) = 0;
    };

    void addListener (Listener* listener)       { listeners.add (listener); }
    void removeListener (Listener* listener)    { listeners.remove (listener); }

    //==============================================================================
    const PluginLicensing& getPluginLicensing() const noexcept  { return plugin; }
    const juce::String& getMachineId() const noexcept           { return machineId; }

    /** The computer's shared store, and then the account's own if it is read too. */
    const std::vector<LicenseStore>& getStores() const noexcept   { return stores; }
    const LicenseStore& getStore() const noexcept                 { return stores.front(); }

private:
    struct PendingRequest;
    class Worker;

    void timerCallback() override;
    void checkForChanges();
    bool startRequest (Request kind, Callback onDone, std::function<Result (PendingRequest&)> job);
    void requestFinished (PendingRequest& request, const Result& result);
    void finishWorker();
    void postResultLater (Callback onDone, Result result);
    void notifyListeners();

    const PluginLicensing plugin;
    const std::vector<LicenseStore> stores;
    const std::shared_ptr<Transport> transport;
    const juce::String machineId;
    const juce::String publicKeyHex;
    const int requestTimeoutMs;

    std::atomic<bool> activated { false };
    static_assert (std::atomic<bool>::is_always_lock_free, "the audio thread reads this flag");

    std::optional<Activation> current, interrupted;
    bool activationForAnotherComputer = false;
    std::vector<LicenseStore::Stamp> lastStamps;
    juce::int64 reloadAtMs = 0;   // a deactivation mark that will be old enough to count then (0: none)

    Request requestInProgress = Request::none;
    std::shared_ptr<PendingRequest> pending;
    Callback pendingCallback;
    std::unique_ptr<Worker> worker;
    std::shared_ptr<bool> alive { std::make_shared<bool> (true) };

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};

} // namespace anymeans::license
