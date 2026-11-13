// The carving lab: what a spectral carving engine (dsp/SpectralCarver.h) does to a song, as an
// analyser shows it. It measures the engines so they can be compared on equal terms.
//
//   FreqArtCarveLab <output-dir> [--engine N] [--input song.wav] [--offset seconds]
//                   [--songs pink,mix,input] [--drawings hello,sun,ramp,portrait]
//                   [--modes carve,stencil] [--ffts 4096,8192,16384] [--depth -60]
//                   [--quick] [--no-png] [--wav]
//
// Songs (48 kHz, stereo, 12 s, deterministic): "pink", pink noise; "mix", a busy synthetic mix
// (kick on every beat, snare, a bass line, chords, hats and a noise bed at 120 BPM); "input",
// the song given with --input (from --offset seconds; mono files are doubled, other rates
// resampled).
//
// Drawings, through the real ArtDocument and Compositor on the log 20 Hz - 20 kHz scale:
// "hello", HELLO across the Mid and High bands (300 Hz - 16 kHz); "sun", a filled disc with
// rays over 60 Hz - 1.2 kHz; "ramp", a picture fading from silent (bottom) to full (top) over
// 30 Hz - 16 kHz; "portrait", a synthesised photo-like face over 40 Hz - 8 kHz.
//
// Each drawing plays one 8 s pass (4 bars at 120 BPM, Transport, from bar 2: 2 s to 10 s of the
// song) through the PlayheadTracker, which drives the engine (--engine N, 0 = baseline) in
// Carve and Stencil at --depth dB (-60), in blocks of 512 and again in random blocks of 1 to
// 2048 (which must give the same output).
//
// The input and the output (moved back by the engine's latency) are analysed like MiniMeters
// does: Hann STFTs of 4096, 8192 and 16384 points, a hop of an eighth of that, in dB, shown on a
// log display of 600 rows from 20 Hz to 20 kHz (linear in dB between bin centres). The drawing
// is resampled onto the same cells: each cell inside the pass should drop by a share of |depth|,
// the drawing's value in Carve, one minus it in Stencil. "Target" cells should drop by at least
// half of it, "touched" cells by more than 2 % of it, "keep" cells (the rest) not at all. Per
// case, analyser and range (30-250 Hz, 250 Hz - 2 kHz, 2 - 16 kHz, and all of them) the lab
// reports (attenuation = input dB - output dB in a cell):
//
//   depth    median attenuation of the target cells
//   vis      visibility: how much darker the target cells are than the keep cells within 4
//            cells of the drawing, in the output, minus the same in the input
//   r        fidelity: Pearson r between the attenuation (clipped to 0..|depth|) and the share
//            over the pass
//   err      median |attenuation - share x |depth|| over the touched cells (how well partial
//            values are followed: the ramp, the portrait)
//   edgeT    10-90 % width of the attenuation across the drawing's edges in time, ms (the
//            median profile of all edges with 8 steady cells on either side)
//   edgeF    the same across edges in frequency, in display rows (16 steady rows either side)
//   near     median attenuation of keep cells within 2 cells of a touched cell (ideal 0)
//   far      ... of keep cells more than 8 cells away
//   pre      ... of keep cells 1 to 3 columns before a touched cell in time (pre-echo)
//   post     ... 1 to 3 columns after one
//   noise    standard deviation of (output dB - input dB) over the far keep cells (musical noise)
//   spread   interquartile range of the attenuation over the target cells (an uneven carve)
//
// and per case: "blocks", the largest difference between the two block patterns; "outside",
// the largest |output - input| outside the pass (by more than twice the latency); "lead" and
// "lag", in Stencil, how long before the pass starts the output first differs from the input
// (by more than -80 dBFS) and how long after it ends it last does, ms; "click", the pre-echo
// of a transient: a train of clicks with nothing else goes through the engine too, and this is
// the energy in the 25 ms before each click relative to the 25 ms from it on, dB (median over
// the clicks where the drawing is; -120 = none); "kick", for the mix (and a song with kicks
// found), the median change (output vs input, dB) of the energy just before each kick onset
// relative to just after it, over the kicks where the drawing is (> 0 = energy pushed ahead of
// the onset, but also the carve reshaping the sound around it). Per engine: transparency with
// an empty drawing, the cost (CPU time per second of stereo audio, blocks of 512; Carve skips
// the moments with nothing drawn) and the latency.
//
// It writes metrics.txt (also printed), metrics.json, canvas-<drawing>.png and, per case and
// analyser, <song>-<drawing>-<mode>-<fft>.png: the drawing on the analyser's grid, the input,
// the output and the attenuation (0 to |depth| dB), time across, 20 Hz at the bottom. With --wav
// also <song>.wav and <song>-<drawing>-<mode>.wav (the output moved back by the latency), 32-bit
// float, to listen to.
//
// On Linux run it under a virtual display (it renders text):  xvfb-run -a FreqArtCarveLab out

#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Fonts.h"
#include "core/ImageProcessing.h"
#include "core/MaskPlan.h"
#include "dsp/PlayheadTracker.h"
#include "dsp/SpectralCarver.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

using namespace freqart;

constexpr double sampleRate = 48000.0;
constexpr double tempo = 120.0;
constexpr int startBar = 2;
constexpr double passStart = 2.0;     // bar 2 at 120 BPM in 4/4
constexpr double passLength = 8.0;    // 4 bars
constexpr double songSeconds = 12.0;
constexpr int songSamples = static_cast<int> (songSeconds * sampleRate);
constexpr int displayRows = 600;
constexpr double displayLowHz = 20.0;
constexpr double displayHighHz = 20000.0;
constexpr int edgeColumns = 8;        // steady cells needed either side of an edge in time
constexpr int edgeRows = 16;          // ... and in frequency
constexpr float floorDb = -160.0f;
const double unmeasured = std::numeric_limits<double>::quiet_NaN();

using Stereo = std::array<std::vector<float>, 2>;

double rowHz (int row)
{
    return displayLowHz * std::pow (displayHighHz / displayLowHz, (row + 0.5) / displayRows);
}

double toDb (double power)
{
    return power > 0.0 ? 10.0 * std::log10 (power) : -300.0;
}

// ---- statistics -----------------------------------------------------------------------------

double percentile (std::vector<double> values, double p)
{
    if (values.empty())
        return unmeasured;

    const auto index = static_cast<size_t> (std::clamp (p, 0.0, 1.0) * static_cast<double> (values.size() - 1) + 0.5);
    std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (index), values.end());
    return values[index];
}

double median (const std::vector<double>& values) { return percentile (values, 0.5); }

double standardDeviation (const std::vector<double>& values)
{
    if (values.size() < 2)
        return unmeasured;

    const auto mean = std::accumulate (values.begin(), values.end(), 0.0) / static_cast<double> (values.size());
    double sum = 0.0;

    for (auto v : values)
        sum += (v - mean) * (v - mean);

    return std::sqrt (sum / static_cast<double> (values.size() - 1));
}

double pearson (const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() < 3 || a.size() != b.size())
        return unmeasured;

    const auto n = static_cast<double> (a.size());
    const auto ma = std::accumulate (a.begin(), a.end(), 0.0) / n;
    const auto mb = std::accumulate (b.begin(), b.end(), 0.0) / n;
    double sab = 0.0, saa = 0.0, sbb = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }

    return saa > 0.0 && sbb > 0.0 ? sab / std::sqrt (saa * sbb) : unmeasured;
}

// ---- songs ----------------------------------------------------------------------------------

struct Song
{
    juce::String name;
    Stereo audio;
    std::vector<double> kicks;  // onsets in seconds (the transient smearing measure)
};

/** Pink noise: white noise through Paul Kellet's filter (-3 dB per octave within 0.05 dB). */
class PinkNoise
{
public:
    explicit PinkNoise (juce::int64 seed) : random (seed) {}

    float next()
    {
        const auto white = static_cast<double> (random.nextFloat()) * 2.0 - 1.0;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        const auto pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        return static_cast<float> (pink * 0.11);
    }

private:
    juce::Random random;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};

void scaleToRms (Stereo& audio, double targetDb)
{
    double sum = 0.0;

    for (const auto& channel : audio)
        for (auto x : channel)
            sum += static_cast<double> (x) * static_cast<double> (x);

    const auto rms = std::sqrt (sum / static_cast<double> (2 * audio[0].size()));
    const auto gain = static_cast<float> (std::pow (10.0, targetDb / 20.0) / std::max (rms, 1e-12));

    for (auto& channel : audio)
        for (auto& x : channel)
            x *= gain;
}

Song makePinkSong()
{
    Song song { "pink", {}, {} };
    PinkNoise left (101), right (202);

    for (auto& channel : song.audio)
        channel.assign (static_cast<size_t> (songSamples), 0.0f);

    for (size_t i = 0; i < static_cast<size_t> (songSamples); ++i)
    {
        song.audio[0][i] = left.next();
        song.audio[1][i] = right.next();
    }

    scaleToRms (song.audio, -20.0);
    return song;
}

