// The measured preview: the real sound, analysed as the user's analyser will show it.

#include "ProcessorTestUtils.h"
#include "TestFramework.h"
#include "TestPlans.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/SpectrumAnalysis.h"
#include "dsp/StandInSong.h"
#include "dsp/WavExporter.h"
#include "ui/CanvasView.h"
#include "ui/Editor.h"
#include "ui/MeasuredPreview.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

using namespace artfreq;

namespace
{
    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;

        std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    double secondsSince (std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    }
} // namespace

ARTFREQ_TEST (spectrumAnalyserReadsASineAtItsLevel)
{
    // A full-scale sine on a bin reads 0 dB; half of it -6 dB; between bins, the rows read the
    // spectrum interpolated, never brighter than the sine.
    constexpr double rate = 48000.0;
    constexpr int size = 8192;
    SpectrumAnalyser analyser (size);
    std::vector<float> signal (size), bins (static_cast<size_t> (analyser.getNumBins()));
    const auto bin = 200;
    const auto hz = bin * rate / size;

    for (int n = 0; n < size; ++n)
        signal[static_cast<size_t> (n)] = 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * hz * n / rate));

    analyser.analyse (signal.data(), bins.data());
    CHECK_NEAR (bins[bin], -6.02f, 0.05f);
    CHECK_LESS (bins[bin + 3], -40.0f);

    SpectrumRows rows;
    FrequencyScale scale;
    rows.prepare (scale, 600, rate, size, 0.0f);
    std::vector<float> rowDb (600);
    rows.apply (bins.data(), rowDb.data());
    const auto loudest = *std::max_element (rowDb.begin(), rowDb.end());
    CHECK_NEAR (loudest, -6.02f, 0.3f);

    // The slope tilts the picture about 1 kHz: +3 dB/oct reads 3 dB brighter an octave up.
    SpectrumRows sloped;
    sloped.prepare (scale, 600, rate, size, 3.0f);
    std::vector<float> slopedDb (600);
    sloped.apply (bins.data(), slopedDb.data());
    const auto peakRow = static_cast<size_t> (std::max_element (rowDb.begin(), rowDb.end()) - rowDb.begin());
    CHECK_NEAR (slopedDb[peakRow] - rowDb[peakRow], 3.0f * static_cast<float> (std::log2 (hz / 1000.0)), 0.2f);
}

ARTFREQ_TEST (analyserViewIsSanitised)
{
    AnalyserView view;
    view.fftSize = 5000;
    view.floorDb = -30.0f;
    view.ceilingDb = -40.0f;
    const auto v = view.sanitised();
    CHECK (v.fftSize == 8192);
    CHECK_NEAR (v.ceilingDb - v.floorDb, AnalyserView::minSpanDb, 0.001f);
    CHECK (colourLevelFor (-100.0f, -100.0f, 0.0f) == 0);
    CHECK (colourLevelFor (0.0f, -100.0f, 0.0f) == 255);
    CHECK (colourLevelFor (12.0f, -100.0f, 0.0f) == 255);
    CHECK (colourLevelFor (-50.0f, -100.0f, 0.0f) == 128);
}

ARTFREQ_TEST (standInSongHasTheTexturesLevels)
{
    // The stand-in, made into sound and analysed at 4096 points, reads the levels the fast
    // preview draws (the median over a few seconds, at every height of the scale).
    constexpr double rate = 48000.0;
    constexpr int size = 4096;
    const FrequencyScale scale;
    const auto passSamples = static_cast<int64_t> (8.0 * rate);
    StandInSong song (rate, passSamples, 0);
    std::vector<float> audio (static_cast<size_t> (4.0 * rate));
    song.render (audio.data(), static_cast<int> (audio.size()));

    SpectrumAnalyser analyser (size);
    std::vector<float> bins (static_cast<size_t> (analyser.getNumBins()));
    std::vector<double> differences, lows, highs;
    constexpr double edges[] { 20.0, 60.0, 120.0, 250.0, 500.0, 1000.0, 2000.0 };
    std::vector<double> byRegion[6];

    for (int start = 0; start + size <= static_cast<int> (audio.size()); start += 777)
    {
        analyser.analyse (audio.data() + start, bins.data());
        const auto x = (start + size / 2) / static_cast<double> (passSamples);

        for (int bin = 2; bin < analyser.getNumBins() - 1; bin += 3)
        {
            const auto hz = bin * rate / size;

            if (hz < scale.minHz || hz > scale.maxHz)
                continue;

            const auto d = static_cast<double> (bins[static_cast<size_t> (bin)]) - standInSongTextureDb (x, hz);
            differences.push_back (d);
            (hz < 300.0 ? lows : highs).push_back (d);

            for (size_t r = 0; r < 6; ++r)
                if (hz >= edges[r] && hz < edges[r + 1])
                    byRegion[r].push_back (d);
        }
    }

    std::printf ("    stand-in at FFT 4096, measured - texture: median %.2f dB (lows %.2f, highs %.2f); 20-60-120-250-500-1k-2k Hz:",
                 median (differences), median (lows), median (highs));

    for (const auto& region : byRegion)
        std::printf (" %.1f", median (region));

    std::printf ("\n");
    CHECK_NEAR (median (differences), 0.0, 1.0);
    CHECK_NEAR (median (lows), 0.0, 2.0);
    CHECK_NEAR (median (highs), 0.0, 2.0);

    // The same timeline gives the same noise, wherever the render starts.
    StandInSong later (rate, passSamples, 12345);
    std::vector<float> part (2000);
    later.render (part.data(), static_cast<int> (part.size()));
    auto same = true;

    for (size_t i = 0; i < part.size(); ++i)
        same = same && std::abs (part[i] - audio[12345 + i]) < 1.0e-5f;

    CHECK (same);
}

