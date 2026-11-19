// The truth lab: are the screenshots honest? It plays the exact drawing and settings of a
// screenshot through the real plug-in (ArtFreqProcessor, activated against the test licensing
// environment, as a buyer's activated copy) and shows what an analyser such as MiniMeters makes
// of the sound, beside the canvas the screenshot shows.
//
//   ArtFreqTruthLab <snapshot-dir> <output-dir> [--states default,rich,...] [--range -100,0]
//                   [--ffts 4096,8192,16384] [--wav]
//   ArtFreqTruthLab --marketing <output-dir> [--range -100,0]
//
// <snapshot-dir> is a run of ArtFreqSnapshot with ARTFREQ_SNAPSHOT_STATES=1: for each state
// artfreq-<name>.png (the screenshot), artfreq-<name>.state (the processor's state as a host
// saves it) and artfreq-<name>.json (the plug-in, and where the canvas's drawing lies in the PNG).
//
// For each state it
//
//   1. loads the state into a fresh processor of the right kind (ArtFreq or ArtFreq FX), prepared
//      at 48 kHz in blocks of 512, and plays one pass the way a DAW does: 0.77 s with the transport
//      stopped, then playing from bar 1 at 120 BPM in 4/4 (the state's own Trigger, Start, Length
//      and tempo decide what happens: On play starts the pass there), for the pass and 1.25 s more.
//      ArtFreq FX gets a dense synthetic song (makeSong: drums, bass, chords, a voice, cymbals and
//      a bed of noise, mastered to -10 dBFS RMS), from the moment the transport starts; its output
//      is moved back by the latency it reports, as the DAW's delay compensation does;
//   2. analyses the output (the mean of the channels) as MiniMeters' spectrogram does: Hann
//      window, FFT 4096, 8192 and 16384 points, dB of the magnitude (a full-scale sine reads
//      0 dB), one analysis per pixel column of the canvas (a hop of 1/W of the pass: about 5 ms,
//      a smoothly scrolling display), frequency on the state's own scale (log 20 Hz - 20 kHz for
//      all but the linear screenshot), each pixel row reading the dB interpolated between the bin
//      centres around it, or the loudest bin inside it where it spans several; coloured with the
//      canvas's own colour map over a fixed range (default -100 to 0 dBFS);
//   3. measures, per band (lows < 250 Hz, low mids 250 Hz - 2 kHz, highs > 2 kHz), how the real
//      display compares with the canvas the screenshot shows (see BandResult);
//   4. writes <output-dir>/truth-<name>.png: the screenshot's canvas, cut out of the PNG, beside
//      the real display at 8192 points, the same size and frequency axis; under them a strip with
//      4096 and 16384 points, the same sound in the canvas's own colours (the instrument's loudest
//      at white over 60 dB; ArtFreq FX's -60 to 0 dB at 4096 points), and for ArtFreq FX the song
//      going in; and the numbers. Also
//      truth-<name>-fft<N>.png (each real display alone), metrics.txt and metrics.json, and with
//      --wav the rendered output, truth-<name>.wav.
//
// --marketing renders a few drawings for the website (the instrument and the effect), each as a
// real measurement of the plug-in's output (FFT 8192), at 2x (2400 x 1280), captioned as such:
// ArtFreq's default drawing, HELLO over WORLD at Density 1024, and ArtFreq FX carving its default
// drawing into the test song from bar 2 (so the song plays either side of the pass).
//
// On Linux run it under a virtual display (it renders text):  xvfb-run -a ArtFreqTruthLab ...

