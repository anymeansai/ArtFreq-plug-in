#pragma once

// The canvas's measured preview: the real sound, as the user's analyser will show it.
//
// The fast preview (CanvasPlanImage, EffectPreviewImage) draws a model of what an analyser shows
// of the plan, quickly enough to follow a drag. A model leaves things out: the rising comb's
// shimmer, energy spilled where shapes start and stop, sines beating against each other, what
// the carver really leaves in a hole. So, once the drawing and the settings have stayed the same
// for a moment, one pass of the real output is rendered in the background with the same code as
// playback and Bounce WAV (PassRenderer: the engine, Level, the safety clipper; ArtFreq FX: the
// real carver over the user's song as it came in during the last pass, dsp/SongCapture.h, or
// before any pass the stand-in, Carve + Draw's halo, the sines, the latency taken out) and
// analysed as a spectrogram analyser does (SpectrumAnalyser: Hann, the FFT size and levels from
// Settings), one analysis per canvas pixel column at most. The canvas cross-fades to that
// picture.
//
// Cost (see docs/DESIGN.md, "Measured preview"): the analyses are at most one per pixel column
// and no closer than a sixteenth of the FFT apart (the window blurs time over far more than that;
// the columns between are interpolated), so a 4-bar pass takes about as long as the render. A
// render that is no longer wanted stops within a block. Memory: one analysis, as 16-bit levels.

#include "Parameters.h"
#include "core/ArtPlan.h"
#include "core/MaskPlan.h"
#include "dsp/SongCapture.h"
#include "dsp/SpectrumAnalysis.h"

#include <juce_graphics/juce_graphics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace artfreq
{
class ArtFreqProcessor;
}

namespace artfreq::ui
{

/** Level, Depth and the pass's length as the previews use them: a host's tempo that wobbles in its
    last digits, or automation moving Level by a hair, does not start a new render. */
float previewGain (float gain) noexcept;          // to a quarter dB
float previewDepthDb (float depthDb) noexcept;    // to half a dB
double previewSeconds (double seconds) noexcept;  // to a millisecond

/** Everything the measured preview depends on: what is heard, and how it is analysed and drawn. */
struct MeasureRequest
{
    bool effect = false;
    std::shared_ptr<const ArtPlan> plan;            // the sines (the instrument; ArtFreq FX's Draw modes)
    std::shared_ptr<const MaskPlanSet> masks;       // ArtFreq FX: what it carves with
    EffectMode mode = EffectMode::carve;
    float depthDb = 0.0f;
    float outputGain = 1.0f;                        // Level, as a gain
    double passSeconds = 8.0;                       // one pass of the drawing
    double sampleRate = 48000.0;

    // ArtFreq FX: the song carved. The user's own, as it came in during the last pass that
    // played (dsp/SongCapture.h), once one has; until then (songSerial 0) the stand-in.
    std::shared_ptr<SongCapture> song;
    uint32_t songSerial = 0;

    FrequencyScale scale;
    float slopeDbPerOctave = 0.0f;
    AnalyserView view;
    int width = 0, height = 0;                      // the picture, in pixels
    uint64_t documentRevision = 0;                  // the drawing the plan and masks were made from

    /** What two requests may differ in (sameExcept). */
    enum Aspect
    {
        colours = 1,   // the analyser's level range
        size = 2,      // the picture's width and height
        level = 4,     // outputGain
        songTake = 8   // which take of the user's song (the same song, another pass of it)
    };

    /** The same but for the aspects in `ignored`. */
    bool sameExcept (const MeasureRequest& other, int ignored) const noexcept;

    /** Everything but the colours' range: the same sound, analysed the same way. */
    bool sameAnalysis (const MeasureRequest& other) const noexcept { return sameExcept (other, colours); }

    /** The same and the same colours. */
    bool operator== (const MeasureRequest& other) const noexcept { return sameExcept (other, 0); }
    bool operator!= (const MeasureRequest& other) const noexcept { return ! operator== (other); }

    /** Whether there is anything to render (a picture size, a plan or masks). */
    bool isValid() const noexcept;

    /** What the canvas asks for a picture of `width` x `height` pixels for the processor's drawing
        and settings now (the snapshot tool and the truth lab ask the same). */
    static MeasureRequest forProcessor (const ArtFreqProcessor& processor, int width, int height);
};

/** One analysed pass: `frames` analyses evenly across it (frame k centred at (k + 0.5) / frames of
    the pass), each read off at `rows` heights (row 0 at the top), in hundredths of a dBFS. */
struct MeasuredSpectrogram
{
    int frames = 0, rows = 0;
    std::vector<int16_t> output;   // [row * frames + frame]: the sound
    std::vector<int16_t> input;    // ArtFreq FX: the song going in, the same way (else empty)
    uint32_t songSerial = 0;       // ArtFreq FX: the take of the user's song carved (0: the stand-in)
    float peak = 0.0f;             // the instrument: the pass's largest sample (after Level and the clipper)

