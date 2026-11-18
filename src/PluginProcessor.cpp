#include "PluginProcessor.h"

#include "dsp/WavExporter.h"
#include "ui/Editor.h"

#include <cmath>

namespace artfreq
{

namespace
{

constexpr int stateVersion = 1;
constexpr int timerHz = 25;
constexpr juce::uint32 saveQuietMs = 300;  // serialise the document once edits pause this long
constexpr int minChunkCapacity = 4096;

// Parameters that change the plan (the others are read by the audio thread directly).
const char* const planParameterIds[] { params::id::density, params::id::contrast, params::id::lowEnd,
                                       params::id::scale, params::id::rangeLow, params::id::rangeHigh,
                                       params::id::slope };

bool sameSettings (const PlanSettings& a, const PlanSettings& b) noexcept
{
    return a.scale == b.scale
        && a.density == b.density
        && juce::exactlyEqual (a.contrastDb, b.contrastDb)
        && juce::exactlyEqual (a.slopeDbPerOctave, b.slopeDbPerOctave)
        && juce::exactlyEqual (a.minLineSpacingHz, b.minLineSpacingHz)
        && juce::exactlyEqual (a.maxFrequencyHz, b.maxFrequencyHz)
        && a.lowEnd == b.lowEnd;
}

uint64_t versionOf (const ArtPlan* plan) noexcept
{
    return plan != nullptr ? plan->version : 0;
}

uint64_t versionOf (const MaskPlanSet* masks) noexcept
{
    return masks != nullptr ? masks->version : 0;
}

/** Where the drawing is at the end of a stretch the segments cover, and whether it is playing
    there (the effect's playhead, which needs no sines to be rendered). */
std::pair<double, bool> positionAfter (const PlaySegment* segments, int numSegments, int numSamples) noexcept
{
    if (numSegments <= 0)
        return { 0.0, false };

    const auto& last = segments[numSegments - 1];
    const auto end = last.fraction + static_cast<double> (numSamples - last.start) * last.fractionPerSample;
    const auto position = last.once ? juce::jlimit (0.0, 1.0, end) : end - std::floor (end);
    return { std::isfinite (position) ? position : 0.0, last.playing };
}

/** The input counts as sounding from this level on (-60 dBFS): quieter, nothing of it shows on
    an analyser's spectrogram (white noise at -60 dBFS lies near -93 dB in each bin of 4096
    points), so carving it would show nothing either. Digital silence, dither and a quiet noise
    floor all lie below it. */
constexpr float soundThreshold = 0.001f;

/** The first sample of a stretch of the input that sounds (`numSamples`: none does). */
int firstSoundingSample (const juce::AudioBuffer<float>& buffer, int numChannels, int start, int numSamples) noexcept
{
    auto first = numSamples;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* samples = buffer.getReadPointer (channel, start);

        for (int i = 0; i < first; ++i)
        {
            if (std::abs (samples[i]) >= soundThreshold)
            {
                first = i;
                break;
            }
        }
    }

