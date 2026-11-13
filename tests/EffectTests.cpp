// FreqArt FX, the effect variant of the processor, driven like a host would: its layout and
// parameters, the latency (constant in every mode, at every rate, and in bypass), transparency
// outside the pass and with nothing drawn, carving and stencilling a sine in a drawn band through
// the real processor, Carve + Draw's hole and sines, Draw's sines on top of the audio (the
// instrument's own sound, as late as the audio), mode and depth changes without clicks, state
// round trips (also to and from the instrument), mono and stereo, sample rates, block sizes, MIDI
// and Preview.

#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace freqart;
using namespace testutil;

namespace
{

using Stereo = std::vector<std::vector<float>>;
using Variant = FreqArtProcessor::Variant;

constexpr double defaultRate = 48000.0;

/** A drawing of full-strength bands (paint, on the default log scale) over a part of the length. */
ArtDocument bandDocument (std::initializer_list<std::pair<double, double>> bands, float from = 0.0f, float to = 1.0f)
{
    ArtDocument document;
    const FrequencyScale scale;

    for (int row = 0; row < ArtDocument::paintHeight; ++row)
    {
        const auto hz = scale.toHz (1.0 - (row + 0.5) / ArtDocument::paintHeight);

        for (const auto& [low, high] : bands)
            if (hz >= low && hz <= high)
                for (auto x = static_cast<int> (from * ArtDocument::paintWidth); x < static_cast<int> (to * ArtDocument::paintWidth); ++x)
                    document.paint[static_cast<size_t> (row * ArtDocument::paintWidth + x)] = 255;
    }

    return document;
}

/** A processor of either variant, set up (parameters, drawing) before the host prepares it. */
std::unique_ptr<FreqArtProcessor> makeProcessor (Variant variant, const ArtDocument& document,
                                                 const std::function<void (FreqArtProcessor&)>& setup = {},
                                                 double rate = defaultRate, int block = 512, int channels = 2)
{
    auto processor = std::make_unique<FreqArtProcessor> (variant);

    if (setup != nullptr)
        setup (*processor);

    processor->setDocument (document);
    processor->rebuildPlanIfNeeded();
    processor->setPlayConfigDetails (variant == Variant::effect ? channels : 0, channels, rate, block);
    processor->prepareToPlay (rate, block);
    return processor;
}

/** Mode, Repeat and so on, by index. */
std::function<void (FreqArtProcessor&)> settingsOf (EffectMode mode, bool repeat = true, float depth = params::defaultDepthDb,
                                                    int lengthIndex = 2, int startBar = 1)
{
    return [=] (FreqArtProcessor& p)
    {
        if (p.isEffect())
        {
            setParameter (p, params::id::mode, static_cast<float> (static_cast<int> (mode)));
            setParameter (p, params::id::depth, depth);
        }

        setParameter (p, params::id::repeat, repeat ? 1.0f : 0.0f);
        setParameter (p, params::id::length, static_cast<float> (lengthIndex));   // 2: one bar, 2 s at 120 BPM
        setParameter (p, params::id::startBar, static_cast<float> (startBar));
    };
}

struct Run
{
    std::vector<int> blocks { 512 };
    bool playing = true;
    double bpm = 120.0;
    std::function<void (int start, int length, juce::MidiBuffer&)> midi;
    std::function<bool (int start)> bypassed;
    std::function<void (FreqArtProcessor&, int start)> beforeBlock;
};

/** Plays `input` through the processor as a host would, block by block, with the DAW's
    transport at the song's start (or stopped). Returns what comes out. */
Stereo process (FreqArtProcessor& processor, const Stereo& input, const Run& run = {})
{
    FakePlayHead head;
    processor.setPlayHead (&head);

    const auto rate = processor.getSampleRate();
    const auto channels = static_cast<int> (input.size());
    const auto total = static_cast<int> (input[0].size());
    auto out = input;
    juce::AudioBuffer<float> buffer (channels, 4096);
    size_t next = 0;

    for (int start = 0; start < total;)
    {
        const auto length = juce::jmin (run.blocks[next++ % run.blocks.size()], total - start);
        buffer.setSize (channels, length, false, false, true);

        for (int channel = 0; channel < channels; ++channel)
            buffer.copyFrom (channel, 0, input[static_cast<size_t> (channel)].data() + start, length);

        const auto seconds = start / rate;
        head.set (run.playing, run.bpm, seconds * run.bpm / 60.0, seconds);

        juce::MidiBuffer midi;

        if (run.midi != nullptr)
            run.midi (start, length, midi);

        if (run.beforeBlock != nullptr)
            run.beforeBlock (processor, start);

        if (run.bypassed != nullptr && run.bypassed (start))
            processor.processBlockBypassed (buffer, midi);
        else
            processor.processBlock (buffer, midi);

        for (int channel = 0; channel < channels; ++channel)
            std::copy (buffer.getReadPointer (channel), buffer.getReadPointer (channel) + length,
                       out[static_cast<size_t> (channel)].begin() + start);

        start += length;
    }

    processor.setPlayHead (nullptr);
    return out;
}

Stereo sines (int numSamples, std::initializer_list<std::pair<double, float>> partials, double rate = defaultRate, int channels = 2)
{
    Stereo audio (static_cast<size_t> (channels), std::vector<float> (static_cast<size_t> (numSamples), 0.0f));
    const auto fadeIn = static_cast<int> (0.01 * rate);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto fade = i < fadeIn ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / fadeIn) : 1.0;

