#include "SongCapture.h"

#include <algorithm>
#include <cmath>

namespace artfreq
{
namespace
{
    int64_t positiveModulo (int64_t value, int64_t divisor) noexcept
    {
        const auto r = value % divisor;
        return r < 0 ? r + divisor : r;
    }

    /** Two passes' lengths that a host's tempo wobbling in its last digits gives are the same. */
    bool samePass (int64_t a, int64_t b) noexcept
    {
        return std::abs (a - b) <= std::max (int64_t (2), std::max (a, b) / 10000);
    }
} // namespace

//==============================================================================
CapturedSong::Reader::Reader (const CapturedSong& songToRead, int64_t firstSample) noexcept
    : song (songToRead), position (firstSample)
{
    const auto length = song.length();
    fade = std::min (static_cast<int64_t> (std::llround (0.010 * song.sampleRate)), length / 4);
    loopLength = std::max (int64_t (1), length - fade);
}

float CapturedSong::Reader::at (int64_t u) const noexcept
{
    const auto* take = song.samples.data();

    if (u >= fade)
        return static_cast<float> (take[static_cast<size_t> (u)]);

    // The loop's seam: its start fades in over the end of the take (equal power: the two are not
    // the same sound).
    const auto w = (static_cast<double> (u) + 0.5) / static_cast<double> (fade) * juce::MathConstants<double>::halfPi;
    return static_cast<float> (take[static_cast<size_t> (u)] * std::sin (w)
                               + take[static_cast<size_t> (loopLength + u)] * std::cos (w));
}

void CapturedSong::Reader::render (float* out, int numSamples) noexcept
{
    if (song.samples.empty())
    {
        std::fill_n (out, numSamples, 0.0f);
        position += numSamples;
        return;
    }

    constexpr auto toFloat = 1.0f / SongCapture::sampleScale;

    for (int i = 0; i < numSamples; ++i, ++position)
        out[i] = at (positiveModulo (position - song.start, loopLength)) * toFloat;
}

//==============================================================================
SongCapture::SongCapture()
{
    for (auto& take : takes)
        take.samples.assign (static_cast<size_t> (maxSamples), 0);
}

void SongCapture::startTake (int64_t position, int64_t passSamples, double sampleRate) noexcept
{
    auto& take = takes[recording];
    take.start = position;
    take.length = 0;
    take.passSamples = passSamples;
    take.sampleRate = sampleRate;
    take.energy.fill (0.0f);
    inTake = true;
}

void SongCapture::process (const float* const* channels, int numChannels, int numSamples,
                           const PlaySegment* segments, int numSegments, double sampleRate) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;

    // The fingerprint's split between lows and the rest: a one-pole low-pass at about 250 Hz.
    const auto split = static_cast<float> (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 250.0 / std::max (8000.0, sampleRate)));
    const auto toChannelMean = 1.0f / static_cast<float> (numChannels);

    for (int s = 0; s < numSegments; ++s)
    {
        const auto& segment = segments[s];

        if (! segment.playing || ! (segment.fractionPerSample > 0.0) || segment.length <= 0)
        {
            endTake();
            continue;
        }

        const auto pass = static_cast<int64_t> (std::llround (1.0 / segment.fractionPerSample));

        if (pass < 2)
        {
            endTake();
            continue;
        }

        auto position = positiveModulo (static_cast<int64_t> (std::llround (segment.fraction * static_cast<double> (pass))), pass);

        // A take is the input from one place in the pass on, without a break: a jump, another
        // length or a position that does not follow on ends it.
        if (inTake)
        {
            const auto& take = takes[recording];
            const auto expected = take.start + take.length + skipped;

            if (segment.jump || ! samePass (pass, take.passSamples) || std::abs (position - expected) > 2)
                endTake();
            else
                position = expected;   // rounding: the take stays one run of samples
        }

        if (! inTake)
        {
            startTake (position, pass, sampleRate);
            skipped = 0;
        }

        for (int i = 0; i < segment.length; ++i)
        {
            auto& take = takes[recording];

            // The pass ends: the take with it, and the next pass's starts.
            if (take.start + take.length + skipped >= take.passSamples)
            {
                endTake();
                startTake (0, pass, sampleRate);
                skipped = 0;
            }

            auto& current = takes[recording];
            const auto index = segment.start + i;
            auto x = channels[0][index];

            for (int channel = 1; channel < numChannels; ++channel)
                x += channels[channel][index];

            x *= toChannelMean;

            if (current.length < maxSamples)
            {
                current.samples[static_cast<size_t> (current.length)]
                    = static_cast<int16_t> (juce::jlimit (-32767.0f, 32767.0f, std::round (x * sampleScale)));

                splitState += split * (x - splitState);
                const auto low = splitState, high = x - splitState;
                const auto slot = static_cast<size_t> (juce::jlimit (int64_t (0), int64_t (numSlots - 1),
                                                                     (current.start + current.length) * numSlots / std::max (int64_t (1), current.passSamples)));
                current.energy[slot * 2] += low * low;
                current.energy[slot * 2 + 1] += high * high;
                ++current.length;
            }
            else
            {
                ++skipped;   // past what a take holds: only the position is followed
            }
        }
    }
}

