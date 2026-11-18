#include "LicenseManager.h"

#include "LicenseKey.h"
#include "MachineId.h"
#include "SignedLicense.h"

#include <initializer_list>
#include <mutex>
#include <utility>

namespace anymeans::license
{

namespace
{

//==============================================================================
// Wording. UTF-8 punctuation is spelled out as bytes, so no compiler has to guess the source's
// encoding; each escape ends its literal, so the next character can't be read as part of it.

const char* const emDash = "\xe2\x80\x94";
const char* const ellipsis = "\xe2\x80\xa6";

juce::String fromUtf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

juce::String supportSentenceEnd()
{
    return juce::String ("write to ") + SUPPORT_EMAIL + ".";
}

juce::String noConnectionMessage()
{
    return "Couldn't reach the server " + fromUtf8 (emDash) + " check your connection and try again.";
}

juce::String serverErrorMessage (int statusCode)
{
    return "Something went wrong on our side (code " + juce::String (statusCode) + "). Try again in a minute, or "
         + supportSentenceEnd();
}

juce::String notConfirmedMessage()
{
    return "Activation couldn't be confirmed. Please try again later or " + supportSentenceEnd();
}

juce::String badKeyMessage()
{
    return "That key doesn't look right " + fromUtf8 (emDash) + " check for typos.";
}

juce::String stillActivated()
{
    return " This computer stays activated.";
}

juce::String pluginsUnlockedBy (const juce::String& productId, const juce::String& fallback)
{
    if (const auto* product = findProduct (productId))
        return fromUtf8 (product->pluginsUnlocked);

    return fallback;
}

juce::String productNameOf (const juce::String& productId)
{
    if (const auto* product = findProduct (productId))
        return fromUtf8 (product->name);

    return productId;
}

/** The server's error text, made safe to show: one line, of a sensible length. */
juce::String cleanServerText (const juce::String& text)
{
    auto clean = text.replaceCharacters ("\r\n\t", "   ").trim();

    if (clean.length() > 300)
        clean = clean.substring (0, 300).trimEnd() + fromUtf8 (ellipsis);

    return clean;
}

//==============================================================================
/** The server's JSON answer, as far as it could be read. */
struct ServerReply
{
    bool isObject = false;
    bool ok = false;
    juce::String code, error;
    juce::String token, license, signature;
    bool alreadyActive = false;
    int activationsUsed = -1, maxActivations = -1;
};

juce::String stringField (const juce::var& object, const char* name)
{
    const auto value = object.getProperty (name, {});
    return value.isString() ? value.toString() : juce::String();
}

int intField (const juce::var& object, const char* name)
{
    const auto value = object.getProperty (name, {});
    return (value.isInt() || value.isInt64() || value.isDouble()) ? static_cast<int> (value) : -1;
}

ServerReply parseReply (const juce::String& body)
{
    ServerReply reply;
    juce::var root;

    if (juce::JSON::parse (body, root).failed() || ! root.isObject())
        return reply;

    const auto ok = root.getProperty ("ok", {});
    const auto alreadyActive = root.getProperty ("alreadyActive", {});

    reply.isObject = true;
    reply.ok = ok.isBool() && static_cast<bool> (ok);
    reply.code = stringField (root, "code");
    reply.error = cleanServerText (stringField (root, "error"));
    reply.token = stringField (root, "activationToken");
    reply.license = stringField (root, "license");
    reply.signature = stringField (root, "signature");
    reply.alreadyActive = alreadyActive.isBool() && static_cast<bool> (alreadyActive);
    reply.activationsUsed = intField (root, "activationsUsed");
    reply.maxActivations = intField (root, "maxActivations");
    return reply;
}

juce::String toJson (std::initializer_list<std::pair<const char*, juce::String>> fields)
{
    auto* object = new juce::DynamicObject();
    const juce::var root (object);

    for (const auto& [name, value] : fields)
        object->setProperty (name, value);

    return juce::JSON::toString (root, true);
}

using Result = LicenseManager::Result;
using Outcome = Result::Outcome;

Result makeResult (Outcome outcome, const juce::String& message, int statusCode = 0, const ServerReply* reply = nullptr)
{
    Result result;
    result.outcome = outcome;
    result.message = message;
    result.statusCode = statusCode;

    if (reply != nullptr)
    {
        result.serverCode = reply->code;
        result.alreadyActive = reply->alreadyActive;
        result.activationsUsed = reply->activationsUsed;
        result.maxActivations = reply->maxActivations;
    }

    return result;
}

std::mutex& defaultEnvironmentLock()
{
    static std::mutex lock;
    return lock;
}

std::optional<LicenseManager::Environment>& defaultEnvironmentOverride()
{
    static std::optional<LicenseManager::Environment> environment;
    return environment;
}

LicenseManager::Environment defaultEnvironment()
{
    const std::lock_guard<std::mutex> lock (defaultEnvironmentLock());
    return defaultEnvironmentOverride().value_or (LicenseManager::Environment());
}

juce::int64 timeOf (const juce::String& iso8601)
{
    return iso8601.isEmpty() ? 0 : juce::Time::fromISO8601 (iso8601).toMilliseconds();
}

/** How long the server's records may take to show an activation everywhere (Cloudflare KV says
    "up to 60 seconds or more"), with room to spare. Measured with this computer's clock against the
    time this computer stored the activation, so no clock but this one is involved.
*/
constexpr juce::int64 serverCatchUpMs = 2 * 60 * 1000;

} // namespace

//==============================================================================
/** What the manager (on the message thread) and its request's background thread share. */
struct LicenseManager::PendingRequest
{
    /** The manager waiting for the result. Message thread only; null once the manager is gone or
        has stopped waiting, and the result is then dropped.
    */
    LicenseManager* owner = nullptr;

