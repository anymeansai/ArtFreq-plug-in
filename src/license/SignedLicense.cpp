#include "SignedLicense.h"

#include "Crypto.h"

namespace anymeans::license
{

bool SignedLicense::isSignedBy (const juce::String& license, const juce::String& signatureBase64, const juce::String& publicKeyHex)
{
    const auto signature = decodeBase64Strict (signatureBase64);
    const auto publicKey = decodeHex (publicKeyHex);

    if (! signature.has_value() || signature->getSize() != 64 || ! publicKey.has_value() || publicKey->getSize() != 32)
        return false;

    // The bytes signed are the licence's UTF-8 bytes, without a terminator.
    const auto utf8 = license.toUTF8();
    const auto numBytes = utf8.sizeInBytes() - 1;

    return verifyEd25519 (static_cast<const std::uint8_t*> (signature->getData()),
                          static_cast<const std::uint8_t*> (publicKey->getData()),
                          utf8.getAddress(), numBytes);
}

juce::StringArray SignedLicense::split (const juce::String& license)
{
    juce::StringArray fields;
    auto rest = license;

    for (;;)
    {
        const auto bar = rest.indexOfChar ('|');

        if (bar < 0)
        {
            fields.add (rest);
            return fields;
        }

        fields.add (rest.substring (0, bar));
        rest = rest.substring (bar + 1);
    }
}

juce::String SignedLicense::productIdOf (const juce::String& license)
{
    const auto fields = split (license);
    return fields.size() >= 2 ? fields[1] : juce::String();
}

SignedLicense::Check SignedLicense::check (const juce::String& license,
                                           const juce::String& signatureBase64,
                                           const juce::String& publicKeyHex,
                                           const juce::StringArray& acceptedProductIds,
                                           const juce::String& normalisedKey,
                                           const juce::String& machineId)
{
    if (! isSignedBy (license, signatureBase64, publicKeyHex))
        return Check::badSignature;

    const auto fields = split (license);

    if (fields.size() != numFields || fields[0] != formatTag)
        return Check::malformed;

    if (fields[1].isEmpty() || ! acceptedProductIds.contains (fields[1]))
        return Check::wrongProduct;

    if (normalisedKey.isEmpty() || fields[2] != normalisedKey)
        return Check::wrongKey;

    if (machineId.isEmpty() || fields[3] != machineId)
        return Check::wrongMachine;

    return Check::valid;
}

juce::String SignedLicense::describe (Check check)
{
    switch (check)
    {
        case Check::valid:          return "valid";
        case Check::badSignature:   return "not signed by the licence server";
        case Check::malformed:      return "not a licence this plug-in understands";
        case Check::wrongProduct:   return "for a product this plug-in doesn't accept";
        case Check::wrongKey:       return "for another key";
        case Check::wrongMachine:   return "for another computer";
    }

    return {};
}

} // namespace anymeans::license
