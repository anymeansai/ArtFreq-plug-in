// The truth lab's scenarios (ArtFreqTruthLab --scenarios <out-dir> [--only a,b]): plays drawings
// through the activated processor in conditions the screenshot states do not vary (sample rate,
// block size, tempo, Repeat, slope, scale, Density, Lows, FFT, range, ArtFreq FX's modes and
// songs other than the stand-in the stand-in was tuned on), analyses the output with an analyser
// written here, and compares it per band with the canvas's measured preview (exactly as the editor
// renders it, from MeasureRequest::forProcessor after the pass played) and with its fast preview
// (what shows while dragging). Writes scenario-<name>.png (measured | fast | real, and the
// differences) and scenarios.txt. First written by a review of the measured preview, to attack it.

#include "DenseProject.h"
#include "LicenseTestSupport.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/Fonts.h"
#include "core/FrequencyScale.h"
#include "core/ImageProcessing.h"
#include "dsp/StandInSong.h"
#include "ui/CanvasPlanImage.h"
#include "ui/EffectPreview.h"
#include "ui/FastPreview.h"
#include "ui/MeasuredPreview.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace
{
using namespace artfreq;
template <typename I> size_t idx (I i) { return static_cast<size_t> (i); }

const double nan = std::numeric_limits<double>::quiet_NaN();

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

void setParameter (ArtFreqProcessor& p, const char* id, float value)
{
    if (auto* parameter = p.getParameters().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

enum class Song { none, standIn, bassHeavy, quiet, bassOnly };

struct Scenario
{
    juce::String name;
    bool effect = false;
    double sr = 48000.0;
    int block = 512;
    double bpm = 120.0;
    int passes = 1;              // > 1: Repeat on; the second pass is compared
    Song song = Song::standIn;
    int w = 1200, h = 560;
    std::function<void (ArtFreqProcessor&)> setUp;
};

struct Played
{
    std::vector<float> out, in;
    int64_t passStart = 0, passSamples = 0;
    int latency = 0;
};

/** A bass-heavy song unlike the stand-in: pink noise, tilted down, with a loud sub bass at 45 Hz
    and a kick at 55 Hz every beat. */
std::vector<float> makeBassSong (double sr, int64_t n, double bpm)
{
    std::vector<float> s (idx (n));
    juce::Random r (7);
    double b0 = 0, b1 = 0, b2 = 0, lp = 0;
    const auto beat = 60.0 / bpm * sr;

    for (int64_t i = 0; i < n; ++i)
    {
        const auto white = r.nextFloat() * 2.0 - 1.0;
        b0 = 0.99765 * b0 + white * 0.0990460;
        b1 = 0.96300 * b1 + white * 0.2965164;
        b2 = 0.57000 * b2 + white * 1.0526913;
        auto pink = (b0 + b1 + b2 + white * 0.1848) * 0.05;
        lp += 0.2 * (pink - lp);
        const auto t = static_cast<double> (i) / sr;
        const auto sinceBeat = std::fmod (static_cast<double> (i), beat) / sr;
        const auto kick = 0.5 * std::exp (-sinceBeat * 12.0) * std::sin (juce::MathConstants<double>::twoPi * 55.0 * sinceBeat);
        const auto bass = 0.25 * std::sin (juce::MathConstants<double>::twoPi * 45.0 * t);
        s[idx (i)] = static_cast<float> (0.6 * lp + kick + bass);
    }

    return s;
}

Played play (ArtFreqProcessor& p, const Scenario& sc)
{
    Played r;
    const auto stopped = static_cast<int64_t> (0.5 * sc.sr);

    {
        // A stopped block first, so the processor knows the host's tempo before the length is read.
        TestPlayHead prime;
        prime.info.setIsPlaying (false);
        prime.info.setBpm (sc.bpm);
        prime.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        p.setPlayHead (&prime);
        juce::AudioBuffer<float> b (2, sc.block);
        juce::MidiBuffer m;
        b.clear();
        p.processBlock (b, m);
        p.setPlayHead (nullptr);
        p.reset();
    }

    const auto passSamples = static_cast<int64_t> (std::llround (p.getLengthSeconds() * sc.sr));
    r.passSamples = passSamples;
    r.passStart = stopped + (sc.passes > 1 ? passSamples : 0);   // Repeat: the second pass
    r.latency = p.getLatencySamples();
    const auto timeline = stopped + passSamples * sc.passes + static_cast<int64_t> (1.0 * sc.sr);
    const auto total = timeline + r.latency;

    std::vector<float> songData;

    if (sc.effect)
    {
        songData.assign (idx (total), 0.0f);

        if (sc.song == Song::standIn || sc.song == Song::quiet)
        {
            // The stand-in, on the timeline of the pass that is compared: the canvas carves the
            // same song, so any difference is the canvas's.
            std::vector<float> tmp (idx (total - stopped));
            StandInSong s2 (sc.sr, passSamples, stopped - r.passStart);
            s2.render (tmp.data(), static_cast<int> (tmp.size()));
            const auto g = sc.song == Song::quiet ? 0.1f : 1.0f;

            for (size_t i = 0; i < tmp.size(); ++i)
                songData[idx (stopped) + i] = tmp[i] * g;
        }
        else if (sc.song == Song::bassHeavy || sc.song == Song::bassOnly)
        {
            auto b = makeBassSong (sc.sr, total - stopped, sc.bpm);

            if (sc.song == Song::bassOnly)
            {
                // A bass track: the kick and the sub alone (no noise bed): nothing above ~200 Hz.
                const auto beat = 60.0 / sc.bpm * sc.sr;

                for (size_t i = 0; i < b.size(); ++i)
                {
                    const auto t = static_cast<double> (i) / sc.sr;
                    const auto sinceBeat = std::fmod (static_cast<double> (i), beat) / sc.sr;
                    b[i] = static_cast<float> (0.5 * std::exp (-sinceBeat * 12.0) * std::sin (juce::MathConstants<double>::twoPi * 55.0 * sinceBeat)
                                               + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 45.0 * t));
                }
            }

            std::copy (b.begin(), b.end(), songData.begin() + static_cast<std::ptrdiff_t> (stopped));
        }
    }

    TestPlayHead head;
    p.setPlayHead (&head);
    juce::AudioBuffer<float> buffer (2, sc.block);
    juce::MidiBuffer midi;
    std::vector<float> out (idx (total));
    r.in.assign (idx (timeline), 0.0f);

    for (int64_t done = 0; done < total; done += sc.block)
    {
        const auto n = static_cast<int> (std::min (static_cast<int64_t> (sc.block), total - done));
        const auto playing = done >= stopped;
        const auto seconds = static_cast<double> (done - stopped) / sc.sr;
        const auto ppq = seconds * sc.bpm / 60.0;
        head.info = {};
        head.info.setIsPlaying (playing);
        head.info.setBpm (sc.bpm);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        head.info.setPpqPosition (playing ? ppq : 0.0);
        head.info.setPpqPositionOfLastBarStart (playing ? 4.0 * std::floor (ppq / 4.0) : 0.0);
        head.info.setTimeInSeconds (playing ? seconds : 0.0);
        head.info.setTimeInSamples (playing ? done - stopped : 0);
        buffer.setSize (2, n, false, false, true);
        buffer.clear();

        if (sc.effect)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buffer.setSample (ch, i, songData[idx (done + i)]);

        for (int i = 0; i < n; ++i)
            if (done + i < timeline)
                r.in[idx (done + i)] = sc.effect ? songData[idx (done + i)] : 0.0f;

        midi.clear();
        p.processBlock (buffer, midi);

        for (int i = 0; i < n; ++i)
            out[idx (done + i)] = 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i));
    }

    p.setPlayHead (nullptr);
    r.out.assign (out.begin() + r.latency, out.begin() + r.latency + static_cast<std::ptrdiff_t> (timeline));
    return r;
}

