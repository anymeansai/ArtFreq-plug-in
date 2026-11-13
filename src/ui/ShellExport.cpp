#include "ShellExport.h"

#include "dsp/WavExporter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace freqart::ui::shell
{
namespace
{
    constexpr int captionHeight = 12;
    constexpr int rowTop = 24;
    constexpr int hintTop = 64;
    constexpr int hintHeight = 16;
    constexpr int saveGap = 4;
    constexpr float chipTextLeft = 27.0f;
    constexpr float minimumTextScale = 0.85f;   // how far a chip line may be narrowed before "..."

    juce::String ellipsis()   { return juce::String::fromUTF8 ("\xe2\x80\xa6"); }

    juce::String secondsText (double seconds)
    {
        return formatNumber (seconds, seconds < 10.0 ? 2 : 1) + " s";
    }

    // Every name a bounce has had: FreqArt.wav (0.1.0), FreqArt-<uuid>.wav (0.1.1), the
    // readable names (see reserveBounceFile), and the temporary files written beside them.
    constexpr const char* bouncePattern = "FreqArt*.wav";

    /** Bounces are kept for dragging (a DAW may read one where it is) and a crash can leave one
        behind, so the folder is swept when an editor opens: anything a day old goes. */
    void removeStaleBounces()
    {
        const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (1.0);

        for (const auto& file : ExportPanel::getBounceFolder().findChildFiles (juce::File::findFiles, false, bouncePattern))
            if (file.getLastModificationTime() < cutoff)
                file.deleteFile();
    }

    bool isTimeStamp (const juce::String& text)
    {
        if (text.length() != 8)
            return false;

        for (int i = 0; i < 8; ++i)
            if ((i == 2 || i == 5) ? text[i] != '-' : ! juce::CharacterFunctions::isDigit (text[i]))
                return false;

        return true;
    }

    /** The chip's two lines for a bounce file: "FreqArt HELLO 14-03-27 2.wav" shows as
        "FreqArt HELLO" over "14-03-27 2.wav", so the part that tells files apart is never cut. */
    std::pair<juce::String, juce::String> chipLines (const juce::File& file)
    {
        const auto name = file.getFileNameWithoutExtension();

        // From the end: the text before it may hold something that looks like a time too.
        for (auto i = name.length() - 8; i > 0; --i)
            if (name[i - 1] == ' ' && isTimeStamp (name.substring (i, i + 8)))
                return { name.substring (0, i - 1), name.substring (i) + file.getFileExtension() };

        return { name, file.getFileExtension() };
    }
} // namespace

//==============================================================================
bool ExportPanel::Take::sameAs (const Take& other) const noexcept
{
    // A host's tempo can wobble in its last digits from block to block: that is not a change
    // (1e-5 of a pass is under a millisecond for any length up to a minute and a half).
    return planVersion != 0
        && planVersion == other.planVersion
        && std::abs (seconds - other.seconds) <= 1.0e-5 * std::max (seconds, other.seconds)
        && std::abs (gain - other.gain) < 1.0e-6f
        && std::abs (sampleRate - other.sampleRate) < 1.0e-6;
}

//==============================================================================
/** Renders one bounce. The plan is shared and immutable, so the thread needs nothing else from
    the processor. */
struct ExportPanel::Job final : public juce::Thread
{
    Job (std::shared_ptr<const ArtPlan> planToRender, ExportSettings settingsToUse, juce::File targetFile, Take takeToRender)
        : juce::Thread ("FreqArt bounce"),
          plan (std::move (planToRender)),
          settings (settingsToUse),
          target (std::move (targetFile)),
          take (takeToRender)
    {
    }

    ~Job() override
    {
        // Asks a running render to stop (it then leaves `target` as it was) and waits for the
        // thread: the render checks once per block, so this takes milliseconds.
        signalThreadShouldExit();
        waitForThreadToExit (-1);
    }

    void run() override
    {
        // The reserved file is replaced only when the render is complete (renderPlanToWav
        // writes a temporary file beside it and moves it into place).
        juce::String error;
        const auto ok = renderPlanToWav (*plan, settings, target, error,
                                         [this] { return threadShouldExit(); },
                                         [this] (double fraction) { progress.store (static_cast<float> (fraction), std::memory_order_relaxed); });

        if (! ok && error.isEmpty())
            error = "Cannot write " + target.getFullPathName();

        succeeded = ok;
        errorText = error;
        done.store (true, std::memory_order_release);
    }

    const std::shared_ptr<const ArtPlan> plan;
    const ExportSettings settings;
    const juce::File target;
    const Take take;

    // Written by the thread before `done`, read by the message thread after it.
    bool succeeded = false;
    juce::String errorText;
    std::atomic<bool> done { false };
    std::atomic<float> progress { 0.0f };   // 0..1, for the chip's progress line
};

//==============================================================================
/** The file to drag into the DAW, named on the chip. While a new file is on its way it says
    "Updating..." and cannot be dragged. */
class ExportPanel::Chip final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    explicit Chip (ExportPanel& ownerToUse)
        : owner (ownerToUse)
    {
        setComponentID ("export-chip");
    }

    /** Follows the panel: its look and file. */
    void update()
    {
        const auto ready = owner.look == Look::ready;
        const auto text = owner.getChipText();

        if (text == getTitle() && ready == shownReady)
            return;

        shownReady = ready;
        setTitle (text);   // what a screen reader (and a test) reads
        setMouseCursor (ready ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        setTooltip (ready ? "Drag " + text + " into your DAW (or onto the desktop). It follows your changes: "
                            "edit the drawing and a new file takes its place."
                          : owner.look == Look::updating ? "A new file of what is drawn now is on its way"
                                                         : "Draw something to export");
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto ready = owner.look == Look::ready;
        const auto hot = ready && isMouseOverOrDragging();

        if (ready)
        {
            g.setColour (theme::accent2.withAlpha (hot ? 0.2f : 0.12f));
            g.fillRoundedRectangle (bounds, theme::controlCorner);
            g.setColour (theme::accent2.withAlpha (hot ? 0.8f : 0.5f));
        }
        else
        {
            g.setColour (theme::raised);
            g.fillRoundedRectangle (bounds, theme::controlCorner);
            g.setColour (theme::line);
        }

        g.drawRoundedRectangle (bounds, theme::controlCorner, 1.0f);

        const auto iconTop = static_cast<float> ((getHeight() - 16) / 2);
        drawIcon (g, shellIcons::waveFile(), { 7.0f, iconTop, 16.0f, 16.0f }, ready ? theme::accent2 : theme::textFaint);

        const auto text = getLocalBounds().toFloat().withTrimmedLeft (chipTextLeft).withTrimmedRight (5.0f);

        if (ready)
        {
            // The name over the time it was made, the part that tells two bounces apart.
            const auto [name, stamp] = chipLines (owner.bounceFile);
            const auto middle = text.getCentreY();

            g.setColour (theme::text);
            g.setFont (fonts::ui (11.5f, fonts::UiWeight::medium));
            g.drawFittedText (name, text.withBottom (middle + 1.0f).withTrimmedTop (2.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, minimumTextScale);

            g.setColour (theme::textDim);
            g.setFont (fonts::mono (10.0f));
            g.drawFittedText (stamp, text.withTop (middle + 1.0f).withTrimmedBottom (3.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, minimumTextScale);
            return;
        }

        g.setColour (theme::textDim);
        g.setFont (fonts::ui (12.0f, fonts::UiWeight::medium));
        g.drawFittedText (owner.getChipText(), text.toNearestInt(), juce::Justification::centredLeft, 1, minimumTextScale);

        // How far the render has got: a thin line along the bottom.
        if (owner.job != nullptr && owner.look == Look::updating)
        {
            const auto track = bounds.reduced (theme::controlCorner, 0.0f).removeFromBottom (4.0f).removeFromTop (2.0f);
            const auto done = juce::jlimit (0.0f, 1.0f, owner.job->progress.load (std::memory_order_relaxed));
            g.setColour (theme::accent2.withAlpha (0.8f));
            g.fillRect (track.withWidth (track.getWidth() * done));
        }
    }

    void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { repaint(); }

    void mouseDown (const juce::MouseEvent&) override
    {
        // Only a press on a finished file starts a drag: one on "Updating..." does not turn into
        // a drag of whatever arrives while the button is held.
        pressedOnFile = owner.look == Look::ready;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || ! pressedOnFile || e.getDistanceFromDragStart() < 4)
            return;

        // Asked now, not at the last timer tick: after an edit a moment ago the file is old.
        const auto file = owner.getFileToHandOut();

        if (! file.existsAsFile())
            return;

        // From now on the file may be in use outside (and the drag itself reads it), so the
        // panel never deletes it.
        owner.bounceDraggedOut = true;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                   [safe = juce::Component::SafePointer<Chip> (this)]
                                                                   {
                                                                       if (safe != nullptr)
                                                                           safe->dragging = false;
                                                                   });
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        pressedOnFile = false;
        repaint();
    }

private:
    ExportPanel& owner;
    bool dragging = false;
    bool pressedOnFile = false;
    bool shownReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chip)
};

