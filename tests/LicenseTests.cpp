// Licensing (src/license/): the key format against keys the server made, SHA-512 and the Ed25519
// check against Node's and WebCrypto's output, the signed licence's fields, the machine ID, the
// shared file (two products, other instances, damaged files), and the manager against a fake
// server answering exactly as the Worker does: the instrument's ArtFreq -> ArtFreq Pro retry,
// ArtFreq FX with an ArtFreq key, every error, timeouts, deactivation's rules, and its threads.
// The real transport is tried against a tiny HTTP server on 127.0.0.1; nothing here uses the
// internet.

#include "LicenseTestSupport.h"
#include "TestFramework.h"

#include "license/Crypto.h"
#include "license/LicenseConfig.h"
#include "license/LicenseKey.h"
#include "license/LicenseManager.h"
#include "license/LicenseStore.h"
#include "license/MachineId.h"
#include "license/SignedLicense.h"

#include <juce_events/juce_events.h>

#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace anymeans::license;
using namespace licensetest;

namespace
{

using Outcome = LicenseManager::Result::Outcome;

juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

juce::String hexOf (const void* data, size_t size)
{
    return juce::String::toHexString (data, static_cast<int> (size), 0);
}

std::vector<std::uint8_t> bytesOf (const juce::MemoryBlock& block)
{
    const auto* data = static_cast<const std::uint8_t*> (block.getData());
    return { data, data + block.getSize() };
}

std::vector<std::uint8_t> bytesOf (const juce::String& text)
{
    const auto utf = text.toUTF8();
    const auto* data = reinterpret_cast<const std::uint8_t*> (utf.getAddress());
    return { data, data + utf.sizeInBytes() - 1 };
}

std::vector<std::uint8_t> hexBytes (const char* hex)
{
    return bytesOf (*decodeHex (hex));
}

bool verifies (const std::vector<std::uint8_t>& signature, const std::vector<std::uint8_t>& publicKey,
               const std::vector<std::uint8_t>& message)
{
    return signature.size() == 64 && publicKey.size() == 32
        && verifyEd25519 (signature.data(), publicKey.data(), message.data(), message.size());
}

/** What a callback received. */
struct Recorded
{
    int calls = 0;
    LicenseManager::Result result;
    bool onMessageThread = false;
};

LicenseManager::Callback recordInto (Recorded& recorded)
{
    return [&recorded] (const LicenseManager::Result& result)
    {
        ++recorded.calls;
        recorded.result = result;
        recorded.onMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
    };
}

Recorded activateAndWait (LicenseManager& manager, const juce::String& typedKey)
{
    Recorded recorded;
    const auto started = manager.activate (typedKey, recordInto (recorded));
    CHECK (started);
    CHECK (pumpUntil ([&] { return recorded.calls > 0; }));
    return recorded;
}

Recorded deactivateAndWait (LicenseManager& manager)
{
    Recorded recorded;
    const auto started = manager.deactivate (recordInto (recorded));
    CHECK (started);
    CHECK (pumpUntil ([&] { return recorded.calls > 0; }));
    return recorded;
}

juce::String jsonField (const FakeServer::Sent& sent, const char* name)
{
    return sent.json.getProperty (name, {}).toString();
}

juce::String noConnectionText()
{
    return utf8 ("Couldn't reach the server \xe2\x80\x94" " check your connection and try again.");
}

juce::String serverErrorText (int code)
{
    return "Something went wrong on our side (code " + juce::String (code)
         + "). Try again in a minute, or write to support@anymeansaudio.com.";
}

/** What a refusal (403) ends with: a genuine key can be refused too. */
const juce::String supportLine (" If you think this is wrong, write to support@anymeansaudio.com.");

const juce::String notConfirmedText ("Activation couldn't be confirmed. Please try again later or write to support@anymeansaudio.com.");

/** A listener that counts calls and checks they come on the message thread. */
struct CountingListener final : public LicenseManager::Listener
{
    void licenseStateChanged (LicenseManager&) override
    {
        ++calls;
        allOnMessageThread = allOnMessageThread && juce::MessageManager::getInstance()->isThisTheMessageThread();
    }

    int calls = 0;
    bool allOnMessageThread = true;
};

} // namespace

//==============================================================================
// Key format
//==============================================================================

ARTFREQ_TEST (licenseKeyFormatMatchesTheServer)
{
    for (const auto& key : serverKeys)
    {
        const juce::String stored (key.stored), formatted (key.formatted);

        CHECK_MSG (LicenseKey::isWellFormed (stored), stored);
        CHECK (LicenseKey::format (stored) == formatted);
        CHECK (LicenseKey::checksumFor (stored.substring (0, 15)) == stored.substring (15));

        // How buyers paste keys: as emailed, lowercase, with spaces, without dashes, with a line break.
        CHECK (LicenseKey::normalise (formatted) == stored);
        CHECK (LicenseKey::normalise (formatted.toLowerCase()) == stored);
        CHECK (LicenseKey::normalise (" " + formatted.replaceCharacter ('-', ' ') + "\n") == stored);
        CHECK (LicenseKey::normalise ("\t" + stored.toLowerCase() + "  ") == stored);

        const auto masked = LicenseKey::mask (stored);
        CHECK (masked.startsWith (stored.substring (0, 5) + "-"));
        CHECK (masked.endsWith ("-" + stored.substring (15)));
        CHECK (masked.length() == 23);
        CHECK (! masked.contains (stored.substring (5, 15)));
    }

    for (const auto* typo : serverRejectedKeys)
        CHECK_MSG (! LicenseKey::isWellFormed (LicenseKey::normalise (typo)), typo);

    CHECK (LicenseKey::mask ("BHH7TTGTG8BG7RCKK4C2")
           == utf8 ("BHH7T-\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2-\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2-KK4C2"));

    // Letters a key never contains read as the digits they look like (Crockford): the checksum
    // still decides.
    CHECK (LicenseKey::normalise ("xr3oy-hztar-pjn27-idzp3") == "XR30YHZTARPJN271DZP3");
    CHECK (LicenseKey::normalise ("XR3OY HZTAR PJN27 LDZP3") == "XR30YHZTARPJN271DZP3");
    CHECK (LicenseKey::isWellFormed (LicenseKey::normalise ("xr3oy-hztar-pjn27-idzp3")));
    CHECK (! LicenseKey::isWellFormed (LicenseKey::normalise ("XR3OY-HZTAR-PJN27-UDZP3")));

    // The test key on the live server passes the offline check (nothing is sent to check it).
    CHECK (LicenseKey::isWellFormed (LicenseKey::normalise ("AF8JB-EA2A0-X5ZPJ-N6W6C")));

    CHECK (! LicenseKey::isWellFormed (""));
    CHECK (LicenseKey::normalise (" - \n") == "");
    CHECK (LicenseKey::format ("ABC") == "");
    CHECK (LicenseKey::mask ("ABC") == "");
}

//==============================================================================
// SHA-512, base64, Ed25519
//==============================================================================

