#include "LicenseKey.h"

#include <juce_cryptography/juce_cryptography.h>

namespace anymeans::license
{

juce::String LicenseKey::normalise (const juce::String& typed)
{
    juce::String result;
    result.preallocateBytes (static_cast<size_t> (length));

    for (auto c : typed.toUpperCase())
    {
        if (c == 'O')
            c = '0';
        else if (c == 'I' || c == 'L')
            c = '1';

        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))
            result += juce::String::charToString (c);
    }

    return result;
}

juce::String LicenseKey::checksumFor (const juce::String& payload)
{
    const auto digest = juce::SHA256 (payload.toUTF8()).getRawData();
    const auto* bytes = static_cast<const juce::uint8*> (digest.getData());

    juce::String checksum;

    for (int i = 0; i < checksumLength; ++i)
        checksum += juce::String::charToString (static_cast<juce::juce_wchar> (alphabet[bytes[i] % 32]));

    return checksum;
}

bool LicenseKey::isWellFormed (const juce::String& normalisedKey)
{
    if (normalisedKey.length() != length || ! normalisedKey.containsOnly (alphabet))
        return false;

    return checksumFor (normalisedKey.substring (0, payloadLength)) == normalisedKey.substring (payloadLength);
}

juce::String LicenseKey::format (const juce::String& normalisedKey)
{
    if (normalisedKey.length() != length)
        return {};

    return normalisedKey.substring (0, 5) + "-" + normalisedKey.substring (5, 10) + "-"
         + normalisedKey.substring (10, 15) + "-" + normalisedKey.substring (15);
}

juce::String LicenseKey::mask (const juce::String& normalisedKey)
{
    if (normalisedKey.length() != length)
        return {};

    // Five bullets (U+2022), written as UTF-8 bytes so no compiler has to guess the source's encoding.
    const auto hidden = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"));
    return normalisedKey.substring (0, 5) + "-" + hidden + "-" + hidden + "-" + normalisedKey.substring (15);
}

} // namespace anymeans::license
