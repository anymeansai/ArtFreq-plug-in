// The host-facing processor: parameters, state, playback in each trigger mode, level safety
// and editing.

#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "ui/CanvasAxes.h"
#include "ui/Editor.h"

#include <chrono>
#include <cstdio>

using namespace freqart;
using namespace testutil;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

std::unique_ptr<FreqArtProcessor> makeProcessor()
{
    auto processor = std::make_unique<FreqArtProcessor>();
    processor->setPlayConfigDetails (0, 2, sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);
    return processor;
}

ArtDocument textDocument (const juce::String& text, Band band = Band::mid)
{
    ArtDocument document;
    const auto range = bandRange (band, FrequencyScale {});
    auto object = ArtDocument::makeTextObject (text, static_cast<float> (range.low), static_cast<float> (range.high));
    object.startTime = 0.0f;
    object.endTime = 1.0f;
    document.objects.push_back (object);
    return document;
}

/** The largest difference between neighbouring samples in [from, to). */
float largestStep (const std::vector<float>& x, size_t from, size_t to)
{
    float largest = 0.0f;

    for (auto i = std::max (from, static_cast<size_t> (1)); i < std::min (to, x.size()); ++i)
        largest = std::max (largest, std::abs (x[i] - x[i - 1]));

    return largest;
}

} // namespace

FREQART_TEST (processorParameterDefaults)
{
    auto processor = makeProcessor();

    CHECK_NEAR (getParameter (*processor, params::id::level), -6.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::density), 256.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::contrast), 40.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::length), static_cast<float> (params::defaultLengthIndex), 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::trigger), 0.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::tempoSource), 0.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::bpm), 120.0f, 1.0e-3);
    CHECK_NEAR (getParameter (*processor, params::id::scale), 0.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::rangeLow), 20.0f, 1.0e-3);
    CHECK_NEAR (getParameter (*processor, params::id::rangeHigh), 20000.0f, 1.0e-2);
    CHECK_NEAR (getParameter (*processor, params::id::slope), 0.0f, 1.0e-4);

    auto* density = processor->getParameters().getParameter (params::id::density);
    auto* level = processor->getParameters().getParameter (params::id::level);
    CHECK (density->getCurrentValueAsText() == "256 lines");
    CHECK (level->getCurrentValueAsText() == "-6.0 dB");
    CHECK_NEAR (level->getValueForText ("-12 dB"), level->convertTo0to1 (-12.0f), 1.0e-4);

    // The analyser settings are not automation targets, nor are Density and Contrast (the plan
    // they shape is rebuilt on the message thread, out of step with an offline bounce); level
    // and the timing controls are.
    CHECK (! processor->getParameters().getParameter (params::id::scale)->isAutomatable());
    CHECK (! processor->getParameters().getParameter (params::id::rangeLow)->isAutomatable());
    CHECK (! processor->getParameters().getParameter (params::id::density)->isAutomatable());
    CHECK (! processor->getParameters().getParameter (params::id::contrast)->isAutomatable());
    CHECK (processor->getParameters().getParameter (params::id::level)->isAutomatable());
    CHECK (processor->getParameters().getParameter (params::id::length)->isAutomatable());

    // Density's skew puts ~200 lines in the middle of the knob.
    CHECK_NEAR (density->convertFrom0to1 (0.5f), 200.0f, 2.0f);
}

