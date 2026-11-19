#include "StandInSong.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace artfreq
{
namespace
{
    /** A repeatable value 0..1 for a pair of integers: the stand-in's grain. */
    float hash (int a, int b) noexcept
    {
        auto h = static_cast<uint32_t> (a) * 73856093u ^ static_cast<uint32_t> (b) * 19349663u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return static_cast<float> (h & 0xffffu) / 65535.0f;
    }

    /** Unit phasors in no order: the random phases of the noise, picked per frame and bin. */
    constexpr int numPhases = 1 << 14;

    const std::vector<std::complex<float>>& phases()
    {
        static const auto table = []
        {
            std::vector<std::complex<float>> t (static_cast<size_t> (numPhases));
            juce::Random random (0x5eed);

            for (auto& p : t)
                p = std::polar (1.0f, random.nextFloat() * juce::MathConstants<float>::twoPi);

            return t;
        }();

        return table;
    }

    inline uint32_t mix (int64_t frame, int bin) noexcept
    {
        auto h = static_cast<uint32_t> (frame) * 0x9e3779b1u ^ static_cast<uint32_t> (frame >> 32) * 0x85ebca6bu;
        h ^= static_cast<uint32_t> (bin) * 0xc2b2ae35u;
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        return h;
    }

    int64_t floorDiv (int64_t a, int64_t b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    /** What the synthesis below gives an analyser of its own frame size, against the level asked
        for: the median of the dB it reads over a long stretch of noise asked to be at 0 dB, in
        dB (measured with SpectrumAnalyser: see the test standInSongHasTheTexturesLevels). The
        frames are scaled by its inverse, so the analyser reads the texture's levels. */
    constexpr float synthesisGainDb = 2.2f;
} // namespace

float standInSongDb (double x, double y) noexcept
{
    // A spectrum that falls towards the highs, as a mix's does on a log scale.
    auto db = -22.0 - 18.0 * y;

    // Sixteen beats across the length: a kick in the lows on the beat, a hat in the highs on the
    // off-beat, each decaying.
    const auto beats = x * 16.0;
    const auto onBeat = beats - std::floor (beats);
    const auto offBeat = beats + 0.5 - std::floor (beats + 0.5);

    if (y < 0.35)
        db += 9.0 * std::exp (-7.0 * onBeat) * (1.0 - y / 0.35);

    if (y > 0.6)
        db += 7.0 * std::exp (-18.0 * offBeat) * (y - 0.6) / 0.4;

    // Held notes: five partials, a new chord every four beats.
    const auto chord = static_cast<int> (std::floor (beats / 4.0));

    for (int k = 0; k < 5; ++k)
    {
        const auto centre = 0.30 + 0.06 * k + 0.03 * static_cast<double> (hash (chord, k));
        const auto distance = std::abs (y - centre);

        if (distance < 0.005)
            db += 8.0 * (1.0 - distance / 0.005);
    }

    // Grain, smooth over a grid of the canvas (so it does not shimmer when the window resizes).
    const auto gx = x * 192.0, gy = y * 128.0;
    const auto ix = static_cast<int> (std::floor (gx)), iy = static_cast<int> (std::floor (gy));
    const auto fx = static_cast<float> (gx - ix), fy = static_cast<float> (gy - iy);
    const auto top = hash (ix, iy + 1) + (hash (ix + 1, iy + 1) - hash (ix, iy + 1)) * fx;
    const auto bottom = hash (ix, iy) + (hash (ix + 1, iy) - hash (ix, iy)) * fx;
    db += 8.0 * (static_cast<double> (bottom + (top - bottom) * fy) - 0.5);

    return static_cast<float> (std::min (0.0, db));
}

const std::vector<float>& standInSongTexture()
{
    static const auto texture = []
    {
        std::vector<float> t (static_cast<size_t> (standInTextureWidth) * static_cast<size_t> (standInTextureHeight));

        for (int y = 0; y < standInTextureHeight; ++y)
        {
            const auto up = 1.0 - (y + 0.5) / standInTextureHeight;

            for (int x = 0; x < standInTextureWidth; ++x)
                t[static_cast<size_t> (y) * static_cast<size_t> (standInTextureWidth) + static_cast<size_t> (x)]
                    = standInSongDb ((x + 0.5) / standInTextureWidth, up);
        }

        return t;
    }();

    return texture;
}

float standInSongTextureDb (double x, double y) noexcept
{
    const auto& t = standInSongTexture();
    const auto tx = (x - std::floor (x)) * standInTextureWidth - 0.5;
    const auto ty = juce::jlimit (0.0, static_cast<double> (standInTextureHeight - 1), (1.0 - y) * standInTextureHeight - 0.5);
    const auto x0 = static_cast<int> (std::floor (tx));
    const auto y0 = juce::jmin (standInTextureHeight - 2, static_cast<int> (ty));
    const auto fx = static_cast<float> (tx - x0), fy = static_cast<float> (ty - y0);
    const auto c0 = static_cast<size_t> ((x0 + standInTextureWidth) % standInTextureWidth);
    const auto c1 = static_cast<size_t> ((x0 + 1 + standInTextureWidth) % standInTextureWidth);
    const auto* above = t.data() + static_cast<size_t> (y0) * standInTextureWidth;
    const auto* below = above + standInTextureWidth;
    const auto upper = above[c0] + (above[c1] - above[c0]) * fx;
    const auto lower = below[c0] + (below[c1] - below[c0]) * fx;
    return upper + (lower - upper) * fy;
}

//==============================================================================
int StandInSong::frameSizeFor (double sampleRate) noexcept
{
    const auto wanted = juce::jlimit (512, 32768, juce::roundToInt (4096.0 * sampleRate / 48000.0));
    return juce::nextPowerOfTwo (wanted);
}

StandInSong::StandInSong (const FrequencyScale& scale, double sampleRate, int64_t passSamplesToUse, int64_t firstSample)
    : frameSize (frameSizeFor (sampleRate)),
      hop (frameSize / 4),
      passSamples (std::max (static_cast<int64_t> (1), passSamplesToUse))
{
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (frameSize)));
    window.resize (static_cast<size_t> (frameSize));
    spectrum.assign (static_cast<size_t> (frameSize) * 2, 0.0f);
    ola.assign (static_cast<size_t> (frameSize), 0.0f);

    for (int n = 0; n < frameSize; ++n)
        window[static_cast<size_t> (n)] = static_cast<float> (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / frameSize));

    // Each bin's height on the scale, as a texture row (frequencies outside the scale's range
    // take its edge's level).
    const auto s = scale.sanitised();
    const auto numBins = frameSize / 2 + 1;
    binRow.resize (static_cast<size_t> (numBins));
    binFraction.resize (static_cast<size_t> (numBins));

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto hz = std::max (1.0, bin * sampleRate / frameSize);
        const auto y = juce::jlimit (0.0, 1.0, s.toNorm (hz));
        const auto ty = juce::jlimit (0.0, static_cast<double> (standInTextureHeight - 1), (1.0 - y) * standInTextureHeight - 0.5);
        const auto row = juce::jmin (standInTextureHeight - 2, static_cast<int> (ty));
        binRow[static_cast<size_t> (bin)] = row;
        binFraction[static_cast<size_t> (bin)] = static_cast<float> (ty - row);
    }

    // A bin asked for amplitude A is a sine of amplitude A over the frame: X = A N / 2 (the real
    // inverse transform divides by N); the overlap of four windowed frames, with independent
    // phases, then reads synthesisGainDb above A on an analyser.
    amplitudeScale = static_cast<float> (frameSize) * 0.5f * juce::Decibels::decibelsToGain (-synthesisGainDb);

    // Frame k covers [k hop, k hop + frameSize). The samples from `firstSample` on are complete
    // once every frame over them is in: start three frames before the hop it falls in.
    const auto firstHop = floorDiv (firstSample, hop);
    nextFrame = firstHop - (frameSize / hop - 1);
    olaStart = nextFrame * hop;
    position = olaStart;

    std::vector<float> discard (static_cast<size_t> (frameSize));

    while (position < firstSample)
        render (discard.data(), static_cast<int> (std::min (static_cast<int64_t> (frameSize), firstSample - position)));
}

