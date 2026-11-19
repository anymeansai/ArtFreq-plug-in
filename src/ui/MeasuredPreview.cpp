#include "MeasuredPreview.h"

#include "PluginProcessor.h"
#include "Theme.h"
#include "dsp/StandInSong.h"
#include "dsp/TunedCarver.h"
#include "dsp/WavExporter.h"

#include <algorithm>
#include <cmath>

namespace artfreq::ui
{
namespace
{
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    int16_t toCentiDb (float db) noexcept
    {
        return static_cast<int16_t> (juce::jlimit (-30000, 30000, static_cast<int> (std::lround (db * 100.0f))));
    }

    /** Analyses frames of a signal that arrives in order, a block at a time, at the frames' centres:
        a ring of the newest samples, long enough for a window and a block. */
    class FrameAnalyser
    {
    public:
        FrameAnalyser (int fftSize, int numFrames, int64_t passSamples, const SpectrumRows& rowsToUse,
                       std::vector<int16_t>& target)
            : analyser (fftSize), rows (rowsToUse), out (target), frames (numFrames),
              ring (static_cast<size_t> (juce::nextPowerOfTwo (fftSize + 2 * PassRenderer::blockSize))),
              mask (static_cast<int64_t> (ring.size()) - 1),
              window (static_cast<size_t> (fftSize)), bins (static_cast<size_t> (analyser.getNumBins())),
              rowDb (static_cast<size_t> (rows.getHeight())), pass (passSamples)
        {
            out.assign (static_cast<size_t> (rows.getHeight()) * static_cast<size_t> (frames), toCentiDb (SpectrumAnalyser::floorDb));
        }

        /** Frame k is centred here (the sample its window's second half starts at). */
        int64_t centreOf (int k) const noexcept
        {
            return static_cast<int64_t> (std::llround ((k + 0.5) / frames * static_cast<double> (pass)));
        }

        int64_t firstNeeded() const noexcept { return centreOf (0) - analyser.getFftSize() / 2; }
        int64_t lastNeeded() const noexcept  { return centreOf (frames - 1) + analyser.getFftSize() / 2; }   // exclusive
        bool isDone() const noexcept { return next >= frames; }

        /** `samples` are the signal at [start, start + count), following on from the last call. */
        void add (const float* samples, int count, int64_t start) noexcept
        {
            for (int i = 0; i < count; ++i)
                ring[static_cast<size_t> ((start + i) & mask)] = samples[i];

            end = start + count;
            const auto half = analyser.getFftSize() / 2;

            while (next < frames && centreOf (next) + half <= end)
            {
                const auto first = centreOf (next) - half;

                for (int n = 0; n < analyser.getFftSize(); ++n)
                {
                    const auto t = first + n;
                    window[static_cast<size_t> (n)] = t >= begin ? ring[static_cast<size_t> (t & mask)] : 0.0f;
                }

                analyser.analyse (window.data(), bins.data());
                rows.apply (bins.data(), rowDb.data());

                for (size_t y = 0; y < rowDb.size(); ++y)
                    out[y * static_cast<size_t> (frames) + static_cast<size_t> (next)] = toCentiDb (rowDb[y]);

                ++next;
            }
        }

        void setBegin (int64_t first) noexcept { begin = first; }

    private:
        SpectrumAnalyser analyser;
        const SpectrumRows& rows;
        std::vector<int16_t>& out;
        int frames;
        std::vector<float> ring;
        int64_t mask;
        std::vector<float> window, bins, rowDb;
        int64_t pass;
        int64_t begin = 0, end = 0;
        int next = 0;
    };

    /** Renders and drops the pass up to `target` (the first frame of a very long pass can start
        after its first sample). */
    void skipTo (PassRenderer& pass, int64_t target, std::vector<float>& scratch) noexcept
    {
        while (pass.getPosition() < target)
            if (pass.render (scratch.data(), static_cast<int> (std::min (static_cast<int64_t> (scratch.size()), target - pass.getPosition()))) <= 0)
                break;
    }