FREQART_TEST (processorLengths)
{
    CHECK_NEAR (params::lengthInQuarterNotes (0), 1.0, 1.0e-9);
    CHECK_NEAR (params::lengthInQuarterNotes (1), 2.0, 1.0e-9);
    CHECK_NEAR (params::lengthInQuarterNotes (2), 4.0, 1.0e-9);
    CHECK_NEAR (params::lengthInQuarterNotes (4), 16.0, 1.0e-9);
    CHECK_NEAR (params::lengthInQuarterNotes (7), 128.0, 1.0e-9);
    CHECK_NEAR (params::lengthInQuarterNotes (4, 3.0), 12.0, 1.0e-9);   // 4 bars of 3/4
    CHECK_NEAR (params::lengthInQuarterNotes (2, 3.0), 3.0, 1.0e-9);    // 1 bar of 6/8
    CHECK_NEAR (params::lengthInQuarterNotes (4, 0.0), 16.0, 1.0e-9);   // nonsense falls back to 4/4
    CHECK_NEAR (params::lengthInQuarterNotes (99), 128.0, 1.0e-9);

    auto processor = makeProcessor();
    setParameter (*processor, params::id::tempoSource, 1.0f);
    setParameter (*processor, params::id::bpm, 90.0f);
    CHECK_NEAR (processor->getEffectiveTempo(), 90.0, 0.01);
    CHECK_NEAR (processor->getLengthSeconds(), 16.0 * 60.0 / 90.0, 0.01);
}

FREQART_TEST (processorBusLayouts)
{
    FreqArtProcessor processor;

    juce::AudioProcessor::BusesLayout stereo;
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (processor.checkBusesLayoutSupported (stereo));

    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK (processor.checkBusesLayoutSupported (mono));

    juce::AudioProcessor::BusesLayout surround;
    surround.outputBuses.add (juce::AudioChannelSet::create5point1());
    CHECK (! processor.checkBusesLayoutSupported (surround));
}

FREQART_TEST (processorStateRoundTrip)
{
    auto source = makeProcessor();
    setParameter (*source, params::id::density, 512.0f);
    setParameter (*source, params::id::level, -12.0f);
    setParameter (*source, params::id::scale, 1.0f);
    setParameter (*source, params::id::slope, 2.0f);
    source->setEditorSize (900, 579);

    source->editDocument ([] (ArtDocument& document)
    {
        document.objects.push_back (ArtDocument::makeTextObject ("Round trip", 300.0f, 3000.0f));
        BrushStroke stroke;
        stroke.from = { 100.0f, 100.0f };
        stroke.to = { 400.0f, 300.0f };
        stroke.radiusX = stroke.radiusY = 12.0f;
        document.paintStroke (stroke);
    });

    juce::MemoryBlock state;
    source->getStateInformation (state);
    CHECK (state.getSize() > 0);

    auto restored = makeProcessor();
    restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    CHECK_NEAR (getParameter (*restored, params::id::density), 512.0f, 0.5f);
    CHECK_NEAR (getParameter (*restored, params::id::level), -12.0f, 0.01f);
    CHECK_NEAR (getParameter (*restored, params::id::scale), 1.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*restored, params::id::slope), 2.0f, 1.0e-4);
    CHECK (restored->getEditorWidth() == 900);
    CHECK (restored->getEditorHeight() == 579);

    const auto& a = source->getDocument();
    const auto& b = restored->getDocument();
    CHECK (b.objects.size() == a.objects.size());

    if (b.objects.size() == a.objects.size())
        for (size_t i = 0; i < a.objects.size(); ++i)
        {
            CHECK (b.objects[i].text == a.objects[i].text);
            CHECK (b.objects[i].id == a.objects[i].id);
            CHECK_NEAR (b.objects[i].lowHz, a.objects[i].lowHz, 0.01f);
        }

    CHECK (b.paint == a.paint);
    CHECK (b.hasPaint());

    // Loading clears the undo history: there is nothing to go back to.
    CHECK (! restored->canUndo());

    // The restored drawing is playable straight away.
    auto plan = restored->getCurrentPlan();
    CHECK (plan != nullptr && ! plan->silent);
}