    CancelToken token;

    /** When the POST in progress began (0: none), for the manager's backstop deadline. */
    std::atomic<juce::int64> attemptStartedMs { 0 };
};

/** The background thread of one request. */
class LicenseManager::Worker final : public juce::Thread
{
public:
    explicit Worker (std::function<void()> jobToRun)
        : juce::Thread ("AnyMeans license request"), job (std::move (jobToRun)) {}

    void run() override   { job(); }

private:
    std::function<void()> job;
};

//==============================================================================
namespace
{

/** Everything a request's background thread needs, copied: it never touches the manager. */
struct RequestContext
{
    PluginLicensing plugin;
    juce::File sharedFolder;
    std::shared_ptr<Transport> transport;
    juce::String machineId, publicKeyHex;
    int timeoutMs = REQUEST_TIMEOUT_MS;
};

/** One POST, with the backstop deadline's bookkeeping. */
HttpReply postTo (const RequestContext& context, const char* path, const juce::String& body,
                  std::atomic<juce::int64>& attemptStartedMs, CancelToken& token)
{
    attemptStartedMs = juce::Time::currentTimeMillis();
    auto reply = context.transport->post (path, body, context.timeoutMs, token);
    attemptStartedMs = 0;
    return reply;
}

bool isStored (const LicenseStore& store, const StoredActivation& activation)
{
    const auto contents = store.read();

    for (const auto& entry : contents.activations)
        if (entry.productId == activation.productId && entry.machineId == activation.machineId
            && entry.license == activation.license && entry.signature == activation.signature)
            return true;

    return false;
}

bool isActivatedHereFor (const LicenseStore& store, const juce::String& productId, const juce::String& key, const juce::String& machineId)
{
    for (const auto& entry : store.read().activations)
        if (entry.productId == productId && LicenseKey::normalise (entry.key) == key && entry.machineId == machineId)
            return true;

    return false;
}

/** What one answer to /api/activate means. */
Result interpretActivation (const RequestContext& context, const juce::String& key,
                            const HttpReply& reply, const ServerReply& parsed)
{
    const auto status = reply.statusCode;

    if (status == 200 && parsed.ok)
    {
        // No licence at all means the server has no signing key: nothing can be stored that
        // would survive the next start, so this is no activation.
        if (parsed.license.isEmpty() || parsed.signature.isEmpty())
            return makeResult (Outcome::notConfirmed, notConfirmedMessage(), status, &parsed);

        const auto check = SignedLicense::check (parsed.license, parsed.signature, context.publicKeyHex,
                                                 context.plugin.acceptedProductIds, key, context.machineId);

        if (check != SignedLicense::Check::valid)
            return makeResult (Outcome::notConfirmed, notConfirmedMessage(), status, &parsed);

        StoredActivation activation;
        activation.productId = SignedLicense::productIdOf (parsed.license);
        activation.key = key;
        activation.machineId = context.machineId;
        activation.token = parsed.token;
        activation.license = parsed.license;
        activation.signature = parsed.signature;
        activation.activatedAt = juce::Time::getCurrentTime().toISO8601 (true);
        activation.activatedIn = context.plugin.pluginName;

        const LicenseStore store (context.sharedFolder);

        if (! store.put (activation) || ! isStored (store, activation))
            return makeResult (Outcome::notSaved,
                               "The key was accepted, but the activation couldn't be saved in "
                                 + store.getDirectory().getFullPathName()
                                 + ". Make sure that folder can be written to, then activate again (it won't use up "
                                   "another computer).",
                               status, &parsed);

        auto result = makeResult (Outcome::activated,
                                  "Activated. This computer is now unlocked for "
                                    + pluginsUnlockedBy (activation.productId, context.plugin.pluginName) + ".",
                                  status, &parsed);
        result.productId = activation.productId;
        return result;
    }

    if (status == 409 && parsed.isObject && parsed.error.isNotEmpty())
    {
        // The server's text already says where to deactivate and whom to write to; adding the
        // same again only makes it longer. Say it ourselves if it ever stops doing so.
        auto message = parsed.error;

        if (! message.containsIgnoreCase (SUPPORT_EMAIL))
            message << " Deactivate it on your old computer, or " << supportSentenceEnd();

        return makeResult (Outcome::refused, message, status, &parsed);
    }

    if (status == 403 && parsed.isObject && parsed.error.isNotEmpty())
    {
        if (parsed.code == "wrong_product" && context.plugin.wrongProductMessage.isNotEmpty())
        {
            // The server doesn't say which product the key is for; this computer may know.
            const auto knownProduct = context.plugin.knownWrongProductId.isNotEmpty()
                                   && isActivatedHereFor (LicenseStore (context.sharedFolder), context.plugin.knownWrongProductId,
                                                          key, context.machineId);

            return makeResult (Outcome::refused,
                               knownProduct ? context.plugin.knownWrongProductMessage : context.plugin.wrongProductMessage,
                               status, &parsed);
        }

        return makeResult (Outcome::refused, parsed.error, status, &parsed);
    }

    // 400, 404, 405, 429, 5xx, a 200 that isn't ok, an answer that isn't the server's JSON
    // (a Cloudflare error page, a captive portal): nothing the user can fix by changing the key.
    return makeResult (Outcome::serverError, serverErrorMessage (status), status, &parsed);
}

Result runActivation (const RequestContext& context, const juce::String& key, std::atomic<juce::int64>& attemptStartedMs,
                      CancelToken& token)
{
    const auto& products = context.plugin.acceptedProductIds;

    for (int i = 0; i < products.size(); ++i)
    {
        const auto body = toJson ({ { "key", LicenseKey::format (key) },
                                    { "machineId", context.machineId },
                                    { "productId", products[i] } });

        const auto reply = postTo (context, "/api/activate", body, attemptStartedMs, token);

        if (reply.outcome != HttpReply::Outcome::answered)
        {
            // Cancelled by the manager going away: the result goes nowhere. Cancelled by the
            // deadline, or no answer at all: the server can't be reached.
            return makeResult (Outcome::noConnection, noConnectionMessage());
        }

        const auto parsed = parseReply (reply.body);

        // The server checks the product strictly, and the instrument comes in two packages: a
        // key for the next one may be what was typed.
        if (reply.statusCode == 403 && parsed.code == "wrong_product" && i + 1 < products.size())
            continue;

        return interpretActivation (context, key, reply, parsed);
    }

    jassertfalse;  // a plug-in that accepts no product
    return makeResult (Outcome::serverError, serverErrorMessage (0));
}

Result runDeactivation (const RequestContext& context, const LicenseManager::Activation& activation, std::atomic<juce::int64>& attemptStartedMs,
                        CancelToken& token)
{
    const auto body = toJson ({ { "key", LicenseKey::format (activation.key) },
                                { "machineId", context.machineId } });

    const auto reply = postTo (context, "/api/deactivate", body, attemptStartedMs, token);

    if (reply.outcome != HttpReply::Outcome::answered)
        return makeResult (Outcome::noConnection, noConnectionMessage() + stillActivated());

    const auto parsed = parseReply (reply.body);
    const auto status = reply.statusCode;

    const auto freedOnServer = status == 200 && parsed.ok;

    // The server keeps its records in Cloudflare KV, which is eventually consistent: for about a
    // minute after an activation, a request that reaches another copy of the record may not see
    // it yet and answer not_active_here. Removing the activation then would leave the computer
    // counted on the server once the record catches up, with nothing left here to free it. So a
    // not_active_here about an activation this young is taken as "not yet", not as "already free".
    if (status == 403 && parsed.code == "not_active_here")
    {
        const auto age = juce::Time::currentTimeMillis() - timeOf (activation.activatedAt);

        if (age >= 0 && age < serverCatchUpMs)
            return makeResult (Outcome::serverError,
                               "The server hasn't caught up with this activation yet. Please try again in a minute."
                                 + stillActivated(),
                               status, &parsed);
    }

    // The server no longer counts this computer for the key (already freed, by support for
    // instance): keeping the activation would leave nothing to deactivate it with.
    const auto unknownToServer = status == 403 && parsed.isObject
                              && (parsed.code == "not_active_here" || parsed.code == "unknown_key");

    if (freedOnServer || unknownToServer)
    {
        const LicenseStore store (context.sharedFolder);

        if (! store.remove (activation.productId, activation.key, context.machineId))
            return makeResult (Outcome::notSaved,
                               "The server has freed this computer, but the activation couldn't be removed from "
                                 + store.getFile().getFullPathName() + ". Make sure it can be written to, and try again.",
                               status, &parsed);

        const auto locked = pluginsUnlockedBy (activation.productId, context.plugin.pluginName);

        auto result = makeResult (Outcome::deactivated,
                                  (freedOnServer ? juce::String ("Deactivated. Your key can now be activated on another computer. ")
                                                 : juce::String ("Deactivated here (the server no longer counted this computer for that key). "))
                                    + locked + " " + (locked.contains (" and ") ? "are" : "is") + " now locked on this computer.",
                                  status, &parsed);
        result.productId = activation.productId;
        return result;
    }

    if (status == 403 && parsed.isObject && parsed.error.isNotEmpty())
        return makeResult (Outcome::refused, parsed.error + stillActivated(), status, &parsed);

    return makeResult (Outcome::serverError, serverErrorMessage (status) + stillActivated(), status, &parsed);
}

} // namespace

//==============================================================================
LicenseManager::LicenseManager (PluginLicensing pluginToUse)
    : LicenseManager (std::move (pluginToUse), defaultEnvironment())
{
}

LicenseManager::LicenseManager (PluginLicensing pluginToUse, Environment environment)
    : plugin (std::move (pluginToUse)),
      sharedFolder (environment.sharedFolder != juce::File() ? environment.sharedFolder : LicenseStore::defaultDirectory()),
      store (sharedFolder),
      transport (environment.transport != nullptr ? environment.transport : makeHttpsTransport (SERVER_BASE_URL)),
      machineId (environment.machineId.isNotEmpty() ? environment.machineId : MachineId::get (sharedFolder)),
      publicKeyHex (environment.publicKeyHex.isNotEmpty() ? environment.publicKeyHex : juce::String (SIGNING_PUBLIC_KEY_HEX)),
      requestTimeoutMs (juce::jmax (1, environment.requestTimeoutMs))
{
    jassert (! plugin.acceptedProductIds.isEmpty());

    reload();
    startTimer (juce::jmax (10, environment.checkIntervalMs));
}

LicenseManager::~LicenseManager()
{
    stopTimer();
    *alive = false;

    if (pending != nullptr)
    {
        pending->owner = nullptr;
        pending->token.cancel();
        pending.reset();
    }

    finishWorker();
}

void LicenseManager::setDefaultEnvironmentForTesting (std::optional<Environment> environment)
{
    const std::lock_guard<std::mutex> lock (defaultEnvironmentLock());
    defaultEnvironmentOverride() = std::move (environment);
}

//==============================================================================
void LicenseManager::reload()
{
    const auto stamp = store.getStamp();
    const auto contents = store.read();

    // Locked for a moment by another program, or not readable: look again at the next check
    // rather than lock the plug-in meanwhile.
    if (contents.status == LicenseStore::Contents::Status::unreadable)
        return;

    lastStamp = stamp;

    // Damaged while this plug-in runs (another program writing it in place, say): keep what
    // was established, until the file changes again. At start-up there is nothing to keep.
    if (contents.status == LicenseStore::Contents::Status::corrupt && current.has_value())
        return;

    std::optional<Activation> best;
    juce::int64 bestTime = 0;
    bool anotherComputer = false;

    for (const auto& entry : contents.activations)
    {
        if (! plugin.acceptedProductIds.contains (entry.productId))
            continue;

        if (entry.machineId != machineId)
        {
            anotherComputer = true;
            continue;
        }

        const auto key = LicenseKey::normalise (entry.key);

        if (SignedLicense::check (entry.license, entry.signature, publicKeyHex, plugin.acceptedProductIds, key, machineId)
                != SignedLicense::Check::valid
            || SignedLicense::productIdOf (entry.license) != entry.productId)
            continue;

        const auto time = timeOf (entry.activatedAt);

        if (best.has_value() && time < bestTime)
            continue;

        Activation activation;
        activation.productId = entry.productId;
        activation.productName = productNameOf (entry.productId);
        activation.pluginsUnlocked = pluginsUnlockedBy (entry.productId, plugin.pluginName);
        activation.key = key;
        activation.maskedKey = LicenseKey::mask (key);
        activation.activatedAt = entry.activatedAt;
        activation.activatedIn = entry.activatedIn;

        best = activation;
        bestTime = time;
    }

    const auto otherComputerOnly = anotherComputer && ! best.has_value();
    const auto changed = best.has_value() != current.has_value()
                      || (best.has_value() && (best->productId != current->productId || best->key != current->key
                                               || best->activatedAt != current->activatedAt))
                      || otherComputerOnly != activationForAnotherComputer;

    activationForAnotherComputer = otherComputerOnly;

    if (! changed)
        return;

    current = best;
    activated.store (current.has_value(), std::memory_order_release);
    notifyListeners();
}

void LicenseManager::checkForChanges()
{
    if (store.getStamp() != lastStamp)
        reload();
}

void LicenseManager::timerCallback()
{
    // The transport has a timeout of its own; this is the backstop for one that hangs anyway
    // (on Windows, for instance, a stalled name lookup isn't covered by WinINet's timeouts).
    if (pending != nullptr)
    {
        const auto started = pending->attemptStartedMs.load();

        if (started > 0 && juce::Time::currentTimeMillis() - started > requestTimeoutMs + requestTimeoutMs / 4)
            pending->token.cancel();  // reported as no connection
    }

    checkForChanges();
}

//==============================================================================
juce::String LicenseManager::getStatusText() const
{
    if (requestInProgress == Request::activating)
        return "Checking your key" + fromUtf8 (ellipsis);

    if (requestInProgress == Request::deactivating)
        return "Deactivating" + fromUtf8 (ellipsis);

    if (current.has_value())
        return "Activated with your " + current->productName + " key " + current->maskedKey + ".";

    if (activationForAnotherComputer)
        return "Not activated on this computer yet. Enter your license key to activate it.";

    return "Not activated. Enter the license key from your purchase email.";
}

juce::String LicenseManager::getDeactivationWarning() const
{
    if (! current.has_value())
        return {};

    const auto& locked = current->pluginsUnlocked;

    return "Deactivating frees your key for another computer. " + locked + " "
         + (locked.contains (" and ") ? "will both" : "will") + " be locked on this computer until you activate again.";
}

//==============================================================================
bool LicenseManager::activate (const juce::String& typedKey, Callback onDone)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (isBusy())
        return false;

