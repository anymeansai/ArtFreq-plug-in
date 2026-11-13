// WAV export (one pass of the drawing rendered to a file) and the safety clipper it shares
// with the plug-in.

#include "TestFramework.h"
#include "TestPlans.h"

#include "dsp/WavExporter.h"

#include <juce_audio_formats/juce_audio_formats.h>

using freqart::ExportSettings;

namespace
{

/** A scratch folder for this run, removed with everything in it at the end. */
struct ScratchFolder
{
    ScratchFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("FreqArtExportTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
    {
        folder.createDirectory();
    }

    ~ScratchFolder() { folder.deleteRecursively(); }

    juce::File folder;
};

struct Loaded
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
    int bitsPerSample = 0;
};

Loaded load (const juce::File& file)
{
    Loaded result;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));

    if (reader == nullptr)
        return result;

    result.sampleRate = reader->sampleRate;
    result.bitsPerSample = static_cast<int> (reader->bitsPerSample);
    result.audio.setSize (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
    reader->read (&result.audio, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);
    return result;
}

std::vector<float> channel (const juce::AudioBuffer<float>& audio, int index)
{
    return { audio.getReadPointer (index), audio.getReadPointer (index) + audio.getNumSamples() };
}

} // namespace

FREQART_TEST (exportWritesOnePass)
{
    ScratchFolder scratch;
    const auto target = scratch.folder.getChildFile ("nested").getChildFile ("FreqArt.wav");

    // Loud in the first half of the drawing, quiet in the second.
    const auto plan = test::makePlan ({ 300.0f, 1200.0f, 5000.0f }, [] (int column, int) { return column < 512 ? 0.2f : 0.05f; });

    ExportSettings settings;
    settings.sampleRate = 48000.0;
    settings.seconds = 1.0;
    settings.gain = 0.5f;

    juce::String error;
    CHECK (freqart::renderPlanToWav (*plan, settings, target, error));
    CHECK (error.isEmpty());
    CHECK (target.existsAsFile());

    const auto loaded = load (target);
    CHECK (loaded.audio.getNumChannels() == 2);
    CHECK (loaded.audio.getNumSamples() == 48000);
    CHECK_NEAR (loaded.sampleRate, 48000.0, 0.0);
    CHECK (loaded.bitsPerSample == 24);

    const auto left = channel (loaded.audio, 0);
    const auto right = channel (loaded.audio, 1);
    CHECK (left == right);
    CHECK (test::peak (left) <= 1.0);
    CHECK_GREATER (test::rms (left), 0.01);

    // Fades in from silence and out to silence.
    CHECK (std::abs (left.front()) < 1.0e-6f);
    CHECK_LESS (std::abs (left.back()), 1.0e-3);
    CHECK_LESS (test::peak (left, 47990), 0.01);

    // The drawing runs from start to end over the length: loud first half, quiet second.
    const auto firstHalf = test::rms (left, 2400, 21600);
    const auto secondHalf = test::rms (left, 26400, 45600);
    CHECK_NEAR (firstHalf / secondHalf, 4.0, 0.2);
    CHECK_NEAR (firstHalf, 0.5 * std::sqrt (3.0 * 0.2 * 0.2 / 2.0), 0.005);

    // Overwrites an existing file; other formats.
    settings.numChannels = 1;
    settings.bitDepth = 16;
    settings.sampleRate = 44100.0;
    settings.seconds = 0.5;
    CHECK (freqart::renderPlanToWav (*plan, settings, target, error));
    const auto again = load (target);
    CHECK (again.audio.getNumChannels() == 1);
    CHECK (again.audio.getNumSamples() == 22050);
    CHECK (again.bitsPerSample == 16);
    CHECK_NEAR (again.sampleRate, 44100.0, 0.0);
    CHECK (scratch.folder.getChildFile ("nested").getNumberOfChildFiles (juce::File::findFiles) == 1);
}