FREQART_TEST (processorGarbageState)
{
    auto processor = makeProcessor();
    const auto before = processor->getCurrentPlan();

    juce::Random random (42);
    juce::MemoryBlock junk (2048);
    for (size_t i = 0; i < junk.getSize(); ++i)
        junk[i] = static_cast<char> (random.nextInt (256));

    processor->setStateInformation (junk.getData(), static_cast<int> (junk.getSize()));
    processor->setStateInformation (nullptr, 0);
    processor->setStateInformation (junk.getData(), 3);

    // Valid XML with the wrong root is ignored.
    juce::MemoryBlock wrongRoot;
    juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("SomethingElse"), wrongRoot);
    processor->setStateInformation (wrongRoot.getData(), static_cast<int> (wrongRoot.getSize()));

    CHECK (processor->getCurrentPlan() != nullptr);
    CHECK (! processor->getDocument().isEmpty());

    // A FreqArt state with a broken drawing loads as an empty drawing.
    juce::XmlElement root ("FreqArtState");
    auto* documentXml = root.createNewChildElement ("FreqArtDocument");
    documentXml->setAttribute ("paint", "!!!not base64!!!");
    documentXml->createNewChildElement ("Object")->setAttribute ("type", "banana");
    juce::MemoryBlock broken;
    juce::AudioProcessor::copyXmlToBinary (root, broken);
    processor->setStateInformation (broken.getData(), static_cast<int> (broken.getSize()));

    CHECK (processor->getDocument().objects.empty());
    CHECK (processor->getCurrentPlan() != nullptr);

    // It still plays (silence) without trouble.
    processor->setPreview (true);
    const auto out = render (*processor, 4096);
    CHECK_LESS (peak (out), 1.0e-6);
}

