#include "WavExporter.h"

#include "ArtEngine.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace artfreq
{

namespace
{
    constexpr float clipThreshold = 0.89f;
    constexpr float clipHeadroom = 1.0f - clipThreshold;
    constexpr float largestBelowOne = 0.99999994f;   // the float just under 1
    constexpr int exportBlockSize = 4096;
    constexpr double longestExportSeconds = 60.0 * 60.0;
}

float softClip (float x) noexcept
{
    const auto magnitude = std::abs (x);

    if (magnitude <= clipThreshold)
        return x;

    if (! std::isfinite (x))
        return std::isnan (x) ? 0.0f : std::copysign (largestBelowOne, x);

    // tanh leaves the threshold with slope 1 (so the curve is smooth there) and never reaches
    // 1; the min keeps float rounding from getting there either.
    const auto shaped = clipThreshold + clipHeadroom * std::tanh ((magnitude - clipThreshold) / clipHeadroom);
    return std::copysign (std::min (shaped, largestBelowOne), x);
}

bool renderPlanToWav (const ArtPlan& plan, const ExportSettings& settings,
                      const juce::File& target, juce::String& error,
                      const std::function<bool()>& shouldCancel,
                      const std::function<void (double)>& onProgress)
{
    if (! (settings.sampleRate >= 8000.0 && settings.sampleRate <= 384000.0))
    {
        error = "Unsupported sample rate: " + juce::String (settings.sampleRate);
        return false;
    }

    if (! (settings.seconds > 0.0 && settings.seconds <= longestExportSeconds))
    {
        error = "Unsupported length: " + juce::String (settings.seconds) + " s";
        return false;
    }

    if (settings.numChannels < 1 || settings.numChannels > 8)
    {
        error = "Unsupported channel count: " + juce::String (settings.numChannels);
        return false;
    }

    if (settings.bitDepth != 16 && settings.bitDepth != 24 && settings.bitDepth != 32)
    {
        error = "Unsupported bit depth: " + juce::String (settings.bitDepth);
        return false;
    }

    if (! std::isfinite (settings.gain))
    {
        error = "Invalid gain";
        return false;
    }

    if (target.isDirectory())
    {
        error = "The target is a folder: " + target.getFullPathName();
        return false;
    }

    if (const auto made = target.getParentDirectory().createDirectory(); made.failed())
    {
        error = "Cannot create " + target.getParentDirectory().getFullPathName() + ": " + made.getErrorMessage();
        return false;
    }

    // Written next to the target and moved over it at the end, so a failed export never leaves
    // half a file where a good one was.
    juce::TemporaryFile temporary (target);
    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (temporary.getFile());
    auto* file = fileStream.get();   // owned by the writer from here on, which outlives every use

    if (file->failedToOpen())
    {
        error = "Cannot write " + temporary.getFile().getFullPathName() + ": " + file->getStatus().getErrorMessage();
        return false;
    }

    std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);

    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate (settings.sampleRate)
                             .withNumChannels (settings.numChannels)
                             .withBitsPerSample (settings.bitDepth)
                             .withSampleFormat (settings.bitDepth == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                        : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
    {
        error = "Cannot create a WAV writer for these settings";
        return false;
    }

    const auto totalSamples = static_cast<int64_t> (std::llround (settings.seconds * settings.sampleRate));
    const auto fadeSamples = static_cast<int64_t> (std::llround (ArtEngine::gateFadeSeconds * settings.sampleRate));
    const auto gateCloses = std::max (static_cast<int64_t> (0), totalSamples - fadeSamples);
    const auto perSample = 1.0 / (settings.seconds * settings.sampleRate);

    auto engine = std::make_unique<ArtEngine>();
    engine->prepare (settings.sampleRate, exportBlockSize);
    engine->setPlan (&plan);

    std::vector<float> mono (static_cast<size_t> (exportBlockSize));
    std::vector<const float*> channels (static_cast<size_t> (settings.numChannels), mono.data());

    for (int64_t done = 0; done < totalSamples;)
    {
        // A long render can be stopped (the editor closing): the temporary file goes with it.
        if (shouldCancel != nullptr && shouldCancel())
        {
            error = "Cancelled";
            return false;
        }

        const auto blockLength = static_cast<int> (std::min (static_cast<int64_t> (exportBlockSize), totalSamples - done));
        PlaySegment segments[2];
        int numSegments = 0;

        // Playing up to the moment the gate closes, so the 5 ms fade-out ends with the file.
        if (done < gateCloses)
        {
            auto& playing = segments[numSegments++];
            playing.start = 0;
            playing.length = static_cast<int> (std::min (static_cast<int64_t> (blockLength), gateCloses - done));
            playing.playing = true;
            playing.fraction = static_cast<double> (done) * perSample;
            playing.fractionPerSample = perSample;
            playing.jump = done == 0;
        }

        const auto playedHere = numSegments > 0 ? segments[0].length : 0;

        if (playedHere < blockLength)
        {
            auto& stopped = segments[numSegments++];
            stopped.start = playedHere;
            stopped.length = blockLength - playedHere;
            stopped.playing = false;
            stopped.fraction = static_cast<double> (done + playedHere) * perSample;
            stopped.fractionPerSample = perSample;
        }

        engine->render (mono.data(), blockLength, segments, numSegments);

        for (int i = 0; i < blockLength; ++i)
            mono[static_cast<size_t> (i)] = softClip (mono[static_cast<size_t> (i)] * settings.gain);

        if (! writer->writeFromFloatArrays (channels.data(), settings.numChannels, blockLength))
        {
            error = "Writing the WAV file failed (is the disk full?)";
            return false;
        }

        done += blockLength;

        if (onProgress != nullptr)
            onProgress (static_cast<double> (done) / static_cast<double> (totalSamples));
    }

    // Finish the header now, while the stream can still say whether everything got there.
    writer->flush();
    file->flush();

    if (file->getStatus().failed())
    {
        error = "Writing the WAV file failed: " + file->getStatus().getErrorMessage();
        return false;
    }

    writer.reset();   // finishes the header and closes the file

    if (! temporary.overwriteTargetFileWithTemporary())
    {
        error = "Cannot replace " + target.getFullPathName();
        return false;
    }

    return true;
}

//==============================================================================
juce::String fileNamePart (const juce::String& text, int maxLength)
{
    // Letters, digits and punctuation that means nothing to a file system, a shell or a DAW's
    // browser. Everything else (/ \ : * ? " < > |, dots, line breaks, emoji ...) separates words.
    const juce::String harmless ("-_()'&+!");
    juce::String spaced;

    for (auto c = text.getCharPointer(); ! c.isEmpty();)
    {
        const auto character = c.getAndAdvance();
        const auto keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                       || (character >= '0' && character <= '9') || harmless.containsChar (character)
                       || (character >= 0xc0 && character <= 0x24f && character != 0xd7 && character != 0xf7);
        spaced += keep ? character : static_cast<juce::juce_wchar> (' ');
    }

    juce::StringArray words;
    words.addTokens (spaced, " ", {});
    words.removeEmptyStrings();

    juce::String result;

    for (const auto& word : words)
    {
        const auto longer = result.isEmpty() ? word : result + " " + word;

        if (longer.length() > maxLength)
        {
            // Whole words where they fit; a single word longer than the limit is cut.
            if (result.isEmpty())
                result = word.substring (0, juce::jmax (0, maxLength));

            break;
        }

        result = longer;
    }

    return result;
}

juce::String bounceLabel (const ArtDocument& document)
{
    constexpr int longestLabel = 20;

    for (const auto& object : document.objects)
        if (object.type == ArtObject::Type::text)
            if (const auto label = fileNamePart (object.text, longestLabel); label.isNotEmpty())
                return label;

    return "Drawing";
}

juce::File reserveBounceFile (const juce::File& folder, const juce::String& label, juce::Time when)
{
    if (folder.createDirectory().failed())
        return {};

    const auto name = label.isEmpty() ? juce::String ("ArtFreq")
                    : label.startsWith ("ArtFreq") ? label
                    : "ArtFreq " + label;
    const auto stem = name + " " + when.formatted ("%H-%M-%S");

    for (int number = 1; number < 1000; ++number)
    {
        const auto file = folder.getChildFile ((number == 1 ? stem : stem + " " + juce::String (number)) + ".wav");

        // Created at once, so the name is taken before the render starts: every instance in a
        // host chooses names on the one message thread, so none can slip in between.
        if (! file.exists())
            return file.create().wasOk() ? file : juce::File();
    }

    return {};
}

} // namespace artfreq
