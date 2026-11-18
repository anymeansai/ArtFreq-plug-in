#pragma once

#include <juce_core/juce_core.h>

namespace anymeans::license
{

/** The licence the server signs on a successful activation, and the plug-in's check of it.

    The licence is one string of five fields separated by '|':

        AMLIC1|<productId>|<KEY>|<machineId>|<ISO time>

    productId is the product of the key's record on the server (for the instrument, ArtFreq's or
    ArtFreq Pro's), KEY the key in its stored form (20 capitals and digits, no dashes), machineId
    exactly the hashed ID the plug-in sent, and the time when the server signed it. The signature is
    standard Ed25519 over the licence's UTF-8 bytes, in standard base64.

    The plug-in is activated only by a licence that passes check() with the public key built into
    it, for a product it accepts, for the key stored with it and for this computer. So an entry
    written by hand, or copied from another computer, unlocks nothing: making one needs the
    server's private key.
*/
struct SignedLicense
{
    static constexpr const char* formatTag = "AMLIC1";
    static constexpr int numFields = 5;

    enum class Check
    {
        valid,
        badSignature,   // not signed by the server's key (or the signature isn't 64 bytes of canonical base64)
        malformed,      // signed, but not five fields starting with AMLIC1
        wrongProduct,   // signed, for a product this plug-in doesn't accept
        wrongKey,       // signed, for another key than the one stored with it
        wrongMachine    // signed, for another computer
    };

    /** Checks a licence and its signature. The signature is checked first, so nothing in an
        unsigned licence is ever looked at; the time field is not checked (a licence doesn't expire).

        @param publicKeyHex        the server's Ed25519 public key, 32 bytes in hex
        @param acceptedProductIds  the products this plug-in accepts
        @param normalisedKey       the key the licence must be for (LicenseKey::normalise form)
        @param machineId           this computer's hashed machine ID
    */
    static Check check (const juce::String& license,
                        const juce::String& signatureBase64,
                        const juce::String& publicKeyHex,
                        const juce::StringArray& acceptedProductIds,
                        const juce::String& normalisedKey,
                        const juce::String& machineId);

    /** Just the signature: true if signatureBase64 is the server's signature of license. */
    static bool isSignedBy (const juce::String& license, const juce::String& signatureBase64, const juce::String& publicKeyHex);

    /** The licence split at every '|' (empty fields kept). */
    static juce::StringArray split (const juce::String& license);

    /** The product ID field of a licence (empty if it has fewer than two fields). Only meaningful
        once check() has said the licence is valid.
    */
    static juce::String productIdOf (const juce::String& license);

    /** A short English description of a check's outcome, for logs and support. */
    static juce::String describe (Check);
};

} // namespace anymeans::license