    const auto key = LicenseKey::normalise (typedKey);

    if (key.isEmpty())
    {
        postResultLater (std::move (onDone), makeResult (Outcome::invalidKey, "Enter the license key from your purchase email."));
        return true;
    }

    if (! LicenseKey::isWellFormed (key))
    {
        postResultLater (std::move (onDone), makeResult (Outcome::invalidKey, badKeyMessage()));
        return true;
    }

    if (machineId.isEmpty())
    {
        postResultLater (std::move (onDone),
                         makeResult (Outcome::computerUnknown,
                                     "This computer couldn't be identified, so it can't be activated. Please "
                                       + supportSentenceEnd()));
        return true;
    }

    RequestContext context { plugin, sharedFolder, transport, machineId, publicKeyHex, requestTimeoutMs };

    return startRequest (Request::activating, std::move (onDone), [context, key] (PendingRequest& request)
    {
        return runActivation (context, key, request.attemptStartedMs, request.token);
    });
}

bool LicenseManager::deactivate (Callback onDone)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (isBusy())
        return false;

    if (! current.has_value())
    {
        postResultLater (std::move (onDone), makeResult (Outcome::notActivated, "This computer isn't activated."));
        return true;
    }

    RequestContext context { plugin, sharedFolder, transport, machineId, publicKeyHex, requestTimeoutMs };
    const auto activation = *current;

    return startRequest (Request::deactivating, std::move (onDone), [context, activation] (PendingRequest& request)
    {
        return runDeactivation (context, activation, request.attemptStartedMs, request.token);
    });
}