//==============================================================================
ExportPanel::ExportPanel (FreqArtProcessor& p)
    : processor (p),
      bounceButton ("Bounce WAV", icons::download(), "Bounce WAV"),
      chip (std::make_unique<Chip> (*this))
{
    removeStaleBounces();

    saveButton.setButtonText ("Save" + ellipsis());
    saveButton.setTooltip ("Save a copy of the current file");
    saveButton.setComponentID ("export-save");
    saveButton.onClick = [this] { chooseSaveTarget(); };

    bounceButton.setTooltip ("Render one pass of the drawing to a WAV file. From then on the file follows your changes.");
    bounceButton.setComponentID ("bounce");
    bounceButton.onClick = [this] { startBounce(); };

    addAndMakeVisible (bounceButton);
    addChildComponent (*chip);
    addChildComponent (saveButton);

    setComponentID ("export");
    chip->update();
    refresh();
}

ExportPanel::~ExportPanel()
{
    // Every render is told to stop before any is waited for: each stops at its next block, so
    // closing the window takes milliseconds whatever is rendering. None of their files (a
    // reserved name, or one finished a moment ago) was handed out.
    if (job != nullptr)
        stopRender();

    for (auto& stopped : stopping)
        stopped->signalThreadShouldExit();

    for (auto& stopped : stopping)
    {
        stopped->waitForThreadToExit (-1);
        stopped->target.deleteFile();
    }

    stopping.clear();
    discardBounceFile();
}

