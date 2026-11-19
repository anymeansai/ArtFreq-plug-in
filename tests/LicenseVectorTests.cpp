// Licensing, checked against a second, independent set of vectors (tests/data/license-vectors.json)
// and against the cases a review of the first version found untested:
//
//   - the deployed Worker's own code (worker.js + license.js, run once in Node 22 with an in-memory
//     KV and a throwaway Ed25519 key of its own) driving the manager: every answer it gives the
//     plug-ins, byte for byte;
//   - Ed25519 against WebCrypto's verdicts on a few hundred signatures and their alterations (one
//     bit of message, R, S or key; S + L, S + 2L, S = L, S = 0, the top bit of S; small-order R and
//     A), SHA-512 against Node's crypto, the key checksum against the server's isValidKeyFormat;
//   - the public key built into the plug-ins: a point of the curve, of prime order;
//   - the shared folder's lock against writers in the same process (as a second AnyMeans plug-in
//     module in one host is), an activation the folder can't take, the machine ID JUCE gives a
//     computer it knows nothing about, deactivating while the server's records catch up, and
//     answers or files nested deep enough to overflow the JSON parser's stack.
//
// Nothing here uses the network.

#include "LicenseTestSupport.h"
#include "TestFramework.h"

#include "license/Crypto.h"
#include "license/LicenseConfig.h"
#include "license/LicenseKey.h"
#include "license/LicenseManager.h"
#include "license/LicenseStore.h"
#include "license/MachineId.h"
#include "license/Monocypher.h"
#include "license/SafeJson.h"
#include "license/SignedLicense.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace anymeans::license;
using namespace licensetest;

namespace
{

using Outcome = LicenseManager::Result::Outcome;

/** The vectors file, parsed once. */
const juce::var& vectors()
{
    static const juce::var parsed = juce::JSON::parse (juce::File (ARTFREQ_TEST_DATA_DIR).getChildFile ("license-vectors.json"));
    return parsed;
}

bool haveVectors()
{
    const auto ok = vectors().isObject();
    CHECK_MSG (ok, "tests/data/license-vectors.json is missing or not JSON");
    return ok;
}

std::vector<std::uint8_t> bytesFromHex (const juce::String& hex)
{
    const auto block = decodeHex (hex);

    if (! block.has_value())
        return {};

    const auto* data = static_cast<const std::uint8_t*> (block->getData());
    return { data, data + block->getSize() };
}

bool ourVerdict (const std::vector<std::uint8_t>& publicKey, const std::vector<std::uint8_t>& signature,
                 const std::vector<std::uint8_t>& message)
{
    return publicKey.size() == 32 && signature.size() == 64
        && verifyEd25519 (signature.data(), publicKey.data(), message.data(), message.size());
}

//==============================================================================
/** The Worker's run: its public key, keys, this computer's ID and its answers. */
struct WorkerRun
{
    juce::String publicKey, keyArtFreq, keyPro, keyCreme, machineHere;

    Reply reply (const char* name) const
    {
        const auto r = vectors()["worker"]["replies"][name];
        bodies.push_back (std::make_unique<std::string> (r["body"].toString().toStdString()));
        return { static_cast<int> (r["status"]), bodies.back()->c_str() };
    }

    mutable std::vector<std::unique_ptr<std::string>> bodies;
};

WorkerRun workerRun()
{
    const auto worker = vectors()["worker"];
    WorkerRun run;
    run.publicKey = worker["publicKey"].toString();
    run.keyArtFreq = worker["keys"]["artFreq"].toString();
    run.keyPro = worker["keys"]["pro"].toString();
    run.keyCreme = worker["keys"]["creme"].toString();
    run.machineHere = worker["machineHere"].toString();
    return run;
}

LicenseManager::Environment workerEnvironment (const juce::File& folder, std::shared_ptr<Transport> server, const WorkerRun& run)
{
    auto environment = makeEnvironment (folder, std::move (server), run.machineHere);
    environment.publicKeyHex = run.publicKey;
    return environment;
}

LicenseManager::Result activateNow (LicenseManager& manager, const juce::String& typedKey)
{
    int calls = 0;
    LicenseManager::Result result;
    CHECK (manager.activate (typedKey, [&] (const LicenseManager::Result& r) { ++calls; result = r; }));
    CHECK (pumpUntil ([&] { return calls > 0; }));
    return result;
}

LicenseManager::Result deactivateNow (LicenseManager& manager)
{
    int calls = 0;
    LicenseManager::Result result;
    CHECK (manager.deactivate ([&] (const LicenseManager::Result& r) { ++calls; result = r; }));
    CHECK (pumpUntil ([&] { return calls > 0; }));
    return result;
}

} // namespace

