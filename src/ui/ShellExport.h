#pragma once

// The footer's Export column: "Bounce WAV" renders one pass of the drawing on a background
// thread; the result is offered as a chip to drag into the DAW, and as "Save..." (an async file
// chooser). From then on the file follows the drawing: each change renders a new file of its
// own a moment later. See docs/DESIGN.md, "Footer".
//
// In FreqArt FX, Carve and Stencil change the song, which only the DAW has: the column then says
// "Bounce the track in your DAW" instead. In the Draw modes it bounces the drawing's sines.

#include "PluginProcessor.h"
#include "ShellControls.h"
#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

namespace freqart::ui::shell
{

class ExportPanel final : public juce::Component
{
public:
    explicit ExportPanel (FreqArtProcessor& processor);
    ~ExportPanel() override;

    /** How long the drawing, length, tempo and level must stay as they are before the file is
        rendered again: long enough that a drag or a run of typing renders once, at the end. */
    static constexpr int updateDelayMilliseconds = 400;

    /** Where bounces go: <temp>/FreqArt. Every bounce is a file of its own there, named after
        the drawing and the time (see reserveBounceFile), so no later bounce, from this instance
        or another, ever replaces a file that was handed out. */
    static juce::File getBounceFolder();

    /** The newest finished bounce (none before the first). It may be of an older drawing:
        getFileToHandOut() says whether it is current. */
    juce::File getBounceFile() const;

    /** The file the chip hands out: the bounce if it is of what is drawn right now (checked
        first, so an edit a moment ago counts), else none. */
    juce::File getFileToHandOut();

    /** "Bounce WAV": renders the current drawing to a new file in the background, and from now
        on (until the panel goes) renders again by itself after every change. */
    void startBounce();

    /** True while a render is running. */
    bool isBouncing() const noexcept;

    /** True once "Bounce WAV" has been pressed: the file follows the drawing from then on. */
    bool isFollowing() const noexcept;

    /** True while the newest bounce matches what is drawn now (plan, length, level, sample
        rate): false from the moment of an edit until the new file is in. */
    bool hasCurrentFile() const;

    /** What the chip says: the file's name when it is current, "Updating..." while a new one is
        on its way, "Nothing drawn" for an empty drawing; empty while the button shows. */
    juce::String getChipText() const;

    /** The file the running render writes (it exists, empty, from the start: the name is
        reserved), or none. */
    juce::File getRenderingFile() const;

    /** Copies the bounce to `target`: now if it is current, else as soon as the update that is
        on its way is ready. What "Save..." does with the chosen file. */
    void saveTo (const juce::File& target);

    /** Picks up a finished render, notices changes to the drawing (the chip then shows
        "Updating..."), and starts the next render once they settle. The editor calls this from
        its timer. */
    void refresh();

    /** For the snapshot tool and tests: blocks until the running bounce (if any) has finished,
        then refreshes. Returns false on timeout. */
    bool waitForBounce (int timeoutMilliseconds);

    /** The last error, empty if the last bounce worked. */
    juce::String getLastError() const;

    /** FreqArt FX in Carve or Stencil: the column asks for a bounce in the DAW instead (nothing
        can be bounced here without the song). */
    bool isAskingForDawBounce() const noexcept { return look == Look::daw; }

    static constexpr const char* dawBounceText = "Bounce the track in your DAW";

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Chip;
    struct Job;

    /** What a file is rendered from: if any of it changes, the file no longer matches. */
    struct Take
    {
        uint64_t planVersion = 0;   // 0: no settled plan
        double seconds = 0.0;
        float gain = 0.0f;
        double sampleRate = 0.0;

        bool sameAs (const Take& other) const noexcept;
    };

    enum class Look
    {
        button,     // "Bounce WAV" (or "Rendering..." for the first file, or after an error)
        ready,      // the chip with the current file
        updating,   // the chip waiting for the next file
        empty,      // the chip with nothing to export
        daw         // FreqArt FX carving: "Bounce the track in your DAW"
    };

    Take takeFor (const ArtPlan& plan) const;
    void startRender (std::shared_ptr<const ArtPlan> plan, const Take& take);
    void stopRender();
    void collectFinishedRenders();
    void finishPendingSave();
    bool isOnShow() const;
    bool needsTheSong() const;
    void updateLook();
    juce::String hintText() const;
    void chooseSaveTarget();
    void discardBounceFile();

    FreqArtProcessor& processor;
    IconButton bounceButton;
    std::unique_ptr<Chip> chip;
    ShellButton saveButton { "Save..." };
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<Job> job;                       // the render in progress
    std::vector<std::unique_ptr<Job>> stopping;     // renders told to stop, until their threads end

    // The newest finished file, what it was rendered from, and whether it was dragged out (a
    // DAW may then be reading it where it is, so it is never deleted from here: the clean-up
    // when an editor opens removes it after a day).
    juce::File bounceFile;
    Take bounceTake;
    bool bounceDraggedOut = false;

    bool following = false;           // "Bounce WAV" was pressed: keep the file up to date
    bool fileReady = false;           // bounceFile is of what is drawn now
    bool settledSilent = false;       // the drawing is settled and empty
    std::optional<Take> failedTake;   // not tried again by itself (it would fail again)
    Take lastSeen;                    // the drawing as the last refresh saw it
    juce::uint32 lastChange = 0;      // when it last changed (ms counter)
    juce::File pendingSave;           // "Save..." chosen while an update was on its way

    Look look = Look::button;
    juce::String lastError;           // of the last render (the button shows again)
    juce::String saveError;           // of the last "Save..." (the chip stays)
    juce::String shownHint;
    float shownProgress = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportPanel)
};

} // namespace freqart::ui::shell