void LicenseManager::cancelRequest()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (pending == nullptr)
        return;

    pending->owner = nullptr;
    pending->token.cancel();
    pending.reset();
    pendingCallback = nullptr;
    requestInProgress = Request::none;

    // The background thread stops by itself once its wait is interrupted; it is joined before the
    // next request, or when the manager goes.
    notifyListeners();
}

bool LicenseManager::startRequest (Request kind, Callback onDone, std::function<Result (PendingRequest&)> job)
{
    finishWorker();

    auto request = std::make_shared<PendingRequest>();
    request->owner = this;

    pending = request;
    pendingCallback = std::move (onDone);
    requestInProgress = kind;

    worker = std::make_unique<Worker> ([request, job]
    {
        const auto result = job (*request);

        juce::MessageManager::callAsync ([request, result]
        {
            if (auto* owner = request->owner)
                owner->requestFinished (*request, result);
        });
    });

    worker->startThread();
    notifyListeners();
    return true;
}

void LicenseManager::requestFinished (PendingRequest& request, const Result& result)
{
    jassert (&request == pending.get());

    request.owner = nullptr;
    pending.reset();
    requestInProgress = Request::none;
    auto onDone = std::move (pendingCallback);
    pendingCallback = nullptr;

    // The request may have changed the shared file: take the new state before saying anything.
    reload();
    notifyListeners();

    if (onDone != nullptr)
        onDone (result);
}

void LicenseManager::finishWorker()
{
    // The thread is done or about to be (its request was answered or cancelled), so this returns
    // at once; the limit only matters if a network call ignored the cancel.
    if (worker != nullptr)
    {
        worker->stopThread (10000);
        worker.reset();
    }
}

void LicenseManager::postResultLater (Callback onDone, Result result)
{
    if (onDone == nullptr)
        return;

    std::weak_ptr<bool> stillAlive (alive);

    juce::MessageManager::callAsync ([stillAlive, done = std::move (onDone), toDeliver = std::move (result)]
    {
        if (const auto flag = stillAlive.lock(); flag != nullptr && *flag)
            done (toDeliver);
    });
}

void LicenseManager::notifyListeners()
{
    listeners.call ([this] (Listener& listener) { listener.licenseStateChanged (*this); });
}

} // namespace anymeans::license