/** One-pole low-pass and high-pass filters, for the synthetic instruments. */
struct OnePole
{
    explicit OnePole (double hz) : a (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate)) {}

    double lowPass (double x) { y += a * (x - y); return y; }
    double highPass (double x) { return x - lowPass (x); }

    double a, y = 0.0;
};

double polyBlep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }

    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }

    return 0.0;
}

/** A band-limited sawtooth. */
struct Saw
{
    double next (double hz)
    {
        const auto dt = hz / sampleRate;
        phase += dt;

        if (phase >= 1.0)
            phase -= 1.0;

        return 2.0 * phase - 1.0 - polyBlep (phase, dt);
    }

    double phase = 0.0;
};

Song makeMixSong()
{
    Song song { "mix", {}, {} };

    for (auto& channel : song.audio)
        channel.assign (static_cast<size_t> (songSamples), 0.0f);

    juce::Random random (2024);
    const auto noise = [&random] { return static_cast<double> (random.nextFloat()) * 2.0 - 1.0; };

    const auto add = [&song] (int index, double left, double right)
    {
        if (index >= 0 && index < songSamples)
        {
            song.audio[0][static_cast<size_t> (index)] += static_cast<float> (left);
            song.audio[1][static_cast<size_t> (index)] += static_cast<float> (right);
        }
    };

    constexpr double beat = 60.0 / tempo;
    constexpr double bar = 4.0 * beat;
    const auto samplesOf = [] (double seconds) { return static_cast<int> (std::lround (seconds * sampleRate)); };

    // Kick on every beat: a sine falling from about 170 Hz to 48 Hz, and a 2 ms click.
    for (auto onset = 0.0; onset < songSeconds; onset += beat)
    {
        song.kicks.push_back (onset);
        const auto start = samplesOf (onset);
        auto phase = 0.0;

        for (int i = 0; i < samplesOf (0.45); ++i)
        {
            const auto t = i / sampleRate;
            phase += juce::MathConstants<double>::twoPi * (48.0 + 120.0 * std::exp (-t / 0.035)) / sampleRate;
            const auto attack = std::min (1.0, i / 16.0);
            auto value = 0.55 * attack * std::exp (-t / 0.22) * std::sin (phase);

            if (i < 96)
                value += 0.15 * noise() * (1.0 - i / 96.0);

            add (start + i, value, value);
        }
    }

    // Snare on beats 2 and 4: filtered noise and a 185 Hz tone.
    for (auto onset = beat; onset < songSeconds; onset += 2.0 * beat)
    {
        const auto start = samplesOf (onset);
        OnePole highPass (900.0);

        for (int i = 0; i < samplesOf (0.3); ++i)
        {
            const auto t = i / sampleRate;
            const auto value = 0.22 * std::exp (-t / 0.11) * highPass.highPass (noise())
                             + 0.12 * std::exp (-t / 0.05) * std::sin (juce::MathConstants<double>::twoPi * 185.0 * t);
            add (start + i, value * 0.9, value);
        }
    }

    // Hats on every eighth note (offbeats louder, an open one at the end of each bar).
    for (int eighth = 0; eighth * beat / 2.0 < songSeconds; ++eighth)
    {
        const auto start = samplesOf (eighth * beat / 2.0);
        const auto open = eighth % 8 == 7;
        const auto level = eighth % 2 == 1 ? 0.16 : 0.10;
        const auto decay = open ? 0.18 : 0.025;
        const auto pan = eighth % 4 < 2 ? 0.8 : 1.0;
        OnePole first (6500.0), second (6500.0);

        for (int i = 0; i < samplesOf (open ? 0.6 : 0.12); ++i)
        {
            const auto value = level * std::exp (-(i / sampleRate) / decay) * second.highPass (first.highPass (noise()));
            add (start + i, value * pan, value * (1.8 - pan));
        }
    }

    // Bass line and chords: Am, F, C, G, one bar each.
    const double roots[] { 55.0, 43.65, 65.41, 49.0 };
    const double chords[][3] { { 220.0, 261.63, 329.63 }, { 174.61, 220.0, 261.63 },
                               { 261.63, 329.63, 392.0 }, { 196.0, 246.94, 293.66 } };
    const double pattern[] { 1.0, 1.0, 2.0, 1.0, 1.0, 1.5, 2.0, 1.0 };

    {
        Saw saw;
        OnePole first (380.0), second (380.0);
        const auto noteLength = beat / 2.0;

        for (int i = 0; i < songSamples; ++i)
        {
            const auto t = i / sampleRate;
            const auto bars = static_cast<int> (t / bar);
            const auto note = static_cast<int> (std::fmod (t, bar) / noteLength);
            const auto inNote = std::fmod (t, noteLength);
            const auto hz = roots[bars % 4] * pattern[note % 8];
            const auto envelope = std::min (1.0, inNote / 0.003) * (0.6 + 0.4 * std::exp (-inNote / 0.08))
                                * std::min (1.0, (noteLength - inNote) / 0.02);
            const auto value = 0.42 * envelope * second.lowPass (first.lowPass (saw.next (hz)));
            add (i, value, value);
        }
    }

    for (int voice = 0; voice < 3; ++voice)
    {
        for (const auto detune : { 0.996, 1.004 })
        {
            Saw saw;
            OnePole first (1800.0), second (1800.0);
            const auto pan = voice == 0 ? 0.7 : (voice == 1 ? 1.0 : 1.3);

            for (int i = 0; i < songSamples; ++i)
            {
                const auto t = i / sampleRate;
                const auto bars = static_cast<int> (t / bar);
                const auto inBar = std::fmod (t, bar);
                const auto envelope = std::min (1.0, inBar / 0.06) * std::min (1.0, (bar - inBar) / 0.08);
                const auto value = 0.05 * envelope * second.lowPass (first.lowPass (saw.next (chords[bars % 4][voice] * detune)));
                add (i, value * pan, value * (2.0 - pan));
            }
        }
    }

    // A noise bed under everything.
    PinkNoise left (303), right (404);

    for (int i = 0; i < songSamples; ++i)
        add (i, 0.006 * left.next(), 0.006 * right.next());

    // Peaks at -1 dBFS.
    auto peak = 0.0f;

    for (const auto& channel : song.audio)
        for (auto x : channel)
            peak = std::max (peak, std::abs (x));

    for (auto& channel : song.audio)
        for (auto& x : channel)
            x *= 0.89f / peak;

    return song;
}

/** Onsets of the low end of a real song: the energy below 150 Hz jumping by 9 dB within 20 ms. */
std::vector<double> findKicks (const Stereo& audio)
{
    OnePole first (150.0), second (150.0), smooth (1.0 / (2.0 * juce::MathConstants<double>::pi * 0.005));
    std::vector<double> envelope (audio[0].size());

    for (size_t i = 0; i < envelope.size(); ++i)
    {
        const auto low = second.lowPass (first.lowPass (0.5 * (audio[0][i] + audio[1][i])));
        envelope[i] = smooth.lowPass (low * low);
    }

    std::vector<double> kicks;
    const auto lag = static_cast<size_t> (0.02 * sampleRate);
    auto last = -1.0;

    for (size_t i = lag; i < envelope.size(); ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;

        if (t - last > 0.15 && envelope[i] > 1e-4 && toDb (envelope[i]) - toDb (envelope[i - lag]) > 9.0)
        {
            kicks.push_back (t - 0.01);  // the rise is measured 20 ms on: the onset is about here
            last = t;
        }
    }

    return kicks;
}

bool loadSong (const juce::File& file, double offsetSeconds, Song& song)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;

    const auto ratio = reader->sampleRate / sampleRate;
    const auto start = static_cast<juce::int64> (offsetSeconds * reader->sampleRate);
    const auto needed = static_cast<int> (std::ceil (songSamples * ratio)) + 8;
    juce::AudioBuffer<float> raw (2, needed);
    raw.clear();
    reader->read (&raw, 0, needed, start, true, true);

    song.name = "input";

    for (int channel = 0; channel < 2; ++channel)
    {
        song.audio[static_cast<size_t> (channel)].assign (static_cast<size_t> (songSamples), 0.0f);
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, raw.getReadPointer (channel), song.audio[static_cast<size_t> (channel)].data(), songSamples);
    }

    song.kicks = findKicks (song.audio);
    return true;
}

// ---- drawings -------------------------------------------------------------------------------

struct Drawing
{
    juce::String name;
    ArtDocument document;
    Canvas canvas;
};

ArtObject pictureObject (const juce::Image& image, float lowHz, float highHz, float startTime, float endTime, float cleanup)
{
    auto object = ArtDocument::makeImageObject (prepareImportedImage (image), lowHz, highHz);
    object.imageMode = ImageMode::original;
    object.cleanup = cleanup;
    object.startTime = startTime;
    object.endTime = endTime;
    return object;
}