/** An analyser written here: periodic Blackman-Harris (four terms, as the plug-in's), |X| scaled so a full-scale sine reads 0 dB, one
    analysis per pixel column centred on it, rows read as interpolated between the bins around
    the row's centre or the loudest bin inside the row, plus the analyser's slope (pivot 1 kHz). */
std::vector<float> analyse (const std::vector<float>& x, double from, double to, int fft, const FrequencyScale& scale,
                            int w, int h, double sr, float slope)
{
    std::vector<float> db (idx (w) * idx (h), -160.0f);
    std::vector<double> win (idx (fft));
    double sum = 0;

    for (int n = 0; n < fft; ++n)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * n / fft;
        win[idx (n)] = 0.35875 - 0.48829 * std::cos (phase) + 0.14128 * std::cos (2.0 * phase) - 0.01168 * std::cos (3.0 * phase);
        sum += win[idx (n)];
    }

    juce::dsp::FFT f (juce::roundToInt (std::log2 (fft)));
    std::vector<float> work (idx (fft) * 2), bins (idx (fft / 2 + 1));
    const auto s = scale.sanitised();

    for (int col = 0; col < w; ++col)
    {
        const auto centre = static_cast<int64_t> (std::llround (from + (col + 0.5) / w * (to - from)));
        std::fill (work.begin(), work.end(), 0.0f);

        for (int n = 0; n < fft; ++n)
        {
            const auto at = centre - fft / 2 + n;

            if (at >= 0 && at < static_cast<int64_t> (x.size()))
                work[idx (n)] = static_cast<float> (x[idx (at)] * win[idx (n)]);
        }

        f.performFrequencyOnlyForwardTransform (work.data(), true);

        for (int b = 0; b <= fft / 2; ++b)
            bins[idx (b)] = static_cast<float> (20.0 * std::log10 (std::max (1.0e-12, work[idx (b)] * 2.0 / sum)));

        for (int y = 0; y < h; ++y)
        {
            const auto hz = s.toHz (1.0 - (y + 0.5) / h);
            const auto lo = s.toHz (1.0 - (y + 1.0) / h), hi = s.toHz (1.0 - static_cast<double> (y) / h);
            const auto c = hz * fft / sr;

            if (hz >= sr / 2)
                continue;

            const auto b0 = juce::jlimit (0, fft / 2 - 1, static_cast<int> (std::floor (c)));
            auto v = bins[idx (b0)] + (bins[idx (b0 + 1)] - bins[idx (b0)]) * static_cast<float> (c - b0);

            for (auto b = static_cast<int> (std::ceil (lo * fft / sr)); b <= std::min (fft / 2, static_cast<int> (std::ceil (hi * fft / sr)) - 1); ++b)
                v = std::max (v, bins[idx (b)]);

            db[idx (y) * idx (w) + idx (col)] = v + slope * static_cast<float> (std::log2 (hz / 1000.0));
        }
    }

    return db;
}

