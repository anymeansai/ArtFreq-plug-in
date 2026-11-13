#include "ShellExport.h"

#include "dsp/WavExporter.h"

#include <atomic>

namespace freqart::ui::shell
{
namespace
{
    constexpr int captionHeight = 12;
    constexpr int rowTop = 24;
    constexpr int hintTop = 64;
    constexpr int hintHeight = 16;
    constexpr int saveGap = 4;

    juce::String ellipsis()   { return juce::String::fromUTF8 ("\xe2\x80\xa6"); }

    juce::String secondsText (double seconds)
    {
        return formatNumber (seconds, seconds < 10.0 ? 2 : 1) + " s";
    }
} // namespace

//==============================================================================
/** Renders one bounce. The plan is shared and immutable, so the thread needs nothing else from
    the processor. */
struct ExportPanel::Job final : public juce::Thread
{
    Job (std::shared_ptr<const ArtPlan> planToRender, ExportSettings settingsToUse, juce::File targetFile)
        : juce::Thread ("FreqArt bounce"),
          plan (std::move (planToRender)),
          settings (settingsToUse),
          target (std::move (targetFile))
    {
    }

    ~Job() override
    {
        // The render cannot be interrupted; it is short for any usual length.
        waitForThreadToExit (-1);
    }

    void run() override
    {
        juce::String error;
        auto ok = target.getParentDirectory().createDirectory().wasOk();

        if (! ok)
        {
            error = "Cannot create " + target.getParentDirectory().getFullPathName();
        }
        else
        {
            // Write next to the target and swap it in, so a file a DAW is still reading from an
            // earlier drag is never seen half-written.
            juce::TemporaryFile temp (target);
            ok = renderPlanToWav (*plan, settings, temp.getFile(), error)
              && temp.overwriteTargetFileWithTemporary();

            if (! ok && error.isEmpty())
                error = "Cannot write " + target.getFullPathName();
        }

        succeeded = ok;
        errorText = error;
        done.store (true, std::memory_order_release);
    }

    const std::shared_ptr<const ArtPlan> plan;
    const ExportSettings settings;
    const juce::File target;

    // Written by the thread before `done`, read by the message thread after it.
    bool succeeded = false;
    juce::String errorText;
    std::atomic<bool> done { false };
};

//==============================================================================
/** The finished file: drag it out of the window into the DAW. */
class ExportPanel::Chip final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    Chip()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag into your DAW (or onto the desktop)");
        setTitle ("FreqArt.wav");
        setComponentID ("export-chip");
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const auto hot = isMouseOverOrDragging();

        g.setColour (theme::accent2.withAlpha (hot ? 0.2f : 0.12f));
        g.fillRoundedRectangle (bounds, theme::controlCorner);
        g.setColour (theme::accent2.withAlpha (hot ? 0.8f : 0.5f));
        g.drawRoundedRectangle (bounds, theme::controlCorner, 1.0f);

        const auto iconTop = static_cast<float> ((getHeight() - 16) / 2);
        drawIcon (g, shellIcons::waveFile(), { 8.0f, iconTop, 16.0f, 16.0f }, theme::accent2);

        g.setColour (theme::text);
        g.setFont (fonts::ui (12.0f, fonts::UiWeight::medium));
        g.drawText (getBounceFile().getFileName(), getLocalBounds().withTrimmedLeft (28).withTrimmedRight (4),
                    juce::Justification::centredLeft, true);
    }

    void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 4)
            return;

        const auto file = getBounceFile();

        if (! file.existsAsFile())
            return;

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
        repaint();
    }

private:
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chip)
};

//==============================================================================
ExportPanel::ExportPanel (FreqArtProcessor& p)
    : processor (p),
      bounceButton ("Bounce WAV", icons::download(), "Bounce WAV"),
      chip (std::make_unique<Chip>())
{
    saveButton.setButtonText ("Save" + ellipsis());
    saveButton.setTooltip ("Save a copy of the bounce");
    saveButton.setComponentID ("export-save");
    saveButton.onClick = [this] { saveCopy(); };

    bounceButton.setTooltip ("Render one pass of the drawing to a WAV file");
    bounceButton.setComponentID ("bounce");
    bounceButton.onClick = [this] { startBounce(); };

    addAndMakeVisible (bounceButton);
    addChildComponent (*chip);
    addChildComponent (saveButton);

    setComponentID ("export");
    refresh();
}

ExportPanel::~ExportPanel()
{
    // Waits for a running render (the thread only touches its own copy of everything).
    job.reset();
}

juce::File ExportPanel::getBounceFile()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("FreqArt")
        .getChildFile ("FreqArt.wav");
}

