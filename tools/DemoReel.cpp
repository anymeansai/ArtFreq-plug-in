// The demo reel: short clips of ArtFreq and ArtFreq FX at work, for the website
// (docs/marketing/README.md).
//
//   ArtFreqDemoReel <output-dir> [--work <dir>] [--clips artfreq,fx,fx-carve-draw] [--ffmpeg <path>]
//
// Nothing in a clip is mocked up. Each one films the real editor (the plug-in's own window, as a
// buyer sees it once activated) while the real processor plays, offline and frame by frame:
//
//   - The host is this tool: it calls processBlock in blocks of 400 samples at 48 kHz, four per
//     video frame (30 fps: 1600 samples a frame), with a transport that is stopped and then
//     plays from bar 1 at 120 BPM in 4/4 (Trigger Transport, start On play: the plug-ins'
//     defaults). ArtFreq FX gets a song as its input (a synthetic one made here: drums, bass,
//     chords, a pluck and hats, see makeSong); the instrument gets nothing.
//   - Per frame, after the frame's audio, the live view takes the analysis of what came out, as
//     its own 60 Hz timer does in a host: its timer callback is called directly, once per frame,
//     so every frame takes every column there is, none dropped and none twice, however long a
//     frame takes to draw. Then the window is drawn (createComponentSnapshot at 2x), and ffmpeg
//     scales it down (Lanczos) to 1280 px.
//   - The sound of the clip is the processor's own output for those same blocks (in the MP4 it
//     fades out over the last 0.25 s, so the clip does not end on a click).
//
// Each clip opens on the canvas with the DAW stopped (a second), the DAW starts, and the window
// switches to the live view at the start of the frame in which the output first sounds (switched
// earlier, it would show "Nothing is playing" over the silence until then). The DAW starts at
// whichever of the frame's four blocks brings that sound to the start of a frame, so the view's
// first frame already shows it. The tool finds all this by playing the clip without the editor
// first (the processors are deterministic) and checks that the filmed run's output is the same,
// sample for sample: the editor changes nothing in the sound.
//
// It checks that the picture is the sound: the live view's newest column ("now") in every
// frame, and every column of the whole live picture in the last frame, against an analysis of
// the clip's sound made here separately (as the live view analyses with the default settings:
// 8192 points, Hann, a column every 512 samples, the same rows and colours). A frame early or late, or a
// column dropped or doubled, would not match. And, with ffprobe, the encoded files: frames,
// durations, the sound.
//
// Clips (each: <name>.mp4, H.264 with the sound as AAC, <name>.gif, silent and looping, every
// other frame, its last frame held 1.5 s before it loops, and <name>-poster.png, the last frame):
//
//   artfreq-demo                 ArtFreq: "ArtFreq" in the high band over a picture made here (a
//                                striped sun going down behind mountains, 70 Hz - 2.8 kHz)
//   artfreq-fx-demo              ArtFreq FX in Carve: "AnyMeans" carved into the song
//   artfreq-fx-carve-draw-demo   ArtFreq FX in Carve + Draw: the same, played into the hole
//
// all with Length 2 bars (4 s at 120 BPM: the live view then shows 5 s). The work folder
// (default: ArtFreqDemoReel in the system's temporary folder) keeps every frame as a PNG at 1280 px
// (frames-<name>/), the clip's sound as a WAV (<name>.wav) and, for ArtFreq FX, the song going in
// (<name>-input.wav). Needs ffmpeg with libx264, and ffprobe (on the PATH, or --ffmpeg with
// ffprobe beside it). Every plug-in instance is activated against a fake licence server
// (tests/LicenseTestSupport.h), so no licence screen shows and nothing reaches the real server.
//
// On Linux run it under a virtual display large enough for the window (the live view runs only
// while its window is on screen):
//
//   xvfb-run -a -s "-screen 0 2400x1600x24" ArtFreqDemoReel docs/marketing --work /tmp/reel

#include "LicenseTestSupport.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Document.h"
#include "core/FrequencyScale.h"
#include "core/ImageProcessing.h"
#include "ui/CanvasAxes.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/LiveView.h"
#include "ui/Theme.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <vector>

namespace
{

using namespace artfreq;

constexpr double sampleRate = 48000.0;
constexpr int framesPerSecond = 30;
constexpr int samplesPerFrame = 1600;            // 48000 / 30
constexpr int blocksPerFrame = 4;
constexpr int blockSize = samplesPerFrame / blocksPerFrame;
constexpr int maxBlockSize = 512;                // what the "host" prepares the plug-in with
constexpr double tempo = 120.0;
constexpr float pixelScale = 2.0f;               // the window is drawn at 2x, then scaled down
constexpr int videoWidth = 1280;
constexpr int gifWidth = 800;
constexpr int gifFramesPerSecond = 15;           // every other frame: the GIF's size
constexpr int gifHoldCentiseconds = 150;         // the GIF's last frame stays 1.5 s before it loops
constexpr double soundFadeSeconds = 0.25;        // the MP4's sound fades out over its last 0.25 s

static_assert (samplesPerFrame * framesPerSecond == static_cast<int> (sampleRate), "whole samples per frame");
static_assert (blockSize * blocksPerFrame == samplesPerFrame && blockSize <= maxBlockSize, "blocks fill a frame");

bool failed = false;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::cerr << "CHECK FAILED: " << what << std::endl;
        failed = true;
    }
}

void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

void setParameter (ArtFreqProcessor& processor, const char* id, float plainValue)
{
    if (auto* parameter = processor.getParameters().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    else
        check (false, juce::String ("parameter ") + id);
}

juce::Component* findComponentWithId (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findComponentWithId (*child, id))
            return found;

    return nullptr;
}

using Stereo = std::array<std::vector<float>, 2>;

//==============================================================================
// The song ArtFreq FX carves: a short loop at 120 BPM in A minor (Am, F, C, G, two beats each),
// stereo, deterministic. Four on the floor, a clap on 2 and 4, closed hats on every sixteenth
// and an open one on the off-beats, a side-chained saw bass, a detuned saw pad, a plucked
// arpeggio, a room reverb on the clap, hats, pluck and pad, a quiet bed of air, and the highs
// lifted. Built so a spectrogram has something everywhere from the kick to 16 kHz, as a mastered
// song does.

struct OnePole
{
    explicit OnePole (double hz) { setCutoff (hz); }
    void setCutoff (double hz) { a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate); }
    double lowPass (double x) { y += a * (x - y); return y; }
    double highPass (double x) { return x - lowPass (x); }
    double a = 0.0, y = 0.0;
};

/** A band-limited sawtooth (polyBLEP). */
struct Saw
{
    double next (double hz)
    {
        const auto dt = hz / sampleRate;
        phase += dt;

        if (phase >= 1.0)
            phase -= 1.0;

        auto value = 2.0 * phase - 1.0;

        if (phase < dt)
        {
            const auto t = phase / dt;
            value -= t + t - t * t - 1.0;
        }
        else if (phase > 1.0 - dt)
        {
            const auto t = (phase - 1.0) / dt;
            value -= t * t + t + t + 1.0;
        }

        return value;
    }

    double phase = 0.0;
};

/** A state-variable filter (Chamberlin, twice oversampled), low-pass or band-pass. */
struct Svf
{
    double process (double x, double hz, double q, bool bandPass)
    {
        const auto f = 2.0 * std::sin (juce::MathConstants<double>::pi * std::min (hz, 18000.0) / (2.0 * sampleRate));
        const auto damping = 1.0 / q;

        for (int i = 0; i < 2; ++i)
        {
            low += f * band;
            const auto high = x - low - damping * band;
            band += f * high;
        }

        return bandPass ? band : low;
    }