ARTFREQ_TEST (measuredPreviewShowsTheRealLevels)
{
    // A steady band at 1 - 2 kHz: the measured preview reads it at the level an analyser shows
    // for its loudest line (the plan's amplitude, Level included), dark above and below.
    Canvas canvas;
    const FrequencyScale scale;
    const auto rowOfHz = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * Canvas::height); };

    for (int y = rowOfHz (2000.0); y < rowOfHz (1000.0); ++y)
        for (int x = 0; x < Canvas::width; ++x)
            canvas.at (x, y) = 1.0f;

    PlanSettings settings;
    settings.scale = scale;
    settings.density = 1024;
    std::shared_ptr<ArtPlan> plan (buildPlan (canvas, settings).release());
    plan->version = 1;

    ui::MeasureRequest request;
    request.plan = plan;
    request.outputGain = 0.5f;
    request.passSeconds = 4.0;
    request.scale = scale;
    request.width = 300;
    request.height = 400;

    const auto started = std::chrono::steady_clock::now();
    const auto result = ui::MeasuredPreview::renderNow (request);
    std::printf ("    4 s pass, density 1024, 300 x 400: %.0f ms\n", secondsSince (started) * 1000.0);
    CHECK (result != nullptr);

    if (result == nullptr)
        return;

    auto loudest = 0.0f;

    for (int line = 0; line < plan->numLines; ++line)
        loudest = std::max (loudest, plan->amplitude (Canvas::width / 2, line));

    const auto expected = 20.0f * std::log10 (loudest * request.outputGain);
    const auto& a = *result->analysis;
    const auto rowAt = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * request.height); };
    const auto middle = a.frames / 2;
    auto bandTop = -200.0f;

    for (int y = rowAt (1900.0); y <= rowAt (1100.0); ++y)
        bandTop = std::max (bandTop, a.outputDb (y, middle));

    std::printf ("    band reads %.1f dB, its loudest line %.1f dB\n", bandTop, expected);
    CHECK_NEAR (bandTop, expected, 3.0f);
    CHECK_LESS (a.outputDb (rowAt (5000.0), middle), expected - 60.0f);
    CHECK_LESS (a.outputDb (rowAt (200.0), middle), expected - 60.0f);
}

//==============================================================================
namespace
{
    /** A plan of a band at 1 - 2 kHz across the whole length (a fully drawn canvas, density 1024,
        with `full`). */
    std::shared_ptr<const ArtPlan> bandPlan (bool full = false, uint64_t version = 1)
    {
        Canvas canvas;
        const FrequencyScale scale;
        const auto rowOfHz = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * Canvas::height); };

        for (int y = full ? 0 : rowOfHz (2000.0); y < (full ? Canvas::height : rowOfHz (1000.0)); ++y)
            for (int x = 0; x < Canvas::width; ++x)
                canvas.at (x, y) = 1.0f;

        PlanSettings settings;
        settings.scale = scale;
        settings.density = 1024;
        settings.lowEnd = LowEnd::smooth;
        std::shared_ptr<ArtPlan> plan (buildPlan (canvas, settings).release());
        plan->version = version;
        return plan;
    }

    ui::MeasureRequest smallRequest (std::shared_ptr<const ArtPlan> plan)
    {
        ui::MeasureRequest request;
        request.plan = std::move (plan);
        request.outputGain = 0.5f;
        request.passSeconds = 2.0;
        request.width = 120;
        request.height = 80;
        return request;
    }

    double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    void sleepMs (int ms) { juce::Thread::sleep (ms); }

    /** Calls update() as the canvas's timer does, until a result is in (or `timeout` seconds). */
    std::unique_ptr<ui::MeasuredPreview::Result> waitForResult (ui::MeasuredPreview& preview, const ui::MeasureRequest& request,
                                                               double timeout)
    {
        for (const auto start = now(); now() - start < timeout;)
        {
            preview.update (request, true);

            if (auto result = preview.takeResult())
                return result;

            sleepMs (10);
        }

        return nullptr;
    }
} // namespace

