#pragma once

// AnyMeans licensing: every constant in one place.
//
// This folder (src/license/) is self-contained (it needs only JUCE: juce_core, juce_events and
// juce_cryptography), so the same code can go into the other AnyMeans plug-ins. When copying it into
// Creme or Vocalyst, this is the only file to change: keep the server, the public key and the product
// IDs, and replace the "plug-ins of this repository" section at the bottom with that plug-in's own.

#include <juce_core/juce_core.h>

namespace anymeans::license
{

//==============================================================================
// The server and the key its licences are signed with
//==============================================================================

/** The licence server (a Cloudflare Worker): POST /api/activate and /api/deactivate. It is built into
    every plug-in that ships, so it must keep answering at this address for as long as those
    plug-ins are in use.
*/
inline constexpr const char* SERVER_BASE_URL = "https://anymeans-store.selfiat-marketing.workers.dev";

/** The Ed25519 public key (the raw 32 bytes, in hex) of the key pair the server signs licences with.
    Its private half lives only in the server's settings (LICENSE_SIGNING_KEY). A plug-in is
    activated only by a licence this key verifies, so changing the server's key pair locks out
    every computer activated before, until it activates again.
*/
inline constexpr const char* SIGNING_PUBLIC_KEY_HEX = "7acdbd078095b8d36a9c91dfaa2166c820fbb382bc71d8659bfd1153231e2307";

inline constexpr const char* SUPPORT_EMAIL = "support@anymeansaudio.com";

/** How long one request may take before the plug-in gives up and says the server can't be reached. */
inline constexpr int REQUEST_TIMEOUT_MS = 15000;

//==============================================================================
// Products: what the store sells. Each package is its own product, and a key activates only
// the product it was bought for (the server checks the product ID strictly).
//==============================================================================

inline constexpr const char* PRODUCT_ID_ARTFREQ     = "prod_Ve87TF5PUEzRgj";  // ArtFreq (the instrument only)
inline constexpr const char* PRODUCT_ID_ARTFREQ_PRO = "prod_Ve87k69rq5fkZN";  // ArtFreq Pro: ArtFreq + ArtFreq FX
inline constexpr const char* PRODUCT_ID_CREME       = "prod_Ve87zT8mss1owN";  // Creme
inline constexpr const char* PRODUCT_ID_VOCALYST    = "prod_Ve87yi93nh2Rth";  // Vocalyst

struct Product
{
    const char* id;
    const char* name;             // as the buyer knows it (UTF-8)
    const char* pluginsUnlocked;  // what an activation of it unlocks on a computer, for messages
};

/** Every product, so any AnyMeans plug-in can name an activation it finds in the shared file. */
inline constexpr Product PRODUCTS[] =
{
    { PRODUCT_ID_ARTFREQ,     "ArtFreq",          "ArtFreq" },
    { PRODUCT_ID_ARTFREQ_PRO, "ArtFreq Pro",      "ArtFreq and ArtFreq FX" },
    { PRODUCT_ID_CREME,       "Cr\xc3\xa8me",     "Cr\xc3\xa8me" },
    { PRODUCT_ID_VOCALYST,    "Vocalyst",         "Vocalyst" },
};

/** The product with this ID, or nullptr. */
inline const Product* findProduct (const juce::String& productId)
{
    for (const auto& product : PRODUCTS)
        if (productId == product.id)
            return &product;

    return nullptr;
}

//==============================================================================
// What one plug-in accepts
//==============================================================================

/** What one plug-in binary accepts, and how it words the cases that depend on it. */
struct PluginLicensing
{
    /** The plug-in's name, as shown to the user ("ArtFreq FX"). */
    juce::String pluginName;

    /** The products whose keys unlock this plug-in, in the order activation tries them: the server
        checks the product strictly, so a key is sent with the first one and, when the server says
        the key is for another product (403 wrong_product), with the next.
    */
    juce::StringArray acceptedProductIds;

    /** Shown instead of the server's text when the key is for a product this plug-in doesn't
        accept (empty: the server's text). The server doesn't say which product the key is for, so
        this has to be right for any of them.
    */
    juce::String wrongProductMessage;

    /** A more precise message for a key this computer already knows to be for one product (it is
        activated here for that product). Empty product ID: none.
    */
    juce::String knownWrongProductId, knownWrongProductMessage;
};

//==============================================================================
// The plug-ins of this repository
//==============================================================================

/** ArtFreq, the instrument: in both packages, so it takes an ArtFreq key or an ArtFreq Pro key.
    ArtFreq is tried first: most keys typed here are for it, and a Pro key costs one more request.
*/
inline PluginLicensing artFreqInstrumentLicensing()
{
    return { "ArtFreq", { PRODUCT_ID_ARTFREQ, PRODUCT_ID_ARTFREQ_PRO }, {}, {}, {} };
}

/** ArtFreq FX, the effect: only in ArtFreq Pro. */
inline PluginLicensing artFreqEffectLicensing()
{
    return { "ArtFreq FX",
             { PRODUCT_ID_ARTFREQ_PRO },
             "That key isn't for ArtFreq FX. ArtFreq FX comes with ArtFreq Pro; a key for ArtFreq "
             "(without FX) unlocks only the ArtFreq instrument.",
             PRODUCT_ID_ARTFREQ,
             "That key is for ArtFreq (without FX). ArtFreq FX comes with ArtFreq Pro." };
}

} // namespace anymeans::license
