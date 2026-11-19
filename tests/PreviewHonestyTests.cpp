// What a review of the measured preview found, pinned: ArtFreq FX carves the user's own song,
// the canvas never shows a measured picture of another sound, a new editor window shows the last
// measurement at once, the fast preview leaves the message thread free (and shows the
// measurement where the drawing did not change), and a change of Level needs no new render.

#include "DenseProject.h"
#include "ProcessorTestUtils.h"
#include "TestFramework.h"
#include "WaitFor.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/SongCapture.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/FastPreview.h"
#include "ui/MeasuredPreview.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace artfreq;

namespace
{
    double seconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    void pumpFor (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    /** Pumps the message loop until `condition` (or `timeout` seconds), asking every
        millisecond, between the timer's ticks (WaitFor.h): the canvas's states that last only a
        few ticks are seen however late a slow machine runs them. */
    template <typename Condition>
    bool pumpUntil (Condition&& condition, double timeout)
    {
        return testwait::pumpUntil (std::forward<Condition> (condition), juce::roundToInt (timeout * 1000.0));
    }

    juce::Component* findById (juce::Component& root, const juce::String& id)
    {
        if (root.getComponentID() == id)
            return &root;

        for (auto* child : root.getChildren())
            if (auto* found = findById (*child, id))
                return found;

        return nullptr;
    }

    /** The tracker's segments for a block of a pass playing from `position` (samples). */
    PlaySegment playing (int64_t position, int length, int64_t passSamples)
    {
        PlaySegment segment;
        segment.length = length;
        segment.playing = true;
        segment.fraction = static_cast<double> (position) / static_cast<double> (passSamples);
        segment.fractionPerSample = 1.0 / static_cast<double> (passSamples);
        segment.jump = position == 0;
        return segment;
    }

    /** Feeds `signal` to `capture` as one pass of `passSamples` from position 0, in blocks of 512,
        then a block with the gate shut (the pass is over). */
    void playPass (SongCapture& capture, const std::vector<float>& signal, int64_t passSamples)
    {
        std::vector<float> block (512);

        for (int64_t at = 0; at < passSamples; at += 512)
        {
            const auto n = static_cast<int> (std::min<int64_t> (512, passSamples - at));
            std::copy (signal.begin() + static_cast<std::ptrdiff_t> (at), signal.begin() + static_cast<std::ptrdiff_t> (at + n), block.begin());
            const float* channels[] { block.data() };
            const auto segment = playing (at, n, passSamples);
            capture.process (channels, 1, n, &segment, 1, 48000.0);
        }

        PlaySegment closed;
        closed.length = 512;
        std::fill (block.begin(), block.end(), 0.0f);
        const float* channels[] { block.data() };
        capture.process (channels, 1, 512, &closed, 1, 48000.0);
    }

    std::vector<float> sine (int64_t samples, double hz, float amplitude)
    {
        std::vector<float> out (static_cast<size_t> (samples));

        for (size_t i = 0; i < out.size(); ++i)
            out[i] = amplitude * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * hz * static_cast<double> (i) / 48000.0));

        return out;
    }

    /** Plays one pass of ArtFreq FX (On play) over `song` (mono, from the transport's start), as a
        DAW does: the song comes in through processBlock. */
    void playEffectPass (ArtFreqProcessor& processor, const std::function<float (int64_t)>& song)
    {
        testutil::FakePlayHead head;
        processor.setPlayHead (&head);
        const auto passSamples = static_cast<int64_t> (std::llround (processor.getLengthSeconds() * 48000.0));
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int64_t done = 0; done < passSamples + 48000; done += 512)
        {
            const auto t = static_cast<double> (done) / 48000.0;
            head.set (true, 120.0, t * 2.0, t);

            for (int i = 0; i < 512; ++i)
            {
                const auto x = song (done + i);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }

            midi.clear();
            processor.processBlock (buffer, midi);
        }

        head.set (false, 120.0, 0.0, 0.0);
        buffer.clear();
        processor.processBlock (buffer, midi);
        processor.setPlayHead (nullptr);
    }

    /** The analysis's mean level over a band of rows and all its frames (dB). */
    float meanDb (const ui::MeasuredSpectrogram& a, const FrequencyScale& scale, double lowHz, double highHz, bool input = false)
    {
        double sum = 0.0;
        int count = 0;

        for (int row = 0; row < a.rows; ++row)
        {
            const auto hz = scale.toHz (1.0 - (row + 0.5) / a.rows);

            if (hz < lowHz || hz >= highHz)
                continue;

            for (int frame = 0; frame < a.frames; ++frame)
            {
                sum += input ? a.inputDb (row, frame) : a.outputDb (row, frame);
                ++count;
            }
        }

        return count > 0 ? static_cast<float> (sum / count) : -200.0f;
    }
} // namespace