        for (const auto& [hz, amplitude] : partials)
            for (int channel = 0; channel < channels; ++channel)
                audio[static_cast<size_t> (channel)][static_cast<size_t> (i)]
                    += amplitude * static_cast<float> (fade * std::sin (juce::MathConstants<double>::twoPi * hz * i / rate + 0.3 * channel));
    }

    return audio;
}

Stereo noise (int numSamples, int seed = 7, int channels = 2)
{
    juce::Random random (seed);
    Stereo audio (static_cast<size_t> (channels), std::vector<float> (static_cast<size_t> (numSamples)));

    for (auto& channel : audio)
        for (auto& x : channel)
            x = 0.5f * (random.nextFloat() - 0.5f);

    return audio;
}

/** Level of a sine of `hz` in a channel over [from, to), in dB (a full-scale sine is 0 dB). */
double levelDb (const Stereo& audio, double hz, int from, int to, double rate = defaultRate, size_t channel = 0)
{
    double re = 0.0, im = 0.0, windowSum = 0.0;

    for (int i = from; i < to; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - from) / (to - from));
        const auto phase = juce::MathConstants<double>::twoPi * hz * i / rate;
        re += w * audio[channel][static_cast<size_t> (i)] * std::cos (phase);
        im += w * audio[channel][static_cast<size_t> (i)] * std::sin (phase);
        windowSum += w;
    }

    return 20.0 * std::log10 (std::max (1e-12, 2.0 * std::sqrt (re * re + im * im) / windowSum));
}

/** The energy of a band of frequencies in channel 0 over [from, to), in dB (FFTs of 4096). */
double bandDb (const Stereo& audio, double lowHz, double highHz, int from, int to, double rate = defaultRate)
{
    juce::dsp::FFT fft (12);
    std::vector<float> buffer (8192, 0.0f);
    double energy = 0.0;
    const auto firstBin = static_cast<int> (std::ceil (lowHz * 4096.0 / rate));
    const auto lastBin = static_cast<int> (std::floor (highHz * 4096.0 / rate));

    for (int start = from; start + 4096 <= to; start += 1024)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);

        for (int i = 0; i < 4096; ++i)
            buffer[static_cast<size_t> (i)] = audio[0][static_cast<size_t> (start + i)]
                                            * static_cast<float> (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / 4096));

        fft.performFrequencyOnlyForwardTransform (buffer.data());

        for (int bin = firstBin; bin <= lastBin; ++bin)
            energy += static_cast<double> (buffer[static_cast<size_t> (bin)]) * buffer[static_cast<size_t> (bin)];
    }

    return 10.0 * std::log10 (std::max (1e-30, energy));
}

/** The largest difference between the output and the input `latency` samples earlier, over
    [from, to) of the output, in every channel. */
float differenceFromDelayed (const Stereo& in, const Stereo& out, int latency, int from, int to = -1)
{
    auto largest = 0.0f;

    for (size_t channel = 0; channel < in.size(); ++channel)
    {
        const auto end = to < 0 ? static_cast<int> (out[channel].size()) : to;

        for (auto i = std::max (from, latency); i < end; ++i)
            largest = std::max (largest, std::abs (out[channel][static_cast<size_t> (i)]
                                                   - in[channel][static_cast<size_t> (i - latency)]));
    }

    return largest;
}

