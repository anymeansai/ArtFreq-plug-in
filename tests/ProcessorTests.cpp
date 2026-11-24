// The host-facing processor: parameters, state, playback in each trigger mode, level safety
// and editing.

#include "LicenseTestSupport.h"
#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "ui/CanvasAxes.h"
#include "ui/Editor.h"

#include <chrono>
#include <cstdio>

using namespace artfreq;
using namespace testutil;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

std::unique_ptr<ArtFreqProcessor> makeProcessor()
{
    auto processor = std::make_unique<ArtFreqProcessor>();
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

ARTFREQ_TEST (processorParameterDefaults)
{
    auto processor = makeProcessor();

    CHECK_NEAR (getParameter (*processor, params::id::level), -6.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::density), 256.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::contrast), 40.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::length), static_cast<float> (params::defaultLengthIndex), 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::trigger), 0.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::repeat), 0.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::startAt), static_cast<float> (params::onPlayStartIndex), 1.0e-4);
    CHECK_NEAR (getParameter (*processor, params::id::startBar), 1.0f, 1.0e-4);
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
    CHECK (processor->getParameters().getParameter (params::id::trigger)->isAutomatable());
    CHECK (processor->getParameters().getParameter (params::id::repeat)->isAutomatable());
    CHECK (processor->getParameters().getParameter (params::id::startBar)->isAutomatable());

    // Where the single pass starts is how the plug-in is used, not something to perform.
    CHECK (! processor->getParameters().getParameter (params::id::startAt)->isAutomatable());

    // Two triggers (nothing plays by itself), Repeat off, a single pass from where the DAW starts
    // playing (so the canvas numbers its bars from 1, not as bars of the song); At bar, from bar 1.
    auto* trigger = processor->getParameters().getParameter (params::id::trigger);
    auto* repeat = processor->getParameters().getParameter (params::id::repeat);
    auto* startBar = processor->getParameters().getParameter (params::id::startBar);
    CHECK (trigger->getNumSteps() == 2);
    CHECK (trigger->getCurrentValueAsText() == "Transport");
    CHECK (repeat->getCurrentValueAsText() == "Off");
    CHECK (startBar->getCurrentValueAsText() == "Bar 1");
    CHECK_NEAR (startBar->convertFrom0to1 (startBar->getValueForText ("Bar 12")), 12.0f, 1.0e-3);
    CHECK_NEAR (startBar->convertFrom0to1 (1.0f), 999.0f, 1.0e-3);

    auto* startAt = processor->getParameters().getParameter (params::id::startAt);
    CHECK (startAt->getNumSteps() == 2);
    CHECK (startAt->getCurrentValueAsText() == "On play");
    CHECK (processor->getSinglePassStartBar() == 0);
    setParameter (*processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    CHECK (startAt->getCurrentValueAsText() == "At bar");
    CHECK (processor->getSinglePassStartBar() == 1);

    // Density's skew puts ~200 lines in the middle of the knob.
    CHECK_NEAR (density->convertFrom0to1 (0.5f), 200.0f, 2.0f);
}

ARTFREQ_TEST (processorLengths)
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

ARTFREQ_TEST (processorBusLayouts)
{
    ArtFreqProcessor processor;

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

ARTFREQ_TEST (processorStateRoundTrip)
{
    auto source = makeProcessor();
    setParameter (*source, params::id::density, 512.0f);
    setParameter (*source, params::id::level, -12.0f);
    setParameter (*source, params::id::scale, 1.0f);
    setParameter (*source, params::id::slope, 2.0f);
    setParameter (*source, params::id::trigger, 1.0f);
    setParameter (*source, params::id::repeat, 1.0f);
    setParameter (*source, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    setParameter (*source, params::id::startBar, 17.0f);
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
    CHECK_NEAR (getParameter (*restored, params::id::trigger), 1.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*restored, params::id::repeat), 1.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*restored, params::id::startAt), static_cast<float> (params::atBarStartIndex), 1.0e-4);
    CHECK_NEAR (getParameter (*restored, params::id::startBar), 17.0f, 1.0e-4);
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

ARTFREQ_TEST (processorLoadsStatesSavedAsFreqArt)
{
    // Before 0.4 the plug-in was called FreqArt and saved FreqArtState, FreqArtParameters and
    // FreqArtDocument: an old project opens with its settings and drawing, and saves again
    // under the new names.
    auto source = makeProcessor();
    setParameter (*source, params::id::repeat, 1.0f);
    setParameter (*source, params::id::startBar, 17.0f);
    source->editDocument ([] (ArtDocument& document)
    {
        document.objects.push_back (ArtDocument::makeTextObject ("Old project", 300.0f, 3000.0f));
    });

    juce::MemoryBlock state;
    source->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    CHECK (xml != nullptr && xml->hasTagName ("ArtFreqState"));

    if (xml == nullptr)
        return;

    xml->setTagName ("FreqArtState");

    for (auto* child : xml->getChildIterator())
        child->setTagName (child->getTagName().replace ("ArtFreq", "FreqArt"));

    CHECK (xml->getChildByName ("FreqArtParameters") != nullptr && xml->getChildByName ("FreqArtDocument") != nullptr);

    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);

    auto restored = makeProcessor();
    restored->setStateInformation (old.getData(), static_cast<int> (old.getSize()));

    CHECK_NEAR (getParameter (*restored, params::id::repeat), 1.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*restored, params::id::startBar), 17.0f, 1.0e-4);
    CHECK (restored->getDocument().objects.size() == source->getDocument().objects.size());
    CHECK (! restored->getDocument().objects.empty() && restored->getDocument().objects.back().text == "Old project");

    juce::MemoryBlock resaved;
    restored->getStateInformation (resaved);
    auto again = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), static_cast<int> (resaved.getSize()));
    CHECK (again != nullptr && again->hasTagName ("ArtFreqState")
           && again->getChildByName ("ArtFreqParameters") != nullptr
           && again->getChildByName ("ArtFreqDocument") != nullptr);
}