//==============================================================================
ARTFREQ_TEST (songCaptureKeepsThePassThatPlayed)
{
    // A whole pass comes in: it is published as it came in (16-bit), from position 0. The same
    // audio again (a DAW looping the same bars) publishes nothing new; other audio does. A
    // moment of a pass after a whole one does not replace it.
    SongCapture capture;
    CHECK (capture.getSerial() == 0);
    CHECK (capture.getLatest() == nullptr);

    constexpr int64_t pass = 96000;   // 2 s
    const auto tone = sine (pass, 100.0, 0.5f);
    playPass (capture, tone, pass);

    CHECK (capture.getSerial() == 1);
    const auto song = capture.getLatest();
    CHECK (song != nullptr);

    if (song == nullptr)
        return;

    CHECK (song->start == 0 && song->length() == pass && song->passSamples == pass && song->coversPass());
    auto worst = 0.0f;

    for (size_t i = 0; i < tone.size(); ++i)
        worst = std::max (worst, std::abs (static_cast<float> (song->samples[i]) / SongCapture::sampleScale - tone[i]));

    CHECK_LESS (worst, 2.0f / SongCapture::sampleScale);

    playPass (capture, tone, pass);
    CHECK (capture.getSerial() == 1);

    playPass (capture, sine (pass, 100.0, 0.05f), pass);
    CHECK (capture.getSerial() == 2);

    // Half a second of the pass, then stopped: kept back (the whole pass before it stays).
    {
        const auto shortTake = sine (24000, 300.0, 0.5f);
        std::vector<float> block (512);

        for (int64_t at = 0; at < 24000; at += 512)
        {
            std::copy (shortTake.begin() + static_cast<std::ptrdiff_t> (at), shortTake.begin() + static_cast<std::ptrdiff_t> (at + 512), block.begin());
            const float* channels[] { block.data() };
            const auto segment = playing (at, 512, pass);
            capture.process (channels, 1, 512, &segment, 1, 48000.0);
        }

        capture.endTake();
        CHECK (capture.getSerial() == 2);
    }

    // The reader loops a take over a longer timeline without a click at the seam.
    const auto latest = capture.getLatest();
    CHECK (latest != nullptr);

    if (latest != nullptr)
    {
        CapturedSong::Reader reader (*latest, -1000);
        std::vector<float> out (static_cast<size_t> (3 * pass));
        reader.render (out.data(), static_cast<int> (out.size()));
        auto biggestStep = 0.0f;

        for (size_t i = 1; i < out.size(); ++i)
            biggestStep = std::max (biggestStep, std::abs (out[i] - out[i - 1]));

        // A 100 Hz sine of 0.05 moves at most 0.00065 a sample; a cut would jump up to 0.1.
        CHECK_LESS (biggestStep, 0.002f);
    }
}