std::vector<float> dbOfAnalysis (const ui::MeasuredSpectrogram& a, int w, int h)
{
    std::vector<float> db (idx (w) * idx (h));

    for (int x = 0; x < w; ++x)
    {
        int f0 = 0, f1 = 0;
        float t = 0;
        ui::MeasuredSpectrogram::columnPosition (x, w, a.frames, f0, f1, t);

        for (int y = 0; y < h; ++y)
        {
            const auto row = juce::jlimit (0, a.rows - 1, static_cast<int> ((y + 0.5) * a.rows / h));
            db[idx (y) * idx (w) + idx (x)] = a.outputDb (row, f0) + (a.outputDb (row, f1) - a.outputDb (row, f0)) * t;
        }
    }

    return db;
}

std::vector<float> dbOfImage (const juce::Image& image, float lo, float hi)
{
    const auto& table = ui::theme::spectrumTable();
    std::map<uint32_t, int> cache;
    std::vector<float> db (idx (image.getWidth()) * idx (image.getHeight()));
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto c = data.getPixelColour (x, y);
            const auto key = c.getARGB() & 0xffffffu;
            auto it = cache.find (key);

            if (it == cache.end())
            {
                int best = 0, bd = 1 << 30;

                for (int i = 0; i < 256; ++i)
                {
                    const auto& p = table[idx (i)];
                    const auto dr = p.getRed() - c.getRed(), dg = p.getGreen() - c.getGreen(), dbb = p.getBlue() - c.getBlue();
                    const auto d = dr * dr + dg * dg + dbb * dbb;

                    if (d < bd) { bd = d; best = i; }
                }

                it = cache.emplace (key, best).first;
            }

            db[idx (y) * idx (image.getWidth()) + idx (x)] = lo + static_cast<float> (it->second) / 255.0f * (hi - lo);
        }

    return db;
}

juce::Image picture (const std::vector<float>& db, int w, int h, float lo, float hi)
{
    const auto& table = ui::theme::spectrumTable();
    juce::Image im (juce::Image::RGB, w, h, false);
    juce::Image::BitmapData d (im, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto t = (db[idx (y) * idx (w) + idx (x)] - lo) / (hi - lo);
            const auto& p = table[idx (juce::jlimit (0, 255, static_cast<int> (t * 255.0f + 0.5f)))];
            d.setPixelColour (x, y, juce::Colour (p.getRed(), p.getGreen(), p.getBlue()));
        }

    return im;
}

/** Red: the canvas brighter than the real output; blue: darker; 30 dB = full. */
juce::Image diffPicture (const std::vector<float>& a, const std::vector<float>& b, int w, int h, float lo, float hi)
{
    juce::Image im (juce::Image::RGB, w, h, false);
    juce::Image::BitmapData d (im, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto i = idx (y) * idx (w) + idx (x);
            const auto diff = juce::jlimit (lo, hi, a[i]) - juce::jlimit (lo, hi, b[i]);
            const auto t = juce::jlimit (0.0f, 1.0f, std::abs (diff) / 30.0f);
            const auto v = static_cast<uint8_t> (t * 255.0f);
            d.setPixelColour (x, y, diff > 0 ? juce::Colour (v, 0, 0) : juce::Colour (0, 0, v));
        }

    return im;
}

struct Band { const char* name; double lo, hi; };
const Band bands[] { { "lows", 0, 250 }, { "lowmids", 250, 2000 }, { "highs", 2000, 1e9 } };