juce::File ExportPanel::getBounceFolder()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("FreqArt");
}

juce::File ExportPanel::getBounceFile() const
{
    return bounceFile;
}

juce::File ExportPanel::getFileToHandOut()
{
    refresh();
    return fileReady ? bounceFile : juce::File();
}

juce::File ExportPanel::getRenderingFile() const
{
    return job != nullptr ? job->target : juce::File();
}

void ExportPanel::discardBounceFile()
{
    if (bounceFile != juce::File() && ! bounceDraggedOut)
        bounceFile.deleteFile();

    bounceFile = juce::File();
    bounceTake = {};
    bounceDraggedOut = false;
    fileReady = false;
}

ExportPanel::Take ExportPanel::takeFor (const ArtPlan& plan) const
{
    Take take;
    take.planVersion = plan.version;
    take.seconds = processor.getLengthSeconds();
    take.gain = processor.getOutputGain();
    take.sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    return take;
}

void ExportPanel::startBounce()
{
    following = true;
    failedTake.reset();
    lastError.clear();

    processor.rebuildPlanIfNeeded();
    auto plan = processor.getCurrentPlan();

    if (plan == nullptr)
    {
        lastError = "Nothing to bounce yet";
        refresh();
        return;
    }

    const auto wanted = takeFor (*plan);
    lastSeen = wanted;
    lastChange = juce::Time::getMillisecondCounter();

    if (job != nullptr && ! job->take.sameAs (wanted))
        stopRender();

    // Asked for, so rendered at once (no wait for the drawing to settle).
    const auto current = bounceTake.sameAs (wanted) && bounceFile.existsAsFile();

    if (job == nullptr && ! plan->silent && ! current)
        startRender (std::move (plan), wanted);

    refresh();
    repaint();
}