ARTFREQ_TEST (licenseSha512MatchesKnownDigests)
{
    const auto digestOf = [] (const std::string& text)
    {
        const auto digest = Sha512::of (text.data(), text.size());
        return hexOf (digest.data(), digest.size());
    };

    // FIPS 180-2 examples.
    CHECK (digestOf ("abc") == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    CHECK (digestOf ("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu")
           == "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");

    // Node's, at every length around the block (128) and length-field (112) boundaries, whole
    // and in uneven pieces.
    for (const auto& vector : sha512PatternVectors)
    {
        std::vector<std::uint8_t> data (static_cast<size_t> (vector.length));

        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<std::uint8_t> ((i * 7 + 3) & 0xff);

        const auto whole = Sha512::of (data.data(), data.size());
        CHECK_MSG (hexOf (whole.data(), whole.size()) == vector.digest, "length " << vector.length);

        for (const size_t pieceSize : { size_t (1), size_t (7), size_t (127) })
        {
            Sha512 hash;

            for (size_t start = 0; start < data.size(); start += pieceSize)
                hash.update (data.data() + start, std::min (pieceSize, data.size() - start));

            const auto pieces = hash.finish();
            CHECK_MSG (pieces == whole, "length " << vector.length << " in pieces of " << pieceSize);
        }
    }

    Sha512 million;
    const std::string thousand (1000, 'a');

    for (int i = 0; i < 1000; ++i)
        million.update (thousand.data(), thousand.size());

    const auto digest = million.finish();
    CHECK (hexOf (digest.data(), digest.size()) == sha512MillionA);

    const auto empty = Sha512::of (nullptr, 0);
    CHECK (hexOf (empty.data(), empty.size()) == sha512PatternVectors[0].digest);
}

ARTFREQ_TEST (licenseBase64AndHexAreStrict)
{
    const auto decodesTo = [] (const char* text, const std::string& expected)
    {
        const auto decoded = decodeBase64Strict (text);
        return decoded.has_value() && decoded->toString().toStdString() == expected;
    };

    CHECK (decodesTo ("TWFu", "Man"));
    CHECK (decodesTo ("TWE=", "Ma"));
    CHECK (decodesTo ("TQ==", "M"));
    CHECK (decodesTo ("", ""));

    for (const auto* refused : { "TWE", "TQ=", "TQ===", "T Q==", "TQ==\n", " TQ==", "TR==", "TWF=", "T=Q=", "TQ-_", "TQ==TQ==", "====", "TW\xc3\xa9=" })
        CHECK_MSG (! decodeBase64Strict (juce::String (juce::CharPointer_UTF8 (refused))).has_value(), "\"" << refused << "\" must be refused");

    // Every signature from the server, and every 64-byte value in general, comes back exactly.
    juce::MemoryBlock all;

    for (int i = 0; i < 64; ++i)
        all.append (&i, 1);

    const auto decoded = decodeBase64Strict (juce::Base64::toBase64 (all.getData(), all.getSize()));
    CHECK (decoded.has_value() && *decoded == all);

    CHECK (decodeHex ("00ff10").has_value() && bytesOf (*decodeHex ("00ff10")) == std::vector<std::uint8_t> ({ 0x00, 0xff, 0x10 }));
    CHECK (decodeHex ("AbCd").has_value());
    CHECK (! decodeHex ("0").has_value());
    CHECK (! decodeHex ("zz").has_value());
    CHECK (! decodeHex ("00 ff").has_value());
}

ARTFREQ_TEST (licenseEd25519MatchesRfc8032)
{
    for (const auto& vector : rfc8032Vectors)
    {
        const auto publicKey = hexBytes (vector.publicKey);
        const auto message = hexBytes (vector.message);
        auto signature = hexBytes (vector.signature);

        CHECK (verifies (signature, publicKey, message));

        signature[10] = static_cast<std::uint8_t> (signature[10] ^ 0x01);
        CHECK (! verifies (signature, publicKey, message));
    }
}

ARTFREQ_TEST (licenseEd25519AcceptsWebCryptoSignaturesAndNothingElse)
{
    const auto publicKey = hexBytes (throwawayPublicKeyHex);
    const auto otherKey = hexBytes (rfc8032Vectors[0].publicKey);
    int failures = 0;

    for (const auto& vector : signedMessages)
    {
        const auto message = bytesOf (juce::String::fromUTF8 (vector.message));
        const auto signature = bytesOf (*decodeBase64Strict (vector.signature));

        CHECK_MSG (verifies (signature, publicKey, message), "\"" << vector.message << "\"");
        CHECK (SignedLicense::isSignedBy (juce::String::fromUTF8 (vector.message), vector.signature, throwawayPublicKeyHex));
        CHECK (! verifies (signature, otherKey, message));

        // Every one-byte change of the message, the signature or the key must fail.
        for (size_t i = 0; i < message.size(); ++i)
        {
            for (const std::uint8_t flip : { std::uint8_t (0x01), std::uint8_t (0x80), std::uint8_t (0xff) })
            {
                auto changed = message;
                changed[i] = static_cast<std::uint8_t> (changed[i] ^ flip);
                failures += verifies (signature, publicKey, changed) ? 1 : 0;
            }
        }

        auto longer = message;
        longer.push_back (0);
        failures += verifies (signature, publicKey, longer) ? 1 : 0;

        if (! message.empty())
        {
            auto shorter = message;
            shorter.pop_back();
            failures += verifies (signature, publicKey, shorter) ? 1 : 0;
        }

        for (size_t i = 0; i < signature.size(); ++i)
        {
            for (const std::uint8_t flip : { std::uint8_t (0x01), std::uint8_t (0x80) })
            {
                auto changed = signature;
                changed[i] = static_cast<std::uint8_t> (changed[i] ^ flip);
                failures += verifies (changed, publicKey, message) ? 1 : 0;
            }
        }

        for (size_t i = 0; i < publicKey.size(); ++i)
        {
            auto changed = publicKey;
            changed[i] = static_cast<std::uint8_t> (changed[i] ^ 0x01);
            failures += verifies (signature, changed, message) ? 1 : 0;
        }
    }

    CHECK_MSG (failures == 0, failures << " altered messages, signatures or keys verified");

    // The same equation with S + L in place of S: refused, as WebCrypto refuses it.
    const juce::String license = juce::String::fromUTF8 (signedMessages[0].message);
    CHECK (! SignedLicense::isSignedBy (license, nonCanonicalSignature, throwawayPublicKeyHex));

    // Signatures of the wrong length, and base64 that isn't canonical.
    const auto good = bytesOf (*decodeBase64Strict (signedMessages[0].signature));
    CHECK (! SignedLicense::isSignedBy (license, juce::Base64::toBase64 (good.data(), 63), throwawayPublicKeyHex));

    auto tooLong = good;
    tooLong.push_back (0);
    CHECK (! SignedLicense::isSignedBy (license, juce::Base64::toBase64 (tooLong.data(), tooLong.size()), throwawayPublicKeyHex));

    const juce::String signature (signedMessages[0].signature);
    CHECK (SignedLicense::isSignedBy (license, signature, throwawayPublicKeyHex));
    CHECK (! SignedLicense::isSignedBy (license, signature.trimCharactersAtEnd ("="), throwawayPublicKeyHex));
    CHECK (! SignedLicense::isSignedBy (license, signature + "\n", throwawayPublicKeyHex));
    CHECK (! SignedLicense::isSignedBy (license, signature.substring (0, 40) + " " + signature.substring (40), throwawayPublicKeyHex));
    CHECK (! SignedLicense::isSignedBy (license, signature.replaceCharacter ('+', '-').replaceCharacter ('/', '_'), throwawayPublicKeyHex));
    CHECK (! SignedLicense::isSignedBy (license, "", throwawayPublicKeyHex));
    CHECK (! SignedLicense::isSignedBy (license, "not base64 at all!", throwawayPublicKeyHex));

    // A public key that isn't 32 bytes of hex.
    CHECK (! SignedLicense::isSignedBy (license, signature, juce::String (throwawayPublicKeyHex).substring (2)));
    CHECK (! SignedLicense::isSignedBy (license, signature, juce::String (throwawayPublicKeyHex).replaceCharacter ('5', 'g')));
}

//==============================================================================
// The signed licence
//==============================================================================

ARTFREQ_TEST (licenseSignedLicenceFieldsAreChecked)
{
    const auto reply = juce::JSON::parse (juce::String::fromUTF8 (activateProFresh.body));
    const auto license = reply.getProperty ("license", {}).toString();
    const auto signature = reply.getProperty ("signature", {}).toString();

    const juce::StringArray instrument { PRODUCT_ID_ARTFREQ, PRODUCT_ID_ARTFREQ_PRO };
    const juce::StringArray effect { PRODUCT_ID_ARTFREQ_PRO };
    const juce::StringArray artFreqOnly { PRODUCT_ID_ARTFREQ };

    using Check = SignedLicense::Check;
    const auto check = [&] (const juce::String& lic, const juce::String& sig, const juce::StringArray& accepted,
                            const juce::String& key, const juce::String& machine, const juce::String& publicKey = throwawayPublicKeyHex)
    {
        return SignedLicense::check (lic, sig, publicKey, accepted, key, machine);
    };

    CHECK (check (license, signature, instrument, keyPro, machineHere) == Check::valid);
    CHECK (check (license, signature, effect, keyPro, machineHere) == Check::valid);
    CHECK (check (license, signature, artFreqOnly, keyPro, machineHere) == Check::wrongProduct);
    CHECK (check (license, signature, instrument, keyArtFreq, machineHere) == Check::wrongKey);
    CHECK (check (license, signature, instrument, LicenseKey::format (keyPro), machineHere) == Check::wrongKey);
    CHECK (check (license, signature, instrument, keyPro, machineOther) == Check::wrongMachine);
    CHECK (check (license, signature, instrument, keyPro, "") == Check::wrongMachine);
    CHECK (SignedLicense::productIdOf (license) == PRODUCT_ID_ARTFREQ_PRO);

    // A licence edited to say another product, or another licence's signature: not signed.
    CHECK (check (license.replace (PRODUCT_ID_ARTFREQ_PRO, PRODUCT_ID_ARTFREQ), signature, instrument, keyPro, machineHere) == Check::badSignature);
    CHECK (check (license, juce::JSON::parse (juce::String::fromUTF8 (activateArtFreqFresh.body)).getProperty ("signature", {}).toString(),
                  instrument, keyPro, machineHere) == Check::badSignature);

    // Licences made for these tests never pass with the key built into the plug-ins.
    CHECK (check (license, signature, instrument, keyPro, machineHere, SIGNING_PUBLIC_KEY_HEX) == Check::badSignature);

    // Signed, but not licences.
    CHECK (check (juce::String::fromUTF8 (signedMessages[3].message), signedMessages[3].signature, instrument, keyPro, machineHere) == Check::malformed);
    CHECK (check (juce::String::fromUTF8 (signedMessages[4].message), signedMessages[4].signature, instrument, keyPro, machineHere) == Check::malformed);

    // A Creme licence: signed, five fields, for a product neither ArtFreq plug-in accepts.
    CHECK (check (juce::String::fromUTF8 (signedMessages[2].message), signedMessages[2].signature, instrument, keyCreme,
                  "e765c22a8a0de890ac1da07c8504c666671cdfbe5a0050cfb9eb44981af7b396") == Check::wrongProduct);

    CHECK (SignedLicense::split ("a||b").size() == 3);
    CHECK (SignedLicense::split ("").size() == 1);
    CHECK (SignedLicense::describe (Check::wrongMachine).isNotEmpty());
}

ARTFREQ_TEST (licenseARealLicenceFromTheLiveServerVerifies)
{
    // An activation answered by the live server (Nov 18 2026: a test key of the store, a made-up
    // machine ID, activated and then deactivated again), checked with the public key built into
    // the plug-ins. The vectors above are signed by a throwaway key; this one proves that the
    // deployed server's signing key is the one the plug-ins trust, and that the plug-in reads its
    // licences as it writes them.
    const juce::String license ("AMLIC1|prod_Ve87k69rq5fkZN|AF8JBEA2A0X5ZPJN6W6C|"
                                "0f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f0|2026-11-18T05:33:13.583Z");
    const juce::String signature ("+5G/UdjkegcaAJHZEvY9GphwkQHAF9qYtLyvlbGhTngwPrjfL6MYnSrTBrJv18eP+uItP19YJhXao2M3QxcrAA==");
    const juce::String key ("AF8JBEA2A0X5ZPJN6W6C");
    const juce::String machine ("0f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f0");   // field 4

    using Check = SignedLicense::Check;
    const juce::StringArray instrument { PRODUCT_ID_ARTFREQ, PRODUCT_ID_ARTFREQ_PRO };
    const juce::StringArray effect { PRODUCT_ID_ARTFREQ_PRO };

    CHECK (juce::String (SIGNING_PUBLIC_KEY_HEX) == "7acdbd078095b8d36a9c91dfaa2166c820fbb382bc71d8659bfd1153231e2307");
    CHECK (LicenseKey::isWellFormed (key));
    CHECK (SignedLicense::isSignedBy (license, signature, SIGNING_PUBLIC_KEY_HEX));
    CHECK (SignedLicense::check (license, signature, SIGNING_PUBLIC_KEY_HEX, instrument, key, machine) == Check::valid);
    CHECK (SignedLicense::check (license, signature, SIGNING_PUBLIC_KEY_HEX, effect, key, machine) == Check::valid);
    CHECK (SignedLicense::productIdOf (license) == PRODUCT_ID_ARTFREQ_PRO);

    // For this licence only: another computer, another key, a product the plug-in doesn't take.
    CHECK (SignedLicense::check (license, signature, SIGNING_PUBLIC_KEY_HEX, effect, key, machineHere) == Check::wrongMachine);
    CHECK (SignedLicense::check (license, signature, SIGNING_PUBLIC_KEY_HEX, effect, keyPro, machine) == Check::wrongKey);
    CHECK (SignedLicense::check (license, signature, SIGNING_PUBLIC_KEY_HEX, { PRODUCT_ID_CREME }, key, machine) == Check::wrongProduct);

    // Not with the tests' key, and not changed by one character (the time, or the signature).
    CHECK (SignedLicense::check (license, signature, throwawayPublicKeyHex, effect, key, machine) == Check::badSignature);
    CHECK (SignedLicense::check (license.replace ("05:33:13.583Z", "05:33:13.584Z"), signature, SIGNING_PUBLIC_KEY_HEX, effect, key, machine)
           == Check::badSignature);
    CHECK (SignedLicense::check (license, signature.replace ("+5G/", "+5H/"), SIGNING_PUBLIC_KEY_HEX, effect, key, machine)
           == Check::badSignature);

    // As the server's whole answer, through the activation itself: a manager on that computer,
    // with the built-in key, activates with it and keeps it.
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    server->replyWithBody (200, "{\"ok\":true,\"activationToken\":\"60b0e1b87e670ec8632de2e24f3699012bffdf6c700e2a6dcf917a5e6ed229d3\","
                                "\"license\":\"" + license + "\",\"signature\":\"" + signature + "\","
                                "\"activationsUsed\":1,\"maxActivations\":2,\"alreadyActive\":false}");

    auto environment = makeEnvironment (temporary.folder, server, machine);
    environment.publicKeyHex = {};   // the built-in key
    LicenseManager manager (artFreqEffectLicensing(), environment);

    std::optional<LicenseManager::Result> result;
    CHECK (manager.activate ("af8jb-ea2a0-x5zpj-n6w6c", [&] (const LicenseManager::Result& r) { result = r; }));
    CHECK (pumpUntil ([&] { return result.has_value(); }));
    CHECK (result.has_value() && result->outcome == LicenseManager::Result::Outcome::activated);
    CHECK (manager.isActivated());
    CHECK (manager.getActivation().has_value() && manager.getActivation()->productName == "ArtFreq Pro");

    // Read back from the file by a new manager, as at the next start: still activated, offline.
    const LicenseManager later (artFreqInstrumentLicensing(), environment);
    CHECK (later.isActivated());
    CHECK (server->numSent() == 1);
}

//==============================================================================
// Machine ID
//==============================================================================

ARTFREQ_TEST (licenseMachineIdIsStableHashedAndNeverRaw)
{
    CHECK (MachineId::hash ("test machine here") == machineHere);  // SHA-256 of "AnyMeans|test machine here", by Node

    const TemporaryFolder temporary;
    const auto first = MachineId::get (temporary.folder);
    const auto second = MachineId::get (temporary.folder);

    CHECK (first == second);
    CHECK_MSG (MachineId::looksValid (first), first);

    const auto identifiers = juce::SystemStats::getMachineIdentifiers (juce::SystemStats::MachineIdFlags::uniqueId);
    const auto raw = identifiers.isEmpty() ? juce::String() : identifiers[0].trim();

    if (raw.isNotEmpty())
    {
        // The hardware ID, hashed; never the ID itself, and no file needed for it.
        CHECK (first == MachineId::hash (raw));
        CHECK (! first.containsIgnoreCase (raw));
        CHECK (! temporary.folder.getChildFile ("MachineId.txt").exists());
    }
    else
    {
        std::printf ("    (this computer reports no hardware ID: the random fallback was used)\n");
    }

    // The choice: the first ID the system lists, then the device ID, then the random fallback.
    CHECK (MachineId::chooseIdentifier ({ "", " abc " }, "device", temporary.folder) == "abc");
    CHECK (MachineId::chooseIdentifier ({}, " device ", temporary.folder) == "device");

    const TemporaryFolder bare;
    const auto fallback = MachineId::chooseIdentifier ({}, {}, bare.folder);
    CHECK (fallback.startsWith ("random:") && fallback.length() == 7 + 32);
    CHECK (MachineId::chooseIdentifier ({}, {}, bare.folder) == fallback);
    CHECK (bare.folder.getChildFile ("MachineId.txt").existsAsFile());

    const TemporaryFolder another;
    CHECK (MachineId::chooseIdentifier ({}, {}, another.folder) != fallback);
}

//==============================================================================
// The shared file
//==============================================================================

ARTFREQ_TEST (licenseStoreWritesAndReadsTwoProductsSideBySide)
{
    const TemporaryFolder temporary;
    const LicenseStore store (temporary.folder);

    CHECK (store.read().status == LicenseStore::Contents::Status::missing);
    CHECK (! store.getStamp().exists);

    const auto pro = storedActivationFrom (activateProFresh, keyPro, "ArtFreq FX");
    CHECK (store.put (pro));

    auto contents = store.read();
    CHECK (contents.status == LicenseStore::Contents::Status::ok);
    CHECK (contents.activations.size() == 1);

    if (contents.activations.size() == 1)
    {
        const auto& read = contents.activations.front();
        CHECK (read.productId == pro.productId && read.key == pro.key && read.machineId == pro.machineId);
        CHECK (read.token == pro.token && read.license == pro.license && read.signature == pro.signature);
        CHECK (read.activatedAt == pro.activatedAt && read.activatedIn == "ArtFreq FX");
    }

    const auto text = store.getFile().loadFileAsString();
    CHECK (text.contains ("\"ArtFreq Pro\""));  // the product's name, for whoever opens the file
    CHECK (text.contains ("\"format\""));

    // A second product next to it, then the first one again (replaced, not added).
    const auto stamp = store.getStamp();
    CHECK (store.put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));
    CHECK (store.getStamp() != stamp);
    CHECK (store.read().activations.size() == 2);

    auto newerPro = pro;
    newerPro.token = "a newer token";
    const auto stampBefore = store.getStamp();
    CHECK (store.put (newerPro));
    CHECK (store.getStamp() != stampBefore);  // even when size and time could look the same

    contents = LicenseStore (temporary.folder).read();  // another instance's view
    CHECK (contents.activations.size() == 2);

    int proEntries = 0;

    for (const auto& activation : contents.activations)
        if (activation.productId == PRODUCT_ID_ARTFREQ_PRO)
            proEntries += activation.token == "a newer token" ? 1 : 100;

    CHECK (proEntries == 1);

    // Another computer's activation of the same product is kept, and removing ours leaves it.
    auto elsewhere = pro;
    elsewhere.machineId = machineOther;
    CHECK (store.put (elsewhere));
    CHECK (store.read().activations.size() == 3);

    // A second key for the same product on this computer keeps its own entry: its place on the
    // server can still be freed from here.
    auto secondKey = pro;
    secondKey.key = keyFull;
    CHECK (store.put (secondKey));
    CHECK (store.read().activations.size() == 4);
    CHECK (store.remove (PRODUCT_ID_ARTFREQ_PRO, keyFull, machineHere));
    CHECK (store.read().activations.size() == 3);

    CHECK (store.remove (PRODUCT_ID_ARTFREQ_PRO, keyPro, machineHere));
    contents = store.read();
    CHECK (contents.activations.size() == 2);

    for (const auto& activation : contents.activations)
        CHECK (! (activation.productId == PRODUCT_ID_ARTFREQ_PRO && activation.machineId == machineHere));

    CHECK (store.remove (PRODUCT_ID_ARTFREQ_PRO, keyPro, machineHere));  // nothing to remove: fine

    // Nothing but the file in the folder: the temporary files are gone.
    CHECK (temporary.folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles) == 1);
    CHECK (temporary.folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories, "*temp*") == 0);
}