    return first;
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

juce::AudioProcessor::BusesProperties ArtFreqProcessor::busesFor (Variant variant)
{
    // The instrument makes its sound from nothing; the effect works on the audio it is given.
    if (variant == Variant::effect)
        return BusesProperties()
            .withInput ("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

ArtFreqProcessor::ArtFreqProcessor (Variant variantToUse)
    : juce::AudioProcessor (busesFor (variantToUse)),
      variant (variantToUse),
      license (std::make_unique<anymeans::license::LicenseManager> (variantToUse == Variant::effect
                                                                        ? anymeans::license::artFreqEffectLicensing()
                                                                        : anymeans::license::artFreqInstrumentLicensing())),
      parameters (*this, nullptr, "ArtFreqParameters", params::createLayout (variantToUse == Variant::effect)),
      history (40)
{
    levelParam       = parameters.getRawParameterValue (params::id::level);
    lengthParam      = parameters.getRawParameterValue (params::id::length);
    triggerParam     = parameters.getRawParameterValue (params::id::trigger);
    repeatParam      = parameters.getRawParameterValue (params::id::repeat);
    startAtParam     = parameters.getRawParameterValue (params::id::startAt);
    startBarParam    = parameters.getRawParameterValue (params::id::startBar);
    tempoSourceParam = parameters.getRawParameterValue (params::id::tempoSource);
    bpmParam         = parameters.getRawParameterValue (params::id::bpm);
    densityParam     = parameters.getRawParameterValue (params::id::density);
    contrastParam    = parameters.getRawParameterValue (params::id::contrast);
    lowEndParam      = parameters.getRawParameterValue (params::id::lowEnd);
    scaleParam       = parameters.getRawParameterValue (params::id::scale);
    rangeLowParam    = parameters.getRawParameterValue (params::id::rangeLow);
    rangeHighParam   = parameters.getRawParameterValue (params::id::rangeHigh);
    slopeParam       = parameters.getRawParameterValue (params::id::slope);

    jassert (levelParam != nullptr && lengthParam != nullptr && triggerParam != nullptr
             && repeatParam != nullptr && startAtParam != nullptr && startBarParam != nullptr
             && tempoSourceParam != nullptr
             && bpmParam != nullptr && densityParam != nullptr && contrastParam != nullptr
             && lowEndParam != nullptr && scaleParam != nullptr && rangeLowParam != nullptr
             && rangeHighParam != nullptr && slopeParam != nullptr);

    if (isEffect())
    {
        modeParam = parameters.getRawParameterValue (params::id::mode);
        depthParam = parameters.getRawParameterValue (params::id::depth);
        jassert (modeParam != nullptr && depthParam != nullptr);
    }

    for (const auto* id : planParameterIds)
        parameters.addParameterListener (id, this);

    effectiveTempo.store (static_cast<double> (bpmParam->load()));

    licenseGain.reset (currentSampleRate.load(), licenseFadeSeconds);
    licenseGain.setCurrentAndTargetValue (isLicensed() ? 1.0f : 0.0f);

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

    // The effect likewise holds its masks, and reports its latency (at 48 kHz until the host
    // prepares it) from the start, for hosts that ask before they prepare.
    if (isEffect())
    {
        carver.prepare (currentSampleRate.load(), 512, 2);

        if (auto* masks = maskExchange.acquire())
            currentMasks = masks;

        maskExchange.reportInUse (versionOf (currentMasks), 0);
        setLatencySamples (carver.getLatencySamples());
    }

    startTimerHz (timerHz);
}

ArtFreqProcessor::~ArtFreqProcessor()
{
    stopTimer();
    cancelPendingUpdate();

    for (const auto* id : planParameterIds)
        parameters.removeParameterListener (id, this);
}

// ---- playback ------------------------------------------------------------------------------

void ArtFreqProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
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

    licenseGain.reset (rate, licenseFadeSeconds);
    licenseGain.setCurrentAndTargetValue (isLicensed() ? 1.0f : 0.0f);
    lockedSilent = false;

    if (! juce::exactlyEqual (analyzer.getSampleRate(), rate))
        analyzer.prepare (rate);

    if (isEffect())
    {
        // One frame of the carver late, in every mode (and in bypass): the host compensates
        // for it, and switching modes never moves the audio. The sines are delayed as much, so
        // they land in the holes carved for them.
        carver.prepare (rate, monoCapacity, 2);
        sineDelayLength = carver.getLatencySamples();
        sineDelay.allocate (static_cast<size_t> (sineDelayLength), true);
        sineDelayPosition = 0;
        sineDelaySilence = sineDelayLength;
        setLatencySamples (sineDelayLength);

        drawGain.reset (rate, 0.02);
        drawGain.setCurrentAndTargetValue (isLicensed() && playsSines (getEffectMode()) ? 1.0f : 0.0f);
        sinesRunning = false;

        // The carver starts afresh: it reads no older masks.
        olderMasks = nullptr;

        if (auto* masks = maskExchange.acquire())
            currentMasks = masks;

        maskExchange.reportInUse (versionOf (currentMasks), 0);
    }
}

void ArtFreqProcessor::releaseResources()
{
    engine.reset();
    tracker.reset();

    if (isEffect())
        carver.reset();
}

void ArtFreqProcessor::reset()
{
    // A host's flush: VST3 setProcessing (false), on stop, on a seek, or while it suspends a
    // silent effect. The effect drops the audio it holds (the song in the carver, the sines in
    // their delay), so the next playback does not start with up to a latency of what played
    // before; from here the output is the new input one latency late, as after prepareToPlay.
    // The instrument holds no audio (its gate fades it out when the DAW stops).
    if (! isEffect())
        return;

    // Hosts may call this from another thread, never during a block: the wrapper processes
    // under the callback lock. Only clearing here: no allocation.
    const juce::ScopedLock lock (getCallbackLock());

    carver.reset();
    olderMasks = nullptr;
    engine.reset();
    tracker.reset();
    sinesRunning = false;

    if (sineDelayLength > 0)
        std::fill (sineDelay.get(), sineDelay.get() + sineDelayLength, 0.0f);

    sineDelayPosition = 0;
    sineDelaySilence = sineDelayLength;

    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (levelParam->load(), -100.0f));
    drawGain.setCurrentAndTargetValue (isLicensed() && playsSines (getEffectMode()) ? 1.0f : 0.0f);

    playPosition.store (0.0);
    sounding.store (false);
    waitingForSound.store (false);
}

bool ArtFreqProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto monoOrStereo = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();

    // The effect: as many channels out as in, mono or stereo.
    if (isEffect())
        return monoOrStereo && layouts.getMainInputChannelSet() == out;