void ExportPanel::startRender (std::shared_ptr<const ArtPlan> plan, const Take& take)
{
    // Named after the drawing as it is now, which is what the plan was built from (no edit is
    // waiting for the plan when a render starts).
    const auto folder = getBounceFolder();
    const auto target = reserveBounceFile (folder, bounceLabel (processor.getDocument()), juce::Time::getCurrentTime());

    if (target == juce::File())
    {
        lastError = "Cannot write to " + folder.getFullPathName();
        failedTake = take;
        return;
    }

    ExportSettings settings;
    settings.sampleRate = take.sampleRate;
    settings.seconds = take.seconds;
    settings.gain = take.gain;

    lastError.clear();
    job = std::make_unique<Job> (std::move (plan), settings, target, take);

    if (! job->startThread (juce::Thread::Priority::low))
    {
        job.reset();
        target.deleteFile();
        lastError = "Cannot start the bounce";
        failedTake = take;
    }
}

void ExportPanel::stopRender()
{
    // It stops at its next block and is collected once its thread has ended, so the message
    // thread never waits for it.
    job->signalThreadShouldExit();
    stopping.push_back (std::move (job));
}

void ExportPanel::collectFinishedRenders()
{
    if (job != nullptr && job->done.load (std::memory_order_acquire))
    {
        job->waitForThreadToExit (-1);   // run() has returned: at once

        if (job->succeeded)
        {
            // The new file takes over the chip; the one before goes (unless it was dragged out).
            discardBounceFile();
            bounceFile = job->target;
            bounceTake = job->take;
            failedTake.reset();
            saveError.clear();
        }
        else
        {
            job->target.deleteFile();   // its reserved name, never handed out
            lastError = job->errorText;
            failedTake = job->take;
        }

        job.reset();
    }

    // Stopped renders: what they leave (a reserved name, or a file finished just before the
    // stop) was never handed out.
    for (auto i = stopping.begin(); i != stopping.end();)
    {
        if ((*i)->done.load (std::memory_order_acquire))
        {
            (*i)->waitForThreadToExit (-1);
            (*i)->target.deleteFile();
            i = stopping.erase (i);
        }
        else
        {
            ++i;
        }
    }
}

bool ExportPanel::isBouncing() const noexcept
{
    return job != nullptr;
}

bool ExportPanel::isFollowing() const noexcept
{
    return following;
}

bool ExportPanel::hasCurrentFile() const
{
    // Asked of the processor now, not taken from the last refresh.
    const auto plan = processor.getCurrentPlan();
    return plan != nullptr && ! processor.isPlanPending() && bounceTake.sameAs (takeFor (*plan)) && bounceFile.existsAsFile();
}

juce::String ExportPanel::getChipText() const
{
    switch (look)
    {
        case Look::ready:    return bounceFile.getFileName();
        case Look::updating: return "Updating" + ellipsis();
        case Look::empty:    return "Nothing drawn";
        case Look::daw:
        case Look::button:   break;
    }

    return {};
}

juce::String ExportPanel::getLastError() const
{
    return lastError.isNotEmpty() ? lastError : saveError;
}

bool ExportPanel::waitForBounce (int timeoutMilliseconds)
{
    if (job != nullptr && ! job->waitForThreadToExit (timeoutMilliseconds))
        return false;

    refresh();
    return true;
}

