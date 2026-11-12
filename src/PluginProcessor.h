#pragma once

// The host-facing plug-in: an instrument whose output is the sound of the drawing.

#include "Parameters.h"
#include "core/ArtPlan.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "dsp/ArtEngine.h"
#include "dsp/LiveAnalyzer.h"
#include "dsp/PlanExchange.h"
#include "dsp/PlayheadTracker.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>

namespace freqart
{

class FreqArtProcessor final : public juce::AudioProcessor,
                               public juce::ChangeBroadcaster,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::Timer
{
public:
    FreqArtProcessor();
    ~FreqArtProcessor() override;

    // ---- juce::AudioProcessor -------------------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FreqArt"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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
    // every edit, undo, redo and state load, and after every new plan.

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

    // ---- playback information for the editor ---------------------------------------------
    double getPlayPosition() const noexcept { return playPosition.load(); }  // 0..1
    bool isSounding() const noexcept { return sounding.load(); }

    /** Plays the drawing on a loop from its start, whatever the trigger mode (an audition
        while the DAW is stopped). */
    void setPreview (bool shouldPreview) noexcept;
    bool isPreviewing() const noexcept { return previewRequested.load(); }

    /** The tempo in use (host or manual) and one pass of the drawing in seconds at it. */
    double getEffectiveTempo() const noexcept;
    double getLengthSeconds() const noexcept;

    LiveAnalyzer& getAnalyzer() noexcept { return analyzer; }

    /** The output level as a linear gain (for the WAV export). */
    float getOutputGain() const noexcept;

    // ---- editor preferences, kept with the session ---------------------------------------
    int getEditorWidth() const noexcept { return editorWidth; }
    int getEditorHeight() const noexcept { return editorHeight; }
    void setEditorSize (int width, int height) noexcept { editorWidth = width; editorHeight = height; }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void documentChanged();
    TrackerSettings currentTrackerSettings() const noexcept;

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
    int editorWidth = 0, editorHeight = 0;  // 0 = default size

    // shared
    PlanExchange planExchange;
    LiveAnalyzer analyzer;
    std::atomic<bool> previewRequested { false };
    std::atomic<double> playPosition { 0.0 };
    std::atomic<bool> sounding { false };
    std::atomic<double> effectiveTempo { 120.0 };
    std::atomic<double> quarterNotesPerBar { 4.0 };

    // audio thread
    ArtEngine engine;
    PlayheadTracker tracker;
    PlaySegment segments[PlayheadTracker::maxSegments];
    juce::LinearSmoothedValue<float> outputGain;
    juce::HeapBlock<float> mono;
    int monoCapacity = 0;

    // parameter pointers (raw values are atomics owned by the value tree)
    std::atomic<float>* levelParam = nullptr;
    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* triggerParam = nullptr;
    std::atomic<float>* tempoSourceParam = nullptr;
    std::atomic<float>* bpmParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreqArtProcessor)
};

} // namespace freqart