FREQART_TEST (processorSavesManyPicturesQuickly)
{
    using clock = std::chrono::steady_clock;
    const auto msSince = [] (clock::time_point start)
    {
        return std::chrono::duration<double, std::milli> (clock::now() - start).count();
    };

    // Eight pictures with some grain (which PNG cannot squeeze away, as in a photo): more than a
    // small cache of encodings would hold. Half the size of an imported photo, to keep the test
    // quick; at full size (1024 x 768) the first save takes ~3 s here and, before encodings were
    // kept for every picture in use, so did every save after an edit.
    constexpr int numPictures = 8, width = 512, height = 384;
    ArtDocument document;

    for (int i = 0; i < numPictures; ++i)
    {
        juce::Image picture (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
        {
            juce::Graphics g (picture);
            g.setGradientFill (juce::ColourGradient (juce::Colour::fromHSV (0.11f * static_cast<float> (i), 0.7f, 1.0f, 1.0f),
                                                     0.0f, 0.0f, juce::Colours::black,
                                                     static_cast<float> (width), static_cast<float> (height), false));
            g.fillAll();
        }

        juce::Random random (i + 1);
        {
            const juce::Image::BitmapData data (picture, juce::Image::BitmapData::readWrite);

            for (int y = 0; y < data.height; ++y)
                for (int x = 0; x < data.width; ++x)
                    data.setPixelColour (x, y, data.getPixelColour (x, y).brighter (0.1f * random.nextFloat()));
        }

        auto object = ArtDocument::makeImageObject (prepareImportedImage (picture), 3500.0f, 16000.0f);
        object.startTime = 0.12f * static_cast<float> (i);
        object.endTime = object.startTime + 0.1f;
        document.objects.push_back (object);
    }

    auto source = makeProcessor();
    source->setDocument (document);

    juce::MemoryBlock state;
    auto start = clock::now();
    source->getStateInformation (state);
    const auto firstSave = msSince (start);

    // An edit that leaves every picture as it was (a move), then a save, as hosts that keep
    // their own undo history do after each edit.
    constexpr int rounds = 3;
    double editedSave = 0.0;

    for (int round = 0; round < rounds; ++round)
    {
        source->editDocument ([round] (ArtDocument& d) { d.objects[0].startTime = 0.01f * static_cast<float> (round); });
        start = clock::now();
        source->getStateInformation (state);
        editedSave += msSince (start) / rounds;
    }

    auto restored = makeProcessor();
    start = clock::now();
    restored->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    const auto load = msSince (start);

    juce::MemoryBlock restoredState;
    start = clock::now();
    restored->getStateInformation (restoredState);
    const auto savedAfterLoad = msSince (start);

    std::printf ("    %d pictures, state %.1f MB: first save %.0f ms, save after an edit %.0f ms, load %.0f ms, "
                 "save after the load %.0f ms\n", numPictures, static_cast<double> (state.getSize()) / 1.0e6,
                 firstSave, editedSave, load, savedAfterLoad);

    CHECK (restored->getDocument().objects.size() == static_cast<size_t> (numPictures));

    // A state saved straight after loading holds the very drawing that was loaded.
    const auto sourceXml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    const auto restoredXml = juce::AudioProcessor::getXmlFromBinary (restoredState.getData(), static_cast<int> (restoredState.getSize()));
    CHECK (sourceXml != nullptr && restoredXml != nullptr);

    if (sourceXml != nullptr && restoredXml != nullptr)
    {
        const auto* a = sourceXml->getChildByName ("FreqArtDocument");
        const auto* b = restoredXml->getChildByName ("FreqArtDocument");
        CHECK (a != nullptr && b != nullptr && a->isEquivalentTo (b, false));
    }

    // Saving after an edit or a load encodes no picture again: it costs a fraction of the first
    // save, which encodes all of them.
    CHECK_LESS (editedSave, 0.5 * firstSave);
    CHECK_LESS (savedAfterLoad, 0.5 * firstSave);
}

FREQART_TEST (processorTellsTheHostAboutUnsavedChanges)
{
    // What the VST3 wrapper listens to: it marks the project dirty (IComponentHandler2::setDirty)
    // only for nonParameterStateChanged.
    struct HostListener final : public juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}

        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            if (details.nonParameterStateChanged)
                ++dirty;
        }

        int dirty = 0;
    };

    auto processor = makeProcessor();
    HostListener host;
    processor->addListener (&host);

    processor->editDocument ([] (ArtDocument& document) { document.objects.clear(); });
    CHECK (host.dirty == 1);
    processor->setDocument (textDocument ("DIRTY"));
    CHECK (host.dirty == 2);
    CHECK (processor->undo());
    CHECK (host.dirty == 3);
    CHECK (processor->redo());
    CHECK (host.dirty == 4);

    // Loading a project is not a change to it.
    juce::MemoryBlock state;
    processor->getStateInformation (state);
    processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (host.dirty == 4);

    // The window size is saved too: resizing counts, opening at the size it had does not.
    processor->setEditorSize (FreqArtEditor::baseWidth, FreqArtEditor::baseHeight);  // never resized = this size
    CHECK (host.dirty == 4);
    processor->setEditorSize (1400, 900);
    CHECK (host.dirty == 5);
    processor->setEditorSize (1400, 900);
    CHECK (host.dirty == 5);

    processor->removeListener (&host);
}

FREQART_TEST (processorTransportStoppedIsSilent)
{
    auto processor = makeProcessor();
    FakePlayHead playHead;
    playHead.set (false, 120.0, 0.0, 0.0);
    processor->setPlayHead (&playHead);

    const auto out = render (*processor, 48000);
    CHECK_LESS (peak (out), 1.0e-6);
    CHECK (! processor->isSounding());
    processor->setPlayHead (nullptr);
}

FREQART_TEST (processorTransportPlayingFollowsTheTimeline)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("FULL", Band::mid));
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    processor->setPlayHead (&playHead);

    // Play from bar 3 (ppq 8) at 120 BPM: the 4-bar drawing is half way through.
    const auto out = render (*processor, 48000, blockSize, [&] (int start, int, juce::MidiBuffer&)
    {
        const auto seconds = 4.0 + static_cast<double> (start) / sampleRate;
        playHead.set (true, 120.0, seconds * 2.0, seconds);
    });

    CHECK_GREATER (rms (out, 4800), 0.01);
    CHECK_NEAR (processor->getPlayPosition(), (8.0 + 2.0) / 16.0, 0.01);
    CHECK (processor->isSounding());
    CHECK_NEAR (processor->getEffectiveTempo(), 120.0, 1.0e-6);

    // The transport stops: the sound fades out within a few milliseconds.
    playHead.set (false, 120.0, 10.0, 5.0);
    const auto after = render (*processor, 4800);
    CHECK_LESS (peak (after, 960), 1.0e-6);
    processor->setPlayHead (nullptr);
}

