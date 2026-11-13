#pragma once

// The footer's Export column: "Bounce WAV" renders one pass of the drawing on a background
// thread; the result is offered as a chip to drag into the DAW, and as "Save..." (an async file
// chooser). See docs/DESIGN.md, "Footer".

#include "PluginProcessor.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace freqart::ui::shell
{

class ExportPanel final : public juce::Component
{
public:
    explicit ExportPanel (FreqArtProcessor& processor);
    ~ExportPanel() override;

    /** Where bounces go: <temp>/FreqArt. Every bounce is a file of its own there
        (FreqArt-<uuid>.wav), so no later bounce, from this instance or another, ever replaces
        a file that was handed out. */
    static juce::File getBounceFolder();

    /** The finished bounce that the chip and "Save..." hand out (none before the first one). */
    juce::File getBounceFile() const;

    /** Renders the current plan to a new file in getBounceFolder() in the background (no-op
        while busy). When it is done it replaces the previous bounce, whose file is deleted
        unless it was dragged out of the window. */
    void startBounce();

    bool isBouncing() const noexcept;

    /** True while a finished bounce matches what is drawn (plan, length, level). */
    bool hasCurrentFile() const noexcept;

    /** Picks up a finished bounce and notices when the drawing has changed since (the chip then
        goes back to "Bounce WAV"). The editor calls this from its timer. */
    void refresh();

    /** For the snapshot tool and tests: blocks until the running bounce (if any) has finished,
        then refreshes. Returns false on timeout. */
    bool waitForBounce (int timeoutMilliseconds);

    /** The last error, empty if the last bounce worked. */
    juce::String getLastError() const;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Chip;
    struct Job;

    juce::String hintText() const;
    void updateVisibility();
    void saveCopy();
    void discardBounceFile();

    FreqArtProcessor& processor;
    IconButton bounceButton;
    std::unique_ptr<Chip> chip;
    ShellButton saveButton { "Save..." };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Job> job;

    // The finished file, and whether it was dragged out (a DAW may then be reading it where it
    // is, so it is never deleted from here: the clean-up at start removes it after a day).
    juce::File bounceFile;
    bool bounceDraggedOut = false;

    // What the finished file was rendered from, to tell when it no longer matches.
    uint64_t bouncedPlanVersion = 0;
    double bouncedSeconds = 0.0;
    float bouncedGain = 0.0f;
    bool fileReady = false;
    juce::String lastError;
    juce::String shownHint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};

} // namespace freqart::ui::shell