/** The largest second difference in channel 0 over [from, to): a click shows as a spike. */
float largestBend (const Stereo& audio, int from, int to)
{
    auto largest = 0.0f;

    for (auto i = std::max (2, from); i < to; ++i)
    {
        const auto* x = audio[0].data();
        largest = std::max (largest, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
    }

    return largest;
}

constexpr EffectMode allModes[] { EffectMode::carve, EffectMode::carveDraw, EffectMode::stencil, EffectMode::draw };

} // namespace

FREQART_TEST (effectHasItsOwnNameLayoutAndParameters)
{
    const auto effect = makeProcessor (Variant::effect, ArtDocument());
    const auto instrument = makeProcessor (Variant::instrument, ArtDocument());

    CHECK (effect->isEffect() && ! instrument->isEffect());
    CHECK (effect->getName() == "FreqArt FX");
    CHECK (instrument->getName() == "FreqArt");
    CHECK (effect->acceptsMidi() && ! effect->producesMidi() && ! effect->isMidiEffect());

    // Mode (Carve by default) and Depth (-80 dB), both automatable; the instrument has neither.
    auto* mode = effect->getParameters().getParameter (params::id::mode);
    auto* depth = effect->getParameters().getParameter (params::id::depth);
    CHECK (mode != nullptr && depth != nullptr);
    CHECK (mode->getCurrentValueAsText() == "Carve");
    CHECK (depth->getCurrentValueAsText() == "-80 dB");
    CHECK (mode->isAutomatable() && depth->isAutomatable());
    CHECK (mode->getNumSteps() == 4);
    CHECK (params::modeNames().joinIntoString ("|") == "Carve|Carve + Draw|Stencil|Draw");
    CHECK_NEAR (depth->convertFrom0to1 (0.0f), -120.0f, 1.0e-4);
    CHECK (effect->getEffectMode() == EffectMode::carve);
    CHECK_NEAR (effect->getDepthDb(), -80.0f, 1.0e-4);

    CHECK (instrument->getParameters().getParameter (params::id::mode) == nullptr);
    CHECK (instrument->getParameters().getParameter (params::id::depth) == nullptr);
    CHECK (instrument->getParameters().getParameter (params::id::level) != nullptr);
    CHECK (effect->getParameters().getParameter (params::id::level) != nullptr);
    CHECK (instrument->getLatencySamples() == 0);

    // As many channels out as in, mono or stereo.
    using Set = juce::AudioChannelSet;
    const auto layout = [] (const Set& in, const Set& out)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        return l;
    };

    CHECK (effect->checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo())));
    CHECK (effect->checkBusesLayoutSupported (layout (Set::mono(), Set::mono())));
    CHECK (! effect->checkBusesLayoutSupported (layout (Set::mono(), Set::stereo())));
    CHECK (! effect->checkBusesLayoutSupported (layout (Set::disabled(), Set::stereo())));
    CHECK (effect->getBusCount (true) == 1 && instrument->getBusCount (true) == 0);
}

FREQART_TEST (effectLatencyIsConstant)
{
    // One frame of the carver, about 85.3 ms at every rate, in every mode, whatever is drawn.
    for (const auto rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        auto effect = makeProcessor (Variant::effect, ArtDocument::createDefault(), {}, rate);
        const auto latency = effect->getLatencySamples();
        CHECK_NEAR (latency / rate, 4096.0 / 48000.0, 0.001);

        const auto input = noise (static_cast<int> (rate / 4));

        for (const auto mode : allModes)
        {
            setParameter (*effect, params::id::mode, static_cast<float> (static_cast<int> (mode)));
            process (*effect, input);
            CHECK (effect->getLatencySamples() == latency);
        }

        effect->setDocument (ArtDocument());
        effect->rebuildPlanIfNeeded();
        process (*effect, input);
        CHECK (effect->getLatencySamples() == latency);
    }

    CHECK (makeProcessor (Variant::effect, ArtDocument())->getLatencySamples() == 4096);
}