void ExportPanel::startBounce()
{
    if (job != nullptr)
        return;

    processor.rebuildPlanIfNeeded();
    auto plan = processor.getCurrentPlan();

    if (plan == nullptr)
    {
        lastError = "Nothing to bounce yet";
        repaint();
        return;
    }

    ExportSettings settings;
    settings.sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    settings.seconds = processor.getLengthSeconds();
    settings.gain = processor.getOutputGain();

    bouncedPlanVersion = plan->version;
    bouncedSeconds = settings.seconds;
    bouncedGain = settings.gain;
    fileReady = false;
    lastError.clear();

    job = std::make_unique<Job> (std::move (plan), settings, getBounceFile());
    job->startThread (juce::Thread::Priority::low);

    updateVisibility();
    repaint();
}

bool ExportPanel::isBouncing() const noexcept
{
    return job != nullptr;
}

bool ExportPanel::hasCurrentFile() const noexcept
{
    return fileReady;
}

juce::String ExportPanel::getLastError() const
{
    return lastError;
}

bool ExportPanel::waitForBounce (int timeoutMilliseconds)
{
    if (job != nullptr && ! job->waitForThreadToExit (timeoutMilliseconds))
        return false;

    refresh();
    return true;
}

void ExportPanel::refresh()
{
    auto changed = false;

    if (job != nullptr && job->done.load (std::memory_order_acquire))
    {
        job->waitForThreadToExit (-1);
        fileReady = job->succeeded;
        lastError = job->errorText;
        job.reset();
        changed = true;
    }

    const auto plan = processor.getCurrentPlan();

    // A bounce belongs to what was drawn: once the drawing, its length or the level change, the
    // chip would hand out something else, so it goes back to "Bounce WAV".
    if (fileReady)
    {
        const auto current = plan != nullptr
                          && plan->version == bouncedPlanVersion
                          && std::abs (processor.getLengthSeconds() - bouncedSeconds) < 1.0e-6
                          && std::abs (processor.getOutputGain() - bouncedGain) < 1.0e-6f
                          && getBounceFile().existsAsFile();

        if (! current)
        {
            fileReady = false;
            changed = true;
        }
    }

    const auto canBounce = plan != nullptr && ! plan->silent && job == nullptr;

    if (bounceButton.isEnabled() != canBounce)
    {
        bounceButton.setEnabled (canBounce);
        changed = true;
    }

    if (changed)
        updateVisibility();

    // The hint follows the length (tempo changes while the host plays).
    const auto hint = hintText();

    if (changed || hint != shownHint)
    {
        shownHint = hint;
        repaint (0, hintTop, getWidth(), hintHeight);
    }
}

juce::String ExportPanel::hintText() const
{
    if (job != nullptr)
        return "Rendering " + secondsText (job->settings.seconds) + ellipsis();

    if (lastError.isNotEmpty())
        return lastError;

    if (fileReady)
        return "Drag into your DAW";

    const auto plan = processor.getCurrentPlan();

    if (plan == nullptr || plan->silent)
        return "Draw something first";

    return "One pass, " + secondsText (processor.getLengthSeconds());
}

void ExportPanel::updateVisibility()
{
    bounceButton.setVisible (! fileReady);
    chip->setVisible (fileReady);
    saveButton.setVisible (fileReady);

    bounceButton.setLabel (job != nullptr ? "Rendering" + ellipsis() : juce::String ("Bounce WAV"));
}

void ExportPanel::saveCopy()
{
    const auto source = getBounceFile();

    if (! source.existsAsFile())
        return;

    const auto start = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("FreqArt.wav");
    chooser = std::make_unique<juce::FileChooser> ("Save the bounce", start, "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<ExportPanel> (this), source] (const juce::FileChooser& fc)
                          {
                              const auto result = fc.getResult();

                              if (safe == nullptr || result == juce::File())
                                  return;

                              const auto target = result.hasFileExtension ("wav") ? result : result.withFileExtension ("wav");

                              if (! source.copyFileTo (target))
                              {
                                  safe->lastError = "Could not save " + target.getFileName();
                                  safe->refresh();
                                  safe->repaint();
                              }
                          });
}

void ExportPanel::paint (juce::Graphics& g)
{
    drawCaps (g, "Export", { 0, 0, getWidth(), captionHeight }, juce::Justification::centredLeft, theme::textDim);

    const auto isError = lastError.isNotEmpty() && job == nullptr;
    g.setFont (fonts::ui (theme::labelSize));
    g.setColour (isError ? theme::danger : fileReady ? theme::accent2 : theme::textFaint);
    g.drawText (hintText(), 0, hintTop, getWidth(), hintHeight, juce::Justification::centredLeft, true);
}

void ExportPanel::resized()
{
    const auto row = juce::Rectangle<int> (0, rowTop, getWidth(), metrics::controlHeight);
    const auto saveWidth = saveButton.getIdealWidth() - 8;   // a compact button: the chip needs the room

    bounceButton.setBounds (row);
    saveButton.setBounds (row.withLeft (row.getRight() - saveWidth));
    chip->setBounds (row.withRight (saveButton.getX() - saveGap));
}

} // namespace freqart::ui::shell