    // An instrument: no audio in, mono or stereo out.
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    return monoOrStereo;
}

TrackerSettings ArtFreqProcessor::currentTrackerSettings() const noexcept
{
    TrackerSettings settings;
    settings.mode = params::triggerForIndex (juce::roundToInt (triggerParam->load()));
    settings.repeat = repeatParam->load() >= 0.5f;
    settings.startAt = params::startAtForIndex (juce::roundToInt (startAtParam->load()));
    settings.startBar = juce::roundToInt (startBarParam->load());
    settings.useHostTempo = juce::roundToInt (tempoSourceParam->load()) == 0;
    settings.manualBpm = static_cast<double> (bpmParam->load());
    settings.quarterNotesPerBar = quarterNotesPerBar (timeSignature.load());
    settings.lengthQuarterNotes = params::lengthInQuarterNotes (juce::roundToInt (lengthParam->load()),
                                                                settings.quarterNotesPerBar);

    // Carving a song (Carve, Stencil) shows nothing over silence: a pass on play begins where
    // the track's audio does. The Draw modes play their sines from the moment the DAW starts.
    settings.waitForSound = isEffect() && ! playsSines (getEffectMode());
    return settings;
}

void ArtFreqProcessor::renderChunk (float* out, int numSamples, const HostTiming& timing,
                                    const juce::MidiBuffer& midi, const TrackerSettings& settings) noexcept
{
    const auto numSegments = tracker.process (timing, midi, numSamples, settings, segments);
    engine.render (out, numSamples, segments, numSegments);
}

void ArtFreqProcessor::writeOutput (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    // The level and the safety clipper on the mono render, then every channel and the analyser.
    // Locking or activating fades the whole output (after the clipper, so the fade is a fade).
    auto* out = mono.get();

    for (int i = 0; i < numSamples; ++i)
        out[i] = softClip (out[i] * outputGain.getNextValue()) * licenseGain.getNextValue();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom (channel, start, out, numSamples);

    if (analyzer.isActive())
        analyzer.push (out, numSamples);
}

HostTiming ArtFreqProcessor::readHostTiming() noexcept
{
    // Host timing, and the time signature for the editor.
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

            // The DAW's loop, for the editor: how long it is (0: no loop, or none reported).
            auto loop = 0.0;

            if (const auto points = position->getLoopPoints(); position->getIsLooping() && points.hasValue())
                if (const auto span = points->ppqEnd - points->ppqStart; std::isfinite (span) && span > 0.0)
                    loop = span;

            hostLoopQuarterNotes.store (loop);
        }
    }

    return timing;
}

void ArtFreqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Whether this computer is activated: the licence core's atomic flag, lock-free. Activating
    // (in this window, another instance or another AnyMeans plug-in) or deactivating sets it on
    // the message thread, and the next block follows. Locked, Preview is off (setPreview refuses
    // it, and one that was on stops).
    const auto licensed = isLicensed();

    if (! licensed)
        previewToken.store (0);

    // Locked, ArtFreq FX lets the song through untouched: it must never mute a master. The
    // bypass path does exactly that, one latency late as always (so nothing moves when it is
    // activated), after fading out what it was carving or playing; activated, it comes back as
    // from a bypass.
    if (isEffect())
    {
        processEffect (buffer, midi, ! licensed);
        return;
    }

    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (numSamples <= 0 || monoCapacity <= 0)
    {
        midi.clear();
        return;
    }

    // Locked, the instrument fades out over 10 ms (whatever it was playing), then stays silent and
    // ignores MIDI and Preview. Activated, it starts afresh and fades in: a pass begins (or picks
    // up where the song is) as when the plug-in is first put on a playing track.
    licenseGain.setTargetValue (licensed ? 1.0f : 0.0f);

    // (With nothing sounding there is nothing to fade: locked at once, so not even a note that
    // comes in during the fade is played.)
    if (! licensed && (! licenseGain.isSmoothing() || ! engine.isSounding()))
    {
        licenseGain.setCurrentAndTargetValue (0.0f);
        processLocked (buffer, midi);
        return;
    }

    lockedSilent = false;

    // Pick up the newest drawing (the engine crossfades to it) and tell the exchange which
    // plans are still being read, so older ones can be freed on the message thread.
    if (auto* plan = planExchange.acquire())
        engine.setPlan (plan);

    const auto timing = readHostTiming();

    // A new press of Preview since the last block starts it again from the top, even if the
    // one before was still on.
    const auto preview = previewToken.load();

    if (preview != previewTokenInUse)
    {
        tracker.setPreview (false);
        previewTokenInUse = preview;
    }

    tracker.setPreview (preview != 0);
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

    // A preview without Repeat has played its pass: it turns itself off (the button follows),
    // unless Preview was pressed again meanwhile.
    if (preview != 0 && tracker.hasPreviewEnded())
    {
        auto expected = preview;
        previewToken.compare_exchange_strong (expected, 0);
    }

    planExchange.reportInUse (versionOf (engine.getPlan()), versionOf (engine.getFadingPlan()));

    playPosition.store (engine.getFraction());
    sounding.store (engine.isSounding());
    effectiveTempo.store (tracker.getTempo());

    // An instrument that makes no MIDI: nothing goes out.
    midi.clear();
}