#include "LicenseTestSupport.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/Fonts.h"
#include "core/FrequencyScale.h"
#include "core/ImageProcessing.h"
#include "ui/CanvasPlanImage.h"
#include "ui/EffectPreview.h"
#include "ui/Theme.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace
{

using namespace artfreq;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr int stoppedBlocks = 72;                 // 0.77 s with the transport stopped
constexpr double tailSeconds = 1.25;              // played after the pass
constexpr double bpm = 120.0;
constexpr float floorDb = -160.0f;
const double nan = std::numeric_limits<double>::quiet_NaN();

template <typename Int>
constexpr size_t idx (Int i) noexcept
{
    return static_cast<size_t> (i);
}

using Stereo = std::array<std::vector<float>, 2>;

// ================================================================================================
// The song for ArtFreq FX
// ================================================================================================

/** Pink noise (Paul Kellet's filter). */
class PinkNoise
{
public:
    explicit PinkNoise (int seed) : random (seed) {}

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

struct OnePole
{
    explicit OnePole (double hz) : a (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate)) {}

    double lowPass (double x) { y += a * (x - y); return y; }
    double highPass (double x) { return x - lowPass (x); }

    double a, y = 0.0;
};

/** A two-pole band-pass (a formant). */
struct BandPass
{
    BandPass (double hz, double q)
    {
        const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        const auto alpha = std::sin (w) / (2.0 * q);
        const auto a0 = 1.0 + alpha;
        b0 = alpha / a0;
        a1 = -2.0 * std::cos (w) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process (double x)
    {
        const auto y = b0 * x - b0 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }

    double b0, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
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

/** A dense, loud synthetic master at 120 BPM: kick on every beat, snare on 2 and 4, sixteenth
    hats and a ride, a bass line, a chord pad, a sung-like lead (a saw through two formants with
    vibrato), and a bed of noise standing for reverb and everything else, so that every height of
    the analyser has music in it, as on a full mix. Mastered to `rmsDb` dBFS RMS with a soft
    clipper (peaks under 0 dBFS). Built after CarveLab's "mix", denser. */
Stereo makeSong (double seconds, double rmsDb)
{
    const auto samples = static_cast<int> (seconds * sampleRate);
    Stereo song;

    for (auto& channel : song)
        channel.assign (idx (samples), 0.0f);

    juce::Random random (2024);
    const auto noise = [&random] { return static_cast<double> (random.nextFloat()) * 2.0 - 1.0; };
    const auto add = [&song, samples] (int index, double left, double right)
    {
        if (index >= 0 && index < samples)
        {
            song[0][idx (index)] += static_cast<float> (left);
            song[1][idx (index)] += static_cast<float> (right);
        }
    };

    constexpr double beat = 60.0 / bpm;
    constexpr double bar = 4.0 * beat;
    const auto samplesOf = [] (double s) { return static_cast<int> (std::lround (s * sampleRate)); };

    // Kick: a sine falling from about 170 Hz to 48 Hz, and a click.
    for (auto onset = 0.0; onset < seconds; onset += beat)
    {
        const auto start = samplesOf (onset);
        auto phase = 0.0;

        for (int i = 0; i < samplesOf (0.45); ++i)
        {
            const auto t = i / sampleRate;
            phase += juce::MathConstants<double>::twoPi * (48.0 + 120.0 * std::exp (-t / 0.035)) / sampleRate;
            auto value = 0.55 * std::min (1.0, i / 16.0) * std::exp (-t / 0.22) * std::sin (phase);

            if (i < 96)
                value += 0.15 * noise() * (1.0 - i / 96.0);

            add (start + i, value, value);
        }
    }

    // Snare on 2 and 4.
    for (auto onset = beat; onset < seconds; onset += 2.0 * beat)
    {
        const auto start = samplesOf (onset);
        OnePole highPass (700.0);

        for (int i = 0; i < samplesOf (0.3); ++i)
        {
            const auto t = i / sampleRate;
            const auto value = 0.26 * std::exp (-t / 0.12) * highPass.highPass (noise())
                             + 0.12 * std::exp (-t / 0.05) * std::sin (juce::MathConstants<double>::twoPi * 185.0 * t);
            add (start + i, value * 0.9, value);
        }
    }

    // Sixteenth hats, accented on the offbeats.
    for (int step = 0; step * beat / 4.0 < seconds; ++step)
    {
        const auto start = samplesOf (step * beat / 4.0);
        const auto level = step % 4 == 2 ? 0.14 : 0.07;
        OnePole first (7000.0), second (7000.0);

        for (int i = 0; i < samplesOf (0.08); ++i)
        {
            const auto value = level * std::exp (-(i / sampleRate) / 0.02) * second.highPass (first.highPass (noise()));
            add (start + i, value * 0.8, value);
        }
    }

    // A ride on every beat, long and bright.
    for (auto onset = 0.0; onset < seconds; onset += beat)
    {
        const auto start = samplesOf (onset);
        OnePole first (3500.0);

        for (int i = 0; i < samplesOf (beat); ++i)
        {
            const auto value = 0.05 * std::exp (-(i / sampleRate) / 0.35) * first.highPass (noise());
            add (start + i, value, value * 0.8);
        }
    }

    // Bass line and chords: Am, F, C, G.
    const double roots[] { 55.0, 43.65, 65.41, 49.0 };
    const double chords[][4] { { 220.0, 261.63, 329.63, 440.0 }, { 174.61, 220.0, 261.63, 349.23 },
                               { 261.63, 329.63, 392.0, 523.25 }, { 196.0, 246.94, 293.66, 392.0 } };
    const double pattern[] { 1.0, 1.0, 2.0, 1.0, 1.0, 1.5, 2.0, 1.0 };

    {
        Saw saw;
        OnePole first (420.0), second (420.0);
        const auto noteLength = beat / 2.0;

        for (int i = 0; i < samples; ++i)
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

    for (int voice = 0; voice < 4; ++voice)
    {
        for (const auto detune : { 0.995, 1.005 })
        {
            Saw saw;
            OnePole first (3200.0), second (3200.0);
            const auto pan = 0.6 + 0.25 * voice;

            for (int i = 0; i < samples; ++i)
            {
                const auto t = i / sampleRate;
                const auto bars = static_cast<int> (t / bar);
                const auto inBar = std::fmod (t, bar);
                const auto envelope = std::min (1.0, inBar / 0.06) * std::min (1.0, (bar - inBar) / 0.08);
                const auto value = 0.045 * envelope * second.lowPass (first.lowPass (saw.next (chords[bars % 4][voice] * detune)));
                add (i, value * pan, value * (2.0 - pan));
            }
        }
    }

    // A lead voice: eighth and quarter notes over the chords, vibrato, two formants.
    {
        const double melody[] { 659.25, 587.33, 523.25, 587.33, 659.25, 783.99, 659.25, 587.33,
                                523.25, 440.0, 523.25, 587.33, 523.25, 493.88, 440.0, 392.0 };
        Saw saw;
        BandPass formant1 (700.0, 4.0), formant2 (2200.0, 6.0);
        double hz = melody[0];

        for (int i = 0; i < samples; ++i)
        {
            const auto t = i / sampleRate;
            const auto step = static_cast<int> (t / (beat / 2.0));
            const auto inStep = std::fmod (t, beat / 2.0);
            hz += (melody[step % 16] - hz) * 0.002;
            const auto vibrato = 1.0 + 0.008 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t);
            const auto envelope = std::min (1.0, inStep / 0.02) * std::min (1.0, (beat / 2.0 - inStep) / 0.03 + 0.4);
            const auto source = saw.next (hz * vibrato) + 0.15 * noise();
            const auto value = 0.30 * envelope * (formant1.process (source) + 0.6 * formant2.process (source));
            add (i, value * 0.95, value);
        }
    }

    // The bed: reverb, room, everything else.
    {
        PinkNoise left (303), right (404);

        for (int i = 0; i < samples; ++i)
            add (i, 0.05 * left.next(), 0.05 * right.next());
    }

    // Mastering: to the level, through a soft clipper.
    const auto measure = [&song]
    {
        double sum = 0.0;

        for (const auto& channel : song)
            for (const auto x : channel)
                sum += static_cast<double> (x) * x;

        return std::sqrt (sum / static_cast<double> (2 * song[0].size()));
    };

    for (int pass = 0; pass < 4; ++pass)
    {
        const auto gain = std::pow (10.0, rmsDb / 20.0) / std::max (measure(), 1e-12);

        for (auto& channel : song)
            for (auto& x : channel)
                x = static_cast<float> (0.97 * std::tanh (gain * x / 0.97));
    }

    return song;
}

// ================================================================================================
// Playing a state through the processor
// ================================================================================================

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

struct Rendered
{
    std::vector<float> output;  // mono (the channels' mean), on the input's timeline
    std::vector<float> input;   // ArtFreq FX: the song going in (mono), on the same timeline
    Stereo stereo;              // the output, both channels
    int passStart = 0;          // the transport starts here
    int passSamples = 0;
    int latency = 0;
};

/** `passOffsetSeconds`: where the state puts the pass after the transport starts (0 for On play;
    (bar - 1) bars for At bar). */
Rendered play (ArtFreqProcessor& processor, const Stereo* song, double passOffsetSeconds = 0.0)
{
    Rendered r;
    const auto transportStart = stoppedBlocks * blockSize;
    r.passStart = transportStart + static_cast<int> (std::lround (passOffsetSeconds * sampleRate));
    r.passSamples = static_cast<int> (std::lround (processor.getLengthSeconds() * sampleRate));
    r.latency = processor.getLatencySamples();

    const auto timeline = r.passStart + r.passSamples + static_cast<int> (tailSeconds * sampleRate);
    const auto total = timeline + r.latency;

    TestPlayHead head;
    processor.setPlayHead (&head);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    Stereo out;

    for (auto& channel : out)
        channel.assign (idx (total), 0.0f);

    if (song != nullptr)
        r.input.assign (idx (timeline), 0.0f);

    for (int done = 0; done < total; done += blockSize)
    {
        const auto n = std::min (blockSize, total - done);
        const auto playing = done >= transportStart;
        const auto seconds = (done - transportStart) / sampleRate;
        const auto ppq = seconds * bpm / 60.0;

        head.info = {};
        head.info.setIsPlaying (playing);
        head.info.setBpm (bpm);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        head.info.setPpqPosition (playing ? ppq : 0.0);
        head.info.setPpqPositionOfLastBarStart (playing ? 4.0 * std::floor (ppq / 4.0) : 0.0);
        head.info.setTimeInSeconds (playing ? seconds : 0.0);
        head.info.setTimeInSamples (playing ? static_cast<int64_t> (done - transportStart) : 0);

        buffer.setSize (2, n, false, false, true);
        buffer.clear();

        // The song plays while the transport does, as a track in the DAW.
        if (song != nullptr && playing)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const auto at = done + i - transportStart;

                    if (at >= 0 && at < static_cast<int> ((*song)[idx (ch)].size()))
                        buffer.setSample (ch, i, (*song)[idx (ch)][idx (at)]);
                }

            for (int i = 0; i < n && done + i < timeline; ++i)
                r.input[idx (done + i)] = 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i));
        }

        midi.clear();
        processor.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out[idx (ch)].begin() + done);
    }

    processor.setPlayHead (nullptr);

    // Moved back by the latency, as the DAW's delay compensation does.
    for (int ch = 0; ch < 2; ++ch)
        r.stereo[idx (ch)].assign (out[idx (ch)].begin() + r.latency, out[idx (ch)].begin() + r.latency + timeline);

    r.output.resize (idx (timeline));

    for (size_t i = 0; i < r.output.size(); ++i)
        r.output[i] = 0.5f * (r.stereo[0][i] + r.stereo[1][i]);

    return r;
}

// ================================================================================================
// The analyser
// ================================================================================================

/** What an analyser shows: W columns across [fromSample, toSample) of the signal, H rows on the
    scale (row 0 = top), in dB (a full-scale sine reads 0 dB). */
struct Display
{
    int width = 0, height = 0, fftSize = 0;
    std::vector<float> db;  // [y * width + x]