ARTFREQ_TEST (processorLoadsStatesFromBeforeRepeat)
{
    // A state as ArtFreq 0.1 saved it: no Repeat, no start bar (nor where the pass starts), no
    // Lows, and a trigger of 0, 1 or 2 ("Free loop", saved as its index or, by some hosts'
    // presets, as its name).
    const auto oldState = [] (const juce::String& triggerValue)
    {
        auto processor = makeProcessor();
        juce::MemoryBlock state;
        processor->getStateInformation (state);

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        auto* parametersXml = xml != nullptr ? xml->getChildByName ("ArtFreqParameters") : nullptr;
        CHECK (parametersXml != nullptr);

        if (parametersXml != nullptr)
        {
            for (auto* id : { params::id::repeat, params::id::startAt, params::id::startBar, params::id::lowEnd })
                if (auto* param = parametersXml->getChildByAttribute ("id", id))
                    parametersXml->removeChildElement (param, true);

            if (auto* trigger = parametersXml->getChildByAttribute ("id", params::id::trigger))
                trigger->setAttribute ("value", triggerValue);
        }

        juce::MemoryBlock old;

        if (xml != nullptr)
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

        return old;
    };

    // Loaded into an instance that has other values, so "left as it was" would show.
    const auto load = [] (const juce::MemoryBlock& state)
    {
        auto processor = makeProcessor();
        setParameter (*processor, params::id::repeat, 1.0f);
        setParameter (*processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
        setParameter (*processor, params::id::startBar, 9.0f);
        setParameter (*processor, params::id::trigger, 1.0f);
        processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        return processor;
    };

    for (const auto& [saved, trigger, repeat] : { std::tuple ("0", 0.0f, 0.0f), std::tuple ("1", 1.0f, 0.0f),
                                                  std::tuple ("2", 0.0f, 1.0f), std::tuple ("2.0", 0.0f, 1.0f),
                                                  std::tuple ("Free loop", 0.0f, 1.0f) })
    {
        const auto processor = load (oldState (saved));
        CHECK_NEAR (getParameter (*processor, params::id::trigger), trigger, 1.0e-4);
        CHECK_NEAR (getParameter (*processor, params::id::repeat), repeat, 1.0e-4);
        CHECK_NEAR (getParameter (*processor, params::id::startBar), 1.0f, 1.0e-4);
        CHECK_NEAR (getParameter (*processor, params::id::startAt), static_cast<float> (params::onPlayStartIndex), 1.0e-4);
        CHECK_NEAR (getParameter (*processor, params::id::lowEnd), static_cast<float> (params::stripesLowEndIndex), 1.0e-4);
    }

    // A free loop state, saved again, is an ordinary Transport + Repeat state.
    const auto migrated = load (oldState ("2"));
    juce::MemoryBlock resaved;
    migrated->getStateInformation (resaved);
    const auto reloaded = load (resaved);
    CHECK_NEAR (getParameter (*reloaded, params::id::trigger), 0.0f, 1.0e-4);
    CHECK_NEAR (getParameter (*reloaded, params::id::repeat), 1.0f, 1.0e-4);
}

ARTFREQ_TEST (processorLoadsStatesFromBeforeStartAt)
{
    // A state as 0.2 to 0.4 saved it: a start bar, and nothing saying where the single pass
    // starts (it always started at that bar). Bar 1, the default nobody chose, loads as On play;
    // any other bar was chosen, and loads as At bar, so that project plays where it did.
    const auto oldState = [] (int startBar)
    {
        auto processor = makeProcessor();
        setParameter (*processor, params::id::startBar, static_cast<float> (startBar));
        juce::MemoryBlock state;
        processor->getStateInformation (state);

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        auto* parametersXml = xml != nullptr ? xml->getChildByName ("ArtFreqParameters") : nullptr;
        CHECK (parametersXml != nullptr && parametersXml->getChildByAttribute ("id", params::id::startAt) != nullptr);

        if (parametersXml != nullptr)
            parametersXml->removeChildElement (parametersXml->getChildByAttribute ("id", params::id::startAt), true);

        juce::MemoryBlock old;

        if (xml != nullptr)
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

        return old;
    };

    // Loaded into an instance set the other way, so "left as it was" would show.
    const auto load = [] (const juce::MemoryBlock& state, int startAtBefore)
    {
        auto processor = makeProcessor();
        setParameter (*processor, params::id::startAt, static_cast<float> (startAtBefore));
        processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        return processor;
    };

    const auto fromBarOne = load (oldState (1), params::atBarStartIndex);
    CHECK_NEAR (getParameter (*fromBarOne, params::id::startAt), static_cast<float> (params::onPlayStartIndex), 1.0e-4);
    CHECK_NEAR (getParameter (*fromBarOne, params::id::startBar), 1.0f, 1.0e-4);
    CHECK (fromBarOne->getSinglePassStartBar() == 0);

    for (const auto bar : { 2, 17, 999 })
    {
        const auto placed = load (oldState (bar), params::onPlayStartIndex);
        CHECK_NEAR (getParameter (*placed, params::id::startAt), static_cast<float> (params::atBarStartIndex), 1.0e-4);
        CHECK_NEAR (getParameter (*placed, params::id::startBar), static_cast<float> (bar), 1.0e-4);
        CHECK (placed->getSinglePassStartBar() == bar);

        // Saved again it is an ordinary At bar state.
        juce::MemoryBlock resaved;
        placed->getStateInformation (resaved);
        const auto again = load (resaved, params::onPlayStartIndex);
        CHECK_NEAR (getParameter (*again, params::id::startAt), static_cast<float> (params::atBarStartIndex), 1.0e-4);
    }

    // A state of this version keeps what it says, whatever its bar: On play with a bar typed in
    // earlier, and At bar 1.
    for (const auto& [startAt, bar] : { std::pair (params::onPlayStartIndex, 9), std::pair (params::atBarStartIndex, 1) })
    {
        auto source = makeProcessor();
        setParameter (*source, params::id::startAt, static_cast<float> (startAt));
        setParameter (*source, params::id::startBar, static_cast<float> (bar));
        juce::MemoryBlock state;
        source->getStateInformation (state);

        const auto restored = load (state, startAt == params::onPlayStartIndex ? params::atBarStartIndex : params::onPlayStartIndex);
        CHECK_NEAR (getParameter (*restored, params::id::startAt), static_cast<float> (startAt), 1.0e-4);
        CHECK_NEAR (getParameter (*restored, params::id::startBar), static_cast<float> (bar), 1.0e-4);
    }
}

ARTFREQ_TEST (processorLowsDefaultsToSmoothButOldStatesKeepStripes)
{
    // New instances play Smooth lows (the standing comb). Lows is a setting of the plan, so, like
    // Density, not an automation target.
    auto fresh = makeProcessor();
    auto* lows = fresh->getParameters().getParameter (params::id::lowEnd);
    CHECK_NEAR (getParameter (*fresh, params::id::lowEnd), static_cast<float> (params::smoothLowEndIndex), 1.0e-4);
    CHECK (lows->getCurrentValueAsText() == "Smooth" && lows->getNumSteps() == 2);
    CHECK (! lows->isAutomatable());
    CHECK (fresh->getPlanSettings().lowEnd == LowEnd::smooth);
    CHECK (fresh->getCurrentPlan() != nullptr && fresh->getCurrentPlan()->combLines > 0);

    // A state saved before Lows existed (0.1, 0.2) was made with the stripes: it loads with them,
    // also into an instance on Smooth, so an old project looks and sounds as it did.
    juce::MemoryBlock state;
    fresh->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    auto* parametersXml = xml != nullptr ? xml->getChildByName ("ArtFreqParameters") : nullptr;
    CHECK (parametersXml != nullptr && parametersXml->getChildByAttribute ("id", params::id::lowEnd) != nullptr);

    if (parametersXml != nullptr)
        parametersXml->removeChildElement (parametersXml->getChildByAttribute ("id", params::id::lowEnd), true);

    juce::MemoryBlock old;

    if (xml != nullptr)
        juce::AudioProcessor::copyXmlToBinary (*xml, old);

    const auto load = [] (const juce::MemoryBlock& block)
    {
        auto processor = makeProcessor();
        processor->setStateInformation (block.getData(), static_cast<int> (block.getSize()));
        return processor;
    };

    const auto loaded = load (old);
    CHECK_NEAR (getParameter (*loaded, params::id::lowEnd), static_cast<float> (params::stripesLowEndIndex), 1.0e-4);
    CHECK (loaded->getCurrentPlan() != nullptr && loaded->getCurrentPlan()->combLines == 0);

    // Saved again (now with Lows in it) it stays Stripes; a Smooth state stays Smooth.
    juce::MemoryBlock resaved;
    loaded->getStateInformation (resaved);
    CHECK_NEAR (getParameter (*load (resaved), params::id::lowEnd), static_cast<float> (params::stripesLowEndIndex), 1.0e-4);

    const auto smoothAgain = load (state);
    CHECK_NEAR (getParameter (*smoothAgain, params::id::lowEnd), static_cast<float> (params::smoothLowEndIndex), 1.0e-4);
    CHECK (smoothAgain->getCurrentPlan() != nullptr && smoothAgain->getCurrentPlan()->combLines > 0);

    // A project saved with 0.5 (Smooth: the gliding comb then; no analyser settings yet, and no
    // knowledge of the fades in time) opens on Smooth, played by the standing comb with its fades
    // worked out for the project's Length and Repeat, and its canvas and Live view get today's
    // defaults: the cleaner sound and the cleaner picture, nothing to set.
    if (xml != nullptr)
    {
        auto fromPointFive = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        auto* saved = fromPointFive != nullptr ? fromPointFive->getChildByName ("ArtFreqParameters") : nullptr;
        CHECK (saved != nullptr);

        if (saved != nullptr)
        {
            for (const auto* id : { params::id::analyserFft, params::id::analyserFloor, params::id::analyserCeiling })
            {
                CHECK (saved->getChildByAttribute ("id", id) != nullptr);
                saved->removeChildElement (saved->getChildByAttribute ("id", id), true);
            }

            juce::MemoryBlock pointFive;
            juce::AudioProcessor::copyXmlToBinary (*fromPointFive, pointFive);
            const auto opened = load (pointFive);
            CHECK (opened->getPlanSettings().lowEnd == LowEnd::smooth);
            CHECK (opened->getCurrentPlan() != nullptr && opened->getCurrentPlan()->combLines > 0);
            CHECK_GREATER (opened->getPlanSettings().passSeconds, 0.0);
            CHECK (opened->getPlanSettings().repeats == (getParameter (*opened, params::id::repeat) >= 0.5f));
            CHECK_NEAR (opened->getAnalyserView().floorDb, params::defaultAnalyserFloorDb, 1.0e-4);
            CHECK_NEAR (opened->getAnalyserView().ceilingDb, params::defaultAnalyserCeilingDb, 1.0e-4);
            CHECK (opened->getAnalyserView().fftSize == 8192);
        }
    }
}

ARTFREQ_TEST (processorSwitchingLowsWhilePlayingDoesNotClick)
{
    // Lows changes the plan's layout (the comb's sines or the lines), so the engine crossfades
    // from one to the other: no click either way, also when switched back before the crossfade
    // has ended.
    auto processor = makeProcessor();
    auto document = textDocument ("LOW", Band::low);
    document.objects.push_back (textDocument ("MID", Band::mid).objects.front());
    processor->setDocument (document);
    processor->rebuildPlanIfNeeded();
    processor->setPreview (true);

    const auto settle = [&] (int index)
    {
        setParameter (*processor, params::id::lowEnd, static_cast<float> (index));
        processor->rebuildPlanIfNeeded();
        return processor->getCurrentPlan()->combLines > 0;
    };

    // The largest sample-to-sample step of each, playing steadily.
    const auto smoothSteady = render (*processor, 24000);
    CHECK (settle (params::stripesLowEndIndex) == false);
    const auto stripesSteady = render (*processor, 24000);
    const auto steadyStep = std::max (largestStep (smoothSteady, 4800, smoothSteady.size()),
                                      largestStep (stripesSteady, 4800, stripesSteady.size()));

    std::vector<float> switching;
    const auto play = [&] (int samples)
    {
        const auto block = render (*processor, samples);
        switching.insert (switching.end(), block.begin(), block.end());
    };

    for (int i = 0; i < 6; ++i)
    {
        CHECK (settle (i % 2 == 0 ? params::smoothLowEndIndex : params::stripesLowEndIndex) == (i % 2 == 0));
        play (i < 4 ? 6000 : 256);   // the last ones: back within a crossfade
    }

    play (6000);
    std::printf ("    largest step: steady %.4f, switching Lows %.4f\n", steadyStep, largestStep (switching, 0, switching.size()));
    CHECK_LESS (largestStep (switching, 0, switching.size()), 1.5f * steadyStep);
    CHECK_GREATER (rms (switching), 0.02);
}

ARTFREQ_TEST (processorGarbageState)
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

    // An ArtFreq state with a broken drawing loads as an empty drawing.
    juce::XmlElement root ("ArtFreqState");
    auto* documentXml = root.createNewChildElement ("ArtFreqDocument");
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

ARTFREQ_TEST (processorSavesManyPicturesQuickly)
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
        const auto* a = sourceXml->getChildByName ("ArtFreqDocument");
        const auto* b = restoredXml->getChildByName ("ArtFreqDocument");
        CHECK (a != nullptr && b != nullptr && a->isEquivalentTo (b, false));
    }

    // Saving after an edit or a load encodes no picture again: it costs a fraction of the first
    // save, which encodes all of them.
    CHECK_LESS (editedSave, 0.5 * firstSave);
    CHECK_LESS (savedAfterLoad, 0.5 * firstSave);
}