void ArtFreqProcessor::processLocked (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    // The instrument, locked and faded out: silence, whatever the transport, MIDI or Preview do.
    buffer.clear();
    midi.clear();

    // New drawings are still taken up, so the exchange can free the old ones (and the engine
    // holds the newest when it is activated).
    auto* plan = planExchange.acquire();

    if (plan != nullptr)
        engine.setPlan (plan);

    // Nothing sounds: the engine lets go of the pass (and any crossfade), so it starts afresh,
    // with a fade-in; the tracker forgets the pass, so one begins when it is activated.
    if (! lockedSilent || plan != nullptr)
        engine.reset();

    if (! lockedSilent)
        tracker.reset();

    lockedSilent = true;

    planExchange.reportInUse (versionOf (engine.getPlan()), versionOf (engine.getFadingPlan()));
    playPosition.store (0.0);
    sounding.store (false);
}

void ArtFreqProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (isEffect())
    {
        processEffect (buffer, midi, true);
        return;
    }

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

// ---- ArtFreq FX ----------------------------------------------------------------------------

void ArtFreqProcessor::processEffect (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed) noexcept
{
    // The audio comes in, the carver carves it in place (one frame late, the latency the host
    // was told), and in the modes that play the drawing its sines are added, as late as the
    // audio they belong to. The tracker's segments drive both, so a carve follows Length,
    // Trigger, Repeat, where the pass starts and tempo exactly as the sines do. In bypass (and in Draw) the
    // carver changes nothing: the audio comes out as late, bit for bit, so a host's bypass never
    // moves it; what was being carved or played fades out first.
    const auto numSamples = buffer.getNumSamples();
    const auto numInputs = getTotalNumInputChannels();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), juce::jmax (1, numInputs), 2);

    // Outputs without an input start silent (the host may leave anything in them).
    for (int channel = numInputs; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0 || monoCapacity <= 0 || sineDelayLength <= 0)
    {
        midi.clear();
        return;
    }

    if (auto* plan = planExchange.acquire())
        engine.setPlan (plan);

    if (auto* masks = maskExchange.acquire())
    {
        // The carver finishes the frame it began with the plan it began with: the set it came
        // from stays alive until then (with every newer one: the exchange keeps all from the
        // oldest version reported).
        if (olderMasks == nullptr)
            olderMasks = currentMasks;

        currentMasks = masks;
    }

    const auto mode = getEffectMode();
    const auto carving = ! bypassed && carvesAudio (mode);

    CarveSettings carve;
    carve.mode = mode == EffectMode::stencil ? CarveMode::stencil : CarveMode::carve;
    carve.depthDb = carving ? juce::jlimit (params::minDepthDb, 0.0f, depthParam->load()) : 0.0f;
    carve.haloBins = mode == EffectMode::carveDraw ? carveDrawHaloBins : 1;
    carve.holdLoudest = mode == EffectMode::carveDraw;

    // Carve + Draw carves its hole a little larger than the drawing: the halo masks.
    if (currentMasks != nullptr)
        carver.setPlan (mode == EffectMode::carveDraw ? currentMasks->halo.get() : currentMasks->exact.get());
    else
        carver.setPlan (nullptr);

    const auto timing = bypassed ? HostTiming() : readHostTiming();
    const auto preview = previewToken.load();

    if (! bypassed)
    {
        if (preview != previewTokenInUse)
        {
            tracker.setPreview (false);
            previewTokenInUse = preview;
        }

        tracker.setPreview (preview != 0);
    }

    const auto settings = currentTrackerSettings();
    const auto sampleRate = currentSampleRate.load();

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (levelParam->load(), -100.0f));
    drawGain.setTargetValue (! bypassed && playsSines (mode) ? 1.0f : 0.0f);

    float* channels[2] {};
    auto playing = false;

    for (int start = 0; start < numSamples;)
    {
        const auto length = juce::jmin (monoCapacity, numSamples - start);
        int numSegments = 1;

        if (bypassed)
        {
            // The gate closes: whatever sounded fades out where it was.
            segments[0] = PlaySegment();
            segments[0].length = length;
            segments[0].fraction = engine.getFraction();
        }
        else
        {
            // (Where the input sounds matters only while a pass on play waits for it.)
            const auto soundFrom = settings.waitForSound ? firstSoundingSample (buffer, numChannels, start, length) : 0;

            if (start == 0 && length == numSamples)
            {
                numSegments = tracker.process (timing, midi, length, settings, segments, soundFrom);
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

                numSegments = tracker.process (chunkTiming, chunkMidi, length, settings, segments, soundFrom);
            }
        }

        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel] = buffer.getWritePointer (channel, start);

        carver.process (channels, numChannels, length, segments, numSegments, carve);

        // The sines, in the modes that play them (and while they fade out of them). Not
        // rendered at all otherwise; they start afresh, with a fade-in, when they are next wanted.
        const auto wanted = drawGain.getTargetValue() > 0.0f || drawGain.getCurrentValue() > 0.0f;

        if (wanted)
        {
            if (! sinesRunning)
            {
                engine.reset();
                sinesRunning = true;
            }

            auto* out = mono.get();
            engine.render (out, length, segments, numSegments);

            for (int i = 0; i < length; ++i)
                out[i] = softClip (out[i] * outputGain.getNextValue() * drawGain.getNextValue());
        }
        else
        {
            // Faded out: let go of the engine's state (a crossfade it was in, a queued plan).
            if (sinesRunning)
                engine.reset();

            sinesRunning = false;
            outputGain.skip (length);
            drawGain.skip (length);
        }

        addDelayedSines (buffer, start, length, numChannels, wanted);

        const auto [position, open] = positionAfter (segments, numSegments, length);
        playPosition.store (position);
        playing = open;

        if (analyzer.isActive())
        {
            // What comes out, as one channel.
            auto* mix = mono.get();
            const auto scale = 1.0f / static_cast<float> (numChannels);
            juce::FloatVectorOperations::copy (mix, buffer.getReadPointer (0, start), length);

            for (int channel = 1; channel < numChannels; ++channel)
                juce::FloatVectorOperations::add (mix, buffer.getReadPointer (channel, start), length);

            juce::FloatVectorOperations::multiply (mix, scale, length);
            analyzer.push (mix, length);
        }

        start += length;
    }

    if (bypassed)
    {
        // Silent: coming back from bypass starts cleanly rather than mid-pass.
        if (! sinesRunning || ! engine.isSounding())
        {
            engine.reset();
            tracker.reset();
            sinesRunning = false;
        }
    }
    else if (preview != 0 && tracker.hasPreviewEnded())
    {
        auto expected = preview;
        previewToken.compare_exchange_strong (expected, 0);
    }

    if (carver.getFadingPlan() == nullptr)
        olderMasks = nullptr;

    planExchange.reportInUse (versionOf (engine.getPlan()), versionOf (engine.getFadingPlan()));
    maskExchange.reportInUse (versionOf (currentMasks), versionOf (olderMasks));

    sounding.store (playing && ! bypassed);
    waitingForSound.store (! bypassed && tracker.isWaitingForSound());
    effectiveTempo.store (tracker.getTempo());

    // An effect that makes no MIDI: nothing goes out.
    midi.clear();
}