    float at (int x, int y) const { return db[idx (y) * idx (width) + idx (x)]; }
};

Display analyse (const std::vector<float>& signal, double fromSample, double toSample, int fftSize,
                 const FrequencyScale& scale, int width, int height)
{
    Display d;
    d.width = width;
    d.height = height;
    d.fftSize = fftSize;
    d.db.assign (idx (width) * idx (height), floorDb);

    std::vector<float> window (idx (fftSize));
    double windowSum = 0.0;

    for (int n = 0; n < fftSize; ++n)
    {
        window[idx (n)] = static_cast<float> (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / fftSize));
        windowSum += window[idx (n)];
    }

    const auto toAmplitude = static_cast<float> (2.0 / windowSum);
    const auto numBins = fftSize / 2 + 1;
    const auto lastBin = numBins - 1;
    const auto binsPerHz = fftSize / sampleRate;
    const auto s = scale.sanitised();

    struct RowMap { int below; float fraction; int firstInside, lastInside; };
    std::vector<RowMap> rows (idx (height));

    for (int y = 0; y < height; ++y)
    {
        const auto centre = s.toHz (1.0 - (y + 0.5) / height) * binsPerHz;
        const auto low = s.toHz (1.0 - (y + 1.0) / height) * binsPerHz;
        const auto high = s.toHz (1.0 - static_cast<double> (y) / height) * binsPerHz;
        auto& row = rows[idx (y)];
        row.below = juce::jlimit (0, lastBin - 1, static_cast<int> (std::floor (centre)));
        row.fraction = static_cast<float> (juce::jlimit (0.0, 1.0, centre - row.below));
        row.firstInside = juce::jlimit (0, lastBin + 1, static_cast<int> (std::ceil (low)));
        row.lastInside = juce::jlimit (-1, lastBin, static_cast<int> (std::ceil (high)) - 1);
    }

    juce::dsp::FFT fft (juce::roundToInt (std::log2 (fftSize)));
    std::vector<float> work (idx (fftSize) * 2), bins (idx (numBins));

    for (int x = 0; x < width; ++x)
    {
        const auto centre = static_cast<int> (std::lround (fromSample + (x + 0.5) / width * (toSample - fromSample)));
        const auto first = centre - fftSize / 2;

        std::fill (work.begin(), work.end(), 0.0f);

        for (int n = 0; n < fftSize; ++n)
        {
            const auto at = first + n;

            if (at >= 0 && at < static_cast<int> (signal.size()))
                work[idx (n)] = signal[idx (at)] * window[idx (n)];
        }

        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        for (int b = 0; b < numBins; ++b)
            bins[idx (b)] = std::max (floorDb, 20.0f * std::log10 (std::max (work[idx (b)] * toAmplitude, 1.0e-12f)));

        for (int y = 0; y < height; ++y)
        {
            const auto& row = rows[idx (y)];
            auto value = bins[idx (row.below)] + (bins[idx (row.below + 1)] - bins[idx (row.below)]) * row.fraction;

            for (int b = row.firstInside; b <= row.lastInside; ++b)
                value = std::max (value, bins[idx (b)]);

            d.db[idx (y) * idx (width) + idx (x)] = value;
        }
    }

    return d;
}

juce::Image displayImage (const Display& d, float lowDb, float highDb)
{
    const auto& table = ui::theme::spectrumTable();
    juce::Image image (juce::Image::RGB, d.width, d.height, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < d.height; ++y)
        for (int x = 0; x < d.width; ++x)
        {
            const auto t = (d.at (x, y) - lowDb) / (highDb - lowDb);
            const auto level = juce::jlimit (0, 255, static_cast<int> (t * 255.0f + 0.5f));
            const auto& p = table[idx (level)];
            data.setPixelColour (x, y, juce::Colour (p.getRed(), p.getGreen(), p.getBlue()));
        }

    return image;
}

// ================================================================================================
// What the canvas shows, on the same grid
// ================================================================================================

/** The canvas's colour-map level (0..255) of each pixel of an image drawn with the colour map. */
std::vector<uint8_t> levelsOf (const juce::Image& image)
{
    const auto& table = ui::theme::spectrumTable();
    std::map<uint32_t, uint8_t> cache;
    std::vector<uint8_t> levels (idx (image.getWidth()) * idx (image.getHeight()));
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto c = data.getPixelColour (x, y);
            const auto key = c.getARGB() & 0xffffffu;
            auto found = cache.find (key);

            if (found == cache.end())
            {
                auto best = 0, bestDistance = std::numeric_limits<int>::max();

                for (int i = 0; i < 256; ++i)
                {
                    const auto& p = table[idx (i)];
                    const auto dr = p.getRed() - c.getRed(), dg = p.getGreen() - c.getGreen(), db = p.getBlue() - c.getBlue();
                    const auto distance = dr * dr + dg * dg + db * db;

                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = i;
                    }
                }

                found = cache.emplace (key, static_cast<uint8_t> (best)).first;
            }

            levels[idx (y) * idx (image.getWidth()) + idx (x)] = found->second;
        }

    return levels;
}

/** The drawing (0..1) over each cell of a W x H grid: the canvas's area mean. */
std::vector<float> drawingOnGrid (const Canvas& canvas, int width, int height)
{
    std::vector<float> grid (idx (width) * idx (height));

    for (int y = 0; y < height; ++y)
    {
        const auto y0 = static_cast<double> (y) * Canvas::height / height, y1 = (y + 1.0) * Canvas::height / height;

        for (int x = 0; x < width; ++x)
        {
            const auto x0 = static_cast<double> (x) * Canvas::width / width, x1 = (x + 1.0) * Canvas::width / width;
            double sum = 0.0, weight = 0.0;

            for (auto cy = static_cast<int> (std::floor (y0)); cy < std::min (Canvas::height, static_cast<int> (std::ceil (y1))); ++cy)
            {
                const auto wy = std::min (y1, cy + 1.0) - std::max (y0, static_cast<double> (cy));

                for (auto cx = static_cast<int> (std::floor (x0)); cx < std::min (Canvas::width, static_cast<int> (std::ceil (x1))); ++cx)
                {
                    const auto wx = std::min (x1, cx + 1.0) - std::max (x0, static_cast<double> (cx));
                    sum += wx * wy * canvas.get (cx, cy);
                    weight += wx * wy;
                }
            }

            grid[idx (y) * idx (width) + idx (x)] = weight > 0.0 ? static_cast<float> (sum / weight) : 0.0f;
        }
    }

    return grid;
}

// ================================================================================================
// The comparison
// ================================================================================================

struct BandDef
{
    const char* name;
    double lowHz, highHz;
};

constexpr std::array<BandDef, 3> bands { { { "lows", 0.0, 250.0 }, { "low-mids", 250.0, 2000.0 }, { "highs", 2000.0, 1.0e9 } } };

/** One band of one display against the canvas. Levels are in dB: the real display's own, and
    the canvas's colour level turned back into dB (CanvasPlanImage: 60 dB below its top; the
    instrument's top is the plan's loudest, ArtFreq FX's a full-scale sine in a cell at FFT 4096).
    "Drawn" cells: the drawing covers at least half of the cell; "around": the drawing covers
    nothing there, in a column where the band has drawn cells, and the nearest drawn cell is more
    than 3 % of the height away (so edges count for neither). NaN: nothing to measure. */