juce::Image sunPicture()
{
    constexpr int size = 512;
    juce::Image image (juce::Image::ARGB, size, size, true);
    juce::Graphics g (image);
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);

    const auto centre = size / 2.0f;
    g.fillEllipse (centre - 150.0f, centre - 150.0f, 300.0f, 300.0f);

    for (int ray = 0; ray < 12; ++ray)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float> (ray) / 12.0f;
        juce::Path path;
        path.addTriangle (0.0f, -250.0f, -16.0f, -175.0f, 16.0f, -175.0f);
        g.fillPath (path, juce::AffineTransform::rotation (angle).translated (centre, centre));
    }

    return image;
}

juce::Image rampPicture()
{
    juce::Image image (juce::Image::ARGB, 64, 256, true);

    for (int y = 0; y < image.getHeight(); ++y)
    {
        const auto level = 1.0f - static_cast<float> (y) / static_cast<float> (image.getHeight() - 1);

        for (int x = 0; x < image.getWidth(); ++x)
            image.setPixelAt (x, y, juce::Colour::greyLevel (level));
    }

    return image;
}

/** A photo-like head and shoulders: soft light, a darker background, hair, eyes, a mouth and
    film grain, so it has every grey level and soft as well as sharp edges. */
juce::Image portraitPicture()
{
    constexpr int width = 384, height = 512;
    juce::Image image (juce::Image::ARGB, width, height, true);

    {
        juce::Graphics g (image);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), 0.0f, 0.0f, juce::Colour (0xff141414), 0.0f, height, false));
        g.fillAll();

        // Shoulders and neck.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6e6e6e), 120.0f, 380.0f, juce::Colour (0xff2c2c2c), 330.0f, 512.0f, false));
        g.fillEllipse (20.0f, 400.0f, 344.0f, 260.0f);
        g.setColour (juce::Colour (0xff8a8a8a));
        g.fillRect (160.0f, 330.0f, 64.0f, 90.0f);

        // The face, lit from the upper left.
        juce::ColourGradient skin (juce::Colour (0xffe8e8e8), 150.0f, 170.0f, juce::Colour (0xff5a5a5a), 300.0f, 360.0f, true);
        g.setGradientFill (skin);
        g.fillEllipse (102.0f, 110.0f, 180.0f, 240.0f);

        // Hair.
        juce::Path hair;
        hair.startNewSubPath (92.0f, 230.0f);
        hair.cubicTo (70.0f, 80.0f, 300.0f, 40.0f, 294.0f, 230.0f);
        hair.cubicTo (280.0f, 150.0f, 210.0f, 120.0f, 190.0f, 132.0f);
        hair.cubicTo (150.0f, 125.0f, 105.0f, 160.0f, 92.0f, 230.0f);
        g.setColour (juce::Colour (0xff1c1c1c));
        g.fillPath (hair);

        // Eyes, brows, nose shadow, mouth.
        for (const auto x : { 150.0f, 214.0f })
        {
            g.setColour (juce::Colour (0xffd0d0d0));
            g.fillEllipse (x - 4.0f, 208.0f, 30.0f, 14.0f);
            g.setColour (juce::Colour (0xff202020));
            g.fillEllipse (x + 6.0f, 209.0f, 11.0f, 12.0f);
            g.setColour (juce::Colour (0xff303030));
            g.fillRoundedRectangle (x - 6.0f, 192.0f, 34.0f, 6.0f, 3.0f);
        }

        g.setColour (juce::Colour (0x66000000));
        g.fillEllipse (184.0f, 232.0f, 16.0f, 50.0f);
        g.setColour (juce::Colour (0xff3c3c3c));
        g.fillEllipse (160.0f, 290.0f, 66.0f, 18.0f);
        g.setColour (juce::Colour (0xff9a9a9a));
        g.fillEllipse (170.0f, 292.0f, 46.0f, 6.0f);
    }

    // Film grain.
    juce::Random random (77);

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto colour = image.getPixelAt (x, y);
            const auto grain = (random.nextFloat() - 0.5f) * 0.08f;
            image.setPixelAt (x, y, juce::Colour::greyLevel (juce::jlimit (0.0f, 1.0f, colour.getBrightness() + grain)));
        }

    return image;
}

std::vector<Drawing> makeDrawings (const FrequencyScale& scale)
{
    std::vector<Drawing> drawings;

    {
        Drawing drawing { "hello", {}, {} };
        auto text = ArtDocument::makeTextObject ("HELLO", static_cast<float> (bandRange (Band::mid, scale).low),
                                                 static_cast<float> (bandRange (Band::high, scale).high));
        text.startTime = 0.04f;
        text.endTime = 0.96f;
        drawing.document.objects.push_back (text);
        drawings.push_back (std::move (drawing));
    }

    {
        Drawing drawing { "sun", {}, {} };
        drawing.document.objects.push_back (pictureObject (sunPicture(), 60.0f, 1200.0f, 0.3f, 0.7f, 0.0f));
        drawings.push_back (std::move (drawing));
    }

    {
        Drawing drawing { "ramp", {}, {} };
        drawing.document.objects.push_back (pictureObject (rampPicture(), 30.0f, 16000.0f, 0.1f, 0.9f, 0.0f));
        drawings.push_back (std::move (drawing));
    }

    {
        Drawing drawing { "portrait", {}, {} };
        drawing.document.objects.push_back (pictureObject (portraitPicture(), 40.0f, 8000.0f, 0.22f, 0.78f, 0.05f));
        drawings.push_back (std::move (drawing));
    }

    Compositor compositor;

    for (auto& drawing : drawings)
        compositor.render (drawing.document, scale, drawing.canvas);

    return drawings;
}

// ---- running an engine ----------------------------------------------------------------------

struct Run
{
    Stereo output;          // moved back by the latency: output[i] answers input[i]
    double seconds = 0.0;   // CPU time spent in process()
};

/** Plays the song through a fresh engine, the drawing's single pass from bar 2 at 120 BPM
    (Transport, through the PlayheadTracker), in blocks taken in turn from `blocks`. */
Run runEngine (int engineIndex, const Song& song, const MaskPlan* plan, const CarveSettings& settings,
               const std::vector<int>& blocks)
{
    auto carver = createSpectralCarver (engineIndex);
    const auto maxBlock = *std::max_element (blocks.begin(), blocks.end());
    carver->prepare (sampleRate, maxBlock, 2);
    carver->setPlan (plan);

    const auto latency = carver->getLatencySamples();
    const auto total = songSamples + latency;

    Stereo audio;

    for (size_t channel = 0; channel < 2; ++channel)
    {
        audio[channel] = song.audio[channel];
        audio[channel].resize (static_cast<size_t> (total), 0.0f);
    }

    PlayheadTracker tracker;
    tracker.prepare (sampleRate);
    tracker.reset();

    TrackerSettings trackerSettings;
    trackerSettings.mode = TriggerMode::transport;
    trackerSettings.repeat = false;
    trackerSettings.startBar = startBar;
    trackerSettings.lengthQuarterNotes = 16.0;
    trackerSettings.quarterNotesPerBar = 4.0;

    PlaySegment segments[PlayheadTracker::maxSegments];
    const juce::MidiBuffer noMidi;
    Run result;
    size_t next = 0;

    for (int start = 0; start < total;)
    {
        const auto length = std::min (blocks[next++ % blocks.size()], total - start);

        HostTiming timing;
        timing.isPlaying = true;
        timing.bpm = tempo;
        timing.timeInSeconds = start / sampleRate;
        timing.ppqPosition = start / sampleRate * tempo / 60.0;
        timing.ppqPositionOfLastBarStart = std::floor (*timing.ppqPosition / 4.0) * 4.0;

        const auto numSegments = tracker.process (timing, noMidi, length, trackerSettings, segments);
        float* channels[2] = { audio[0].data() + start, audio[1].data() + start };

        const auto before = std::chrono::steady_clock::now();
        carver->process (channels, 2, length, segments, numSegments, settings);
        result.seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - before).count();

        start += length;
    }

    for (size_t channel = 0; channel < 2; ++channel)
        result.output[channel].assign (audio[channel].begin() + latency, audio[channel].end());

    return result;
}

float largestDifference (const Stereo& a, const Stereo& b, double fromSeconds = 0.0, double toSeconds = songSeconds)
{
    auto largest = 0.0f;
    const auto from = static_cast<size_t> (std::max (0.0, fromSeconds * sampleRate));
    const auto to = std::min (a[0].size(), static_cast<size_t> (std::max (0.0, toSeconds * sampleRate)));

    for (size_t channel = 0; channel < 2; ++channel)
        for (auto i = from; i < to; ++i)
            largest = std::max (largest, std::abs (a[channel][i] - b[channel][i]));

    return largest;
}

