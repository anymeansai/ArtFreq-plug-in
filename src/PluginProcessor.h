#pragma once

// The host-facing plug-in, in two variants built from the same code (one class, a flag):
//
// - ArtFreq, an instrument: its output is the sound of the drawing.
// - ArtFreq FX, an audio effect for audio tracks, buses and the master: the drawing is carved
//   out of the audio coming in (Carve), kept alone (Stencil), played into a hole carved for it
//   (Carve + Draw) or played on top of it (Draw). See docs/DESIGN.md, "ArtFreq FX".
//
// Hosts decide from a VST3's category whether it goes in an instrument slot or an effect slot
// (Ableton Live, FL Studio and Cubase offer an instrument only on MIDI / instrument tracks and an
// effect only on audio), so the same plug-in is registered twice: once as each.

#include "Parameters.h"
#include "core/ArtPlan.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/MaskPlan.h"
#include "dsp/ArtEngine.h"
#include "dsp/LiveAnalyzer.h"
#include "dsp/PlanExchange.h"
#include "dsp/PlayheadTracker.h"
#include "dsp/TunedCarver.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>

namespace artfreq
{

class ArtFreqProcessor final : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::Timer,
                               private juce::AsyncUpdater
{
public:
    enum class Variant
    {
        instrument,  // ArtFreq: no audio in, makes the sound
        effect       // ArtFreq FX: audio in and out, Mode and Depth
    };

    explicit ArtFreqProcessor (Variant variant = Variant::instrument);
    ~ArtFreqProcessor() override;

    Variant getVariant() const noexcept { return variant; }
    bool isEffect() const noexcept { return variant == Variant::effect; }

    // ---- juce::AudioProcessor -------------------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return isEffect() ? "ArtFreq FX" : "ArtFreq"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // The effect always has one frame of audio in flight (and its sines in the Draw modes), so a
    // host that stops calling it on silent input must keep going that long; the instrument fades
    // within a block.
    double getTailLengthSeconds() const override
    {
        const auto rate = getSampleRate();
        return isEffect() && rate > 0.0 ? getLatencySamples() / rate + 0.05 : 0.0;
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- parameters ------------------------------------------------------------------------
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    /** The analyser scale and plan settings the current parameter values describe. */
    FrequencyScale getScale() const;
    PlanSettings getPlanSettings() const;

    // ---- the drawing (message thread only) -----------------------------------------------
    // Every change to the document goes through editDocument, so undo, the plan and the
    // editor stay in step. The processor sends a change message (ChangeBroadcaster) after
    // every edit, undo, redo and state load, and after every new plan. Edits, undo and redo
    // (not loads) also tell the host the project has unsaved changes.

    const ArtDocument& getDocument() const noexcept { return document; }

    /** Applies `edit` to the document. An undo step is recorded first unless `coalesceKey` is
        non-empty and equal to the key of the previous edit (so a whole brush stroke, or typing
        into one text box, is one undo step). The plan is rebuilt shortly after. */
    void editDocument (const std::function<void (ArtDocument&)>& edit, const juce::String& coalesceKey = {});

    /** Replaces the whole document (one undo step). */
    void setDocument (const ArtDocument& newDocument);

    /** Ends coalescing: the next edit starts a new undo step whatever its key. */
    void endCoalescing() noexcept { lastCoalesceKey.clear(); }

    bool canUndo() const noexcept { return history.canUndo(); }
    bool canRedo() const noexcept { return history.canRedo(); }
    bool undo();
    bool redo();

    /** The plan for what is currently drawn (rebuilt within ~40 ms of a change). */
    std::shared_ptr<const ArtPlan> getCurrentPlan() const { return planExchange.latest(); }

    /** Rebuilds the plan now if anything changed (normally done by a timer). */
    void rebuildPlanIfNeeded();

    /** True from an edit (or a change to a plan setting) until the timer has rebuilt the plan:
        getCurrentPlan() is then older than the drawing. Message thread. */
    bool isPlanPending() const noexcept { return documentDirty || parametersDirty.load(); }

    // ---- playback information for the editor ---------------------------------------------
    double getPlayPosition() const noexcept { return playPosition.load(); }  // 0..1
    bool isSounding() const noexcept { return sounding.load(); }

    /** Plays the drawing from its start, whatever the trigger mode (an audition while the DAW
        is stopped): on a loop with Repeat, else once, after which the preview turns itself off
        (isPreviewing() turns false). Turning it on while it is on changes nothing. */
    void setPreview (bool shouldPreview) noexcept;
    bool isPreviewing() const noexcept { return previewToken.load() != 0; }

    /** The tempo in use (host or manual) and one pass of the drawing in seconds at it. */
    double getEffectiveTempo() const noexcept;
    double getLengthSeconds() const noexcept;

    /** The bar a single pass on the DAW's timeline starts on (Transport without Repeat; 1 is the
        song start), or 0 when the drawing does not play at one place in the song (Repeat, MIDI).
        The canvas numbers its bars from it. */
    int getSinglePassStartBar() const noexcept;

    /** The host's time signature as the audio thread last saw it (4/4 until a host gives one):
        the length in bars and the canvas's bar and beat grid follow it. */
    juce::AudioPlayHead::TimeSignature getTimeSignature() const noexcept { return timeSignature.load(); }

    LiveAnalyzer& getAnalyzer() noexcept { return analyzer; }

    // ---- ArtFreq FX ------------------------------------------------------------------------

    /** The effect's Mode (Carve for the instrument, which has none). */
    EffectMode getEffectMode() const noexcept;

    /** How far carved frequencies drop, in dB (0 for the instrument). */
    float getDepthDb() const noexcept;

    /** The masks the effect carves with (null for the instrument, and before the first build).
        Rebuilt with the plan, and when a change of length or tempo changes the halo's width. */
    std::shared_ptr<const MaskPlanSet> getCurrentMasks() const { return maskExchange.latest(); }

    /** Carve + Draw's halo: the hole is this many canvas rows wider than the drawing above and
        below it, and about haloSeconds longer before and after it (in columns, from the length
        at the time of the build, at most maxHaloColumns: a sixteenth of the drawing). */
    static constexpr int haloRows = 4;
    static constexpr double haloSeconds = 0.060;
    static constexpr int maxHaloColumns = Canvas::width / 16;

    /** ... and it carves this many FFT bins wider still (Carve itself widens by one). */
    static constexpr int carveDrawHaloBins = 3;

    /** The output level as a linear gain (for the WAV export). */
    float getOutputGain() const noexcept;

    // ---- editor preferences, kept with the session ---------------------------------------
    int getEditorWidth() const noexcept { return editorWidth.load(); }
    int getEditorHeight() const noexcept { return editorHeight.load(); }
    void setEditorSize (int width, int height);  // message thread; a change marks the project changed

    /** The sample rate the host prepared the plug-in with (48 kHz before that). */
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

private:
    static BusesProperties busesFor (Variant variant);
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void handleAsyncUpdate() override;
    void documentChanged();
    void markProjectChanged();
    void refreshSavedDocument();
    void applyDocumentState (const juce::ValueTree& documentTree);
    void prepareLoadedParameters (juce::ValueTree& loadedParameters) const;
    TrackerSettings currentTrackerSettings() const noexcept;
    void renderChunk (float* out, int numSamples, const HostTiming& timing, const juce::MidiBuffer& midi,
                      const TrackerSettings& settings) noexcept;
    void writeOutput (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;
    HostTiming readHostTiming() noexcept;
    void processEffect (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed) noexcept;
    void addDelayedSines (juce::AudioBuffer<float>& buffer, int start, int numSamples, int numChannels, bool rendered) noexcept;
    void buildMasks (const FrequencyScale& scale);
    int wantedHaloColumns() const noexcept;

    const Variant variant;
    juce::AudioProcessorValueTreeState parameters;

    // message thread
    ArtDocument document;
    DocumentHistory history;
    juce::String lastCoalesceKey;
    Compositor compositor;
    Canvas canvas;
    PlanSettings lastBuiltSettings;
    bool documentDirty = true;
    std::atomic<bool> parametersDirty { true };
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };  // 0 = default size
    int builtHaloColumns = -1;   // the effect's masks were built with this halo (-1: none yet)

    // The document as last serialised (or as loaded), for hosts that save state from another
    // thread. Refreshed on the message thread shortly after edits stop.
    juce::CriticalSection savedStateLock;
    juce::ValueTree savedDocument;
    bool savedDocumentStale = true;
    juce::uint32 lastEditTime = 0;

    // A document loaded by setStateInformation on a thread other than the message thread waits
    // here until the message thread applies it.
    juce::ValueTree pendingDocument;

    // shared
    PlanExchange planExchange;
    MaskSetExchange maskExchange;   // the effect only
    LiveAnalyzer analyzer;
    // 0 while Preview is off; otherwise which press of Preview is playing, so a single pass
    // that ends (the audio thread turns the preview off then) never turns off a newer press.
    std::atomic<juce::uint32> previewToken { 0 };
    std::atomic<juce::uint32> previewPresses { 0 };
    std::atomic<double> playPosition { 0.0 };
    std::atomic<bool> sounding { false };
    std::atomic<double> effectiveTempo { 120.0 };
    std::atomic<juce::AudioPlayHead::TimeSignature> timeSignature { juce::AudioPlayHead::TimeSignature {} };
    static_assert (std::atomic<juce::AudioPlayHead::TimeSignature>::is_always_lock_free,
                   "the audio thread stores the time signature: it must not lock");

    // audio thread
    ArtEngine engine;
    PlayheadTracker tracker;
    PlaySegment segments[PlayheadTracker::maxSegments];
    juce::uint32 previewTokenInUse = 0;
    juce::LinearSmoothedValue<float> outputGain;
    juce::HeapBlock<float> mono;
    int monoCapacity = 0;
    juce::MidiBuffer chunkMidi;  // for hosts that send blocks larger than announced
    std::atomic<double> currentSampleRate { 48000.0 };

    // audio thread, the effect only
    TunedCarver carver;
    const MaskPlanSet* currentMasks = nullptr;
    const MaskPlanSet* olderMasks = nullptr;       // the oldest set the carver may still read (a frame it began)
    juce::LinearSmoothedValue<float> drawGain;     // 1 in the modes that play the sines, else 0
    bool sinesRunning = false;                     // the engine is being rendered
    juce::HeapBlock<float> sineDelay;              // the sines, as late as the carved audio
    int sineDelayLength = 0;
    int sineDelayPosition = 0;
    int sineDelaySilence = 0;                      // samples of silence written into it in a row

    // parameter pointers (raw values are atomics owned by the value tree)
    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* triggerParam = nullptr;
    std::atomic<float>* repeatParam = nullptr;
    std::atomic<float>* startBarParam = nullptr;
    std::atomic<float>* tempoSourceParam = nullptr;
    std::atomic<float>* bpmParam = nullptr;
    std::atomic<float>* densityParam = nullptr;
    std::atomic<float>* contrastParam = nullptr;
    std::atomic<float>* lowEndParam = nullptr;
    std::atomic<float>* scaleParam = nullptr;
    std::atomic<float>* rangeLowParam = nullptr;
    std::atomic<float>* rangeHighParam = nullptr;
    std::atomic<float>* slopeParam = nullptr;
    std::atomic<float>* modeParam = nullptr;       // the effect only
    std::atomic<float>* depthParam = nullptr;      // the effect only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArtFreqProcessor)
};

} // namespace artfreq