FREQART_TEST (processorMidiNoteStartsTheDrawing)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("MIDI", Band::mid));
    setParameter (*processor, params::id::trigger, 1.0f);   // MIDI note
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    playHead.set (false, 120.0, 0.0, 0.0);
    processor->setPlayHead (&playHead);

    constexpr int noteBlock = 4;
    constexpr int noteOffset = 100;
    const auto noteSample = static_cast<size_t> (noteBlock * blockSize + noteOffset);

    const auto out = render (*processor, 48000, blockSize, [&] (int start, int, juce::MidiBuffer& midi)
    {
        if (start == noteBlock * blockSize)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), noteOffset);
    });

    CHECK_LESS (peak (out, 0, noteSample), 1.0e-6);
    CHECK_GREATER (rms (out, noteSample + 960, noteSample + 9600), 0.005);
    CHECK (processor->isSounding());
    processor->setPlayHead (nullptr);
}

FREQART_TEST (processorMidiPassEndsOnPanicAndStop)
{
    // A 4-bar pass lasts 8 s. All Sound Off, All Notes Off (on any channel) and stopping the
    // DAW each end it within the gate fade; a note-off alone does not.
    for (int variant = 0; variant < 4; ++variant)
    {
        auto processor = makeProcessor();
        processor->setDocument (textDocument ("PANIC", Band::mid));
        setParameter (*processor, params::id::trigger, 1.0f);   // MIDI note
        processor->rebuildPlanIfNeeded();

        FakePlayHead playHead;
        playHead.set (true, 120.0, 0.0, 0.0);
        processor->setPlayHead (&playHead);

        constexpr int endBlock = 94;   // about 1 s in
        const auto endSample = static_cast<size_t> (endBlock * blockSize + 100);

        const auto out = render (*processor, 96000, blockSize, [&] (int start, int, juce::MidiBuffer& midi)
        {
            const auto seconds = static_cast<double> (start) / sampleRate;
            playHead.set (variant != 3 || start < endBlock * blockSize, 120.0, seconds * 2.0, seconds);

            if (start == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);

            if (start == endBlock * blockSize)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 50);

                if (variant == 1)
                    midi.addEvent (juce::MidiMessage::allSoundOff (1), 100);
                else if (variant == 2)
                    for (int channel = 1; channel <= 16; ++channel)
                        midi.addEvent (juce::MidiMessage::allNotesOff (channel), 100);
            }
        });

        CHECK_GREATER (rms (out, 9600, endSample - 960), 0.005);

        if (variant == 0)
        {
            CHECK_GREATER (rms (out, endSample + 4800), 0.005);
            CHECK (processor->isSounding());
        }
        else
        {
            CHECK_LESS (peak (out, endSample + 960), 1.0e-6);
            CHECK (! processor->isSounding());
        }

        processor->setPlayHead (nullptr);
    }
}

FREQART_TEST (processorPreviewPlaysWhileStopped)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("PREVIEW", Band::high));
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    playHead.set (false, 120.0, 0.0, 0.0);
    processor->setPlayHead (&playHead);

    processor->setPreview (true);
    CHECK (processor->isPreviewing());
    const auto out = render (*processor, 24000);
    CHECK_GREATER (rms (out, 2400), 0.005);

    processor->setPreview (false);
    const auto after = render (*processor, 4800);
    CHECK_LESS (peak (after, 960), 1.0e-6);
    processor->setPlayHead (nullptr);
}