ARTFREQ_TEST (licenseStoreToleratesDamagedFiles)
{
    const TemporaryFolder temporary;
    const LicenseStore store (temporary.folder);
    const auto file = store.getFile();

    CHECK (store.put (storedActivationFrom (activateProFresh, keyPro)));
    const auto good = file.loadFileAsString();

    if (! good.contains ("activations"))
        return;  // already reported: nothing below can work

    // Half-written at any point, empty, or not JSON: no activations, no crash.
    for (const auto cut : { 0, 1, good.length() / 3, good.length() / 2, good.length() - 3 })
    {
        CHECK (file.replaceWithText (good.substring (0, cut)));
        const auto contents = store.read();
        CHECK_MSG (contents.status == LicenseStore::Contents::Status::corrupt && contents.activations.empty(), "cut at " << cut);
    }

    for (const auto* text : { "null", "[]", "\"text\"", "{ \"activations\": " })
    {
        CHECK (file.replaceWithText (text));
        CHECK_MSG (store.read().activations.empty(), text);
    }

    const unsigned char binary[] = { 0xff, 0xfe, 0x00, 0x7b, 0x80, 0xc3 };
    CHECK (file.replaceWithData (binary, sizeof (binary)));
    CHECK (store.read().status == LicenseStore::Contents::Status::corrupt);

    juce::MemoryBlock huge (2 * 1024 * 1024, true);
    CHECK (file.replaceWithData (huge.getData(), huge.getSize()));
    CHECK (store.read().status == LicenseStore::Contents::Status::corrupt);

    // Entries it can't use are skipped, the others still count.
    const auto validEntry = juce::JSON::parse (good).getProperty ("activations", {})[0];
    juce::Array<juce::var> mixed;
    mixed.add (42);
    mixed.add ("text");
    mixed.add (juce::JSON::parse ("{ \"productId\": \"prod_Ve87k69rq5fkZN\", \"key\": \"Y1F2FJVXBXWCNVBVCZKJ\" }"));
    mixed.add (validEntry);

    auto* root = new juce::DynamicObject();
    const juce::var rootVar (root);
    root->setProperty ("format", 1);
    root->setProperty ("written by", "a later version");
    root->setProperty ("activations", mixed);
    CHECK (file.replaceWithText (juce::JSON::toString (rootVar)));

    auto contents = store.read();
    CHECK (contents.status == LicenseStore::Contents::Status::ok);
    CHECK (contents.activations.size() == 1);

    // Writing keeps what it doesn't understand.
    CHECK (store.put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));
    const auto rewritten = file.loadFileAsString();
    CHECK (rewritten.contains ("written by"));
    CHECK (rewritten.contains ("Y1F2FJVXBXWCNVBVCZKJ"));
    CHECK (store.read().activations.size() == 2);

    // Writing over a corrupt file: it is put aside for support, and a fresh file written.
    CHECK (file.replaceWithText ("{ \"activations\": [ {"));
    CHECK (store.put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));
    contents = store.read();
    CHECK (contents.status == LicenseStore::Contents::Status::ok && contents.activations.size() == 1);
    CHECK (file.getSiblingFile ("Licenses.unreadable.json").loadFileAsString() == "{ \"activations\": [ {");
}

