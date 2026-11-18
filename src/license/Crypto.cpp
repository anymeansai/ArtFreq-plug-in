#include "Crypto.h"

#include "Monocypher.h"

#include <algorithm>
#include <cstring>

namespace anymeans::license
{

//==============================================================================
namespace
{

constexpr std::uint64_t initialState[8] =
{
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

constexpr std::uint64_t roundConstants[80] =
{
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

constexpr std::uint64_t rotateRight (std::uint64_t x, int n) noexcept
{
    return (x >> n) | (x << (64 - n));
}

std::uint64_t readBigEndian64 (const std::uint8_t* p) noexcept
{
    std::uint64_t value = 0;

    for (int i = 0; i < 8; ++i)
        value = (value << 8) | p[i];

    return value;
}

void writeBigEndian64 (std::uint8_t* p, std::uint64_t value) noexcept
{
    for (int i = 7; i >= 0; --i)
    {
        p[i] = static_cast<std::uint8_t> (value & 0xff);
        value >>= 8;
    }
}

} // namespace

Sha512::Sha512() noexcept
{
    std::memcpy (state, initialState, sizeof (state));
}

void Sha512::processBlock (const std::uint8_t* block) noexcept
{
    std::uint64_t w[80];

    for (int t = 0; t < 16; ++t)
        w[t] = readBigEndian64 (block + 8 * t);

    for (int t = 16; t < 80; ++t)
    {
        const auto s0 = rotateRight (w[t - 15], 1) ^ rotateRight (w[t - 15], 8) ^ (w[t - 15] >> 7);
        const auto s1 = rotateRight (w[t - 2], 19) ^ rotateRight (w[t - 2], 61) ^ (w[t - 2] >> 6);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }

    auto a = state[0], b = state[1], c = state[2], d = state[3];
    auto e = state[4], f = state[5], g = state[6], h = state[7];

    for (int t = 0; t < 80; ++t)
    {
        const auto bigSigma1 = rotateRight (e, 14) ^ rotateRight (e, 18) ^ rotateRight (e, 41);
        const auto choose = (e & f) ^ (~e & g);
        const auto t1 = h + bigSigma1 + choose + roundConstants[t] + w[t];
        const auto bigSigma0 = rotateRight (a, 28) ^ rotateRight (a, 34) ^ rotateRight (a, 39);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = bigSigma0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;  state[1] += b;  state[2] += c;  state[3] += d;
    state[4] += e;  state[5] += f;  state[6] += g;  state[7] += h;
}

void Sha512::update (const void* data, size_t numBytes) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*> (data);
    totalBytes += numBytes;

    while (numBytes > 0)
    {
        const auto take = std::min (numBytes, sizeof (buffer) - bufferSize);
        std::memcpy (buffer + bufferSize, bytes, take);
        bufferSize += take;
        bytes += take;
        numBytes -= take;

        if (bufferSize == sizeof (buffer))
        {
            processBlock (buffer);
            bufferSize = 0;
        }
    }
}

Sha512::Digest Sha512::finish() noexcept
{
    // The message's length in bits, as the 128-bit big-endian number that ends the padding.
    const auto bitsHigh = totalBytes >> 61;
    const auto bitsLow = totalBytes << 3;

    // A 1 bit, then zeros up to 16 bytes before the end of a block (into a second block if the
    // length doesn't fit after the message's last bytes).
    buffer[bufferSize++] = 0x80;

    if (bufferSize > sizeof (buffer) - 16)
    {
        std::memset (buffer + bufferSize, 0, sizeof (buffer) - bufferSize);
        processBlock (buffer);
        bufferSize = 0;
    }

    std::memset (buffer + bufferSize, 0, sizeof (buffer) - 16 - bufferSize);
    writeBigEndian64 (buffer + sizeof (buffer) - 16, bitsHigh);
    writeBigEndian64 (buffer + sizeof (buffer) - 8, bitsLow);
    processBlock (buffer);
    bufferSize = 0;

    Digest digest;

    for (size_t i = 0; i < 8; ++i)
        writeBigEndian64 (digest.data() + 8 * i, state[i]);

    return digest;
}

Sha512::Digest Sha512::of (const void* data, size_t numBytes) noexcept
{
    Sha512 hash;
    hash.update (data, numBytes);
    return hash.finish();
}

//==============================================================================
bool verifyEd25519 (const std::uint8_t* signature64, const std::uint8_t* publicKey32,
                    const void* message, size_t messageSize) noexcept
{
    Sha512 hash;
    hash.update (signature64, 32);  // R
    hash.update (publicKey32, 32);  // A
    hash.update (message, messageSize);
    const auto expanded = hash.finish();

    std::uint8_t reduced[32];
    anymeans_monocypher::crypto_eddsa_reduce (reduced, expanded.data());

    return anymeans_monocypher::crypto_eddsa_check_equation (signature64, publicKey32, reduced) == 0;
}

//==============================================================================
namespace
{

int base64Value (juce::juce_wchar c) noexcept
{
    if (c >= 'A' && c <= 'Z')  return static_cast<int> (c - 'A');
    if (c >= 'a' && c <= 'z')  return static_cast<int> (c - 'a') + 26;
    if (c >= '0' && c <= '9')  return static_cast<int> (c - '0') + 52;
    if (c == '+')              return 62;
    if (c == '/')              return 63;
    return -1;
}

int hexValue (juce::juce_wchar c) noexcept
{
    if (c >= '0' && c <= '9')  return static_cast<int> (c - '0');
    if (c >= 'a' && c <= 'f')  return static_cast<int> (c - 'a') + 10;
    if (c >= 'A' && c <= 'F')  return static_cast<int> (c - 'A') + 10;
    return -1;
}

} // namespace

std::optional<juce::MemoryBlock> decodeBase64Strict (const juce::String& text)
{
    const auto length = text.length();

    if (length % 4 != 0)
        return std::nullopt;

    const auto padding = text.endsWith ("==") ? 2 : (text.endsWith ("=") ? 1 : 0);
    const auto numDataChars = length - padding;

    juce::MemoryBlock result;
    juce::uint32 accumulator = 0;
    int bitsHeld = 0;

    for (int i = 0; i < numDataChars; ++i)
    {
        const auto value = base64Value (text[i]);

        if (value < 0)
            return std::nullopt;  // also any '=' before the end

        accumulator = (accumulator << 6) | static_cast<juce::uint32> (value);
        bitsHeld += 6;

        if (bitsHeld >= 8)
        {
            bitsHeld -= 8;
            const auto byte = static_cast<juce::uint8> ((accumulator >> bitsHeld) & 0xff);
            result.append (&byte, 1);
        }
    }

    // What padding leaves over must be zero bits: otherwise two different texts would decode to
    // the same bytes.
    if ((accumulator & ((1u << bitsHeld) - 1u)) != 0)
        return std::nullopt;

    return result;
}

std::optional<juce::MemoryBlock> decodeHex (const juce::String& text)
{
    const auto length = text.length();

    if (length % 2 != 0)
        return std::nullopt;

    juce::MemoryBlock result;

    for (int i = 0; i < length; i += 2)
    {
        const auto high = hexValue (text[i]);
        const auto low = hexValue (text[i + 1]);

        if (high < 0 || low < 0)
            return std::nullopt;

        const auto byte = static_cast<juce::uint8> ((high << 4) | low);
        result.append (&byte, 1);
    }

    return result;
}

} // namespace anymeans::license