bool ExportPanel::needsTheSong() const
{
    return processor.isEffect() && ! playsSines (processor.getEffectMode());
}

bool ExportPanel::isOnShow() const
{
    // Renders start by themselves only while the panel can be seen: everything up to the window
    // visible, and the window not minimised. (With no window at all, as in the tests and the
    // snapshot tool, a visible editor counts as seen.)
    for (auto* c = static_cast<const juce::Component*> (this); c != nullptr; c = c->getParentComponent())
        if (! c->isVisible())
            return false;

    if (auto* peer = getPeer())
        return ! peer->isMinimised();

    return true;
}

void ExportPanel::refresh()
{
    collectFinishedRenders();

    const auto plan = processor.getCurrentPlan();
    const auto now = juce::Time::getMillisecondCounter();

    // Settled: the plan is of what is drawn now. Right after an edit it is not (the processor's
    // timer rebuilds it within ~40 ms), and nothing is handed out until it is.
    const auto settled = plan != nullptr && ! processor.isPlanPending();
    const auto wanted = settled ? takeFor (*plan) : Take {};

    // Any change, and every moment an edit waits for its plan, starts the wait again.
    if (! settled || ! wanted.sameAs (lastSeen))
    {
        lastSeen = wanted;
        lastChange = now;
    }

    // A render of something no longer drawn is of no use: stop it (it leaves nothing behind).
    // While an edit waits for its plan the render goes on: the edit may change nothing.
    if (job != nullptr && settled && ! job->take.sameAs (wanted))
        stopRender();

    fileReady = settled && bounceTake.sameAs (wanted) && bounceFile.existsAsFile();
    settledSilent = settled && plan->silent;

    // Following the drawing: once it has stayed the same for a moment, render it (but not
    // again something that has just failed: it would fail again, over and over).
    if (following && job == nullptr && settled && ! settledSilent && ! fileReady && ! needsTheSong()
        && ! (failedTake.has_value() && failedTake->sameAs (wanted))
        && now - lastChange >= static_cast<juce::uint32> (updateDelayMilliseconds)
        && isOnShow())
    {
        startRender (plan, wanted);
    }

    finishPendingSave();
    updateLook();
}

void ExportPanel::updateLook()
{
    // The chip, once there has been a file, until a render fails: then the button again, to
    // try once more, under the error.
    auto newLook = Look::button;

    if (following && lastError.isEmpty() && bounceFile != juce::File())
        newLook = fileReady ? Look::ready : settledSilent ? Look::empty : Look::updating;

    // Carving needs the song: whatever was bounced before waits for a Draw mode.
    if (needsTheSong())
        newLook = Look::daw;

    const auto plan = processor.getCurrentPlan();
    const auto canBounce = plan != nullptr && ! plan->silent && job == nullptr;
    const auto changed = newLook != look || bounceButton.isEnabled() != canBounce;
    look = newLook;

    if (changed)
    {
        bounceButton.setEnabled (canBounce);
        bounceButton.setVisible (look == Look::button);
        chip->setVisible (look != Look::button && look != Look::daw);
        saveButton.setVisible (look != Look::button && look != Look::daw);
        repaint();
    }

    saveButton.setEnabled (look == Look::ready);
    bounceButton.setLabel (job != nullptr ? "Rendering" + ellipsis() : juce::String ("Bounce WAV"));
    chip->update();

    // The progress line moves while a render runs.
    const auto progress = job != nullptr && look == Look::updating ? job->progress.load (std::memory_order_relaxed) : -1.0f;

    if (std::abs (progress - shownProgress) >= 0.01f || (progress < 0.0f) != (shownProgress < 0.0f))
    {
        shownProgress = progress;
        chip->repaint();
    }

    // The hint follows the length too (tempo changes while the host plays).
    const auto hint = hintText();

    if (changed || hint != shownHint)
    {
        shownHint = hint;
        repaint (0, hintTop, getWidth(), hintHeight);
    }
}