//==============================================================================
// The manager: what is on this computer
//==============================================================================

ARTFREQ_TEST (licenseManagerReadsTheSharedFileOffline)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();

    {
        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        CHECK (! instrument.isActivated());
        CHECK (! instrument.getActivation().has_value());
        CHECK (instrument.getStatusText() == "Not activated. Enter the license key from your purchase email.");
        CHECK (instrument.getDeactivationWarning().isEmpty());
    }

    // ArtFreq Pro, activated in either plug-in: both are activated, from the file alone.
    CHECK (LicenseStore (temporary.folder).put (storedActivationFrom (activateProFresh, keyPro, "ArtFreq FX")));

    {
        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));

        CHECK (instrument.isActivated() && effect.isActivated());
        CHECK (instrument.getActivatedFlag().load() && &instrument.getActivatedFlag() == &instrument.getActivatedFlag());

        if (const auto& activation = instrument.getActivation())
        {
            CHECK (activation->productId == PRODUCT_ID_ARTFREQ_PRO);
            CHECK (activation->productName == "ArtFreq Pro");
            CHECK (activation->pluginsUnlocked == "ArtFreq and ArtFreq FX");
            CHECK (activation->maskedKey == LicenseKey::mask (keyPro));
            CHECK (activation->key == keyPro);
            CHECK (activation->activatedIn == "ArtFreq FX");
        }

        CHECK (instrument.getStatusText() == "Activated with your ArtFreq Pro key " + LicenseKey::mask (keyPro) + ".");
        CHECK (instrument.getDeactivationWarning() == "Deactivating frees your key for another computer. ArtFreq and ArtFreq FX will both "
                                                      "be locked on this computer until you activate again.");
    }

    // An ArtFreq key: the instrument only.
    const TemporaryFolder artFreqOnly;
    CHECK (LicenseStore (artFreqOnly.folder).put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));

    {
        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (artFreqOnly.folder, server));
        LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (artFreqOnly.folder, server));

        CHECK (instrument.isActivated());
        CHECK (! effect.isActivated());
        CHECK (instrument.getDeactivationWarning() == "Deactivating frees your key for another computer. ArtFreq will be locked "
                                                      "on this computer until you activate again.");
    }

    CHECK (server->numSent() == 0);  // starting up never asks the server
}

ARTFREQ_TEST (licenseManagerIgnoresForgedOrCopiedEntries)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const auto server = std::make_shared<FakeServer>();
    const auto genuine = storedActivationFrom (activateArtFreqFresh, keyArtFreq);

    const auto activatedWith = [&] (const StoredActivation& entry, const juce::String& publicKey = throwawayPublicKeyHex)
    {
        const TemporaryFolder temporary;
        CHECK (LicenseStore (temporary.folder).put (entry));

        auto environment = makeEnvironment (temporary.folder, server);
        environment.publicKeyHex = publicKey;
        const LicenseManager instrument (artFreqInstrumentLicensing(), environment);
        return instrument.isActivated();
    };

    CHECK (activatedWith (genuine));

    auto tampered = genuine;
    tampered.signature = tampered.signature.replaceSection (10, 1, tampered.signature[10] == 'A' ? "B" : "A");
    CHECK (! activatedWith (tampered));

    // The licence edited to another product (the signature then fails).
    auto edited = genuine;
    edited.license = edited.license.replace (PRODUCT_ID_ARTFREQ, PRODUCT_ID_ARTFREQ_PRO);
    edited.productId = PRODUCT_ID_ARTFREQ_PRO;
    CHECK (! activatedWith (edited));

    // The entry's own fields changed around a genuine licence.
    auto otherKey = genuine;
    otherKey.key = keyPro;
    CHECK (! activatedWith (otherKey));

    auto otherProduct = genuine;
    otherProduct.productId = PRODUCT_ID_ARTFREQ_PRO;
    CHECK (! activatedWith (otherProduct));

    // A genuine licence from another computer, filed under this one's ID.
    StoredActivation copied;
    copied.productId = PRODUCT_ID_ARTFREQ;
    copied.key = keyArtFreq;
    copied.machineId = machineHere;
    copied.license = juce::String::fromUTF8 (signedMessages[1].message);  // ArtFreq, keyArtFreq, machineOther
    copied.signature = signedMessages[1].signature;
    CHECK (! activatedWith (copied));

    // A hand-made entry without a signature.
    auto withoutSignature = genuine;
    withoutSignature.signature = {};
    CHECK (! activatedWith (withoutSignature));

    // Signed with the test key: never valid with the key built into the plug-ins.
    CHECK (! activatedWith (genuine, {}));

    // The file arriving on another computer (a migrated user folder): kept, not used, and said so.
    {
        const TemporaryFolder temporary;
        CHECK (LicenseStore (temporary.folder).put (genuine));
        const LicenseManager elsewhere (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server, machineOther));
        CHECK (! elsewhere.isActivated());
        CHECK (elsewhere.getStatusText() == "Not activated on this computer yet. Enter your license key to activate it.");
    }
}

//==============================================================================
// The manager: activating
//==============================================================================

ARTFREQ_TEST (licenseInstrumentActivatesWithAnArtFreqKey)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    server->reply (activateArtFreqFresh);

    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
    CountingListener listener;
    instrument.addListener (&listener);

    Recorded recorded;
    CHECK (instrument.activate ("  5vbnq-505rh-w9p0a-280qw \n", recordInto (recorded)));
    CHECK (instrument.isBusy());
    CHECK (instrument.getRequestInProgress() == LicenseManager::Request::activating);
    CHECK (instrument.getStatusText() == utf8 ("Checking your key\xe2\x80\xa6"));
    CHECK (recorded.calls == 0);  // never from inside activate()

    CHECK (pumpUntil ([&] { return recorded.calls > 0; }));
    CHECK (recorded.calls == 1);
    CHECK (recorded.onMessageThread);
    CHECK (recorded.result.outcome == Outcome::activated);
    CHECK (recorded.result.succeeded());
    CHECK (recorded.result.message == "Activated. This computer is now unlocked for ArtFreq.");
    CHECK (recorded.result.productId == PRODUCT_ID_ARTFREQ);
    CHECK (recorded.result.statusCode == 200);
    CHECK (recorded.result.activationsUsed == 1 && recorded.result.maxActivations == 2 && ! recorded.result.alreadyActive);

    CHECK (instrument.isActivated());
    CHECK (! instrument.isBusy());
    CHECK (listener.calls >= 2 && listener.allOnMessageThread);  // busy, then activated

    // What was sent: the key normalised and dashed, the hashed machine ID, ArtFreq's product.
    const auto sent = server->sent();
    CHECK (sent.size() == 1);

    if (sent.size() == 1)
    {
        CHECK (sent[0].path == "/api/activate");
        CHECK (jsonField (sent[0], "key") == "5VBNQ-505RH-W9P0A-280QW");
        CHECK (jsonField (sent[0], "machineId") == machineHere);
        CHECK (jsonField (sent[0], "productId") == PRODUCT_ID_ARTFREQ);
        CHECK (sent[0].json.getDynamicObject() != nullptr && sent[0].json.getDynamicObject()->getProperties().size() == 3);
        CHECK (sent[0].timeoutMs == 2000);
        CHECK (! sent[0].onMessageThread);
    }

    // Stored exactly as the server sent it.
    const auto reply = juce::JSON::parse (juce::String::fromUTF8 (activateArtFreqFresh.body));
    const auto contents = LicenseStore (temporary.folder).read();
    CHECK (contents.activations.size() == 1);

    if (contents.activations.size() == 1)
    {
        const auto& stored = contents.activations.front();
        CHECK (stored.productId == PRODUCT_ID_ARTFREQ);
        CHECK (stored.key == keyArtFreq);
        CHECK (stored.machineId == machineHere);
        CHECK (stored.token == reply.getProperty ("activationToken", {}).toString());
        CHECK (stored.license == reply.getProperty ("license", {}).toString());
        CHECK (stored.signature == reply.getProperty ("signature", {}).toString());
        CHECK (stored.activatedIn == "ArtFreq");
        CHECK (juce::Time::fromISO8601 (stored.activatedAt).toMilliseconds() > 0);
    }

    instrument.removeListener (&listener);

    // Activating again on the same computer is free (the server says so) and leaves one entry.
    server->reply (activateArtFreqAgain);
    const auto again = activateAndWait (instrument, keyArtFreq);
    CHECK (again.result.outcome == Outcome::activated);
    CHECK (again.result.alreadyActive);
    CHECK (LicenseStore (temporary.folder).read().activations.size() == 1);
    CHECK (instrument.isActivated());
}

ARTFREQ_TEST (licenseInstrumentRetriesAProKeyAsArtFreqPro)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    server->reply (activateProAsArtFreq);  // 403 wrong_product
    server->reply (activateProFresh);

    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
    LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));
    CountingListener effectListener;
    effect.addListener (&effectListener);
    CHECK (! effect.isActivated());

    const auto recorded = activateAndWait (instrument, LicenseKey::format (keyPro));
    CHECK (recorded.result.outcome == Outcome::activated);
    CHECK (recorded.result.productId == PRODUCT_ID_ARTFREQ_PRO);
    CHECK (recorded.result.message == "Activated. This computer is now unlocked for ArtFreq and ArtFreq FX.");

    const auto sent = server->sent();
    CHECK (sent.size() == 2);

    if (sent.size() == 2)
    {
        CHECK (jsonField (sent[0], "productId") == PRODUCT_ID_ARTFREQ);
        CHECK (jsonField (sent[1], "productId") == PRODUCT_ID_ARTFREQ_PRO);
        CHECK (jsonField (sent[1], "key") == LicenseKey::format (keyPro));
    }

    CHECK (instrument.getActivation().has_value() && instrument.getActivation()->productName == "ArtFreq Pro");

    // ArtFreq FX, open at the same time, sees it without a restart.
    CHECK (pumpUntil ([&] { return effect.isActivated(); }));
    CHECK (effectListener.calls >= 1 && effectListener.allOnMessageThread);
    effect.removeListener (&effectListener);
}

