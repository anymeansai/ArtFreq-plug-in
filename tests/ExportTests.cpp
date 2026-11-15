// WAV export (one pass of the drawing rendered to a file) and the safety clipper it shares
// with the plug-in.

#include "TestFramework.h"
#include "TestPlans.h"

#include "dsp/WavExporter.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>

using artfreq::ExportSettings;

namespace
{

/** A scratch folder for this run, removed with everything in it at the end. */
struct ScratchFolder
{
    ScratchFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("ArtFreqExportTests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
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

ARTFREQ_TEST (exportWritesOnePass)
{
    ScratchFolder scratch;
    const auto target = scratch.folder.getChildFile ("nested").getChildFile ("ArtFreq.wav");

    // Loud in the first half of the drawing, quiet in the second.
    const auto plan = test::makePlan ({ 300.0f, 1200.0f, 5000.0f }, [] (int column, int) { return column < 512 ? 0.2f : 0.05f; });

    ExportSettings settings;
    settings.sampleRate = 48000.0;
    settings.seconds = 1.0;
    settings.gain = 0.5f;

    juce::String error;
    CHECK (artfreq::renderPlanToWav (*plan, settings, target, error));
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
    CHECK (artfreq::renderPlanToWav (*plan, settings, target, error));
    const auto again = load (target);
    CHECK (again.audio.getNumChannels() == 1);
    CHECK (again.audio.getNumSamples() == 22050);
    CHECK (again.bitsPerSample == 16);
    CHECK_NEAR (again.sampleRate, 44100.0, 0.0);
    CHECK (scratch.folder.getChildFile ("nested").getNumberOfChildFiles (juce::File::findFiles) == 1);
}

ARTFREQ_TEST (exportClipsLoudPlans)
{
    ScratchFolder scratch;
    const auto target = scratch.folder.getChildFile ("Loud.wav");
    const auto plan = test::makeConstantPlan (test::logLines (64, 100.0, 10000.0), 0.2f);

    ExportSettings settings;
    settings.seconds = 0.5;
    settings.gain = 8.0f;
    settings.bitDepth = 32;

    juce::String error;
    CHECK (artfreq::renderPlanToWav (*plan, settings, target, error));

    const auto left = channel (load (target).audio, 0);
    CHECK (left.size() == 24000);
    CHECK_LESS (test::peak (left), 1.0);
    CHECK_GREATER (test::peak (left), 0.95);
}

ARTFREQ_TEST (exportReportsErrors)
{
    ScratchFolder scratch;
    const auto plan = test::makeConstantPlan ({ 1000.0f }, 0.1f);
    juce::String error;

    // The "folder" it should go in is a file.
    const auto blocker = scratch.folder.getChildFile ("notAFolder");
    blocker.replaceWithText ("x");
    CHECK (! artfreq::renderPlanToWav (*plan, {}, blocker.getChildFile ("ArtFreq.wav"), error));
    CHECK (error.isNotEmpty());

    ExportSettings bad;
    bad.seconds = 0.0;
    error.clear();
    CHECK (! artfreq::renderPlanToWav (*plan, bad, scratch.folder.getChildFile ("a.wav"), error));
    CHECK (error.isNotEmpty());

    bad = {};
    bad.bitDepth = 12;
    error.clear();
    CHECK (! artfreq::renderPlanToWav (*plan, bad, scratch.folder.getChildFile ("b.wav"), error));
    CHECK (error.isNotEmpty());
    CHECK (! scratch.folder.getChildFile ("b.wav").exists());
}

ARTFREQ_TEST (exportCanBeCancelled)
{
    // Closing the editor stops a long bounce: the render asks once per block, stops, and leaves
    // the target as it was (no half file, no temporary file).
    ScratchFolder scratch;
    const auto plan = test::makeConstantPlan ({ 1000.0f }, 0.1f);
    const auto target = scratch.folder.getChildFile ("Long.wav");
    target.replaceWithText ("an earlier file");

    ExportSettings settings;
    settings.seconds = 600.0;

    auto asked = 0;
    juce::String error;
    CHECK (! artfreq::renderPlanToWav (*plan, settings, target, error, [&asked] { return ++asked > 3; }));
    CHECK (error == "Cancelled");
    CHECK (asked == 4);
    CHECK (target.loadFileAsString() == "an earlier file");
    CHECK (scratch.folder.getNumberOfChildFiles (juce::File::findFiles) == 1);

    // Never cancelled: as without it. Progress is told after every block, rising to 1.
    settings.seconds = 0.25;
    error.clear();
    std::vector<double> progress;
    CHECK (artfreq::renderPlanToWav (*plan, settings, target, error, [] { return false; },
                                     [&progress] (double fraction) { progress.push_back (fraction); }));
    CHECK (error.isEmpty() && load (target).audio.getNumSamples() == 12000);
    CHECK (progress.size() == 3);   // 12000 samples in blocks of 4096
    CHECK (std::is_sorted (progress.begin(), progress.end()) && ! progress.empty() && progress.front() > 0.0);
    CHECK (! progress.empty() && std::abs (progress.back() - 1.0) < 1.0e-12);
}

ARTFREQ_TEST (exportFileNamesAreReadableAndSafe)
{
    using artfreq::fileNamePart;

    // What file systems, shells and DAWs choke on separates words; runs of spaces collapse.
    CHECK (fileNamePart ("HELLO", 20) == "HELLO");
    CHECK (fileNamePart ("  a/b\\c:d*e?f\"g<h>i|j  ", 40) == "a b c d e f g h i j");
    CHECK (fileNamePart ("two\nlines\tand tabs", 40) == "two lines and tabs");
    CHECK (fileNamePart ("v1.2 ../etc", 40) == "v1 2 etc");
    CHECK (fileNamePart ("Rock & Roll (live)!", 40) == "Rock & Roll (live)!");
    CHECK (fileNamePart (juce::String::fromUTF8 ("Caf\xc3\xa9 \xc3\x97 Na\xc3\xafve"), 40)
           == juce::String::fromUTF8 ("Caf\xc3\xa9 Na\xc3\xafve"));                 // accents kept, the times sign not
    CHECK (fileNamePart (juce::String::fromUTF8 ("Hi \xf0\x9f\x8e\xb5 there"), 40) == "Hi there");   // no emoji
    CHECK (fileNamePart ("...", 20).isEmpty());

    // Whole words up to the limit; one long word is cut.
    CHECK (fileNamePart ("The quick brown fox jumps over", 20) == "The quick brown fox");
    CHECK (fileNamePart ("Supercalifragilisticexpialidocious", 20) == "Supercalifragilistic");
    CHECK (fileNamePart ("anything", 0).isEmpty());

    // The label: the first text that leaves something, else "Drawing".
    artfreq::ArtDocument document;
    CHECK (artfreq::bounceLabel (document) == "Drawing");
    document.objects.push_back (artfreq::ArtDocument::makeTextObject ("  ?? ", 3500.0f, 16000.0f));
    CHECK (artfreq::bounceLabel (document) == "Drawing");
    document.objects.push_back (artfreq::ArtDocument::makeTextObject ("Hello, World:\nline two", 300.0f, 3000.0f));
    document.objects.push_back (artfreq::ArtDocument::makeTextObject ("Second", 40.0f, 250.0f));
    CHECK (artfreq::bounceLabel (document) == "Hello World line two");

    // Reserved at once, so even in the same second every bounce has its own name.
    ScratchFolder scratch;
    const auto folder = scratch.folder.getChildFile ("ArtFreq");
    const auto when = juce::Time (2026, 10, 13, 14, 3, 27);   // 13 Nov 2026, 14:03:27 local
    const auto first = artfreq::reserveBounceFile (folder, "HELLO", when);
    const auto second = artfreq::reserveBounceFile (folder, "HELLO", when);
    const auto third = artfreq::reserveBounceFile (folder, "HELLO", when);
    CHECK (first.getFileName() == "ArtFreq HELLO 14-03-27.wav");
    CHECK (second.getFileName() == "ArtFreq HELLO 14-03-27 2.wav");
    CHECK (third.getFileName() == "ArtFreq HELLO 14-03-27 3.wav");
    CHECK (first.existsAsFile() && second.existsAsFile() && third.existsAsFile() && first.getSize() == 0);
    CHECK (artfreq::reserveBounceFile (folder, "Drawing", when).getFileName() == "ArtFreq Drawing 14-03-27.wav");
    CHECK (artfreq::reserveBounceFile (folder, "ArtFreq", when).getFileName() == "ArtFreq 14-03-27.wav");
    CHECK (artfreq::reserveBounceFile (folder, "ArtFreq Rocks", when).getFileName() == "ArtFreq Rocks 14-03-27.wav");

    // A name made from any text is a plain file name in that folder.
    const auto awkward = artfreq::reserveBounceFile (folder, fileNamePart ("..\\..//<x>|:*?\"", 20), when);
    CHECK (awkward.getParentDirectory() == folder && awkward.getFileName() == juce::File::createLegalFileName (awkward.getFileName()));

    // A folder that cannot be made: no file.
    const auto blocker = scratch.folder.getChildFile ("notAFolder");
    blocker.replaceWithText ("x");
    CHECK (artfreq::reserveBounceFile (blocker, "HELLO", when) == juce::File());
}

ARTFREQ_TEST (exportSoftClipShape)
{
    // Transparent up to 0.89.
    auto transparent = true;

    for (int i = -890; i <= 890; ++i)
    {
        const auto x = static_cast<float> (i) / 1000.0f;
        transparent = transparent && std::abs (artfreq::softClip (x) - x) <= 0.0f;
    }

    CHECK (transparent);

    // Above it: below 1 in magnitude always, rising, odd, and without a corner.
    auto bounded = true, rising = true, odd = true;
    auto previous = artfreq::softClip (0.89f);

    for (int i = 1; i < 200000; ++i)
    {
        const auto x = 0.89f + static_cast<float> (i) * 0.0001f;
        const auto y = artfreq::softClip (x);
        bounded = bounded && std::abs (y) < 1.0f;
        rising = rising && y >= previous;
        odd = odd && std::abs (artfreq::softClip (-x) + y) <= 0.0f;
        previous = y;
    }

    CHECK (bounded);
    CHECK (rising);
    CHECK (odd);

    for (const auto huge : { 1.0e3f, 1.0e9f, 3.0e38f, std::numeric_limits<float>::infinity() })
    {
        CHECK (std::abs (artfreq::softClip (huge)) < 1.0f);
        CHECK (std::abs (artfreq::softClip (-huge)) < 1.0f);
    }

    CHECK (std::abs (artfreq::softClip (std::numeric_limits<float>::quiet_NaN())) <= 0.0f);

    // Slope 1 on both sides of the knee (C1), falling towards 0 far above it.
    const auto h = 1.0e-3;
    const auto below = (artfreq::softClip (0.89f) - artfreq::softClip (static_cast<float> (0.89 - h))) / h;
    const auto above = (artfreq::softClip (static_cast<float> (0.89 + h)) - artfreq::softClip (0.89f)) / h;
    CHECK_NEAR (below, 1.0, 0.01);
    CHECK_NEAR (above, 1.0, 0.02);
    CHECK_LESS ((artfreq::softClip (1.5f) - artfreq::softClip (1.49f)) / 0.01, 0.01);
}
