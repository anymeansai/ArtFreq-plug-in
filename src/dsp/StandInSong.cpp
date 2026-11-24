#include "StandInSong.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iterator>

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
        frames are scaled by its inverse, so the analyser reads the texture's levels. (2.2 dB
        with the Hann window of 0.6; the Blackman-Harris window reads noise 1.25 dB higher.) */
    constexpr float synthesisGainDb = 3.45f;

    /** The lows' noise bed is this much quieter at 60 Hz and below, and the tones' levels (peak
        amplitudes): together they give the lows the test master's levels on an analyser of 8192
        points (median about -33 dB a cell, the brightest 5 % about -15 dB). */
    constexpr double lowsBedDipDb = 14.0;
    constexpr double kickLevel = 0.55;
    constexpr double bassLevel = 0.28;

    /** The bass line: a root per four beats, a note per half beat. */
    constexpr double bassRoots[] { 55.0, 43.65, 65.41, 49.0 };
    constexpr double bassPattern[] { 1.0, 1.0, 2.0, 1.0, 1.0, 1.5, 2.0, 1.0 };
    constexpr int bassHarmonics = 6;
} // namespace

namespace
{
    /** How far below 250 Hz a frequency is, 0 at 250 Hz to 1 at 60 Hz and below. */
    double lowsShare (double hz) noexcept
    {
        return juce::jlimit (0.0, 1.0, std::log2 (250.0 / std::max (1.0, hz)) / std::log2 (250.0 / 60.0));
    }

    /** The song without its tones (see standInSongDb). */
    float songBaseDb (double x, double hz) noexcept;
} // namespace

float standInSongDb (double x, double hz) noexcept
{
    // The sound's lows are its tones over a quieter noise (bedDb): they brighten 60 Hz - 1 kHz (the
    // kick, the bass and its harmonics) and leave the lowest octave darker. On an analyser of 4096
    // points the median differs from the noise's level by about this much (measured with
    // standInSongHasTheTexturesLevels), which the song's look follows.
    struct Point { double hz, db; };
    static constexpr Point tones[] { { 25.0, -10.0 }, { 45.0, -6.0 }, { 85.0, 6.0 }, { 175.0, 3.0 },
                                     { 350.0, 2.3 }, { 700.0, 0.5 }, { 1400.0, 0.0 } };
    auto offset = 0.0;

    if (hz <= tones[0].hz)
        offset = tones[0].db;
    else
        for (size_t i = 1; i < std::size (tones); ++i)
            if (hz < tones[i].hz)
            {
                const auto t = std::log2 (hz / tones[i - 1].hz) / std::log2 (tones[i].hz / tones[i - 1].hz);
                offset = tones[i - 1].db + (tones[i].db - tones[i - 1].db) * t;
                break;
            }

    return std::min (0.0f, songBaseDb (x, hz) + static_cast<float> (offset));
}

namespace
{
float songBaseDb (double x, double hz) noexcept
{
    hz = std::max (1.0, hz);
    const auto octaves = std::log2 (hz / 70.0);   // from the lows

    // A master's tilt: 5 dB an octave in an analyser's cells, rolled off below 30 Hz and above
    // 16 kHz (12 dB an octave).
    auto db = -27.0 - 5.0 * octaves;

    if (hz < 30.0)
        db -= 12.0 * std::log2 (30.0 / hz);

    if (hz > 16000.0)
        db -= 12.0 * std::log2 (hz / 16000.0);


    // Sixteen beats across the length: a kick in the lows on the beat (below 224 Hz, strongest at
    // the bottom), a hat in the highs on the off-beat (above 1.26 kHz), each decaying.
    const auto beats = x * 16.0;
    const auto onBeat = beats - std::floor (beats);
    const auto offBeat = beats + 0.5 - std::floor (beats + 0.5);

    if (hz < 224.0)
        db += 9.0 * std::exp (-7.0 * onBeat) * std::min (1.0, std::log2 (224.0 / hz) / std::log2 (224.0 / 20.0));

    if (hz > 1260.0)
        db += 7.0 * std::exp (-18.0 * offBeat) * std::min (1.0, std::log2 (hz / 1260.0) / std::log2 (20000.0 / 1260.0));

    // Held notes: five partials between about 160 Hz and 1 kHz, a new chord every four beats,
    // each a twentieth of an octave wide.
    const auto chord = static_cast<int> (std::floor (beats / 4.0));
    const auto position = std::log2 (hz / 20.0) / std::log2 (1000.0);   // where 20 Hz - 20 kHz is 0..1

    for (int k = 0; k < 5; ++k)
    {
        const auto centre = 0.30 + 0.06 * k + 0.03 * static_cast<double> (hash (chord, k));
        const auto distance = std::abs (position - centre);

        if (distance < 0.005)
            db += 8.0 * (1.0 - distance / 0.005);
    }

    // Grain, smooth over a grid (fixed in time and log frequency, so it does not shimmer when the
    // window resizes).
    const auto gx = x * 192.0, gy = position * 128.0;
    const auto ix = static_cast<int> (std::floor (gx)), iy = static_cast<int> (std::floor (gy));
    const auto fx = static_cast<float> (gx - ix), fy = static_cast<float> (gy - iy);
    const auto top = hash (ix, iy + 1) + (hash (ix + 1, iy + 1) - hash (ix, iy + 1)) * fx;
    const auto bottom = hash (ix, iy) + (hash (ix + 1, iy) - hash (ix, iy)) * fx;
    db += 8.0 * (static_cast<double> (bottom + (top - bottom) * fy) - 0.5);

    return static_cast<float> (std::min (0.0, db));
}
} // namespace