FREQART_TEST (processorNeverExceedsFullScale)
{
    auto processor = makeProcessor();
    setParameter (*processor, params::id::level, 0.0f);
    setParameter (*processor, params::id::density, 1024.0f);
    setParameter (*processor, params::id::contrast, 12.0f);

    // A canvas that is loud everywhere, plus a band of few dense lines (the peakiest case).
    ArtDocument document;
    std::fill (document.paint.begin(), document.paint.end(), static_cast<uint8_t> (255));
    processor->setDocument (document);
    processor->rebuildPlanIfNeeded();
    processor->setPreview (true);

    auto out = render (*processor, 96000);
    CHECK (peak (out) <= 1.0f);
    CHECK_GREATER (rms (out, 4800), 0.05);

    ArtDocument narrow;
    narrow.objects.push_back (ArtDocument::makeTextObject ("||||", 2000.0f, 2200.0f));
    processor->setDocument (narrow);
    processor->rebuildPlanIfNeeded();
    out = render (*processor, 96000);
    CHECK (peak (out) <= 1.0f);

    for (auto sample : out)
        if (! std::isfinite (sample))
        {
            CHECK (false);
            break;
        }
}

FREQART_TEST (processorLevelScalesTheOutput)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("LEVEL", Band::mid));
    processor->rebuildPlanIfNeeded();
    processor->setPreview (true);

    setParameter (*processor, params::id::level, -6.0f);
    const auto loud = render (*processor, 48000);
    const auto loudRms = rms (loud, 9600);

    setParameter (*processor, params::id::level, -26.0f);
    render (*processor, 4800);   // let the 20 ms gain ramp finish
    processor->setPreview (false);
    render (*processor, 4800);
    processor->setPreview (true);
    const auto quiet = render (*processor, 48000);
    const auto quietRms = rms (quiet, 9600);

    CHECK_NEAR (juce::Decibels::gainToDecibels (quietRms / loudRms), -20.0, 0.5);
}

FREQART_TEST (processorEmptyDrawingIsSilent)
{
    auto processor = makeProcessor();
    processor->setDocument (ArtDocument());
    processor->rebuildPlanIfNeeded();
    processor->setPreview (true);

    const auto out = render (*processor, 24000);
    CHECK_LESS (peak (out), 1.0e-6);

    auto plan = processor->getCurrentPlan();
    CHECK (plan != nullptr && plan->silent);
}

FREQART_TEST (processorRebuildsThePlan)
{
    auto processor = makeProcessor();
    const auto first = processor->getCurrentPlan();
    CHECK (first != nullptr);

    // Nothing changed: no new plan.
    processor->rebuildPlanIfNeeded();
    CHECK (processor->getCurrentPlan() == first);

    processor->editDocument ([] (ArtDocument& document) { document.objects.clear(); });
    processor->rebuildPlanIfNeeded();
    const auto second = processor->getCurrentPlan();
    CHECK (second != first);
    CHECK (second->version > first->version);
    CHECK (second->silent);

    // A setting that changes the plan.
    processor->setDocument (textDocument ("DENSITY"));
    processor->rebuildPlanIfNeeded();
    const auto withDefault = processor->getCurrentPlan();
    setParameter (*processor, params::id::density, 64.0f);
    processor->rebuildPlanIfNeeded();
    const auto lowDensity = processor->getCurrentPlan();
    CHECK (lowDensity != withDefault);
    CHECK_LESS (lowDensity->numLines, withDefault->numLines);

    // A setting that does not (level): no new plan.
    setParameter (*processor, params::id::level, -20.0f);
    processor->rebuildPlanIfNeeded();
    CHECK (processor->getCurrentPlan() == lowDensity);
}

