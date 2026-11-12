#pragma once

// The plug-in window. See docs/DESIGN.md, "Interface".

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace freqart
{

class FreqArtEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int baseWidth = 1120;
    static constexpr int baseHeight = 720;

    explicit FreqArtEditor (FreqArtProcessor& processor);
    ~FreqArtEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** For the snapshot tool and tests: switches the main view between the canvas (false) and
        the live spectrogram (true). */
    void showLiveView (bool live);

    /** For the snapshot tool and tests: opens or closes the in-window panels. */
    void showHelp (bool shouldShow);
    void showSettings (bool shouldShow);

    /** For the snapshot tool and tests: selects an object (or nothing with a null Uuid) as if it
        had been clicked. */
    void selectObject (const juce::Uuid& id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FreqArtEditor)
};

} // namespace freqart