FREQART_TEST (effectLeavesTheAudioAloneOutsideThePass)
{
    // A pass of one bar from bar 2 (2 s to 4 s at 120 BPM) over 800 - 1250 Hz. Before it, after
    // it (and its 5 ms fade) and with the DAW stopped, the output is the input one latency later,
    // bit for bit, in every mode; inside it the band is carved (in Draw it is not, and the
    // drawing's sines are added).
    const auto document = bandDocument ({ { 800.0, 1250.0 } });
    const auto input = noise (5 * 48000, 3);

    for (const auto mode : allModes)
    {
        auto effect = makeProcessor (Variant::effect, document, settingsOf (mode, false, params::defaultDepthDb, 2, 2));
        const auto latency = effect->getLatencySamples();
        const auto out = process (*effect, input);

        CHECK_NEAR (differenceFromDelayed (input, out, latency, 0, 2 * 48000 + latency), 0.0, 0.0);
        CHECK_NEAR (differenceFromDelayed (input, out, latency, 4 * 48000 + latency + 480), 0.0, 0.0);

        const auto inside = bandDb (out, 900.0, 1100.0, 2 * 48000 + latency + 4800, 4 * 48000 + latency - 4800)
                          - bandDb (input, 900.0, 1100.0, 2 * 48000 + 4800, 4 * 48000 - 4800);

        if (mode == EffectMode::draw)
            CHECK_GREATER (inside, 0.5);
        else if (mode == EffectMode::stencil)
            CHECK_NEAR (inside, 0.0, 1.0);
        else if (mode == EffectMode::carve)
            CHECK_LESS (inside, -40.0);

        // The DAW stopped: nothing happens at all.
        auto stopped = makeProcessor (Variant::effect, document, settingsOf (mode, true));
        Run run;
        run.playing = false;
        const auto still = process (*stopped, input, run);
        CHECK_NEAR (differenceFromDelayed (input, still, latency, 0), 0.0, 0.0);
        CHECK (! stopped->isSounding());
    }
}

FREQART_TEST (effectLeavesTheAudioAloneWithNothingDrawn)
{
    // Nothing drawn (or everything hidden): every mode passes the audio through untouched, as
    // late as ever (Stencil does not mute it all, and Draw has no sines to add).
    const auto input = noise (2 * 48000, 5);
    auto hidden = ArtDocument::createDefault();

    for (auto& object : hidden.objects)
        object.visible = false;

    for (const auto& document : { ArtDocument(), hidden })
    {
        for (const auto mode : allModes)
        {
            auto effect = makeProcessor (Variant::effect, document, settingsOf (mode, true));
            const auto out = process (*effect, input);
            CHECK_NEAR (differenceFromDelayed (input, out, effect->getLatencySamples(), 0), 0.0, 0.0);
        }
    }
}

FREQART_TEST (effectCarvesAndStencilsASineInADrawnBand)
{
    // Through the real processor: a 1 kHz sine in a drawn band of 800 - 1250 Hz and a 4 kHz one
    // outside it. Carve drops the first by Depth (within a few dB) and leaves the second alone;
    // Stencil the other way round.
    const auto document = bandDocument ({ { 800.0, 1250.0 } });
    const auto input = sines (3 * 48000, { { 1000.0, 0.25f }, { 4000.0, 0.25f } });
    const auto from = 48000, to = 3 * 48000;

    for (const auto depth : { -80.0f, -40.0f })
    {
        auto carve = makeProcessor (Variant::effect, document, settingsOf (EffectMode::carve, true, depth));
        const auto latency = carve->getLatencySamples();
        const auto out = process (*carve, input);
        const auto drop = [&] (double hz) { return levelDb (out, hz, from, to) - levelDb (input, hz, from - latency, to - latency); };

        CHECK_LESS (drop (1000.0), depth + 4.0f);
        CHECK_GREATER (drop (1000.0), depth - 6.0f);
        CHECK_NEAR (drop (4000.0), 0.0, 0.1);

        // The other channel is carved the same way.
        CHECK_LESS (levelDb (out, 1000.0, from, to, defaultRate, 1) - levelDb (input, 1000.0, from - latency, to - latency, defaultRate, 1),
                    depth + 4.0f);
    }

    auto stencil = makeProcessor (Variant::effect, document, settingsOf (EffectMode::stencil, true));
    const auto latency = stencil->getLatencySamples();
    const auto out = process (*stencil, input);
    CHECK_NEAR (levelDb (out, 1000.0, from, to) - levelDb (input, 1000.0, from - latency, to - latency), 0.0, 0.5);
    CHECK_LESS (levelDb (out, 4000.0, from, to) - levelDb (input, 4000.0, from - latency, to - latency), -70.0);
}