struct BandResult
{
    int drawn = 0, around = 0;
    double r = nan;              // shape: Pearson r over the band's cells. Instrument: the real display (60 dB
                                 // under its top, clipped) against the canvas. ArtFreq FX, whose canvas stands
                                 // in for a song it does not have: the drawing (0..1) against what the effect
                                 // did to the real song there (song - output in Carve, clipped to 0..80 dB;
                                 // the same against 1 - drawing in Stencil; output - song, -40..40 dB, in the
                                 // Draw modes)
    double insideDb = nan;       // median real dB of the drawn cells (dBFS in an analyser cell)
    double aroundDb = nan;       // ... of the cells around
    double contrastDb = nan;     // inside - around (instrument: how far shapes stand out; FX Carve: negative)
    double canvasContrastDb = nan;  // the same on the canvas (capped by its 60 dB window)
    double canvasAroundDb = nan;    // the canvas's level around the drawing (ArtFreq FX: its stand-in song,
                                    // in dB of a cell at FFT 4096, against aroundDb: the real song there)
    double songMedianDb = nan;      // FX: the song going in over the band, median and 95th percentile
    double songP95Db = nan;
    double kept = nan;           // instrument: share of drawn cells no more than 12 dB darker than the canvas
                                 // shows (each relative to its own top); FX: see below
    double stray = nan;          // share of "around" cells the real display shows within 30 dB of its top
                                 // (instrument) / of the drawn level (FX), where the canvas shows them dark
    double attenuationDb = nan;  // FX: median (song - output) dB over the drawn cells
    double darkShare = nan;      // FX Carve / Carve + Draw's hole: share of drawn cells 30 dB or more below the song
    double songKept = nan;       // FX Stencil: share of drawn cells within 6 dB of the song
};

double median (std::vector<double> v)
{
    if (v.empty())
        return nan;

    std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2), v.end());
    return v[v.size() / 2];
}

double percentile (std::vector<double> v, double p)
{
    if (v.empty())
        return nan;

    const auto k = static_cast<size_t> (std::lround (p * static_cast<double> (v.size() - 1)));
    std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (k), v.end());
    return v[k];
}

double pearson (const std::vector<double>& a, const std::vector<double>& b)
{
    if (a.size() < 2)
        return nan;

    double ma = 0, mb = 0;

    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }

    ma /= static_cast<double> (a.size());
    mb /= static_cast<double> (b.size());
    double ab = 0, aa = 0, bb = 0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        ab += (a[i] - ma) * (b[i] - mb);
        aa += (a[i] - ma) * (a[i] - ma);
        bb += (b[i] - mb) * (b[i] - mb);
    }

    return aa > 0 && bb > 0 ? ab / std::sqrt (aa * bb) : nan;
}

struct Comparison
{
    std::array<BandResult, bands.size()> band;
    double topDb = nan;  // the real display's top (99.9th percentile)
};

Comparison compare (const Display& real, const Display* song, const std::vector<uint8_t>& canvasLevels,
                    const std::vector<float>& drawing, const FrequencyScale& scale, bool effect, EffectMode mode)
{
    Comparison c;
    const auto w = real.width, h = real.height;
    const auto s = scale.sanitised();

    {
        std::vector<double> all (real.db.begin(), real.db.end());
        c.topDb = percentile (all, 0.999);
    }

    const auto canvasDb = [&] (int x, int y)
    {
        return static_cast<double> (canvasLevels[idx (y) * idx (w) + idx (x)]) / 255.0 * 60.0 - 60.0;
    };

    const auto drawnAt = [&] (int x, int y) { return drawing[idx (y) * idx (w) + idx (x)] >= 0.5f; };
    const auto clearAt = [&] (int x, int y) { return drawing[idx (y) * idx (w) + idx (x)] <= 0.02f; };
    const auto gap = std::max (2, static_cast<int> (0.03 * h));

    // Per column, the distance in rows to the nearest drawn cell.
    std::vector<int> distance (idx (w) * idx (h), std::numeric_limits<int>::max() / 2);

    for (int x = 0; x < w; ++x)
    {
        auto last = -1000000;

        for (int y = 0; y < h; ++y)
        {
            if (drawnAt (x, y))
                last = y;

            distance[idx (y) * idx (w) + idx (x)] = y - last;
        }

        last = 1000000;

        for (int y = h - 1; y >= 0; --y)
        {
            if (drawnAt (x, y))
                last = y;

            auto& d = distance[idx (y) * idx (w) + idx (x)];
            d = std::min (d, last - y);
        }
    }

    for (size_t b = 0; b < bands.size(); ++b)
    {
        auto& result = c.band[b];
        std::vector<double> realValues, canvasValues, inside, around, canvasInside, canvasAround, attenuation, shapeX, shapeY, songCells;
        int kept = 0, dark = 0, songKept = 0;

        for (int y = 0; y < h; ++y)
        {
            const auto hz = s.toHz (1.0 - (y + 0.5) / h);

            if (hz < bands[b].lowHz || hz >= bands[b].highHz)
                continue;

            for (int x = 0; x < w; ++x)
            {
                const auto realDb = static_cast<double> (real.at (x, y));
                const auto relative = std::max (-60.0, realDb - c.topDb);
                const auto cDb = canvasDb (x, y);

                // The instrument's canvas is relative to its loudest; ArtFreq FX's is in dB of a
                // cell (at FFT 4096), as the real display is.
                if (! effect)
                {
                    realValues.push_back (relative);
                    canvasValues.push_back (cDb);
                }
                else if (song != nullptr)
                {
                    songCells.push_back (static_cast<double> (song->at (x, y)));
                    const auto change = static_cast<double> (song->at (x, y)) - realDb;
                    const auto value = static_cast<double> (drawing[idx (y) * idx (w) + idx (x)]);

                    if (mode == EffectMode::carve)
                        shapeX.push_back (value), shapeY.push_back (juce::jlimit (0.0, 80.0, change));
                    else if (mode == EffectMode::stencil)
                        shapeX.push_back (1.0 - value), shapeY.push_back (juce::jlimit (0.0, 80.0, change));
                    else
                        shapeX.push_back (value), shapeY.push_back (juce::jlimit (-40.0, 40.0, -change));
                }

                if (drawnAt (x, y))
                {
                    ++result.drawn;
                    inside.push_back (realDb);
                    canvasInside.push_back (cDb);

                    if (! effect && relative >= cDb - 12.0)
                        ++kept;

                    if (song != nullptr)
                    {
                        const auto a = static_cast<double> (song->at (x, y)) - realDb;
                        attenuation.push_back (a);
                        dark += a >= 30.0 ? 1 : 0;
                        songKept += std::abs (a) <= 6.0 ? 1 : 0;
                    }
                }
                else if (clearAt (x, y) && distance[idx (y) * idx (w) + idx (x)] > gap)
                {
                    around.push_back (realDb);
                    canvasAround.push_back (cDb);
                    ++result.around;
                }
            }
        }

        if (result.drawn < 50)
            continue;

        result.r = effect ? pearson (shapeX, shapeY) : pearson (realValues, canvasValues);
        result.insideDb = median (inside);
        result.aroundDb = median (around);
        result.contrastDb = result.insideDb - result.aroundDb;
        result.canvasContrastDb = median (canvasInside) - median (canvasAround);
        result.canvasAroundDb = median (canvasAround);
        result.songMedianDb = median (songCells);
        result.songP95Db = percentile (songCells, 0.95);

        if (! effect)
        {
            result.kept = static_cast<double> (kept) / result.drawn;
            int bright = 0;

            for (const auto v : around)
                bright += v - c.topDb > -30.0 ? 1 : 0;

            result.stray = around.empty() ? nan : static_cast<double> (bright) / static_cast<double> (around.size());
        }
        else if (song != nullptr)
        {
            result.attenuationDb = median (attenuation);
            result.darkShare = static_cast<double> (dark) / result.drawn;
            result.songKept = static_cast<double> (songKept) / result.drawn;
        }
    }

    return c;
}

// ================================================================================================
// Pictures
// ================================================================================================

juce::Colour ink()   { return juce::Colour (0xffe8ebf2); }
juce::Colour dim()   { return juce::Colour (0xff8c94a6); }
juce::Colour faint() { return juce::Colour (0xff5a6274); }
juce::Colour paper() { return juce::Colour (0xff0b0d12); }

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}