struct Metrics { double r = nan, mean = nan, absMean = nan, brighter10 = nan, darker10 = nan, excess = nan; int drawn = 0; };

double median (std::vector<double> v)
{
    if (v.empty()) return nan;
    std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2), v.end());
    return v[v.size() / 2];
}

Metrics compare (const std::vector<float>& canvas, const std::vector<float>& real, const std::vector<float>& drawing,
                  const FrequencyScale& scale, int w, int h, const Band& band, float lo, float hi)
{
    Metrics m;
    const auto s = scale.sanitised();
    std::vector<double> a, b, ci, ca, ri, ra;
    double sum = 0, sumAbs = 0;
    int bright = 0, dark = 0;
    const auto gap = std::max (2, static_cast<int> (0.03 * h));

    for (int y = 0; y < h; ++y)
    {
        const auto hz = s.toHz (1.0 - (y + 0.5) / h);

        if (hz < band.lo || hz >= band.hi)
            continue;

        for (int x = 0; x < w; ++x)
        {
            const auto i = idx (y) * idx (w) + idx (x);
            const double c = juce::jlimit (lo, hi, canvas[i]), r = juce::jlimit (lo, hi, real[i]);
            a.push_back (c); b.push_back (r);
            sum += c - r; sumAbs += std::abs (c - r);
            bright += c - r > 10.0 ? 1 : 0;
            dark += r - c > 10.0 ? 1 : 0;
            const auto drawn = drawing[i] >= 0.5f;
            bool near = false;

            for (int dy = -gap; dy <= gap && ! near; ++dy)
                if (y + dy >= 0 && y + dy < h && drawing[idx (y + dy) * idx (w) + idx (x)] > 0.02f)
                    near = true;

            if (drawn) { ci.push_back (c); ri.push_back (r); }
            else if (! near) { ca.push_back (c); ra.push_back (r); }
        }
    }

    if (a.empty()) return m;
    m.mean = sum / static_cast<double> (a.size());
    m.absMean = sumAbs / static_cast<double> (a.size());
    m.brighter10 = 100.0 * bright / static_cast<double> (a.size());
    m.darker10 = 100.0 * dark / static_cast<double> (a.size());
    m.drawn = static_cast<int> (ci.size());

    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double> (a.size()); mb /= static_cast<double> (a.size());
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ab += (a[i] - ma) * (b[i] - mb); aa += (a[i] - ma) * (a[i] - ma); bb += (b[i] - mb) * (b[i] - mb); }
    m.r = aa > 0 && bb > 0 ? ab / std::sqrt (aa * bb) : nan;

    if (ci.size() >= 50 && ! ca.empty())
        m.excess = std::abs (median (ci) - median (ca)) - std::abs (median (ri) - median (ra));

    return m;
}

std::vector<float> drawingGrid (ArtFreqProcessor& p, int w, int h)
{
    Canvas canvas;
    Compositor compositor;
    compositor.render (p.getDocument(), p.getScale(), canvas);
    std::vector<float> g (idx (w) * idx (h));

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto cx = juce::jlimit (0, Canvas::width - 1, static_cast<int> ((x + 0.5) * Canvas::width / w));
            const auto cy = juce::jlimit (0, Canvas::height - 1, static_cast<int> ((y + 0.5) * Canvas::height / h));
            g[idx (y) * idx (w) + idx (x)] = canvas.get (cx, cy);
        }

    return g;
}

juce::String fmt (double v, int d = 1) { return std::isfinite (v) ? juce::String (v, d) : juce::String ("-"); }

bool useStandIn = false;   // --standin: the canvas as before any pass played