FREQART_TEST (exportClipsLoudPlans)
{
    ScratchFolder scratch;
    const auto target = scratch.folder.getChildFile ("Loud.wav");
    const auto plan = test::makeConstantPlan (test::logLines (64, 100.0, 10000.0), 0.2f);

    ExportSettings settings;
    settings.seconds = 0.5;
    settings.gain = 8.0f;
    settings.bitDepth = 32;

    juce::String error;
    CHECK (freqart::renderPlanToWav (*plan, settings, target, error));

    const auto left = channel (load (target).audio, 0);
    CHECK (left.size() == 24000);
    CHECK_LESS (test::peak (left), 1.0);
    CHECK_GREATER (test::peak (left), 0.95);
}

FREQART_TEST (exportReportsErrors)
{
    ScratchFolder scratch;
    const auto plan = test::makeConstantPlan ({ 1000.0f }, 0.1f);
    juce::String error;

    // The "folder" it should go in is a file.
    const auto blocker = scratch.folder.getChildFile ("notAFolder");
    blocker.replaceWithText ("x");
    CHECK (! freqart::renderPlanToWav (*plan, {}, blocker.getChildFile ("FreqArt.wav"), error));
    CHECK (error.isNotEmpty());

    ExportSettings bad;
    bad.seconds = 0.0;
    error.clear();
    CHECK (! freqart::renderPlanToWav (*plan, bad, scratch.folder.getChildFile ("a.wav"), error));
    CHECK (error.isNotEmpty());

    bad = {};
    bad.bitDepth = 12;
    error.clear();
    CHECK (! freqart::renderPlanToWav (*plan, bad, scratch.folder.getChildFile ("b.wav"), error));
    CHECK (error.isNotEmpty());
    CHECK (! scratch.folder.getChildFile ("b.wav").exists());
}

FREQART_TEST (exportSoftClipShape)
{
    // Transparent up to 0.89.
    auto transparent = true;

    for (int i = -890; i <= 890; ++i)
    {
        const auto x = static_cast<float> (i) / 1000.0f;
        transparent = transparent && std::abs (freqart::softClip (x) - x) <= 0.0f;
    }

    CHECK (transparent);

    // Above it: below 1 in magnitude always, rising, odd, and without a corner.
    auto bounded = true, rising = true, odd = true;
    auto previous = freqart::softClip (0.89f);

    for (int i = 1; i < 200000; ++i)
    {
        const auto x = 0.89f + static_cast<float> (i) * 0.0001f;
        const auto y = freqart::softClip (x);
        bounded = bounded && std::abs (y) < 1.0f;
        rising = rising && y >= previous;
        odd = odd && std::abs (freqart::softClip (-x) + y) <= 0.0f;
        previous = y;
    }

    CHECK (bounded);
    CHECK (rising);
    CHECK (odd);

    for (const auto huge : { 1.0e3f, 1.0e9f, 3.0e38f, std::numeric_limits<float>::infinity() })
    {
        CHECK (std::abs (freqart::softClip (huge)) < 1.0f);
        CHECK (std::abs (freqart::softClip (-huge)) < 1.0f);
    }

    CHECK (std::abs (freqart::softClip (std::numeric_limits<float>::quiet_NaN())) <= 0.0f);

    // Slope 1 on both sides of the knee (C1), falling towards 0 far above it.
    const auto h = 1.0e-3;
    const auto below = (freqart::softClip (0.89f) - freqart::softClip (static_cast<float> (0.89 - h))) / h;
    const auto above = (freqart::softClip (static_cast<float> (0.89 + h)) - freqart::softClip (0.89f)) / h;
    CHECK_NEAR (below, 1.0, 0.01);
    CHECK_NEAR (above, 1.0, 0.02);
    CHECK_LESS ((freqart::softClip (1.5f) - freqart::softClip (1.49f)) / 0.01, 0.01);
}