bool SongCapture::isWorthPublishing (const Take& take, const Take& published) const noexcept
{
    const auto pass = take.passSamples;

    // At least a second (or the whole of a shorter pass): less says little about the song.
    if (take.length < std::min (std::min (pass, maxSamples), static_cast<int64_t> (take.sampleRate)))
        return false;

    const auto compatible = published.length > 0 && samePass (published.passSamples, pass)
                         && juce::exactlyEqual (published.sampleRate, take.sampleRate);

    if (! compatible)
        return true;

    // A part of a pass does not replace a fuller take of it (playing a moment of the song again).
    const auto half = std::min (pass, maxSamples) / 2;

    if (take.length < published.length && take.length < half)
        return false;

    // The same audio again (a DAW looping the same bars): nothing new to show.
    if (std::abs (take.start - published.start) > 2 || std::abs (take.length - published.length) > 2)
        return true;

    const auto floorEnergy = 1.0e-6f * static_cast<float> (std::max (int64_t (1), pass / numSlots));

    for (size_t i = 0; i < take.energy.size(); ++i)
    {
        const auto a = take.energy[i] + floorEnergy, b = published.energy[i] + floorEnergy;

        if (std::abs (10.0f * std::log10 (a / b)) > 1.5f)
            return true;
    }

    return false;
}

void SongCapture::endTake() noexcept
{
    if (! inTake)
        return;

    inTake = false;
    auto& take = takes[recording];

    if (! isWorthPublishing (take, takes[1 - recording]))
        return;

    // Swap, unless a reader is copying the published take (then this take is dropped: the next
    // pass's end tries again).
    auto s = state.load (std::memory_order_acquire);

    if ((s & readingBit) != 0 || static_cast<int> (s & 1u) != 1 - recording)
        return;

    take.serial = nextSerial;

    if (state.compare_exchange_strong (s, s ^ 1u, std::memory_order_acq_rel))
    {
        publishedSerial.store (nextSerial, std::memory_order_release);
        ++nextSerial;
        recording = 1 - recording;
    }
}

std::shared_ptr<const CapturedSong> SongCapture::getLatest() const
{
    const std::lock_guard<std::mutex> lock (readers);

    if (publishedSerial.load (std::memory_order_acquire) == 0)
        return nullptr;

    if (auto copy = lastCopy.lock(); copy != nullptr && copy->serial == publishedSerial.load (std::memory_order_acquire))
        return copy;

    // While the bit is set the audio thread does not swap: the published take stays as it is.
    const auto s = state.fetch_or (readingBit, std::memory_order_acq_rel);
    const auto& take = takes[s & 1u];
    auto song = std::make_shared<CapturedSong>();

    if (take.length > 0)
    {
        song->samples.assign (take.samples.begin(), take.samples.begin() + static_cast<std::ptrdiff_t> (take.length));
        song->start = take.start;
        song->passSamples = take.passSamples;
        song->sampleRate = take.sampleRate;
        song->serial = take.serial;
    }

    state.fetch_and (~readingBit, std::memory_order_release);

    if (song->samples.empty())
        return nullptr;

    lastCopy = song;
    return song;
}

} // namespace artfreq