namespace
{
    /** The noise under the sound's kick and bass line: quieter in the lows, where the tones carry
        the brightest lines (the song looks as standInSongDb says: the tones make up the rest, on
        an analyser of 4096 points). */
    float bedDb (double x, double hz) noexcept
    {
        return songBaseDb (x, hz) - static_cast<float> (lowsBedDipDb * lowsShare (hz));
    }

    std::vector<float> makeTexture (float (*levelAt) (double, double))
    {
        std::vector<float> t (static_cast<size_t> (standInTextureWidth) * static_cast<size_t> (standInTextureHeight));
        const auto span = std::log2 (standInTextureTopHz / standInTextureBottomHz);

        for (int y = 0; y < standInTextureHeight; ++y)
        {
            const auto up = 1.0 - (y + 0.5) / standInTextureHeight;
            const auto hz = standInTextureBottomHz * std::pow (2.0, up * span);

            for (int x = 0; x < standInTextureWidth; ++x)
                t[static_cast<size_t> (y) * static_cast<size_t> (standInTextureWidth) + static_cast<size_t> (x)]
                    = levelAt ((x + 0.5) / standInTextureWidth, hz);
        }

        return t;
    }

    /** bedDb tabulated as standInSongTexture is: what the noise is made from. */
    const std::vector<float>& bedTexture()
    {
        static const auto texture = makeTexture (bedDb);
        return texture;
    }
} // namespace

double standInTextureRow (double hz) noexcept
{
    const auto span = std::log2 (standInTextureTopHz / standInTextureBottomHz);
    const auto up = std::log2 (std::max (standInTextureBottomHz, std::min (standInTextureTopHz, hz)) / standInTextureBottomHz) / span;
    return juce::jlimit (0.0, static_cast<double> (standInTextureHeight - 1), (1.0 - up) * standInTextureHeight - 0.5);
}

const std::vector<float>& standInSongTexture()
{
    static const auto texture = makeTexture (standInSongDb);
    return texture;
}

float standInSongTextureDb (double x, double hz) noexcept
{
    const auto& t = standInSongTexture();
    const auto tx = (x - std::floor (x)) * standInTextureWidth - 0.5;
    const auto ty = standInTextureRow (hz);
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

StandInSong::StandInSong (double sampleRateToUse, int64_t passSamplesToUse, int64_t firstSample)
    : frameSize (frameSizeFor (sampleRateToUse)),
      hop (frameSize / 4),
      passSamples (std::max (static_cast<int64_t> (1), passSamplesToUse)),
      sampleRate (sampleRateToUse),
      beatSamples (static_cast<double> (std::max (static_cast<int64_t> (16), passSamplesToUse)) / 16.0)
{

    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (frameSize)));
    window.resize (static_cast<size_t> (frameSize));
    spectrum.assign (static_cast<size_t> (frameSize) * 2, 0.0f);
    ola.assign (static_cast<size_t> (frameSize), 0.0f);

    for (int n = 0; n < frameSize; ++n)
        window[static_cast<size_t> (n)] = static_cast<float> (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / frameSize));

    // Each bin's texture row.
    const auto numBins = frameSize / 2 + 1;
    binRow.resize (static_cast<size_t> (numBins));
    binFraction.resize (static_cast<size_t> (numBins));

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto ty = standInTextureRow (std::max (1.0, bin * sampleRate / frameSize));
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
    tonalPosition = olaStart;

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
    const auto& texture = bedTexture();
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

