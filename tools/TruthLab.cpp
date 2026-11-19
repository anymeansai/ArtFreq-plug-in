// The truth lab: is the canvas honest? It plays the exact drawing and settings of a screenshot
// through the real plug-in (ArtFreqProcessor, activated against the test licensing environment,
// as a buyer's activated copy) and compares what an analyser such as MiniMeters makes of the
// sound with what the canvas shows: the canvas before the measured preview (--before: screenshots
// of an older build) and the canvas now.
//
//   ArtFreqTruthLab <snapshot-dir> <output-dir> [--before <old-snapshot-dir>] [--states default,rich,...]
//                   [--range -100,0] [--ffts 4096,8192,16384] [--wav]
//   ArtFreqTruthLab --marketing <output-dir> [--range -100,0]
//   ArtFreqTruthLab --scenarios <output-dir> [--only a,b] [--standin]     (tools/TruthScenarios.cpp)
//   ArtFreqTruthLab --bench | --drag
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
//   2. analyses the output (the mean of the channels) as an FFT spectrogram such as MiniMeters'
//      does (its settings assumed, not looked up; written here on its own, not with the plug-in's
//      analysis code): Hann window, FFT 4096, 8192 and 16384 points, dB of the magnitude (a
//      full-scale sine reads 0 dB), one analysis per pixel column of the canvas, frequency on the
//      state's own scale, each pixel row reading the dB interpolated between the bin centres
//      around it, or the loudest bin inside it where it spans several; coloured with the canvas's
//      own colour map over a fixed range (default -100 to 0 dBFS);
//   3. renders the new canvas the way the editor does (MeasuredPreview at each FFT size, at the
//      screenshot's size) and the new fast preview, reads the old screenshot's canvas, and
//      measures each against the real display per band (lows < 250 Hz, low mids 250 Hz - 2 kHz,
//      highs > 2 kHz): see BandCompare;
//   4. writes <output-dir>/truth-<name>.png: the old canvas, the new canvas and the real display
//      side by side (the same size and frequency axis), the real display at the other FFT sizes,
//      for ArtFreq FX the song going in, and the numbers; also truth-<name>-fft<N>.png (each real
//      display alone), metrics.txt and metrics.json, and with --wav the rendered output.
//
// --marketing renders a few drawings for the website (the instrument and the effect), each as a
// real measurement of the plug-in's output (FFT 8192), at 2x (2400 x 1280), captioned as such.
//
// On Linux run it under a virtual display (it renders text):  xvfb-run -a ArtFreqTruthLab ...

#include "DenseProject.h"
#include "LicenseTestSupport.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/Fonts.h"
#include "core/FrequencyScale.h"
#include "core/ImageProcessing.h"
#include "ui/CanvasPlanImage.h"
#include "ui/EffectPreview.h"
#include "ui/MeasuredPreview.h"
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

namespace artfreq::truthlab
{
int runScenarios (const juce::File& outDir, const juce::StringArray& only, bool standIn);   // tools/TruthScenarios.cpp
}

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

/** One band of a canvas against the real display, both in dB as the analyser's colour key reads
    them (a canvas's colour level read with the range's key: what a buyer compares by eye), both
    clipped to the range (an analyser shows nothing beyond it).

    "Drawn" cells: the drawing covers at least half of the cell; "around": the drawing covers
    nothing there, in a column where the band has drawn cells, and the nearest drawn cell is more
    than 3 % of the height away (so edges count for neither). NaN: nothing to measure. */