ARTFREQ_TEST (licenseInstrumentRefusesAKeyForAnotherPlugin)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    server->reply (activateCremeAsArtFreq);
    server->reply (activateCremeAsPro);

    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
    const auto recorded = activateAndWait (instrument, keyCreme);

    CHECK (recorded.result.outcome == Outcome::refused);
    CHECK (recorded.result.message == "That key is for a different AnyMeans Audio plug-in. If you think this is wrong, write to support@anymeansaudio.com.");
    CHECK (recorded.result.serverCode == "wrong_product" && recorded.result.statusCode == 403);
    CHECK (server->numSent() == 2);
    CHECK (! instrument.isActivated());
    CHECK (! LicenseStore (temporary.folder).getFile().exists());
}

ARTFREQ_TEST (licenseEffectTakesOnlyArtFreqProKeys)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    // A Pro key: one request, with ArtFreq Pro's product; the instrument is unlocked too.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (activateProFresh);

        LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));
        const auto recorded = activateAndWait (effect, keyPro);
        CHECK (recorded.result.outcome == Outcome::activated);
        CHECK (server->numSent() == 1 && jsonField (server->sent()[0], "productId") == PRODUCT_ID_ARTFREQ_PRO);
        CHECK (effect.isActivated());
        CHECK (LicenseManager (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server)).isActivated());
    }

    // An ArtFreq key: the server says wrong product, and the effect says what that means.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (activateArtFreqAsPro);

        LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));
        const auto recorded = activateAndWait (effect, keyArtFreq);
        CHECK (recorded.result.outcome == Outcome::refused);
        CHECK (recorded.result.message == artFreqEffectLicensing().wrongProductMessage + supportLine);
        CHECK (server->numSent() == 1);
        CHECK (! effect.isActivated());
    }

    // ... and when this computer already has ArtFreq activated with that very key, it says so plainly.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (activateArtFreqAsPro);
        CHECK (LicenseStore (temporary.folder).put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));

        LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));
        const auto recorded = activateAndWait (effect, LicenseKey::format (keyArtFreq).toLowerCase());
        CHECK (recorded.result.outcome == Outcome::refused);
        CHECK (recorded.result.message == "That key is for ArtFreq (without FX). ArtFreq FX comes with ArtFreq Pro." + supportLine);
        CHECK (! effect.isActivated());
        CHECK (LicenseManager (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server)).isActivated());
    }
}

ARTFREQ_TEST (licenseActivationReportsEveryServerAnswer)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    struct Case
    {
        Reply reply;
        Outcome outcome;
        juce::String message;
    };

    const Case cases[] =
    {
        // The server's own text; it already names the support address, so nothing is added.
        { activateLimit, Outcome::refused, "This key is already active on 2 machines. Deactivate it there first (or write to support@anymeansaudio.com)." },
        { { 409, "{\"ok\":false,\"code\":\"activation_limit\",\"error\":\"This key is already active on another machine.\"}" },
          Outcome::refused, "This key is already active on another machine. Deactivate it on your old computer, or write to support@anymeansaudio.com." },
        // A refusal adds the support address: a genuine key can be refused too.
        { activateUnknownKey, Outcome::refused, utf8 ("That license key isn't valid \xe2\x80\x94" " check for typos and try again.") + supportLine },
        { activateInvalidKey, Outcome::refused, utf8 ("That license key isn't valid \xe2\x80\x94" " check for typos and try again.") + supportLine },
        { activateMissingMachine, Outcome::refused, "A machine id is required to activate." + supportLine },
        { activateNotJson, Outcome::serverError, serverErrorText (400) },
        { activateNoMachine, Outcome::serverError, serverErrorText (400) },
        { notFound, Outcome::serverError, serverErrorText (404) },
        { activateWrongMethod, Outcome::serverError, serverErrorText (405) },
        { rateLimited, Outcome::serverError, serverErrorText (429) },
        { cloudflare500, Outcome::serverError, serverErrorText (500) },
        { cloudflare502, Outcome::serverError, serverErrorText (502) },
        { proxyForbidden, Outcome::serverError, serverErrorText (403) },
        { truncatedOk, Outcome::serverError, serverErrorText (200) },
        { okFalse, Outcome::serverError, serverErrorText (200) },
        { activateUnsigned, Outcome::notConfirmed, notConfirmedText },
    };

    for (const auto& testCase : cases)
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (testCase.reply);

        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        const auto recorded = activateAndWait (instrument, keyArtFreq);

        CHECK_MSG (recorded.result.outcome == testCase.outcome, testCase.reply.status << " " << testCase.reply.body);
        CHECK_MSG (recorded.result.message == testCase.message, "got: " << recorded.result.message);
        CHECK (recorded.result.statusCode == testCase.reply.status);
        CHECK (! recorded.result.succeeded());
        CHECK (server->numSent() == 1);

        // Nothing is stored unless the server said ok with a licence that checks out.
        CHECK (! instrument.isActivated());
        CHECK (! LicenseStore (temporary.folder).getFile().exists());
    }

    // No connection at all.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->noConnection();

        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
        const auto recorded = activateAndWait (instrument, keyArtFreq);
        CHECK (recorded.result.outcome == Outcome::noConnection);
        CHECK (recorded.result.message == noConnectionText());
        CHECK (! instrument.isActivated());

        // Retrying is always possible.
        server->reply (activateArtFreqFresh);
        CHECK (activateAndWait (instrument, keyArtFreq).result.outcome == Outcome::activated);
    }
}

ARTFREQ_TEST (licenseActivationNeedsALicenceThatChecksOut)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    const auto attempt = [] (const PluginLicensing& plugin, const juce::String& replyBody, const juce::String& typedKey,
                             const juce::String& machine, const juce::String& publicKey)
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->replyWithBody (200, replyBody);

        auto environment = makeEnvironment (temporary.folder, server, machine);
        environment.publicKeyHex = publicKey;
        LicenseManager manager (plugin, environment);

        const auto recorded = activateAndWait (manager, typedKey);
        CHECK (! manager.isActivated());
        CHECK (! LicenseStore (temporary.folder).getFile().exists());
        return recorded.result.outcome == Outcome::notConfirmed && recorded.result.message == notConfirmedText;
    };

    const auto artFreqBody = juce::String::fromUTF8 (activateArtFreqFresh.body);
    const juce::String throwaway (throwawayPublicKeyHex);

    // For another computer, another key, a product this plug-in doesn't take, a broken signature,
    // or not signed by the key built into the plug-in.
    CHECK (attempt (artFreqInstrumentLicensing(), artFreqBody, keyArtFreq, machineOther, throwaway));
    CHECK (attempt (artFreqInstrumentLicensing(), juce::String::fromUTF8 (activateProFresh.body), keyArtFreq, machineHere, throwaway));
    CHECK (attempt (artFreqEffectLicensing(), artFreqBody, keyArtFreq, machineHere, throwaway));
    CHECK (attempt (artFreqInstrumentLicensing(), artFreqBody.replace ("rMGQyNV3", "rMGQyNV4"), keyArtFreq, machineHere, throwaway));
    CHECK (attempt (artFreqInstrumentLicensing(), artFreqBody, keyArtFreq, machineHere, SIGNING_PUBLIC_KEY_HEX));

    // A licence, but no signature (or the other way round).
    CHECK (attempt (artFreqInstrumentLicensing(), artFreqBody.replace ("\"signature\"", "\"signatur\""), keyArtFreq, machineHere, throwaway));
    CHECK (attempt (artFreqInstrumentLicensing(), artFreqBody.replace ("\"license\"", "\"licence\""), keyArtFreq, machineHere, throwaway));
}

ARTFREQ_TEST (licenseActivationCatchesTyposWithoutAskingTheServer)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));

    Recorded typo;
    CHECK (instrument.activate ("5VBNQ-505RH-W9P0A-280QX", recordInto (typo)));
    CHECK (typo.calls == 0);
    CHECK (pumpUntil ([&] { return typo.calls > 0; }));
    CHECK (typo.onMessageThread);
    CHECK (typo.result.outcome == Outcome::invalidKey);
    CHECK (typo.result.message == utf8 ("That key doesn't look right \xe2\x80\x94" " check for typos."));

    CHECK (activateAndWait (instrument, "5VBNQ-505RH-W9P0A-280Q").result.outcome == Outcome::invalidKey);

    const auto empty = activateAndWait (instrument, "  -  ");
    CHECK (empty.result.outcome == Outcome::invalidKey);
    CHECK (empty.result.message == "Enter the license key from your purchase email.");

    CHECK (server->numSent() == 0);
    CHECK (! instrument.isBusy());
}

ARTFREQ_TEST (licenseActivationGivesUpOnASilentServer)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    server->hang();

    auto environment = makeEnvironment (temporary.folder, server);
    environment.requestTimeoutMs = 150;
    LicenseManager instrument (artFreqInstrumentLicensing(), environment);

    const auto started = juce::Time::getMillisecondCounter();
    const auto recorded = activateAndWait (instrument, keyArtFreq);
    const auto elapsed = juce::Time::getMillisecondCounter() - started;

    CHECK (recorded.result.outcome == Outcome::noConnection);
    CHECK (recorded.result.message == noConnectionText());
    CHECK (server->cancelsSeen == 1);
    CHECK_LESS (elapsed, 2000);
    CHECK (! instrument.isBusy() && ! instrument.isActivated());

    server->reply (activateArtFreqFresh);
    CHECK (activateAndWait (instrument, keyArtFreq).result.outcome == Outcome::activated);
}

//==============================================================================
// The manager: deactivating
//==============================================================================