//==============================================================================
// The public key built into the plug-ins
//==============================================================================

ARTFREQ_TEST (licenseShippedPublicKeyIsAPrimeOrderCurvePoint)
{
    namespace mc = anymeans_monocypher;

    // The key the deployed Worker signs with (its operator's message of Nov 18 2026).
    CHECK (juce::String (SIGNING_PUBLIC_KEY_HEX) == "7acdbd078095b8d36a9c91dfaa2166c820fbb382bc71d8659bfd1153231e2307");

    // A "signature" R = [1]B, S = 1 makes Monocypher's check [8]([S]B - [h]A - R) = 0 test the key
    // alone: with h = 0 it holds exactly when A decodes to a curve point; with h = 8 it holds only
    // if [64]A = 0, i.e. A has small order. A small-order key would accept forgeries for every
    // message (R = 0, S = 0), and a key that isn't a point would refuse every licence: either would
    // be a disaster nobody notices until buyers do.
    std::uint8_t scalarOne[32] = { 1 };
    std::uint8_t signature[64] = {};
    mc::crypto_eddsa_scalarbase (signature, scalarOne);
    signature[32] = 1;

    const std::uint8_t hashZero[32] = {};
    const std::uint8_t hashEight[32] = { 8 };

    const auto decodes = [&] (const std::vector<std::uint8_t>& key)
    {
        return key.size() == 32 && mc::crypto_eddsa_check_equation (signature, key.data(), hashZero) == 0;
    };

    const auto hasSmallOrder = [&] (const std::vector<std::uint8_t>& key)
    {
        return key.size() == 32 && mc::crypto_eddsa_check_equation (signature, key.data(), hashEight) == 0;
    };

    const auto shipped = bytesFromHex (SIGNING_PUBLIC_KEY_HEX);
    CHECK (shipped.size() == 32);
    CHECK (decodes (shipped));
    CHECK (! hasSmallOrder (shipped));

    // The method tells them apart: the identity (small order) and y = 2 (not a point).
    std::vector<std::uint8_t> identity (32, 0), notAPoint (32, 0);
    identity[0] = 1;
    notAPoint[0] = 2;
    CHECK (decodes (identity) && hasSmallOrder (identity));
    CHECK (! decodes (notAPoint));
    CHECK (decodes (bytesFromHex (throwawayPublicKeyHex)) && ! hasSmallOrder (bytesFromHex (throwawayPublicKeyHex)));
}

//==============================================================================
// Against WebCrypto and Node's crypto
//==============================================================================