juce::String ExportPanel::hintText() const
{
    if (saveError.isNotEmpty() && look != Look::button)
        return saveError;

    switch (look)
    {
        case Look::ready:    return "Drag into your DAW";
        case Look::empty:    return "Draw something first";
        case Look::updating: return "Rendering " + secondsText (job != nullptr ? job->settings.seconds : processor.getLengthSeconds()) + ellipsis();
        case Look::daw:      return "Carving needs the song";
        case Look::button:   break;
    }

    if (job != nullptr)
        return "Rendering " + secondsText (job->settings.seconds) + ellipsis();

    if (lastError.isNotEmpty())
        return lastError;

    const auto plan = processor.getCurrentPlan();

    if (plan == nullptr || plan->silent)
        return "Draw something first";

    // The effect bounces the drawing's sines, not the song.
    return (processor.isEffect() ? "The sines, one pass, " : "One pass, ") + secondsText (processor.getLengthSeconds());
}

void ExportPanel::saveTo (const juce::File& target)
{
    pendingSave = target;
    refresh();
}

void ExportPanel::finishPendingSave()
{
    if (pendingSave == juce::File())
        return;

    if (fileReady)
    {
        const auto target = std::exchange (pendingSave, juce::File());

        if (bounceFile.copyFileTo (target))
            saveError.clear();
        else
            saveError = "Could not save " + target.getFileName();

        return;
    }

    // Nothing is on its way (an empty drawing, or a render that failed): say so rather than
    // wait for ever.
    if (settledSilent || ! following || lastError.isNotEmpty())
    {
        saveError = "Nothing to save: " + pendingSave.getFileName();
        pendingSave = juce::File();
    }
}

void ExportPanel::chooseSaveTarget()
{
    const auto name = bounceFile != juce::File() ? bounceFile.getFileName() : juce::String ("FreqArt.wav");
    const auto start = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (name);
    chooser = std::make_unique<juce::FileChooser> ("Save the bounce", start, "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<ExportPanel> (this)] (const juce::FileChooser& fc)
                          {
                              const auto result = fc.getResult();

                              if (safe == nullptr || result == juce::File())
                                  return;

                              // What is drawn when the choice is made, not when the chooser
                              // opened (automation may have moved the level meanwhile).
                              safe->saveTo (result.hasFileExtension ("wav") ? result : result.withFileExtension ("wav"));
                          });
}

void ExportPanel::paint (juce::Graphics& g)
{
    drawCaps (g, "Export", { 0, 0, getWidth(), captionHeight }, juce::Justification::centredLeft, theme::textDim);

    const auto isError = (look == Look::button && lastError.isNotEmpty() && job == nullptr)
                      || (look != Look::button && saveError.isNotEmpty());
    g.setFont (fonts::ui (theme::labelSize));
    g.setColour (isError ? theme::danger : look == Look::ready ? theme::accent2 : theme::textFaint);
    g.drawText (hintText(), 0, hintTop, getWidth(), hintHeight, juce::Justification::centredLeft, true);

    // FreqArt FX carving: where the bounce happens instead, in a quiet box where the button was.
    if (look == Look::daw)
    {
        const auto row = juce::Rectangle<int> (0, rowTop, getWidth(), metrics::controlHeight).toFloat().reduced (0.5f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (row, theme::controlCorner, 1.0f);

        g.setColour (theme::textDim);
        g.setFont (fonts::ui (12.0f, fonts::UiWeight::medium));
        g.drawFittedText (dawBounceText, row.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, minimumTextScale);
    }
}

void ExportPanel::resized()
{
    const auto row = juce::Rectangle<int> (0, rowTop, getWidth(), metrics::controlHeight);
    const auto saveWidth = saveButton.getIdealWidth() - 12;   // a compact button: the chip needs the room

    bounceButton.setBounds (row);
    saveButton.setBounds (row.withLeft (row.getRight() - saveWidth));
    chip->setBounds (row.withRight (saveButton.getX() - saveGap));
}

} // namespace freqart::ui::shell