    /** The instrument: one pass of the drawing as Bounce WAV renders it, silence around it. */
    bool measureInstrument (const MeasureRequest& request, int64_t passSamples, FrameAnalyser& output,
                            const std::function<bool()>& shouldStop)
    {
        constexpr int block = PassRenderer::blockSize;
        std::vector<float> samples (static_cast<size_t> (block));
        std::unique_ptr<PassRenderer> pass;

        if (request.plan != nullptr && ! request.plan->silent)
            pass = std::make_unique<PassRenderer> (*request.plan, request.sampleRate, request.passSeconds, request.outputGain);

        output.setBegin (output.firstNeeded());

        for (auto t = output.firstNeeded(); ! output.isDone();)
        {
            if (shouldStop != nullptr && shouldStop())
                return false;

            auto count = block;

            if (t < 0)
            {
                count = static_cast<int> (std::min (static_cast<int64_t> (block), -t));
                std::fill_n (samples.begin(), count, 0.0f);
            }
            else if (pass != nullptr && t < pass->getTotalSamples())
            {
                skipTo (*pass, t, samples);
                count = pass->render (samples.data(), block);
            }
            else
            {
                std::fill (samples.begin(), samples.end(), 0.0f);
            }

            output.add (samples.data(), count, t);
            t += count;
        }

        juce::ignoreUnused (passSamples);
        return true;
    }