ARTFREQ_TEST (licenseEd25519AgreesWithWebCryptoOnEveryAlteration)
{
    if (! haveVectors())
        return;

    const auto* cases = vectors()["ed25519"].getArray();
    CHECK (cases != nullptr && cases->size() > 300);

    if (cases == nullptr)
        return;

    int compared = 0, good = 0, licencesThroughStrings = 0, smallOrderForgeries = 0;

    for (const auto& c : *cases)
    {
        const auto kind = c["kind"].toString();
        const auto publicKey = bytesFromHex (c["pub"].toString());
        const auto signature = bytesFromHex (c["sig"].toString());
        const auto message = bytesFromHex (c["msg"].toString());
        const auto webCrypto = static_cast<bool> (c["node"]);
        const auto ours = ourVerdict (publicKey, signature, message);

        if (kind == "forgery with small-order key")
        {
            // R = 0, S = 0 under a small-order key: Monocypher's cofactored check accepts all eight,
            // OpenSSL's some. Never a question for the plug-ins, whose key is fixed and of prime
            // order (licenseShippedPublicKeyIsAPrimeOrderCurvePoint); counted to show the vectors
            // were read.
            ++smallOrderForgeries;
            continue;
        }

        ++compared;
        CHECK_MSG (ours == webCrypto, kind << ": WebCrypto says " << (webCrypto ? "valid" : "invalid") << ", the plug-in "
                                           << (ours ? "valid" : "invalid") << " (sig " << c["sig"].toString() << ")");

        if (kind == "good")
        {
            ++good;
            CHECK (webCrypto);

            // A realistic licence also goes through the string path the plug-in uses (the other
            // messages are random bytes, not text).
            const std::string raw (message.begin(), message.end());

            if (raw.rfind ("AMLIC1|", 0) == 0)
            {
                ++licencesThroughStrings;
                const auto text = juce::String::fromUTF8 (raw.data(), static_cast<int> (raw.size()));
                const auto base64 = juce::Base64::toBase64 (signature.data(), signature.size());
                CHECK (SignedLicense::isSignedBy (text, base64, c["pub"].toString()));
                CHECK (! SignedLicense::isSignedBy (text + " ", base64, c["pub"].toString()));
            }
        }
        else
        {
            CHECK_MSG (! ours, kind << " verified");
        }
    }

    CHECK (good == 30);
    CHECK (licencesThroughStrings == 16);
    CHECK (smallOrderForgeries == 8);
    CHECK (compared > 350);
}

ARTFREQ_TEST (licenseSha512AgreesWithNodeOnRandomInput)
{
    if (! haveVectors())
        return;

    const auto* cases = vectors()["sha512"].getArray();
    CHECK (cases != nullptr && cases->size() == 24);

    if (cases == nullptr)
        return;

    for (const auto& c : *cases)
    {
        const auto data = bytesFromHex (c["data"].toString());
        const auto digest = Sha512::of (data.data(), data.size());
        CHECK_MSG (juce::String::toHexString (digest.data(), static_cast<int> (digest.size()), 0) == c["digest"].toString(),
                   "length " << data.size());
    }
}

ARTFREQ_TEST (licenseKeyChecksumAgreesWithTheServerOnTypos)
{
    if (! haveVectors())
        return;

    const auto* cases = vectors()["keys"].getArray();
    CHECK (cases != nullptr && cases->size() == 144);

    if (cases == nullptr)
        return;

    int accepted = 0;

    // Keys the server made, each with one character changed and two swapped: the plug-in's offline
    // check says what the server's isValidKeyFormat says, however the key is typed.
    for (const auto& c : *cases)
    {
        const auto key = c["key"].toString();
        const auto server = static_cast<bool> (c["server"]);

        CHECK_MSG (LicenseKey::isWellFormed (LicenseKey::normalise (key)) == server, key);
        CHECK_MSG (LicenseKey::isWellFormed (LicenseKey::normalise (" " + LicenseKey::format (key).toLowerCase() + "\n")) == server, key);
        accepted += server ? 1 : 0;
    }

    CHECK (accepted >= 48);  // the 48 genuine keys, and the rare typo the checksum can't see
}

ARTFREQ_TEST (licenseMachineIdHashMatchesNode)
{
    if (! haveVectors())
        return;

    // Node's SHA-256 of "AnyMeans|skeptic here", the ID the Worker run was made for.
    CHECK (MachineId::hash ("skeptic here") == workerRun().machineHere);
}

//==============================================================================
// The Worker's own answers, through the manager
//==============================================================================