FREQART_TEST (effectCarveAndDrawPlaysTheDrawingInAWiderHole)
{
    // A band of 800 - 1000 Hz. Carve + Draw plays the drawing's sines there and carves a little
    // wider than the drawing (4 rows and 3 bins): a sine at 1020 Hz, just above the drawing, is
    // carved deeply, where Carve (one bin) leaves most of it. One far away is left alone. The
    // sines follow Level.
    const auto document = bandDocument ({ { 800.0, 1000.0 } });
    const auto input = sines (3 * 48000, { { 1020.0, 0.25f }, { 4000.0, 0.25f } });
    const auto from = 48000, to = 3 * 48000;

    const auto run = [&] (EffectMode mode, float levelDb)
    {
        auto effect = makeProcessor (Variant::effect, document, [&] (FreqArtProcessor& p)
        {
            settingsOf (mode, true) (p);
            setParameter (p, params::id::level, levelDb);
        });

        return std::make_pair (process (*effect, input), effect->getLatencySamples());
    };

    const auto [drawn, latency] = run (EffectMode::carveDraw, -6.0f);
    const auto [carved, carveLatency] = run (EffectMode::carve, -6.0f);
    const auto [quieter, quieterLatency] = run (EffectMode::carveDraw, -18.0f);
    CHECK (latency == carveLatency && latency == quieterLatency);

    const auto dropIn = [&] (const Stereo& out, double hz) { return levelDb (out, hz, from, to) - levelDb (input, hz, from - latency, to - latency); };

    CHECK_LESS (dropIn (drawn, 1020.0), -30.0);
    CHECK_GREATER (dropIn (carved, 1020.0), -12.0);
    CHECK_NEAR (dropIn (drawn, 4000.0), 0.0, 0.2);

    // The drawing sounds in its band (the input has nothing there), 12 dB quieter at Level -18.
    const auto sinesDb = bandDb (drawn, 830.0, 970.0, from, to);
    CHECK_GREATER (sinesDb - bandDb (carved, 830.0, 970.0, from, to), 30.0);
    CHECK_NEAR (sinesDb - bandDb (quieter, 830.0, 970.0, from, to), 12.0, 1.0);
}

FREQART_TEST (effectDrawAddsTheInstrumentsSoundAsLateAsTheAudio)
{
    // Draw leaves the audio as it is and adds the drawing's sines: what it adds is exactly what
    // the instrument plays (same plan, same pass, Level and clipper), one latency later, so the
    // sines line up with the audio they were played against.
    const auto document = ArtDocument::createDefault();
    const auto input = noise (3 * 48000, 9);
    const auto silence = Stereo (2, std::vector<float> (input[0].size(), 0.0f));

    auto effect = makeProcessor (Variant::effect, document, settingsOf (EffectMode::draw, false));
    auto instrument = makeProcessor (Variant::instrument, document, settingsOf (EffectMode::draw, false));
    const auto latency = effect->getLatencySamples();

    const auto out = process (*effect, input);
    const auto played = process (*instrument, silence);

    auto largest = 0.0f, loudest = 0.0f;

    for (size_t i = static_cast<size_t> (latency); i < out[0].size(); ++i)
    {
        const auto added = out[0][i] - input[0][i - static_cast<size_t> (latency)];
        largest = std::max (largest, std::abs (added - played[0][i - static_cast<size_t> (latency)]));
        loudest = std::max (loudest, std::abs (played[0][i]));
    }

    CHECK_GREATER (loudest, 0.01f);
    CHECK_LESS (largest, 1.0e-6f);
}

