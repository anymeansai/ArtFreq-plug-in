#include "FastPreview.h"

namespace artfreq::ui
{

bool FastRequest::operator== (const FastRequest& other) const noexcept
{
    return effect == other.effect && plan == other.plan && model == other.model && width == other.width && height == other.height
        && measured == other.measured && measuredPlan == other.measuredPlan && juce::exactlyEqual (measuredGain, other.measuredGain)
        && (! effect || (settings == other.settings && masks == other.masks && song == other.song));
}

//==============================================================================
FastPreview::FastPreview (std::function<void()> onNewPictureToUse)
    : juce::Thread ("ArtFreq fast preview"), onNewPicture (std::move (onNewPictureToUse))
{
}

FastPreview::~FastPreview()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
    cancelPendingUpdate();
}

juce::Image FastPreview::make (const FastRequest& r)
{
    const std::lock_guard<std::mutex> hold (makers);
    planImage.setMeasuredLows (r.measured, r.measuredPlan, r.measuredGain);

    if (! r.effect)
        return planImage.get (r.plan, r.model, r.width, r.height);

    // ArtFreq FX: over the song, as the mode will show it (the sines' levels only in the modes
    // that play them).
    if (playsSines (r.settings.mode))
        planImage.prepareLevels (r.plan, r.model, r.width, r.height);

    effectImage.setMeasuredSong (r.song);
    return effectImage.get (planImage.getPixelDbBeforeLevel(), planImage.getLevelDb(), planImage.getRenderCount(),
                            r.settings, r.masks, r.model.scale, r.width, r.height);
}

juce::Image FastPreview::get (const FastRequest& request, bool atOnce)
{
    bool haveAny = false;

    {
        const juce::ScopedLock sl (lock);

        if (hasNewest && newestRequest == request)
        {
            current = true;
            return newest;
        }

        haveAny = hasNewest;
    }

    if (atOnce || ! haveAny)
    {
        // Here and now (a picture being made in the background is finished first).
        auto picture = make (request);
        const juce::ScopedLock sl (lock);
        newest = picture;
        newestRequest = request;
        hasNewest = true;

        if (hasWanted && wanted == request)
            hasWanted = false;

        current = true;
        return picture;
    }

    {
        const juce::ScopedLock sl (lock);

        if (! (making && inProgress == request) && ! (hasWanted && wanted == request))
        {
            wanted = request;
            hasWanted = true;
            wake.signal();
        }
    }

    if (! isThreadRunning())
        startThread (juce::Thread::Priority::normal);

    const juce::ScopedLock sl (lock);
    current = false;
    return newest;
}

void FastPreview::run()
{
    while (! threadShouldExit())
    {
        wake.wait (-1);

        while (! threadShouldExit())
        {
            FastRequest request;

            {
                const juce::ScopedLock sl (lock);

                if (! hasWanted)
                    break;

                request = wanted;
                hasWanted = false;
                inProgress = request;
                making = true;
            }

            auto picture = make (request);

            {
                const juce::ScopedLock sl (lock);
                newest = picture;
                newestRequest = request;
                hasNewest = true;
                making = false;
                inProgress = {};
            }

            ++backgroundPictures;
            triggerAsyncUpdate();
        }
    }
}

void FastPreview::handleAsyncUpdate()
{
    if (onNewPicture != nullptr)
        onNewPicture();
}

} // namespace artfreq::ui