    double low = 0.0, band = 0.0;
};

/** A small stereo room (Freeverb's combs and all-passes, at 48 kHz). */
class Room
{
public:
    Room()
    {
        const int combs[] { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        const int allPasses[] { 556, 441, 341, 225 };

        for (size_t side = 0; side < 2; ++side)
        {
            const auto spread = static_cast<int> (side) * 23;

            for (auto length : combs)
                comb[side].push_back ({ std::vector<float> (static_cast<size_t> ((length + spread) * 48 / 44), 0.0f) });

            for (auto length : allPasses)
                allPass[side].push_back ({ std::vector<float> (static_cast<size_t> ((length + spread) * 48 / 44), 0.0f) });
        }
    }

    std::array<float, 2> process (float input)
    {
        std::array<float, 2> out {};

        for (size_t side = 0; side < 2; ++side)
        {
            auto sum = 0.0f;

            for (auto& c : comb[side])
            {
                auto& value = c.buffer[c.position];
                c.store = value * 0.8f + c.store * 0.2f;   // damping
                sum += value;
                value = input + c.store * 0.82f;            // room size
                c.position = (c.position + 1) % c.buffer.size();
            }

            for (auto& a : allPass[side])
            {
                auto& value = a.buffer[a.position];
                const auto output = value - sum;
                value = sum + value * 0.5f;
                sum = output;
                a.position = (a.position + 1) % a.buffer.size();
            }

            out[side] = sum;
        }

        return out;
    }

private:
    struct Line
    {
        std::vector<float> buffer;
        size_t position = 0;
        float store = 0.0f;
    };

    std::array<std::vector<Line>, 2> comb, allPass;
};

double midiToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

Stereo makeSong (double seconds)
{
    const auto total = static_cast<int> (std::ceil (seconds * sampleRate));
    Stereo dry, send;

    for (auto* s : { &dry, &send })
        for (auto& channel : *s)
            channel.assign (static_cast<size_t> (total), 0.0f);

    juce::Random random (20261119);
    const auto noise = [&random] { return static_cast<double> (random.nextFloat()) * 2.0 - 1.0; };
    const auto add = [&] (Stereo& target, int index, double left, double right)
    {
        if (index >= 0 && index < total)
        {
            target[0][static_cast<size_t> (index)] += static_cast<float> (left);
            target[1][static_cast<size_t> (index)] += static_cast<float> (right);
        }
    };

    constexpr double beat = 60.0 / tempo;
    constexpr double sixteenth = beat / 4.0;
    const auto samplesOf = [] (double s) { return static_cast<int> (std::lround (s * sampleRate)); };

    // Chords, two beats each: Am, F, C, G (MIDI notes); the bass plays their roots.
    const int chords[4][4] { { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 55, 60, 64, 67 }, { 55, 59, 62, 67 } };
    const int roots[4] { 33, 29, 36, 31 };
    const auto chordAt = [&] (double t) { return static_cast<int> (t / (2.0 * beat)) % 4; };

    // The side-chain: everything but the drums ducks under each kick.
    const auto duck = [beat] (double t)
    {
        const auto since = std::fmod (t, beat);
        return 1.0 - 0.6 * std::exp (-since / 0.09);
    };

    // Kick: a sine falling from 150 Hz to 50 Hz, a click on top.
    for (double onset = 0.0; onset < seconds; onset += beat)
    {
        auto phase = 0.0;
        const auto start = samplesOf (onset);

        for (int i = 0; i < samplesOf (0.42); ++i)
        {
            const auto t = i / sampleRate;
            phase += juce::MathConstants<double>::twoPi * (50.0 + 100.0 * std::exp (-t / 0.03)) / sampleRate;
            auto value = 0.5 * std::min (1.0, i / 24.0) * std::exp (-t / 0.2) * std::sin (phase);

            if (i < 120)
                value += 0.12 * noise() * (1.0 - i / 120.0);

            add (dry, start + i, value, value);
        }
    }

    // Clap on 2 and 4: three quick bursts of band-passed noise and a tail, into the room.
    for (double onset = beat; onset < seconds; onset += 2.0 * beat)
    {
        Svf filter;
        const auto start = samplesOf (onset);

        for (int i = 0; i < samplesOf (0.35); ++i)
        {
            const auto t = i / sampleRate;
            auto envelope = std::exp (-t / 0.09);

            for (const auto burst : { 0.0, 0.011, 0.022 })
                if (t >= burst && t < burst + 0.009)
                    envelope = std::max (envelope, 1.6 * std::exp (-(t - burst) / 0.004));

            const auto value = 0.33 * envelope * filter.process (noise(), 1300.0, 1.4, true);
            add (dry, start + i, value * 0.95, value);
            add (send, start + i, value * 0.6, value * 0.6);
        }
    }

    // Hats: closed on every sixteenth (accents on the off-beats), open on the off-beat eighths.
    for (int step = 0; step * sixteenth < seconds; ++step)
    {
        const auto open = step % 4 == 2;
        const auto start = samplesOf (step * sixteenth);
        const auto level = open ? 0.11 : (step % 2 == 1 ? 0.085 : 0.06);
        const auto decay = open ? 0.2 : 0.045;
        const auto pan = open ? 1.0 : (step % 2 == 0 ? 0.75 : 1.25);
        OnePole first (7000.0), second (7000.0);

        for (int i = 0; i < samplesOf (open ? 0.5 : 0.15); ++i)
        {
            const auto value = level * std::exp (-(i / sampleRate) / decay) * second.highPass (first.highPass (noise()));
            add (dry, start + i, value * pan, value * (2.0 - pan));
            add (send, start + i, value * 0.35, value * 0.35);
        }
    }

    // Bass: a saw an octave below the chord's root, on the eighths, low-passed, ducking.
    {
        Saw saw;
        Svf filter;
        const auto eighth = beat / 2.0;

        for (int i = 0; i < total; ++i)
        {
            const auto t = i / sampleRate;
            const auto inNote = std::fmod (t, eighth);
            const auto step = static_cast<int> (t / eighth);
            const auto note = roots[chordAt (t)] + (step % 4 == 3 ? 12 : 0);
            const auto envelope = std::min (1.0, inNote / 0.004) * std::min (1.0, (eighth - inNote) / 0.015);
            const auto cutoff = 180.0 + 700.0 * std::exp (-inNote / 0.06);
            const auto value = 0.25 * envelope * duck (t) * filter.process (saw.next (midiToHz (note)), cutoff, 1.2, false);
            add (dry, i, value, value);
        }
    }

    // Pad: the chord on detuned saws, wide, low-passed, ducking.
    for (int voice = 0; voice < 4; ++voice)
    {
        for (const auto detune : { -0.12, 0.0, 0.12 })
        {
            Saw saw;
            saw.phase = 0.13 * voice + 0.31 * (detune + 0.2);
            OnePole first (4500.0), second (4500.0);
            const auto pan = 1.0 + detune * 4.0;

            for (int i = 0; i < total; ++i)
            {
                const auto t = i / sampleRate;
                const auto inChord = std::fmod (t, 2.0 * beat);
                const auto envelope = std::min (1.0, inChord / 0.05) * std::min (1.0, (2.0 * beat - inChord) / 0.05);
                const auto hz = midiToHz (chords[chordAt (t)][voice] + detune);
                const auto value = 0.02 * envelope * duck (t) * second.lowPass (first.lowPass (saw.next (hz)));
                add (dry, i, value * pan, value * (2.0 - pan));
                add (send, i, value * 0.5, value * 0.5);
            }
        }
    }

    // Pluck: the chord's notes an octave up, one per sixteenth, a filter that closes fast.
    {
        Saw saw;
        Svf filter;
        const int order[] { 0, 1, 2, 3, 2, 1, 3, 2 };

        for (int i = 0; i < total; ++i)
        {
            const auto t = i / sampleRate;
            const auto step = static_cast<int> (t / sixteenth);
            const auto inNote = std::fmod (t, sixteenth);
            const auto note = chords[chordAt (t)][order[step % 8]] + 12;
            const auto envelope = std::min (1.0, inNote / 0.002) * std::exp (-inNote / 0.09);
            const auto cutoff = 600.0 + 5000.0 * std::exp (-inNote / 0.05);
            const auto value = 0.075 * envelope * filter.process (saw.next (midiToHz (note)), cutoff, 2.0, false);
            const auto pan = step % 2 == 0 ? 0.7 : 1.3;
            add (dry, i, value * pan, value * (2.0 - pan));
            add (send, i, value * 0.7, value * 0.7);
        }
    }

    // Air: high-passed noise, very quiet, the cymbals' wash.
    {
        OnePole left (5000.0), right (5000.0);

        for (int i = 0; i < total; ++i)
            add (dry, i, 0.02 * left.highPass (noise()), 0.02 * right.highPass (noise()));
    }

    // The room, mixed in.
    {
        Room room;

        for (int i = 0; i < total; ++i)
        {
            const auto wet = room.process (0.5f * (send[0][static_cast<size_t> (i)] + send[1][static_cast<size_t> (i)]));
            add (dry, i, 0.22 * wet[0], 0.22 * wet[1]);
        }
    }

    // Mastering, of a kind: a broad lift of the highs (about +3 dB at 1 kHz, +8 dB at 5 kHz and
    // +10 dB at 15 kHz), as a mastered song is brighter than its parts.
    for (auto& channel : dry)
    {
        OnePole presence (2000.0), air (7000.0);

        for (auto& x : channel)
        {
            const auto in = static_cast<double> (x);
            x = static_cast<float> (in + 1.2 * presence.highPass (in) + 1.0 * air.highPass (in));
        }
    }

    // Peaks at -3 dBFS: room for what carving does to the peaks (docs/DESIGN.md, "Peaks").
    auto peak = 0.0f;

    for (const auto& channel : dry)
        for (auto x : channel)
            peak = std::max (peak, std::abs (x));

    for (auto& channel : dry)
        for (auto& x : channel)
            x *= 0.708f / peak;

    return dry;
}

//==============================================================================
// The pictures, made here.

/** A striped sun going down behind mountains, on a transparent ground (ArtFreq's Auto mode then
    plays its alpha: opaque is loud). 720 x 480. */
juce::Image makeSunsetPicture()
{
    constexpr int width = 720, height = 480;
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics g (image);

    // The sun: a disc, fading a little towards its foot, cut by bands that widen downwards.
    const auto sun = juce::Rectangle<float> (220.0f, 34.0f, 280.0f, 280.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0.0f, sun.getY(),
                                             juce::Colours::white.withAlpha (0.78f), 0.0f, sun.getBottom(), false));
    g.fillEllipse (sun);