/** The scale's grid labels on the left of `area`, and faint band edges at 250 Hz and 2 kHz. */
void drawAxis (juce::Graphics& g, juce::Rectangle<float> area, const FrequencyScale& scale, float fontSize, bool bandEdges)
{
    const auto s = scale.sanitised();
    g.setFont (fonts::mono (fontSize));

    for (const auto hz : gridFrequencies (s))
    {
        const auto y = area.getBottom() - static_cast<float> (s.toNorm (hz)) * area.getHeight();
        g.setColour (dim());
        const auto label = hz >= 1000.0 ? juce::String (hz / 1000.0, std::fmod (hz, 1000.0) > 1.0 ? 1 : 0) + "k"
                                        : juce::String (juce::roundToInt (hz));
        g.drawText (label, juce::Rectangle<float> (area.getX() - fontSize * 4.2f, y - fontSize, fontSize * 3.8f, fontSize * 2.0f),
                    juce::Justification::centredRight, false);
        g.setColour (faint());
        g.fillRect (area.getX() - fontSize * 0.35f, y - 0.5f, fontSize * 0.3f, 1.0f);
    }

    if (bandEdges)
    {
        for (const auto hz : { 250.0, 2000.0 })
        {
            const auto y = area.getBottom() - static_cast<float> (s.toNorm (hz)) * area.getHeight();
            g.setColour (juce::Colours::white.withAlpha (0.55f));

            for (auto x = area.getX(); x < area.getRight(); x += fontSize * 1.2f)
                g.fillRect (x, y - 0.75f, fontSize * 0.5f, 1.5f);
        }
    }
}

void drawPanel (juce::Graphics& g, const juce::Image& picture, juce::Rectangle<float> area, const juce::String& title,
                const FrequencyScale& scale, float fontSize, bool bandEdges)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (picture, area);
    g.setColour (juce::Colour (0xff262b36));
    g.drawRect (area.expanded (1.0f), 1.0f);
    drawAxis (g, area, scale, fontSize, bandEdges);
    g.setColour (ink());
    g.setFont (fonts::ui (fontSize * 1.25f, fonts::UiWeight::semiBold));
    g.drawText (title, juce::Rectangle<float> (area.getX(), area.getY() - fontSize * 2.1f, area.getWidth(), fontSize * 1.8f),
                juce::Justification::bottomLeft, true);
}

juce::String num (double v, int decimals = 0, const char* unit = "")
{
    if (! std::isfinite (v))
        return "-";

    return (decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals)) + unit;
}

juce::String pct (double v)
{
    return std::isfinite (v) ? juce::String (juce::roundToInt (v * 100.0)) + " %" : "-";
}

// ================================================================================================
// One state
// ================================================================================================

struct Options
{
    juce::File snapshotDir, outputDir;
    juce::StringArray states { "default", "rich", "layers", "on-play", "linear", "150",
                               "fx-default", "fx-carve", "fx-carve-draw", "fx-stencil" };
    std::vector<int> ffts { 4096, 8192, 16384 };
    float lowDb = -100.0f, highDb = 0.0f;
    bool wav = false;
    bool marketing = false;
};

const char* modeName (EffectMode mode)
{
    switch (mode)
    {
        case EffectMode::carve: return "Carve";
        case EffectMode::carveDraw: return "Carve + Draw";
        case EffectMode::stencil: return "Stencil";
        case EffectMode::draw: return "Draw";
    }

    return "?";
}

juce::String settingsLine (ArtFreqProcessor& p)
{
    const auto raw = [&p] (const char* id) { return p.getParameters().getRawParameterValue (id)->load(); };
    const auto settings = p.getPlanSettings();
    const auto scale = p.getScale();
    juce::String line;

    if (p.isEffect())
        line << "ArtFreq FX, " << modeName (p.getEffectMode()) << ", Depth " << num (p.getDepthDb()) << " dB, ";
    else
        line << "ArtFreq, ";

    line << "density " << settings.density << ", contrast " << num (settings.contrastDb) << " dB, Lows "
         << (settings.lowEnd == LowEnd::smooth ? "Smooth" : "Stripes") << ", Level " << num (raw (params::id::level), 1) << " dB, "
         << num (p.getLengthSeconds(), 0) << " s pass, "
         << (scale.type == ScaleType::log ? "log" : scale.type == ScaleType::linear ? "linear" : "mel") << " "
         << num (scale.minHz) << " Hz - " << num (scale.maxHz / 1000.0) << " kHz";
    return line;
}

struct StateResult
{
    juce::String name, settings;
    bool effect = false;
    EffectMode mode = EffectMode::carve;
    std::map<int, Comparison> byFft;
    double outputRmsDb = nan, outputPeakDb = nan, songRmsDb = nan;
};

double rmsDb (const std::vector<float>& v, int from, int to)
{
    double sum = 0.0;

    for (int i = from; i < to; ++i)
        sum += static_cast<double> (v[idx (i)]) * v[idx (i)];

    return 10.0 * std::log10 (std::max (1e-20, sum / std::max (1, to - from)));
}

double peakDb (const Stereo& v, int from, int to)
{
    double peak = 0.0;

    for (const auto& channel : v)
        for (int i = from; i < to; ++i)
            peak = std::max (peak, std::abs (static_cast<double> (channel[idx (i)])));

    return 20.0 * std::log10 (std::max (1e-10, peak));
}

bool writeWav (const Stereo& audio, const juce::File& file)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
        return false;

    juce::WavAudioFormat format;
    const auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                        .withNumChannels (2)
                                                                                        .withBitsPerSample (24));

    if (writer == nullptr)
        return false;

    const float* channels[] { audio[0].data(), audio[1].data() };
    return writer->writeFromFloatArrays (channels, 2, static_cast<int> (audio[0].size()));
}

/** Loads a state into a processor and makes its plan (and ArtFreq FX's masks). */
std::unique_ptr<ArtFreqProcessor> makeProcessor (bool effect, const juce::MemoryBlock& state)
{
    auto p = std::make_unique<ArtFreqProcessor> (effect ? ArtFreqProcessor::Variant::effect : ArtFreqProcessor::Variant::instrument);
    p->setPlayConfigDetails (effect ? 2 : 0, 2, sampleRate, blockSize);
    p->prepareToPlay (sampleRate, blockSize);

    if (state.getSize() > 0)
        p->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    p->rebuildPlanIfNeeded();
    return p;
}