ARTFREQ_TEST (licenseDeactivationRemovesOnlyWhatTheServerFreed)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    struct Case
    {
        std::function<void (FakeServer&)> script;
        Outcome outcome;
        bool removed;
        juce::String message;
    };

    const juce::String freed ("Deactivated. Your key can now be activated on another computer. ArtFreq is now locked on this computer.");
    const juce::String unknown ("Deactivated here (the server no longer counted this computer for that key). ArtFreq is now locked on this computer.");
    const juce::String kept (" This computer stays activated.");

    const Case cases[] =
    {
        { [] (FakeServer& s) { s.reply (deactivateArtFreq); },        Outcome::deactivated, true, freed },
        { [] (FakeServer& s) { s.reply (deactivateArtFreqAgain); },   Outcome::deactivated, true, unknown },   // not_active_here
        { [] (FakeServer& s) { s.reply (deactivateUnknownKey); },     Outcome::deactivated, true, unknown },
        { [] (FakeServer& s) { s.reply (deactivateInvalidKey); },     Outcome::refused, false,
          utf8 ("That license key isn't valid \xe2\x80\x94" " check for typos and try again.") + supportLine + kept },
        { [] (FakeServer& s) { s.reply (deactivateNotJson); },        Outcome::serverError, false, serverErrorText (400) + kept },
        { [] (FakeServer& s) { s.reply (cloudflare502); },            Outcome::serverError, false, serverErrorText (502) + kept },
        { [] (FakeServer& s) { s.reply (rateLimited); },              Outcome::serverError, false, serverErrorText (429) + kept },
        { [] (FakeServer& s) { s.reply (proxyForbidden); },           Outcome::serverError, false, serverErrorText (403) + kept },
        { [] (FakeServer& s) { s.noConnection(); },                   Outcome::noConnection, false, noConnectionText() + kept },
        { [] (FakeServer& s) { s.hang(); },                           Outcome::noConnection, false, noConnectionText() + kept },
    };

    for (const auto& testCase : cases)
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        testCase.script (*server);
        CHECK (LicenseStore (temporary.folder).put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));

        auto environment = makeEnvironment (temporary.folder, server);
        environment.requestTimeoutMs = 150;
        LicenseManager instrument (artFreqInstrumentLicensing(), environment);
        CHECK (instrument.isActivated());

        Recorded recorded;
        CHECK (instrument.deactivate (recordInto (recorded)));
        CHECK (instrument.getRequestInProgress() == LicenseManager::Request::deactivating);
        CHECK (instrument.getStatusText() == utf8 ("Deactivating\xe2\x80\xa6"));
        CHECK (pumpUntil ([&] { return recorded.calls > 0; }));

        CHECK_MSG (recorded.result.outcome == testCase.outcome, "got: " << recorded.result.message);
        CHECK_MSG (recorded.result.message == testCase.message, "got: " << recorded.result.message);
        CHECK (recorded.onMessageThread);
        CHECK (instrument.isActivated() == ! testCase.removed);
        CHECK (LicenseStore (temporary.folder).read().activations.size() == (testCase.removed ? 0u : 1u));

        const auto sent = server->sent();
        CHECK (sent.size() == 1);

        if (sent.size() == 1)
        {
            CHECK (sent[0].path == "/api/deactivate");
            CHECK (jsonField (sent[0], "key") == LicenseKey::format (keyArtFreq));
            CHECK (jsonField (sent[0], "machineId") == machineHere);
            CHECK (! sent[0].json.hasProperty ("productId"));
        }
    }
}

ARTFREQ_TEST (licenseDeactivatingProLocksBothPlugins)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    CHECK (LicenseStore (temporary.folder).put (storedActivationFrom (activateProFresh, keyPro)));

    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
    LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));
    CHECK (instrument.isActivated() && effect.isActivated());
    CHECK (effect.getDeactivationWarning().contains ("ArtFreq and ArtFreq FX will both be locked"));

    server->reply (deactivateArtFreq);
    const auto recorded = deactivateAndWait (effect);
    CHECK (recorded.result.outcome == Outcome::deactivated);
    CHECK (recorded.result.message.endsWith ("ArtFreq and ArtFreq FX are now locked on this computer."));
    CHECK (! effect.isActivated());
    CHECK (pumpUntil ([&] { return ! instrument.isActivated(); }));

    // Nothing to deactivate now: said at once, nothing sent.
    const auto nothing = deactivateAndWait (effect);
    CHECK (nothing.result.outcome == Outcome::notActivated);
    CHECK (server->numSent() == 1);
}

ARTFREQ_TEST (licenseTwoKeysOnOneComputerDeactivateOneAtATime)
{
    // A buyer of ArtFreq who later bought ArtFreq Pro (for ArtFreq FX) has both keys activated on
    // this computer. Deactivating frees the newer key, ArtFreq Pro: that locks ArtFreq FX, and
    // the ArtFreq key keeps ArtFreq. Both the question and the result say so; then the ArtFreq
    // key can be deactivated in turn.
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    const LicenseStore store (temporary.folder);

    auto artFreq = storedActivationFrom (activateArtFreqFresh, keyArtFreq);
    auto pro = storedActivationFrom (activateProFresh, keyPro, "ArtFreq FX");
    pro.activatedAt = "2021-01-01T00:00:00.000Z";   // the newer one
    CHECK (store.put (artFreq) && store.put (pro));

    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
    LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));
    CHECK (instrument.isActivated() && effect.isActivated());

    if (const auto& activation = instrument.getActivation())
    {
        CHECK (activation->productId == PRODUCT_ID_ARTFREQ_PRO);
        CHECK (activation->pluginsKeptByAnotherKey == juce::StringArray { "ArtFreq" });
        CHECK (activation->anotherKeyProduct == "ArtFreq");
    }

    const juce::String warning ("Deactivating frees your ArtFreq Pro key for another computer. ArtFreq FX will be locked on "
                                "this computer until you activate again; ArtFreq stays activated with your ArtFreq key.");
    CHECK_MSG (instrument.getDeactivationWarning() == warning, instrument.getDeactivationWarning());
    CHECK (effect.getDeactivationWarning() == warning);

    server->reply (deactivateArtFreq);
    const auto recorded = deactivateAndWait (instrument);
    CHECK (recorded.result.outcome == Outcome::deactivated);
    CHECK_MSG (recorded.result.message == "Deactivated. Your ArtFreq Pro key can now be activated on another computer. ArtFreq FX "
                                          "is now locked on this computer; ArtFreq stays activated with your ArtFreq key.",
               recorded.result.message);
    CHECK (jsonField (server->sent().back(), "key") == LicenseKey::format (keyPro));

    CHECK (instrument.isActivated() && instrument.getActivation()->productId == PRODUCT_ID_ARTFREQ);
    CHECK (pumpUntil ([&] { return ! effect.isActivated(); }));

    // The ArtFreq key alone now: the plain question, and the instrument locks once it goes.
    CHECK (instrument.getDeactivationWarning() == "Deactivating frees your key for another computer. ArtFreq will be locked "
                                                  "on this computer until you activate again.");
    server->reply (deactivateArtFreq);
    CHECK (deactivateAndWait (instrument).result.message.endsWith ("ArtFreq is now locked on this computer."));
    CHECK (! instrument.isActivated() && store.read().activations.empty());
}

ARTFREQ_TEST (licenseADeactivationCutOffLeavesTheComputerLocked)
{
    // The project closes (or the DAW quits) while a deactivation is with the server: the request
    // is cut off and its answer never arrives. The server may have freed this computer, so the
    // activation must not go on counting here (with its place free, the key could go to a third
    // computer). It was marked before the request was sent, and the mark stays: once the request
    // could have ended, the computer is locked everywhere, and deactivating finishes the job.
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    const LicenseStore store (temporary.folder);
    CHECK (store.put (storedActivationFrom (activateProFresh, keyPro)));

    auto environment = makeEnvironment (temporary.folder, server);
    // A request may take 1.25 of these: the mark counts after 2. (Long enough for the project to
    // close, below, while the answer is still on its way, even on a slow, busy machine.)
    environment.requestTimeoutMs = 2000;

    LicenseManager other (artFreqInstrumentLicensing(), environment);   // another instance, open all along

    {
        LicenseManager effect (artFreqEffectLicensing(), environment);
        server->replyAfter (deactivateArtFreq, 1500);
        Recorded never;
        CHECK (effect.deactivate (recordInto (never)));
        CHECK (server->started.wait (10000));

        // On its way: marked in the file, and still activated everywhere (the request may fail
        // and change nothing).
        const auto marked = store.read().activations;
        CHECK (marked.size() == 1 && marked[0].deactivatingSince.isNotEmpty());
        pump (60);
        CHECK (effect.isActivated() && other.isActivated());
    }   // the project closes: the request is cut off

    CHECK (server->cancelsSeen.load() == 1);
    CHECK (store.read().activations.size() == 1 && store.read().activations[0].deactivatingSince.isNotEmpty());

    // Once the request could have ended, the open instance locks by itself, and a new one is
    // locked from the start; both know why.
    CHECK (pumpUntil ([&] { return ! other.isActivated(); }));
    CHECK (other.getInterruptedDeactivation().has_value() && ! other.getActivation().has_value());
    CHECK (other.getStatusText().startsWith ("Deactivating your ArtFreq Pro key didn't finish, so this computer is locked."));

    {
        LicenseManager later (artFreqEffectLicensing(), environment);
        CHECK (! later.isActivated() && later.getInterruptedDeactivation().has_value());
    }

    // Deactivating finishes it: the server had freed this computer already (not_active_here).
    // Nothing is marked again meanwhile, so nothing unlocks while it runs.
    server->replyAfter (deactivateArtFreqAgain, 100);
    Recorded finished;
    CHECK (other.deactivate (recordInto (finished)));
    CHECK (server->started.wait (10000));
    CHECK (! other.isActivated());
    CHECK (pumpUntil ([&] { return finished.calls > 0; }));
    CHECK (finished.result.outcome == Outcome::deactivated);
    CHECK (finished.result.message.startsWith ("Deactivated here (the server no longer counted this computer for that key)."));
    CHECK (store.read().activations.empty());
    CHECK (! other.getInterruptedDeactivation().has_value());
    CHECK (other.getStatusText() == "Not activated. Enter the license key from your purchase email.");

    // Or, cut off again, activating again with the key replaces it (the server answers as for a
    // computer it knows, or gives back the place it freed).
    auto cutOff = storedActivationFrom (activateProFresh, keyPro);
    cutOff.deactivatingSince = "2021-01-01T00:00:00.000Z";
    CHECK (store.put (cutOff));
    CHECK (pumpUntil ([&] { return other.getInterruptedDeactivation().has_value(); }));
    CHECK (! other.isActivated());

    server->reply (activateProAsArtFreq);   // (the instrument tries an ArtFreq key first)
    server->reply (activateProFresh);
    CHECK (activateAndWait (other, keyPro).result.outcome == Outcome::activated);
    CHECK (other.isActivated() && ! other.getInterruptedDeactivation().has_value());
    CHECK (store.read().activations.size() == 1 && store.read().activations[0].deactivatingSince.isEmpty());
}

