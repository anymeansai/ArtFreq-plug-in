#pragma once

// ArtFreq FX's fast preview: what an analyser will show of the effect's output in each mode,
// worked out without rendering sound, quickly enough to follow a drag (the measured preview,
// MeasuredPreview.h, renders the real carver and takes over once the drawing stays still).
//
// The effect works on a song the canvas does not have, so a stand-in is drawn under the drawing
// (dsp/StandInSong.h): once the measured preview has analysed it, its real analysis; until then,
// the level the stand-in is made with. It is carved by the same masks the audio thread carves
// with, as deep as Depth, and the carve is blurred as the carver and the analyser blur it (in
// frequency by about a bin of each, so small holes in the lows fill in; in time by about half of
// each one's window, so thin strokes in time come out faint):
//
//  - Carve: the stand-in with the drawing as dark shapes;
//  - Stencil: the drawing filled with the stand-in, the rest dark;
//  - Carve + Draw: the stand-in with a dark hole a little larger than the drawing (the halo
//    masks) and the drawing's sines in it;
//  - Draw: the stand-in with the drawing's sines on top.
//
// The sines are the instrument's fast preview (CanvasPlanImage: dBFS at Level, as the analyser
// shows them), added to the stand-in as powers, as in an analyser's cell. Colours: the analyser's
// range from Settings, as everywhere. Nothing drawn (or everything hidden) carves nothing.

#include "MeasuredPreview.h"
#include "Parameters.h"
#include "core/MaskPlan.h"
#include "dsp/SpectrumAnalysis.h"

#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace artfreq::ui
{

class EffectPreviewImage
{
public:
    /** What the picture depends on besides the drawing. */
    struct Settings
    {
        EffectMode mode = EffectMode::carve;
        float depthDb = params::defaultDepthDb;
        AnalyserView view;
        double sampleRate = 48000.0;
        double passSeconds = 8.0;

        bool operator== (const Settings& other) const noexcept
        {
            return mode == other.mode && juce::exactlyEqual (depthDb, other.depthDb) && view == other.view
                && juce::exactlyEqual (sampleRate, other.sampleRate) && juce::exactlyEqual (passSeconds, other.passSeconds);
        }

        bool operator!= (const Settings& other) const noexcept { return ! operator== (other); }
    };

    /** The picture, `width` x `height` pixels. `sinesDb` is the sines' level per pixel, row by row,
        before Level, and `levelDb` Level (CanvasPlanImage::getPixelDbBeforeLevel and getLevelDb;
        ignored in the modes that do not play them); `sinesKey` changes whenever `sinesDb` does.
        Rebuilt only when something changed; otherwise the cached image is returned. */
    juce::Image get (const std::vector<float>& sinesDb, float levelDb, uint64_t sinesKey, const Settings& settings,
                     const std::shared_ptr<const MaskPlanSet>& masks, const FrequencyScale& scale, int width, int height);

    /** The stand-in as the measured preview analysed it (its `input`), to draw instead of its
        texture; null to go back to the texture. The caller passes only an analysis made with the
        scale, the FFT, the rate and the length of the pictures to come. */
    void setMeasuredSong (std::shared_ptr<const MeasuredSpectrogram> song);
    bool hasMeasuredSong() const noexcept { return measuredSong != nullptr; }

    /** The stand-in song's level at a place on the canvas, in dB of an analyser cell at FFT 4096
        (x 0..1 along the length, y 0..1 from the bottom of the scale to the top). */
    static float songDb (double x, double y) noexcept;

private:
    void makeSong (const Settings& settings, int width, int height);
    void makeCarve (const MaskPlan* mask, const Settings& settings, const FrequencyScale& scale, int width, int height);

    juce::Image image;
    uint64_t imageSinesKey = 0;
    float imageLevelDb = 0.0f;
    Settings imageSettings;
    FrequencyScale imageScale;
    std::shared_ptr<const MaskPlanSet> imageMasks;

    // The stand-in at the picture's size, dB
    std::vector<float> song;
    int songWidth = 0, songHeight = 0, songFft = 0;
    bool songMeasured = false;
    std::shared_ptr<const MeasuredSpectrogram> measuredSong;
    bool songDirty = true;

    // What carving leaves of the song, as dB to add, at the picture's size
    std::vector<float> carve;
    const MaskPlan* carveMask = nullptr;
    std::shared_ptr<const MaskPlanSet> carveMasks;
    Settings carveSettings;
    FrequencyScale carveScale;
    int carveWidth = 0, carveHeight = 0;
    bool carveValid = false;

    std::vector<double> runningSums;
    std::vector<float> carveScratch;
    std::vector<juce::PixelARGB> rowPixels;
};

} // namespace artfreq::ui