bool run (const Scenario& sc, const juce::File& outDir)
{
    auto processor = std::make_unique<ArtFreqProcessor> (sc.effect ? ArtFreqProcessor::Variant::effect : ArtFreqProcessor::Variant::instrument);
    auto& p = *processor;
    p.setPlayConfigDetails (sc.effect ? 2 : 0, 2, sc.sr, sc.block);
    p.prepareToPlay (sc.sr, sc.block);

    if (sc.passes > 1)
        setParameter (p, params::id::repeat, 1.0f);

    // ArtFreq FX keeps the song that comes in, as it does once its editor has been open: the
    // canvas then carves the song that played, not the stand-in (--standin: as before any pass).
    if (sc.effect && ! useStandIn)
        p.enableSongCapture();

    if (sc.setUp)
        sc.setUp (p);

    p.rebuildPlanIfNeeded();

    if (! p.isLicensed())
    {
        std::cerr << "not licensed\n";
        return false;
    }

    const auto played = play (p, sc);
    p.rebuildPlanIfNeeded();

    const auto w = sc.w, h = sc.h;
    const auto scale = p.getScale();
    const auto view = p.getAnalyserView().sanitised();
    const auto slope = p.getPlanSettings().slopeDbPerOctave;

    auto request = ui::MeasureRequest::forProcessor (p, w, h);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    auto result = ui::MeasuredPreview::renderNow (request);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - started;

    if (result == nullptr)
    {
        std::cerr << sc.name << ": no measured preview\n";
        return false;
    }

    const auto measuredDb = dbOfAnalysis (*result->analysis, w, h);

    // The fast preview, as the canvas draws it while dragging.
    ui::AnalyserModel model;
    model.scale = scale;
    model.slopeDbPerOctave = slope;
    model.view = view;
    model.sampleRate = request.sampleRate;
    model.passSeconds = request.passSeconds;
    model.outputGain = ui::previewGain (p.getOutputGain());
    ui::CanvasPlanImage planImage;
    juce::Image fast;

    if (! sc.effect)
    {
        fast = planImage.get (p.getCurrentPlan(), model, w, h);
    }
    else
    {
        if (playsSines (p.getEffectMode()))
            planImage.prepareLevels (p.getCurrentPlan(), model, w, h);

        ui::EffectPreviewImage preview;
        ui::EffectPreviewImage::Settings settings;
        settings.mode = p.getEffectMode();
        settings.depthDb = request.depthDb;
        settings.view = view;
        settings.sampleRate = request.sampleRate;
        settings.passSeconds = request.passSeconds;
        preview.setMeasuredSong (result->analysis);
        fast = preview.get (planImage.getPixelDbBeforeLevel(), planImage.getLevelDb(), 1, settings, p.getCurrentMasks(), scale, w, h);
    }

    const auto fastDb = dbOfImage (fast, view.floorDb, view.ceilingDb);
    const auto from = static_cast<double> (played.passStart), to = from + static_cast<double> (played.passSamples);
    const auto realDb = analyse (played.out, from, to, view.fftSize, scale, w, h, sc.sr, slope);
    const auto drawing = drawingGrid (p, w, h);

    juce::String line;
    line << sc.name << "  (pass " << fmt (static_cast<double> (played.passSamples) / sc.sr, 2) << " s, request "
         << fmt (request.passSeconds, 3) << " s @ " << fmt (request.sampleRate, 0) << ", fft " << view.fftSize << ", range "
         << fmt (view.floorDb, 0) << ".." << fmt (view.ceilingDb, 0) << ", slope " << fmt (slope, 1) << ", measured " << fmt (ms, 0) << " ms"
         << (sc.effect ? (result->analysis->songSerial != 0 ? ", carving the song that played" : ", carving the stand-in") : "") << ")\n";

    for (const auto& band : bands)
    {
        const auto m = compare (measuredDb, realDb, drawing, scale, w, h, band, view.floorDb, view.ceilingDb);
        const auto f = compare (fastDb, realDb, drawing, scale, w, h, band, view.floorDb, view.ceilingDb);
        line << "   " << juce::String (band.name).paddedRight (' ', 8)
             << "MEASURED r " << fmt (m.r, 3) << " mean " << fmt (m.mean) << " |d| " << fmt (m.absMean) << " >10brighter% " << fmt (m.brighter10)
             << " >10darker% " << fmt (m.darker10) << " excess " << fmt (m.excess) << " drawn " << m.drawn
             << "   | FAST r " << fmt (f.r, 3) << " mean " << fmt (f.mean) << " |d| " << fmt (f.absMean) << " brighter% " << fmt (f.brighter10)
             << " darker% " << fmt (f.darker10) << " excess " << fmt (f.excess) << "\n";
    }

    // The swap: the drawing's first object moved a little (as a drag step does), and the fast
    // preview of that, first as its model alone, then as the canvas shows it (the last
    // measurement where the drawing did not change near, FastPreview::makePatch), each against
    // the new drawing's measured preview (which is the real output: see MEASURED above).
    if (! p.getDocument().objects.empty())
    {
        p.editDocument ([] (ArtDocument& d) { d.objects.front().startTime = juce::jlimit (0.0f, 0.9f, d.objects.front().startTime + 0.02f);
                                              d.objects.front().endTime = juce::jlimit (0.1f, 1.0f, d.objects.front().endTime + 0.02f); });
        p.rebuildPlanIfNeeded();
        auto moved = ui::MeasureRequest::forProcessor (p, w, h);
        const auto after = ui::MeasuredPreview::renderNow (moved);

        ui::FastRequest fr;
        fr.effect = sc.effect;
        fr.plan = p.getCurrentPlan();
        fr.model = model;
        fr.width = w;
        fr.height = h;

        if (sc.effect)
        {
            fr.settings.mode = p.getEffectMode();
            fr.settings.depthDb = moved.depthDb;
            fr.settings.view = view;
            fr.settings.sampleRate = moved.sampleRate;
            fr.settings.passSeconds = moved.passSeconds;
            fr.masks = p.getCurrentMasks();
            fr.song = result->analysis;
        }

        ui::FastPreview modelOnly (nullptr), patched (nullptr);
        const auto modelDb = dbOfImage (modelOnly.get (fr, true), view.floorDb, view.ceilingDb);
        fr.measured = result->analysis;
        fr.measuredRequest = request;
        const auto patchedDb = dbOfImage (patched.get (fr, true), view.floorDb, view.ceilingDb);

        if (after != nullptr)
        {
            const auto truth = dbOfAnalysis (*after->analysis, w, h);
            const auto movedDrawing = drawingGrid (p, w, h);
            line << "   after a drag step, against the new measurement:\n";

            for (const auto& band : bands)
            {
                const auto a = compare (modelDb, truth, movedDrawing, scale, w, h, band, view.floorDb, view.ceilingDb);
                const auto b = compare (patchedDb, truth, movedDrawing, scale, w, h, band, view.floorDb, view.ceilingDb);
                line << "   " << juce::String (band.name).paddedRight (' ', 8) << "SWAP model r " << fmt (a.r, 3) << " |d| " << fmt (a.absMean)
                     << " excess " << fmt (a.excess) << "   | shown r " << fmt (b.r, 3) << " |d| " << fmt (b.absMean) << " excess " << fmt (b.excess) << "\n";
            }
        }
    }

    std::cout << line << std::flush;

    // TRUTH_DUMP=1: the pictures as raw floats (dB, row by row, w x h), for analysis elsewhere.
    if (juce::SystemStats::getEnvironmentVariable ("TRUTH_DUMP", {}).isNotEmpty())
    {
        const auto dump = [&] (const std::vector<float>& values, const juce::String& what)
        {
            outDir.getChildFile (sc.name + "-" + what + "-" + juce::String (w) + "x" + juce::String (h) + ".f32")
                .replaceWithData (values.data(), values.size() * sizeof (float));
        };

        dump (measuredDb, "measured");
        dump (fastDb, "fast");
        dump (realDb, "real");
        dump (drawing, "drawing");
    }
    outDir.getChildFile ("scenarios.txt").appendText (line);

    // measured | fast | real, and the measured - real difference under them
    const auto gap = 12;
    juce::Image sheet (juce::Image::RGB, w * 3 + gap * 4, h * 2 + gap * 3 + 20, true);
    juce::Graphics g (sheet);
    g.fillAll (juce::Colour (0xff0b0d12));
    g.drawImageAt (result->pixels.empty() ? juce::Image() : picture (measuredDb, w, h, view.floorDb, view.ceilingDb), gap, gap + 20);
    g.drawImageAt (fast, gap * 2 + w, gap + 20);
    g.drawImageAt (picture (realDb, w, h, view.floorDb, view.ceilingDb), gap * 3 + w * 2, gap + 20);
    g.drawImageAt (diffPicture (measuredDb, realDb, w, h, view.floorDb, view.ceilingDb), gap, gap * 2 + h + 20);
    g.drawImageAt (diffPicture (fastDb, realDb, w, h, view.floorDb, view.ceilingDb), gap * 2 + w, gap * 2 + h + 20);
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawText (sc.name + ": measured canvas | fast canvas | real output (own analyser).  Under: canvas - real (red brighter, blue darker)",
                gap, 2, w * 3, 18, juce::Justification::left);
    juce::PNGImageFormat png;
    auto file = outDir.getChildFile ("scenario-" + sc.name + ".png");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    png.writeImageToStream (sheet, stream);
    return true;
}