void ArtFreqProcessor::addDelayedSines (juce::AudioBuffer<float>& buffer, int start, int numSamples,
                                        int numChannels, bool rendered) noexcept
{
    // Once the delay holds nothing but silence and nothing new comes in, there is nothing to
    // add: the carved (or untouched) audio stays exactly as it is.
    if (! rendered && sineDelaySilence >= sineDelayLength)
        return;

    auto* sines = mono.get();

    if (! rendered)
        std::fill (sines, sines + numSamples, 0.0f);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto in = sines[i];
        sines[i] = sineDelay[sineDelayPosition];
        sineDelay[sineDelayPosition] = in;

        if (++sineDelayPosition == sineDelayLength)
            sineDelayPosition = 0;

        sineDelaySilence = juce::exactlyEqual (in, 0.0f) ? juce::jmin (sineDelaySilence + 1, sineDelayLength) : 0;
    }

    for (int channel = 0; channel < numChannels; ++channel)
        buffer.addFrom (channel, start, sines, numSamples);
}

EffectMode ArtFreqProcessor::getEffectMode() const noexcept
{
    return modeParam != nullptr ? params::effectModeForIndex (juce::roundToInt (modeParam->load())) : EffectMode::carve;
}

float ArtFreqProcessor::getDepthDb() const noexcept
{
    return depthParam != nullptr ? depthParam->load() : 0.0f;
}

int ArtFreqProcessor::wantedHaloColumns() const noexcept
{
    // About haloSeconds of the drawing, in columns at the length in use, at most a sixteenth of
    // it (in a drawing a beat long, 60 ms would blur a lot of it).
    const auto seconds = juce::jmax (0.05, getLengthSeconds());
    const auto columns = static_cast<int> (std::ceil (haloSeconds * Canvas::width / seconds - 1.0e-9));
    return juce::jlimit (1, maxHaloColumns, columns);
}

void ArtFreqProcessor::buildMasks (const FrequencyScale& scale)
{
    JUCE_ASSERT_MESSAGE_THREAD

    MaskSettings settings;
    settings.scale = scale;

    auto masks = std::make_shared<MaskPlanSet>();
    masks->exact = buildMaskPlan (canvas, settings);
    masks->haloRows = haloRows;
    masks->haloColumns = wantedHaloColumns();
    masks->halo = widenMaskPlan (*masks->exact, masks->haloRows, masks->haloColumns);

    builtHaloColumns = masks->haloColumns;
    maskExchange.publish (std::move (masks));
}