FREQART_TEST (processorUndoRedoAndCoalescing)
{
    auto processor = makeProcessor();
    processor->setDocument (ArtDocument());
    const auto addText = [] (const juce::String& text)
    {
        return [text] (ArtDocument& document) { document.objects.push_back (ArtDocument::makeTextObject (text, 300.0f, 3000.0f)); };
    };

    processor->editDocument (addText ("one"));
    processor->editDocument (addText ("two"));
    CHECK (processor->getDocument().objects.size() == 2);

    // A stroke made of many edits with one key is one undo step.
    for (int i = 0; i < 10; ++i)
        processor->editDocument ([i] (ArtDocument& document)
        {
            BrushStroke stroke;
            stroke.from = stroke.to = { 50.0f + 10.0f * static_cast<float> (i), 200.0f };
            document.paintStroke (stroke);
        }, "stroke-1");

    processor->endCoalescing();
    CHECK (processor->getDocument().hasPaint());

    CHECK (processor->undo());
    CHECK (! processor->getDocument().hasPaint());
    CHECK (processor->getDocument().objects.size() == 2);

    CHECK (processor->undo());
    CHECK (processor->getDocument().objects.size() == 1);

    CHECK (processor->redo());
    CHECK (processor->getDocument().objects.size() == 2);
    CHECK (processor->redo());
    CHECK (processor->getDocument().hasPaint());
    CHECK (! processor->redo());

    // A new edit clears the redo steps.
    CHECK (processor->undo());
    processor->editDocument (addText ("three"));
    CHECK (! processor->canRedo());
}

FREQART_TEST (processorBypassIsSilentAndRecovers)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("BYPASS"));
    processor->rebuildPlanIfNeeded();
    processor->setPreview (true);
    const auto before = render (*processor, 9600);

    // Into bypass while sounding: the sound carries on into the gate's fade-out (no step down
    // to zero mid-waveform) and is gone once it has run, on every channel.
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    processor->processBlockBypassed (buffer, midi);

    std::vector<float> bypassed (buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    const auto fadeEnd = static_cast<size_t> (std::ceil (ArtEngine::gateFadeSeconds * sampleRate)) + 1;
    const auto tail = before.size() - 2400;
    CHECK_GREATER (peak (bypassed, 0, 48), 0.25f * peak (before, tail));
    CHECK_LESS (std::abs (bypassed[0] - before.back()), 1.5f * largestStep (before, tail, before.size()) + 1.0e-6f);
    CHECK_LESS (largestStep (bypassed, 0, blockSize), 1.5f * largestStep (before, tail, before.size()) + 1.0e-6f);
    CHECK_LESS (peak (bypassed, fadeEnd), 1.0e-9f);
    CHECK_LESS (std::abs (buffer.getSample (1, 10) - bypassed[10]), 1.0e-9f);
    CHECK (! processor->isSounding());

    buffer.setSample (0, 0, 1.0f);
    processor->processBlockBypassed (buffer, midi);
    CHECK_LESS (buffer.getMagnitude (0, blockSize), 1.0e-9f);

    // Out of bypass: back in with a fade-in from silence.
    const auto out = render (*processor, 24000);
    CHECK_LESS (std::abs (out[0]), 1.0e-6f);
    CHECK_LESS (peak (out, 0, 24), 0.25f * peak (out, 4800));
    CHECK_GREATER (rms (out, 4800), 0.005);
}