    float outputDb (int row, int frame) const noexcept { return 0.01f * output[static_cast<size_t> (row) * static_cast<size_t> (frames) + static_cast<size_t> (frame)]; }
    float inputDb (int row, int frame) const noexcept { return 0.01f * input[static_cast<size_t> (row) * static_cast<size_t> (frames) + static_cast<size_t> (frame)]; }

    /** The level at a pixel of a picture `width` wide (the frames interpolated). */
    static void columnPosition (int x, int width, int frames, int& frame0, int& frame1, float& fraction) noexcept;
};

/** Renders and analyses `request` (blocking; any thread but the audio thread). Returns false if
    `shouldStop` said so (asked once per block) or there was nothing to render. */
bool measure (const MeasureRequest& request, MeasuredSpectrogram& result, const std::function<bool()>& shouldStop = nullptr);

/** Whether `analysis` of `from` gives `to` by a change of level alone: the instrument, the same
    but for Level, and the clipper untouched at both levels (the sound is then the same times a
    gain: the analysis moves by as many dB). */
bool canRelevel (const MeasuredSpectrogram& analysis, const MeasureRequest& from, const MeasureRequest& to) noexcept;

/** `analysis` (of `from`) as `to`'s Level makes it (canRelevel must hold). */
std::shared_ptr<const MeasuredSpectrogram> relevel (const MeasuredSpectrogram& analysis, const MeasureRequest& from, const MeasureRequest& to);

/** The picture of an analysis with a view's colours, `width` x `height` (its frames stretched
    across the width), row by row. */
void colourise (const MeasuredSpectrogram& analysis, const AnalyserView& view, int width, int height,
                std::vector<juce::PixelARGB>& pixels);

/** How many analyses a pass gets: one per pixel column at most, none closer than fftSize / 16
    samples, at least 2. */
int framesFor (double passSamples, int fftSize, int width) noexcept;

/** The canvas's last measured preview, which the processor keeps while no editor is open: the
    next editor window shows it at once and renders again only what changed since (opening and
    closing a plug-in's window is frequent; the picture is the same). Message thread. */
struct PreviewMemory
{
    MeasureRequest request;
    std::shared_ptr<const MeasuredSpectrogram> analysis;
    juce::Image image;   // request.width x request.height
};

//==============================================================================
/** Renders measured previews on a background thread. Message thread only, but the thread. */
class MeasuredPreview final : private juce::Thread
{
public:
    static constexpr double debounceSeconds = 0.2;

    struct Result
    {
        MeasureRequest request;
        std::shared_ptr<const MeasuredSpectrogram> analysis;
        std::vector<juce::PixelARGB> pixels;    // request.width x request.height
        double milliseconds = 0.0;              // how long it took (the render, or the colours alone)
    };

    MeasuredPreview();
    ~MeasuredPreview() override;   // a running render stops within a block

    /** What the canvas shows now, called from its timer. A request that differs from the last one
        stops the render of an older one at once; once a request has stayed the same for
        debounceSeconds and `mayStart` (the canvas is on screen), it is rendered. A request that
        only changes the colours recolours the last analysis instead. The thread is started the
        first time one is (so an editor opened and closed at once, as host scans and validators
        do, starts none). */
    void update (const MeasureRequest& request, bool mayStart);

    /** The newest finished result, once (null if none since the last call). */
    std::unique_ptr<Result> takeResult();

    /** A request is waiting for its moment or being rendered. */
    bool isBusy() const noexcept;

    /** How many renders started (tests). */
    int getRendersStarted() const noexcept { return rendersStarted.load(); }
    int getRendersFinished() const noexcept { return rendersFinished.load(); }

    /** Takes over an analysis made earlier (in an editor window closed since): a request for
        exactly it renders nothing, and recolouring and relevelling start from it. */
    void adopt (const MeasureRequest& request, std::shared_ptr<const MeasuredSpectrogram> analysis);

    /** Renders `request` on the calling thread (the snapshot tool, the truth lab, tests). */
    static std::unique_ptr<Result> renderNow (const MeasureRequest& request);

private:
    void run() override;
    static std::unique_ptr<Result> compute (const MeasureRequest& request, const std::shared_ptr<const MeasuredSpectrogram>& reuse,
                                            const MeasureRequest* reuseRequest, const std::function<bool()>& shouldStop);

    mutable juce::CriticalSection lock;
    MeasureRequest latest;                    // message thread
    bool hasLatest = false;
    double latestSince = 0.0;                 // when it last changed (seconds)
    bool latestHandedOver = false;            // given to the thread (or finished)

    MeasureRequest job;                       // under lock: the next one for the thread
    bool hasJob = false;
    uint32_t jobGeneration = 0;
    std::atomic<uint32_t> generation { 0 };   // changes with every new request: a render of another stops

    std::unique_ptr<Result> finished;         // under lock
    std::shared_ptr<const MeasuredSpectrogram> lastAnalysis;   // under lock: for recolouring
    MeasureRequest lastAnalysisRequest;
    std::atomic<bool> running { false };
    std::atomic<int> rendersStarted { 0 }, rendersFinished { 0 };
    juce::WaitableEvent wake;
};

} // namespace artfreq::ui