ARTFREQ_TEST (measuredPreviewWaitsForTheDrawingToStayStill)
{
    // While the request keeps changing (a drag), nothing renders; once it has stayed the same for
    // the debounce time, it renders once. Not allowed to start (the canvas not on screen), it
    // never does.
    auto request = smallRequest (bandPlan());

    {
        ui::MeasuredPreview hidden;

        for (int i = 0; i < 40; ++i)
        {
            hidden.update (request, false);
            sleepMs (10);
        }

        CHECK (hidden.getRendersStarted() == 0);
        CHECK (hidden.takeResult() == nullptr);
    }

    ui::MeasuredPreview preview;
    auto started = now();

    for (int i = 0; i < 8; ++i)
    {
        request.outputGain = 0.5f + 0.01f * static_cast<float> (i);
        started = now();
        preview.update (request, true);
        CHECK (preview.isBusy());
        sleepMs (40);
    }

    CHECK (preview.getRendersStarted() == 0);

    const auto result = waitForResult (preview, request, 5.0);
    CHECK (result != nullptr);

    if (result == nullptr)
        return;

    std::printf ("    the result was in %.0f ms after the last change (debounce %.0f ms)\n", (now() - started) * 1000.0,
                 ui::MeasuredPreview::debounceSeconds * 1000.0);
    CHECK (result->request == request);
    CHECK (now() - started >= ui::MeasuredPreview::debounceSeconds - 0.02);
    CHECK (preview.getRendersStarted() == 1);
    CHECK (result->pixels.size() == static_cast<size_t> (request.width * request.height));

    // The same request again: nothing more to do.
    for (int i = 0; i < 30; ++i)
    {
        preview.update (request, true);
        sleepMs (10);
    }

    CHECK (preview.getRendersStarted() == 1);
    CHECK (! preview.isBusy());
}

ARTFREQ_TEST (measuredPreviewStopsWhatIsNoLongerWanted)
{
    // A long render (a 2-minute pass of a fully drawn canvas at density 1024), and then another
    // request: the long one stops within a block, never delivers, and the new one comes in well
    // before the long one could have finished.
    auto slow = smallRequest (bandPlan (true, 2));
    slow.passSeconds = 120.0;
    slow.width = 400;

    const auto timed = now();
    const auto whole = ui::MeasuredPreview::renderNow (slow);
    const auto wholeSeconds = now() - timed;
    std::printf ("    the long render alone: %.0f ms\n", wholeSeconds * 1000.0);
    CHECK (whole != nullptr);

    ui::MeasuredPreview preview;

    for (const auto start = now(); preview.getRendersStarted() == 0 && now() - start < 3.0;)
    {
        preview.update (slow, true);
        sleepMs (5);
    }

    CHECK (preview.getRendersStarted() == 1);
    sleepMs (50);   // well into it

    const auto quick = smallRequest (bandPlan (false, 3));
    const auto changed = now();
    const auto result = waitForResult (preview, quick, 10.0);
    const auto waited = now() - changed;
    std::printf ("    the new result was in %.0f ms after the change\n", waited * 1000.0);

    CHECK (result != nullptr && result->request == quick);
    CHECK (preview.getRendersFinished() == 1);
    CHECK_LESS (waited, std::max (0.6, wholeSeconds * 0.6));
}

ARTFREQ_TEST (measuredPreviewRecoloursWithoutRendering)
{
    // A new colour range (Settings' levels) recolours the analysis it has, at once.
    ui::MeasuredPreview preview;
    auto request = smallRequest (bandPlan());
    const auto first = waitForResult (preview, request, 5.0);
    CHECK (first != nullptr);

    if (first == nullptr)
        return;

    request.view.floorDb = -80.0f;
    const auto changed = now();
    const auto second = waitForResult (preview, request, 2.0);
    CHECK (second != nullptr);

    if (second == nullptr)
        return;

    CHECK (second->analysis == first->analysis);
    CHECK_LESS (now() - changed, ui::MeasuredPreview::debounceSeconds);
    CHECK (second->pixels.size() == first->pixels.size());
    CHECK (std::memcmp (second->pixels.data(), first->pixels.data(), first->pixels.size() * sizeof (juce::PixelARGB)) != 0);
}