bool runState (const Options& options, const juce::String& name, const Stereo& song, StateResult& result, juce::String& error)
{
    const auto base = options.snapshotDir.getChildFile ("artfreq-" + name);
    const auto json = juce::JSON::parse (base.withFileExtension ("json"));
    juce::MemoryBlock state;

    if (! json.isObject() || ! base.withFileExtension ("state").loadFileAsData (state))
    {
        error = "no state for " + name + " in " + options.snapshotDir.getFullPathName() + " (run ArtFreqSnapshot with ARTFREQ_SNAPSHOT_STATES=1)";
        return false;
    }

    const auto screenshot = juce::ImageFileFormat::loadFrom (base.withFileExtension ("png"));
    const auto effect = json["plugin"].toString() == "effect";
    const auto pixelScale = static_cast<float> (static_cast<double> (json["pixelScale"]));
    const auto plot = juce::Rectangle<double> (json["plotX"], json["plotY"], json["plotWidth"], json["plotHeight"]);
    const auto w = juce::roundToInt (plot.getWidth()), h = juce::roundToInt (plot.getHeight());

    if (! screenshot.isValid() || w < 16 || h < 16)
    {
        error = "no screenshot or canvas for " + name;
        return false;
    }

    auto processor = makeProcessor (effect, state);
    auto& p = *processor;

    if (! p.isLicensed())
    {
        error = "the processor is not activated (the test licensing environment)";
        return false;
    }

    result.name = name;
    result.effect = effect;
    result.mode = p.getEffectMode();
    result.settings = settingsLine (p);

    const auto scale = p.getScale();
    const auto plan = p.getCurrentPlan();

    // ---- what the canvas shows, as levels, at the screenshot's size ----------------------------
    ui::CanvasPlanImage planImage;
    auto picture = planImage.get (plan, scale, w, h, pixelScale);
    std::vector<uint8_t> canvasLevels = planImage.getPixelLevels();

    if (effect)
    {
        ui::EffectPreviewImage preview;
        picture = preview.get (planImage.getPixelLevels(), planImage.getRenderCount(), p.getEffectMode(), p.getDepthDb(),
                               ui::EffectPreviewImage::sinesDbFor (planImage.getLoudestAmplitude(), p.getOutputGain()),
                               p.getCurrentMasks(), w, h);
        canvasLevels = levelsOf (picture);
    }

    Canvas canvas;
    Compositor compositor;
    compositor.render (p.getDocument(), scale, canvas);
    const auto drawing = drawingOnGrid (canvas, w, h);

    // ---- the sound -----------------------------------------------------------------------------
    const auto rendered = play (p, effect ? &song : nullptr);
    const auto from = static_cast<double> (rendered.passStart);
    const auto to = from + rendered.passSamples;
    result.outputRmsDb = rmsDb (rendered.output, rendered.passStart, rendered.passStart + rendered.passSamples);
    result.outputPeakDb = peakDb (rendered.stereo, rendered.passStart, rendered.passStart + rendered.passSamples);

    if (effect)
        result.songRmsDb = rmsDb (rendered.input, rendered.passStart, rendered.passStart + rendered.passSamples);

    if (options.wav)
        writeWav (rendered.stereo, options.outputDir.getChildFile ("truth-" + name + ".wav"));

    std::map<int, Display> displays, songs;

    for (const auto n : options.ffts)
    {
        displays[n] = analyse (rendered.output, from, to, n, scale, w, h);

        if (effect)
            songs[n] = analyse (rendered.input, from, to, n, scale, w, h);

        result.byFft[n] = compare (displays[n], effect ? &songs[n] : nullptr, canvasLevels, drawing, scale, effect, p.getEffectMode());
        writePng (displayImage (displays[n], options.lowDb, options.highDb),
                  options.outputDir.getChildFile ("truth-" + name + "-fft" + juce::String (n) + ".png"));
    }

    // ---- the side by side ----------------------------------------------------------------------
    const auto crop = screenshot.getClippedImage (plot.toNearestInt());
    const auto mainFft = displays.count (8192) != 0 ? 8192 : options.ffts.front();
    const auto f = 13.0f * pixelScale / 2.0f * 2.0f;   // label size, image pixels
    const auto margin = f * 5.0f;
    const auto stripW = static_cast<float> (w) * 0.5f, stripH = static_cast<float> (h) * 0.5f;
    const auto pw = static_cast<float> (w), ph = static_cast<float> (h);
    const auto width = juce::roundToInt (margin + pw + margin + pw + f * 2.0f);
    std::vector<int> stripFfts;

    for (const auto n : options.ffts)
        if (n != mainFft)
            stripFfts.push_back (n);

    const auto stripPanels = static_cast<int> (stripFfts.size()) + 1 + (effect ? 1 : 0);
    const auto stripRows = (stripPanels + 2) / 3;
    const auto tableH = f * 1.7f * 7.0f;
    const auto height = juce::roundToInt (f * 6.5f + ph + f * 3.2f + static_cast<float> (stripRows) * (stripH + f * 3.2f) + tableH + f * 2.0f);

    juce::Image sheet (juce::Image::RGB, width, height, true);
    juce::Graphics g (sheet);
    g.fillAll (paper());

    g.setColour (ink());
    g.setFont (fonts::ui (f * 1.5f, fonts::UiWeight::semiBold));
    g.drawText ("artfreq-" + name + ".png: the screenshot's canvas beside the plug-in's real output",
                juce::Rectangle<float> (f, f * 0.6f, static_cast<float> (width) - 2.0f * f, f * 2.0f), juce::Justification::left, true);
    g.setColour (dim());
    g.setFont (fonts::ui (f * 1.05f));
    g.drawText (result.settings + ".",
                juce::Rectangle<float> (f, f * 2.5f, static_cast<float> (width) - 2.0f * f, f * 1.5f), juce::Justification::left, true);
    g.drawText ("Real: one pass through the activated processor"
                    + (effect ? " on a dense synthetic master (" + num (result.songRmsDb, 1) + " dBFS RMS)" : juce::String())
                    + ", analysed like MiniMeters (Hann, a column every " + num (1000.0 * rendered.passSamples / sampleRate / w, 1)
                    + " ms, colours " + num (options.lowDb) + " to " + num (options.highDb) + " dBFS). Dashes: 250 Hz and 2 kHz.",
                juce::Rectangle<float> (f, f * 4.0f, static_cast<float> (width) - 2.0f * f, f * 1.5f), juce::Justification::left, true);

    auto top = f * 6.5f + f * 1.2f;
    drawPanel (g, crop, { margin, top, pw, ph }, "Screenshot (the canvas: a preview)", scale, f, true);
    drawPanel (g, displayImage (displays[mainFft], options.lowDb, options.highDb), { margin + pw + margin, top, pw, ph },
               "Real output, FFT " + juce::String (mainFft), scale, f, true);
    top += ph + f * 3.2f;

    {
        auto column = 0;
        auto x = margin;
        const auto addStrip = [&] (const juce::Image& image, const juce::String& title)
        {
            drawPanel (g, image, { x, top + f * 0.4f, stripW, stripH }, title, scale, f * 0.85f, true);
            x += stripW + margin * 0.8f;

            if (++column == 3)
            {
                column = 0;
                x = margin;
                top += stripH + f * 3.2f;
            }
        };

        for (const auto n : stripFfts)
            addStrip (displayImage (displays[n], options.lowDb, options.highDb), "Real output, FFT " + juce::String (n));

        // The same sound in the canvas's own colours: the instrument's canvas puts its loudest at the
        // top of a 60 dB window, ArtFreq FX's a full-scale sine in a cell at FFT 4096 (-60 to 0 dB).
        // What differs from the screenshot here is the sound; what differs only above is the range.
        if (effect)
            addStrip (displayImage (displays[options.ffts.front()], -60.0f, 0.0f),
                      "FFT " + juce::String (options.ffts.front()) + ", the canvas's colours (-60 to 0 dB)");
        else
            addStrip (displayImage (displays[mainFft], static_cast<float> (result.byFft[mainFft].topDb) - 60.0f,
                                    static_cast<float> (result.byFft[mainFft].topDb)),
                      "FFT " + juce::String (mainFft) + ", the canvas's colours (loudest = white)");

        if (effect)
            addStrip (displayImage (songs[mainFft], options.lowDb, options.highDb), "The song going in, FFT " + juce::String (mainFft));

        if (column != 0)
            top += stripH + f * 3.2f;
    }

    // The numbers, per band, at each FFT size.
    g.setFont (fonts::mono (f * 0.95f));
    const auto lineH = f * 1.55f;
    auto y = top;
    const auto writeLine = [&] (const juce::String& text, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawText (text, juce::Rectangle<float> (margin, y, static_cast<float> (width) - margin - f, lineH), juce::Justification::left, false);
        y += lineH;
    };

    writeLine (effect ? "band       FFT    r     inside  around  contrast real/canvas   carved by   >=30 dB dark   stencil kept"
                      : "band       FFT    r     inside  around  contrast real/canvas   kept    stray",
               dim());

    for (size_t b = 0; b < bands.size(); ++b)
    {
        for (const auto n : options.ffts)
        {
            const auto& m = result.byFft[n].band[b];

            if (m.drawn < 50)
            {
                if (n == options.ffts.front())
                    writeLine (juce::String (bands[b].name).paddedRight (' ', 10) + " (nothing drawn)", faint());

                continue;
            }

            juce::String line;
            line << juce::String (bands[b].name).paddedRight (' ', 10) << juce::String (n).paddedLeft (' ', 6)
                 << num (m.r, 2).paddedLeft (' ', 6) << num (m.insideDb, 0, " dB").paddedLeft (' ', 9)
                 << num (m.aroundDb, 0, " dB").paddedLeft (' ', 8)
                 << (num (m.contrastDb, 0) + " / " + num (m.canvasContrastDb, 0) + " dB").paddedLeft (' ', 20);

            if (effect)
                line << num (m.attenuationDb, 0, " dB").paddedLeft (' ', 13) << pct (m.darkShare).paddedLeft (' ', 15)
                     << pct (m.songKept).paddedLeft (' ', 15);
            else
                line << pct (m.kept).paddedLeft (' ', 8) << pct (m.stray).paddedLeft (' ', 9);

            writeLine (line, ink());
        }
    }

    return writePng (sheet, options.outputDir.getChildFile ("truth-" + name + ".png"));
}