ARTFREQ_TEST (processorTellsTheHostAboutUnsavedChanges)
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
    processor->setEditorSize (ArtFreqEditor::baseWidth, ArtFreqEditor::baseHeight);  // never resized = this size
    CHECK (host.dirty == 4);
    processor->setEditorSize (1400, 900);
    CHECK (host.dirty == 5);
    processor->setEditorSize (1400, 900);
    CHECK (host.dirty == 5);

    processor->removeListener (&host);
}

ARTFREQ_TEST (processorTransportStoppedIsSilent)
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

ARTFREQ_TEST (processorTransportPlayingFollowsTheTimeline)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("FULL", Band::mid));
    setParameter (*processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    processor->setPlayHead (&playHead);

    // At bar 1. Play from bar 3 (ppq 8) at 120 BPM: the 4-bar drawing is half way through.
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

ARTFREQ_TEST (processorPlaysAtOnceWhereverTheDawStarts)
{
    // A fresh instance, nothing touched (Transport, Repeat off, On play), a painted "1 bar"
    // drawing. The DAW starts playing in the middle of bar 33: sound within 10 ms, the drawing
    // from its start, once (2 s at 120 BPM), then silence. Stop and play again: once more.
    auto processor = makeProcessor();
    ArtDocument painted;
    std::fill (painted.paint.begin(), painted.paint.end(), static_cast<uint8_t> (255));
    processor->setDocument (painted);
    setParameter (*processor, params::id::length, 2.0f);   // 1 bar
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    processor->setPlayHead (&playHead);

    const auto playFrom = [&] (double ppq, int numSamples, int block = blockSize)
    {
        return render (*processor, numSamples, block, [&] (int start, int, juce::MidiBuffer&)
        {
            const auto seconds = ppq * 0.5 + static_cast<double> (start) / sampleRate;
            playHead.set (true, 120.0, seconds * 2.0, seconds);
        });
    };

    auto out = playFrom (129.37, 3 * 48000);
    CHECK_GREATER (peak (out, 0, 480), 1.0e-4);
    CHECK_GREATER (rms (out, 4800, 96000 - 4800), 0.005);
    CHECK_GREATER (peak (out, 96000 - 720, 96000), 1.0e-4);
    CHECK_LESS (peak (out, 96000), 1.0e-6);
    CHECK (! processor->isSounding());

    // The playhead on the canvas: half way through after a second.
    playHead.set (false, 120.0, 135.37, 67.685);
    render (*processor, 4800);
    out = playFrom (200.0, 48000);
    CHECK_GREATER (rms (out, 4800), 0.005);
    CHECK_NEAR (processor->getPlayPosition(), 0.5, 0.01);
    CHECK (processor->isSounding());

    // Stopped: silent within a few milliseconds.
    playHead.set (false, 120.0, 202.0, 101.0);
    CHECK_LESS (peak (render (*processor, 4800), 960), 1.0e-6);

    // The same for any block size, and for an instance first prepared while the DAW plays.
    for (const auto block : { 32, 480, 2048 })
    {
        processor->releaseResources();
        processor->prepareToPlay (sampleRate, block);
        out = playFrom (1000.5, 2 * 48000 + 4800, block);
        CHECK_GREATER (peak (out, 0, 480), 1.0e-4);
        CHECK_GREATER (rms (out, 4800, 96000 - 4800), 0.005);
        CHECK_LESS (peak (out, 96000), 1.0e-6);
    }

    processor->setPlayHead (nullptr);
}

ARTFREQ_TEST (processorOnPlayUnderADawLoopAndOutOfBypass)
{
    auto processor = makeProcessor();
    ArtDocument painted;
    std::fill (painted.paint.begin(), painted.paint.end(), static_cast<uint8_t> (255));
    processor->setDocument (painted);
    setParameter (*processor, params::id::length, 2.0f);   // 1 bar: 96000 samples at 120 BPM
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    processor->setPlayHead (&playHead);

    // The DAW loops over two bars from bar 9 (ppq 32 to 40): every round the drawing sounds over
    // the first bar and is silent over the second.
    const auto out = render (*processor, 3 * 192000, 480, [&] (int start, int, juce::MidiBuffer&)
    {
        const auto ppq = 32.0 + static_cast<double> (start % 192000) / 24000.0;
        playHead.set (true, 120.0, ppq, ppq * 0.5);
    });

    for (size_t round = 0; round < 3; ++round)
    {
        const auto from = round * 192000;
        CHECK_GREATER (peak (out, from, from + 960), 1.0e-4);
        CHECK_GREATER (rms (out, from + 4800, from + 96000 - 4800), 0.005);
        CHECK_LESS (peak (out, from + 96000 + 480, from + 192000), 1.0e-6);
    }

    // Bypassed while the DAW plays on, then back: a new pass begins where it comes back.
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int i = 0; i < 4; ++i)
        processor->processBlockBypassed (buffer, midi);

    const auto back = render (*processor, 2 * 96000 + 4800, blockSize, [&] (int start, int, juce::MidiBuffer&)
    {
        const auto ppq = 500.0 + static_cast<double> (start) / 24000.0;
        playHead.set (true, 120.0, ppq, ppq * 0.5);
    });

    CHECK_GREATER (peak (back, 0, 480), 1.0e-4);
    CHECK_GREATER (rms (back, 4800, 96000 - 4800), 0.005);
    CHECK_LESS (peak (back, 96000), 1.0e-6);
    processor->setPlayHead (nullptr);
}