FREQART_TEST (effectBypassKeepsTheLatency)
{
    // Carving while playing, then the host bypasses it: the carve and the sines fade out within a
    // frame, then the output is the input as late as before, bit for bit (a host's bypass never
    // moves the audio). Back from bypass it carves again (measured in Carve: Carve + Draw plays sines
    // in the band). No clicks either way.
    const auto document = bandDocument ({ { 200.0, 400.0 } });
    const auto input = sines (4 * 48000, { { 300.0, 0.4f } });

    for (const auto mode : { EffectMode::carve, EffectMode::carveDraw })
    {
        auto effect = makeProcessor (Variant::effect, document, settingsOf (mode, true));
        const auto latency = effect->getLatencySamples();

        Run run;
        run.bypassed = [] (int start) { return start >= 48000 && start < 3 * 48000; };
        const auto out = process (*effect, input, run);

        CHECK (effect->getLatencySamples() == latency);
        CHECK_NEAR (differenceFromDelayed (input, out, latency, 48000 + 2 * latency + 1024, 3 * 48000), 0.0, 0.0);

        if (mode == EffectMode::carve)
            CHECK_LESS (levelDb (out, 300.0, 3 * 48000 + 2 * latency, 4 * 48000) - levelDb (input, 300.0, 3 * 48000, 4 * 48000 - 2 * latency), -60.0);

        // A 300 Hz sine of 0.4 bends by at most 0.4 (2 pi 300 / 48000)^2 = 0.0006 a sample.
        const auto steady = largestBend (input, 0, 4 * 48000);
        CHECK_LESS (largestBend (out, 48000 + latency - 4096, 48000 + 2 * latency + 4096), 20.0f * steady);
        CHECK_LESS (largestBend (out, 3 * 48000 + latency - 4096, 3 * 48000 + 2 * latency + 4096), 20.0f * steady);
    }
}

FREQART_TEST (effectModeAndDepthChangesDoNotClick)
{
    // A sine in a drawn low band, the modes switched every half second while playing and Depth
    // jumped from -80 to 0 to -120 dB: carving changes from frame to frame, crossfaded by the
    // overlapping windows, and the sines fade in and out over 20 ms, so the output never bends
    // more sharply than the sines themselves do (a click would bend it by far more).
    const auto document = bandDocument ({ { 150.0, 400.0 } });
    const auto input = sines (5 * 48000, { { 300.0, 0.4f }, { 3000.0, 0.02f } });

    auto effect = makeProcessor (Variant::effect, document, settingsOf (EffectMode::carve, true));
    const EffectMode sequence[] { EffectMode::carve, EffectMode::stencil, EffectMode::draw, EffectMode::carveDraw,
                                  EffectMode::carve, EffectMode::draw, EffectMode::stencil, EffectMode::carveDraw };

    Run run;
    run.beforeBlock = [&] (FreqArtProcessor& p, int start)
    {
        const auto step = start / 24000;

        if (step < 8)
            setParameter (p, params::id::mode, static_cast<float> (static_cast<int> (sequence[step])));

        setParameter (p, params::id::depth, start < 4 * 48000 ? -80.0f : (start < 4 * 48000 + 24000 ? 0.0f : -120.0f));
    };

    const auto out = process (*effect, input, run);

    // How sharply the sines alone bend: the instrument playing the same drawing.
    auto instrument = makeProcessor (Variant::instrument, document, settingsOf (EffectMode::carve, true));
    const auto played = process (*instrument, Stereo (2, std::vector<float> (input[0].size(), 0.0f)));
    const auto bound = 2.0f * (largestBend (input, 0, 5 * 48000) + largestBend (played, 0, 5 * 48000));

    CHECK_LESS (largestBend (out, 0, 5 * 48000), bound);
    CHECK_GREATER (largestBend (played, 0, 5 * 48000), 0.0f);
}