ARTFREQ_TEST (licenseManagerHandlesTheWorkersOwnAnswers)
{
    if (! haveVectors())
        return;

    const juce::ScopedJuceInitialiser_GUI gui;
    const auto run = workerRun();

    // An ArtFreq key in the instrument: one request, stored exactly as the Worker sent it.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (run.reply ("artFresh"));

        LicenseManager instrument (artFreqInstrumentLicensing(), workerEnvironment (temporary.folder, server, run));
        const auto result = activateNow (instrument, LicenseKey::format (run.keyArtFreq).toLowerCase());
        CHECK (result.outcome == Outcome::activated);
        CHECK (result.productId == PRODUCT_ID_ARTFREQ);
        CHECK (instrument.isActivated());
        CHECK (! LicenseManager (artFreqEffectLicensing(), workerEnvironment (temporary.folder, server, run)).isActivated());

        const auto stored = LicenseStore (temporary.folder).read().activations;
        const auto sentBody = juce::JSON::parse (juce::String::fromUTF8 (run.reply ("artFresh").body));
        CHECK (stored.size() == 1 && stored[0].license == sentBody["license"].toString() && stored[0].signature == sentBody["signature"].toString());

        // The same machine again: free, and still one entry.
        server->reply (run.reply ("artAgain"));
        const auto again = activateNow (instrument, run.keyArtFreq);
        CHECK (again.outcome == Outcome::activated && again.alreadyActive);
        CHECK (LicenseStore (temporary.folder).read().activations.size() == 1);

        // Deactivating at once: the Worker's records may not show the activation everywhere yet,
        // so its not_active_here is not believed (the activation is minutes old at most) ...
        server->reply (run.reply ("deactivateArtAgain"));
        const auto early = deactivateNow (instrument);
        CHECK (early.outcome == Outcome::serverError);
        CHECK (early.message.startsWith ("The server hasn't caught up with this activation yet."));
        CHECK (instrument.isActivated());

        // ... its ok is.
        server->reply (run.reply ("deactivateArt"));
        const auto freed = deactivateNow (instrument);
        CHECK (freed.outcome == Outcome::deactivated);
        CHECK (! instrument.isActivated());
        CHECK (LicenseStore (temporary.folder).read().activations.empty());
    }

    // An ArtFreq Pro key in the instrument: wrong product for ArtFreq, then activated as Pro, which
    // unlocks ArtFreq FX on this computer too.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (run.reply ("proAsArt"));
        server->reply (run.reply ("proFresh"));

        LicenseManager instrument (artFreqInstrumentLicensing(), workerEnvironment (temporary.folder, server, run));
        const auto result = activateNow (instrument, run.keyPro);
        CHECK (result.outcome == Outcome::activated && result.productId == PRODUCT_ID_ARTFREQ_PRO);
        CHECK (server->numSent() == 2);
        CHECK (LicenseManager (artFreqEffectLicensing(), workerEnvironment (temporary.folder, server, run)).isActivated());
    }

    // An ArtFreq key in ArtFreq FX, a Creme key in the instrument, the Worker without a signing key,
    // and a key already on two computers.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (run.reply ("artAsPro"));
        server->reply (run.reply ("cremeAsArt"));
        server->reply (run.reply ("cremeAsPro"));
        server->reply (run.reply ("artUnsigned"));
        server->reply (run.reply ("proLimit"));

        LicenseManager effect (artFreqEffectLicensing(), workerEnvironment (temporary.folder, server, run));
        LicenseManager instrument (artFreqInstrumentLicensing(), workerEnvironment (temporary.folder, server, run));

        const auto artFreqInEffect = activateNow (effect, run.keyArtFreq);
        CHECK (artFreqInEffect.outcome == Outcome::refused);
        CHECK (artFreqInEffect.message.startsWith (artFreqEffectLicensing().wrongProductMessage));
        CHECK (artFreqInEffect.message.contains (SUPPORT_EMAIL));

        const auto creme = activateNow (instrument, run.keyCreme);
        CHECK (creme.outcome == Outcome::refused);
        CHECK (creme.message == "That key is for a different AnyMeans Audio plug-in. If you think this is wrong, write to "
                                + juce::String (SUPPORT_EMAIL) + ".");

        const auto unsignedReply = activateNow (instrument, run.keyArtFreq);
        CHECK (unsignedReply.outcome == Outcome::notConfirmed);
        CHECK (unsignedReply.message == "Activation couldn't be confirmed. Please try again later or write to support@anymeansaudio.com.");

        const auto full = activateNow (effect, run.keyPro);
        CHECK (full.outcome == Outcome::refused && full.statusCode == 409);
        CHECK (full.message == "This key is already active on 2 machines. Deactivate it there first (or write to support@anymeansaudio.com).");

        CHECK (! effect.isActivated() && ! instrument.isActivated());
        CHECK (! LicenseStore (temporary.folder).getFile().exists());
        CHECK (server->numSent() == 5);
    }

    // What the Worker does with requests the plug-ins never send, and why the licence's product
    // field is checked: it trims the machine ID it is sent (the licence carries the trimmed one),
    // and without a productId it skips its product check altogether and signs whatever product
    // the key is for.
    {
        const auto padded = juce::JSON::parse (juce::String::fromUTF8 (run.reply ("paddedMachine").body));
        CHECK (SignedLicense::check (padded["license"].toString(), padded["signature"].toString(), run.publicKey,
                                     { PRODUCT_ID_CREME }, run.keyCreme, run.machineHere) == SignedLicense::Check::valid);

        const auto noProduct = juce::JSON::parse (juce::String::fromUTF8 (run.reply ("noProduct").body));
        CHECK (SignedLicense::check (noProduct["license"].toString(), noProduct["signature"].toString(), run.publicKey,
                                     artFreqInstrumentLicensing().acceptedProductIds, run.keyCreme,
                                     MachineId::hash ("skeptic a1")) == SignedLicense::Check::wrongProduct);
    }
}