ARTFREQ_TEST (processorTransportPlaysOnceFromTheStartBar)
{
    // Repeat off (the default), At bar, "1 bar" from bar 2: at 120 BPM the drawing sounds from
    // 2 s to 4 s of the song and nowhere else. Then Repeat on: it loops every bar from bar 1.
    // Paint across the whole drawing, so there is sound right up to its edges.
    auto processor = makeProcessor();
    ArtDocument painted;
    std::fill (painted.paint.begin(), painted.paint.end(), static_cast<uint8_t> (255));
    processor->setDocument (painted);
    setParameter (*processor, params::id::length, 2.0f);   // 1 bar
    setParameter (*processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    setParameter (*processor, params::id::startBar, 2.0f);
    processor->rebuildPlanIfNeeded();
    CHECK (processor->getSinglePassStartBar() == 2);

    FakePlayHead playHead;
    processor->setPlayHead (&playHead);

    const auto playFromTheStart = [&] (int numSamples)
    {
        return render (*processor, numSamples, blockSize, [&] (int start, int, juce::MidiBuffer&)
        {
            const auto seconds = static_cast<double> (start) / sampleRate;
            playHead.set (true, 120.0, seconds * 2.0, seconds);
        });
    };

    const auto out = playFromTheStart (6 * 48000);
    CHECK_LESS (peak (out, 0, 96000), 1.0e-6);
    CHECK_GREATER (rms (out, 96000 + 4800, 192000 - 4800), 0.005);
    CHECK_LESS (peak (out, 192000), 1.0e-6);

    // The fades at its edges are short: sound within 10 ms of each.
    CHECK_GREATER (peak (out, 96000, 96000 + 480), 1.0e-4);
    CHECK_GREATER (peak (out, 192000 - 720, 192000), 1.0e-4);

    setParameter (*processor, params::id::repeat, 1.0f);
    CHECK (processor->getSinglePassStartBar() == 0);
    processor->releaseResources();
    processor->prepareToPlay (sampleRate, blockSize);
    const auto looped = playFromTheStart (6 * 48000);
    CHECK_GREATER (rms (looped, 4800, 96000 - 4800), 0.005);
    CHECK_GREATER (rms (looped, 192000 + 4800), 0.005);
    processor->setPlayHead (nullptr);
}

ARTFREQ_TEST (processorSinglePassFollowsTheSongsBars)
{
    // A painted drawing played once at a start bar (Repeat off, At bar) through songs whose time
    // signature changes, from the song start, the host reporting each block's signature and bar
    // start. Returns which quarter notes of the song sound (at 120 BPM: 24000 samples each).
    struct Run { double from; int numerator; };

    const auto soundingQuarters = [] (std::vector<Run> runs, int lengthIndex, int startBar, int quarters)
    {
        auto processor = makeProcessor();
        ArtDocument painted;
        std::fill (painted.paint.begin(), painted.paint.end(), static_cast<uint8_t> (255));
        processor->setDocument (painted);
        setParameter (*processor, params::id::length, static_cast<float> (lengthIndex));
        setParameter (*processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
        setParameter (*processor, params::id::startBar, static_cast<float> (startBar));
        processor->rebuildPlanIfNeeded();

        FakePlayHead playHead;
        processor->setPlayHead (&playHead);

        const auto out = render (*processor, quarters * 24000, blockSize, [&] (int start, int, juce::MidiBuffer&)
        {
            const auto ppq = static_cast<double> (start) / 24000.0;
            playHead.set (true, 120.0, ppq, ppq * 0.5);

            // Walk the bars to the one the block starts in.
            auto barStart = 0.0;
            auto numerator = runs.front().numerator;

            for (;;)
            {
                for (const auto& run : runs)
                    if (barStart >= run.from - 1.0e-9)
                        numerator = run.numerator;

                if (ppq < barStart + numerator - 1.0e-9)
                    break;

                barStart += numerator;
            }

            playHead.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { numerator, 4 });
            playHead.info.setPpqPositionOfLastBarStart (barStart);
        });

        processor->setPlayHead (nullptr);
        std::vector<int> sounding;

        for (int q = 0; q < quarters; ++q)
            if (rms (out, static_cast<size_t> (q * 24000 + 600), static_cast<size_t> ((q + 1) * 24000 - 600)) > 1.0e-3)
                sounding.push_back (q);

        return sounding;
    };

    const auto range = [] (int first, int last)
    {
        std::vector<int> quarters;

        for (auto q = first; q <= last; ++q)
            quarters.push_back (q);

        return quarters;
    };

    // 3/4 from bar 9, where the pass starts: "1 bar" sounds from bar 9 for a bar of 3/4, "4 bars"
    // for four.
    CHECK (soundingQuarters ({ { 0.0, 4 }, { 32.0, 3 } }, 2, 9, 52) == range (32, 34));
    CHECK (soundingQuarters ({ { 0.0, 4 }, { 32.0, 3 } }, 4, 9, 52) == range (32, 43));

    // A longer bar after the pass does not play it again; a change inside it does not cut it short.
    CHECK (soundingQuarters ({ { 0.0, 4 }, { 16.0, 7 } }, 2, 3, 30) == range (8, 11));
    CHECK (soundingQuarters ({ { 0.0, 4 }, { 12.0, 3 } }, 4, 2, 26) == range (4, 19));

    // One 2/4 bar at bar 5: nothing sounds in it, and bar 9 is the DAW's bar 9 (quarter note 30).
    CHECK (soundingQuarters ({ { 0.0, 4 }, { 16.0, 2 }, { 18.0, 4 } }, 4, 9, 50) == range (30, 45));
}

ARTFREQ_TEST (processorSinglePassAtATypedTempoStartsOnTheDawsBar)
{
    // The DAW at 90 BPM, ArtFreq at a typed 120: "1 bar" from bar 9 sounds from the DAW's bar 9
    // (21.33 s), for 2 s.
    auto processor = makeProcessor();
    ArtDocument painted;
    std::fill (painted.paint.begin(), painted.paint.end(), static_cast<uint8_t> (255));
    processor->setDocument (painted);
    setParameter (*processor, params::id::length, 2.0f);
    setParameter (*processor, params::id::startAt, static_cast<float> (params::atBarStartIndex));
    setParameter (*processor, params::id::startBar, 9.0f);
    setParameter (*processor, params::id::tempoSource, 1.0f);
    setParameter (*processor, params::id::bpm, 120.0f);
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    processor->setPlayHead (&playHead);
    const auto out = render (*processor, 26 * 48000, blockSize, [&] (int start, int, juce::MidiBuffer&)
    {
        const auto seconds = static_cast<double> (start) / sampleRate;
        playHead.set (true, 90.0, seconds * 1.5, seconds);
    });
    processor->setPlayHead (nullptr);

    const auto barNine = static_cast<size_t> (32.0 / 1.5 * sampleRate);
    CHECK_LESS (peak (out, 0, barNine), 1.0e-6);
    CHECK_GREATER (peak (out, barNine, barNine + 480), 1.0e-4);
    CHECK_GREATER (rms (out, barNine + 4800, barNine + 96000 - 4800), 0.005);
    CHECK_LESS (peak (out, barNine + 96000), 1.0e-6);
    CHECK (processor->getSinglePassStartBar() == 9);
}

ARTFREQ_TEST (processorMidiRepeatLoopsWhileHeld)
{
    // "1 beat" (0.5 s at 120). With Repeat a held note plays on past the end of the drawing;
    // letting go ends it.
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("HELD", Band::mid));
    setParameter (*processor, params::id::trigger, 1.0f);
    setParameter (*processor, params::id::repeat, 1.0f);
    setParameter (*processor, params::id::length, 0.0f);
    processor->rebuildPlanIfNeeded();
    CHECK (processor->getSinglePassStartBar() == 0);

    constexpr int releaseAt = 72000;   // 1.5 s: three times round
    const auto out = render (*processor, 96000, blockSize, [&] (int start, int length, juce::MidiBuffer& midi)
    {
        if (start == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);

        if (releaseAt >= start && releaseAt < start + length)
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), releaseAt - start);
    });

    CHECK_GREATER (rms (out, 30000, 42000), 0.005);   // the second time round
    CHECK_GREATER (rms (out, 54000, 66000), 0.005);   // the third
    CHECK_LESS (peak (out, releaseAt + 480), 1.0e-6);
    CHECK (! processor->isSounding());
}

