#include "PluginProcessor.h"

#include "dsp/WavExporter.h"
#include "ui/Editor.h"

#include <cmath>

namespace freqart
{

namespace
{

constexpr int stateVersion = 1;
constexpr int timerHz = 25;
constexpr juce::uint32 saveQuietMs = 300;  // serialise the document once edits pause this long
constexpr int minChunkCapacity = 4096;

// Parameters that change the plan (the others are read by the audio thread directly).
const char* const planParameterIds[] { params::id::density, params::id::contrast, params::id::scale,
                                       params::id::rangeLow, params::id::rangeHigh, params::id::slope };

bool sameSettings (const PlanSettings& a, const PlanSettings& b) noexcept
{
    return a.scale == b.scale
        && a.density == b.density
        && juce::exactlyEqual (a.contrastDb, b.contrastDb)
        && juce::exactlyEqual (a.slopeDbPerOctave, b.slopeDbPerOctave)
        && juce::exactlyEqual (a.minLineSpacingHz, b.minLineSpacingHz)
        && juce::exactlyEqual (a.maxFrequencyHz, b.maxFrequencyHz)
        && a.lowMethod == b.lowMethod;
}

uint64_t versionOf (const ArtPlan* plan) noexcept
{
    return plan != nullptr ? plan->version : 0;
}

double quarterNotesPerBar (juce::AudioPlayHead::TimeSignature signature) noexcept
{
    return static_cast<double> (signature.numerator) * 4.0 / static_cast<double> (signature.denominator);
}

/** True on the message thread, and also when there is no message thread at all (a console
    app driving the processor directly), where every call comes from the one thread there is. */
bool onMessageThread() noexcept
{
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    return messageManager == nullptr || messageManager->isThisTheMessageThread();
}

} // namespace

FreqArtProcessor::FreqArtProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FreqArtParameters", params::createLayout()),
      history (40)
{
    levelParam       = parameters.getRawParameterValue (params::id::level);
    lengthParam      = parameters.getRawParameterValue (params::id::length);
    triggerParam     = parameters.getRawParameterValue (params::id::trigger);
    tempoSourceParam = parameters.getRawParameterValue (params::id::tempoSource);
    bpmParam         = parameters.getRawParameterValue (params::id::bpm);
    densityParam     = parameters.getRawParameterValue (params::id::density);
    contrastParam    = parameters.getRawParameterValue (params::id::contrast);
    scaleParam       = parameters.getRawParameterValue (params::id::scale);
    rangeLowParam    = parameters.getRawParameterValue (params::id::rangeLow);
    rangeHighParam   = parameters.getRawParameterValue (params::id::rangeHigh);
    slopeParam       = parameters.getRawParameterValue (params::id::slope);

    jassert (levelParam != nullptr && lengthParam != nullptr && triggerParam != nullptr
             && tempoSourceParam != nullptr && bpmParam != nullptr && densityParam != nullptr
             && contrastParam != nullptr && scaleParam != nullptr && rangeLowParam != nullptr
             && rangeHighParam != nullptr && slopeParam != nullptr);

    for (const auto* id : planParameterIds)
        parameters.addParameterListener (id, this);

    effectiveTempo.store (static_cast<double> (bpmParam->load()));

    // A fresh instance shows (and plays) something straight away.
    document = ArtDocument::createDefault();
    documentDirty = true;
    rebuildPlanIfNeeded();
    refreshSavedDocument();

    // The engine must hold a plan before the first block, even if the host never runs the
    // message loop in between.
    engine.prepare (currentSampleRate.load(), 512);
    if (auto* plan = planExchange.acquire())
        engine.setPlan (plan);
    planExchange.reportInUse (versionOf (engine.getPlan()), versionOf (engine.getFadingPlan()));

    startTimerHz (timerHz);
}

FreqArtProcessor::~FreqArtProcessor()
{
    stopTimer();
    cancelPendingUpdate();

    for (const auto* id : planParameterIds)
        parameters.removeParameterListener (id, this);
}

// ---- playback ------------------------------------------------------------------------------

void FreqArtProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const auto rate = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0;
    currentSampleRate.store (rate);

    monoCapacity = juce::jmax (minChunkCapacity, maximumExpectedSamplesPerBlock);
    mono.allocate (static_cast<size_t> (monoCapacity), true);
    chunkMidi.ensureSize (4096);

    // prepare() forgets the oscillators; re-adopting the plan sets them up again.
    const auto* plan = engine.getQueuedPlan() != nullptr ? engine.getQueuedPlan() : engine.getPlan();
    engine.prepare (rate, monoCapacity);
    engine.setPlan (nullptr);
    engine.setPlan (plan);

    if (auto* newer = planExchange.acquire())
        engine.setPlan (newer);