FREQART_TEST (effectStateRoundTrips)
{
    // FX states load in FX (Mode, Depth, the drawing); an FX state loaded into the instrument
    // keeps the drawing and the shared settings (and drops Mode and Depth), and an instrument
    // state loaded into FX keeps its drawing too, with Mode and Depth at their defaults.
    const auto document = ArtDocument::createDefault();
    auto effect = makeProcessor (Variant::effect, document, [] (FreqArtProcessor& p)
    {
        setParameter (p, params::id::mode, 2.0f);
        setParameter (p, params::id::depth, -50.0f);
        setParameter (p, params::id::level, -12.0f);
        setParameter (p, params::id::lowEnd, 1.0f);
    });

    juce::MemoryBlock fxState;
    effect->getStateInformation (fxState);

    auto again = makeProcessor (Variant::effect, ArtDocument());
    again->setStateInformation (fxState.getData(), static_cast<int> (fxState.getSize()));
    CHECK (again->getEffectMode() == EffectMode::stencil);
    CHECK_NEAR (again->getDepthDb(), -50.0f, 0.05);
    CHECK_NEAR (getParameter (*again, params::id::level), -12.0f, 0.05);
    CHECK_NEAR (getParameter (*again, params::id::lowEnd), 1.0f, 1.0e-4);
    CHECK (again->getDocument().objects.size() == document.objects.size());

    auto instrument = makeProcessor (Variant::instrument, ArtDocument());
    instrument->setStateInformation (fxState.getData(), static_cast<int> (fxState.getSize()));
    CHECK (instrument->getDocument().objects.size() == document.objects.size());
    CHECK_NEAR (getParameter (*instrument, params::id::level), -12.0f, 0.05);
    CHECK_NEAR (getParameter (*instrument, params::id::lowEnd), 1.0f, 1.0e-4);

    juce::MemoryBlock instrumentState;
    instrument->getStateInformation (instrumentState);
    const auto saved = juce::AudioProcessor::getXmlFromBinary (instrumentState.getData(), static_cast<int> (instrumentState.getSize()));
    CHECK (saved != nullptr && saved->getStringAttribute ("plugin") == "FreqArt");
    CHECK (saved != nullptr && saved->toString().contains ("\"depth\"") == false);

    auto fromInstrument = makeProcessor (Variant::effect, ArtDocument(), [] (FreqArtProcessor& p) { setParameter (p, params::id::mode, 3.0f); });
    fromInstrument->setStateInformation (instrumentState.getData(), static_cast<int> (instrumentState.getSize()));
    CHECK (fromInstrument->getDocument().objects.size() == document.objects.size());
    CHECK (fromInstrument->getEffectMode() == EffectMode::carve);
    CHECK_NEAR (fromInstrument->getDepthDb(), params::defaultDepthDb, 0.05);
    CHECK_NEAR (getParameter (*fromInstrument, params::id::level), -12.0f, 0.05);
}

FREQART_TEST (effectWorksInMonoAndStereo)
{
    // One channel in and out carves like each of two; the two channels of a stereo signal are
    // carved alike.
    const auto document = bandDocument ({ { 800.0, 1250.0 } });
    const auto from = 48000, to = 2 * 48000;

    auto mono = makeProcessor (Variant::effect, document, settingsOf (EffectMode::carve, true), defaultRate, 512, 1);
    CHECK (mono->getTotalNumInputChannels() == 1 && mono->getTotalNumOutputChannels() == 1);
    const auto monoIn = sines (2 * 48000, { { 1000.0, 0.25f }, { 4000.0, 0.25f } }, defaultRate, 1);
    const auto monoOut = process (*mono, monoIn);
    const auto latency = mono->getLatencySamples();
    CHECK_LESS (levelDb (monoOut, 1000.0, from, to) - levelDb (monoIn, 1000.0, from - latency, to - latency), -70.0);
    CHECK_NEAR (levelDb (monoOut, 4000.0, from, to) - levelDb (monoIn, 4000.0, from - latency, to - latency), 0.0, 0.1);

    auto stereo = makeProcessor (Variant::effect, document, settingsOf (EffectMode::carve, true));
    Stereo same (2, monoIn[0]);
    const auto stereoOut = process (*stereo, same);
    auto largest = 0.0f;

    for (size_t i = 0; i < stereoOut[0].size(); ++i)
        largest = std::max ({ largest, std::abs (stereoOut[0][i] - stereoOut[1][i]), std::abs (stereoOut[0][i] - monoOut[0][i]) });

    CHECK_LESS (largest, 1.0e-6f);
}