ARTFREQ_TEST (measuredPreviewOfTheEffectIsOnTime)
{
    // ArtFreq FX: a band carved from 30 % to 70 % of the pass shows there, not a latency late:
    // dark over 1 - 2 kHz in the middle, the stand-in song before and after it.
    Canvas canvas;
    const FrequencyScale scale;
    const auto rowOfHz = [&] (double hz) { return static_cast<int> ((1.0 - scale.toNorm (hz)) * Canvas::height); };

    for (int y = rowOfHz (2000.0); y < rowOfHz (1000.0); ++y)
        for (int x = Canvas::width * 3 / 10; x < Canvas::width * 7 / 10; ++x)
            canvas.at (x, y) = 1.0f;

    auto masks = std::make_shared<MaskPlanSet>();
    MaskSettings settings;
    settings.scale = scale;
    masks->exact = buildMaskPlan (canvas, settings);
    masks->halo = widenMaskPlan (*masks->exact, 4, 8);

    ui::MeasureRequest request;
    request.effect = true;
    request.masks = masks;
    request.mode = EffectMode::carve;
    request.depthDb = -80.0f;
    request.passSeconds = 8.0;
    request.scale = scale;
    request.width = 400;
    request.height = 200;

    const auto result = ui::MeasuredPreview::renderNow (request);
    CHECK (result != nullptr);

    if (result == nullptr)
        return;

    const auto& a = *result->analysis;
    CHECK (a.input.size() == a.output.size());
    const auto row = static_cast<int> ((1.0 - scale.toNorm (1400.0)) * a.rows);
    const auto at = [&] (double fraction) { return std::min (a.frames - 1, static_cast<int> (fraction * a.frames)); };
    const auto carved = [&] (double fraction) { return a.inputDb (row, at (fraction)) - a.outputDb (row, at (fraction)); };

    std::printf ("    carved at 20 / 27 / 33 / 50 / 67 / 73 / 80 %%: %.0f %.0f %.0f %.0f %.0f %.0f %.0f dB\n", carved (0.2), carved (0.27),
                 carved (0.33), carved (0.5), carved (0.67), carved (0.73), carved (0.8));
    CHECK_GREATER (carved (0.5), 40.0f);
    CHECK_GREATER (carved (0.33), 20.0f);
    CHECK_GREATER (carved (0.67), 20.0f);
    CHECK_LESS (std::abs (carved (0.2)), 1.0f);
    CHECK_LESS (std::abs (carved (0.8)), 1.0f);

    // Symmetric about the band: a latency left in would carve after it, not before.
    CHECK_LESS (std::abs (carved (0.27) - carved (0.73)), 6.0f);
}

ARTFREQ_TEST (analyserSettingsAreSavedWithTheProject)
{
    // FFT size and levels are saved with the state and loaded back (both plug-ins); a state from
    // before them loads the defaults.
    for (const auto variant : { ArtFreqProcessor::Variant::instrument, ArtFreqProcessor::Variant::effect })
    {
        ArtFreqProcessor source (variant);
        testutil::setParameter (source, params::id::analyserFft, 2.0f);
        testutil::setParameter (source, params::id::analyserFloor, -90.0f);
        testutil::setParameter (source, params::id::analyserCeiling, -12.0f);
        CHECK (source.getAnalyserView().fftSize == 16384);

        juce::MemoryBlock state;
        source.getStateInformation (state);

        ArtFreqProcessor loaded (variant);
        loaded.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        const auto view = loaded.getAnalyserView();
        CHECK (view.fftSize == 16384);
        CHECK_NEAR (view.floorDb, -90.0f, 0.001f);
        CHECK_NEAR (view.ceilingDb, -12.0f, 0.001f);

        // The same state without them (as 0.5 saved it): the defaults.
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        CHECK (xml != nullptr);

        if (xml == nullptr)
            continue;

        if (auto* parameters = xml->getChildByName ("ArtFreqParameters"))
            for (auto* child = parameters->getFirstChildElement(); child != nullptr;)
            {
                auto* next = child->getNextElement();

                if (child->getStringAttribute ("id").startsWith ("analyser"))
                    parameters->removeChildElement (child, true);

                child = next;
            }

        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        loaded.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
        const auto defaults = loaded.getAnalyserView();
        CHECK (defaults.fftSize == 8192);
        CHECK_NEAR (defaults.floorDb, -100.0f, 0.001f);
        CHECK_NEAR (defaults.ceilingDb, 0.0f, 0.001f);

        // A range turned inside out keeps its floor and spans at least 20 dB.
        testutil::setParameter (loaded, params::id::analyserFloor, -40.0f);
        testutil::setParameter (loaded, params::id::analyserCeiling, -60.0f);
        CHECK_NEAR (loaded.getAnalyserView().ceilingDb - loaded.getAnalyserView().floorDb, AnalyserView::minSpanDb, 0.001f);
    }
}