// ================================================================================================
// Report
// ================================================================================================

juce::var toVar (double v)
{
    return std::isfinite (v) ? juce::var (std::round (v * 1000.0) / 1000.0) : juce::var();
}

juce::String report (const std::vector<StateResult>& results, const Options& options)
{
    juce::String text;
    text << "ArtFreqTruthLab: each screenshot's exact state played through the activated processor and analysed\n"
         << "like MiniMeters (Hann, log scale of the state, colours " << num (options.lowDb) << " to " << num (options.highDb) << " dBFS).\n"
         << "Bands by row: lows < 250 Hz, low-mids 250 Hz - 2 kHz, highs > 2 kHz.\n"
         << "r: ArtFreq, correlation of the real display (60 dB under its top) with the canvas; ArtFreq FX, of the\n"
         << "drawing with what the effect did to the song there (see BandResult::r); inside / around: median dB of\n"
         << "drawn cells / of undrawn cells around them; contrast: inside - around, real / on the canvas;\n"
         << "kept: drawn cells no more than 12 dB darker than the canvas shows (each relative to its top);\n"
         << "stray: undrawn cells around the drawing shown within 30 dB of the top (bright where the canvas is dark);\n"
         << "FX: carved by = median (song - output) over drawn cells; dark = share carved 30 dB or more; stencil kept =\n"
         << "share of drawn cells within 6 dB of the song; song median / p95: the song going in over the band; stand-in\n"
         << "around: the canvas's stand-in song around the drawing (dB of a cell at FFT 4096, as the real 'around').\n\n";

    for (const auto& r : results)
    {
        text << "== " << r.name << ": " << r.settings << "\n   output " << num (r.outputRmsDb, 1) << " dBFS RMS, peak "
             << num (r.outputPeakDb, 1) << " dBFS";

        if (r.effect)
            text << "; song in " << num (r.songRmsDb, 1) << " dBFS RMS";

        text << "\n";

        for (size_t b = 0; b < bands.size(); ++b)
        {
            for (const auto& [n, c] : r.byFft)
            {
                const auto& m = c.band[b];

                if (m.drawn < 50)
                    continue;

                text << "   " << juce::String (bands[b].name).paddedRight (' ', 9) << juce::String (n).paddedLeft (' ', 6)
                     << "  r " << num (m.r, 2).paddedLeft (' ', 5) << "  inside " << num (m.insideDb, 1).paddedLeft (' ', 6)
                     << "  around " << num (m.aroundDb, 1).paddedLeft (' ', 6) << "  contrast " << num (m.contrastDb, 1).paddedLeft (' ', 6)
                     << " / canvas " << num (m.canvasContrastDb, 1).paddedLeft (' ', 6);

                if (r.effect)
                    text << "  carved " << num (m.attenuationDb, 1).paddedLeft (' ', 6) << "  dark " << pct (m.darkShare).paddedLeft (' ', 5)
                         << "  stencilKept " << pct (m.songKept).paddedLeft (' ', 5) << "  song median/p95 "
                         << num (m.songMedianDb, 1) << " / " << num (m.songP95Db, 1) << "  stand-in around " << num (m.canvasAroundDb, 1);
                else
                    text << "  kept " << pct (m.kept).paddedLeft (' ', 5) << "  stray " << pct (m.stray).paddedLeft (' ', 5);

                text << "\n";
            }
        }

        text << "\n";
    }

    return text;
}

juce::var toJson (const std::vector<StateResult>& results)
{
    juce::Array<juce::var> states;

    for (const auto& r : results)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("name", r.name);
        object->setProperty ("settings", r.settings);
        object->setProperty ("outputRmsDb", toVar (r.outputRmsDb));
        object->setProperty ("outputPeakDb", toVar (r.outputPeakDb));
        object->setProperty ("songRmsDb", toVar (r.songRmsDb));

        auto* ffts = new juce::DynamicObject();

        for (const auto& [n, c] : r.byFft)
        {
            auto* perBand = new juce::DynamicObject();

            for (size_t b = 0; b < bands.size(); ++b)
            {
                const auto& m = c.band[b];
                auto* o = new juce::DynamicObject();
                o->setProperty ("drawnCells", m.drawn);
                o->setProperty ("r", toVar (m.r));
                o->setProperty ("insideDb", toVar (m.insideDb));
                o->setProperty ("aroundDb", toVar (m.aroundDb));
                o->setProperty ("contrastDb", toVar (m.contrastDb));
                o->setProperty ("canvasContrastDb", toVar (m.canvasContrastDb));
                o->setProperty ("kept", toVar (m.kept));
                o->setProperty ("stray", toVar (m.stray));
                o->setProperty ("attenuationDb", toVar (m.attenuationDb));
                o->setProperty ("darkShare", toVar (m.darkShare));
                o->setProperty ("stencilKept", toVar (m.songKept));
                o->setProperty ("canvasAroundDb", toVar (m.canvasAroundDb));
                o->setProperty ("songMedianDb", toVar (m.songMedianDb));
                o->setProperty ("songP95Db", toVar (m.songP95Db));
                perBand->setProperty (bands[b].name, juce::var (o));
            }

            perBand->setProperty ("topDb", toVar (c.topDb));
            ffts->setProperty (juce::String (n), juce::var (perBand));
        }

        object->setProperty ("ffts", juce::var (ffts));
        states.add (juce::var (object));
    }

    return juce::var (states);
}

// ================================================================================================
// Marketing: real measurements, for the website
// ================================================================================================

struct MarketingShot
{
    juce::String file, title, caption;
    bool effect;
    std::function<void (ArtFreqProcessor&)> setUp;
    double passOffsetSeconds = 0.0;   // where setUp puts the pass after the transport starts
};