    planExchange.reportInUse (versionOf (engine.getPlan()), versionOf (engine.getFadingPlan()));

    tracker.prepare (rate);
    tracker.reset();

    outputGain.reset (rate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelParam->load()));

    if (! juce::exactlyEqual (analyzer.getSampleRate(), rate))
        analyzer.prepare (rate);
}

void FreqArtProcessor::releaseResources()
{
    engine.reset();
    tracker.reset();
}

bool FreqArtProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // An instrument: no audio in, mono or stereo out.
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

TrackerSettings FreqArtProcessor::currentTrackerSettings() const noexcept
{
    TrackerSettings settings;
    settings.mode = params::triggerForIndex (juce::roundToInt (triggerParam->load()));
    settings.useHostTempo = juce::roundToInt (tempoSourceParam->load()) == 0;
    settings.manualBpm = static_cast<double> (bpmParam->load());
    settings.quarterNotesPerBar = quarterNotesPerBar (timeSignature.load());
    settings.lengthQuarterNotes = params::lengthInQuarterNotes (juce::roundToInt (lengthParam->load()),
                                                                settings.quarterNotesPerBar);
    return settings;
}

void FreqArtProcessor::renderChunk (float* out, int numSamples, const HostTiming& timing,
                                    const juce::MidiBuffer& midi, const TrackerSettings& settings) noexcept
{
    const auto numSegments = tracker.process (timing, midi, numSamples, settings, segments);
    engine.render (out, numSamples, segments, numSegments);
}

void FreqArtProcessor::writeOutput (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    // The level and the safety clipper on the mono render, then every channel and the analyser.
    auto* out = mono.get();

    for (int i = 0; i < numSamples; ++i)
        out[i] = softClip (out[i] * outputGain.getNextValue());

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom (channel, start, out, numSamples);

    if (analyzer.isActive())
        analyzer.push (out, numSamples);
}

void FreqArtProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (numSamples <= 0 || monoCapacity <= 0)
    {
        midi.clear();
        return;
    }

    // Pick up the newest drawing (the engine crossfades to it) and tell the exchange which
    // plans are still being read, so older ones can be freed on the message thread.
    if (auto* plan = planExchange.acquire())
        engine.setPlan (plan);

    // Host timing and time signature.
    HostTiming timing;

    if (auto* hostPlayHead = getPlayHead())
    {
        const auto position = hostPlayHead->getPosition();
        timing = makeHostTiming (position);

        if (position.hasValue())
        {
            if (const auto signature = position->getTimeSignature(); signature.hasValue()
                && signature->numerator > 0 && signature->denominator > 0)
            {
                timeSignature.store (*signature);
            }
        }
    }

    tracker.setPreview (previewRequested.load());
    const auto settings = currentTrackerSettings();
    const auto sampleRate = currentSampleRate.load();

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (levelParam->load(), -100.0f));

    // Hosts occasionally send more samples than they announced: work in chunks then, moving
    // the host's clock along with each chunk so the tracker sees a continuous timeline.
    for (int start = 0; start < numSamples;)
    {
        const auto length = juce::jmin (monoCapacity, numSamples - start);
        auto* out = mono.get();

        if (start == 0 && length == numSamples)
        {
            renderChunk (out, length, timing, midi, settings);
        }
        else
        {
            chunkMidi.clear();
            chunkMidi.addEvents (midi, start, length, -start);

            auto chunkTiming = timing;
            const auto elapsed = static_cast<double> (start) / sampleRate;

            if (chunkTiming.timeInSeconds.has_value())
                *chunkTiming.timeInSeconds += elapsed;

            if (chunkTiming.ppqPosition.has_value() && chunkTiming.bpm.has_value())
                *chunkTiming.ppqPosition += elapsed * *chunkTiming.bpm / 60.0;

            renderChunk (out, length, chunkTiming, chunkMidi, settings);
        }

        writeOutput (buffer, start, length);
        start += length;
    }

    planExchange.reportInUse (versionOf (engine.getPlan()), versionOf (engine.getFadingPlan()));

    playPosition.store (engine.getFraction());
    sounding.store (engine.isSounding());
    effectiveTempo.store (tracker.getTempo());

    // An instrument that makes no MIDI: nothing goes out.
    midi.clear();
}

void FreqArtProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    buffer.clear();
    midi.clear();

    if (auto* plan = planExchange.acquire())
        engine.setPlan (plan);

    // Going into bypass while the drawing sounds: the gate closes and its fade-out is heard
    // first (over this block and, for short blocks, the next ones), so the sound does not stop
    // mid-waveform.
    for (int start = 0; start < numSamples && monoCapacity > 0 && engine.isSounding();)
    {
        const auto length = juce::jmin (monoCapacity, numSamples - start);

        PlaySegment closing;
        closing.length = length;
        closing.fraction = engine.getFraction();
        engine.render (mono.get(), length, &closing, 1);

        writeOutput (buffer, start, length);
        start += length;
    }

    // Silent: coming back from bypass starts cleanly (with a fade-in) rather than mid-sound.
    if (! engine.isSounding())
    {
        engine.reset();
        tracker.reset();
    }

    planExchange.reportInUse (versionOf (engine.getPlan()), versionOf (engine.getFadingPlan()));

    playPosition.store (engine.getFraction());
    sounding.store (engine.isSounding());
}