ARTFREQ_TEST (effectCanvasCarvesTheSongThatPlayed)
{
    // ArtFreq FX on a bass track (a 60 Hz tone, nothing above): before any pass the canvas carves
    // the stand-in, a full mix, and shows the drawing dark in the mids and highs; once a pass has
    // played, it carves the bass track, and the mids and highs are as empty as the real output.
    ArtFreqProcessor processor (ArtFreqProcessor::Variant::effect);
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    testutil::setParameter (processor, params::id::length, 2.0f);   // 1 bar: 2 s at 120
    processor.rebuildPlanIfNeeded();

    const auto scale = processor.getScale();
    auto before = ui::MeasureRequest::forProcessor (processor, 300, 200);
    CHECK (before.songSerial == 0);

    processor.enableSongCapture();
    playEffectPass (processor, [] (int64_t n) { return 0.4f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 60.0 * static_cast<double> (n) / 48000.0)); });

    const auto after = ui::MeasureRequest::forProcessor (processor, 300, 200);
    CHECK (after.songSerial != 0);

    const auto standIn = ui::MeasuredPreview::renderNow (before);
    const auto ownSong = ui::MeasuredPreview::renderNow (after);
    CHECK (standIn != nullptr && ownSong != nullptr);

    if (standIn == nullptr || ownSong == nullptr)
        return;

    CHECK (standIn->analysis->songSerial == 0);
    CHECK (ownSong->analysis->songSerial == after.songSerial);

    const auto standInHighs = meanDb (*standIn->analysis, scale, 2000.0, 16000.0, true);
    const auto songHighs = meanDb (*ownSong->analysis, scale, 2000.0, 16000.0, true);
    const auto songTone = meanDb (*ownSong->analysis, scale, 55.0, 65.0, true);
    std::printf ("    highs going in: stand-in %.0f dB, the bass track %.0f dB (its tone %.0f dB)\n", standInHighs, songHighs, songTone);
    CHECK_GREATER (standInHighs, -90.0f);
    CHECK_LESS (songHighs, -100.0f);   // under the darkest colour of the default range
    CHECK_GREATER (songTone, -30.0f);
}

ARTFREQ_TEST (effectCanvasFollowsEverySoundChangeAtOnce)
{
    // ArtFreq FX, Carve, the measured preview on screen: a change of mode or Depth from the host
    // (the mouse is up) shows the fast preview within a frame, never the measured picture of the
    // mode before (Carve's dark words where Stencil keeps only them); then the new measurement.
    ArtFreqProcessor processor (ArtFreqProcessor::Variant::effect);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));
    CHECK (editor != nullptr);

    if (editor == nullptr)
        return;

    editor->addToDesktop (0);
    editor->setVisible (true);
    auto* canvas = dynamic_cast<ui::CanvasView*> (findById (*editor, "canvas"));
    CHECK (canvas != nullptr);

    if (canvas == nullptr)
        return;

    const auto current = [&] { const auto s = canvas->getPreviewStatus(); return s.measuredShown && s.current && ! s.fading; };

    // (A render of ArtFreq FX's stand-in song takes many seconds on a slow, busy machine.)
    constexpr double renderTimeout = 60.0;

    for (const auto& [id, value] : { std::pair<const char*, float> { params::id::mode, 2.0f }, { params::id::depth, -12.0f },
                                     { params::id::mode, 3.0f } })
    {
        CHECK (pumpUntil (current, renderTimeout));
        testutil::setParameter (processor, id, value);

        // The canvas takes the change in on its timer, which a slow machine runs late: it then
        // shows the fast preview, until the new measurement is in (a render later).
        const auto fast = [&] { const auto s = canvas->getPreviewStatus(); return ! s.measuredShown && s.text.startsWith ("Updating"); };
        CHECK (pumpUntil (fast, 10.0));
        CHECK (! canvas->getPreviewStatus().measuredShown);
        CHECK (canvas->getPreviewStatus().text.startsWith ("Updating"));
    }

    CHECK (pumpUntil (current, renderTimeout));
    editor->removeFromDesktop();
    editor.reset();
    pumpFor (20);
}