void ArtFreqProcessor::setPreview (bool shouldPreview) noexcept
{
    // Preview needs an activated plug-in.
    if (! shouldPreview || ! isLicensed())
    {
        previewToken.store (0);
        return;
    }

    // A token of its own for this press (never 0), taken only if the preview is off.
    auto token = previewPresses.fetch_add (1) + 1;

    if (token == 0)
        token = previewPresses.fetch_add (1) + 1;

    juce::uint32 off = 0;
    previewToken.compare_exchange_strong (off, token);
}

double ArtFreqProcessor::getEffectiveTempo() const noexcept
{
    if (juce::roundToInt (tempoSourceParam->load()) != 0)
        return static_cast<double> (bpmParam->load());

    return effectiveTempo.load();
}

double ArtFreqProcessor::getLengthSeconds() const noexcept
{
    const auto lengthQn = params::lengthInQuarterNotes (juce::roundToInt (lengthParam->load()),
                                                        quarterNotesPerBar (timeSignature.load()));
    const auto tempo = juce::jlimit (20.0, 400.0, getEffectiveTempo());
    return lengthQn * 60.0 / tempo;
}

int ArtFreqProcessor::getSinglePassStartBar() const noexcept
{
    const auto transport = params::triggerForIndex (juce::roundToInt (triggerParam->load())) == TriggerMode::transport;

    if (! transport || repeatParam->load() >= 0.5f
        || params::startAtForIndex (juce::roundToInt (startAtParam->load())) != StartAt::bar)
        return 0;

    return juce::jlimit (params::firstStartBar, params::lastStartBar, juce::roundToInt (startBarParam->load()));
}

float ArtFreqProcessor::getOutputGain() const noexcept
{
    return juce::Decibels::decibelsToGain (levelParam->load(), -100.0f);
}

// ---- the plan ------------------------------------------------------------------------------

FrequencyScale ArtFreqProcessor::getScale() const
{
    FrequencyScale scale;
    scale.type = params::scaleForIndex (juce::roundToInt (scaleParam->load()));
    scale.minHz = static_cast<double> (rangeLowParam->load());
    scale.maxHz = static_cast<double> (rangeHighParam->load());
    return scale.sanitised();
}

PlanSettings ArtFreqProcessor::getPlanSettings() const
{
    PlanSettings settings;
    settings.scale = getScale();
    settings.density = juce::jlimit (16, 1024, juce::roundToInt (densityParam->load()));
    settings.contrastDb = contrastParam->load();
    settings.slopeDbPerOctave = params::slopeForIndex (juce::roundToInt (slopeParam->load()));
    settings.lowEnd = params::lowEndForIndex (juce::roundToInt (lowEndParam->load()));
    // Lines above the top of the hearing range only cost CPU; the engine also keeps every line
    // below Nyquist.
    settings.maxFrequencyHz = 22000.0f;
    return settings;
}

void ArtFreqProcessor::rebuildPlanIfNeeded()
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto parametersChanged = parametersDirty.exchange (false);
    const auto havePlan = planExchange.latest() != nullptr;

    // The effect's halo follows the length and the tempo (the host's too), so it is checked on
    // every tick.
    const auto haloChanged = isEffect() && wantedHaloColumns() != builtHaloColumns;

    if (! documentDirty && ! parametersChanged && havePlan && ! haloChanged)
        return;

    const auto settings = getPlanSettings();
    const auto scaleChanged = settings.scale != lastBuiltSettings.scale;
    const auto planNeeded = ! havePlan || documentDirty || ! sameSettings (settings, lastBuiltSettings);

    if (! planNeeded && ! haloChanged)
        return;

    // The canvas only depends on the document and the scale.
    const auto canvasChanged = documentDirty || scaleChanged || ! havePlan;

    if (canvasChanged)
        compositor.render (document, settings.scale, canvas);

    if (planNeeded)
    {
        std::shared_ptr<ArtPlan> plan (buildPlan (canvas, settings).release());
        planExchange.publish (plan);
        lastBuiltSettings = settings;
    }

    // The masks come from the same canvas (Density, Contrast and Lows do not change them).
    if (isEffect() && (canvasChanged || haloChanged))
        buildMasks (settings.scale);

    documentDirty = false;
    sendChangeMessage();
}

void ArtFreqProcessor::parameterChanged (const juce::String&, float)
{
    // May be called on any thread: just note it; the timer rebuilds.
    parametersDirty.store (true);
}

void ArtFreqProcessor::timerCallback()
{
    rebuildPlanIfNeeded();
    planExchange.collectGarbage();
    maskExchange.collectGarbage();

    if (savedDocumentStale && juce::Time::getMillisecondCounter() - lastEditTime >= saveQuietMs)
        refreshSavedDocument();
}