//==============================================================================
// Deactivating while the server's records catch up
//==============================================================================

ARTFREQ_TEST (licenseDeactivationWaitsForTheServerToSeeAFreshActivation)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    struct Case
    {
        juce::String activatedAt;
        Reply reply;
        bool removed;
    };

    const auto now = juce::Time::getCurrentTime();

    const Case cases[] =
    {
        { now.toISO8601 (true),                                         deactivateArtFreqAgain, false },  // not_active_here, seconds after activating
        { (now - juce::RelativeTime::seconds (100)).toISO8601 (true),   deactivateArtFreqAgain, false },
        { (now - juce::RelativeTime::minutes (3)).toISO8601 (true),     deactivateArtFreqAgain, true },   // long enough: believed
        { (now + juce::RelativeTime::days (1)).toISO8601 (true),        deactivateArtFreqAgain, true },   // a clock set back never blocks it
        { "not a time",                                                 deactivateArtFreqAgain, true },
        { now.toISO8601 (true),                                         deactivateUnknownKey,   true },   // the key is gone: nothing to wait for
        { now.toISO8601 (true),                                         deactivateArtFreq,      true },   // freed
    };

    for (const auto& testCase : cases)
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (testCase.reply);

        auto activation = storedActivationFrom (activateArtFreqFresh, keyArtFreq);
        activation.activatedAt = testCase.activatedAt;
        CHECK (LicenseStore (temporary.folder).put (activation));

        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        CHECK (instrument.isActivated());

        const auto result = deactivateNow (instrument);
        CHECK_MSG (instrument.isActivated() == ! testCase.removed, testCase.activatedAt << ": " << result.message);
        CHECK (LicenseStore (temporary.folder).read().activations.size() == (testCase.removed ? 0u : 1u));

        if (! testCase.removed)
        {
            CHECK (result.outcome == Outcome::serverError);
            CHECK (result.message == "The server hasn't caught up with this activation yet. Please try again in a minute. "
                                     "This computer stays activated.");
        }
    }
}

//==============================================================================
// The shared folder
//==============================================================================