void setParameter (ArtFreqProcessor& p, const char* id, float value)
{
    if (auto* parameter = p.getParameters().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

/** A real measurement, framed for the website: the analyser's picture of the plug-in's output
    over one pass and a little either side, with its frequency axis, a caption that says what it
    is, at 2x (1200 x 640 logical). */
bool renderMarketingShot (const MarketingShot& shot, const Options& options, const Stereo& song)
{
    auto processor = makeProcessor (shot.effect, {});
    auto& p = *processor;
    shot.setUp (p);
    p.rebuildPlanIfNeeded();

    const auto rendered = play (p, shot.effect ? &song : nullptr, shot.passOffsetSeconds);
    const auto scale = p.getScale();

    constexpr float s = 2.0f;                        // 2x
    constexpr float logicalW = 1200.0f, logicalH = 640.0f;
    const auto width = juce::roundToInt (logicalW * s), height = juce::roundToInt (logicalH * s);
    const auto plot = juce::Rectangle<float> (64.0f, 72.0f, logicalW - 64.0f - 24.0f, logicalH - 72.0f - 64.0f) * s;

    // A little before and after the pass, as a scrolling spectrogram shows it.
    const auto lead = 0.06 * rendered.passSamples;
    const auto display = analyse (rendered.output, rendered.passStart - lead, rendered.passStart + rendered.passSamples + lead,
                                  8192, scale, juce::roundToInt (plot.getWidth()), juce::roundToInt (plot.getHeight()));

    juce::Image image (juce::Image::RGB, width, height, true);
    juce::Graphics g (image);
    g.fillAll (paper());
    g.drawImage (displayImage (display, options.lowDb, options.highDb), plot);
    g.setColour (juce::Colour (0xff262b36));
    g.drawRect (plot.expanded (1.0f), 1.0f);
    drawAxis (g, plot, scale, 12.0f * s, false);

    g.setColour (ink());
    g.setFont (fonts::ui (22.0f * s, fonts::UiWeight::semiBold));
    g.drawText (shot.title, juce::Rectangle<float> (64.0f * s, 18.0f * s, plot.getWidth(), 34.0f * s), juce::Justification::left, true);

    g.setColour (dim());
    g.setFont (fonts::ui (13.0f * s));
    g.drawText (shot.caption, juce::Rectangle<float> (64.0f * s, logicalH * s - 52.0f * s, plot.getWidth(), 20.0f * s),
                juce::Justification::left, true);
    g.setColour (faint());
    g.setFont (fonts::ui (11.5f * s));
    g.drawText ("Real measurement of the plug-in's audio, not a mock-up: Hann window, FFT 8192, log 20 Hz - 20 kHz, "
                    + num (options.lowDb) + " to " + num (options.highDb) + " dBFS.",
                juce::Rectangle<float> (64.0f * s, logicalH * s - 30.0f * s, plot.getWidth(), 18.0f * s), juce::Justification::left, true);

    const auto file = options.outputDir.getChildFile (shot.file);
    std::cout << file.getFullPathName() << std::endl;
    return writePng (image, file);
}

/** The drawings for the website, each one real output. */
std::vector<MarketingShot> marketingShots()
{
    std::vector<MarketingShot> shots;

    // 1. ArtFreq's own default: "ArtFreq" across the mid band, as a fresh instance plays it.
    shots.push_back ({ "artfreq-real-output.png", "ArtFreq: the drawing is the sound",
                       "ArtFreq's output for its default drawing (the word ArtFreq, 300 Hz - 3 kHz), one 8 s pass at 120 BPM.", false,
                       [] (ArtFreqProcessor&) {} });

    // 2. Text in the highs and the mids, where an analyser shows the most detail; Density at its
    //    top, 1024, so the highs are solid rather than fine scan lines.
    shots.push_back ({ "artfreq-real-output-hello.png", "ArtFreq: text in the highs and mids",
                       "ArtFreq's output for HELLO (3.5 - 16 kHz) over WORLD (300 Hz - 3 kHz), Density 1024, one 8 s pass.", false,
                       [] (ArtFreqProcessor& p)
                       {
                           const auto scale = p.getScale();
                           ArtDocument document;
                           const auto high = bandRange (Band::high, scale), mid = bandRange (Band::mid, scale);
                           auto hello = ArtDocument::makeTextObject ("HELLO", static_cast<float> (high.low), static_cast<float> (high.high));
                           auto world = ArtDocument::makeTextObject ("WORLD", static_cast<float> (mid.low), static_cast<float> (mid.high));
                           hello.startTime = world.startTime = 0.04f;
                           hello.endTime = world.endTime = 0.96f;
                           document.objects.push_back (hello);
                           document.objects.push_back (world);
                           p.setDocument (document);
                           setParameter (p, params::id::density, 1024.0f);
                       } });

    // 3. ArtFreq FX: its default drawing carved into a dense master, from bar 2 (At bar), so the
    //    song plays before and after the pass.
    shots.push_back ({ "artfreq-fx-real-output.png", "ArtFreq FX: carved into a full mix",
                       "ArtFreq FX (Carve, Depth -80 dB) on a dense synthetic master at -10 dBFS RMS: its default drawing, one 8 s pass.", true,
                       [] (ArtFreqProcessor& p)
                       {
                           setParameter (p, params::id::startAt, 1.0f);   // At bar
                           setParameter (p, params::id::startBar, 2.0f);
                       },
                       2.0 });

    return shots;
}

// ================================================================================================

bool parseOptions (int argc, char* argv[], Options& options, juce::String& error)
{
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    juce::StringArray positional;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto value = i + 1 < args.size() ? args[i + 1] : juce::String();

        if (a == "--states") { options.states = juce::StringArray::fromTokens (value, ",", ""); ++i; }
        else if (a == "--ffts")
        {
            options.ffts.clear();

            for (const auto& t : juce::StringArray::fromTokens (value, ",", ""))
                options.ffts.push_back (t.getIntValue());

            ++i;
        }
        else if (a == "--range")
        {
            const auto parts = juce::StringArray::fromTokens (value, ",", "");
            options.lowDb = parts[0].getFloatValue();
            options.highDb = parts[1].getFloatValue();
            ++i;
        }
        else if (a == "--wav") options.wav = true;
        else if (a == "--marketing") options.marketing = true;
        else if (a.startsWith ("-")) { error = "unknown option " + a; return false; }
        else positional.add (a);
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (options.marketing && positional.size() == 1)
        options.outputDir = cwd.getChildFile (positional[0]);
    else if (! options.marketing && positional.size() == 2)
    {
        options.snapshotDir = cwd.getChildFile (positional[0]);
        options.outputDir = cwd.getChildFile (positional[1]);
    }
    else
        error = "usage: ArtFreqTruthLab <snapshot-dir> <output-dir> [--states a,b] [--ffts 4096,8192,16384] [--range -100,0] [--wav]\n"
                "       ArtFreqTruthLab --marketing <output-dir> [--range -100,0]";

    for (const auto n : options.ffts)
        if (n < 256 || n > 65536 || ! juce::isPowerOfTwo (n))
            error = "FFT sizes must be powers of two";

    if (! (options.highDb > options.lowDb))
        error = "--range low,high";

    return error.isEmpty();
}

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_LINUX || JUCE_BSD
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return nullptr; };
   #endif

    const juce::ScopedJuceInitialiser_GUI gui;

    Options options;
    juce::String error;

    if (! parseOptions (argc, argv, options, error))
    {
        std::cerr << error << std::endl;
        return 1;
    }

    options.outputDir.createDirectory();

    // Activated, against a fake server: a buyer's copy once activated. Nothing reaches the network.
    licensetest::installRunnerLicensing();

    // The song: long enough for the longest pass with its tail.
    const auto song = makeSong (12.0, -10.0);
    auto ok = true;

    if (options.marketing)
    {
        for (const auto& shot : marketingShots())
            ok = renderMarketingShot (shot, options, song) && ok;

        licensetest::removeRunnerLicensing();
        return ok ? 0 : 1;
    }

    std::vector<StateResult> results;

    for (const auto& name : options.states)
    {
        StateResult result;

        if (! runState (options, name.trim(), song, result, error))
        {
            std::cerr << error << std::endl;
            ok = false;
            continue;
        }

        std::cout << options.outputDir.getChildFile ("truth-" + name.trim() + ".png").getFullPathName() << std::endl;
        results.push_back (result);
    }

    const auto text = report (results, options);
    std::cout << "\n" << text;
    options.outputDir.getChildFile ("metrics.txt").replaceWithText (text);
    options.outputDir.getChildFile ("metrics.json").replaceWithText (juce::JSON::toString (toJson (results)));

    licensetest::removeRunnerLicensing();
    return ok ? 0 : 1;
}