ARTFREQ_TEST (processorMidiNoteStartsTheDrawing)
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

ARTFREQ_TEST (processorMidiPassEndsOnPanicAndStop)
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

ARTFREQ_TEST (processorPreviewPlaysWhileStopped)
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

ARTFREQ_TEST (processorPreviewFollowsRepeat)
{
    // "1 beat" (0.5 s at 120). Without Repeat the preview plays it once and turns itself off.
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("ONCE", Band::high));
    setParameter (*processor, params::id::length, 0.0f);
    processor->rebuildPlanIfNeeded();

    processor->setPreview (true);
    auto out = render (*processor, 18000);
    CHECK_GREATER (rms (out, 2400, 16000), 0.005);
    CHECK (processor->isPreviewing());

    out = render (*processor, 18000);
    CHECK (! processor->isPreviewing());
    CHECK_LESS (peak (out, 6000), 1.0e-6);

    // Pressed again, it plays again from the top (and only presses that are still on are
    // turned off when a pass ends).
    processor->setPreview (true);
    out = render (*processor, 12000);
    CHECK (processor->isPreviewing());
    CHECK_GREATER (rms (out, 2400), 0.005);
    processor->setPreview (false);
    processor->setPreview (true);   // between two blocks: a new press, from the start
    out = render (*processor, 18000);
    CHECK (processor->isPreviewing());
    CHECK_GREATER (rms (out, 2400, 16000), 0.005);
    render (*processor, 12000);
    CHECK (! processor->isPreviewing());

    // With Repeat it loops until turned off.
    setParameter (*processor, params::id::repeat, 1.0f);
    processor->setPreview (true);
    out = render (*processor, 96000);
    CHECK (processor->isPreviewing());
    CHECK_GREATER (rms (out, 72000), 0.005);
    processor->setPreview (false);
    out = render (*processor, 4800);
    CHECK_LESS (peak (out, 960), 1.0e-6);
}