ARTFREQ_TEST (canvasRemembersItsMeasurementAcrossWindows)
{
    // Closing the plug-in's window and opening it again on the same project shows the measured
    // preview at once (before a new one could even start), for both plug-ins.
    //
    // The canvas's clock starts when it is first painted (on a slow machine a window takes a while
    // to come up), so the time is taken from then. The window is opened again three times: each
    // time it must show the measurement the first one made (none made anew), and the quickest must
    // be in before a new one could even start (a busy machine can stall any one of them).
    for (const auto variant : { ArtFreqProcessor::Variant::instrument, ArtFreqProcessor::Variant::effect })
    {
        ArtFreqProcessor processor (variant);
        processor.setRateAndBufferSizeDetails (48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        auto quickest = 1.0e9;

        for (int window = 0; window < 4; ++window)
        {
            const auto remembered = processor.getPreviewMemory();   // what the window before measured
            std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor.createEditor()));
            editor->addToDesktop (0);
            editor->setVisible (true);
            auto* canvas = dynamic_cast<ui::CanvasView*> (findById (*editor, "canvas"));
            CHECK (canvas != nullptr);

            if (canvas == nullptr)
                return;

            const auto painted = [&] { const auto s = canvas->getPreviewStatus(); return s.fastCurrent || s.measuredShown; };
            CHECK (pumpUntil (painted, 10.0));
            const auto opened = seconds();
            const auto current = [&] { const auto s = canvas->getPreviewStatus(); return s.measuredShown && s.current; };

            // (The first window renders it: ArtFreq FX's takes seconds on a slow machine.)
            CHECK (pumpUntil (current, window == 0 ? 60.0 : 10.0));
            const auto took = seconds() - opened;

            if (window > 0)
            {
                std::printf ("    %s, opened again: the measured preview after %.0f ms\n", variant == ArtFreqProcessor::Variant::effect ? "ArtFreq FX" : "ArtFreq",
                             took * 1000.0);
                CHECK (remembered != nullptr && processor.getPreviewMemory() == remembered);   // the one measured before, not a new one
                quickest = std::min (quickest, took);
            }

            editor->removeFromDesktop();
            editor.reset();
            pumpFor (50);
        }

        CHECK_LESS (quickest, 0.45);   // a first render waits 0.5 s after opening, then 0.2 s more, then renders
    }
}

