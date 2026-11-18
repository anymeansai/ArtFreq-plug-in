#pragma once

#include <juce_core/juce_core.h>

namespace anymeans::license
{

/** The license key format, as the server makes keys: 15 random characters from Crockford's base32
    alphabet, then 5 checksum characters (SHA-256 of the 15, the first five bytes modulo 32, mapped
    through the alphabet). Shown to buyers in groups of five: 8K3QX-7M2PD-R9T4W-NV6ZH.

    The checksum lets the plug-in catch a typo before it asks the server anything.
*/
struct LicenseKey
{
    /** The alphabet: digits and capitals without I, L, O and U. */
    static constexpr const char* alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    static constexpr int payloadLength = 15;
    static constexpr int checksumLength = 5;
    static constexpr int length = payloadLength + checksumLength;

    /** What the buyer typed or pasted, as the server stores keys: capitals, digits only (spaces,
        dashes, line breaks and anything else dropped, lowercase raised). Like the server, plus
        Crockford's rule for letters a key never contains: O reads as 0, I and L as 1, so a key
        copied by eye from a font where they look alike still works (the checksum still has the
        last word).
    */
    static juce::String normalise (const juce::String& typed);

    /** True if a normalised key has the right length, only alphabet characters and a matching checksum. */
    static bool isWellFormed (const juce::String& normalisedKey);

    /** The five checksum characters for a 15-character payload. */
    static juce::String checksumFor (const juce::String& payload);

    /** 8K3QX-7M2PD-R9T4W-NV6ZH (empty unless the key is 20 characters long). */
    static juce::String format (const juce::String& normalisedKey);

    /** 8K3QX-*****-*****-NV6ZH, with bullets (U+2022) for the stars: enough to tell keys apart in
        the interface without showing one.
    */
    static juce::String mask (const juce::String& normalisedKey);
};

} // namespace anymeans::license