    for (auto y = sun.getCentreY() - 6.0f, gap = 8.0f; y < sun.getBottom(); y += gap + 20.0f, gap += 5.0f)
        image.clear ({ 0, juce::roundToInt (y), width, juce::roundToInt (gap) });

    // Mountains in front: two ridges, the far one dimmer, each with a dark edge that keeps it
    // apart from what is behind it.
    const auto ridge = [&] (const std::vector<juce::Point<float>>& points, float alpha)
    {
        // The outline runs outside the picture except along the ridge, so only the ridge has an edge.
        juce::Path path;
        path.startNewSubPath (-40.0f, static_cast<float> (height) + 40.0f);
        path.lineTo (-40.0f, points.front().y);

        for (const auto& p : points)
            path.lineTo (p);

        path.lineTo (static_cast<float> (width) + 40.0f, points.back().y);
        path.lineTo (static_cast<float> (width) + 40.0f, static_cast<float> (height) + 40.0f);
        path.closeSubPath();

        juce::Path edge;
        juce::PathStrokeType (16.0f, juce::PathStrokeType::curved).createStrokedPath (edge, path);

        // Clear the edge and the inside, then fill the inside at the ridge's own level.
        {
            juce::Image cut (juce::Image::ARGB, width, height, true);
            juce::Graphics cg (cut);
            cg.setColour (juce::Colours::white);
            cg.fillPath (path);
            cg.fillPath (edge);

            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    if (cut.getPixelAt (x, y).getAlpha() > 0)
                    {
                        const auto keep = 1.0f - cut.getPixelAt (x, y).getFloatAlpha();
                        image.setPixelAt (x, y, image.getPixelAt (x, y).withMultipliedAlpha (keep));
                    }
        }

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (alpha), 0.0f, 260.0f,
                                                 juce::Colours::white.withAlpha (alpha * 0.7f), 0.0f, static_cast<float> (height), false));
        g.fillPath (path);
    };

    ridge ({ { 0.0f, 330.0f }, { 70.0f, 270.0f }, { 150.0f, 318.0f }, { 250.0f, 236.0f }, { 330.0f, 300.0f },
             { 400.0f, 268.0f }, { 470.0f, 316.0f }, { 560.0f, 224.0f }, { 650.0f, 300.0f }, { 720.0f, 262.0f } }, 0.62f);
    ridge ({ { 0.0f, 400.0f }, { 110.0f, 340.0f }, { 210.0f, 392.0f }, { 330.0f, 330.0f }, { 450.0f, 396.0f },
             { 560.0f, 344.0f }, { 640.0f, 380.0f }, { 720.0f, 350.0f } }, 0.9f);

    return image;
}

//==============================================================================
// The clips.

struct ClipSpec
{
    juce::String name;          // file stem
    bool effect = false;
    EffectMode mode = EffectMode::carve;
    double canvasSeconds = 1.0; // on the canvas, the DAW stopped
    double playSeconds = 4.8;   // from pressing play to the end
};

/** The drawing of a clip, as a buyer would have made it. */
ArtDocument makeDrawing (const ClipSpec& clip)
{
    ArtDocument document;

    if (! clip.effect)
    {
        auto picture = ArtDocument::makeImageObject (prepareImportedImage (makeSunsetPicture()), 70.0f, 2800.0f);
        picture.fileName = "sunset.png";
        picture.startTime = 0.2f;
        picture.endTime = 0.8f;
        document.objects.push_back (picture);

        const auto high = bandRange (Band::high, FrequencyScale {});
        auto text = ArtDocument::makeTextObject ("ArtFreq", static_cast<float> (high.low), static_cast<float> (high.high));
        text.startTime = 0.04f;
        text.endTime = 0.96f;
        document.objects.push_back (text);
    }
    else
    {
        auto text = ArtDocument::makeTextObject ("AnyMeans", 300.0f, 12000.0f);
        text.startTime = 0.08f;
        text.endTime = 0.92f;
        document.objects.push_back (text);
    }

    return document;
}