// ---- drawings ----------------------------------------------------------------------------------

void textAt (ArtDocument& d, const juce::String& text, float lo, float hi, float t0 = 0.05f, float t1 = 0.95f)
{
    auto o = ArtDocument::makeTextObject (text, lo, hi);
    o.startTime = t0;
    o.endTime = t1;
    d.objects.push_back (o);
}

juce::Image dotsPicture()
{
    juce::Image image (juce::Image::ARGB, 600, 300, true);
    juce::Graphics g (image);
    g.fillAll (juce::Colours::white);
    g.setColour (juce::Colours::black);

    for (int i = 0; i < 8; ++i)
        g.fillEllipse (20.0f + i * 72.0f, 120.0f, 40.0f, 40.0f);

    g.fillRect (0, 280, 600, 6);    // a thin bar along the bottom
    g.fillRect (300, 0, 4, 300);    // a thin vertical stroke
    return image;
}

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> list;
    const auto lowText = [] (ArtFreqProcessor& p)
    {
        ArtDocument d;
        textAt (d, "BASS", 40.0f, 240.0f);
        textAt (d, "MIDS", 400.0f, 1800.0f);
        textAt (d, "HIGH", 4000.0f, 15000.0f);
        p.setDocument (d);
    };
    const auto dots = [] (ArtFreqProcessor& p)
    {
        ArtDocument d;
        auto o = ArtDocument::makeImageObject (prepareImportedImage (dotsPicture()), 30.0f, 400.0f);
        o.startTime = 0.05f;
        o.endTime = 0.95f;
        d.objects.push_back (o);
        auto hi = ArtDocument::makeImageObject (prepareImportedImage (dotsPicture()), 3000.0f, 16000.0f);
        hi.startTime = 0.05f;
        hi.endTime = 0.95f;
        d.objects.push_back (hi);
        p.setDocument (d);
    };

    // The instrument
    list.push_back ({ "i-default-48k", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560, nullptr });
    list.push_back ({ "i-lowtext-44k-b64", false, 44100.0, 64, 120.0, 1, Song::none, 1200, 560, lowText });
    list.push_back ({ "i-lowtext-96k-b1024", false, 96000.0, 1024, 120.0, 1, Song::none, 1200, 560, lowText });
    list.push_back ({ "i-lowtext-stripes", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [lowText] (ArtFreqProcessor& p) { lowText (p); setParameter (p, params::id::lowEnd, 1.0f); } });
    list.push_back ({ "i-dots-d64", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [dots] (ArtFreqProcessor& p) { dots (p); setParameter (p, params::id::density, 64.0f); } });
    list.push_back ({ "i-dots-d1024-fft16k", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [dots] (ArtFreqProcessor& p) { dots (p); setParameter (p, params::id::density, 1024.0f); setParameter (p, params::id::analyserFft, 2.0f); } });
    list.push_back ({ "i-slope6", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [lowText] (ArtFreqProcessor& p) { lowText (p); setParameter (p, params::id::slope, 3.0f); } });
    list.push_back ({ "i-mel-40-8k-range", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [lowText] (ArtFreqProcessor& p)
                      {
                          setParameter (p, params::id::scale, 2.0f);
                          setParameter (p, params::id::rangeLow, 40.0f);
                          setParameter (p, params::id::rangeHigh, 8000.0f);
                          lowText (p);
                          setParameter (p, params::id::analyserFloor, -70.0f);
                          setParameter (p, params::id::analyserCeiling, -20.0f);
                      } });
    list.push_back ({ "i-1beat-180bpm-fft16k", false, 48000.0, 512, 180.0, 1, Song::none, 1200, 560,
                      [lowText] (ArtFreqProcessor& p) { lowText (p); setParameter (p, params::id::length, 0.0f); setParameter (p, params::id::analyserFft, 2.0f); } });
    list.push_back ({ "i-repeat-1bar-fft16k", false, 48000.0, 512, 120.0, 3, Song::none, 1200, 560,
                      [lowText] (ArtFreqProcessor& p) { lowText (p); setParameter (p, params::id::length, 2.0f); setParameter (p, params::id::analyserFft, 2.0f); } });
    list.push_back ({ "i-repeat-fill-1bar-fft16k", false, 48000.0, 512, 120.0, 3, Song::none, 1200, 560,
                      [] (ArtFreqProcessor& p)
                      {
                          ArtDocument d;
                          textAt (d, "LOOPLOOP", 300.0f, 3000.0f, 0.0f, 1.0f);
                          textAt (d, "LOWS", 40.0f, 200.0f, 0.0f, 1.0f);
                          p.setDocument (d);
                          setParameter (p, params::id::length, 2.0f);
                          setParameter (p, params::id::analyserFft, 2.0f);
                      } });
    const auto hello = [] (ArtFreqProcessor& p)
    {
        ArtDocument d;
        textAt (d, "HELLO", 3500.0f, 16000.0f, 0.05f, 0.5f);
        p.setDocument (d);
        setParameter (p, params::id::density, 384.0f);
    };
    list.push_back ({ "i-hello-d384-h462", false, 48000.0, 512, 120.0, 1, Song::none, 764, 462, hello });
    list.push_back ({ "i-hello-d384-h924", false, 48000.0, 512, 120.0, 1, Song::none, 1528, 924, hello });
    list.push_back ({ "i-loud-level", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [lowText] (ArtFreqProcessor& p) { lowText (p); setParameter (p, params::id::level, 0.0f); } });
    list.push_back ({ "i-32bars-60bpm", false, 48000.0, 512, 60.0, 1, Song::none, 1200, 560,
                      [dots] (ArtFreqProcessor& p) { dots (p); setParameter (p, params::id::length, 7.0f); } });
    list.push_back ({ "i-dense", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [] (ArtFreqProcessor& p) { testutil::makeDenseProject (p); setParameter (p, params::id::density, 384.0f); } });
    list.push_back ({ "i-manual-tempo-90", false, 48000.0, 512, 120.0, 1, Song::none, 1200, 560,
                      [lowText] (ArtFreqProcessor& p) { lowText (p); setParameter (p, params::id::tempoSource, 1.0f); setParameter (p, params::id::bpm, 90.0f); } });

    // The effect, on the stand-in itself (the canvas carves the same song)
    const auto fx = [] (int mode, float depth, std::function<void (ArtFreqProcessor&)> draw)
    {
        return [mode, depth, draw] (ArtFreqProcessor& p)
        {
            if (draw) draw (p);
            setParameter (p, params::id::mode, static_cast<float> (mode));
            setParameter (p, params::id::depth, depth);
        };
    };
    list.push_back ({ "fx-carve-standin", true, 48000.0, 512, 120.0, 1, Song::standIn, 1200, 560, fx (0, -80.0f, lowText) });
    list.push_back ({ "fx-carve-standin-44k-b64", true, 44100.0, 64, 120.0, 1, Song::standIn, 1200, 560, fx (0, -80.0f, lowText) });
    list.push_back ({ "fx-carvedraw-standin", true, 48000.0, 512, 120.0, 1, Song::standIn, 1200, 560, fx (1, -80.0f, lowText) });
    list.push_back ({ "fx-stencil-standin", true, 48000.0, 512, 120.0, 1, Song::standIn, 1200, 560, fx (2, -80.0f, lowText) });
    list.push_back ({ "fx-draw-standin", true, 48000.0, 512, 120.0, 1, Song::standIn, 1200, 560, fx (3, -80.0f, lowText) });
    list.push_back ({ "fx-carve-depth20-standin", true, 48000.0, 512, 120.0, 1, Song::standIn, 1200, 560, fx (0, -20.0f, dots) });
    list.push_back ({ "fx-carve-repeat-standin", true, 48000.0, 512, 120.0, 3, Song::standIn, 1200, 560,
                      [fx, lowText] (ArtFreqProcessor& p) { fx (0, -80.0f, lowText) (p); setParameter (p, params::id::length, 2.0f); } });
    list.push_back ({ "fx-carve-repeat-fill", true, 48000.0, 512, 120.0, 3, Song::standIn, 1200, 560,
                      [fx] (ArtFreqProcessor& p)
                      {
                          fx (0, -80.0f, [] (ArtFreqProcessor& q)
                          {
                              ArtDocument d;
                              textAt (d, "LOOPLOOP", 300.0f, 3000.0f, 0.0f, 1.0f);
                              q.setDocument (d);
                          }) (p);
                          setParameter (p, params::id::length, 2.0f);
                          setParameter (p, params::id::analyserFft, 2.0f);
                      } });
    list.push_back ({ "fx-dense-carvedraw", true, 48000.0, 512, 120.0, 1, Song::standIn, 1200, 560,
                      [] (ArtFreqProcessor& p) { testutil::makeDenseProject (p); setParameter (p, params::id::mode, 1.0f); } });
    list.push_back ({ "fx-dense-carve", true, 48000.0, 512, 120.0, 1, Song::standIn, 1200, 560,
                      [] (ArtFreqProcessor& p) { testutil::makeDenseProject (p); setParameter (p, params::id::mode, 0.0f); } });
    list.push_back ({ "fx-carve-basssong", true, 48000.0, 512, 120.0, 1, Song::bassHeavy, 1200, 560, fx (0, -80.0f, lowText) });
    list.push_back ({ "fx-carve-bassonly", true, 48000.0, 512, 120.0, 1, Song::bassOnly, 1200, 560, fx (0, -80.0f, lowText) });
    list.push_back ({ "fx-carve-quietsong", true, 48000.0, 512, 120.0, 1, Song::quiet, 1200, 560, fx (0, -80.0f, lowText) });
    list.push_back ({ "fx-draw-quietsong", true, 48000.0, 512, 120.0, 1, Song::quiet, 1200, 560, fx (3, -80.0f, lowText) });
    return list;
}

} // namespace

namespace artfreq::truthlab
{
/** Runs the scenarios named in `only` (all when empty) into `outDir`: 0 if all ran. */
int runScenarios (const juce::File& outDir, const juce::StringArray& only, bool standIn)
{
    useStandIn = standIn;
    outDir.createDirectory();
    outDir.getChildFile ("scenarios.txt").deleteFile();
    auto ok = true;

    for (const auto& sc : scenarios())
        if (only.isEmpty() || only.contains (sc.name))
            ok = run (sc, outDir) && ok;

    return ok ? 0 : 1;
}
} // namespace artfreq::truthlab