ARTFREQ_TEST (licenseADeactivationTheServerDidNotDoChangesNothing)
{
    // A deactivation the server refuses or fails, or that gets no answer (no connection, or none
    // in time), takes its mark off again: the computer stays activated, and another open
    // instance never locked meanwhile.
    const juce::ScopedJuceInitialiser_GUI gui;

    for (const auto failure : { 0, 1, 2 })
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        const LicenseStore store (temporary.folder);
        CHECK (store.put (storedActivationFrom (activateProFresh, keyPro)));

        // The other instance takes the mark for an interrupted deactivation once it is two
        // timeouts old, so the request must be over by then: two seconds here (not the 300 ms of
        // the tests that time a cut-off), which leaves a slow, busy machine 1.5 s to spare.
        auto environment = makeEnvironment (temporary.folder, server);
        environment.requestTimeoutMs = 2000;
        LicenseManager effect (artFreqEffectLicensing(), environment);
        LicenseManager other (artFreqInstrumentLicensing(), environment);

        if (failure == 0)
            server->replyAfter (cloudflare502, 150);
        else if (failure == 1)
            server->noConnection();
        else
            server->replyAfter (cloudflare502, 6000);   // past the deadline (and the mark's grace)

        Recorded recorded;
        CHECK (effect.deactivate (recordInto (recorded)));

        auto otherStayed = true;
        CHECK (pumpUntil ([&] { otherStayed = otherStayed && other.isActivated() && effect.isActivated(); return recorded.calls > 0; }));
        CHECK (otherStayed);
        CHECK (recorded.result.message.endsWith ("This computer stays activated."));
        CHECK (store.read().activations.size() == 1 && store.read().activations[0].deactivatingSince.isEmpty());

        pump (4100);   // past the mark's grace: nothing locks later either
        CHECK (effect.isActivated() && other.isActivated() && ! other.getInterruptedDeactivation().has_value());
    }
}

//==============================================================================
// One file for the computer
//==============================================================================

ARTFREQ_TEST (licenseOneActivationForEveryAccountOnTheComputer)
{
    // The server counts computers, not accounts. Two accounts of one computer share its folder
    // (each also has its own, where the file was kept before): an activation made in one
    // counts in the other at once, without asking again, and deactivating in the other locks
    // both (before, the second account was asked for the key again, and its deactivation freed
    // the computer's place while the first stayed unlocked).
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder computer, accountA, accountB;
    const auto server = std::make_shared<FakeServer>();

    auto environmentA = makeEnvironment (computer.folder, server);
    auto environmentB = makeEnvironment (computer.folder, server);
    environmentA.userFolder = accountA.folder;
    environmentB.userFolder = accountB.folder;

    LicenseManager a (artFreqEffectLicensing(), environmentA);
    LicenseManager b (artFreqEffectLicensing(), environmentB);
    CHECK (a.getStores().size() == 2 && a.getStore().getDirectory() == computer.folder);

    server->reply (activateProFresh);
    CHECK (activateAndWait (a, keyPro).result.outcome == Outcome::activated);
    CHECK (pumpUntil ([&] { return b.isActivated(); }));
    CHECK (b.getStatusText().startsWith ("Activated with your ArtFreq Pro key"));
    CHECK (LicenseStore (computer.folder).read().activations.size() == 1);
    CHECK (! LicenseStore (accountA.folder).getFile().exists());

    server->reply (deactivateArtFreq);
    CHECK (deactivateAndWait (b).result.outcome == Outcome::deactivated);
    CHECK (pumpUntil ([&] { return ! a.isActivated(); }));
    CHECK (LicenseStore (computer.folder).read().activations.empty());
    CHECK (server->numSent() == 2);

    // An activation in an account's own folder (where earlier versions kept it): it counts in
    // that account, and deactivating takes it from there.
    CHECK (LicenseStore (accountA.folder).put (storedActivationFrom (activateProFresh, keyPro)));
    CHECK (pumpUntil ([&] { return a.isActivated(); }));
    CHECK (! b.isActivated());
    server->reply (deactivateArtFreq);
    CHECK (deactivateAndWait (a).result.outcome == Outcome::deactivated);
    CHECK (! a.isActivated() && LicenseStore (accountA.folder).read().activations.empty());

    // Activated again: into the computer's folder, and the account's own copy goes.
    CHECK (LicenseStore (accountA.folder).put (storedActivationFrom (activateProFresh, keyPro)));
    server->reply (activateProFresh);
    CHECK (activateAndWait (a, keyPro).result.outcome == Outcome::activated);
    CHECK (LicenseStore (computer.folder).read().activations.size() == 1);
    CHECK (LicenseStore (accountA.folder).read().activations.empty());
    CHECK (pumpUntil ([&] { return b.isActivated(); }));
}

ARTFREQ_TEST (licenseTheAccountsOwnFolderWhenTheComputersCantBeWritten)
{
    // The computer's folder can't be written to (another account made it and couldn't open it
    // to the others, or the system won't let anyone): the activation goes into this account's
    // own folder rather than fail. With neither, it says where it couldn't be saved.
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder parent, account;
    const auto server = std::make_shared<FakeServer>();
    const auto blocked = parent.folder.getChildFile ("AnyMeans");
    CHECK (blocked.replaceWithText ("a file where the folder would be"));

    auto environment = makeEnvironment (blocked, server);
    environment.userFolder = account.folder;
    LicenseManager effect (artFreqEffectLicensing(), environment);

    server->reply (activateProFresh);
    CHECK (activateAndWait (effect, keyPro).result.outcome == Outcome::activated);
    CHECK (effect.isActivated());
    CHECK (LicenseStore (account.folder).read().activations.size() == 1);

    LicenseManager alone (artFreqEffectLicensing(), makeEnvironment (blocked, server));
    server->reply (activateProFresh);
    const auto recorded = activateAndWait (alone, keyPro);
    CHECK (recorded.result.outcome == Outcome::notSaved);
    CHECK (recorded.result.message.contains (blocked.getFullPathName()));
}

ARTFREQ_TEST (licenseTheComputersFolderIsOpenToEveryAccount)
{
    // The computer's folder is made so that every account can replace the file another wrote
    // (each write replaces it whole): rwxrwxrwx, no sticky bit, and files readable by all
    // whatever the umask. (Windows: an access list giving Users Modify, which this can't check
    // on Linux.) A folder for one account is left as the umask makes it.
   #if JUCE_WINDOWS
    std::printf ("    (Windows: the access list is not checked here)\n");
   #else
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto shared = temporary.folder.getChildFile ("Computer").getChildFile ("AnyMeans");
    const auto own = temporary.folder.getChildFile ("Account").getChildFile ("AnyMeans");

    const auto modeOf = [] (const juce::File& file)
    {
        struct stat info {};
        return ::stat (file.getFullPathName().toRawUTF8(), &info) == 0 ? static_cast<int> (info.st_mode & 07777) : -1;
    };

    const auto previousMask = ::umask (077);   // the strictest a user might have
    const LicenseStore store (shared, LicenseStore::Sharing::everyAccount);
    const LicenseStore ownStore (own);
    const auto written = store.put (storedActivationFrom (activateProFresh, keyPro)) && ownStore.put (storedActivationFrom (activateProFresh, keyPro));
    const auto fallbackId = store.readOrCreateFallbackMachineIdentifier();
    ::umask (previousMask);

    CHECK (written && fallbackId.length() == 32);
    CHECK (store.getSharing() == LicenseStore::Sharing::everyAccount && ownStore.getSharing() == LicenseStore::Sharing::thisAccount);
    CHECK_MSG (modeOf (shared) == 0777, modeOf (shared));
    CHECK_MSG (modeOf (store.getFile()) == 0644, modeOf (store.getFile()));
    CHECK (modeOf (shared.getChildFile ("MachineId.txt")) == 0644);
    CHECK (shared.getChildFile (LicenseStore::FolderLock::lockFileName).existsAsFile());
    CHECK (modeOf (own) == 0700 && modeOf (ownStore.getFile()) == 0600);

    // A second write (another account's, say) keeps it so.
    CHECK (store.remove (PRODUCT_ID_ARTFREQ_PRO, keyPro, machineHere));
    CHECK (modeOf (shared) == 0777 && modeOf (store.getFile()) == 0644);
   #endif
}

//==============================================================================
// The manager: threads and other instances
//==============================================================================

ARTFREQ_TEST (licenseManagerOneRequestAtATimeAndSafeToDestroy)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    // One request at a time.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->hang();
        LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));

        Recorded first, second;
        CHECK (instrument.activate (keyArtFreq, recordInto (first)));
        CHECK (! instrument.activate (keyArtFreq, recordInto (second)));
        CHECK (! instrument.deactivate (recordInto (second)));
        CHECK (server->hanging.wait (10000));

        // Cancelled (the editor closing): no callback, not busy, and the next request works.
        instrument.cancelRequest();
        CHECK (! instrument.isBusy());
        CHECK (pumpUntil ([&] { return server->cancelsSeen == 1; }));
        pump (50);
        CHECK (first.calls == 0 && second.calls == 0);

        server->reply (activateArtFreqFresh);
        CHECK (activateAndWait (instrument, keyArtFreq).result.outcome == Outcome::activated);
    }

    // Destroyed while the server keeps it waiting: returns at once, the request is stopped, and
    // the callback never comes.
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->hang();
        auto instrument = std::make_unique<LicenseManager> (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));

        Recorded recorded;
        CHECK (instrument->activate (keyArtFreq, recordInto (recorded)));
        CHECK (server->hanging.wait (10000));

        const auto started = juce::Time::getMillisecondCounter();
        instrument.reset();
        CHECK_LESS (juce::Time::getMillisecondCounter() - started, 1000);
        CHECK (server->cancelsSeen == 1);

        pump (100);
        CHECK (recorded.calls == 0);
    }

    // Destroyed after the answer came but before the callback was delivered: no callback. The
    // activation the server granted is kept (activating again would be free anyway).
    {
        const TemporaryFolder temporary;
        const auto server = std::make_shared<FakeServer>();
        server->reply (activateArtFreqFresh);
        auto instrument = std::make_unique<LicenseManager> (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));

        Recorded recorded;
        CHECK (instrument->activate (keyArtFreq, recordInto (recorded)));

        for (int i = 0; i < 200 && ! LicenseStore (temporary.folder).getFile().exists(); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));

        std::this_thread::sleep_for (std::chrono::milliseconds (50));
        instrument.reset();
        pump (100);
        CHECK (recorded.calls == 0);
        CHECK (LicenseManager (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server)).isActivated());
    }

    // A typo's answer, posted for later, isn't delivered to a manager that is gone either.
    {
        const TemporaryFolder temporary;
        auto instrument = std::make_unique<LicenseManager> (artFreqInstrumentLicensing(),
                                                            makeEnvironment (temporary.folder, std::make_shared<FakeServer>()));
        Recorded recorded;
        CHECK (instrument->activate ("typo", recordInto (recorded)));
        instrument.reset();
        pump (50);
        CHECK (recorded.calls == 0);
    }
}