FREQART_TEST (processorFollowsTheHostTimeSignature)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("BARS", Band::mid));
    setParameter (*processor, params::id::length, 2.0f);   // 1 bar
    processor->rebuildPlanIfNeeded();
    CHECK (processor->getTimeSignature() == (juce::AudioPlayHead::TimeSignature { 4, 4 }));

    // One bar of 4/4, then 3/4 from ppq 4, at 120 BPM (24000 samples a quarter note), the host
    // reporting its bars. Every 3/4 bar starts the drawing again.
    FakePlayHead playHead;
    processor->setPlayHead (&playHead);
    int64_t played = 0;
    std::vector<double> atBars;

    const auto playTo = [&] (double ppq)
    {
        const auto samples = static_cast<int> (std::llround (ppq * 24000.0) - played);
        render (*processor, samples, 64, [&] (int start, int, juce::MidiBuffer&)
        {
            const auto at = static_cast<double> (played + start) / 24000.0;
            playHead.set (true, 120.0, at, at * 0.5);

            if (at >= 4.0)
            {
                playHead.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 3, 4 });
                playHead.info.setPpqPositionOfLastBarStart (4.0 + 3.0 * std::floor ((at - 4.0) / 3.0 + 1.0e-9));
            }
            else
            {
                playHead.info.setPpqPositionOfLastBarStart (0.0);
            }
        });

        played += samples;
    };

    for (const auto bar : { 7.0, 10.0, 13.0 })
    {
        playTo (bar + 64.0 / 24000.0);   // to the end of the first block of the bar
        atBars.push_back (processor->getPlayPosition());
    }

    for (const auto position : atBars)
        CHECK_NEAR (position, 64.0 / 24000.0 / 3.0, 1.0e-6);

    CHECK (processor->getTimeSignature() == (juce::AudioPlayHead::TimeSignature { 3, 4 }));
    CHECK_NEAR (processor->getLengthSeconds(), 1.5, 1.0e-9);
    processor->setPlayHead (nullptr);

    // The canvas's grid in that signature: three beats to the bar, not four.
    const auto fractionsOf = [] (const std::vector<ui::axes::TimeMark>& marks)
    {
        std::vector<double> fractions;

        for (const auto& mark : marks)
            fractions.push_back (mark.fraction);

        return fractions;
    };

    const auto oneBar = ui::axes::timeMarks (2, 3, 4, 600.0f);
    CHECK (oneBar.size() == 3);

    if (oneBar.size() == 3)
    {
        CHECK_NEAR (oneBar[1].fraction, 1.0 / 3.0, 1.0e-9);
        CHECK_NEAR (oneBar[2].fraction, 2.0 / 3.0, 1.0e-9);
        CHECK (oneBar[0].major && oneBar[0].label == "1");
        CHECK (! oneBar[1].major && oneBar[1].label == "1.2" && oneBar[2].label == "1.3");
    }

    // 4 bars of 3/4: 4 bars and 8 beats in between; 6/8: six eighths to the bar.
    const auto fourBars = ui::axes::timeMarks (4, 3, 4, 600.0f);
    CHECK (fourBars.size() == 12);
    CHECK (fractionsOf (ui::axes::timeMarks (2, 6, 8, 600.0f)).size() == 6);

    // 4/4 as before: 4 bars of 4 beats; under a bar, sixteenths.
    CHECK (ui::axes::timeMarks (4, 4, 4, 600.0f).size() == 16);
    CHECK (ui::axes::timeMarks (0, 3, 4, 600.0f).size() == 4);

    // "2 beats" in 3/8 is a bar and a third: the second bar is short.
    const auto uneven = fractionsOf (ui::axes::timeMarks (1, 3, 8, 600.0f));
    CHECK (uneven.size() == 4);

    if (uneven.size() == 4)
    {
        CHECK_NEAR (uneven[1], 0.25, 1.0e-9);
        CHECK_NEAR (uneven[2], 0.5, 1.0e-9);
        CHECK_NEAR (uneven[3], 0.75, 1.0e-9);
    }
}

FREQART_TEST (processorLargerBlocksThanAnnounced)
{
    auto processor = std::make_unique<FreqArtProcessor>();
    processor->setPlayConfigDetails (0, 2, sampleRate, 64);
    processor->prepareToPlay (sampleRate, 64);
    processor->setDocument (textDocument ("BIG"));
    processor->rebuildPlanIfNeeded();
    processor->setPreview (true);

    // Blocks far larger than announced (and than the internal chunk) are handled in pieces.
    const auto out = render (*processor, 48000, 12000);
    CHECK_GREATER (rms (out, 4800), 0.005);
    CHECK (peak (out) <= 1.0f);
}