ARTFREQ_TEST (processorNeverExceedsFullScale)
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

ARTFREQ_TEST (processorLevelScalesTheOutput)
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

ARTFREQ_TEST (processorEmptyDrawingIsSilent)
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

ARTFREQ_TEST (processorRebuildsThePlan)
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

ARTFREQ_TEST (processorUndoRedoAndCoalescing)
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

ARTFREQ_TEST (processorBypassIsSilentAndRecovers)
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

ARTFREQ_TEST (processorFollowsTheHostTimeSignature)
{
    auto processor = makeProcessor();
    processor->setDocument (textDocument ("BARS", Band::mid));
    setParameter (*processor, params::id::length, 2.0f);   // 1 bar
    setParameter (*processor, params::id::repeat, 1.0f);   // looping on the bar lines
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

ARTFREQ_TEST (processorCanvasNumbersTheSongsBars)
{
    // A single Transport pass sits at a bar of the song: the canvas labels that song's bars.
    const auto labelsOf = [] (const std::vector<ui::axes::TimeMark>& marks)
    {
        juce::StringArray labels;

        for (const auto& mark : marks)
            if (mark.label.isNotEmpty())
                labels.add (mark.label);

        return labels;
    };

    CHECK (labelsOf (ui::axes::timeMarks (4, 4, 4, 600.0f, 9)) == juce::StringArray ({ "9", "10", "11", "12" }));
    CHECK (labelsOf (ui::axes::timeMarks (2, 4, 4, 600.0f, 5)) == juce::StringArray ({ "5", "5.2", "5.3", "5.4" }));
    CHECK (labelsOf (ui::axes::timeMarks (2, 3, 4, 600.0f, 5)) == juce::StringArray ({ "5", "5.2", "5.3" }));

    // Under a bar: the beats of that bar it covers (in 6/8, a quarter note is two eighths).
    CHECK (labelsOf (ui::axes::timeMarks (1, 4, 4, 600.0f, 3)) == juce::StringArray ({ "3", "3.2" }));
    CHECK (labelsOf (ui::axes::timeMarks (1, 6, 8, 600.0f, 3)) == juce::StringArray ({ "3", "3.3" }));
    CHECK (labelsOf (ui::axes::timeMarks (0, 4, 4, 600.0f, 3)) == juce::StringArray ({ "3" }));

    // Crowded: every other bar, still the song's.
    const auto many = labelsOf (ui::axes::timeMarks (7, 4, 4, 600.0f, 990));
    CHECK (many.size() > 4 && many[0] == "990" && many[1] == "992");

    // Without one (Repeat, MIDI) the drawing's own bars, as before.
    CHECK (labelsOf (ui::axes::timeMarks (4, 4, 4, 600.0f, 0)) == labelsOf (ui::axes::timeMarks (4, 4, 4, 600.0f)));
    CHECK (labelsOf (ui::axes::timeMarks (4, 4, 4, 600.0f)) == juce::StringArray ({ "1", "2", "3", "4" }));
}

ARTFREQ_TEST (processorLargerBlocksThanAnnounced)
{
    auto processor = std::make_unique<ArtFreqProcessor>();
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

//==============================================================================
// Licensing: until this computer is activated the instrument is silent and ignores MIDI and
// Preview; activated (here or in another instance) it fades in, deactivated it fades out.

namespace
{

/** Plays the processor with the DAW's transport running from the song's start, `blockSize` at a
    time, calling `beforeBlock` with each block's first sample (where a test locks or activates). */
std::vector<float> playTransport (ArtFreqProcessor& processor, FakePlayHead& playHead, int64_t& played, int numSamples,
                                  const std::function<void (int64_t)>& beforeBlock = {},
                                  const std::function<void (int64_t, juce::MidiBuffer&)>& midiFor = {})
{
    processor.setPlayHead (&playHead);

    const auto out = render (processor, numSamples, 256, [&] (int start, int, juce::MidiBuffer& midi)
    {
        const auto at = played + start;

        if (beforeBlock != nullptr)
            beforeBlock (at);

        if (midiFor != nullptr)
            midiFor (at, midi);

        const auto seconds = static_cast<double> (at) / sampleRate;
        playHead.set (true, 120.0, seconds * 2.0, seconds);
    });

    played += numSamples;
    processor.setPlayHead (nullptr);
    return out;
}

} // namespace

ARTFREQ_TEST (processorLockedInstrumentIsSilentUntilActivated)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const licensetest::ScopedLockedLicensing licensing;

    auto processor = makeProcessor();
    CHECK (! processor->isLicensed());

    processor->setDocument (textDocument ("LOCKED"));
    setParameter (*processor, params::id::repeat, 1.0f);   // loops while the DAW plays
    processor->rebuildPlanIfNeeded();

    // Preview is refused, MIDI notes and the transport play nothing: every sample is zero.
    processor->setPreview (true);
    CHECK (! processor->isPreviewing());

    FakePlayHead playHead;
    int64_t played = 0;
    const auto locked = playTransport (*processor, playHead, played, 48000, {}, [] (int64_t at, juce::MidiBuffer& midi)
    {
        if (at % 4800 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    });
    CHECK (juce::exactlyEqual (peak (locked), 0.0f));
    CHECK (! processor->isSounding());

    // MIDI as the trigger, notes held: still nothing.
    setParameter (*processor, params::id::trigger, 1.0f);
    juce::MidiBuffer note;
    note.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    juce::AudioBuffer<float> buffer (2, blockSize);
    processor->processBlock (buffer, note);
    CHECK (juce::exactlyEqual (buffer.getMagnitude (0, blockSize), 0.0f) && note.isEmpty());
    setParameter (*processor, params::id::trigger, 0.0f);

    // Activated (as by another instance): it plays, fading in from silence.
    CHECK (licensing.activatePro());
    processor->getLicense().reload();
    CHECK (processor->isLicensed());

    const auto unlocked = playTransport (*processor, playHead, played, 48000);
    CHECK_LESS (std::abs (unlocked[0]), 1.0e-6f);
    CHECK_LESS (peak (unlocked, 0, 48), 0.25f * peak (unlocked, 9600));
    CHECK_GREATER (rms (unlocked, 9600), 0.005);
    CHECK (processor->isSounding());

    // And Preview works now.
    processor->setPreview (true);
    CHECK (processor->isPreviewing());

    // Locked again while nothing sounds (the DAW stopped, Preview off): silent at once, so a note
    // in the very next block, inside the time a fade would take, plays nothing either.
    processor->setPreview (false);
    setParameter (*processor, params::id::trigger, 1.0f);

    for (int i = 0; i < 40 && processor->isSounding(); ++i)
    {
        juce::MidiBuffer none;
        processor->processBlock (buffer, none);
    }

    CHECK (! processor->isSounding());
    CHECK (licensing.deactivate());
    processor->getLicense().reload();
    CHECK (! processor->isLicensed());

    juce::MidiBuffer lateNote;
    lateNote.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    processor->processBlock (buffer, lateNote);
    CHECK (juce::exactlyEqual (buffer.getMagnitude (0, blockSize), 0.0f));
    CHECK (! processor->isSounding());
}

ARTFREQ_TEST (processorLockingAndActivatingDoNotClick)
{
    // Playing with the DAW (Repeat on), deactivated in the middle of a block stream and activated
    // again: the sound fades out over 10 ms and is then exactly silent; activated, it fades in. At
    // no point does the output step more than the drawing itself does.
    const juce::ScopedJuceInitialiser_GUI gui;
    const licensetest::ScopedLockedLicensing licensing;
    CHECK (licensing.activatePro());

    auto processor = makeProcessor();
    CHECK (processor->isLicensed());

    processor->setDocument (textDocument ("FADE", Band::low));
    setParameter (*processor, params::id::repeat, 1.0f);
    processor->rebuildPlanIfNeeded();

    FakePlayHead playHead;
    int64_t played = 0;
    const auto steady = playTransport (*processor, playHead, played, 48000);
    const auto steadyStep = largestStep (steady, 4800, steady.size());
    CHECK_GREATER (rms (steady, 4800), 0.005);

    // Locked at 1 s + 2560 samples (a block's start).
    constexpr int64_t lockAt = 48000 + 2560;
    const auto fade = static_cast<size_t> (std::ceil (ArtFreqProcessor::licenseFadeSeconds * sampleRate));
    const auto lockedPart = playTransport (*processor, playHead, played, 48000, [&] (int64_t at)
    {
        if (at == lockAt)
        {
            CHECK (licensing.deactivate());
            processor->getLicense().reload();
        }
    });

    const auto lockIndex = static_cast<size_t> (lockAt - 48000);
    CHECK (! processor->isLicensed());
    CHECK_GREATER (peak (lockedPart, lockIndex, lockIndex + 48), 0.1f * peak (lockedPart, 0, lockIndex));   // a fade, not a cut
    CHECK_LESS (largestStep (lockedPart, 1, lockedPart.size()), 1.5f * steadyStep + 1.0e-6f);
    CHECK (juce::exactlyEqual (peak (lockedPart, lockIndex + fade + 1), 0.0f));
    CHECK (! processor->isSounding());

    // Activated again at 2 s + 5120: back in with a fade.
    constexpr int64_t unlockAt = 2 * 48000 + 5120;
    const auto back = playTransport (*processor, playHead, played, 48000, [&] (int64_t at)
    {
        if (at == unlockAt)
        {
            CHECK (licensing.activatePro());
            processor->getLicense().reload();
        }
    });

    const auto unlockIndex = static_cast<size_t> (unlockAt - 2 * 48000);
    CHECK (juce::exactlyEqual (peak (back, 0, unlockIndex), 0.0f));
    CHECK_LESS (std::abs (back[unlockIndex]), 1.0e-4f);
    CHECK_LESS (largestStep (back, unlockIndex, back.size()), 1.5f * steadyStep + 1.0e-6f);
    CHECK_GREATER (rms (back, unlockIndex + 4800), 0.005);
}