    /** ArtFreq FX: the stand-in song through the carver as the audio thread runs it (the mode's
        masks and settings, the gate open for the pass), the sines of the Draw modes added, the
        carver's latency taken out (as the host's delay compensation does). */
    bool measureEffect (const MeasureRequest& request, int64_t passSamples, FrameAnalyser& output, FrameAnalyser& input,
                        const std::function<bool()>& shouldStop)
    {
        constexpr int block = 1024;
        const auto sampleRate = request.sampleRate;
        const auto carves = carvesAudio (request.mode) && request.masks != nullptr;
        const MaskPlan* mask = nullptr;

        if (carves)
            mask = request.mode == EffectMode::carveDraw ? request.masks->halo.get() : request.masks->exact.get();

        const auto settings = ArtFreqProcessor::carveSettingsFor (request.mode, request.depthDb, true);
        const auto useCarver = mask != nullptr && ! mask->empty && settings.depthDb < 0.0f;

        TunedCarver carver;
        auto latency = 0;

        if (useCarver)
        {
            carver.prepare (sampleRate, block, 1);
            carver.setPlan (mask);
            latency = carver.getLatencySamples();
        }

        // The input starts a frame before the carver's first output that is needed, so its
        // history is the song, not silence.
        const auto firstOut = output.firstNeeded();
        const auto firstIn = firstOut - 2 * static_cast<int64_t> (latency);
        StandInSong song (sampleRate, passSamples, firstIn);

        std::unique_ptr<PassRenderer> sines;

        if (playsSines (request.mode) && request.plan != nullptr && ! request.plan->silent)
            sines = std::make_unique<PassRenderer> (*request.plan, sampleRate, request.passSeconds, request.outputGain);

        const auto fadeSamples = static_cast<int64_t> (std::llround (ArtEngine::gateFadeSeconds * sampleRate));
        const auto gateCloses = std::max (static_cast<int64_t> (0), passSamples - fadeSamples);
        const auto perSample = 1.0 / static_cast<double> (std::max (static_cast<int64_t> (1), passSamples));

        std::vector<float> audio (static_cast<size_t> (block)), sineBlock (static_cast<size_t> (PassRenderer::blockSize));
        output.setBegin (firstOut);
        input.setBegin (firstOut);

        for (auto t = firstIn; ! output.isDone(); t += block)
        {
            if (shouldStop != nullptr && shouldStop())
                return false;

            song.render (audio.data(), block);

            if (! input.isDone() && t + block > firstOut)
            {
                const auto skip = static_cast<int> (std::max (static_cast<int64_t> (0), firstOut - t));
                input.add (audio.data() + skip, block - skip, t + skip);
            }

            if (useCarver)
            {
                // The tracker's segments for this block: the gate open over the pass (closing 5 ms
                // before its end, as a bounce's), its position along the drawing.
                PlaySegment segments[3];
                int count = 0;
                const int64_t edges[] { 0, gateCloses };
                auto from = t;

                while (from < t + block)
                {
                    auto to = t + block;

                    for (const auto edge : edges)
                        if (edge > from && edge < to)
                            to = edge;

                    auto& segment = segments[count++];
                    segment.start = static_cast<int> (from - t);
                    segment.length = static_cast<int> (to - from);
                    segment.playing = from >= 0 && from < gateCloses;
                    segment.fraction = static_cast<double> (from) * perSample;
                    segment.fractionPerSample = perSample;
                    segment.jump = segment.playing && from == 0;
                    from = to;
                }

                float* channels[] { audio.data() };
                carver.process (channels, 1, block, segments, count, settings);
            }

            // What came out is the audio of `latency` samples ago.
            const auto outStart = t - latency;

            if (sines != nullptr)
            {
                // The sines of [outStart, outStart + block), from the pass's start on.
                if (outStart > sines->getPosition())
                    skipTo (*sines, outStart, sineBlock);

                auto at = std::max (outStart, sines->getPosition());

                while (at < outStart + block && at < sines->getTotalSamples())
                {
                    const auto made = sines->render (sineBlock.data(), static_cast<int> (outStart + block - at));

                    if (made <= 0)
                        break;

                    for (int i = 0; i < made; ++i)
                        audio[static_cast<size_t> (at - outStart + i)] += sineBlock[static_cast<size_t> (i)];

                    at += made;
                }
            }

            if (outStart + block > firstOut)
            {
                const auto skip = static_cast<int> (std::max (static_cast<int64_t> (0), firstOut - outStart));
                output.add (audio.data() + skip, block - skip, outStart + skip);
            }
        }

        return true;
    }

} // namespace

//==============================================================================
float previewGain (float gain) noexcept
{
    if (! (gain > 0.0f))
        return 0.0f;

    return juce::Decibels::decibelsToGain (std::round (juce::Decibels::gainToDecibels (gain, -200.0f) * 4.0f) / 4.0f, -200.0f);
}

float previewDepthDb (float depthDb) noexcept
{
    return std::round (depthDb * 2.0f) / 2.0f;
}

double previewSeconds (double seconds) noexcept
{
    return std::round (seconds * 1000.0) / 1000.0;
}

MeasureRequest MeasureRequest::forProcessor (const ArtFreqProcessor& processor, int width, int height)
{
    MeasureRequest request;
    request.effect = processor.isEffect();
    request.plan = processor.getCurrentPlan();
    request.masks = request.effect ? processor.getCurrentMasks() : nullptr;
    request.mode = processor.getEffectMode();
    request.depthDb = previewDepthDb (processor.getDepthDb());
    request.outputGain = previewGain (processor.getOutputGain());

    // ArtFreq FX's Carve and Stencil play no sines: neither the plan nor Level changes their sound.
    if (request.effect && ! playsSines (request.mode))
    {
        request.plan = nullptr;
        request.outputGain = 1.0f;
    }

    request.passSeconds = previewSeconds (processor.getLengthSeconds());
    request.sampleRate = processor.getCurrentSampleRate();
    request.scale = processor.getScale();
    request.slopeDbPerOctave = processor.getPlanSettings().slopeDbPerOctave;
    request.view = processor.getAnalyserView();
    request.width = width;
    request.height = height;
    request.documentRevision = processor.getDocumentRevision();
    return request;
}

bool MeasureRequest::sameAnalysis (const MeasureRequest& other) const noexcept
{
    return effect == other.effect && plan == other.plan && masks == other.masks && mode == other.mode
        && juce::exactlyEqual (depthDb, other.depthDb) && juce::exactlyEqual (outputGain, other.outputGain)
        && juce::exactlyEqual (passSeconds, other.passSeconds) && juce::exactlyEqual (sampleRate, other.sampleRate)
        && scale == other.scale && juce::exactlyEqual (slopeDbPerOctave, other.slopeDbPerOctave)
        && view.fftSize == other.view.fftSize && width == other.width && height == other.height
        && documentRevision == other.documentRevision;
}

bool MeasureRequest::operator== (const MeasureRequest& other) const noexcept
{
    return sameAnalysis (other) && view == other.view;
}

bool MeasureRequest::isValid() const noexcept
{
    return width > 0 && height > 0 && passSeconds > 0.0 && sampleRate >= 8000.0
        && (effect || plan != nullptr);
}

void MeasuredSpectrogram::columnPosition (int x, int width, int frames, int& frame0, int& frame1, float& fraction) noexcept
{
    const auto p = (x + 0.5) / std::max (1, width) * frames - 0.5;
    frame0 = juce::jlimit (0, frames - 1, static_cast<int> (std::floor (p)));
    frame1 = juce::jmin (frames - 1, frame0 + 1);
    fraction = static_cast<float> (juce::jlimit (0.0, 1.0, p - frame0));
}

int framesFor (double passSamples, int fftSize, int width) noexcept
{
    const auto hop = std::max (64, fftSize / 16);
    const auto wanted = static_cast<int> (std::ceil (passSamples / hop));
    return juce::jlimit (2, std::max (2, width), wanted);
}

bool measure (const MeasureRequest& request, MeasuredSpectrogram& result, const std::function<bool()>& shouldStop)
{
    if (! request.isValid())
        return false;

    const auto view = request.view.sanitised();
    const auto passSamples = std::max (static_cast<int64_t> (1), static_cast<int64_t> (std::llround (request.passSeconds * request.sampleRate)));

    result.frames = framesFor (static_cast<double> (passSamples), view.fftSize, request.width);
    result.rows = request.height;

    SpectrumRows rows;
    rows.prepare (request.scale, request.height, request.sampleRate, view.fftSize, request.slopeDbPerOctave);

    FrameAnalyser output (view.fftSize, result.frames, passSamples, rows, result.output);

    if (! request.effect)
    {
        result.input.clear();
        return measureInstrument (request, passSamples, output, shouldStop);
    }

    FrameAnalyser input (view.fftSize, result.frames, passSamples, rows, result.input);
    return measureEffect (request, passSamples, output, input, shouldStop);
}

void colourise (const MeasuredSpectrogram& analysis, const AnalyserView& view, int width, int height,
                std::vector<juce::PixelARGB>& pixels)
{
    pixels.resize (static_cast<size_t> (std::max (0, width)) * static_cast<size_t> (std::max (0, height)));

    if (width <= 0 || height <= 0 || analysis.frames <= 0 || analysis.rows <= 0)
        return;

    const auto v = view.sanitised();
    const auto& table = theme::spectrumTable();

    // Levels per hundredth of a dB to colour-map index, as a straight line: index = a + b * centiDb.
    const auto b = 255.0f / ((v.ceilingDb - v.floorDb) * 100.0f);
    const auto a = -v.floorDb * 100.0f * b + 0.5f;

    std::vector<int> frame0 (static_cast<size_t> (width)), frame1 (static_cast<size_t> (width));
    std::vector<float> fraction (static_cast<size_t> (width));

    for (int x = 0; x < width; ++x)
        MeasuredSpectrogram::columnPosition (x, width, analysis.frames, frame0[static_cast<size_t> (x)],
                                             frame1[static_cast<size_t> (x)], fraction[static_cast<size_t> (x)]);

    for (int y = 0; y < height; ++y)
    {
        // The analysis has its own rows (a picture asked for at another size is stretched).
        const auto row = juce::jlimit (0, analysis.rows - 1, static_cast<int> ((y + 0.5) * analysis.rows / height));
        const auto* levels = analysis.output.data() + static_cast<size_t> (row) * static_cast<size_t> (analysis.frames);
        auto* out = pixels.data() + static_cast<size_t> (y) * static_cast<size_t> (width);

        for (int x = 0; x < width; ++x)
        {
            const auto l0 = static_cast<float> (levels[frame0[static_cast<size_t> (x)]]);
            const auto l1 = static_cast<float> (levels[frame1[static_cast<size_t> (x)]]);
            const auto index = a + b * (l0 + (l1 - l0) * fraction[static_cast<size_t> (x)]);
            out[x] = table[static_cast<size_t> (juce::jlimit (0, 255, static_cast<int> (index)))];
        }
    }
}

//==============================================================================
MeasuredPreview::MeasuredPreview() : juce::Thread ("ArtFreq measured preview") {}

MeasuredPreview::~MeasuredPreview()
{
    ++generation;   // a render in progress stops at its next block
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

void MeasuredPreview::update (const MeasureRequest& request, bool mayStart)
{
    const auto now = nowSeconds();

    if (! hasLatest || request != latest)
    {
        latest = request;
        hasLatest = true;
        latestSince = now;
        latestHandedOver = false;
        ++generation;   // stops whatever is rendering: it is no longer what the canvas shows

        const juce::ScopedLock sl (lock);
        hasJob = false;
    }

    if (latestHandedOver || ! mayStart || ! latest.isValid())
        return;

    // A change of colours alone needs no wait: it recolours the last analysis.
    bool coloursOnly = false;
    {
        const juce::ScopedLock sl (lock);
        coloursOnly = lastAnalysis != nullptr && lastAnalysisRequest.sameAnalysis (latest);
    }

    if (! coloursOnly && now - latestSince < debounceSeconds)
        return;

    {
        const juce::ScopedLock sl (lock);
        job = latest;
        hasJob = true;
        jobGeneration = generation.load();
    }

    latestHandedOver = true;

    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);

    wake.signal();
}

std::unique_ptr<MeasuredPreview::Result> MeasuredPreview::takeResult()
{
    const juce::ScopedLock sl (lock);
    return std::move (finished);
}

bool MeasuredPreview::isBusy() const noexcept
{
    if (hasLatest && ! latestHandedOver && latest.isValid())
        return true;

    const juce::ScopedLock sl (lock);
    return hasJob || running.load();
}

std::unique_ptr<MeasuredPreview::Result> MeasuredPreview::renderNow (const MeasureRequest& request)
{
    return compute (request, nullptr, nullptr, nullptr);
}

std::unique_ptr<MeasuredPreview::Result> MeasuredPreview::compute (const MeasureRequest& request,
                                                                   const std::shared_ptr<const MeasuredSpectrogram>& reuse,
                                                                   const MeasureRequest* reuseRequest,
                                                                   const std::function<bool()>& shouldStop)
{
    const auto started = nowSeconds();
    auto result = std::make_unique<Result>();
    result->request = request;

    if (reuse != nullptr && reuseRequest != nullptr && reuseRequest->sameAnalysis (request))
    {
        result->analysis = reuse;
    }
    else
    {
        auto analysis = std::make_shared<MeasuredSpectrogram>();

        if (! measure (request, *analysis, shouldStop))
            return nullptr;

        result->analysis = std::move (analysis);
    }

    if (shouldStop != nullptr && shouldStop())
        return nullptr;

    colourise (*result->analysis, request.view, request.width, request.height, result->pixels);
    result->milliseconds = (nowSeconds() - started) * 1000.0;
    return result;
}

void MeasuredPreview::run()
{
    while (! threadShouldExit())
    {
        wake.wait (-1);

        MeasureRequest request;
        uint32_t myGeneration = 0;
        std::shared_ptr<const MeasuredSpectrogram> reuse;
        MeasureRequest reuseRequest;

        {
            const juce::ScopedLock sl (lock);

            if (! hasJob)
                continue;

            request = job;
            myGeneration = jobGeneration;
            hasJob = false;
            reuse = lastAnalysis;
            reuseRequest = lastAnalysisRequest;
            running = true;
        }

        ++rendersStarted;
        const auto shouldStop = [this, myGeneration] { return threadShouldExit() || generation.load() != myGeneration; };
        auto result = compute (request, reuse, &reuseRequest, shouldStop);

        const juce::ScopedLock sl (lock);
        running = false;

        if (result != nullptr && ! shouldStop())
        {
            ++rendersFinished;
            lastAnalysis = result->analysis;
            lastAnalysisRequest = result->request;
            finished = std::move (result);
        }
    }
}

} // namespace artfreq::ui