struct BandCompare
{
    int cells = 0, drawn = 0, around = 0;
    double r = nan;                 // shape: Pearson r of the canvas's dB with the real dB over the band's cells
    double meanDiffDb = nan;        // canvas - real over all cells (positive: the canvas brighter)
    double meanAbsDiffDb = nan;     // |canvas - real|
    double insideDiffDb = nan;      // canvas - real, median over drawn cells
    double aroundDiffDb = nan;      // ... over the cells around (negative: the canvas cleaner than reality)
    double canvasContrastDb = nan;  // median drawn - median around, on the canvas
    double realContrastDb = nan;    // ... in reality
    double songDb = nan;            // ArtFreq FX: the median level of the song going in over the band: the
    double standInDb = nan;         // test master's, and the stand-in's on the canvas
    double songP95Db = nan;         // ... and the 95th percentile (the brightest lines: kick, bass, hats)
    double standInP95Db = nan;
    double excessContrastDb = nan;  // |canvas contrast| - |real contrast|: how much more the canvas
                                    // makes of the drawing than reality does (brighter shapes over a
                                    // cleaner background, or deeper carves); > 0 looks better than it is
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

using BandCompares = std::array<BandCompare, bands.size()>;

/** `canvasDb`: the canvas as dB on the real display's grid (W x H, row 0 at the top). */
BandCompares compareCanvas (const Display& real, const std::vector<float>& canvasDb, const std::vector<float>& drawing,
                            const FrequencyScale& scale, float lowDb, float highDb)
{
    BandCompares result;
    const auto w = real.width, h = real.height;
    const auto s = scale.sanitised();
    const auto clip = [lowDb, highDb] (double v) { return juce::jlimit (static_cast<double> (lowDb), static_cast<double> (highDb), v); };
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
        auto& m = result[b];
        std::vector<double> canvasValues, realValues, insideDiff, aroundDiff, canvasInside, canvasAround, realInside, realAround;
        double sumDiff = 0.0, sumAbs = 0.0;

        for (int y = 0; y < h; ++y)
        {
            const auto hz = s.toHz (1.0 - (y + 0.5) / h);

            if (hz < bands[b].lowHz || hz >= bands[b].highHz)
                continue;

            for (int x = 0; x < w; ++x)
            {
                const auto c = clip (canvasDb[idx (y) * idx (w) + idx (x)]);
                const auto r = clip (real.at (x, y));
                canvasValues.push_back (c);
                realValues.push_back (r);
                sumDiff += c - r;
                sumAbs += std::abs (c - r);

                if (drawnAt (x, y))
                {
                    insideDiff.push_back (c - r);
                    canvasInside.push_back (c);
                    realInside.push_back (r);
                }
                else if (clearAt (x, y) && distance[idx (y) * idx (w) + idx (x)] > gap)
                {
                    aroundDiff.push_back (c - r);
                    canvasAround.push_back (c);
                    realAround.push_back (r);
                }
            }
        }

        m.cells = static_cast<int> (canvasValues.size());
        m.drawn = static_cast<int> (insideDiff.size());
        m.around = static_cast<int> (aroundDiff.size());

        if (m.cells == 0)
            continue;

        m.meanDiffDb = sumDiff / m.cells;
        m.meanAbsDiffDb = sumAbs / m.cells;

        if (m.drawn < 50)
            continue;   // nothing drawn here: the shape and the contrast mean nothing

        m.r = pearson (canvasValues, realValues);
        m.insideDiffDb = median (insideDiff);
        m.aroundDiffDb = median (aroundDiff);
        m.canvasContrastDb = median (canvasInside) - median (canvasAround);
        m.realContrastDb = median (realInside) - median (realAround);
        m.excessContrastDb = std::abs (m.canvasContrastDb) - std::abs (m.realContrastDb);
    }

    return result;
}

/** The dB a canvas's colours stand for, read with the analyser's colour key (lowDb .. highDb). */
std::vector<float> dbOfLevels (const std::vector<uint8_t>& levels, float lowDb, float highDb)
{
    std::vector<float> db (levels.size());

    for (size_t i = 0; i < levels.size(); ++i)
        db[i] = lowDb + static_cast<float> (levels[i]) / 255.0f * (highDb - lowDb);

    return db;
}

/** A measured analysis on the display's grid: its frames interpolated across the columns, its rows
    stretched to the height. */