// ---- the drawing ---------------------------------------------------------------------------

void ArtFreqProcessor::documentChanged()
{
    documentDirty = true;
    savedDocumentStale = true;
    lastEditTime = juce::Time::getMillisecondCounter();
    sendChangeMessage();
    markProjectChanged();
}

void ArtFreqProcessor::markProjectChanged()
{
    // The drawing and the window size are saved state but not parameters, so the host cannot
    // see them change: without this a VST3 host would not offer to save the project, and the
    // change would be lost when it closes. (The wrapper merges these calls asynchronously, so
    // one per brush dab or key press is cheap.)
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void ArtFreqProcessor::setEditorSize (int width, int height)
{
    const auto oldWidth = editorWidth.exchange (width);
    const auto oldHeight = editorHeight.exchange (height);

    // 0 x 0 is "never resized", which shows at the default size: only a real change counts.
    const auto shown = [] (int w, int h)
    {
        return w > 0 && h > 0 ? juce::Point<int> (w, h)
                              : juce::Point<int> (ArtFreqEditor::baseWidth, ArtFreqEditor::baseHeight);
    };

    if (shown (oldWidth, oldHeight) != shown (width, height))
        markProjectChanged();
}

void ArtFreqProcessor::editDocument (const std::function<void (ArtDocument&)>& edit, const juce::String& coalesceKey)
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

void ArtFreqProcessor::setDocument (const ArtDocument& newDocument)
{
    JUCE_ASSERT_MESSAGE_THREAD

    history.push (document);
    lastCoalesceKey.clear();
    document = newDocument;
    documentChanged();
}

bool ArtFreqProcessor::undo()
{
    lastCoalesceKey.clear();

    if (! history.undo (document))
        return false;

    documentChanged();
    return true;
}

bool ArtFreqProcessor::redo()
{
    lastCoalesceKey.clear();

    if (! history.redo (document))
        return false;

    documentChanged();
    return true;
}

// ---- state ---------------------------------------------------------------------------------

void ArtFreqProcessor::refreshSavedDocument()
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto tree = document.toValueTree();

    const juce::ScopedLock lock (savedStateLock);
    savedDocument = tree;
    savedDocumentStale = false;
}

void ArtFreqProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Hosts may save from any thread. On the message thread the document is serialised now;
    // elsewhere the copy made when the last edit settled is used.
    if (onMessageThread() && savedDocumentStale)
        refreshSavedDocument();

    juce::XmlElement root ("ArtFreqState");
    root.setAttribute ("version", stateVersion);
    root.setAttribute ("pluginVersion", ARTFREQ_VERSION_STRING);
    // Which of the two saved it. The state is the same otherwise, so either loads the other's
    // drawing and shared settings (the effect's Mode and Depth are left out by the instrument).
    root.setAttribute ("plugin", getName());
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

void ArtFreqProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto root = getXmlFromBinary (data, sizeInBytes);

    // Before 0.4 the plug-in was called FreqArt and its state used FreqArt* tags; those load too.
    if (root == nullptr || ! (root->hasTagName ("ArtFreqState") || root->hasTagName ("FreqArtState")))
        return;

    editorWidth.store (juce::jlimit (0, 8000, root->getIntAttribute ("editorWidth", 0)));
    editorHeight.store (juce::jlimit (0, 8000, root->getIntAttribute ("editorHeight", 0)));

    auto* parametersXml = root->getChildByName (parameters.state.getType().toString());

    if (parametersXml == nullptr)
        parametersXml = root->getChildByName ("FreqArtParameters");

    if (parametersXml != nullptr)
    {
        auto loadedParameters = juce::ValueTree::fromXml (*parametersXml);

        // Saved under the old name: the same tree, kept under the type this version saves.
        if (loadedParameters.isValid() && ! loadedParameters.hasType (parameters.state.getType()))
        {
            juce::ValueTree renamed (parameters.state.getType());
            renamed.copyPropertiesAndChildrenFrom (loadedParameters, nullptr);
            loadedParameters = renamed;
        }

        prepareLoadedParameters (loadedParameters);
        parameters.replaceState (loadedParameters);
    }

    parametersDirty.store (true);

    juce::ValueTree documentTree;

    auto* documentXml = root->getChildByName ("ArtFreqDocument");

    if (documentXml == nullptr)
        documentXml = root->getChildByName ("FreqArtDocument");

    if (documentXml != nullptr)
        documentTree = juce::ValueTree::fromXml (*documentXml);

    if (documentTree.isValid() && ! documentTree.hasType ("ArtFreqDocument"))
    {
        juce::ValueTree renamed ("ArtFreqDocument");
        renamed.copyPropertiesAndChildrenFrom (documentTree, nullptr);
        documentTree = renamed;
    }

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