void FreqArtProcessor::setPreview (bool shouldPreview) noexcept
{
    previewRequested.store (shouldPreview);
}

double FreqArtProcessor::getEffectiveTempo() const noexcept
{
    if (juce::roundToInt (tempoSourceParam->load()) != 0)
        return static_cast<double> (bpmParam->load());

    return effectiveTempo.load();
}

double FreqArtProcessor::getLengthSeconds() const noexcept
{
    const auto lengthQn = params::lengthInQuarterNotes (juce::roundToInt (lengthParam->load()),
                                                        quarterNotesPerBar (timeSignature.load()));
    const auto tempo = juce::jlimit (20.0, 400.0, getEffectiveTempo());
    return lengthQn * 60.0 / tempo;
}

float FreqArtProcessor::getOutputGain() const noexcept
{
    return juce::Decibels::decibelsToGain (levelParam->load(), -100.0f);
}

// ---- the plan ------------------------------------------------------------------------------

FrequencyScale FreqArtProcessor::getScale() const
{
    FrequencyScale scale;
    scale.type = params::scaleForIndex (juce::roundToInt (scaleParam->load()));
    scale.minHz = static_cast<double> (rangeLowParam->load());
    scale.maxHz = static_cast<double> (rangeHighParam->load());
    return scale.sanitised();
}

PlanSettings FreqArtProcessor::getPlanSettings() const
{
    PlanSettings settings;
    settings.scale = getScale();
    settings.density = juce::jlimit (16, 1024, juce::roundToInt (densityParam->load()));
    settings.contrastDb = contrastParam->load();
    settings.slopeDbPerOctave = params::slopeForIndex (juce::roundToInt (slopeParam->load()));
    // Lines above the top of the hearing range only cost CPU; the engine also keeps every line
    // below Nyquist.
    settings.maxFrequencyHz = 22000.0f;
    return settings;
}

void FreqArtProcessor::rebuildPlanIfNeeded()
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto parametersChanged = parametersDirty.exchange (false);
    const auto havePlan = planExchange.latest() != nullptr;

    if (! documentDirty && ! parametersChanged && havePlan)
        return;

    const auto settings = getPlanSettings();
    const auto scaleChanged = settings.scale != lastBuiltSettings.scale;

    if (havePlan && ! documentDirty && sameSettings (settings, lastBuiltSettings))
        return;

    // The canvas only depends on the document and the scale.
    if (documentDirty || scaleChanged || ! havePlan)
        compositor.render (document, settings.scale, canvas);

    std::shared_ptr<ArtPlan> plan (buildPlan (canvas, settings).release());
    planExchange.publish (plan);

    lastBuiltSettings = settings;
    documentDirty = false;
    sendChangeMessage();
}

void FreqArtProcessor::parameterChanged (const juce::String&, float)
{
    // May be called on any thread: just note it; the timer rebuilds.
    parametersDirty.store (true);
}

void FreqArtProcessor::timerCallback()
{
    rebuildPlanIfNeeded();
    planExchange.collectGarbage();

    if (savedDocumentStale && juce::Time::getMillisecondCounter() - lastEditTime >= saveQuietMs)
        refreshSavedDocument();
}

// ---- the drawing ---------------------------------------------------------------------------

void FreqArtProcessor::documentChanged()
{
    documentDirty = true;
    savedDocumentStale = true;
    lastEditTime = juce::Time::getMillisecondCounter();
    sendChangeMessage();
    markProjectChanged();
}

void FreqArtProcessor::markProjectChanged()
{
    // The drawing and the window size are saved state but not parameters, so the host cannot
    // see them change: without this a VST3 host would not offer to save the project, and the
    // change would be lost when it closes. (The wrapper merges these calls asynchronously, so
    // one per brush dab or key press is cheap.)
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void FreqArtProcessor::setEditorSize (int width, int height)
{
    const auto oldWidth = editorWidth.exchange (width);
    const auto oldHeight = editorHeight.exchange (height);

    // 0 x 0 is "never resized", which shows at the default size: only a real change counts.
    const auto shown = [] (int w, int h)
    {
        return w > 0 && h > 0 ? juce::Point<int> (w, h)
                              : juce::Point<int> (FreqArtEditor::baseWidth, FreqArtEditor::baseHeight);
    };

    if (shown (oldWidth, oldHeight) != shown (width, height))
        markProjectChanged();
}

void FreqArtProcessor::editDocument (const std::function<void (ArtDocument&)>& edit, const juce::String& coalesceKey)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (edit == nullptr)
        return;

    if (coalesceKey.isEmpty() || coalesceKey != lastCoalesceKey)
        history.push (document);

    lastCoalesceKey = coalesceKey;
    edit (document);
    documentChanged();
}