void StandInSong::addLows (float* out, int numSamples) noexcept
{
    const auto twoPi = juce::MathConstants<double>::twoPi;
    const auto dt = 1.0 / sampleRate;

    for (int i = 0; i < numSamples; ++i, ++tonalPosition)
    {
        const auto t = static_cast<double> (tonalPosition);
        double value = 0.0;

        // The kick: a tone falling from 168 to 48 Hz, on every beat.
        const auto beat = static_cast<int64_t> (std::floor (t / beatSamples));
        const auto intoBeat = (t - static_cast<double> (beat) * beatSamples) * dt;

        if (beat != kickBeat)
        {
            // Started here (the beat's first sample, or the middle of the beat when render()
            // starts there): its phase and envelopes as the steps below would have made them from
            // the beat's first sample, summed in closed form, so any start gives the same sound.
            kickBeat = beat;
            const auto beatStart = static_cast<double> (beat) * beatSamples;
            const auto firstSample = std::ceil (beatStart);
            const auto steps = t - firstSample;              // samples of this beat before this one
            const auto lead = (firstSample - beatStart) * dt; // the first sample's time into the beat
            const auto ratio = std::exp (-dt / 0.035);
            const auto atFirst = std::exp (-lead / 0.035);
            const auto sum = steps > 0.0 ? atFirst * (1.0 - std::pow (ratio, steps)) / (1.0 - ratio) : 0.0;
            kickPhase = twoPi * dt * (48.0 * steps + 120.0 * sum);
            kickSeconds = intoBeat;
            kickFall = std::exp (-intoBeat / 0.035);
            kickDecay = std::exp (-intoBeat / 0.22);
        }

        if (kickSeconds < 0.45)
        {
            const auto attack = std::min (1.0, kickSeconds * sampleRate / 16.0);
            value += kickLevel * attack * kickDecay * std::sin (kickPhase);
        }

        kickPhase += twoPi * (48.0 + 120.0 * kickFall) * dt;
        kickFall *= std::exp (-dt / 0.035);
        kickDecay *= std::exp (-dt / 0.22);
        kickSeconds += dt;

        // The bass line: a note every half beat, six harmonics falling as a filtered saw's do.
        const auto halfBeat = beatSamples * 0.5;
        const auto note = static_cast<int64_t> (std::floor (t / halfBeat));
        const auto intoNote = (t - static_cast<double> (note) * halfBeat) * dt;

        if (note != bassNote)
        {
            bassNote = note;
            const auto wrapped = static_cast<size_t> (((note % 32) + 32) % 32);
            const auto hz = bassRoots[wrapped / 8] * bassPattern[wrapped % 8];
            bassSeconds = intoNote;
            bassNoteSeconds = halfBeat * dt;
            bassRe = std::cos (twoPi * hz * intoNote);
            bassIm = std::sin (twoPi * hz * intoNote);
            bassStepRe = std::cos (twoPi * hz * dt);
            bassStepIm = std::sin (twoPi * hz * dt);
        }

        const auto envelope = std::min (1.0, bassSeconds / 0.003) * (0.6 + 0.4 * std::exp (-bassSeconds / 0.08))
                            * juce::jlimit (0.0, 1.0, (bassNoteSeconds - bassSeconds) / 0.02);

        if (envelope > 0.0)
        {
            double re = bassRe, im = bassIm, saw = 0.0;

            for (int k = 1; k <= bassHarmonics; ++k)
            {
                saw += im / k;
                const auto nextRe = re * bassRe - im * bassIm;
                im = re * bassIm + im * bassRe;
                re = nextRe;
            }

            value += bassLevel * envelope * saw;
        }

        const auto nextRe = bassRe * bassStepRe - bassIm * bassStepIm;
        bassIm = bassRe * bassStepIm + bassIm * bassStepRe;
        bassRe = nextRe;
        bassSeconds += dt;

        out[i] += static_cast<float> (value);
    }
}

void StandInSong::render (float* out, int numSamples) noexcept
{
    auto* const start = out;
    const auto total = numSamples;

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

    addLows (start, total);
}

} // namespace artfreq