//==============================================================================
namespace
{
    juce::Component* findById (juce::Component& root, const juce::String& id)
    {
        if (root.getComponentID() == id)
            return &root;

        for (auto* child : root.getChildren())
            if (auto* found = findById (*child, id))
                return found;

        return nullptr;
    }

    void pumpFor (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    /** Pumps the message loop until `condition` (or `timeout` seconds); returns whether it held. */
    template <typename Condition>
    bool pumpUntil (Condition&& condition, double timeout)
    {
        for (const auto start = now(); now() - start < timeout;)
        {
            if (condition())
                return true;

            pumpFor (20);
        }

        return condition();
    }
} // namespace

ARTFREQ_TEST (canvasCrossFadesToTheMeasuredPreview)
{
    // The editor on screen: the canvas starts with the fast preview ("Updating..."), and once the
    // drawing has stayed still a moment fades to the measured one ("Preview of the real sound").
    // A change to the drawing or to the sound goes back to the fast preview at once; a change of
    // Level alone moves the measured picture by as many dB.
    ArtFreqProcessor processor;
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

    pumpFor (150);
    CHECK (! canvas->getPreviewStatus().measuredShown);
    CHECK (canvas->getPreviewStatus().text.startsWith ("Updating"));

    auto sawFade = false;
    const auto measured = pumpUntil ([&]
    {
        sawFade = sawFade || canvas->getPreviewStatus().fading;
        const auto status = canvas->getPreviewStatus();
        return status.measuredShown && status.current && ! status.fading;
    }, 8.0);

    CHECK (measured);
    CHECK (sawFade);
    CHECK (canvas->getPreviewStatus().text == "Preview of the real sound");

    // Level (the clipper untouched): the sound is the same times a gain, so the measured picture
    // moves by as many dB at once, without a new render, and stays on screen meanwhile.
    // (Until the review of the measured preview it stayed, stale, for a whole new render.)
    testutil::setParameter (processor, params::id::level, -12.0f);
    auto alwaysMeasured = true;
    const auto relevelled = pumpUntil ([&]
    {
        alwaysMeasured = alwaysMeasured && canvas->getPreviewStatus().measuredShown;
        return canvas->getPreviewStatus().current;
    }, 0.6);
    CHECK (relevelled);
    CHECK (alwaysMeasured);

    // Density changes the sound itself: the fast preview at once (it follows the change within a
    // frame), whether the change came from the mouse, the wheel, the keyboard or the host; the
    // measured one when it is in. (It used to keep the stale measured picture unless the mouse
    // was down.)
    testutil::setParameter (processor, params::id::density, 64.0f);
    pumpFor (80);
    CHECK (! canvas->getPreviewStatus().measuredShown);
    CHECK (canvas->getPreviewStatus().text.startsWith ("Updating"));
    CHECK (pumpUntil ([&] { return canvas->getPreviewStatus().current && canvas->getPreviewStatus().measuredShown; }, 8.0));

    // The drawing: the fast preview at once, the measured one when it is in.
    processor.editDocument ([] (ArtDocument& document)
    {
        for (auto& object : document.objects)
            object.startTime = std::max (0.0f, object.startTime - 0.1f);
    });

    pumpFor (80);
    CHECK (! canvas->getPreviewStatus().measuredShown);
    CHECK (pumpUntil ([&] { return canvas->getPreviewStatus().current && canvas->getPreviewStatus().measuredShown; }, 8.0));

    // Loading a project: the same.
    juce::MemoryBlock state;
    {
        ArtFreqProcessor other;
        ArtDocument document;
        document.objects.push_back (ArtDocument::makeTextObject ("LOADED", 3000.0f, 9000.0f));
        other.setDocument (document);
        other.getStateInformation (state);
    }

    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    pumpFor (80);
    CHECK (! canvas->getPreviewStatus().current);
    CHECK (pumpUntil ([&] { return canvas->getPreviewStatus().current; }, 8.0));

    editor->removeFromDesktop();
    editor.reset();
    pumpFor (20);
}