/** Makes a processor of the clip's kind with its drawing and settings. The drawing arrives as a
    saved state (as when a project opens), so there is no undo step in the window. */
std::unique_ptr<ArtFreqProcessor> makeProcessor (const ClipSpec& clip)
{
    const auto variant = clip.effect ? ArtFreqProcessor::Variant::effect : ArtFreqProcessor::Variant::instrument;
    juce::MemoryBlock state;

    {
        ArtFreqProcessor source (variant);
        source.setDocument (makeDrawing (clip));
        source.rebuildPlanIfNeeded();
        source.getStateInformation (state);
    }

    auto processor = std::make_unique<ArtFreqProcessor> (variant);

    if (clip.effect)
        processor->setPlayConfigDetails (2, 2, sampleRate, maxBlockSize);
    else
        processor->setPlayConfigDetails (0, 2, sampleRate, maxBlockSize);

    processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    // 2 bars (4 s at 120 BPM): the live view then shows 5 s, the whole drawing and a quarter more.
    setParameter (*processor, params::id::length, 3.0f);
    check (params::lengthNames()[3] == "2 bars", "length index 3 is 2 bars");

    if (! clip.effect)
        setParameter (*processor, params::id::density, 512.0f);

    if (clip.effect)
        setParameter (*processor, params::id::mode, static_cast<float> (static_cast<int> (clip.mode)));

    processor->prepareToPlay (sampleRate, maxBlockSize);
    processor->rebuildPlanIfNeeded();
    return processor;
}

/** The DAW's transport: stopped until `playSample`, then playing from bar 1. */
struct Transport final : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }

    void moveTo (int64_t sample, int64_t playSample)
    {
        const auto playing = sample >= playSample;
        const auto songSample = playing ? sample - playSample : 0;
        const auto seconds = static_cast<double> (songSample) / sampleRate;
        const auto ppq = seconds * tempo / 60.0;

        info = {};
        info.setIsPlaying (playing);
        info.setBpm (tempo);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setTimeInSamples (songSample);
        info.setTimeInSeconds (seconds);
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
    }

    PositionInfo info;
};

/** Plays one frame of the clip through the processor: four blocks, the transport following.
    Appends the output to `out`; the input is the song (silence while the DAW is stopped). */
void playFrame (ArtFreqProcessor& processor, Transport& transport, int frame, int64_t playSample,
                const Stereo& song, Stereo& out)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < blocksPerFrame; ++block)
    {
        const auto start = static_cast<int64_t> (frame) * samplesPerFrame + block * blockSize;
        transport.moveTo (start, playSample);
        buffer.clear();

        if (processor.isEffect())
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto songIndex = start + i - playSample;

                if (songIndex >= 0 && songIndex < static_cast<int64_t> (song[0].size()))
                    for (int channel = 0; channel < 2; ++channel)
                        buffer.setSample (channel, i, song[static_cast<size_t> (channel)][static_cast<size_t> (songIndex)]);
            }
        }

        processor.processBlock (buffer, midi);

        for (int channel = 0; channel < 2; ++channel)
            out[static_cast<size_t> (channel)].insert (out[static_cast<size_t> (channel)].end(),
                                                      buffer.getReadPointer (channel), buffer.getReadPointer (channel) + blockSize);
    }
}

bool writeWav (const Stereo& audio, const juce::File& file)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

    if (! dynamic_cast<juce::FileOutputStream*> (stream.get())->openedOk())
        return false;

    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (sampleRate)
                             .withNumChannels (2)
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);

    if (writer == nullptr)
        return false;

    const float* channels[] { audio[0].data(), audio[1].data() };
    return writer->writeFromFloatArrays (channels, 2, static_cast<int> (audio[0].size()));
}

float peakOf (const Stereo& audio, size_t from = 0, size_t to = std::numeric_limits<size_t>::max())
{
    auto peak = 0.0f;

    for (const auto& channel : audio)
    {
        const auto end = std::min (to, channel.size());

        for (auto* x = channel.data() + std::min (from, end); x < channel.data() + end; ++x)
            peak = std::max (peak, std::abs (*x));
    }

    return peak;
}

juce::String quoted (const juce::String& s) { return s.quoted(); }

int run (const juce::String& command)
{
    std::cout << "  $ " << command << std::endl;
    return std::system (command.toRawUTF8());
}

/** Runs a command and returns what it printed. */
juce::String capture (const juce::String& command)
{
    juce::String result;

    if (auto* pipe = popen (command.toRawUTF8(), "r"))
    {
        char buffer[512];

        while (std::fgets (buffer, sizeof (buffer), pipe) != nullptr)
            result += juce::String::fromUTF8 (buffer);

        pclose (pipe);
    }

    return result;
}

/** ffprobe's value of one entry for one stream ("v:0", "a:0") or the container (empty stream). */
juce::String probe (const juce::String& ffprobe, const juce::File& file, const juce::String& stream, const juce::String& entry,
                    bool countFrames = false)
{
    const auto entries = stream.isEmpty() ? "format=" + entry : "stream=" + entry;
    return capture (quoted (ffprobe) + " -v error" + (countFrames ? " -count_frames" : "")
                    + (stream.isNotEmpty() ? " -select_streams " + stream : juce::String())
                    + " -show_entries " + entries + " -of default=noprint_wrappers=1:nokey=1 "
                    + quoted (file.getFullPathName())).trim();
}

/** The encoded files: their frames, durations and sound, as made. */
void checkFiles (const ClipSpec& clip, const juce::String& ffprobe, const juce::File& mp4, const juce::File& gif, int totalFrames)
{
    const auto seconds = totalFrames / static_cast<double> (framesPerSecond);
    const auto gifFrames = (totalFrames + 1) / 2;

    const auto videoFrames = probe (ffprobe, mp4, "v:0", "nb_frames").getIntValue();
    const auto videoSeconds = probe (ffprobe, mp4, "v:0", "duration").getDoubleValue();
    const auto audioSeconds = probe (ffprobe, mp4, "a:0", "duration").getDoubleValue();
    const auto size = probe (ffprobe, mp4, "v:0", "width") + "x" + probe (ffprobe, mp4, "v:0", "height");
    const auto codecs = probe (ffprobe, mp4, "v:0", "codec_name") + " " + probe (ffprobe, mp4, "v:0", "pix_fmt") + ", "
                      + probe (ffprobe, mp4, "a:0", "codec_name") + " " + probe (ffprobe, mp4, "a:0", "sample_rate") + " Hz";
    const auto loopedFrames = probe (ffprobe, gif, "v:0", "nb_read_frames", true).getIntValue();
    const auto gifSize = probe (ffprobe, gif, "v:0", "width") + "x" + probe (ffprobe, gif, "v:0", "height");

    std::cout << "  " << mp4.getFileName() << ": " << size << ", " << codecs << ", " << videoFrames << " frames, video "
              << videoSeconds << " s, sound " << audioSeconds << " s, " << mp4.getSize() << " bytes" << std::endl;
    std::cout << "  " << gif.getFileName() << ": " << gifSize << ", " << loopedFrames << " frames, " << gif.getSize() << " bytes" << std::endl;

    check (videoFrames == totalFrames, clip.name + ".mp4 has every frame");
    check (std::abs (videoSeconds - seconds) < 0.001 && std::abs (audioSeconds - seconds) < 0.03,
           clip.name + ".mp4: the picture and the sound last the clip");
    check (codecs.startsWith ("h264 yuv420p, aac 48000"), clip.name + ".mp4: H.264 in yuv420p, AAC at 48 kHz");
    check (loopedFrames == gifFrames, clip.name + ".gif has every other frame");
    const auto gifSeconds = probe (ffprobe, gif, "", "duration").getDoubleValue();
    const auto gifExpected = (gifFrames - 1) / static_cast<double> (gifFramesPerSecond) + gifHoldCentiseconds / 100.0;
    std::cout << "  " << gif.getFileName() << ": " << gifSeconds << " s a loop (the last frame held " << gifHoldCentiseconds / 100.0 << " s)" << std::endl;
    check (std::abs (gifSeconds - gifExpected) < 0.05, clip.name + ".gif holds its last frame before it loops");
    check (gif.getSize() < 6 * 1024 * 1024, clip.name + ".gif is under 6 MB");
}