std::vector<float> dbOfAnalysis (const ui::MeasuredSpectrogram& a, int width, int height)
{
    std::vector<float> db (idx (width) * idx (height));

    for (int x = 0; x < width; ++x)
    {
        int f0 = 0, f1 = 0;
        float t = 0.0f;
        ui::MeasuredSpectrogram::columnPosition (x, width, a.frames, f0, f1, t);

        for (int y = 0; y < height; ++y)
        {
            const auto row = juce::jlimit (0, a.rows - 1, static_cast<int> ((y + 0.5) * a.rows / height));
            db[idx (y) * idx (width) + idx (x)] = a.outputDb (row, f0) + (a.outputDb (row, f1) - a.outputDb (row, f0)) * t;
        }
    }

    return db;
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

// ================================================================================================
// One state
// ================================================================================================

struct Options
{
    juce::File snapshotDir, outputDir;
    juce::File beforeDir;   // a run of the snapshot tool with the old canvas (--before)
    juce::StringArray states { "default", "rich", "layers", "linear",
                               "fx-default", "fx-carve", "fx-carve-draw", "fx-stencil" };
    std::vector<int> ffts { 4096, 8192, 16384 };
    float lowDb = -100.0f, highDb = 0.0f;
    bool wav = false;
    bool marketing = false;
    bool bench = false;
    bool scenarios = false;
    bool dragOnly = false;    // --drag: only the fast preview's cost per drag step
    bool standIn = false;     // --scenarios: ArtFreq FX's canvas on the stand-in (as before any pass played)
    juce::StringArray only;   // --scenarios: which (all when empty)
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

/** Old canvas, new canvas (and the new fast preview, alone), each against the real output, per FFT
    size and band. */
struct CanvasCompare
{
    BandCompares before, after, fast;
    BandCompares standIn;   // ArtFreq FX: the new canvas before any pass has played (the stand-in song)
};

struct StateResult
{
    juce::String name, settings;
    bool effect = false;
    EffectMode mode = EffectMode::carve;
    std::map<int, CanvasCompare> byFft;
    double outputRmsDb = nan, outputPeakDb = nan, songRmsDb = nan;
    double measureMilliseconds = nan;   // the measured preview's render at the screenshot's size, FFT 8192
    double screenshotAgreementDb = nan; // the new screenshot's canvas against a direct render, mean |dB|
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

/** The canvas cut out of a screenshot, as dB read with the analyser's key. */
bool canvasFromScreenshot (const juce::File& base, juce::Rectangle<int> plot, float lowDb, float highDb,
                           juce::Image& crop, std::vector<float>& db)
{
    const auto screenshot = juce::ImageFileFormat::loadFrom (base.withFileExtension ("png"));

    if (! screenshot.isValid())
        return false;

    crop = screenshot.getClippedImage (plot);
    db = dbOfLevels (levelsOf (crop), lowDb, highDb);
    return true;
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

    const auto effect = json["plugin"].toString() == "effect";
    const auto plot = juce::Rectangle<double> (json["plotX"], json["plotY"], json["plotWidth"], json["plotHeight"]).toNearestInt();
    const auto w = plot.getWidth(), h = plot.getHeight();

    // The screenshots: the new canvas (this build) and the old one (--before).
    juce::Image afterCrop, beforeCrop;
    std::vector<float> afterShotDb, beforeDb;

    if (! canvasFromScreenshot (base, plot, options.lowDb, options.highDb, afterCrop, afterShotDb) || w < 16 || h < 16)
    {
        error = "no screenshot or canvas for " + name;
        return false;
    }

    const auto haveBefore = options.beforeDir != juce::File()
                         && canvasFromScreenshot (options.beforeDir.getChildFile ("artfreq-" + name), plot, options.lowDb, options.highDb,
                                                  beforeCrop, beforeDb)
                         && beforeCrop.getWidth() == w && beforeCrop.getHeight() == h;

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
    const auto view = p.getAnalyserView();

    Canvas canvas;
    Compositor compositor;
    compositor.render (p.getDocument(), scale, canvas);
    const auto drawing = drawingOnGrid (canvas, w, h);

    // ---- the sound -----------------------------------------------------------------------------
    // ArtFreq FX keeps the song that comes in, as it does once its editor has been open: the new
    // canvas then carves the song that played (and before any pass, the stand-in: both measured).
    if (effect)
        p.enableSongCapture();

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
    std::map<int, std::vector<float>> afterDbs, standInDbs;
    juce::Image newCanvas;   // the new canvas at the state's FFT, as the editor shows it (after a pass, in ArtFreq FX)

    for (const auto n : options.ffts)
    {
        displays[n] = analyse (rendered.output, from, to, n, scale, w, h);

        if (effect)
            songs[n] = analyse (rendered.input, from, to, n, scale, w, h);

        // The new canvas at this FFT size: what the editor renders and shows with Settings at it
        // (the same code: MeasuredPreview), at the screenshot's size.
        auto request = ui::MeasureRequest::forProcessor (p, w, h);
        request.view.fftSize = n;
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto measuredResult = ui::MeasuredPreview::renderNow (request);
        const auto milliseconds = juce::Time::getMillisecondCounterHiRes() - started;

        if (measuredResult == nullptr)
        {
            error = "the measured preview rendered nothing for " + name;
            return false;
        }

        if (n == view.fftSize)
            result.measureMilliseconds = milliseconds;

        afterDbs[n] = dbOfAnalysis (*measuredResult->analysis, w, h);

        if (n == view.fftSize && measuredResult->pixels.size() == idx (w) * idx (h))
        {
            newCanvas = juce::Image (juce::Image::RGB, w, h, false);
            juce::Image::BitmapData data (newCanvas, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < h; ++y)
                ui::writePixelRow (data, y, measuredResult->pixels.data() + idx (y) * idx (w), w);
        }

        // ArtFreq FX before any pass: the stand-in.
        if (effect)
        {
            auto standInRequest = request;
            standInRequest.song = nullptr;
            standInRequest.songSerial = 0;

            if (const auto standIn = ui::MeasuredPreview::renderNow (standInRequest))
                standInDbs[n] = dbOfAnalysis (*standIn->analysis, w, h);
        }

        // The new fast preview alone (what shows while dragging), at the same size.
        std::vector<float> fastDb (idx (w) * idx (h));
        {
            ui::AnalyserModel model;
            model.scale = scale;
            model.slopeDbPerOctave = request.slopeDbPerOctave;
            model.view = request.view;
            model.sampleRate = request.sampleRate;
            model.passSeconds = request.passSeconds;
            model.outputGain = ui::previewGain (p.getOutputGain());
            ui::CanvasPlanImage planImage;
            juce::Image picture;

            if (! effect)
            {
                picture = planImage.get (p.getCurrentPlan(), model, w, h);
            }
            else
            {
                planImage.prepareLevels (p.getCurrentPlan(), model, w, h);
                ui::EffectPreviewImage preview;
                preview.setMeasuredSong (measuredResult->analysis);   // as the canvas has it once measured
                ui::EffectPreviewImage::Settings settings;
                settings.mode = request.mode;
                settings.depthDb = request.depthDb;
                settings.view = request.view;
                settings.sampleRate = request.sampleRate;
                settings.passSeconds = request.passSeconds;
                picture = preview.get (planImage.getPixelDbBeforeLevel(), planImage.getLevelDb(), 1, settings,
                                       p.getCurrentMasks(), scale, w, h);
            }

            fastDb = dbOfLevels (levelsOf (picture), request.view.floorDb, request.view.ceilingDb);
        }

        auto& c = result.byFft[n];
        c.after = compareCanvas (displays[n], afterDbs[n], drawing, scale, options.lowDb, options.highDb);

        // The songs going in: the test master, and the stand-in the canvas carves.
        if (effect)
        {
            const auto& a = *measuredResult->analysis;
            const auto s = scale.sanitised();

            for (size_t b = 0; b < bands.size(); ++b)
            {
                std::vector<double> master, standIn;

                for (int y = 0; y < h; ++y)
                {
                    const auto hz = s.toHz (1.0 - (y + 0.5) / h);

                    if (hz < bands[b].lowHz || hz >= bands[b].highHz)
                        continue;

                    const auto row = juce::jlimit (0, a.rows - 1, static_cast<int> ((y + 0.5) * a.rows / h));

                    for (int x = 0; x < w; x += 4)
                        master.push_back (songs[n].at (x, y));

                    for (int frame = 0; frame < a.frames; ++frame)
                        standIn.push_back (a.inputDb (row, frame));
                }

                c.after[b].songDb = median (master);
                c.after[b].standInDb = median (standIn);
                c.after[b].songP95Db = percentile (master, 0.95);
                c.after[b].standInP95Db = percentile (standIn, 0.95);
            }
        }
        c.fast = compareCanvas (displays[n], fastDb, drawing, scale, options.lowDb, options.highDb);

        if (standInDbs.count (n) != 0)
            c.standIn = compareCanvas (displays[n], standInDbs[n], drawing, scale, options.lowDb, options.highDb);

        if (haveBefore)
            c.before = compareCanvas (displays[n], beforeDb, drawing, scale, options.lowDb, options.highDb);

        writePng (displayImage (displays[n], options.lowDb, options.highDb),
                  options.outputDir.getChildFile ("truth-" + name + "-fft" + juce::String (n) + ".png"));
    }

    // The screenshot shows what the direct render shows (grid lines and outlines aside).
    // (The screenshot tool plays no audio: ArtFreq FX's screenshot is of the stand-in.)
    if (const auto& renders = effect ? standInDbs : afterDbs; renders.count (view.fftSize) != 0)
    {
        std::vector<double> diffs;
        const auto& direct = renders.at (view.fftSize);

        for (size_t i = 0; i < direct.size(); ++i)
            diffs.push_back (std::abs (juce::jlimit (options.lowDb, options.highDb, direct[i]) - afterShotDb[i]));

        result.screenshotAgreementDb = median (diffs);
    }

    // ---- the side by side: old canvas | new canvas | real --------------------------------------
    const auto mainFft = displays.count (view.fftSize) != 0 ? view.fftSize : options.ffts.front();
    const auto f = 13.0f;                     // label size, image pixels
    const auto pw = static_cast<float> (w) * 0.5f, ph = static_cast<float> (h) * 0.5f;
    const auto margin = f * 4.5f;
    const auto columns = effect ? 4 : 3;
    const auto width = juce::roundToInt (margin + columns * (pw + margin));
    std::vector<int> stripFfts;

    for (const auto n : options.ffts)
        if (n != mainFft)
            stripFfts.push_back (n);

    const auto stripPanels = static_cast<int> (stripFfts.size()) + (effect ? 2 : 0);
    const auto tableH = f * 1.55f * (effect ? 22.0f : 14.0f);   // ArtFreq FX: a stand-in line under each
    const auto height = juce::roundToInt (f * 6.5f + ph + f * 3.2f + (stripPanels > 0 ? ph + f * 3.2f : 0.0f) + tableH);

    juce::Image sheet (juce::Image::RGB, width, height, true);
    juce::Graphics g (sheet);
    g.fillAll (paper());

    g.setColour (ink());
    g.setFont (fonts::ui (f * 1.5f, fonts::UiWeight::semiBold));
    g.drawText ("artfreq-" + name + ": the old canvas, the new canvas and the plug-in's real output",
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
    auto x = margin;

    if (haveBefore)
        drawPanel (g, beforeCrop, { x, top, pw, ph }, "Old canvas (the screenshot before)", scale, f, true);

    x += pw + margin;
    drawPanel (g, effect && newCanvas.isValid() ? newCanvas : afterCrop, { x, top, pw, ph },
               effect ? "New canvas (the real carve of the song that played)" : "New canvas (the real sound, measured)", scale, f, true);
    x += pw + margin;
    drawPanel (g, displayImage (displays[mainFft], options.lowDb, options.highDb), { x, top, pw, ph },
               "Real output, FFT " + juce::String (mainFft) + (effect ? " (test master)" : ""), scale, f, true);
    top += ph + f * 3.2f;

    if (stripPanels > 0)
    {
        x = margin;

        for (const auto n : stripFfts)
        {
            drawPanel (g, displayImage (displays[n], options.lowDb, options.highDb), { x, top, pw, ph },
                       "Real output, FFT " + juce::String (n), scale, f * 0.9f, true);
            x += pw + margin;
        }

        if (effect)
        {
            drawPanel (g, displayImage (songs[mainFft], options.lowDb, options.highDb), { x, top, pw, ph },
                       "The test master going in, FFT " + juce::String (mainFft), scale, f * 0.9f, true);
            x += pw + margin;
            drawPanel (g, afterCrop, { x, top, pw, ph }, "New canvas before any pass (stand-in song)", scale, f * 0.9f, true);
        }

        top += ph + f * 3.2f;
    }

    // The numbers, per band, at each FFT size: before -> after.
    g.setFont (fonts::mono (f * 0.95f));
    const auto lineH = f * 1.55f;
    auto y = top;
    const auto writeLine = [&] (const juce::String& text, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawText (text, juce::Rectangle<float> (margin, y, static_cast<float> (width) - margin - f, lineH), juce::Justification::left, false);
        y += lineH;
    };

    writeLine ("band       FFT   r old -> new     mean dB old -> new    excess contrast old -> new (> 0: canvas promises more)", dim());

    for (size_t b = 0; b < bands.size(); ++b)
    {
        for (const auto n : options.ffts)
        {
            const auto& c = result.byFft[n];
            const auto& before = c.before[b];
            const auto& after = c.after[b];
            juce::String line;
            line << juce::String (bands[b].name).paddedRight (' ', 10) << juce::String (n).paddedLeft (' ', 6)
                 << (num (before.r, 2) + " -> " + num (after.r, 2)).paddedLeft (' ', 16)
                 << (num (before.meanDiffDb, 1) + " -> " + num (after.meanDiffDb, 1)).paddedLeft (' ', 20)
                 << (num (before.excessContrastDb, 1) + " -> " + num (after.excessContrastDb, 1)).paddedLeft (' ', 24);
            writeLine (line, after.drawn < 50 ? faint() : ink());

            if (effect)
            {
                const auto& standIn = c.standIn[b];
                juce::String second;
                second << juce::String ("  stand-in").paddedRight (' ', 16) << num (standIn.r, 2).paddedLeft (' ', 16)
                       << num (standIn.meanDiffDb, 1).paddedLeft (' ', 20) << num (standIn.excessContrastDb, 1).paddedLeft (' ', 24);
                writeLine (second, faint());
            }
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

juce::String bandLine (const char* label, const BandCompare& m)
{
    juce::String line;
    line << juce::String (label).paddedRight (' ', 7) << "r " << num (m.r, 2).paddedLeft (' ', 5) << "  mean " << num (m.meanDiffDb, 1).paddedLeft (' ', 6)
         << "  |mean| " << num (m.meanAbsDiffDb, 1).paddedLeft (' ', 5) << "  inside " << num (m.insideDiffDb, 1).paddedLeft (' ', 6)
         << "  around " << num (m.aroundDiffDb, 1).paddedLeft (' ', 6) << "  contrast canvas/real " << num (m.canvasContrastDb, 1).paddedLeft (' ', 6)
         << " / " << num (m.realContrastDb, 1).paddedLeft (' ', 6) << "  excess " << num (m.excessContrastDb, 1).paddedLeft (' ', 6);

    if (std::isfinite (m.songDb))
        line << "  song median: master " << num (m.songDb, 1) << " / stand-in " << num (m.standInDb, 1)
             << ", p95 " << num (m.songP95Db, 1) << " / " << num (m.standInP95Db, 1);

    return line;
}

juce::String report (const std::vector<StateResult>& results, const Options& options)
{
    juce::String text;
    text << "ArtFreqTruthLab: each screenshot's state played through the activated processor and analysed like MiniMeters\n"
         << "(Hann, the state's scale, colours " << num (options.lowDb) << " to " << num (options.highDb) << " dBFS), against the old canvas\n"
         << "(--before: the screenshots before), the new canvas (the measured preview, as the editor renders it at each FFT size)\n"
         << "and the new fast preview alone (what shows while dragging). Bands: lows < 250 Hz, low-mids 250 Hz - 2 kHz,\n"
         << "highs > 2 kHz. All in dB as the analyser's colour key reads them, clipped to its range.\n"
         << "r: correlation of canvas and real over the band; mean: canvas - real (> 0: canvas brighter); |mean|: mean\n"
         << "absolute difference; inside / around: median canvas - real over drawn cells / cells around the drawing; contrast:\n"
         << "median drawn - median around; excess: |canvas contrast| - |real contrast| (> 0: the canvas makes more of the\n"
         << "drawing than reality does: brighter shapes, a cleaner background, deeper carves).\n"
         << "ArtFreq FX: 'new' is the canvas once a pass has played (it carves the song that came in: here the dense\n"
         << "synthetic master), 'standin' the canvas before any pass (the stand-in song, against the master: two songs).\n\n";

    for (const auto& r : results)
    {
        text << "== " << r.name << ": " << r.settings << "\n   output " << num (r.outputRmsDb, 1) << " dBFS RMS, peak "
             << num (r.outputPeakDb, 1) << " dBFS";

        if (r.effect)
            text << "; song in " << num (r.songRmsDb, 1) << " dBFS RMS";

        text << "; measured preview " << num (r.measureMilliseconds, 0) << " ms; screenshot vs direct render median |dB| "
             << num (r.screenshotAgreementDb, 1) << "\n";

        for (size_t b = 0; b < bands.size(); ++b)
        {
            for (const auto& [n, c] : r.byFft)
            {
                text << "   " << juce::String (bands[b].name).paddedRight (' ', 9) << juce::String (n).paddedLeft (' ', 6) << "  "
                     << bandLine ("old", c.before[b]) << "\n";
                text << "   " << juce::String().paddedRight (' ', 17) << bandLine ("new", c.after[b]) << "\n";

                if (r.effect)
                    text << "   " << juce::String().paddedRight (' ', 17) << bandLine ("standin", c.standIn[b]) << "\n";
                text << "   " << juce::String().paddedRight (' ', 17) << bandLine ("fast", c.fast[b]) << "\n";
            }
        }

        text << "\n";
    }

    // The summary: at each state's analyser FFT (8192), old -> new.
    text << "Summary at FFT 8192 (old -> new; ArtFreq FX's new: once a pass has played, [before: on the stand-in]): r, mean dB, excess contrast dB\n";
    text << "state            band        r                mean dB              excess dB\n";

    for (const auto& r : results)
    {
        const auto found = r.byFft.find (8192);

        if (found == r.byFft.end())
            continue;

        for (size_t b = 0; b < bands.size(); ++b)
        {
            const auto& before = found->second.before[b];
            const auto& after = found->second.after[b];
            text << juce::String (r.name).paddedRight (' ', 17) << juce::String (bands[b].name).paddedRight (' ', 10)
                 << (num (before.r, 2) + " -> " + num (after.r, 2)).paddedLeft (' ', 14)
                 << (num (before.meanDiffDb, 1) + " -> " + num (after.meanDiffDb, 1)).paddedLeft (' ', 20)
                 << (num (before.excessContrastDb, 1) + " -> " + num (after.excessContrastDb, 1)).paddedLeft (' ', 20)
                 << (r.effect ? "   [" + num (found->second.standIn[b].r, 2) + ", " + num (found->second.standIn[b].meanDiffDb, 1) + ", "
                                    + num (found->second.standIn[b].excessContrastDb, 1) + "]" : juce::String())
                 << (after.drawn < 50 ? "   (nothing drawn)" : "") << "\n";
        }
    }

    return text;
}

juce::var bandVar (const BandCompare& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("cells", m.cells);
    o->setProperty ("drawnCells", m.drawn);
    o->setProperty ("aroundCells", m.around);
    o->setProperty ("r", toVar (m.r));
    o->setProperty ("meanDiffDb", toVar (m.meanDiffDb));
    o->setProperty ("meanAbsDiffDb", toVar (m.meanAbsDiffDb));
    o->setProperty ("insideDiffDb", toVar (m.insideDiffDb));
    o->setProperty ("aroundDiffDb", toVar (m.aroundDiffDb));
    o->setProperty ("canvasContrastDb", toVar (m.canvasContrastDb));
    o->setProperty ("realContrastDb", toVar (m.realContrastDb));
    o->setProperty ("excessContrastDb", toVar (m.excessContrastDb));
    o->setProperty ("songDb", toVar (m.songDb));
    o->setProperty ("standInDb", toVar (m.standInDb));
    o->setProperty ("songP95Db", toVar (m.songP95Db));
    o->setProperty ("standInP95Db", toVar (m.standInP95Db));
    return juce::var (o);
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
        object->setProperty ("measureMilliseconds", toVar (r.measureMilliseconds));
        object->setProperty ("screenshotAgreementDb", toVar (r.screenshotAgreementDb));

        auto* ffts = new juce::DynamicObject();

        for (const auto& [n, c] : r.byFft)
        {
            auto* perBand = new juce::DynamicObject();

            for (size_t b = 0; b < bands.size(); ++b)
            {
                auto* trio = new juce::DynamicObject();
                trio->setProperty ("old", bandVar (c.before[b]));
                trio->setProperty ("new", bandVar (c.after[b]));
                trio->setProperty ("fast", bandVar (c.fast[b]));

                if (r.effect)
                    trio->setProperty ("standIn", bandVar (c.standIn[b]));
                perBand->setProperty (bands[b].name, juce::var (trio));
            }

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
    //    top, 1024, so the highs are solid rather than fine scan lines. The words are the product
    //    and the company (not test text: this one goes on the website).
    shots.push_back ({ "artfreq-real-output-text.png", "ArtFreq: text in the highs and mids",
                       "ArtFreq's output for ARTFREQ (3.5 - 16 kHz) over ANYMEANS (300 Hz - 3 kHz), Density 1024, one 8 s pass.", false,
                       [] (ArtFreqProcessor& p)
                       {
                           const auto scale = p.getScale();
                           ArtDocument document;
                           const auto high = bandRange (Band::high, scale), mid = bandRange (Band::mid, scale);
                           auto top = ArtDocument::makeTextObject ("ARTFREQ", static_cast<float> (high.low), static_cast<float> (high.high));
                           auto bottom = ArtDocument::makeTextObject ("ANYMEANS", static_cast<float> (mid.low), static_cast<float> (mid.high));
                           top.startTime = bottom.startTime = 0.04f;
                           top.endTime = bottom.endTime = 0.96f;
                           document.objects.push_back (top);
                           document.objects.push_back (bottom);
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


// ================================================================================================
// The budget: how long the previews take in the worst cases
// ================================================================================================

/** A canvas drawn everywhere (the worst case for the plan, the masks and the previews). */
void drawEverything (ArtFreqProcessor& p)
{
    ArtDocument document;
    const auto scale = p.getScale();
    auto block = ArtDocument::makeTextObject ("ARTFREQ ARTFREQ", static_cast<float> (scale.minHz), static_cast<float> (scale.maxHz));
    block.startTime = 0.0f;
    block.endTime = 1.0f;
    document.objects.push_back (block);
    p.setDocument (document);

    // And paint over all of it, at full strength.
    p.editDocument ([scale] (ArtDocument& d)
    {
        d.paint.assign (static_cast<size_t> (ArtDocument::paintWidth) * static_cast<size_t> (ArtDocument::paintHeight), 255);
        d.paintScale = scale;
    });
}

int runBench()
{
    struct Case
    {
        const char* name;
        bool effect;
        EffectMode mode;
        int lengthIndex;   // 7: 32 bars
        float bpm;
        bool everything;
        int width, height;
        int fft;
    };

    const Case cases[] {
        { "ArtFreq, default drawing, 4 bars at 120, 2x at 100 % (1528 x 924)", false, EffectMode::carve, 4, 120.0f, false, 1528, 924, 8192 },
        { "ArtFreq, worst: drawn everywhere, density 1024, 32 bars at 20 BPM (384 s), 2x at 100 %", false, EffectMode::carve, 7, 20.0f, true, 1528, 924, 8192 },
        { "... the same at FFT 16384", false, EffectMode::carve, 7, 20.0f, true, 1528, 924, 16384 },
        { "... the same, 2x at 200 % (3056 x 1848)", false, EffectMode::carve, 7, 20.0f, true, 3056, 1848, 8192 },
        { "ArtFreq FX, Carve, default drawing, 4 bars at 120, 2x at 100 % (1304 x 600)", true, EffectMode::carve, 4, 120.0f, false, 1304, 600, 8192 },
        { "ArtFreq FX, Carve, worst: drawn everywhere, 32 bars at 20 BPM, 2x at 100 %", true, EffectMode::carve, 7, 20.0f, true, 1304, 600, 8192 },
        { "ArtFreq FX, Carve + Draw, worst, density 1024, 32 bars at 20 BPM, 2x at 100 %", true, EffectMode::carveDraw, 7, 20.0f, true, 1304, 600, 8192 },
        { "... the same, 2x at 200 % (2608 x 1200)", true, EffectMode::carveDraw, 7, 20.0f, true, 2608, 1200, 8192 },
    };

    std::cout << "The previews' cost on this machine (one core; JUCE's own FFT, as on Windows without IPP)\n\n";

    for (const auto& c : cases)
    {
        auto processor = makeProcessor (c.effect, {});
        auto& p = *processor;
        setParameter (p, params::id::length, static_cast<float> (c.lengthIndex));
        setParameter (p, params::id::tempoSource, 1.0f);
        setParameter (p, params::id::bpm, c.bpm);
        setParameter (p, params::id::analyserFft, static_cast<float> (c.fft == 4096 ? 0 : c.fft == 8192 ? 1 : 2));

        if (c.effect)
            setParameter (p, params::id::mode, static_cast<float> (static_cast<int> (c.mode)));

        if (c.everything)
        {
            setParameter (p, params::id::density, 1024.0f);
            drawEverything (p);
        }

        p.rebuildPlanIfNeeded();

        const auto request = ui::MeasureRequest::forProcessor (p, c.width, c.height);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto result = ui::MeasuredPreview::renderNow (request);
        const auto measuredMs = juce::Time::getMillisecondCounterHiRes() - started;

        // The fast preview, on the message thread while dragging: the first picture, and one after
        // the drawing changes (a drag step: a new plan, and in ArtFreq FX new masks).
        ui::CanvasPlanImage planImage;
        ui::EffectPreviewImage effectImage;
        ui::AnalyserModel model;
        model.scale = request.scale;
        model.view = request.view;
        model.sampleRate = request.sampleRate;
        model.passSeconds = request.passSeconds;
        model.outputGain = ui::previewGain (p.getOutputGain());
        ui::EffectPreviewImage::Settings settings;
        settings.mode = request.mode;
        settings.depthDb = request.depthDb;
        settings.view = request.view;
        settings.sampleRate = request.sampleRate;
        settings.passSeconds = request.passSeconds;

        double planMs = 0.0;
        const auto fast = [&] (uint64_t key)
        {
            const auto planStarted = juce::Time::getMillisecondCounterHiRes();

            if (! c.effect)
                planImage.get (p.getCurrentPlan(), model, c.width, c.height);
            else if (playsSines (c.mode))
                planImage.prepareLevels (p.getCurrentPlan(), model, c.width, c.height);

            planMs = juce::Time::getMillisecondCounterHiRes() - planStarted;

            if (c.effect)
                effectImage.get (planImage.getPixelDbBeforeLevel(), planImage.getLevelDb(), key, settings, p.getCurrentMasks(),
                                 request.scale, c.width, c.height);
        };

        fast (1);
        p.editDocument ([] (ArtDocument& d) { if (! d.objects.empty()) d.objects.front().startTime += 0.001f; });
        p.rebuildPlanIfNeeded();
        const auto fastStarted = juce::Time::getMillisecondCounterHiRes();
        fast (2);
        const auto fastMs = juce::Time::getMillisecondCounterHiRes() - fastStarted;

        const auto analysisBytes = result != nullptr ? (result->analysis->output.size() + result->analysis->input.size()) * sizeof (int16_t) : 0;
        std::cout << c.name << "\n   pass " << num (request.passSeconds, 0) << " s, FFT " << request.view.fftSize << ": measured preview "
                  << num (measuredMs, 0) << " ms (background), " << (result != nullptr ? result->analysis->frames : 0) << " analyses, "
                  << num (static_cast<double> (analysisBytes) / 1.0e6, 1) << " MB analysis + "
                  << num (static_cast<double> (c.width) * c.height * 4.0 / 1.0e6, 1) << " MB picture; fast preview after a change "
                  << num (fastMs, 0) << " ms (message thread; the plan's part " << num (planMs, 0) << " ms)\n";
    }

    return 0;
}

/** The fast preview's cost per drag step on the message thread, on a dense project (nine words,
    a picture, a brush stroke, Density 1024): the median over 15 steps of moving a word, after the
    plan (and the masks) are rebuilt, at the canvas's size at 100 % (1x), on a Retina screen (2x)
    and at 200 % on one (4x). */
void benchDrag()
{
    struct Size { const char* name; int width, height; };
    const Size sizes[] { { "1x", 764, 462 }, { "2x", 1528, 924 }, { "4x", 3056, 1848 } };

    std::cout << "\nThe fast preview per drag step (dense project, median of 15)\n";

    for (const auto effect : { false, true })
    {
        auto processor = makeProcessor (effect, {});
        auto& p = *processor;
        const auto first = testutil::makeDenseProject (p);

        if (effect)
            setParameter (p, params::id::mode, static_cast<float> (static_cast<int> (EffectMode::carveDraw)));

        p.rebuildPlanIfNeeded();

        for (const auto& size : sizes)
        {
            ui::CanvasPlanImage planImage;
            ui::EffectPreviewImage effectImage;
            std::vector<double> times;

            for (int step = 0; step < 16; ++step)
            {
                p.editDocument ([first, step] (ArtDocument& d)
                {
                    if (auto* o = d.find (first))
                        o->startTime = 0.02f + 0.002f * static_cast<float> (step);
                });
                p.rebuildPlanIfNeeded();

                const auto request = ui::MeasureRequest::forProcessor (p, size.width, size.height);
                ui::AnalyserModel model;
                model.scale = request.scale;
                model.slopeDbPerOctave = request.slopeDbPerOctave;
                model.view = request.view;
                model.sampleRate = request.sampleRate;
                model.passSeconds = request.passSeconds;
                model.outputGain = ui::previewGain (p.getOutputGain());
                ui::EffectPreviewImage::Settings settings;
                settings.mode = request.mode;
                settings.depthDb = request.depthDb;
                settings.view = request.view;
                settings.sampleRate = request.sampleRate;
                settings.passSeconds = request.passSeconds;

                const auto started = juce::Time::getMillisecondCounterHiRes();

                if (! effect)
                {
                    planImage.get (p.getCurrentPlan(), model, size.width, size.height);
                }
                else
                {
                    planImage.prepareLevels (p.getCurrentPlan(), model, size.width, size.height);
                    effectImage.get (planImage.getPixelDbBeforeLevel(), planImage.getLevelDb(), planImage.getRenderCount(), settings,
                                     p.getCurrentMasks(), request.scale, size.width, size.height);
                }

                if (step > 0)
                    times.push_back (juce::Time::getMillisecondCounterHiRes() - started);
            }

            std::sort (times.begin(), times.end());
            std::cout << "   " << (effect ? "ArtFreq FX, Carve + Draw" : "ArtFreq") << ", " << size.name << " (" << size.width << " x "
                      << size.height << "): " << num (times[times.size() / 2], 1) << " ms (max " << num (times.back(), 1) << ")\n";
        }
    }
}

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
        else if (a == "--before") { options.beforeDir = juce::File::getCurrentWorkingDirectory().getChildFile (value); ++i; }
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
        else if (a == "--bench") options.bench = true;
        else if (a == "--drag") { options.bench = true; options.dragOnly = true; }
        else if (a == "--scenarios") options.scenarios = true;
        else if (a == "--standin") options.standIn = true;
        else if (a == "--only") { options.only = juce::StringArray::fromTokens (value, ",", ""); ++i; }
        else if (a.startsWith ("-")) { error = "unknown option " + a; return false; }
        else positional.add (a);
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (options.bench)
        return error.isEmpty();

    if ((options.marketing || options.scenarios) && positional.size() == 1)
        options.outputDir = cwd.getChildFile (positional[0]);
    else if (! options.marketing && positional.size() == 2)
    {
        options.snapshotDir = cwd.getChildFile (positional[0]);
        options.outputDir = cwd.getChildFile (positional[1]);
    }
    else
        error = "usage: ArtFreqTruthLab <snapshot-dir> <output-dir> [--before <old-snapshot-dir>] [--states a,b] [--ffts 4096,8192,16384]\n"
                "                       [--range -100,0] [--wav]\n"
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

    if (options.scenarios)
    {
        licensetest::installRunnerLicensing();
        const auto result = artfreq::truthlab::runScenarios (options.outputDir, options.only, options.standIn);
        licensetest::removeRunnerLicensing();
        return result;
    }

    if (options.bench)
    {
        licensetest::installRunnerLicensing();
        const auto result = options.dragOnly ? 0 : runBench();
        benchDrag();
        licensetest::removeRunnerLicensing();
        return result;
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
