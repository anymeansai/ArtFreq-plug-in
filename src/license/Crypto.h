#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace anymeans::license
{

/** SHA-512 (FIPS 180-4). Ed25519 hashes with it and JUCE has only SHA-256, so here is a small,
    plain one: correctness over speed, it hashes a few hundred bytes per check.
*/
class Sha512
{
public:
    static constexpr size_t digestSize = 64;
    using Digest = std::array<std::uint8_t, digestSize>;

    Sha512() noexcept;

    /** Adds bytes to the message. May be called any number of times; data may be null when numBytes is 0. */
    void update (const void* data, size_t numBytes) noexcept;

    /** Pads the message and returns its digest. Call once; the object is spent afterwards. */
    Digest finish() noexcept;

    static Digest of (const void* data, size_t numBytes) noexcept;

private:
    void processBlock (const std::uint8_t* block) noexcept;

    std::uint64_t state[8];
    std::uint8_t buffer[128] = {};
    size_t bufferSize = 0;
    std::uint64_t totalBytes = 0;
};

/** Standard Ed25519 (RFC 8032) signature check, as WebCrypto signs on the server: the 64-byte
    signature R || S, the 32-byte public key A, any message. Built from Monocypher's own pieces the
    way its optional monocypher-ed25519.c does it (Monocypher's core EdDSA hashes with BLAKE2b):
    h = SHA-512 (R || A || M) reduced modulo L, then Monocypher's check of [S]B = R + [h]A, which
    also rejects S >= L (so a signature can't be altered into another valid one).
*/
bool verifyEd25519 (const std::uint8_t* signature64, const std::uint8_t* publicKey32,
                    const void* message, size_t messageSize) noexcept;

/** Standard base64 (RFC 4648, + and /, padded) in its one canonical form: whitespace, missing or
    extra padding, characters outside the alphabet and stray bits in the last character are all
    refused. Returns nothing if the text isn't exactly that.
*/
std::optional<juce::MemoryBlock> decodeBase64Strict (const juce::String& text);

/** Hex, upper or lower case, an even number of digits and nothing else. */
std::optional<juce::MemoryBlock> decodeHex (const juce::String& text);

} // namespace anymeans::license