void ArtFreqProcessor::prepareLoadedParameters (juce::ValueTree& loadedParameters) const
{
    // The value tree state keeps each parameter as a PARAM child with an "id" and its plain
    // "value".
    static const juce::Identifier paramType ("PARAM"), idProperty ("id"), valueProperty ("value");

    const auto child = [&] (const juce::String& id) { return loadedParameters.getChildWithProperty (idProperty, id); };

    // ArtFreq 0.1's third trigger, "Free loop", looped by itself; nothing plays by itself any
    // more, so it becomes the nearest thing left: Transport with Repeat on. (A clamp to the two
    // choices left would make it MIDI note.)
    if (auto trigger = child (params::id::trigger); trigger.isValid())
    {
        const auto value = trigger.getProperty (valueProperty);
        const auto text = value.toString().trim();
        const auto freeLoop = text.equalsIgnoreCase (params::legacyFreeLoopName)
                           || (text.containsAnyOf ("0123456789")
                               && juce::roundToInt (static_cast<double> (value)) == params::legacyFreeLoopIndex);

        if (freeLoop)
        {
            trigger.setProperty (valueProperty, 0, nullptr);

            auto repeat = child (params::id::repeat);

            if (! repeat.isValid())
            {
                repeat = juce::ValueTree (paramType);
                repeat.setProperty (idProperty, params::id::repeat, nullptr);
                loadedParameters.appendChild (repeat, nullptr);
            }

            repeat.setProperty (valueProperty, 1, nullptr);
        }
    }

    // Lows came after 0.2: a state without it was made with the stripes, the only look there was,
    // and loads with them, so an old project sounds and looks as it did (new instances start
    // Smooth).
    if (auto lowEnd = child (params::id::lowEnd); ! (lowEnd.isValid() && lowEnd.hasProperty (valueProperty)))
    {
        if (! lowEnd.isValid())
        {
            lowEnd = juce::ValueTree (paramType);
            lowEnd.setProperty (idProperty, params::id::lowEnd, nullptr);
            loadedParameters.appendChild (lowEnd, nullptr);
        }

        lowEnd.setProperty (valueProperty, params::stripesLowEndIndex, nullptr);
    }

    // Start at came after 0.4: until then a single Transport pass always started at its start
    // bar. Bar 1 was the default (a drawing nobody placed): it loads as On play, so pressing play
    // anywhere now plays it. Any other bar was chosen: it loads as At bar and plays where it did.
    if (auto startAt = child (params::id::startAt); ! (startAt.isValid() && startAt.hasProperty (valueProperty)))
    {
        if (! startAt.isValid())
        {
            startAt = juce::ValueTree (paramType);
            startAt.setProperty (idProperty, params::id::startAt, nullptr);
            loadedParameters.appendChild (startAt, nullptr);
        }

        const auto startBar = child (params::id::startBar);
        const auto placed = startBar.isValid() && startBar.hasProperty (valueProperty)
                         && juce::roundToInt (static_cast<double> (startBar.getProperty (valueProperty))) != params::firstStartBar;

        startAt.setProperty (valueProperty, placed ? params::atBarStartIndex : params::onPlayStartIndex, nullptr);
    }

    // Parameters this variant does not have (the effect's Mode and Depth, in a state the effect
    // saved, loaded into the instrument) are left out, so they are not saved back as if they
    // were its own.
    for (int i = loadedParameters.getNumChildren(); --i >= 0;)
    {
        const auto saved = loadedParameters.getChild (i);

        if (saved.hasType (paramType) && parameters.getParameter (saved.getProperty (idProperty).toString()) == nullptr)
            loadedParameters.removeChild (i, nullptr);
    }

    // Any other parameter the state does not have (saved before it existed, or by the other
    // variant) loads at its default: replaceState would otherwise leave it at whatever this
    // instance had.
    for (auto* parameter : juce::AudioProcessor::getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (ranged == nullptr)
            continue;

        auto saved = child (ranged->getParameterID());

        if (saved.isValid() && saved.hasProperty (valueProperty))
            continue;

        if (! saved.isValid())
        {
            saved = juce::ValueTree (paramType);
            saved.setProperty (idProperty, ranged->getParameterID(), nullptr);
            loadedParameters.appendChild (saved, nullptr);
        }

        saved.setProperty (valueProperty, ranged->convertFrom0to1 (ranged->getDefaultValue()), nullptr);
    }
}

void ArtFreqProcessor::handleAsyncUpdate()
{
    juce::ValueTree documentTree;
    {
        const juce::ScopedLock lock (savedStateLock);
        documentTree = std::exchange (pendingDocument, juce::ValueTree());
    }

    if (documentTree.isValid())
        applyDocumentState (documentTree);
}

void ArtFreqProcessor::applyDocumentState (const juce::ValueTree& documentTree)
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

juce::AudioProcessorEditor* ArtFreqProcessor::createEditor()
{
    return new ArtFreqEditor (*this);
}

} // namespace artfreq

// createPluginFilter, which makes the one or the other, is in src/plugin/ (one file per plug-in
// target).