std::vector<float> mono (const Stereo& audio)
{
    std::vector<float> mid (audio[0].size());

    for (size_t i = 0; i < mid.size(); ++i)
        mid[i] = 0.5f * (audio[0][i] + audio[1][i]);

    return mid;
}

// ---- the analyser ---------------------------------------------------------------------------

/** What an analyser shows: dB per cell, a column per frame (centred at column x hop), 600 rows
    on the log scale from 20 Hz (row 0) to 20 kHz. */
struct Display
{
    int fftSize = 0;
    int hop = 0;
    int columns = 0;
    std::vector<float> db;  // db[column * displayRows + row]

    float at (int column, int row) const { return db[static_cast<size_t> (column * displayRows + row)]; }
    double secondsAt (int column) const { return column * hop / sampleRate; }
};

Display analyse (const std::vector<float>& signal, int fftSize)
{
    Display display;
    display.fftSize = fftSize;
    display.hop = fftSize / 8;
    display.columns = static_cast<int> (signal.size()) / display.hop;
    display.db.assign (static_cast<size_t> (display.columns * displayRows), floorDb);

    juce::dsp::FFT fft (juce::roundToInt (std::log2 (fftSize)));
    std::vector<float> window (static_cast<size_t> (fftSize)), work (static_cast<size_t> (2 * fftSize));
    std::vector<float> binDb (static_cast<size_t> (fftSize / 2 + 1));

    for (int n = 0; n < fftSize; ++n)
        window[static_cast<size_t> (n)] = static_cast<float> (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / fftSize));

    // A full-scale sine reads 0 dB.
    const auto norm = static_cast<float> (fftSize) / 4.0f;

    std::vector<int> rowBin (static_cast<size_t> (displayRows));
    std::vector<float> rowFraction (static_cast<size_t> (displayRows));

    for (int row = 0; row < displayRows; ++row)
    {
        const auto bin = rowHz (row) * fftSize / sampleRate;
        rowBin[static_cast<size_t> (row)] = static_cast<int> (bin);
        rowFraction[static_cast<size_t> (row)] = static_cast<float> (bin - std::floor (bin));
    }

    for (int column = 0; column < display.columns; ++column)
    {
        const auto first = column * display.hop - fftSize / 2;

        for (int n = 0; n < fftSize; ++n)
        {
            const auto index = first + n;
            work[static_cast<size_t> (n)] = index >= 0 && index < static_cast<int> (signal.size())
                                          ? signal[static_cast<size_t> (index)] * window[static_cast<size_t> (n)] : 0.0f;
        }

        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        for (size_t bin = 0; bin < binDb.size(); ++bin)
            binDb[bin] = std::max (floorDb, 20.0f * std::log10 (std::max (1e-12f, work[bin] / norm)));

        for (int row = 0; row < displayRows; ++row)
        {
            const auto bin = static_cast<size_t> (rowBin[static_cast<size_t> (row)]);
            const auto t = rowFraction[static_cast<size_t> (row)];
            display.db[static_cast<size_t> (column * displayRows + row)] = binDb[bin] + (binDb[bin + 1] - binDb[bin]) * t;
        }
    }

    return display;
}

// ---- metrics --------------------------------------------------------------------------------

struct Range
{
    const char* name;
    double lowHz, highHz;
};

const Range ranges[] { { "30-250", 30.0, 250.0 }, { "250-2k", 250.0, 2000.0 }, { "2k-16k", 2000.0, 16000.0 },
                       { "all", 30.0, 16000.0 } };
constexpr int numRanges = 4;

struct Metrics
{
    double depth = unmeasured, visibility = unmeasured, fidelity = unmeasured, error = unmeasured;
    double edgeTimeMs = unmeasured, edgeRows = unmeasured;
    double leakNear = unmeasured, leakFar = unmeasured, preEcho = unmeasured, postEcho = unmeasured;
    double noise = unmeasured, spread = unmeasured;
    int numTarget = 0, numKeep = 0, numTimeEdges = 0, numFrequencyEdges = 0;
};

/** The drawing on the analyser's cells and what follows from it for one case. */
struct CellMap
{
    int columns = 0;
    std::vector<float> drawn;     // the mask plan's value (0..1) at the cell's time and frequency
    std::vector<float> intended;  // the share of |depth| the cell should drop by
    std::vector<uint8_t> inPass;  // per column
    std::vector<int> distance;    // chessboard distance to the nearest touched cell

    size_t index (int column, int row) const { return static_cast<size_t> (column * displayRows + row); }
    bool isTarget (int column, int row) const { return intended[index (column, row)] >= 0.5f; }
    bool isTouched (int column, int row) const { return intended[index (column, row)] > 0.02f; }
    bool isKeep (int column, int row) const { return intended[index (column, row)] <= 0.02f; }
};

CellMap mapCells (const Display& display, const MaskPlan& plan, CarveMode mode)
{
    CellMap map;
    map.columns = display.columns;
    const auto size = static_cast<size_t> (display.columns * displayRows);
    map.drawn.assign (size, 0.0f);
    map.intended.assign (size, 0.0f);
    map.inPass.assign (static_cast<size_t> (display.columns), 0);

    for (int column = 0; column < display.columns; ++column)
    {
        const auto fraction = (display.secondsAt (column) - passStart) / passLength;

        if (fraction < 0.0 || fraction >= 1.0)
            continue;

        map.inPass[static_cast<size_t> (column)] = 1;
        const auto canvasColumn = plan.columnAt (fraction);

        for (int row = 0; row < displayRows; ++row)
        {
            const auto value = plan.valueAt (canvasColumn, rowHz (row));
            map.drawn[map.index (column, row)] = value;
            map.intended[map.index (column, row)] = mode == CarveMode::carve ? value : 1.0f - value;
        }
    }

    // Two-pass chessboard distance transform.
    constexpr int unreached = 1000;
    map.distance.assign (size, unreached);

    for (int column = 0; column < display.columns; ++column)
        for (int row = 0; row < displayRows; ++row)
            if (map.isTouched (column, row))
                map.distance[map.index (column, row)] = 0;

    const auto relax = [&map, &display] (int column, int row, int dc, int dr)
    {
        const auto c = column + dc, r = row + dr;

        if (c >= 0 && c < display.columns && r >= 0 && r < displayRows)
            map.distance[map.index (column, row)] = std::min (map.distance[map.index (column, row)], map.distance[map.index (c, r)] + 1);
    };

    for (int column = 0; column < display.columns; ++column)
        for (int row = 0; row < displayRows; ++row)
        {
            relax (column, row, -1, -1);
            relax (column, row, -1, 0);
            relax (column, row, -1, 1);
            relax (column, row, 0, -1);
        }

    for (int column = display.columns - 1; column >= 0; --column)
        for (int row = displayRows - 1; row >= 0; --row)
        {
            relax (column, row, 1, 1);
            relax (column, row, 1, 0);
            relax (column, row, 1, -1);
            relax (column, row, 0, 1);
        }

    return map;
}

/** 10-90 % width (in cells) of the median of a set of edge profiles that rise from outside
    (index 0) to inside (the last index); NaN if there are none or they hardly rise. */
double transitionWidth (const std::vector<std::vector<double>>& profiles)
{
    if (profiles.size() < 3)
        return unmeasured;

    const auto length = profiles.front().size();
    std::vector<double> profile (length);

    for (size_t j = 0; j < length; ++j)
    {
        std::vector<double> column;

        for (const auto& p : profiles)
            column.push_back (p[j]);

        profile[j] = median (column);
    }

    const auto low = 0.5 * (profile[0] + profile[1]);
    const auto high = 0.5 * (profile[length - 2] + profile[length - 1]);

    if (high - low < 6.0)
        return unmeasured;

    const auto crossing = [&] (double fraction)
    {
        const auto level = low + fraction * (high - low);

        for (size_t j = 1; j < length; ++j)
            if (profile[j] >= level && profile[j - 1] < level)
                return static_cast<double> (j - 1) + (level - profile[j - 1]) / (profile[j] - profile[j - 1]);

        return unmeasured;
    };

    return crossing (0.9) - crossing (0.1);
}

/** Collects edge profiles along one line of cells (a row of the display in time, or a column
    in frequency): wherever the drawing crosses half way, with `steadyCells` on either side that
    are clearly outside (keep) and inside (target) it, allowing two cells of transition outside
    the crossing (the drawing's own anti-aliasing). Each profile runs from outside to inside. */