// ---- the picture against the sound ------------------------------------------------------------
// The live view's colour window and analysis, as src/ui/LiveView.cpp and src/dsp/LiveAnalyzer.h
// define them with the default analyser settings (a full-scale sine reads 0 dB; the colours run
// from -100 to 0 dB; FFT 8192, Hann, a column every sixteenth of it).
constexpr float viewFloorDb = -100.0f;
constexpr float viewCeilingDb = 0.0f;

/** The colour-map level (0..255) a pixel of the live view shows, by the nearest colour of the map
    (grid lines and the frame lie over some pixels: their nearest colour is near their own). */
int levelOfPixel (juce::Colour c)
{
    const auto& table = ui::theme::spectrumTable();
    auto best = 0;
    auto bestDistance = std::numeric_limits<int>::max();

    for (int i = 0; i < 256; ++i)
    {
        const auto& t = table[static_cast<size_t> (i)];
        const auto dr = static_cast<int> (t.getRed()) - c.getRed();
        const auto dg = static_cast<int> (t.getGreen()) - c.getGreen();
        const auto db = static_cast<int> (t.getBlue()) - c.getBlue();
        const auto distance = dr * dr + dg * dg + db * db;

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

/** An analysis of the clip's sound made here, independently of the live view's code: an analyser
    of 8192 points (Hann) with a column every 512 samples from when the view was turned
    on (nothing before that), each column as the colour-map levels of the view's rows (`rows`
    tall over the scale). */
class SoundAnalysis
{
public:
    SoundAnalysis (const Stereo& output, int64_t startSample, int rowsToUse, const FrequencyScale& scaleToUse)
        : start (startSample), rows (rowsToUse), scale (scaleToUse.sanitised())
    {
        mono.resize (output[0].size());

        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (output[0][i] + output[1][i]);

        double sum = 0.0;

        for (int i = 0; i < size; ++i)
        {
            const auto x = juce::MathConstants<double>::twoPi * i / size;
            window[static_cast<size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (x));
            sum += window[static_cast<size_t> (i)];
        }

        magnitudeScale = static_cast<float> (2.0 / sum);
    }

    static constexpr int order = 13, size = 1 << order, hop = size / 16;

    /** How many columns the analyser has made by `sample`. */
    int64_t columnsBy (int64_t sample) const { return std::max<int64_t> (0, (sample - start) / hop); }

    /** Column `n` (0 = the first after the view was turned on), as levels of the rows. */
    std::vector<int> column (int64_t n)
    {
        const auto end = start + (n + 1) * hop;

        for (int i = 0; i < size; ++i)
        {
            const auto index = end - size + i;
            const auto x = index >= start && index < static_cast<int64_t> (mono.size()) ? mono[static_cast<size_t> (index)] : 0.0f;
            work[static_cast<size_t> (i)] = x * window[static_cast<size_t> (i)];
        }

        std::fill (work.begin() + size, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        std::vector<float> db (size / 2 + 1);

        for (size_t bin = 0; bin < db.size(); ++bin)
            db[bin] = std::max (-160.0f, 20.0f * std::log10 (std::max (1.0e-8f, work[bin] * magnitudeScale)));

        std::vector<int> levels (static_cast<size_t> (rows), 0);
        const auto binHz = sampleRate / size;

        for (int y = 0; y < rows; ++y)
        {
            const auto top = scale.toHz (1.0 - static_cast<double> (y) / rows);
            const auto bottom = scale.toHz (1.0 - static_cast<double> (y + 1) / rows);
            const auto centre = scale.toHz (1.0 - (static_cast<double> (y) + 0.5) / rows);
            const auto first = std::max (0, static_cast<int> (std::ceil (bottom / binHz)));
            const auto last = std::min (static_cast<int> (db.size()) - 1, static_cast<int> (std::floor (top / binHz)));
            // The spectrum between the bins around the row's centre, or the loudest bin inside it.
            const auto position = static_cast<float> (std::clamp (centre / binHz, 0.0, static_cast<double> (db.size() - 2)));
            const auto i = static_cast<size_t> (position);
            auto value = db[i] + (db[i + 1] - db[i]) * (position - static_cast<float> (i));

            for (auto bin = first; bin <= last; ++bin)
                value = std::max (value, db[static_cast<size_t> (bin)]);

            const auto t = (value - viewFloorDb) / (viewCeilingDb - viewFloorDb);
            levels[static_cast<size_t> (y)] = juce::jlimit (0, 255, static_cast<int> (t * 255.0f + 0.5f));
        }

        return levels;
    }

private:
    int64_t start;
    int rows;
    FrequencyScale scale;
    std::vector<float> mono;
    juce::dsp::FFT fft { order };
    std::vector<float> window = std::vector<float> (size), work = std::vector<float> (2 * size);
    float magnitudeScale = 1.0f;
};

/** Does the live view show the sound when it sounds? Frame by frame from the live view on, the
    newest column of the live view's picture (its right edge, "now") against the analysis made
    above of the clip's own sound up to the end of that frame: they should match with no shift
    between them, and match worse shifted by a frame either way (the picture a frame late or
    early). Returns the mean difference at no shift, in colour-map levels (of 255). */
double checkSync (const ClipSpec& clip, const std::vector<std::vector<int>>& picture, const Stereo& output, int liveFrame,
                  const FrequencyScale& scale)
{
    const auto frames = static_cast<int> (picture.size());
    const auto rows = static_cast<int> (picture.front().size());
    SoundAnalysis analysis (output, static_cast<int64_t> (liveFrame) * samplesPerFrame, rows, scale);
    std::vector<std::vector<int>> sound;   // the newest column at the end of each frame's audio

    for (int f = liveFrame; f < liveFrame + frames; ++f)
        sound.push_back (analysis.column (analysis.columnsBy (static_cast<int64_t> (f + 1) * samplesPerFrame) - 1));

    constexpr int maxShift = 3;
    std::vector<double> errors;
    juce::String table;

    for (int shift = -maxShift; shift <= maxShift; ++shift)
    {
        // picture[f] against sound[f + shift], over the rows where either shows something
        double total = 0.0;
        auto count = 0;

        for (int f = maxShift; f < frames - maxShift; ++f)
        {
            for (int y = 0; y < rows; ++y)
            {
                const auto p = picture[static_cast<size_t> (f)][static_cast<size_t> (y)];
                const auto q = sound[static_cast<size_t> (f + shift)][static_cast<size_t> (y)];

                if (p > 8 || q > 8)
                {
                    total += std::abs (p - q);
                    ++count;
                }
            }
        }

        errors.push_back (count > 0 ? total / count : 255.0);
        table << "  " << (shift > 0 ? "+" : "") << shift << ": " << juce::String (errors.back(), 1);
    }

    const auto atZero = errors[static_cast<size_t> (maxShift)];
    const auto best = std::min_element (errors.begin(), errors.end()) - errors.begin() - maxShift;
    std::cout << "  sync: the live view's newest column against the sound's own analysis, mean difference in colour levels"
              << " (of 255) with the picture shifted by n frames:" << table << std::endl;
    check (best == 0 && atZero < 8.0 && errors[static_cast<size_t> (maxShift - 1)] > 2.0 * atZero && errors[static_cast<size_t> (maxShift + 1)] > 2.0 * atZero,
           clip.name + ": the live view shows the sound in the frame it sounds, no earlier or later");
    return atZero;
}

/** Every column the live view shows in the last frame, against the analysis made above: the
    view lays its columns out from the right edge ("now"), each `pixelsPerColumn` wide, newest
    first; with none dropped, none twice and no silence put in for a stalled host, every pixel
    column shows the column its place stands for. Compared again with the history moved by one
    column either way. Returns the mean difference in colour-map levels (of 255). */
double checkWholePicture (const ClipSpec& clip, const juce::Image& frame, juce::Rectangle<int> plot, double spanSeconds,
                          const Stereo& output, int liveFrame, int lastFrame, const FrequencyScale& scale)
{
    const auto left = juce::roundToInt (static_cast<float> (plot.getX()) * pixelScale);
    const auto top = juce::roundToInt (static_cast<float> (plot.getY()) * pixelScale);
    const auto width = juce::roundToInt (static_cast<float> (plot.getWidth()) * pixelScale);
    const auto rows = juce::roundToInt (static_cast<float> (plot.getHeight()) * pixelScale);
    const auto pixelsPerColumn = static_cast<float> (width / (spanSeconds * sampleRate / SoundAnalysis::hop));

    // The live view's badge (top right) lies over the picture: left out.
    const auto badge = juce::Rectangle<int> (width - juce::roundToInt (120 * pixelScale), 0, juce::roundToInt (120 * pixelScale),
                                             juce::roundToInt (40 * pixelScale));

    SoundAnalysis analysis (output, static_cast<int64_t> (liveFrame) * samplesPerFrame, rows, scale);
    const auto newest = analysis.columnsBy (static_cast<int64_t> (lastFrame + 1) * samplesPerFrame) - 1;
    const juce::Image::BitmapData data (frame, juce::Image::BitmapData::readOnly);
    std::vector<std::vector<int>> picture (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
        for (int y = 0; y < rows; ++y)
            picture[static_cast<size_t> (x)].push_back (levelOfPixel (data.getPixelColour (left + x, top + y)));

    std::map<int64_t, std::vector<int>> columns;
    const auto columnAt = [&] (int64_t n) -> const std::vector<int>&
    {
        if (n < 0)
            n = -1;   // before the view was turned on: silence

        auto found = columns.find (n);

        if (found == columns.end())
            found = columns.emplace (n, n >= 0 ? analysis.column (n) : std::vector<int> (static_cast<size_t> (rows), 0)).first;

        return found->second;
    };

    juce::String table;
    std::vector<double> errors;

    for (int shift = -1; shift <= 1; ++shift)
    {
        double total = 0.0;
        auto count = 0;

        for (int age = 0;; ++age)
        {
            const auto right = juce::roundToInt (static_cast<float> (width) - static_cast<float> (age) * pixelsPerColumn);
            const auto from = juce::roundToInt (static_cast<float> (width) - static_cast<float> (age + 1) * pixelsPerColumn);

            if (right <= 0)
                break;

            const auto& expected = columnAt (newest - age + shift);

            for (int x = std::max (0, from); x < right; ++x)
                for (int y = 0; y < rows; ++y)
                {
                    if (badge.contains (x, y))
                        continue;

                    const auto p = picture[static_cast<size_t> (x)][static_cast<size_t> (y)];
                    const auto q = expected[static_cast<size_t> (y)];

                    if (p > 8 || q > 8)
                    {
                        total += std::abs (p - q);
                        ++count;
                    }
                }
        }

        errors.push_back (count > 0 ? total / count : 255.0);
        table << "  " << (shift > 0 ? "+" : "") << shift << ": " << juce::String (errors.back(), 1);
    }

    std::cout << "  the whole live picture of the last frame (" << newest + 1 << " columns analysed) against the sound's own analysis,"
              << " mean difference in colour levels with the history moved by n columns:" << table << std::endl;
    check (errors[1] < 3.0 && errors[0] > 3.0 * errors[1] && errors[2] > 3.0 * errors[1],
           clip.name + ": every column the live view shows is in its place (none dropped, none twice)");
    return errors[1];
}

/** Films one clip and encodes it. */
bool makeClip (const ClipSpec& clip, const juce::File& outputDir, const juce::File& workDir, const juce::String& ffmpeg,
               const juce::String& ffprobe)
{
    std::cout << "== " << clip.name << std::endl;

    const auto playFrameIndex = juce::roundToInt (clip.canvasSeconds * framesPerSecond);
    const auto totalFrames = playFrameIndex + juce::roundToInt (clip.playSeconds * framesPerSecond);
    const auto song = clip.effect ? makeSong (clip.playSeconds + 1.0) : Stereo {};

    // ---- runs without the editor: when does the output first sound? ------------------------------
    // The window switches to the live view at the start of the frame in which the output first
    // sounds. The DAW starts in the clip's first second, at one of the frame's four blocks: the
    // one that brings that first sound to the start of a frame, so the live view's first frame
    // already shows it (a sound starting at a frame's end would be in no column yet, and the
    // view would say "Nothing is playing" for a frame). Nothing that sounds is missed either way.
    Stereo reference;
    auto liveFrame = -1;
    int64_t playSample = 0;
    auto firstFrameDb = -200.0f;

    for (int shift = 0; shift < blocksPerFrame; ++shift)
    {
        const auto candidatePlay = static_cast<int64_t> (playFrameIndex) * samplesPerFrame + shift * blockSize;
        Stereo audio;
        auto processor = makeProcessor (clip);
        check (processor->isLicensed(), clip.name + ": the plug-in is activated (test licensing)");
        Transport transport;
        processor->setPlayHead (&transport);

        for (int frame = 0; frame < totalFrames; ++frame)
            playFrame (*processor, transport, frame, candidatePlay, song, audio);

        processor->setPlayHead (nullptr);
        auto onsetFrame = -1;

        for (size_t i = static_cast<size_t> (candidatePlay); i < audio[0].size() && onsetFrame < 0; ++i)
            if (std::abs (audio[0][i]) > 1.0e-4f || std::abs (audio[1][i]) > 1.0e-4f)
                onsetFrame = static_cast<int> (i / samplesPerFrame);

        if (onsetFrame < 0)
            continue;

        // What the live view's first frame shows: the analyser, turned on at the frame's start,
        // fed what the processor pushes (the channels' mean), its loudest reading in 20 Hz - 20 kHz.
        LiveAnalyzer analyzer;
        analyzer.prepare (sampleRate);
        analyzer.setActive (true);
        std::vector<float> mono (static_cast<size_t> (samplesPerFrame));

        for (size_t i = 0; i < mono.size(); ++i)
        {
            const auto index = static_cast<size_t> (onsetFrame) * samplesPerFrame + i;
            mono[i] = 0.5f * (audio[0][index] + audio[1][index]);
        }

        analyzer.push (mono.data(), samplesPerFrame);
        auto loudest = -200.0f;
        const auto binHz = sampleRate / analyzer.getFftSize();

        analyzer.process ([&] (const float* db, int numBins)
        {
            for (int bin = static_cast<int> (20.0 / binHz); bin < numBins && bin * binHz <= 20000.0; ++bin)
                loudest = std::max (loudest, db[bin]);
        });

        if (loudest > firstFrameDb)
        {
            firstFrameDb = loudest;
            liveFrame = onsetFrame;
            playSample = candidatePlay;
            reference = std::move (audio);
        }
    }

    if (liveFrame < 0)
    {
        check (false, clip.name + ": the output never sounds");
        return false;
    }

    const auto missed = peakOf (reference, 0, static_cast<size_t> (liveFrame) * samplesPerFrame);
    std::cout << "  frames " << totalFrames << " (" << totalFrames / static_cast<double> (framesPerSecond) << " s), play at sample "
              << playSample << " (frame " << playFrameIndex << " + " << playSample - playFrameIndex * samplesPerFrame
              << "), live view from frame " << liveFrame << ", whose analysis peaks at " << firstFrameDb
              << " dB; the output before it peaks at " << juce::Decibels::gainToDecibels (missed, -200.0f) << " dBFS" << std::endl;
    check (missed < 1.0e-5f, clip.name + ": nothing sounds before the live view is shown");
    check (firstFrameDb > viewFloorDb + 6.0f, clip.name + ": the live view's first frame shows the sound (no \"Nothing is playing\")");

    // ---- the filmed run ---------------------------------------------------------------------------
    auto processor = makeProcessor (clip);
    Transport transport;
    processor->setPlayHead (&transport);

    const std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor->createEditor()));

    if (editor == nullptr)
    {
        check (false, "the processor makes an ArtFreqEditor");
        return false;
    }

    // On the (virtual) screen, away from the mouse: the live view runs only while it is showing.
    editor->setTopLeftPosition (0, 0);
    editor->addToDesktop (0);
    editor->setVisible (true);
    juce::Desktop::setMousePosition ({ 4000, 4000 });
    pump (300);
    processor->rebuildPlanIfNeeded();
    pump (100);

    check (editor->isShowing(), clip.name + ": the window is on screen");
    check (! editor->getScreenBounds().contains (juce::Desktop::getMousePosition()), clip.name + ": the mouse is not over the window");

    if (auto* activation = findComponentWithId (*editor, "activation"))
        check (! activation->isShowing(), clip.name + ": no licence screen");

    auto* liveView = dynamic_cast<ui::LiveView*> (findComponentWithId (*editor, "live-view"));

    if (liveView == nullptr)
    {
        check (false, "the editor has its live view");
        return false;
    }

    // The live view's own timer callback (a private base: hence the C-style cast), which is what
    // takes the analysed columns in a host.
    auto* liveTimer = (juce::Timer*) liveView;

    const auto width = juce::roundToInt (static_cast<float> (editor->getWidth()) * pixelScale);
    const auto height = juce::roundToInt (static_cast<float> (editor->getHeight()) * pixelScale);
    const auto framesDir = workDir.getChildFile ("frames-" + clip.name);
    framesDir.deleteRecursively();
    framesDir.createDirectory();

    const auto encode = quoted (ffmpeg) + " -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgr0 -s "
                      + juce::String (width) + "x" + juce::String (height) + " -framerate " + juce::String (framesPerSecond)
                      + " -i - -vf scale=" + juce::String (videoWidth) + ":-2:flags=lanczos+accurate_rnd+full_chroma_int"
                      + " -pix_fmt rgb24 " + quoted (framesDir.getChildFile ("frame-%04d.png").getFullPathName());
    std::cout << "  $ " << encode << std::endl;
    auto* pipe = popen (encode.toRawUTF8(), "w");

    if (pipe == nullptr)
    {
        check (false, "start ffmpeg");
        return false;
    }

    Stereo output;
    std::vector<uint8_t> row (static_cast<size_t> (width) * 4);
    auto longestFrameMs = 0.0;
    std::vector<std::vector<int>> newestColumns;   // the live view's right edge, per frame, as levels
    juce::Image lastImage;
    juce::Rectangle<int> plot;                       // the live view's picture, in the window

    for (int frame = 0; frame < totalFrames; ++frame)
    {
        if (frame == liveFrame)
        {
            editor->showLiveView (true);
            check (processor->getAnalyzer().isActive(), clip.name + ": the live view turns the analyser on");
            plot = ui::axes::plotArea (liveView->getLocalBounds(), ui::CanvasView::axisWidth, ui::CanvasView::axisHeight)
                 + editor->getLocalArea (liveView, liveView->getLocalBounds()).getPosition();
        }

        const auto started = juce::Time::getMillisecondCounterHiRes();
        playFrame (*processor, transport, frame, playSample, song, output);
        const auto taken = juce::Time::getMillisecondCounterHiRes();

        if (frame >= liveFrame)
        {
            liveTimer->timerCallback();

            // Everything the processor pushed was taken (the analyser has nothing left over).
            check (processor->getAnalyzer().process ({}) == 0, clip.name + ": frame " + juce::String (frame) + " left analysis behind");
        }

        // The rest of the window follows the processor on its own timers (30 Hz): the bars, the
        // canvas's playhead. The live view's timer may run here too and finds nothing new; it
        // would scroll in silence only after 0.3 s without new columns (a host that has stopped).
        pump (45);
        check (juce::Time::getMillisecondCounterHiRes() - taken < 290.0,
               clip.name + ": frame " + juce::String (frame) + ": the window's timers ran within 0.29 s of the live view's last columns");

        // The live view redraws its picture from the analysed columns, at the snapshot's scale
        // (the window on the virtual screen draws it at 1x in between).
        liveView->resized();

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, pixelScale);
        lastImage = image;
        check (image.getWidth() == width && image.getHeight() == height, "frame size");
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

        if (frame >= liveFrame)
        {
            // The newest pixels of the live view ("now"): the last pixel column of its picture.
            const auto x = juce::roundToInt (static_cast<float> (plot.getRight()) * pixelScale) - 1;
            std::vector<int> column;

            for (auto y = juce::roundToInt (static_cast<float> (plot.getY()) * pixelScale); y < juce::roundToInt (static_cast<float> (plot.getBottom()) * pixelScale); ++y)
                column.push_back (levelOfPixel (data.getPixelColour (x, y)));

            newestColumns.push_back (std::move (column));
        }

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                // Opaque (the window covers itself), so premultiplied is the colour itself.
                const auto pixel = data.getPixelColour (x, y);
                auto* out = row.data() + static_cast<size_t> (x) * 4;
                out[0] = pixel.getBlue();
                out[1] = pixel.getGreen();
                out[2] = pixel.getRed();
                out[3] = 255;
            }

            if (std::fwrite (row.data(), 1, row.size(), pipe) != row.size())
            {
                check (false, clip.name + ": ffmpeg took the frames");
                break;
            }
        }

        longestFrameMs = std::max (longestFrameMs, juce::Time::getMillisecondCounterHiRes() - started);
    }

    const auto status = pclose (pipe);
    check (status == 0, clip.name + ": ffmpeg wrote the frames");
    processor->setPlayHead (nullptr);
    editor->removeFromDesktop();
    pump (50);

    std::cout << "  longest frame " << juce::roundToInt (longestFrameMs) << " ms" << std::endl;

    // ---- the picture against the sound -----------------------------------------------------------
    checkSync (clip, newestColumns, output, liveFrame, processor->getScale());
    checkWholePicture (clip, lastImage, plot, liveView->getVisibleSeconds(), output, liveFrame, totalFrames - 1, processor->getScale());

    // ---- the sound ---------------------------------------------------------------------------------
    auto largestDifference = 0.0f;

    for (size_t channel = 0; channel < 2; ++channel)
        for (size_t i = 0; i < output[channel].size(); ++i)
            largestDifference = std::max (largestDifference, std::abs (output[channel][i] - reference[channel][i]));

    check (output[0].size() == static_cast<size_t> (totalFrames) * samplesPerFrame, clip.name + ": a frame's audio for every frame");
    check (largestDifference <= 0.0f, clip.name + ": the filmed run sounds exactly like the run without the editor (largest difference "
                                          + juce::String (largestDifference) + ")");

    const auto peak = peakOf (output);
    std::cout << "  output peak " << juce::Decibels::gainToDecibels (peak) << " dBFS" << std::endl;
    check (peak < 1.0f, clip.name + ": the output does not clip");

    const auto wav = workDir.getChildFile (clip.name + ".wav");
    check (writeWav (output, wav), "write " + wav.getFullPathName());

    if (clip.effect)
    {
        // The song as it went in, on the clip's timeline (silence until play).
        Stereo input;

        for (size_t channel = 0; channel < 2; ++channel)
        {
            input[channel].assign (output[channel].size(), 0.0f);

            for (size_t i = static_cast<size_t> (playSample); i < input[channel].size(); ++i)
                if (i - static_cast<size_t> (playSample) < song[channel].size())
                    input[channel][i] = song[channel][i - static_cast<size_t> (playSample)];
        }

        check (writeWav (input, workDir.getChildFile (clip.name + "-input.wav")), "write the input");
    }

    // ---- encoding ------------------------------------------------------------------------------------
    const auto frames = quoted (framesDir.getChildFile ("frame-%04d.png").getFullPathName());
    const auto mp4 = outputDir.getChildFile (clip.name + ".mp4");
    const auto gif = outputDir.getChildFile (clip.name + ".gif");
    const auto poster = outputDir.getChildFile (clip.name + "-poster.png");

    // H.264 (High, yuv420p, BT.709 and tagged so), the sound as AAC, moov first (plays as it loads).
    // The sound fades out over the clip's last quarter second: the song in ArtFreq FX is still
    // playing when the clip ends, and a cut at full level clicks (and clicks again on every loop).
    const auto clipSeconds = totalFrames / static_cast<double> (framesPerSecond);
    check (run (quoted (ffmpeg) + " -hide_banner -loglevel error -y -framerate " + juce::String (framesPerSecond) + " -i " + frames
                + " -i " + quoted (wav.getFullPathName())
                + " -map 0:v -map 1:a -c:v libx264 -preset slower -crf 18 -profile:v high -tune animation"
                + " -af afade=t=out:st=" + juce::String (clipSeconds - soundFadeSeconds, 3) + ":d=" + juce::String (soundFadeSeconds, 3)
                + " -vf scale=out_color_matrix=bt709:out_range=tv,format=yuv420p"
                + " -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv"
                + " -c:a aac -b:a 192k -ar 48000 -movflags +faststart -shortest " + quoted (mp4.getFullPathName())) == 0,
           "encode " + mp4.getFullPathName());

    // A silent GIF that loops: every other frame, one palette for the whole clip, and the last frame
    // (the whole drawing in view) held for a moment before it starts again.
    check (run (quoted (ffmpeg) + " -hide_banner -loglevel error -y -framerate " + juce::String (framesPerSecond) + " -i " + frames
                + " -vf \"fps=" + juce::String (gifFramesPerSecond) + ",scale=" + juce::String (gifWidth)
                + ":-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=full[p];"
                + "[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle\" -loop 0 -final_delay "
                + juce::String (gifHoldCentiseconds) + " " + quoted (gif.getFullPathName())) == 0,
           "encode " + gif.getFullPathName());

    // The poster: the last frame (the whole drawing in view).
    const auto last = framesDir.getChildFile (juce::String::formatted ("frame-%04d.png", totalFrames));
    check (last.existsAsFile() && last.copyFileTo (poster), "the poster " + poster.getFullPathName());

    checkFiles (clip, ffprobe, mp4, gif, totalFrames);

    return ! failed;
}

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_LINUX || JUCE_BSD
    // As a standalone app JUCE installs X error handlers that tolerate a virtual display without
    // a window manager (Xlib's own would end the process). No application object is created.
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return nullptr; };
   #endif

    const juce::ScopedJuceInitialiser_GUI gui;

    // If ffmpeg stops reading the frames, writing them fails (and is reported) rather than ending
    // the tool.
    std::signal (SIGPIPE, SIG_IGN);

    if (argc < 2)
    {
        std::cerr << "usage: ArtFreqDemoReel <output-dir> [--work <dir>] [--clips artfreq,fx,fx-carve-draw] [--ffmpeg <path>]" << std::endl;
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto outputDir = cwd.getChildFile (argv[1]);
    auto workDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ArtFreqDemoReel");
    juce::StringArray wanted { "artfreq", "fx", "fx-carve-draw" };
    juce::String ffmpeg = "ffmpeg";

    for (int i = 2; i + 1 < argc; i += 2)
    {
        const juce::String option (argv[i]), value (argv[i + 1]);

        if (option == "--work")
            workDir = cwd.getChildFile (value);
        else if (option == "--clips")
            wanted = juce::StringArray::fromTokens (value, ",", {});
        else if (option == "--ffmpeg")
            ffmpeg = value;
        else
        {
            std::cerr << "unknown option " << option << std::endl;
            return 2;
        }
    }

    // ffprobe beside the ffmpeg given (or on the PATH, as ffmpeg).
    const auto ffprobe = ffmpeg.containsChar ('/') ? juce::File (ffmpeg).getSiblingFile ("ffprobe").getFullPathName() : juce::String ("ffprobe");

    if (! outputDir.createDirectory() || ! workDir.createDirectory())
    {
        std::cerr << "Cannot create " << outputDir.getFullPathName() << " or " << workDir.getFullPathName() << std::endl;
        return 1;
    }

    // Every instance is activated, against a fake server in a folder of the tool's own.
    licensetest::installRunnerLicensing();

    std::vector<ClipSpec> clips;

    if (wanted.contains ("artfreq"))
        clips.push_back ({ "artfreq-demo", false, EffectMode::carve, 1.0, 4.5 });

    if (wanted.contains ("fx"))
        clips.push_back ({ "artfreq-fx-demo", true, EffectMode::carve, 1.0, 4.8 });

    if (wanted.contains ("fx-carve-draw"))
        clips.push_back ({ "artfreq-fx-carve-draw-demo", true, EffectMode::carveDraw, 1.0, 4.8 });

    if (clips.empty())
    {
        std::cerr << "no clips: --clips takes artfreq, fx and fx-carve-draw" << std::endl;
        return 2;
    }

    for (const auto& clip : clips)
        makeClip (clip, outputDir, workDir, ffmpeg, ffprobe);

    pump (50);
    licensetest::removeRunnerLicensing();

    std::cout << (failed ? "Some checks failed" : "All checks passed") << std::endl;
    return failed ? 1 : 0;
}