ARTFREQ_TEST (fastPreviewLeavesTheMessageThreadFree)
{
    // The dense project, at Retina sizes (2x: 1528 x 924; 200 % on Retina, 4x: 3056 x 1848): a
    // drag step's new picture is made on the fast preview's thread; the message thread is given
    // the last picture at once, and the new one is in shortly after. (Its cost is pinned
    // loosely: it runs in the background; on this kind of machine about 15 / 30 ms for ArtFreq
    // and 50 / 90 ms for ArtFreq FX's Carve + Draw, see ArtFreqTruthLab --drag.)
    for (const auto effect : { false, true })
    {
        ArtFreqProcessor processor (effect ? ArtFreqProcessor::Variant::effect : ArtFreqProcessor::Variant::instrument);
        processor.setRateAndBufferSizeDetails (48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        const auto first = testutil::makeDenseProject (processor);

        if (effect)
            testutil::setParameter (processor, params::id::mode, 1.0f);

        processor.rebuildPlanIfNeeded();

        for (const auto& size : { std::pair<int, int> { 1528, 924 }, { 3056, 1848 } })
        {
            const auto width = size.first, height = size.second;
            ui::FastPreview fast (nullptr);
            const auto requestNow = [&]
            {
                const auto m = ui::MeasureRequest::forProcessor (processor, width, height);
                ui::FastRequest request;
                request.effect = effect;
                request.plan = processor.getCurrentPlan();
                request.model.scale = m.scale;
                request.model.slopeDbPerOctave = m.slopeDbPerOctave;
                request.model.view = m.view;
                request.model.sampleRate = m.sampleRate;
                request.model.passSeconds = m.passSeconds;
                request.model.outputGain = ui::previewGain (processor.getOutputGain());
                request.width = width;
                request.height = height;

                if (effect)
                {
                    request.settings.mode = m.mode;
                    request.settings.depthDb = m.depthDb;
                    request.settings.view = m.view;
                    request.settings.sampleRate = m.sampleRate;
                    request.settings.passSeconds = m.passSeconds;
                    request.masks = processor.getCurrentMasks();
                }

                return request;
            };

            const auto made = seconds();
            const auto firstPicture = fast.get (requestNow(), true);
            const auto makeSeconds = seconds() - made;
            CHECK (firstPicture.isValid() && firstPicture.getWidth() == width);

            for (int step = 1; step <= 3; ++step)
            {
                processor.editDocument ([first, step] (ArtDocument& d) { if (auto* o = d.find (first)) o->startTime = 0.02f + 0.01f * static_cast<float> (step); });
                processor.rebuildPlanIfNeeded();
                const auto request = requestNow();
                const auto asked = seconds();
                const auto shown = fast.get (request);
                const auto askSeconds = seconds() - asked;
                CHECK_LESS (askSeconds, 0.005);
                CHECK (shown.isValid());
                CHECK (! fast.isCurrent());
                // In shortly after, and counted: the thread counts a picture just after handing it
                // over, and on a busy machine (or one core) the message thread can see the picture
                // before the count.
                CHECK (pumpUntil ([&] { fast.get (request); return fast.isCurrent() && fast.getBackgroundPictures() >= step; }, 10.0));
            }

            std::printf ("    %s %d x %d: a picture %.0f ms (background), asking for one on the message thread < 5 ms\n",
                         effect ? "ArtFreq FX, Carve + Draw" : "ArtFreq", width, height, makeSeconds * 1000.0);
            CHECK_LESS (makeSeconds, width > 2000 ? 0.6 : 0.35);
            CHECK (fast.getBackgroundPictures() == 3);
        }
    }
}

ARTFREQ_TEST (fastPreviewShowsTheMeasurementWhereTheDrawingDidNotChange)
{
    // The dense project measured; then its first word (the left third) moves a little. The fast
    // preview shows the measurement over the right half (the drawing there did not change), so
    // it equals the measured picture there, and the model where the word moved.
    ArtFreqProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    const auto first = testutil::makeDenseProject (processor);
    processor.rebuildPlanIfNeeded();

    constexpr int width = 600, height = 300;
    const auto before = ui::MeasureRequest::forProcessor (processor, width, height);
    const auto measured = ui::MeasuredPreview::renderNow (before);
    CHECK (measured != nullptr);

    if (measured == nullptr)
        return;

    processor.editDocument ([first] (ArtDocument& d) { if (auto* o = d.find (first)) o->startTime += 0.03f; });
    processor.rebuildPlanIfNeeded();

    ui::FastRequest request;
    request.plan = processor.getCurrentPlan();
    request.model.scale = before.scale;
    request.model.view = before.view;
    request.model.sampleRate = before.sampleRate;
    request.model.passSeconds = before.passSeconds;
    request.model.outputGain = before.outputGain;
    request.width = width;
    request.height = height;

    ui::FastPreview modelOnly (nullptr), shown (nullptr);
    const auto model = modelOnly.get (request, true);
    request.measured = measured->analysis;
    request.measuredRequest = before;
    const auto patched = shown.get (request, true);

    // Colour by colour against the measured picture: the right half the same, the left part not.
    const auto differs = [&] (const juce::Image& image, int x0, int x1)
    {
        auto count = 0, total = 0;

        for (int y = 0; y < height; y += 2)
            for (int x = x0; x < x1; x += 2)
            {
                const auto& m = measured->pixels[static_cast<size_t> (y) * width + static_cast<size_t> (x)];
                const auto c = image.getPixelAt (x, y);
                count += std::abs (c.getRed() - m.getRed()) + std::abs (c.getGreen() - m.getGreen()) + std::abs (c.getBlue() - m.getBlue()) > 12 ? 1 : 0;
                ++total;
            }

        return static_cast<double> (count) / total;
    };

    std::printf ("    right half: model %.0f %% of pixels off the measurement, shown %.1f %%; where the word moved: shown %.0f %%\n",
                 differs (model, width / 2, width) * 100.0, differs (patched, width / 2, width) * 100.0, differs (patched, 0, width / 4) * 100.0);
    CHECK_LESS (differs (patched, width / 2, width), 0.01);
    CHECK_GREATER (differs (model, width / 2, width), 0.05);
}
