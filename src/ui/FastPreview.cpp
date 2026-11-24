#include "FastPreview.h"

#include "dsp/WavExporter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace artfreq::ui
{

bool FastRequest::operator== (const FastRequest& other) const noexcept
{
    return effect == other.effect && plan == other.plan && model == other.model && width == other.width && height == other.height
        && measured == other.measured
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

bool FastPreview::makePatch (const FastRequest& r, MeasuredPatch& patch)
{
    const auto* analysis = r.measured.get();
    const auto& m = r.measuredRequest;

    patch.shiftDb = 0.0f;

    if (analysis == nullptr || analysis->rows != r.height || analysis->frames <= 0 || r.width <= 0 || m.effect != r.effect
        || r.plan == nullptr)
        return false;

    // Which of the drawing's columns sound different now, by the plan (the sines) and, ArtFreq
    // FX, the mask its mode carves with.
    const auto numColumns = r.plan->numColumns;
    std::vector<uint8_t> changed (static_cast<size_t> (std::max (0, numColumns)), 0);
    auto scale = 1.0;   // the sines' loudness as a whole (the plan's normalisation)
    const auto sines = ! r.effect || playsSines (r.settings.mode);

    if (r.effect)
    {
        // The same mode, Depth and (where the sines play) Level, or the measurement is of another sound.
        if (m.mode != r.settings.mode || ! juce::exactlyEqual (m.depthDb, previewDepthDb (r.settings.depthDb))
            || (sines && ! juce::exactlyEqual (m.outputGain, r.model.outputGain)))
            return false;

        if (carvesAudio (r.settings.mode))
        {
            if (r.masks == nullptr || m.masks == nullptr)
                return false;

            const auto* now = r.settings.mode == EffectMode::carveDraw ? r.masks->halo.get() : r.masks->exact.get();
            const auto* then = r.settings.mode == EffectMode::carveDraw ? m.masks->halo.get() : m.masks->exact.get();

            if (now == nullptr || then == nullptr || now->numColumns != numColumns || then->numColumns != numColumns
                || now->numRows != then->numRows || ! (now->scale == then->scale))
                return false;

            if (now != then)
                for (int c = 0; c < numColumns; ++c)
                    changed[static_cast<size_t> (c)] = std::memcmp (now->column (c), then->column (c), sizeof (float) * static_cast<size_t> (now->numRows)) != 0;
        }
    }

    if (sines)
    {
        const auto& a = *r.plan;
        const auto* bPlan = m.plan.get();

        if (bPlan == nullptr)
            return false;

        const auto& b = *bPlan;

        if (a.numLines != b.numLines || a.numColumns != b.numColumns || a.layoutHash != b.layoutHash || a.combLines != b.combLines
            || a.amplitudes.size() != b.amplitudes.size())
            return false;

        if (&a != &b)
        {
            // The plan is normalised as a whole (its loudest column), so a shape moved elsewhere
            // can change every amplitude by one factor: the columns' most common ratio.
            std::vector<double> ratios;
            ratios.reserve (static_cast<size_t> (numColumns));
            auto loudest = 0.0f;

            for (int c = 0; c < numColumns; ++c)
            {
                double sumA = 0.0, sumB = 0.0;
                const auto* ca = a.amplitudes.data() + static_cast<size_t> (c) * static_cast<size_t> (a.numLines);
                const auto* cb = b.amplitudes.data() + static_cast<size_t> (c) * static_cast<size_t> (b.numLines);

                for (int line = 0; line < a.numLines; ++line)
                {
                    sumA += ca[line];
                    sumB += cb[line];
                    loudest = std::max (loudest, ca[line]);
                }

                if (sumA > 0.0 && sumB > 0.0)
                    ratios.push_back (sumA / sumB);
            }

            if (! ratios.empty())
            {
                std::nth_element (ratios.begin(), ratios.begin() + static_cast<std::ptrdiff_t> (ratios.size() / 2), ratios.end());
                scale = ratios[ratios.size() / 2];
            }

            const auto tolerance = 1.0e-4f * loudest;

            for (int c = 0; c < numColumns; ++c)
            {
                const auto* ca = a.amplitudes.data() + static_cast<size_t> (c) * static_cast<size_t> (a.numLines);
                const auto* cb = b.amplitudes.data() + static_cast<size_t> (c) * static_cast<size_t> (b.numLines);

                for (int line = 0; line < a.numLines && changed[static_cast<size_t> (c)] == 0; ++line)
                    if (std::abs (ca[line] - static_cast<float> (scale) * cb[line]) > tolerance)
                        changed[static_cast<size_t> (c)] = 1;
            }
        }

        if (! r.effect)
        {
            // Level, and the loudness as a whole: as many dB, while the clipper stays out of it.
            if (! (m.outputGain > 0.0f) || ! (r.model.outputGain > 0.0f) || ! (scale > 0.0))
                return false;

            const auto gain = scale * r.model.outputGain / m.outputGain;

            if (analysis->peak * gain > softClipThreshold)
                return false;

            patch.shiftDb = static_cast<float> (20.0 * std::log10 (gain));
        }
        else if (! (std::abs (20.0 * std::log10 (std::max (1.0e-6, scale))) <= 0.5))
        {
            return false;   // the sines over the song: no longer one shift (within half a dB, near enough)
        }
    }

    // A change reaches as far as the analyser's window (and the measurement's spacing, and in
    // ArtFreq FX the carver's frame) either side: those columns are the model's; then a blend over
    // as many again.
    const auto samplesPerColumn = std::max (1.0, r.model.passSeconds * r.model.sampleRate / std::max (1, numColumns));
    auto reachSamples = 0.5 * r.model.view.fftSize + r.model.view.fftSize / 16.0 + 2.0 * samplesPerColumn;

    if (r.effect)
        reachSamples += 4096.0 / 48000.0 * r.model.sampleRate;

    const auto reach = reachSamples / samplesPerColumn;
    std::vector<double> distance (changed.size(), 1.0e9);   // to the nearest changed column, in columns
    auto any = false, anyUnchanged = false;

    for (int c = 0; c < numColumns; ++c)
    {
        any = any || changed[static_cast<size_t> (c)] != 0;
        anyUnchanged = anyUnchanged || changed[static_cast<size_t> (c)] == 0;
    }

    if (! anyUnchanged)
        return false;

    if (any)
    {
        auto last = -1.0e9;

        for (int c = 0; c < numColumns; ++c)
        {
            if (changed[static_cast<size_t> (c)] != 0)
                last = c;

            distance[static_cast<size_t> (c)] = c - last;
        }

        last = 1.0e9;

        for (int c = numColumns - 1; c >= 0; --c)
        {
            if (changed[static_cast<size_t> (c)] != 0)
                last = c;

            distance[static_cast<size_t> (c)] = std::min (distance[static_cast<size_t> (c)], last - c);
        }
    }

    patch.analysis = analysis;
    patch.weight.resize (static_cast<size_t> (r.width));

    for (int x = 0; x < r.width; ++x)
    {
        const auto c = juce::jlimit (0, numColumns - 1, static_cast<int> ((x + 0.5) * numColumns / r.width));
        patch.weight[static_cast<size_t> (x)] = static_cast<float> (juce::jlimit (0.0, 1.0, (distance[static_cast<size_t> (c)] - reach) / reach));
    }

    patch.prepare (r.width);
    return true;
}

juce::Image FastPreview::make (const FastRequest& r)
{
    const std::lock_guard<std::mutex> hold (makers);
    const auto* usePatch = makePatch (r, patch) ? &patch : nullptr;

    if (! r.effect)
        return planImage.get (r.plan, r.model, r.width, r.height, usePatch);

    // ArtFreq FX: over the song, as the mode will show it (the sines' levels only in the modes
    // that play them).
    if (playsSines (r.settings.mode))
        planImage.prepareLevels (r.plan, r.model, r.width, r.height);

    effectImage.setMeasuredSong (r.song);
    return effectImage.get (planImage.getPixelDbBeforeLevel(), planImage.getLevelDb(), planImage.getRenderCount(),
                            r.settings, r.masks, r.model.scale, r.width, r.height, usePatch);
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