ARTFREQ_TEST (licenseStoreFolderLockKeepsOutWritersInThisProcessToo)
{
    const TemporaryFolder temporary;

    // A second plug-in module in the same host process is just another lock in the same process:
    // it must wait, as one in another process would.
    {
        const LicenseStore::FolderLock first (temporary.folder, 1000);
        CHECK (first.isLocked());

        bool secondLocked = true;
        juce::uint32 waited = 0;

        std::thread other ([&]
        {
            const auto started = juce::Time::getMillisecondCounter();
            const LicenseStore::FolderLock second (temporary.folder, 300);
            secondLocked = second.isLocked();
            waited = juce::Time::getMillisecondCounter() - started;
        });

        other.join();
        CHECK (! secondLocked);
        CHECK (waited >= 250);
    }

    {
        const LicenseStore::FolderLock again (temporary.folder, 0);
        CHECK (again.isLocked());  // free again once the first is gone
    }

    // Read-modify-write under the lock alone (no lock inside the module): no update is lost. (Each
    // writer waits as long as it takes: on a slow, busy machine one can be kept out for seconds.)
    const auto counter = temporary.folder.getChildFile ("counter.txt");
    CHECK (counter.replaceWithText ("0"));

    std::atomic<int> notLocked { 0 };
    std::vector<std::thread> writers;

    for (int t = 0; t < 4; ++t)
    {
        writers.emplace_back ([&]
        {
            for (int i = 0; i < 50; ++i)
            {
                const LicenseStore::FolderLock lock (temporary.folder, 60000);
                notLocked += lock.isLocked() ? 0 : 1;

                const auto value = counter.loadFileAsString().getIntValue();
                std::this_thread::yield();
                counter.replaceWithText (juce::String (value + 1));
            }
        });
    }

    for (auto& writer : writers)
        writer.join();

    CHECK (notLocked == 0);
    CHECK (counter.loadFileAsString().getIntValue() == 200);

    // And the store itself: four writers at once, each with a store of its own, lose nothing.
    std::vector<std::thread> stores;
    std::atomic<int> failedPuts { 0 };

    for (int t = 0; t < 4; ++t)
    {
        stores.emplace_back ([&, t]
        {
            const LicenseStore store (temporary.folder);

            for (int i = 0; i < 10; ++i)
            {
                auto activation = storedActivationFrom (activateProFresh, keyPro);
                activation.key = "K" + juce::String (t) + "X" + juce::String (i);
                failedPuts += store.put (activation) ? 0 : 1;
            }
        });
    }

    for (auto& thread : stores)
        thread.join();

    CHECK (failedPuts == 0);
    CHECK (LicenseStore (temporary.folder).read().activations.size() == 40);
}

ARTFREQ_TEST (licenseActivationTheFolderCantTakeIsReportedNotStored)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();

    // A file where the shared folder should be: nothing can be written there (root can't be kept
    // out of a folder by its permissions, so this stands in for a folder that can't be written).
    const auto blocked = temporary.folder.getChildFile ("AnyMeans");
    CHECK (blocked.replaceWithText ("not a folder"));

    server->reply (activateArtFreqFresh);
    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (blocked, server));

    const auto result = activateNow (instrument, keyArtFreq);
    CHECK (result.outcome == Outcome::notSaved);
    CHECK (result.message.contains (blocked.getFullPathName()));
    CHECK (result.message.contains ("it won't use up another computer"));
    CHECK (! instrument.isActivated());
    CHECK (server->numSent() == 1);

    // Once the folder can be written, activating again works (and the server counts no new computer).
    CHECK (blocked.deleteFile());
    server->reply (activateArtFreqAgain);
    const auto again = activateNow (instrument, keyArtFreq);
    CHECK (again.outcome == Outcome::activated && again.alreadyActive);
    CHECK (instrument.isActivated());
}

//==============================================================================
// The machine ID JUCE gives a computer it knows nothing about
//==============================================================================