void FreqArtProcessor::setDocument (const ArtDocument& newDocument)
{
    JUCE_ASSERT_MESSAGE_THREAD

    history.push (document);
    lastCoalesceKey.clear();
    document = newDocument;
    documentChanged();
}

bool FreqArtProcessor::undo()
{
    lastCoalesceKey.clear();

    if (! history.undo (document))
        return false;

    documentChanged();
    return true;
}

bool FreqArtProcessor::redo()
{
    lastCoalesceKey.clear();

    if (! history.redo (document))
        return false;

    documentChanged();
    return true;
}

// ---- state ---------------------------------------------------------------------------------

void FreqArtProcessor::refreshSavedDocument()
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto tree = document.toValueTree();

    const juce::ScopedLock lock (savedStateLock);
    savedDocument = tree;
    savedDocumentStale = false;
}

void FreqArtProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Hosts may save from any thread. On the message thread the document is serialised now;
    // elsewhere the copy made when the last edit settled is used.
    if (onMessageThread() && savedDocumentStale)
        refreshSavedDocument();

    juce::XmlElement root ("FreqArtState");
    root.setAttribute ("version", stateVersion);
    root.setAttribute ("pluginVersion", FREQART_VERSION_STRING);
    root.setAttribute ("editorWidth", editorWidth.load());
    root.setAttribute ("editorHeight", editorHeight.load());

    if (auto parametersXml = parameters.copyState().createXml())
        root.addChildElement (parametersXml.release());

    juce::ValueTree documentTree;
    {
        const juce::ScopedLock lock (savedStateLock);
        documentTree = savedDocument;
    }

    if (documentTree.isValid())
        if (auto documentXml = documentTree.createXml())
            root.addChildElement (documentXml.release());

    copyXmlToBinary (root, destData);
}

void FreqArtProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto root = getXmlFromBinary (data, sizeInBytes);

    if (root == nullptr || ! root->hasTagName ("FreqArtState"))
        return;

    editorWidth.store (juce::jlimit (0, 8000, root->getIntAttribute ("editorWidth", 0)));
    editorHeight.store (juce::jlimit (0, 8000, root->getIntAttribute ("editorHeight", 0)));

    if (auto* parametersXml = root->getChildByName (parameters.state.getType().toString()))
        parameters.replaceState (juce::ValueTree::fromXml (*parametersXml));

    parametersDirty.store (true);

    juce::ValueTree documentTree;

    if (auto* documentXml = root->getChildByName ("FreqArtDocument"))
        documentTree = juce::ValueTree::fromXml (*documentXml);

    // A state without a drawing loads as an empty drawing, not as whatever was there before.
    if (! documentTree.isValid())
        documentTree = ArtDocument().toValueTree();

    if (onMessageThread())
    {
        applyDocumentState (documentTree);
    }
    else
    {
        {
            const juce::ScopedLock lock (savedStateLock);
            pendingDocument = documentTree;
            // What a save right now should return: the state just loaded.
            savedDocument = documentTree;
        }

        triggerAsyncUpdate();
    }
}

void FreqArtProcessor::handleAsyncUpdate()
{
    juce::ValueTree documentTree;
    {
        const juce::ScopedLock lock (savedStateLock);
        documentTree = std::exchange (pendingDocument, juce::ValueTree());
    }

    if (documentTree.isValid())
        applyDocumentState (documentTree);
}

void FreqArtProcessor::applyDocumentState (const juce::ValueTree& documentTree)
{
    JUCE_ASSERT_MESSAGE_THREAD

    document = ArtDocument::fromValueTree (documentTree);
    history.clear();
    lastCoalesceKey.clear();

    documentDirty = true;
    rebuildPlanIfNeeded();

    // A save straight after a load returns the drawing as loaded, so there is nothing to
    // serialise again (and its pictures keep the text they were loaded from). A load is not a
    // user edit, so the host is not told the project changed.
    {
        const juce::ScopedLock lock (savedStateLock);
        savedDocument = documentTree;
        savedDocumentStale = false;
    }

    sendChangeMessage();
}

juce::AudioProcessorEditor* FreqArtProcessor::createEditor()
{
    return new FreqArtEditor (*this);
}

} // namespace freqart

#if ! FREQART_NO_PLUGIN_CLIENT
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new freqart::FreqArtProcessor();
}
#endif
