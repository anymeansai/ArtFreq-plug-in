#pragma once

// The plug-in window: ArtFreq's (the instrument), and ArtFreq FX's, a smaller window of its own
// laid out for the effect. See docs/DESIGN.md, "Interface" and "ArtFreq FX".

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace artfreq
{

class ArtFreqEditor final : public juce::AudioProcessorEditor
{
public:
    // The window at 100 %: ArtFreq's, and ArtFreq FX's.
    static constexpr int baseWidth = 1120;
    static constexpr int baseHeight = 720;
    static constexpr int effectBaseWidth = 720;
    static constexpr int effectBaseHeight = 440;

    explicit ArtFreqEditor (ArtFreqProcessor& processor);
    ~ArtFreqEditor() override;

    /** This plug-in's window at 100 %. */
    int getBaseWidth() const noexcept;
    int getBaseHeight() const noexcept;

    /** The scale (0.75 to 2) a window saved at `width` x `height` opens at. ArtFreq FX's window
        was the instrument's until 0.4: a size saved with that shape is read as a scale of that
        window, so a project saved at 100 % opens at 100 % of the small one, not one and a half
        times as large. */
    static float scaleForSavedSize (bool effect, int width, int height) noexcept;

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

    /** For the snapshot tool and tests: the selected object (a null Uuid if none), and whether
        the inspector shows the layer list (rather than the selected object's settings). In
        ArtFreq FX, where the inspector is a drawer over the canvas: whether the drawer is open
        on the list. */
    juce::Uuid getSelectedObject() const;
    bool isLayerListShown() const;

    /** For the snapshot tool and tests: ArtFreq FX's drawer. Opens or closes the layer list as
        the Layers button does; whether the drawer is open (on the list or an object's settings)
        and on which side of the canvas. Always false in ArtFreq, whose inspector is a panel. */
    void showLayers (bool shouldShow);
    bool isDrawerOpen() const;
    bool isDrawerOnLeft() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArtFreqEditor)
};

} // namespace artfreq