template <typename Intended, typename Attenuation, typename Usable>
void collectEdges (int length, int steadyCells, std::vector<std::vector<double>>& profiles,
                   Intended&& intended, Attenuation&& attenuation, Usable&& usable)
{
    for (int i = steadyCells; i + steadyCells <= length; ++i)
    {
        for (const auto rising : { true, false })
        {
            // Step k from outside (k < 0) to inside (k >= 0) of the edge between i - 1 and i.
            const auto at = [i, rising] (int k) { return rising ? i + k : i - 1 - k; };

            if (! (intended (at (-1)) < 0.5f && intended (at (0)) >= 0.5f))
                continue;

            auto ok = true;

            for (int k = -steadyCells; k < steadyCells && ok; ++k)
            {
                const auto cell = at (k);
                ok = usable (cell) && (k < -2 ? intended (cell) <= 0.02f : (k > 0 ? intended (cell) >= 0.98f : true));
            }

            if (! ok)
                continue;

            std::vector<double> profile;

            for (int k = -steadyCells; k < steadyCells; ++k)
                profile.push_back (attenuation (at (k)));

            profiles.push_back (std::move (profile));
        }
    }
}

Metrics measure (const Display& in, const Display& out, const CellMap& map, const Range& range, float depthDb)
{
    Metrics m;
    const auto fullDepth = static_cast<double> (-depthDb);

    std::vector<double> targetAtten, ringOut, ringIn, targetOut, targetIn, nearAtten, farAtten, farDiff, preAtten, postAtten;
    std::vector<double> clippedAtten, intended, errors;

    const auto inRange = [&range] (int row) { const auto hz = rowHz (row); return hz >= range.lowHz && hz < range.highHz; };
    const auto atten = [&] (int column, int row) { return static_cast<double> (in.at (column, row)) - static_cast<double> (out.at (column, row)); };
    const auto passColumn = [&map] (int column) { return column >= 0 && column < map.columns && map.inPass[static_cast<size_t> (column)] != 0; };

    for (int column = 0; column < map.columns; ++column)
    {
        if (! passColumn (column))
            continue;

        for (int row = 0; row < displayRows; ++row)
        {
            if (! inRange (row))
                continue;

            const auto a = atten (column, row);
            const auto share = static_cast<double> (map.intended[map.index (column, row)]);
            const auto clipped = std::clamp (a, 0.0, fullDepth);
            clippedAtten.push_back (clipped);
            intended.push_back (share);

            if (map.isTouched (column, row))
                errors.push_back (std::abs (clipped - share * fullDepth));

            const auto distance = map.distance[map.index (column, row)];

            if (map.isTarget (column, row))
            {
                ++m.numTarget;
                targetAtten.push_back (a);
                targetOut.push_back (out.at (column, row));
                targetIn.push_back (in.at (column, row));
                continue;
            }

            if (! map.isKeep (column, row))
                continue;

            ++m.numKeep;

            if (distance <= 4)
            {
                ringOut.push_back (out.at (column, row));
                ringIn.push_back (in.at (column, row));
            }

            if (distance <= 2)
                nearAtten.push_back (a);

            if (distance > 8)
            {
                farAtten.push_back (a);
                farDiff.push_back (-a);
            }

            const auto touchedAt = [&] (int c) { return passColumn (c) && map.isTouched (c, row); };

            if (touchedAt (column + 1) || touchedAt (column + 2) || touchedAt (column + 3))
                preAtten.push_back (a);

            if (touchedAt (column - 1) || touchedAt (column - 2) || touchedAt (column - 3))
                postAtten.push_back (a);
        }
    }

    m.depth = median (targetAtten);
    m.spread = percentile (targetAtten, 0.75) - percentile (targetAtten, 0.25);
    m.visibility = (median (ringOut) - median (targetOut)) - (median (ringIn) - median (targetIn));
    m.fidelity = pearson (clippedAtten, intended);
    m.error = median (errors);
    m.leakNear = median (nearAtten);
    m.leakFar = median (farAtten);
    m.preEcho = median (preAtten);
    m.postEcho = median (postAtten);
    m.noise = standardDeviation (farDiff);

    // Edges: profiles of the attenuation from outside a target region into it, along each row
    // (time) and each column (frequency).
    std::vector<std::vector<double>> timeProfiles, frequencyProfiles;

    for (int row = 0; row < displayRows; ++row)
        if (inRange (row))
            collectEdges (map.columns, edgeColumns, timeProfiles,
                          [&] (int c) { return map.intended[map.index (c, row)]; },
                          [&] (int c) { return atten (c, row); },
                          passColumn);

    for (int column = 0; column < map.columns; ++column)
        if (passColumn (column))
            collectEdges (displayRows, edgeRows, frequencyProfiles,
                          [&] (int r) { return map.intended[map.index (column, r)]; },
                          [&] (int r) { return atten (column, r); },
                          [&] (int r) { return inRange (r); });

    m.numTimeEdges = static_cast<int> (timeProfiles.size());
    m.numFrequencyEdges = static_cast<int> (frequencyProfiles.size());
    m.edgeTimeMs = transitionWidth (timeProfiles) * in.hop / sampleRate * 1000.0;
    m.edgeRows = transitionWidth (frequencyProfiles);
    return m;
}

/** Energy just before each kick onset relative to just after it, output vs input (median over
    the kicks inside the pass at a moment with something drawn, dB): above 0, the engine pushed
    energy ahead of the transients. */
double kickSmear (const Song& song, const MaskPlan& plan, const std::vector<float>& in, const std::vector<float>& out,
                  int& numKicks)
{
    const auto energy = [] (const std::vector<float>& x, double from, double to)
    {
        double sum = 1e-20;

        for (auto i = static_cast<size_t> (std::max (0.0, from * sampleRate)); i < std::min (x.size(), static_cast<size_t> (to * sampleRate)); ++i)
            sum += static_cast<double> (x[i]) * static_cast<double> (x[i]);

        return sum;
    };

    std::vector<double> changes;

    for (const auto onset : song.kicks)
    {
        if (onset < passStart + 0.05 || onset > passStart + passLength - 0.05
            || ! plan.isColumnUsed (plan.columnAt ((onset - passStart) / passLength)))
            continue;

        const auto ratio = [&] (const std::vector<float>& x)
        {
            return toDb (energy (x, onset - 0.030, onset - 0.003)) - toDb (energy (x, onset, onset + 0.030));
        };

        changes.push_back (ratio (out) - ratio (in));
    }

    numKicks = static_cast<int> (changes.size());
    return median (changes);
}

/** A click (one sample at -6 dBFS) every 250 ms, an eighth of a beat after the beats, and
    nothing else: whatever comes out before a click came from it. */
Song makeClickSong()
{
    Song song { "clicks", {}, {} };

    for (auto& channel : song.audio)
        channel.assign (static_cast<size_t> (songSamples), 0.0f);

    for (auto t = 0.0625; t < songSeconds; t += 0.25)
    {
        song.kicks.push_back (t);
        const auto index = static_cast<size_t> (std::lround (t * sampleRate));
        song.audio[0][index] = song.audio[1][index] = 0.5f;
    }

    return song;
}

/** The energy that comes out in the 25 ms before each click relative to the 25 ms from it on
    (dB, median over the clicks inside the pass at a moment with something drawn; -120 for
    none at all). A time-invariant gain gives none; carving a transient's frequencies with
    long frames spreads it over the frame, ahead of the click too. */
double clickPreEcho (const Song& clickSong, const MaskPlan& plan, const Stereo& out, int& numClicks)
{
    std::vector<double> echoes;
    const auto x = mono (out);

    for (const auto onset : clickSong.kicks)
    {
        if (onset < passStart + 0.05 || onset > passStart + passLength - 0.05
            || ! plan.isColumnUsed (plan.columnAt ((onset - passStart) / passLength)))
            continue;

        const auto click = static_cast<int> (std::lround (onset * sampleRate));
        const auto span = static_cast<int> (0.025 * sampleRate);
        double before = 0.0, after = 0.0;

        for (int i = click - span; i < click + span; ++i)
        {
            const auto e = static_cast<double> (x[static_cast<size_t> (i)]) * static_cast<double> (x[static_cast<size_t> (i)]);
            (i < click ? before : after) += e;
        }

        echoes.push_back (std::max (-120.0, toDb (before) - toDb (after)));
    }

    numClicks = static_cast<int> (echoes.size());
    return median (echoes);
}

/** How long before `from` the output first differs from the input (by more than -80 dBFS),
    and how long after `to` it last does, in ms (NaN: never). */
std::pair<double, double> leadAndLag (const Stereo& in, const Stereo& out, double from, double to)
{
    constexpr float threshold = 1.0e-4f;
    auto first = -1, last = -1;

    for (int i = 0; i < songSamples; ++i)
        for (size_t channel = 0; channel < 2; ++channel)
            if (std::abs (out[channel][static_cast<size_t> (i)] - in[channel][static_cast<size_t> (i)]) > threshold)
            {
                if (first < 0)
                    first = i;

                last = i;
            }

    if (first < 0)
        return { unmeasured, unmeasured };

    return { (from - first / sampleRate) * 1000.0, (last / sampleRate - to) * 1000.0 };
}