FREQART_TEST (effectFollowsSampleRatesAndBlockSizes)
{
    // 44.1 to 96 kHz carve alike; any block sizes (also longer than prepared, and single
    // samples) give the same output, in Carve + Draw too (the sines and the carve).
    const auto document = bandDocument ({ { 800.0, 1250.0 } });

    for (const auto rate : { 44100.0, 96000.0 })
    {
        auto effect = makeProcessor (Variant::effect, document, settingsOf (EffectMode::carve, true), rate);
        const auto total = static_cast<int> (2 * rate);
        const auto input = sines (total, { { 1000.0, 0.25f }, { 4000.0, 0.25f } }, rate);
        const auto out = process (*effect, input);
        const auto latency = effect->getLatencySamples();
        const auto from = static_cast<int> (rate);
        CHECK_LESS (levelDb (out, 1000.0, from, total, rate) - levelDb (input, 1000.0, from - latency, total - latency, rate), -65.0);
        CHECK_NEAR (levelDb (out, 4000.0, from, total, rate) - levelDb (input, 4000.0, from - latency, total - latency, rate), 0.0, 0.1);
    }

    const auto input = noise (3 * 48000, 21);
    const auto silence = Stereo (2, std::vector<float> (input[0].size(), 0.0f));

    juce::Random random (4);
    Run varied;
    varied.blocks.clear();

    for (int i = 0; i < 300; ++i)
        varied.blocks.push_back (i % 50 == 7 ? 1 : 1 + random.nextInt (4096));

    const auto largestDifference = [] (const Stereo& a, const Stereo& b)
    {
        auto largest = 0.0f;

        for (size_t channel = 0; channel < a.size(); ++channel)
            for (size_t i = 0; i < a[channel].size(); ++i)
                largest = std::max (largest, std::abs (a[channel][i] - b[channel][i]));

        return largest;
    };

    // The sines themselves move a little with where the blocks fall (the instrument's engine
    // ramps its amplitudes per block): Carve + Draw may differ by what the instrument does, and
    // by nothing more.
    auto instrument = makeProcessor (Variant::instrument, ArtDocument::createDefault(), settingsOf (EffectMode::carveDraw, true));
    auto instrumentVaried = makeProcessor (Variant::instrument, ArtDocument::createDefault(), settingsOf (EffectMode::carveDraw, true));
    const auto sinesMove = largestDifference (process (*instrument, silence), process (*instrumentVaried, silence, varied));

    for (const auto mode : { EffectMode::carve, EffectMode::stencil, EffectMode::carveDraw })
    {
        auto steady = makeProcessor (Variant::effect, ArtDocument::createDefault(), settingsOf (mode, true));
        const auto reference = process (*steady, input);

        auto other = makeProcessor (Variant::effect, ArtDocument::createDefault(), settingsOf (mode, true));
        const auto out = process (*other, input, varied);

        CHECK_LESS (largestDifference (out, reference), (mode == EffectMode::carveDraw ? sinesMove : 0.0f) + 1.0e-6f);
        CHECK_GREATER (differenceFromDelayed (input, reference, steady->getLatencySamples(), 0), 0.01f);
    }
}

FREQART_TEST (effectFollowsMidiAndPreview)
{
    // With the DAW stopped (hosts keep feeding effects audio), a MIDI note plays the pass (for
    // the hosts that send effects MIDI), and so does Preview: the audio coming in is carved from
    // then on, and not before.
    const auto document = bandDocument ({ { 800.0, 1250.0 } });
    const auto input = sines (3 * 48000, { { 1000.0, 0.25f } });

    auto midi = makeProcessor (Variant::effect, document, [] (FreqArtProcessor& p)
    {
        settingsOf (EffectMode::carve, false) (p);
        setParameter (p, params::id::trigger, 1.0f);
    });

    const auto latency = midi->getLatencySamples();
    Run run;
    run.playing = false;
    run.midi = [] (int start, int length, juce::MidiBuffer& buffer)
    {
        if (start <= 48000 && 48000 < start + length)
            buffer.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 48000 - start);
    };

    const auto out = process (*midi, input, run);
    CHECK_NEAR (differenceFromDelayed (input, out, latency, 0, 48000 + latency), 0.0, 0.0);
    CHECK_LESS (levelDb (out, 1000.0, 48000 + latency + 4800, 2 * 48000 + latency) - levelDb (input, 1000.0, 48000 + 4800, 2 * 48000), -60.0);

    auto preview = makeProcessor (Variant::effect, document, settingsOf (EffectMode::carve, false));
    Run stopped;
    stopped.playing = false;
    stopped.beforeBlock = [] (FreqArtProcessor& p, int start)
    {
        if (start == 24576)
            p.setPreview (true);
    };

    const auto previewed = process (*preview, input, stopped);
    CHECK_NEAR (differenceFromDelayed (input, previewed, latency, 0, 24576 + latency), 0.0, 0.0);
    CHECK_LESS (levelDb (previewed, 1000.0, 24576 + latency + 4800, 24576 + 48000 + latency) - levelDb (input, 1000.0, 24576 + 4800, 24576 + 48000), -60.0);

    // The single pass of one bar ends after 2 s, and Preview turns itself off.
    CHECK (! preview->isPreviewing());
}