ARTFREQ_TEST (licenseMachineIdRefusesJucesIdForNoHardware)
{
    CHECK (! MachineId::isUsableHardwareIdentifier ("0"));
    CHECK (! MachineId::isUsableHardwareIdentifier (" 0\n"));
    CHECK (! MachineId::isUsableHardwareIdentifier (""));
    CHECK (MachineId::isUsableHardwareIdentifier ("-6312218476543261702"));
    CHECK (MachineId::isUsableHardwareIdentifier ("0A1B2C3D4E5F"));

    // Two computers that both report "0" get IDs of their own, each stable.
    const TemporaryFolder first, second;
    const auto one = MachineId::chooseIdentifier ({ "0" }, "0", first.folder);
    const auto two = MachineId::chooseIdentifier ({ "0" }, "0", second.folder);

    CHECK (one.startsWith ("random:"));
    CHECK (two.startsWith ("random:"));
    CHECK (one != two);
    CHECK (MachineId::chooseIdentifier ({}, "0", first.folder) == one);
    CHECK (MachineId::chooseIdentifier ({ "0", "1234" }, "0", first.folder) == "1234");
    CHECK (MachineId::chooseIdentifier ({ "0" }, "5678", first.folder) == "5678");

    // This computer's own ID is never JUCE's "0".
    CHECK (MachineId::get (first.folder) != MachineId::hash ("0"));
}

//==============================================================================
// Text nested deep enough to overflow the parser's stack
//==============================================================================

ARTFREQ_TEST (licenseJsonTooDeepIsRefusedNotParsed)
{
    const auto nested = [] (int depth, const juce::String& inside = {})
    {
        return juce::String::repeatedString ("[", depth) + inside + juce::String::repeatedString ("]", depth);
    };

    juce::var result;
    CHECK (parseShallowJson (nested (32), result).wasOk());
    CHECK (parseShallowJson (nested (33), result).failed());
    CHECK (parseShallowJson ("{\"a\": [ {\"b\": 1} ]}", result).wasOk() && result["a"][0]["b"].equals (1));

    // Brackets inside strings, in either kind of quotes, with escaped quotes, don't count ...
    CHECK (parseShallowJson ("[\"" + juce::String::repeatedString ("[", 100) + "\\\"[[[\"]", result).wasOk());
    CHECK (parseShallowJson ("['" + juce::String::repeatedString ("{", 100) + "\\'{{{']", result).wasOk());

    // ... and a quote of the other kind inside a string doesn't hide what follows it.
    CHECK (parseShallowJson ("['\"'," + nested (40) + "]", result).failed());
    CHECK (parseShallowJson ("[\"'\"," + nested (40) + "]", result).failed());

    // What a few thousand levels did to the parser: it never gets them.
    CHECK (parseShallowJson (nested (200000), result).failed());
}

ARTFREQ_TEST (licenseHostileAnswersAndFilesCantCrashThePlugin)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    // An answer nested 200 000 deep (no real server sends one; something between it and the
    // plug-in that the computer trusts could): an error, not a crash on the request thread.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->replyWithBody (200, juce::String::repeatedString ("[", 200000) + juce::String::repeatedString ("]", 200000));

        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        const auto result = activateNow (instrument, keyArtFreq);
        CHECK (result.outcome == Outcome::serverError);
        CHECK (! instrument.isActivated());
    }

    // The same in the shared file, read on the message thread as the plug-in loads: no activation,
    // and the next activation replaces the file.
    {
        const TemporaryFolder temporary;
        const LicenseStore store (temporary.folder);
        CHECK (store.getFile().replaceWithText ("{\"activations\": " + juce::String::repeatedString ("[", 200000)
                                                + juce::String::repeatedString ("]", 200000) + "}"));
        CHECK (store.read().status == LicenseStore::Contents::Status::corrupt);

        const auto server = std::make_shared<FakeServer>();
        server->reply (activateArtFreqFresh);
        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        CHECK (! instrument.isActivated());
        CHECK (activateNow (instrument, keyArtFreq).outcome == Outcome::activated);
        CHECK (store.read().status == LicenseStore::Contents::Status::ok && store.read().activations.size() == 1);
    }

    // Counts no real answer has are shown as unknown (converting them would be undefined).
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->replyWithBody (200, juce::String::fromUTF8 (activateArtFreqFresh.body)
                                        .replace ("\"activationsUsed\":1", "\"activationsUsed\":1e300")
                                        .replace ("\"maxActivations\":2", "\"maxActivations\":-7"));

        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        const auto result = activateNow (instrument, keyArtFreq);
        CHECK (result.outcome == Outcome::activated);
        CHECK (result.activationsUsed == -1 && result.maxActivations == -1);
    }
}