// ---- pictures -------------------------------------------------------------------------------

juce::Colour colourAt (float value)
{
    static const juce::Colour stops[] { juce::Colour (0xff07080c), juce::Colour (0xff1b1036), juce::Colour (0xff3d1466),
                                        juce::Colour (0xff7a1f7f), juce::Colour (0xffb83a6e), juce::Colour (0xffe8664a),
                                        juce::Colour (0xfff9a13a), juce::Colour (0xfffce7a0), juce::Colour (0xffffffff) };
    constexpr int last = static_cast<int> (std::size (stops)) - 1;
    const auto x = std::clamp (value, 0.0f, 1.0f) * static_cast<float> (last);
    const auto index = std::min (last - 1, static_cast<int> (x));
    return stops[index].interpolatedWith (stops[index + 1], x - static_cast<float> (index));
}

constexpr int panelWidth = 1200;
constexpr int panelHeight = 300;
constexpr int marginLeft = 48;
constexpr int labelHeight = 22;

/** Draws a panel of cell values (0..1) at y, with its label, the Hz scale and the pass's ends. */
void drawPanel (juce::Image& image, int top, const juce::String& label, int columns,
                const std::function<float (int column, int row)>& value, double hop)
{
    {
        juce::Graphics g (image);
        g.setColour (juce::Colour (0xffe8ebf2));
        g.setFont (fonts::ui (14.0f, fonts::UiWeight::medium));
        g.drawText (label, marginLeft, top, panelWidth, labelHeight, juce::Justification::centredLeft);
    }

    const auto y0 = top + labelHeight;

    {
        juce::Image::BitmapData pixels (image, marginLeft, y0, panelWidth, panelHeight, juce::Image::BitmapData::writeOnly);

        for (int x = 0; x < panelWidth; ++x)
        {
            const auto column = std::min (columns - 1, x * columns / panelWidth);

            for (int y = 0; y < panelHeight; ++y)
            {
                const auto row = (panelHeight - 1 - y) * displayRows / panelHeight;
                const auto v = 0.5f * (value (column, row) + value (column, std::min (displayRows - 1, row + 1)));
                pixels.setPixelColour (x, y, colourAt (v));
            }
        }
    }

    juce::Graphics g (image);
    g.setFont (fonts::mono (11.0f));

    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const auto y = static_cast<float> (y0 + panelHeight) - static_cast<float> (std::log (hz / displayLowHz) / std::log (displayHighHz / displayLowHz) * panelHeight);
        g.setColour (juce::Colour (0xff5a6274));
        g.drawHorizontalLine (juce::roundToInt (y), static_cast<float> (marginLeft - 6), static_cast<float> (marginLeft));
        g.setColour (juce::Colour (0xff8c94a6));
        g.drawText (formatHz (hz), 0, juce::roundToInt (y) - 7, marginLeft - 8, 14, juce::Justification::centredRight);
    }

    g.setColour (juce::Colour (0xff36d7c4));

    for (const auto seconds : { passStart, passStart + passLength })
    {
        const auto x = static_cast<float> (marginLeft) + static_cast<float> (seconds / (columns * hop / sampleRate) * panelWidth);
        g.fillRect (x - 1.0f, static_cast<float> (y0 - 5), 2.0f, 5.0f);
    }
}

bool writeWav (const Stereo& audio, const juce::File& file)
{
    file.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);

    if (! fileStream->openedOk())
        return false;

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
    juce::WavAudioFormat wav;
    const auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                         .withSampleRate (sampleRate)
                                                         .withNumChannels (2)
                                                         .withBitsPerSample (32)
                                                         .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));

    if (writer == nullptr)
        return false;

    const float* channels[2] = { audio[0].data(), audio[1].data() };
    return writer->writeFromFloatArrays (channels, 2, static_cast<int> (audio[0].size()));
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream);
}

void writeCasePicture (const juce::File& file, const juce::String& title, const Display& in, const Display& out,
                       const CellMap& map, float depthDb)
{
    constexpr int panels = 4;
    juce::Image image (juce::Image::ARGB, marginLeft + panelWidth + 16, 34 + panels * (labelHeight + panelHeight + 10), true);

    {
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff0b0d12));
        g.setColour (juce::Colour (0xffe8ebf2));
        g.setFont (fonts::ui (16.0f, fonts::UiWeight::semiBold));
        g.drawText (title, marginLeft, 6, panelWidth, 22, juce::Justification::centredLeft);
    }

    // Input and output on one dB scale: 80 dB under the input's loudest cells.
    std::vector<double> levels;

    for (size_t i = 0; i < in.db.size(); i += 7)
        levels.push_back (in.db[i]);

    const auto top = static_cast<float> (percentile (levels, 0.995));
    const auto bottom = top - 80.0f;
    const auto shade = [top, bottom] (float db) { return (db - bottom) / (top - bottom); };

    auto y = 34;
    const auto hop = static_cast<double> (in.hop);
    drawPanel (image, y, "the drawing on the analyser's cells: how far each should drop (white = by all of |depth|)",
               map.columns, [&map] (int c, int r) { return map.intended[map.index (c, r)]; }, hop);
    y += labelHeight + panelHeight + 10;
    drawPanel (image, y, "input (FFT " + juce::String (in.fftSize) + ", " + juce::String (juce::roundToInt (bottom)) + " to " + juce::String (juce::roundToInt (top)) + " dB)",
               in.columns, [&in, &shade] (int c, int r) { return shade (in.at (c, r)); }, hop);
    y += labelHeight + panelHeight + 10;
    drawPanel (image, y, "output", out.columns, [&out, &shade] (int c, int r) { return shade (out.at (c, r)); }, hop);
    y += labelHeight + panelHeight + 10;
    drawPanel (image, y, "attenuation, input - output (0 to " + juce::String (juce::roundToInt (-depthDb)) + " dB)", in.columns,
               [&in, &out, depthDb] (int c, int r) { return (in.at (c, r) - out.at (c, r)) / -depthDb; }, hop);

    if (! writePng (image, file))
        std::cerr << "Cannot write " << file.getFullPathName() << std::endl;
}

void writeCanvasPicture (const juce::File& file, const Canvas& canvas)
{
    juce::Image image (juce::Image::ARGB, Canvas::width / 2, Canvas::height / 2, false);

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto v = 0.25f * (canvas.get (2 * x, 2 * y) + canvas.get (2 * x + 1, 2 * y)
                                    + canvas.get (2 * x, 2 * y + 1) + canvas.get (2 * x + 1, 2 * y + 1));
            image.setPixelAt (x, y, colourAt (v));
        }

    if (! writePng (image, file))
        std::cerr << "Cannot write " << file.getFullPathName() << std::endl;
}

// ---- the report -----------------------------------------------------------------------------

struct CaseResult
{
    juce::String song, drawing, mode;
    double blocks = unmeasured, outside = 0.0, kick = unmeasured, costMsPerSecond = 0.0;
    double lead = unmeasured, lag = unmeasured, clickPreEcho = unmeasured;
    int kicks = 0, clicks = 0;
    std::vector<int> ffts;
    std::vector<std::array<Metrics, numRanges>> metrics;  // per analyser
};

juce::String number (double value, int decimals = 1, int width = 6)
{
    const auto text = ! std::isfinite (value) ? juce::String ("-")
                    : (decimals > 0 ? juce::String (value, decimals) : juce::String (juce::roundToInt (value)));
    return text.paddedLeft (' ', width);
}

juce::String dbfs (double linear)
{
    if (! std::isfinite (linear))
        return "-";

    return linear > 0.0 ? juce::String (20.0 * std::log10 (linear), 1) + " dBFS" : juce::String ("exact");
}

juce::var toVar (double value)
{
    return std::isfinite (value) ? juce::var (value) : juce::var();
}

juce::var metricsVar (const Metrics& m)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("depth", toVar (m.depth));
    object->setProperty ("visibility", toVar (m.visibility));
    object->setProperty ("fidelity", toVar (m.fidelity));
    object->setProperty ("error", toVar (m.error));
    object->setProperty ("edgeTimeMs", toVar (m.edgeTimeMs));
    object->setProperty ("edgeRows", toVar (m.edgeRows));
    object->setProperty ("leakNear", toVar (m.leakNear));
    object->setProperty ("leakFar", toVar (m.leakFar));
    object->setProperty ("preEcho", toVar (m.preEcho));
    object->setProperty ("postEcho", toVar (m.postEcho));
    object->setProperty ("noise", toVar (m.noise));
    object->setProperty ("spread", toVar (m.spread));
    object->setProperty ("targetCells", m.numTarget);
    object->setProperty ("keepCells", m.numKeep);
    object->setProperty ("timeEdges", m.numTimeEdges);
    object->setProperty ("frequencyEdges", m.numFrequencyEdges);
    return juce::var (object);
}

