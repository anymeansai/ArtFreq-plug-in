#pragma once

// Licensing without the real server: vectors made by the server's own code, a fake server, and a
// test environment (a folder of its own, a fixed machine ID, the public key of a test key pair).
// Used by LicenseTests.cpp; the snapshot tool and processor tests can use it for an activated or
// an unactivated plug-in.
//
// The vectors were made by the deployed Worker's code (worker.js + license.js, copied Nov 18 2026)
// running in Node 22 against an in-memory KV, with LICENSE_SIGNING_KEY set to a THROWAWAY Ed25519
// key pair made by WebCrypto for these tests (its private half was never saved). So every body
// below is byte for byte what the Worker answers, and every licence is signed the way it signs,
// only with a different key: the tests check them with that key's public half.

#include "license/LicenseManager.h"
#include "license/LicenseStore.h"
#include "license/LicenseTransport.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace licensetest
{

//==============================================================================
// Keys from the server's generateLicenseKey / formatKey / isValidKeyFormat.

struct KeyVector
{
    const char* stored;     // as the server stores it
    const char* formatted;  // as the purchase email shows it
};

inline constexpr KeyVector serverKeys[] =
{
    { "BHH7TTGTG8BG7RCKK4C2", "BHH7T-TGTG8-BG7RC-KK4C2" },
    { "XR30YHZTARPJN271DZP3", "XR30Y-HZTAR-PJN27-1DZP3" },
    { "49CPB8NMK22FSSQP0NK2", "49CPB-8NMK2-2FSSQ-P0NK2" },
    { "HYXV98CRJGW8REWPGB6A", "HYXV9-8CRJG-W8REW-PGB6A" },
    { "J495W8MBQ6WBBS263QY9", "J495W-8MBQ6-WBBS2-63QY9" },
    { "P72065HTZ97JGYY5J0Y8", "P7206-5HTZ9-7JGYY-5J0Y8" },
    { "RP8TJE5WXYG919TJTD98", "RP8TJ-E5WXY-G919T-JTD98" },
    { "155FEDK1XFH4M8Q1JB39", "155FE-DK1XF-H4M8Q-1JB39" },
};

/** Typos the server's isValidKeyFormat refuses: one character changed, two swapped, one too few,
    one too many, a U (not in the alphabet).
*/
inline constexpr const char* serverRejectedKeys[] =
{
    "BHH8TTGTG8BG7RCKK4C2", "BHH7TTGTG8BG7RCKK9C2", "BHH7TTTGG8BG7RCKK4C2", "BHH7TTGTG8BG7RCKK4C", "BHH7TTGTG8BG7RCKK4C20", "BHU7TTGTG8BG7RCKK4C2",
    "XR31YHZTARPJN271DZP3", "XR30YHZTARPJN271D4P3", "XR30YHTZARPJN271DZP3", "XR30YHZTARPJN271DZP", "XR30YHZTARPJN271DZP30", "XRU0YHZTARPJN271DZP3",
    "49CQB8NMK22FSSQP0NK2", "49CPB8NMK22FSSQP0TK2", "49CPB8MNK22FSSQP0NK2", "49CPB8NMK22FSSQP0NK", "49CPB8NMK22FSSQP0NK20", "49UPB8NMK22FSSQP0NK2",
};

//==============================================================================
// The Worker's answers. Keys and machine IDs they are about:

inline constexpr const char* throwawayPublicKeyHex = "253b73e351b16a06a78c90a84ca29cbb7e0f782de82be2f73c83eb5ba563c487";
inline constexpr const char* keyArtFreq = "5VBNQ505RHW9P0A280QW";    // an ArtFreq key
inline constexpr const char* keyPro = "Y1F2FJVXBXWCNVBVCZKJ";        // an ArtFreq Pro key
inline constexpr const char* keyCreme = "P63R7HB3NKH50SGGNCRK";      // a Creme key
inline constexpr const char* keyFull = "W2B68Z6AAP1BX2H6DJ7X";       // an ArtFreq Pro key active on two other computers
inline constexpr const char* keyUnknown = "ZZZZZ777773333365N8D";    // a good checksum, but no such key
inline constexpr const char* machineHere = "f44e5dea76b69f7cbaa40516583c4dc50d531e0bd9e785cc61fb12f3e49526ec";   // SHA-256 of "AnyMeans|test machine here"
inline constexpr const char* machineOther = "a6327d438b81bc6497e77dc9b2b26735e621493c0a58aba0591ba38aefe78eaa";  // SHA-256 of "AnyMeans|test machine other 1"

struct Reply
{
    int status;
    const char* body;  // UTF-8
};

// POST /api/activate, in the order the server answered them (ArtFreq key first activated, then again; ...).
inline const Reply activateArtFreqFresh { 200, "{\"ok\":true,\"activationToken\":\"326b579a2b58a246c5400f3424750aaa8f8540090de1ea6c6aa75581edcd178d\",\"license\":\"AMLIC1|prod_Ve87TF5PUEzRgj|5VBNQ505RHW9P0A280QW|f44e5dea76b69f7cbaa40516583c4dc50d531e0bd9e785cc61fb12f3e49526ec|2026-11-18T04:54:13.674Z\",\"signature\":\"rMGQyNV3IxPUdKw0WuN8zaP3TYdTaDfJ6CKeST8Px9vcg1o0d3LuhKbbHlHQ5BEKrZdA7v//6RNhEh93B1HjCA==\",\"activationsUsed\":1,\"maxActivations\":2,\"alreadyActive\":false}" };
inline const Reply activateArtFreqAgain { 200, "{\"ok\":true,\"activationToken\":\"326b579a2b58a246c5400f3424750aaa8f8540090de1ea6c6aa75581edcd178d\",\"license\":\"AMLIC1|prod_Ve87TF5PUEzRgj|5VBNQ505RHW9P0A280QW|f44e5dea76b69f7cbaa40516583c4dc50d531e0bd9e785cc61fb12f3e49526ec|2026-11-18T04:54:13.676Z\",\"signature\":\"4uyfoWOXHDty0MItQRfuXadi/oSaU21YBcwGa8utNx1c2Iws4GZ4s9vs82vsDNRp0WgP3FEyf4zYOr+lWQWAAA==\",\"activationsUsed\":1,\"maxActivations\":2,\"alreadyActive\":true}" };
inline const Reply activateProAsArtFreq { 403, "{\"ok\":false,\"code\":\"wrong_product\",\"error\":\"That key is for a different AnyMeans Audio plug-in.\"}" };
inline const Reply activateProFresh { 200, "{\"ok\":true,\"activationToken\":\"73ffc611a985ec0a470db3a1462fe92ffaeb017096d5f0cbe6337b3d2affd4d4\",\"license\":\"AMLIC1|prod_Ve87k69rq5fkZN|Y1F2FJVXBXWCNVBVCZKJ|f44e5dea76b69f7cbaa40516583c4dc50d531e0bd9e785cc61fb12f3e49526ec|2026-11-18T04:54:13.678Z\",\"signature\":\"RutvGgTly2e3IfO5RKZDpBiUXpq+Tjp9PexSSaZiY+QOqV+v8HiB1Z8iplVJFUbvlCrHrKknSJb7x4aLXmrcBQ==\",\"activationsUsed\":1,\"maxActivations\":2,\"alreadyActive\":false}" };
inline const Reply activateArtFreqAsPro { 403, "{\"ok\":false,\"code\":\"wrong_product\",\"error\":\"That key is for a different AnyMeans Audio plug-in.\"}" };
inline const Reply activateCremeAsArtFreq { 403, "{\"ok\":false,\"code\":\"wrong_product\",\"error\":\"That key is for a different AnyMeans Audio plug-in.\"}" };
inline const Reply activateCremeAsPro { 403, "{\"ok\":false,\"code\":\"wrong_product\",\"error\":\"That key is for a different AnyMeans Audio plug-in.\"}" };
inline const Reply activateLimit { 409, "{\"ok\":false,\"code\":\"activation_limit\",\"error\":\"This key is already active on 2 machines. Deactivate it there first (or write to support@anymeansaudio.com).\"}" };
inline const Reply activateUnknownKey { 403, "{\"ok\":false,\"code\":\"unknown_key\",\"error\":\"That license key isn't valid \xe2" "\x80" "\x94" " check for typos and try again.\"}" };
inline const Reply activateInvalidKey { 403, "{\"ok\":false,\"code\":\"invalid_key\",\"error\":\"That license key isn't valid \xe2" "\x80" "\x94" " check for typos and try again.\"}" };
inline const Reply activateMissingMachine { 403, "{\"ok\":false,\"code\":\"missing_machine\",\"error\":\"A machine id is required to activate.\"}" };
inline const Reply activateNotJson { 400, "{\"ok\":false,\"code\":\"bad_request\",\"error\":\"Body must be JSON\"}" };
inline const Reply activateNoMachine { 400, "{\"ok\":false,\"code\":\"bad_request\",\"error\":\"key and machineId are required\"}" };
inline const Reply activateWrongMethod { 405, "{\"ok\":false,\"code\":\"method_not_allowed\",\"error\":\"POST only\"}" };
inline const Reply notFound { 404, "{\"ok\":false,\"code\":\"not_found\",\"error\":\"Not found\"}" };
// The Worker without LICENSE_SIGNING_KEY: ok, but no licence and no signature.
inline const Reply activateUnsigned { 200, "{\"ok\":true,\"activationToken\":\"326b579a2b58a246c5400f3424750aaa8f8540090de1ea6c6aa75581edcd178d\",\"activationsUsed\":1,\"maxActivations\":2,\"alreadyActive\":true}" };

// POST /api/deactivate.
inline const Reply deactivateArtFreq { 200, "{\"ok\":true,\"activationsUsed\":0,\"maxActivations\":2}" };
inline const Reply deactivateArtFreqAgain { 403, "{\"ok\":false,\"code\":\"not_active_here\",\"error\":\"This key isn't activated on that machine.\"}" };
inline const Reply deactivateUnknownKey { 403, "{\"ok\":false,\"code\":\"unknown_key\",\"error\":\"That license key isn't valid \xe2" "\x80" "\x94" " check for typos and try again.\"}" };
inline const Reply deactivateInvalidKey { 403, "{\"ok\":false,\"code\":\"invalid_key\",\"error\":\"That license key isn't valid \xe2" "\x80" "\x94" " check for typos and try again.\"}" };
inline const Reply deactivateNotJson { 400, "{\"ok\":false,\"code\":\"bad_request\",\"error\":\"Body must be JSON\"}" };

// Not from the Worker: what Cloudflare (or a network in between) answers when the Worker can't.
inline const Reply cloudflare502 { 502, "<!DOCTYPE html>\n<html><head><title>anymeans-store.selfiat-marketing.workers.dev | 502: Bad gateway</title></head><body><h1>Bad gateway</h1><p>Error code 502</p></body></html>\n" };
inline const Reply cloudflare500 { 500, "error code: 1101" };
inline const Reply rateLimited { 429, "error code: 1015" };
inline const Reply proxyForbidden { 403, "<html><body>Access to this site is blocked by your network.</body></html>" };
inline const Reply truncatedOk { 200, "{\"ok\":true,\"activationToken\":\"326b579a2b58" };
inline const Reply okFalse { 200, "{\"ok\":false}" };

//==============================================================================
// Signatures straight from WebCrypto (the throwaway key), over realistic licences and two plain messages.

struct SignedMessage
{
    const char* message;
    const char* signature;
};

inline constexpr SignedMessage signedMessages[] =
{
    { "AMLIC1|prod_Ve87k69rq5fkZN|Y1F2FJVXBXWCNVBVCZKJ|f44e5dea76b69f7cbaa40516583c4dc50d531e0bd9e785cc61fb12f3e49526ec|2026-11-18T09:15:02.417Z", "ZWxeIFScPcJUoux6rUOenu/z7MWcssVpzaFp5nT1XUPiwFOAGvdr1EGpidQiWvr2s6cEGjHBPHE7CuECHlj0DA==" },
    { "AMLIC1|prod_Ve87TF5PUEzRgj|5VBNQ505RHW9P0A280QW|a6327d438b81bc6497e77dc9b2b26735e621493c0a58aba0591ba38aefe78eaa|2026-12-01T23:59:59.999Z", "RNjCqPTLPwBM6gwi69V8pA943TFHA1Va4eEJd09NiM7Tf+9oR5vKxKOHrOWfIppOorGThL98bPDQeMyDkRVCCQ==" },
    { "AMLIC1|prod_Ve87zT8mss1owN|P63R7HB3NKH50SGGNCRK|e765c22a8a0de890ac1da07c8504c666671cdfbe5a0050cfb9eb44981af7b396|2027-03-04T05:06:07.000Z", "48gtGM/noE/mLFnMMkTEDPDfPyTLfCy9hsR+FfS8+6QLy7TQBAe9dy56Yxu4Xue9wYiiRzQKJvn8pgzB5bVEDA==" },
    { "", "IIPZdQ9wu2dP6UpQfFP3QmTglFfXiCwfSb5IOFXuUW1E6nkUInB7SLS3elYEI+jMN5IsIcAFvAdoYKFkgpqdCA==" },
    { "x", "v1BSewcePl5o3lITB6hvQslUGm5lbDZzt40Rql+dUWWwkjPXLIx7+uRXJI+az1hf3KvgU1OQf9itlAnNpAeZCw==" },
};

/** signedMessages[0]'s signature with S replaced by S + L: the same equation holds, but WebCrypto
    (checked when the vectors were made) and the plug-in must both refuse it.
*/
inline constexpr const char* nonCanonicalSignature = "ZWxeIFScPcJUoux6rUOenu/z7MWcssVpzaFp5nT1XUPPlEndNFp+LBhGgXcBVNkLtKcEGjHBPHE7CuECHlj0HA==";

/** RFC 8032, section 7.1, TEST 1 to 3 (public key, message, signature; hex). */
struct RfcVector
{
    const char* publicKey;
    const char* message;
    const char* signature;
};

inline constexpr RfcVector rfc8032Vectors[] =
{
    { "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "", "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
    { "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72", "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
    { "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82", "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
};

//==============================================================================
// SHA-512 from Node's crypto, of n bytes (i * 7 + 3) & 0xff, around the block and padding boundaries.

struct ShaVector
{
    int length;
    const char* digest;
};

inline constexpr ShaVector sha512PatternVectors[] =
{
    { 0, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
    { 1, "e45bf5817ddf94aa2f7a407071f0eedc6beb98f768b4cd33d1176d44d1563a45a5d7212290eb7670c6786b13591aedac86478993895e8b24e612014abaa6ba04" },
    { 3, "8853167ab14c043919981a6409307ef68b5b726f4b80dcab259daa24b7cabcb8b4bbfd3e2e36ed98465059661f672e6529cdcc673e7eabfa78dd3186745b112a" },
    { 55, "14fd424b1fcadee624da946ab03f7e1def7c0d6e00f689594319881a26ff30b875ba4c622ac13100c8cc784c9c2eb23159aecbb4a02e3999062f551193e2b256" },
    { 56, "480fa85be41ef55a41208ca28ffc8743c91cf7d24758defe6f95bfb16de614fc86b701034896b047dd571de4318853d80e0809df162f1752cb26da6ddb94a0dd" },
    { 63, "ecd42a703a4e93e163d60d55e3785b1a763838b0351bc2e6f7c94b4bfb24f9aa15da5d744ebcebe11f0fc4315d45ba3a047b6e60e07448357f2795bf34b73502" },
    { 64, "8f3cc30b3fb5bf963688a46488249248ac2c67f0f85a145233c6c1e3c16dcd1df634c07d1d31da02576f65b9cf64e1c3fdb318b689b8a14e2e9552bcf30fb133" },
    { 111, "68cffa6d0d76f309c9ce0d35280939f8e25990c43b7b086ccdf709be35b07d4ddba599541ff2b1c19d34ea49aeafb9659adb7ac3c0b078bb30a22d57fc6687ef" },
    { 112, "d0865c524d1dddf7c23b799c413f5adcd7caefd3f66a9b49750ec81066012c25a8bcf94ddea6dc525691673097ca40e0101e897fc97218cfdb0704084e2bef4b" },
    { 113, "606314353bd419f9f7720e8297f2af8a9ed5f0bb0bed29b716bf18e34577623ae7effa46f496bd4282134e7e895284a4760a910d2ad8ec11e9dd75863058cd9c" },
    { 127, "e0b6a20f1c0c88970a9340152cd5a1c1ecf3d3b8de55102741879438079473540133b812706e5dbec322c8c9523b6fc8c6d16ee626e87ad5fe3d2916afedc369" },
    { 128, "99b16f17aa0b969a5b8f08f367719d516e330ccd2660b6f0688ec031dbc783de50a1cd185a2568dba75070a2403d17d4741d163578515dfd2ff756ddfe4d47b1" },
    { 129, "a1556e29185778aa5991e34b8884c840d589f0fbb4b8ed590e51e9ac4eb03a008125000db2671f8fe7f485b59a77b518670078ecb41a54b4cd02a7f1d2ca4c6d" },
    { 239, "18ee83f30261c3c645d52aee6a209105b25bba39d33845ef48984cc238e4f21661fb7bd7dd4336f71c40fe87d95e5115d6c7be52e0d3e7e7877d24500b5b58df" },
    { 240, "9d60ee60d29ec4fa0b9690c04c1c29413bbe3ed345639182d9d53dcc05926b77b04f4fec1562fb85182954c96b7cbb5d5e4410251ff4f352d09a2da90419fb13" },
    { 255, "c2e3bb67012f9eb526202efa59997933f7d3e75e7ded738818bc27d94977f4573afddb1b2793745701e62affa3b7a1c8262c992a321f488a6b1942a4795bab98" },
    { 256, "e49c208e41556e859d1a52d14784a061c2d5ae2c8690a5360e9f9344f60861c1362a9ec05a9f08a4167b3da41bdd122a387413dd06976470e4beff5053f2ac71" },
    { 1000, "00e36fccf193e59697a92b5ab24666ce6326d7fa16bf10832d0991ddc591112e9dfa6a636950ed9c4d67344a760654c2ff7785e1d60094d651038735b5dccabd" },
};

inline constexpr const char* sha512MillionA = "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b";

//==============================================================================
/** A server in a box: answers each POST with the next scripted reply, and records what it was sent.
    Thread-safe: the manager calls it from its request thread.
*/
class FakeServer final : public anymeans::license::Transport
{
public:
    struct Sent
    {
        juce::String path;
        juce::String body;
        juce::var json;
        int timeoutMs = 0;
        bool onMessageThread = false;
    };

    void reply (const Reply& r)
    {
        Step step;
        step.reply.outcome = anymeans::license::HttpReply::Outcome::answered;
        step.reply.statusCode = r.status;
        step.reply.body = juce::String::fromUTF8 (r.body);
        add (std::move (step));
    }

    void replyWithBody (int status, const juce::String& body)
    {
        Step step;
        step.reply.outcome = anymeans::license::HttpReply::Outcome::answered;
        step.reply.statusCode = status;
        step.reply.body = body;
        add (std::move (step));
    }

    void noConnection()
    {
        Step step;
        step.reply.outcome = anymeans::license::HttpReply::Outcome::noConnection;
        add (std::move (step));
    }

    /** The next request waits until it is cancelled (an unresponsive server). */
    void hang()
    {
        Step step;
        step.hang = true;
        add (std::move (step));
    }

    /** The next request is answered after `milliseconds` (a slow server), unless it is cancelled
        first. `started` is signalled when it begins to wait. */
    void replyAfter (const Reply& r, int milliseconds)
    {
        Step step;
        step.reply.outcome = anymeans::license::HttpReply::Outcome::answered;
        step.reply.statusCode = r.status;
        step.reply.body = juce::String::fromUTF8 (r.body);
        step.delayMs = milliseconds;
        add (std::move (step));
    }

    anymeans::license::HttpReply post (const juce::String& path, const juce::String& body, int timeoutMs,
                                       anymeans::license::CancelToken& token) override
    {
        Step step;

        {
            const std::lock_guard<std::mutex> lock (mutex);

            Sent sent;
            sent.path = path;
            sent.body = body;
            sent.json = juce::JSON::parse (body);
            sent.timeoutMs = timeoutMs;
            sent.onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
            sentRequests.push_back (sent);

            if (! script.empty())
            {
                step = std::move (script.front());
                script.pop_front();
            }
            else
            {
                step.reply.outcome = anymeans::license::HttpReply::Outcome::answered;
                step.reply.statusCode = 500;
                step.reply.body = "the test didn't script this request";
            }
        }

        if (step.hang)
        {
            juce::WaitableEvent wake;

            if (token.setAbortAction ([&wake] { wake.signal(); }))
            {
                hanging.signal();
                wake.wait (30000);  // a test that never cancels fails, it doesn't hang the run
                token.clearAbortAction();
            }

            cancelsSeen += token.isCancelled() ? 1 : 0;
            anymeans::license::HttpReply cancelled;
            cancelled.outcome = anymeans::license::HttpReply::Outcome::cancelled;
            return cancelled;
        }

        if (step.delayMs > 0)
        {
            juce::WaitableEvent wake;

            if (token.setAbortAction ([&wake] { wake.signal(); }))
            {
                started.signal();
                wake.wait (step.delayMs);
                token.clearAbortAction();
            }

            if (token.isCancelled())
            {
                ++cancelsSeen;
                anymeans::license::HttpReply cancelled;
                cancelled.outcome = anymeans::license::HttpReply::Outcome::cancelled;
                return cancelled;
            }
        }

        return step.reply;
    }

    std::vector<Sent> sent() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return sentRequests;
    }

    size_t numSent() const    { return sent().size(); }

    juce::WaitableEvent hanging;      // signalled when a hanging request starts waiting
    juce::WaitableEvent started;      // signalled when a slow request (replyAfter) starts waiting
    std::atomic<int> cancelsSeen { 0 };

private:
    struct Step
    {
        anymeans::license::HttpReply reply;
        bool hang = false;
        int delayMs = 0;
    };

    void add (Step step)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        script.push_back (std::move (step));
    }

    mutable std::mutex mutex;
    std::deque<Step> script;
    std::vector<Sent> sentRequests;
};

//==============================================================================
/** A shared folder of its own for one test, deleted afterwards. */
struct TemporaryFolder
{
    TemporaryFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("AnyMeansLicenseTest", {}, false))
    {
        folder.createDirectory();
    }

    ~TemporaryFolder()
    {
        folder.deleteRecursively();
    }

    juce::File folder;

    JUCE_DECLARE_NON_COPYABLE (TemporaryFolder)
};

/** An environment that never leaves the test: its own folder, the fake server, this test
    computer's machine ID and the throwaway key pair's public key.
*/
inline anymeans::license::LicenseManager::Environment makeEnvironment (const juce::File& folder,
                                                                       std::shared_ptr<anymeans::license::Transport> transport,
                                                                       const juce::String& machineId = machineHere)
{
    anymeans::license::LicenseManager::Environment environment;
    environment.sharedFolder = folder;
    environment.transport = std::move (transport);
    environment.machineId = machineId;
    environment.publicKeyHex = throwawayPublicKeyHex;
    environment.requestTimeoutMs = 2000;
    environment.checkIntervalMs = 20;
    return environment;
}

/** The activation a successful /api/activate reply leaves in the shared file. */
inline anymeans::license::StoredActivation storedActivationFrom (const Reply& reply, const juce::String& key,
                                                                 const juce::String& activatedIn = "ArtFreq")
{
    const auto json = juce::JSON::parse (juce::String::fromUTF8 (reply.body));
    const auto license = json.getProperty ("license", {}).toString();

    anymeans::license::StoredActivation activation;
    activation.productId = juce::StringArray::fromTokens (license, "|", {})[1];
    activation.key = key;
    activation.machineId = machineHere;
    activation.token = json.getProperty ("activationToken", {}).toString();
    activation.license = license;
    activation.signature = json.getProperty ("signature", {}).toString();
    // Long ago: deactivating treats an activation of the last two minutes differently (the
    // server's records may not show it yet), and these stand for settled ones.
    activation.activatedAt = "2020-01-01T00:00:00.000Z";
    activation.activatedIn = activatedIn;
    return activation;
}

/** Runs the message loop until the condition holds or the time is up. The time is generous
    (as in WaitFor.h): only a failure waits that long, and a slow machine (the macOS runner's
    Intel half, under Rosetta) runs timers, listeners and requests' answers much later. */
inline bool pumpUntil (const std::function<bool()>& condition, int timeoutMs = 10000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

    while (! condition())
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;

        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }

    return true;
}

inline void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

//==============================================================================
// Plug-in instances in the test runner and the snapshot tool. A processor makes its
// LicenseManager the ordinary way, with the default environment: these set it, so no instance
// ever reads the user's real folder or reaches the real server.

/** What every plug-in instance gets unless a test says otherwise: activated with an ArtFreq Pro
    key (both plug-ins) on this test computer, in a folder of its own, against a fake server that
    answers nothing it was not told to. So the tests and snapshots of everything but licensing see
    the plug-ins as a buyer does once activated.
*/
struct RunnerLicensing
{
    RunnerLicensing()
    {
        anymeans::license::LicenseStore (folder.folder).put (storedActivationFrom (activateProFresh, keyPro));
        environment = makeEnvironment (folder.folder, server);
        environment.checkIntervalMs = 1000;   // as in a plug-in: hundreds of instances, nothing changes
    }

    TemporaryFolder folder;
    std::shared_ptr<FakeServer> server = std::make_shared<FakeServer>();
    anymeans::license::LicenseManager::Environment environment;
};

inline std::unique_ptr<RunnerLicensing>& runnerLicensing()
{
    static std::unique_ptr<RunnerLicensing> licensing;
    return licensing;
}

/** Makes the activated environment the default (before the first test or snapshot). */
inline void installRunnerLicensing()
{
    runnerLicensing() = std::make_unique<RunnerLicensing>();
    anymeans::license::LicenseManager::setDefaultEnvironmentForTesting (runnerLicensing()->environment);
}

/** Puts the runner's environment back as the default, after a test that replaced it. */
inline void restoreRunnerLicensing()
{
    anymeans::license::LicenseManager::setDefaultEnvironmentForTesting (
        runnerLicensing() != nullptr ? std::optional<anymeans::license::LicenseManager::Environment> (runnerLicensing()->environment)
                                     : std::nullopt);
}

/** At the end: back to no default, and the folder deleted. */
inline void removeRunnerLicensing()
{
    anymeans::license::LicenseManager::setDefaultEnvironmentForTesting (std::nullopt);
    runnerLicensing().reset();
}

/** For one test (or one part of the snapshot tool): plug-in instances made while this exists are
    not activated. They share a folder of their own, empty, and a fake server the test scripts,
    and they look for changes to the shared file every `checkIntervalMs`. activatePro() and
    deactivate() write and remove an activation there as another instance (or another AnyMeans
    plug-in) would; every instance follows within the interval, or at once after
    LicenseManager::reload(). Make it before the instances, so they go before it does.
*/
struct ScopedLockedLicensing
{
    explicit ScopedLockedLicensing (int checkIntervalMs = 20)
    {
        environment = makeEnvironment (folder.folder, server);
        environment.checkIntervalMs = checkIntervalMs;
        anymeans::license::LicenseManager::setDefaultEnvironmentForTesting (environment);
    }

    ~ScopedLockedLicensing()
    {
        restoreRunnerLicensing();
    }

    /** An ArtFreq Pro activation for this computer: both plug-ins unlock. */
    bool activatePro() const
    {
        return anymeans::license::LicenseStore (folder.folder).put (storedActivationFrom (activateProFresh, keyPro));
    }

    /** An ArtFreq activation for this computer: the instrument unlocks, ArtFreq FX does not. */
    bool activateArtFreq() const
    {
        return anymeans::license::LicenseStore (folder.folder).put (storedActivationFrom (activateArtFreqFresh, keyArtFreq));
    }

    /** An ArtFreq activation, then a newer ArtFreq Pro one (a buyer of ArtFreq who later bought
        ArtFreq Pro for ArtFreq FX): both plug-ins unlock, with the Pro key in use. */
    bool activateArtFreqThenPro() const
    {
        auto pro = storedActivationFrom (activateProFresh, keyPro, "ArtFreq FX");
        pro.activatedAt = "2021-01-01T00:00:00.000Z";
        const anymeans::license::LicenseStore store (folder.folder);
        return activateArtFreq() && store.put (pro);
    }

    /** Instances made from now on give a request this long (and a deactivation's mark twice
        that, see LicenseManager::deactivate): for tests that wait for one to run out. */
    void setRequestTimeout (int milliseconds)
    {
        environment.requestTimeoutMs = milliseconds;
        anymeans::license::LicenseManager::setDefaultEnvironmentForTesting (environment);
    }

    /** Removes every activation (as deactivating elsewhere does). */
    bool deactivate() const
    {
        const anymeans::license::LicenseStore store (folder.folder);
        auto removed = true;

        for (const auto& entry : store.read().activations)
            removed = store.remove (entry.productId, entry.key, entry.machineId) && removed;

        return removed;
    }

    TemporaryFolder folder;
    std::shared_ptr<FakeServer> server = std::make_shared<FakeServer>();
    anymeans::license::LicenseManager::Environment environment;

    JUCE_DECLARE_NON_COPYABLE (ScopedLockedLicensing)
};

} // namespace licensetest
