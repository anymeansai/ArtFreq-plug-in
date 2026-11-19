#pragma once

// The fast preview, made off the message thread.
//
// The fast preview (CanvasPlanImage; ArtFreq FX: EffectPreviewImage over it) is what the canvas
// shows while the drawing or a setting changes, until the measured preview of the real sound is
// in (MeasuredPreview.h). Modelling an analyser well enough to look like the measured picture
// costs 10 to 80 ms a picture on a dense project at Retina sizes: too much for the message thread
// on every step of a drag, a brush stroke or a key. So a thread of its own makes it: the canvas
// asks for the picture of what is drawn now and is given the newest one there is at once (the
// last step's, a frame or two old while the next is made); when the new one is in, the canvas is
// told and repaints. The editing affordances (outlines, handles, the brush cursor) are drawn by
// the canvas on top as they move, so they follow the mouse exactly; only the picture under them
// trails by a frame. Asked for a picture when it has none (the first paint), it makes it at once.
//
// Where the drawing has not changed near a column since the last measurement (MeasuredPreview.h)
// the sound there is what was measured, so that column shows the measurement, not the model: the
// picture changes only where the drawing does, and the swap to the measured preview is gentle.
//
// Nothing runs while nothing changes; the thread starts with the first picture asked for
// asynchronously (an editor opened and closed at once starts none).

#include "CanvasPlanImage.h"
#include "EffectPreview.h"
#include "MeasuredPreview.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace artfreq::ui
{

/** Everything the fast preview is a picture of. */
struct FastRequest
{
    bool effect = false;
    std::shared_ptr<const ArtPlan> plan;
    AnalyserModel model;
    EffectPreviewImage::Settings settings;             // ArtFreq FX
    std::shared_ptr<const MaskPlanSet> masks;          // ArtFreq FX
    std::shared_ptr<const MeasuredSpectrogram> song;   // ArtFreq FX: the song as last measured (null: the stand-in's texture)

    // The last measured preview and what it was of, when it was made with this request's scale,
    // slope, FFT, rate, length and height: shown where the drawing did not change near since
    // (makePatch), so the picture changes only where the drawing does.
    std::shared_ptr<const MeasuredSpectrogram> measured;
    MeasureRequest measuredRequest;

    int width = 0, height = 0;

    bool operator== (const FastRequest& other) const noexcept;
    bool operator!= (const FastRequest& other) const noexcept { return ! operator== (other); }
};

class FastPreview final : private juce::Thread,
                          private juce::AsyncUpdater
{
public:
    /** `onNewPicture` is called on the message thread when a picture made in the background is in. */
    explicit FastPreview (std::function<void()> onNewPicture);
    ~FastPreview() override;   // waits for a picture being made (tens of ms at most)

    /** Message thread: the newest picture there is, of `request` if it is in. A request that
        differs from the last is made in the background, unless there is no picture yet or
        `atOnce` (the snapshot tool, tests): then it is made here and returned. */
    juce::Image get (const FastRequest& request, bool atOnce = false);

    /** The picture last returned is of the last request. */
    bool isCurrent() const noexcept { return current; }

    /** Pictures made in the background (tests). */
    int getBackgroundPictures() const noexcept { return backgroundPictures.load(); }

private:
    void run() override;
    void handleAsyncUpdate() override;
    juce::Image make (const FastRequest& request);

    /** Where `request`'s picture may show its measurement (false: nowhere). */
    static bool makePatch (const FastRequest& request, MeasuredPatch& patch);

    std::function<void()> onNewPicture;

    std::mutex makers;                     // the renderers below, used by one picture at a time
    CanvasPlanImage planImage;
    EffectPreviewImage effectImage;
    MeasuredPatch patch;

    juce::CriticalSection lock;            // the hand-over
    FastRequest wanted;                    // the newest asked for, for the thread
    bool hasWanted = false;
    juce::Image newest;                    // the newest picture made, and what it is of
    FastRequest newestRequest;
    bool hasNewest = false;
    FastRequest inProgress;                // what the thread is making
    bool making = false;

    bool current = false;                  // message thread
    std::atomic<int> backgroundPictures { 0 };
    juce::WaitableEvent wake;
};

} // namespace artfreq::ui