const char* const metricHeader = "  depth    vis      r    err  edgeT  edgeF   near    far    pre   post  noise spread";

juce::String metricLine (const Metrics& m)
{
    return number (m.depth) + " " + number (m.visibility) + " " + number (m.fidelity, 3) + " " + number (m.error)
         + " " + number (m.edgeTimeMs, 0) + " " + number (m.edgeRows) + " " + number (m.leakNear) + " "
         + number (m.leakFar) + " " + number (m.preEcho) + " " + number (m.postEcho) + " " + number (m.noise)
         + " " + number (m.spread);
}

/** The mean of each metric over a set (skipping the ones that could not be measured). */
Metrics meanOf (const std::vector<const Metrics*>& set)
{
    Metrics mean;
    const auto average = [&set] (double Metrics::* field)
    {
        double sum = 0.0;
        int count = 0;

        for (const auto* m : set)
            if (std::isfinite (m->*field))
            {
                sum += m->*field;
                ++count;
            }

        return count > 0 ? sum / count : unmeasured;
    };

    for (const auto field : { &Metrics::depth, &Metrics::visibility, &Metrics::fidelity, &Metrics::error, &Metrics::edgeTimeMs,
                              &Metrics::edgeRows, &Metrics::leakNear, &Metrics::leakFar, &Metrics::preEcho, &Metrics::postEcho,
                              &Metrics::noise, &Metrics::spread })
        mean.*field = average (field);

    return mean;
}

// ---- options --------------------------------------------------------------------------------

struct Options
{
    juce::File outputDir;
    int engine = 0;
    juce::File input;
    double offset = 0.0;
    juce::StringArray songs { "pink", "mix", "input" };
    juce::StringArray drawings { "hello", "sun", "ramp", "portrait" };
    juce::StringArray modes { "carve", "stencil" };
    std::vector<int> ffts { 4096, 8192, 16384 };
    float depthDb = -60.0f;
    bool randomBlocks = true;
    bool pictures = true;
    bool sounds = false;
};

bool parseOptions (int argc, char* argv[], Options& options)
{
    if (argc < 2 || juce::String (argv[1]).startsWith ("-"))
        return false;

    options.outputDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);

    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto value = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        const auto list = [&] { return juce::StringArray::fromTokens (value(), ",", ""); };

        if (arg == "--engine")         options.engine = value().getIntValue();
        else if (arg == "--input")     options.input = juce::File::getCurrentWorkingDirectory().getChildFile (value());
        else if (arg == "--offset")    options.offset = value().getDoubleValue();
        else if (arg == "--songs")     options.songs = list();
        else if (arg == "--drawings")  options.drawings = list();
        else if (arg == "--modes")     options.modes = list();
        else if (arg == "--depth")     options.depthDb = juce::jlimit (SpectralCarver::minDepthDb, 0.0f, value().getFloatValue());
        else if (arg == "--no-png")    options.pictures = false;
        else if (arg == "--wav")       options.sounds = true;
        else if (arg == "--ffts")
        {
            options.ffts.clear();

            for (const auto& size : list())
                if (juce::isPowerOfTwo (size.getIntValue()) && size.getIntValue() >= 256)
                    options.ffts.push_back (size.getIntValue());
        }
        else if (arg == "--quick")
        {
            options.ffts = { 8192 };
            options.randomBlocks = false;
        }
        else
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return false;
        }
    }

    return ! options.ffts.empty();
}