void StandInSong::addFrame() noexcept
{
    // The frame's place on the canvas: its centre, along the pass (the texture repeats).
    const auto centre = static_cast<double> (nextFrame * hop + frameSize / 2);
    const auto x = centre / static_cast<double> (passSamples);
    const auto tx = (x - std::floor (x)) * standInTextureWidth - 0.5;
    const auto x0 = static_cast<int> (std::floor (tx));
    const auto fx = static_cast<float> (tx - x0);
    const auto c0 = static_cast<size_t> ((x0 + standInTextureWidth) % standInTextureWidth);
    const auto c1 = static_cast<size_t> ((x0 + 1 + standInTextureWidth) % standInTextureWidth);
    const auto& texture = standInSongTexture();
    const auto& phase = phases();
    const auto numBins = frameSize / 2 + 1;

    constexpr float nepersPerDb = 0.11512925f;   // ln (10) / 20

    std::fill (spectrum.begin(), spectrum.end(), 0.0f);

    for (int bin = 1; bin < numBins - 1; ++bin)
    {
        const auto* above = texture.data() + static_cast<size_t> (binRow[static_cast<size_t> (bin)]) * standInTextureWidth;
        const auto* below = above + standInTextureWidth;
        const auto upper = above[c0] + (above[c1] - above[c0]) * fx;
        const auto lower = below[c0] + (below[c1] - below[c0]) * fx;
        const auto db = upper + (lower - upper) * binFraction[static_cast<size_t> (bin)];
        const auto amplitude = std::exp (db * nepersPerDb) * amplitudeScale;
        const auto p = phase[mix (nextFrame, bin) & (numPhases - 1)];
        spectrum[static_cast<size_t> (2 * bin)] = amplitude * p.real();
        spectrum[static_cast<size_t> (2 * bin + 1)] = amplitude * p.imag();
    }

    fft->performRealOnlyInverseTransform (spectrum.data());

    for (int n = 0; n < frameSize; ++n)
        ola[static_cast<size_t> (n)] += spectrum[static_cast<size_t> (n)] * window[static_cast<size_t> (n)];

    ++nextFrame;
}

void StandInSong::render (float* out, int numSamples) noexcept
{
    for (int i = 0; i < numSamples;)
    {
        // ola holds [olaStart, olaStart + frameSize); its first hop is complete once the frame
        // starting there is in.
        if (nextFrame * hop <= olaStart)
            addFrame();

        const auto offset = static_cast<int> (position - olaStart);
        const auto take = std::min (numSamples - i, hop - offset);
        std::copy (ola.begin() + offset, ola.begin() + offset + take, out + i);
        i += take;
        position += take;

        if (position - olaStart >= hop)
        {
            // Move on by a hop.
            std::copy (ola.begin() + hop, ola.end(), ola.begin());
            std::fill (ola.end() - hop, ola.end(), 0.0f);
            olaStart += hop;
        }
    }
}

} // namespace artfreq