ARTFREQ_TEST (licenseManagerFollowsTheFileOtherInstancesWrite)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    const LicenseStore store (temporary.folder);

    LicenseManager instrument (artFreqInstrumentLicensing(), makeEnvironment (temporary.folder, server));
    LicenseManager effect (artFreqEffectLicensing(), makeEnvironment (temporary.folder, server));
    CountingListener listener;
    instrument.addListener (&listener);

    // Another plug-in (another process, even) activates ArtFreq Pro.
    CHECK (store.put (storedActivationFrom (activateProFresh, keyPro, "ArtFreq FX")));
    CHECK (pumpUntil ([&] { return instrument.isActivated() && effect.isActivated(); }));
    CHECK (listener.calls == 1 && listener.allOnMessageThread);

    // Something writes the file badly while they run: they keep what they had.
    CHECK (store.getFile().replaceWithText ("{ \"activations\": [ { \"productId\": "));
    pump (150);
    CHECK (instrument.isActivated() && effect.isActivated());

    // Another instance deactivates: the file is written without it.
    CHECK (store.getFile().replaceWithText ("{ \"format\": 1, \"activations\": [] }"));
    CHECK (pumpUntil ([&] { return ! instrument.isActivated() && ! effect.isActivated(); }));
    CHECK (listener.calls == 2);

    // Activated again, then the folder removed by hand.
    CHECK (store.put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));
    CHECK (pumpUntil ([&] { return instrument.isActivated(); }));
    CHECK (! effect.isActivated());
    CHECK (temporary.folder.deleteRecursively());
    CHECK (pumpUntil ([&] { return ! instrument.isActivated(); }));

    instrument.removeListener (&listener);

    // The flag the audio thread reads, read from another thread while it changes.
    std::atomic<bool> stop { false };
    std::atomic<int> reads { 0 };
    std::thread audio ([&]
    {
        while (! stop)
        {
            reads += instrument.getActivatedFlag().load (std::memory_order_acquire) ? 2 : 1;
            std::this_thread::yield();
        }
    });

    // (The reader is reading before the flag changes, and reads again after it has: a slow, busy
    // machine can keep a new thread waiting longer than the change takes.)
    CHECK (pumpUntil ([&] { return reads.load() > 0; }));
    CHECK (LicenseStore (temporary.folder).put (storedActivationFrom (activateArtFreqFresh, keyArtFreq)));
    CHECK (pumpUntil ([&] { return instrument.isActivated(); }));
    const auto readsBefore = reads.load();
    CHECK (pumpUntil ([&] { return reads.load() > readsBefore; }));
    stop = true;
    audio.join();
    CHECK (reads > 0);
    CHECK (instrument.getActivatedFlag().is_lock_free());
}

ARTFREQ_TEST (licenseDefaultEnvironmentCanBeReplacedByTests)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const TemporaryFolder temporary;
    const auto server = std::make_shared<FakeServer>();
    CHECK (LicenseStore (temporary.folder).put (storedActivationFrom (activateProFresh, keyPro)));

    LicenseManager::setDefaultEnvironmentForTesting (makeEnvironment (temporary.folder, server));

    {
        const LicenseManager effect (artFreqEffectLicensing());
        CHECK (effect.isActivated());
        CHECK (effect.getStore().getDirectory() == temporary.folder);
        CHECK (effect.getMachineId() == machineHere);
    }

    // Back to the runner's own (the plug-in instances of later tests are activated in it).
    restoreRunnerLicensing();

    // The real folders: the computer's AnyMeans folder (on Linux, which ships nothing, the
    // user's), and the user's own in their application data, where the file was kept before.
    const auto real = LicenseStore::defaultDirectory();
    const auto user = LicenseStore::userDirectory();
    CHECK (real.getFileName() == "AnyMeans" && user.getFileName() == "AnyMeans");
    CHECK (user.isAChildOf (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)));

   #if JUCE_MAC
    CHECK (real == juce::File ("/Users/Shared/AnyMeans"));
   #elif JUCE_WINDOWS
    CHECK (real.getParentDirectory() == juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory));
   #else
    CHECK (real == user);
   #endif

   #if JUCE_MAC || JUCE_WINDOWS
    CHECK (! real.isAChildOf (juce::File::getSpecialLocation (juce::File::userHomeDirectory)));
    CHECK (LicenseStore (real).getSharing() == LicenseStore::Sharing::everyAccount);
   #endif

    CHECK (LicenseStore (user).getSharing() == LicenseStore::Sharing::thisAccount || real == user);
}

//==============================================================================
// The real transport, against a server on this computer
//==============================================================================

namespace
{

/** A one-request HTTP server on 127.0.0.1: answers with a given status and body, or never. */
class LocalHttpServer
{
public:
    LocalHttpServer (int statusToSend, const juce::String& bodyToSend, bool answer)
        : LocalHttpServer (statusToSend, juce::MemoryBlock (bodyToSend.toRawUTF8(), bodyToSend.getNumBytesAsUTF8()), answer)
    {
    }

    /** Any bytes at all as the body, UTF-8 or not. */
    LocalHttpServer (int statusToSend, juce::MemoryBlock bodyToSend, bool answer)
        : status (statusToSend), body (std::move (bodyToSend))
    {
        listening = listener.createListener (0, "127.0.0.1");

        if (listening && answer)
            thread = std::thread ([this] { serveOne(); });
    }

    ~LocalHttpServer()
    {
        listener.close();

        if (thread.joinable())
            thread.join();
    }

    bool isListening() const     { return listening; }
    juce::String baseUrl() const { return "http://127.0.0.1:" + juce::String (listener.getBoundPort()); }
    juce::String request() const { const std::lock_guard<std::mutex> lock (mutex); return received; }

private:
    void serveOne()
    {
        std::unique_ptr<juce::StreamingSocket> connection (listener.waitForNextConnection());

        if (connection == nullptr)
            return;

        // Read the headers, then as much body as they announce.
        juce::MemoryOutputStream in;
        char byte = 0;

        while (! in.toString().contains ("\r\n\r\n") && connection->waitUntilReady (true, 3000) == 1 && connection->read (&byte, 1, true) == 1)
            in.writeByte (byte);

        const auto headers = in.toString();
        const auto lengthLine = headers.fromFirstOccurrenceOf ("Content-Length:", false, true).upToFirstOccurrenceOf ("\r\n", false, false);
        auto remaining = lengthLine.trim().getIntValue();

        while (remaining-- > 0 && connection->waitUntilReady (true, 3000) == 1 && connection->read (&byte, 1, true) == 1)
            in.writeByte (byte);

        {
            const std::lock_guard<std::mutex> lock (mutex);
            received = in.toString();
        }

        const auto head = "HTTP/1.1 " + juce::String (status) + " Status\r\nContent-Type: application/json\r\nContent-Length: "
                        + juce::String (body.getSize()) + "\r\nConnection: close\r\n\r\n";

        juce::MemoryBlock reply (head.toRawUTF8(), head.getNumBytesAsUTF8());
        reply.append (body.getData(), body.getSize());
        connection->write (reply.getData(), static_cast<int> (reply.getSize()));
        connection->close();
    }

    juce::StreamingSocket listener;
    bool listening = false;
    const int status;
    const juce::MemoryBlock body;
    std::thread thread;
    mutable std::mutex mutex;
    juce::String received;
};

} // namespace

ARTFREQ_TEST (licenseHttpsTransportPostsJsonAndReadsAnswers)
{
    // An answer: status and body come back as sent, the request is a JSON POST.
    {
        LocalHttpServer local (403, juce::String::fromUTF8 (activateUnknownKey.body), true);
        CHECK (local.isListening());

        CancelToken token;
        const auto reply = makeHttpsTransport (local.baseUrl())->post ("/api/activate", "{\"key\":\"A\"}", 5000, token);

        CHECK (reply.outcome == HttpReply::Outcome::answered);
        CHECK (reply.statusCode == 403);
        CHECK (reply.body == juce::String::fromUTF8 (activateUnknownKey.body));

        const auto request = local.request();
        CHECK_MSG (request.startsWith ("POST /api/activate "), request);
        CHECK (request.containsIgnoreCase ("Content-Type: application/json"));
        CHECK (request.endsWith ("{\"key\":\"A\"}"));
    }

    // Bytes that aren't UTF-8 (a portal's Latin-1 page): the status comes back, the body doesn't
    // (and JUCE's debug assertion on such text isn't reached).
    {
        const unsigned char latin1[] = { '<', 'p', '>', 'C', 'r', 0xe8, 'm', 'e', '<', '/', 'p', '>' };
        LocalHttpServer local (200, juce::MemoryBlock (latin1, sizeof (latin1)), true);
        CHECK (local.isListening());

        CancelToken token;
        const auto reply = makeHttpsTransport (local.baseUrl())->post ("/api/activate", "{}", 5000, token);
        CHECK (reply.outcome == HttpReply::Outcome::answered);
        CHECK (reply.statusCode == 200);
        CHECK (reply.body.isEmpty());
    }

    // Nobody listening: no connection, quickly.
    {
        juce::StreamingSocket probe;
        CHECK (probe.createListener (0, "127.0.0.1"));
        const auto closedPort = probe.getBoundPort();
        probe.close();

        CancelToken token;
        const auto started = juce::Time::getMillisecondCounter();
        const auto reply = makeHttpsTransport ("http://127.0.0.1:" + juce::String (closedPort))->post ("/api/activate", "{}", 3000, token);
        CHECK (reply.outcome == HttpReply::Outcome::noConnection);
        CHECK_LESS (juce::Time::getMillisecondCounter() - started, 3500);
    }

    // A server that accepts and never answers: cancelled from another thread.
    {
        LocalHttpServer silent (200, juce::String(), false);
        CHECK (silent.isListening());

        CancelToken token;
        HttpReply reply;
        std::thread request ([&] { reply = makeHttpsTransport (silent.baseUrl())->post ("/api/activate", "{}", 1500, token); });

        std::this_thread::sleep_for (std::chrono::milliseconds (150));
        const auto started = juce::Time::getMillisecondCounter();
        token.cancel();
        request.join();

        CHECK (reply.outcome == HttpReply::Outcome::cancelled);
        CHECK_LESS (juce::Time::getMillisecondCounter() - started, 2500);

        // Once cancelled, a token stops the next request before it starts.
        CHECK (makeHttpsTransport (silent.baseUrl())->post ("/api/activate", "{}", 1500, token).outcome == HttpReply::Outcome::cancelled);
    }
}