int runLab (const Options& options)
{
    const auto engineProbe = createSpectralCarver (options.engine);

    if (engineProbe == nullptr)
    {
        std::cerr << "No engine " << options.engine << " (0 to " << getNumSpectralCarvers() - 1 << ")" << std::endl;
        return 1;
    }

    engineProbe->prepare (sampleRate, 512, 2);
    const auto latency = engineProbe->getLatencySamples();

    if (! options.outputDir.createDirectory())
    {
        std::cerr << "Cannot create " << options.outputDir.getFullPathName() << std::endl;
        return 1;
    }

    // Songs.
    std::vector<Song> songs;

    if (options.songs.contains ("pink"))
        songs.push_back (makePinkSong());

    if (options.songs.contains ("mix"))
        songs.push_back (makeMixSong());

    if (options.input != juce::File() && options.songs.contains ("input"))
    {
        Song song;

        if (! loadSong (options.input, options.offset, song))
        {
            std::cerr << "Cannot read " << options.input.getFullPathName() << std::endl;
            return 1;
        }

        songs.push_back (std::move (song));
    }

    const FrequencyScale scale;  // MiniMeters: log, 20 Hz - 20 kHz
    auto drawings = makeDrawings (scale);
    drawings.erase (std::remove_if (drawings.begin(), drawings.end(),
                                    [&options] (const Drawing& d) { return ! options.drawings.contains (d.name); }),
                    drawings.end());

    std::vector<CarveMode> modes;

    if (options.modes.contains ("carve"))
        modes.push_back (CarveMode::carve);

    if (options.modes.contains ("stencil"))
        modes.push_back (CarveMode::stencil);

    std::vector<int> randomBlocks;
    juce::Random random (1234);

    for (int i = 0; i < 4096; ++i)
        randomBlocks.push_back (1 + random.nextInt (2048));

    MaskSettings maskSettings;
    maskSettings.scale = scale;
    const auto clicks = makeClickSong();
    const auto emptyPlan = buildMaskPlan (Canvas(), maskSettings);

    double emptyCarve = 0.0, emptyStencil = 0.0;
    std::vector<CaseResult> results;
    std::vector<double> costs;

    for (const auto& song : songs)
    {
        std::cerr << "song " << song.name << std::endl;
        const auto songMono = mono (song.audio);

        if (options.sounds && ! writeWav (song.audio, options.outputDir.getChildFile (song.name + ".wav")))
            std::cerr << "Cannot write " << song.name << ".wav" << std::endl;
        std::vector<Display> inputs;

        for (const auto size : options.ffts)
            inputs.push_back (analyse (songMono, size));

        // Nothing drawn: the audio must come through untouched.
        for (const auto mode : modes)
        {
            CarveSettings settings;
            settings.mode = mode;
            settings.depthDb = options.depthDb;
            const auto run = runEngine (options.engine, song, emptyPlan.get(), settings, { 512 });
            auto& worst = mode == CarveMode::carve ? emptyCarve : emptyStencil;
            worst = std::max (worst, static_cast<double> (largestDifference (run.output, song.audio)));
        }

        for (const auto& drawing : drawings)
        {
            const auto plan = buildMaskPlan (drawing.canvas, maskSettings);

            if (options.pictures)
                writeCanvasPicture (options.outputDir.getChildFile ("canvas-" + drawing.name + ".png"), drawing.canvas);

            for (const auto mode : modes)
            {
                const juce::String modeName (mode == CarveMode::carve ? "carve" : "stencil");
                std::cerr << "  " << drawing.name << " " << modeName << std::endl;

                CarveSettings settings;
                settings.mode = mode;
                settings.depthDb = options.depthDb;

                const auto steady = runEngine (options.engine, song, plan.get(), settings, { 512 });
                CaseResult result;
                result.song = song.name;
                result.drawing = drawing.name;
                result.mode = modeName;
                result.costMsPerSecond = steady.seconds / songSeconds * 1000.0;
                costs.push_back (result.costMsPerSecond);

                if (options.randomBlocks)
                {
                    const auto varied = runEngine (options.engine, song, plan.get(), settings, randomBlocks);
                    result.blocks = largestDifference (steady.output, varied.output);
                }

                const auto margin = std::max (0.1, 2.0 * latency / sampleRate);
                result.outside = std::max (largestDifference (steady.output, song.audio, 0.0, passStart - margin),
                                           largestDifference (steady.output, song.audio, passStart + passLength + margin, songSeconds));

                const auto outMono = mono (steady.output);

                if (options.sounds)
                {
                    const auto name = song.name + "-" + drawing.name + "-" + modeName + ".wav";

                    if (! writeWav (steady.output, options.outputDir.getChildFile (name)))
                        std::cerr << "Cannot write " << name << std::endl;
                }

                if (! song.kicks.empty())
                    result.kick = kickSmear (song, *plan, songMono, outMono, result.kicks);

                // Stencil changes everything from the start of the pass to its end, so that is
                // where the output should start and stop differing from the input. (Carve's
                // start and end depend on the drawing's shape: its edges are measured in cells.)
                if (mode == CarveMode::stencil)
                    std::tie (result.lead, result.lag) = leadAndLag (song.audio, steady.output, passStart, passStart + passLength);

                // Clicks alone, for the pre-echo of a transient.
                const auto clickRun = runEngine (options.engine, clicks, plan.get(), settings, { 512 });
                result.clickPreEcho = clickPreEcho (clicks, *plan, clickRun.output, result.clicks);

                for (size_t a = 0; a < options.ffts.size(); ++a)
                {
                    const auto output = analyse (outMono, options.ffts[a]);
                    const auto map = mapCells (output, *plan, mode);
                    std::array<Metrics, numRanges> metrics;

                    for (int r = 0; r < numRanges; ++r)
                        metrics[static_cast<size_t> (r)] = measure (inputs[a], output, map, ranges[r], options.depthDb);

                    result.ffts.push_back (options.ffts[a]);
                    result.metrics.push_back (metrics);

                    if (options.pictures)
                    {
                        const auto name = song.name + "-" + drawing.name + "-" + modeName + "-" + juce::String (options.ffts[a]);
                        writeCasePicture (options.outputDir.getChildFile (name + ".png"),
                                          name + "   engine " + juce::String (options.engine) + ": " + engineProbe->getName(),
                                          inputs[a], output, map, options.depthDb);
                    }
                }

                results.push_back (std::move (result));
            }
        }
    }

    // ---- report ----
    const auto cost = median (costs);
    const auto costOf = [&results] (const char* mode)
    {
        std::vector<double> modeCosts;

        for (const auto& result : results)
            if (result.mode == mode)
                modeCosts.push_back (result.costMsPerSecond);

        return median (modeCosts);
    };

    juce::String text;
    text << "FreqArt carve lab\n"
         << "engine " << options.engine << ": " << engineProbe->getName() << "\n"
         << "latency " << latency << " samples (" << juce::String (latency / sampleRate * 1000.0, 1) << " ms at 48 kHz)\n"
         << "cost " << juce::String (cost, 2) << " ms of CPU per second of stereo audio (median over cases, blocks of 512; carve "
         << number (costOf ("carve"), 2, 0) << ", stencil " << number (costOf ("stencil"), 2, 0) << ")\n"
         << "depth " << juce::String (options.depthDb, 1) << " dB; one 8 s pass at 120 BPM from bar 2 (2 s - 10 s of 12 s)\n"
         << "transparency with nothing drawn: carve " << dbfs (emptyCarve) << ", stencil " << dbfs (emptyStencil) << "\n\n"
         << "Columns (dB unless noted; attenuation = input dB - output dB in the analyser):\n"
         << "  depth  median attenuation of target cells     vis   visibility gain vs the input\n"
         << "  r      fidelity (Pearson, attenuation vs drawing)  err   median |attenuation - share x depth| (touched cells)\n"
         << "  edgeT  10-90 % edge width in time, ms          edgeF same in frequency, display rows (600 = 20 Hz - 20 kHz)\n"
         << "  near / far / pre / post  attenuation of keep cells next to / far from / just before / just after the drawing\n"
         << "  noise  std of output - input over far keep cells   spread  IQR of the target attenuation\n\n";

    // Scorecard: the mean over songs and drawings per mode, analyser and range.
    text << "SCORECARD (mean over songs and drawings)\n";

    for (const auto& mode : options.modes)
    {
        for (size_t a = 0; a < options.ffts.size(); ++a)
        {
            text << "\n" << mode << ", FFT " << options.ffts[a] << "\n" << juce::String ("range").paddedRight (' ', 8) << metricHeader << "\n";

            for (int r = 0; r < numRanges; ++r)
            {
                std::vector<const Metrics*> set;

                for (const auto& result : results)
                    if (result.mode == mode && a < result.metrics.size())
                        set.push_back (&result.metrics[a][static_cast<size_t> (r)]);

                text << juce::String (ranges[r].name).paddedRight (' ', 8) << metricLine (meanOf (set)) << "\n";
            }
        }
    }

    text << "\nTIMING (mean over cases; lead / lag: ms before / after the pass, Stencil only; click pre-echo and kick\n"
            "smear: dB, see the top of tools/CarveLab.cpp)\n" << juce::String ("mode").paddedRight (' ', 8) << "   lead    lag  click   kick\n";

    for (const auto& mode : options.modes)
    {
        const auto mean = [&results, &mode] (double CaseResult::* field)
        {
            double sum = 0.0;
            int count = 0;

            for (const auto& result : results)
                if (result.mode == mode && std::isfinite (result.*field))
                {
                    sum += result.*field;
                    ++count;
                }

            return count > 0 ? sum / count : unmeasured;
        };

        text << mode.paddedRight (' ', 8) << number (mean (&CaseResult::lead)) << " " << number (mean (&CaseResult::lag)) << " "
             << number (mean (&CaseResult::clickPreEcho)) << " " << number (mean (&CaseResult::kick)) << "\n";
    }

    text << "\nCASES\n";

    for (const auto& result : results)
    {
        text << "\n" << result.song << " / " << result.drawing << " / " << result.mode
             << "   blocks " << dbfs (result.blocks) << ", outside the pass " << dbfs (result.outside)
             << ", lead " << number (result.lead, 1, 0) << " ms, lag " << number (result.lag, 1, 0)
             << " ms, click pre-echo " << number (result.clickPreEcho, 1, 0) << " dB (" << result.clicks << " clicks), kick smear "
             << number (result.kick, 1, 0) << " dB (" << result.kicks << " kicks), cost " << juce::String (result.costMsPerSecond, 2) << " ms/s\n"
             << juce::String ("fft").paddedRight (' ', 6) << juce::String ("range").paddedRight (' ', 8) << metricHeader << "\n";

        for (size_t a = 0; a < result.ffts.size(); ++a)
            for (int r = 0; r < numRanges; ++r)
                text << juce::String (result.ffts[a]).paddedRight (' ', 6) << juce::String (ranges[r].name).paddedRight (' ', 8)
                     << metricLine (result.metrics[a][static_cast<size_t> (r)]) << "\n";
    }

    std::cout << text << std::flush;
    options.outputDir.getChildFile ("metrics.txt").replaceWithText (text);

    // JSON.
    auto* root = new juce::DynamicObject();
    {
        auto* engine = new juce::DynamicObject();
        engine->setProperty ("index", options.engine);
        engine->setProperty ("name", juce::String (engineProbe->getName()));
        engine->setProperty ("latencySamples", latency);
        engine->setProperty ("latencyMs", latency / sampleRate * 1000.0);
        engine->setProperty ("costMsPerSecond", cost);
        engine->setProperty ("emptyDrawingMaxDifferenceCarve", emptyCarve);
        engine->setProperty ("emptyDrawingMaxDifferenceStencil", emptyStencil);
        engine->setProperty ("depthDb", options.depthDb);
        root->setProperty ("engine", juce::var (engine));
    }

    juce::Array<juce::var> cases;

    for (const auto& result : results)
    {
        auto* item = new juce::DynamicObject();
        item->setProperty ("song", result.song);
        item->setProperty ("drawing", result.drawing);
        item->setProperty ("mode", result.mode);
        item->setProperty ("blockSizeMaxDifference", toVar (result.blocks));
        item->setProperty ("outsidePassMaxDifference", result.outside);
        item->setProperty ("kickSmearDb", toVar (result.kick));
        item->setProperty ("kicksMeasured", result.kicks);
        item->setProperty ("leadMs", toVar (result.lead));
        item->setProperty ("clickPreEchoDb", toVar (result.clickPreEcho));
        item->setProperty ("clicksMeasured", result.clicks);
        item->setProperty ("lagMs", toVar (result.lag));
        item->setProperty ("costMsPerSecond", result.costMsPerSecond);

        juce::Array<juce::var> analysers;

        for (size_t a = 0; a < result.ffts.size(); ++a)
        {
            auto* analyser = new juce::DynamicObject();
            analyser->setProperty ("fft", result.ffts[a]);

            for (int r = 0; r < numRanges; ++r)
                analyser->setProperty (ranges[r].name, metricsVar (result.metrics[a][static_cast<size_t> (r)]));

            analysers.add (juce::var (analyser));
        }

        item->setProperty ("analysers", analysers);
        cases.add (juce::var (item));
    }

    root->setProperty ("cases", cases);
    options.outputDir.getChildFile ("metrics.json").replaceWithText (juce::JSON::toString (juce::var (root)));

    std::cerr << "Wrote " << options.outputDir.getFullPathName() << std::endl;
    return 0;
}

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_LINUX || JUCE_BSD
    // As in the snapshot tool: no application object, JUCE's tolerant X error handlers.
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return nullptr; };
   #endif

    const juce::ScopedJuceInitialiser_GUI gui;

    Options options;

    if (! parseOptions (argc, argv, options))
    {
        std::cerr << "Usage: FreqArtCarveLab <output-dir> [--engine N] [--input song.wav] [--offset seconds]\n"
                     "       [--songs pink,mix,input] [--drawings hello,sun,ramp,portrait] [--modes carve,stencil]\n"
                     "       [--ffts 4096,8192,16384] [--depth -60] [--quick] [--no-png] [--wav]" << std::endl;
        return 2;
    }

    return runLab (options);
}
