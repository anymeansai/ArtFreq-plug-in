// The lows lab: how ArtFreq's lows and low mids (about 30 Hz - 2 kHz) look in a real
// spectrogram, measured, so that competing ways of drawing them can be compared by numbers.
//
//   ArtFreqLab <output-dir> [--lows stripes|smooth] [--cases sun,text,ramp,photo] [--densities 256,1024]
//              [--ffts 4096,8192,16384,32768] [--contrast 40] [--image <picture>]
//              [--baseline <metrics.json>] [--wav]
//
// ArtFreq keeps its lines at least 25 Hz apart (PlanSettings::minLineSpacingHz), as closer
// lines beat and pulse visibly. On a log scale that leaves only a handful of lines per octave
// below roughly 1.5 kHz, so a picture there shows as separate horizontal stripes with dark gaps.
// This tool is the bench on which ways of doing better are measured. It
//
//   1. draws four test drawings on the canvas (log, 20 Hz - 20 kHz, as in MiniMeters):
//        sun    a filled disc over 60 Hz - 1.2 kHz, softly brighter on its left (raw canvas fill)
//        text   "LOW" in the Low band and "MID" in the Mid band (Document + Compositor)
//        ramp   a block over 30 Hz - 2 kHz, loudest at the bottom and fading upwards (raw fill)
//        photo  a portrait over 40 Hz - 2 kHz (Document + Compositor, Auto mode): the picture
//               given with --image, else one synthesised here (the repository holds no photo)
//   2. builds each drawing's plan (buildPlan, with PlanSettings::lowEnd = the Lows under test) at
//      each density and plays it with the real ArtEngine at 48 kHz: one 8 s pass (the
//      default 4 bars at 120 BPM), looped, with half a second either side so that every
//      analysis window is full of sound, as in a DAW playing the loop;
//   3. analyses the sound like an analyser does: an STFT for each FFT size, with Hann and
//      4-term Blackman-Harris windows, hop = FFT size / 8, dB of the magnitude (a sine of
//      amplitude A reads 20 log10 A), mapped onto a 600-row display, log 20 Hz - 20 kHz. Where
//      display rows are finer than the bins (the lows), a row shows the dB interpolated linearly
//      between the two bin centres around it (as MiniMeters and SPAN draw) or, for comparison,
//      the nearest bin; where a row spans several bins it shows the loudest of them;
//   4. measures each display over its rows from 30 Hz to 2 kHz, and separately over 30 - 250 Hz
//      and 250 Hz - 2 kHz: fidelity, fill, gap depth, flicker and its modulation peak, leakage
//      (see BandMetrics), and the cost of each render (see Cost);
//   5. writes into <output-dir>:
//        metrics.json                          every number
//        metrics.txt                           the summary tables (also printed)
//        <case>-canvas.png                     the drawing as the display would ideally show it
//        <case>-d<density>-fft<N>-<window>.png the display (linear interpolation), full range
//        <case>-d<density>-sheet.png           30 Hz - 2 kHz side by side: the canvas, the plan's
//                                              lines, then each FFT size with Hann, with
//                                              Blackman-Harris, and with Hann mapped to the
//                                              nearest bin, each labelled with fill and fidelity
//        <case>-d<density>.wav                 with --wav: the bounce, as Export writes it
//
// The Lows under test come from --lows stripes|smooth, else from the ARTFREQ_LOWS environment
// variable, else Stripes (the lines ArtFreq 0.1 played, the baseline the others are measured
// against). --method 0 and --method 3, as the prototypes were numbered, mean Stripes and Smooth.
// The lab passes it into PlanSettings::lowEnd and changes nothing else. --baseline adds the
// change against an earlier run's metrics.json to metrics.txt. Everything but the timings is
// deterministic.

#include "core/ArtPlan.h"
#include "core/Compositor.h"
#include "core/Document.h"
#include "core/Fonts.h"
#include "core/FrequencyScale.h"
#include "core/ImageProcessing.h"
#include "dsp/ArtEngine.h"
#include "dsp/WavExporter.h"
#include "ui/Theme.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

using namespace artfreq;

constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr double notMeasured = std::numeric_limits<double>::quiet_NaN();

// ---- The sound ---------------------------------------------------------------------------------
constexpr double sampleRate = 48000.0;
constexpr double passSeconds = 8.0;   // the default length, 4 bars at 120 BPM
constexpr double padSeconds = 0.5;    // looped sound either side of the pass: more than half the
                                      // longest window (32768 samples), so no window sees silence
constexpr int blockSize = 512;
constexpr int largestFft = 32768;
constexpr float defaultLevel = 0.5f;  // the Level parameter's default, -6 dB
constexpr float clipperThreshold = 0.89f;  // where softClip starts to bend

// ---- The display -------------------------------------------------------------------------------
constexpr int displayRows = 600;
constexpr double displayLowHz = 20.0;
constexpr double displayHighHz = 20000.0;
constexpr int pngWidth = 800;
constexpr int sheetPanelWidth = 400;
constexpr float visibleRangeDb = 60.0f;  // as ArtFreq's canvas and live view: the loudest is white,
                                         // 60 dB below it the colour of silence
constexpr float floorDb = -160.0f;

// ---- The measurements --------------------------------------------------------------------------
constexpr double labLowHz = 30.0;
constexpr double labSplitHz = 250.0;
constexpr double labHighHz = 2000.0;
constexpr float drawnValue = 0.5f;        // canvas values above this are "drawn"
constexpr float silentValue = 0.01f;      // ... and below this "not drawn"
constexpr double minUndrawnShare = 0.01;  // leakage needs this share of the band's cells undrawn: a
                                          // few dark pixels inside a picture (pupils) say nothing
constexpr float steadyTolerance = 0.1f;   // a steady cell's canvas moves less than this under its window
constexpr double fillToleranceDb = 6.0;
constexpr double minSteadySeconds = 0.5;  // shorter steady stretches are not used for flicker
constexpr double modulationLowHz = 2.0;   // the pulsing range: slower is a gradient, faster a texture
constexpr double modulationHighHz = 20.0;
constexpr double modulationStepHz = 0.25;

template <typename Int>
constexpr size_t idx (Int i) noexcept
{
    return static_cast<size_t> (i);
}

// ================================================================================================
// The display's geometry. The canvas is drawn on the same scale (ArtFreq's default), so a display
// row and a band of canvas rows cover the same frequencies.
// ================================================================================================

double hzAtNorm (double norm)
{
    return displayLowHz * std::pow (displayHighHz / displayLowHz, norm);
}

double normAtHz (double hz)
{
    return std::log (hz / displayLowHz) / std::log (displayHighHz / displayLowHz);
}

/** Frequency at the centre of a display row (row 0 = top). */
double rowCentreHz (int row)
{
    return hzAtNorm (1.0 - (row + 0.5) / displayRows);
}

/** Height of a frequency on the display, in rows from the top (fractional: 0 = top edge). */
double rowPosition (double hz)
{
    return (1.0 - normAtHz (hz)) * displayRows;
}

/** Height of a frequency on the canvas, in canvas rows from the top (fractional). */
double canvasYForHz (double hz)
{
    return (1.0 - normAtHz (hz)) * Canvas::height;
}

/** The rows a lab band covers: those whose centre lies in [lowHz, highHz). */
struct LabBand
{
    const char* name;
    double lowHz;
    double highHz;
};

constexpr std::array<LabBand, 3> labBands { { { "all", labLowHz, labHighHz },
                                              { "low", labLowHz, labSplitHz },
                                              { "mid", labSplitHz, labHighHz } } };

bool rowInBand (int row, const LabBand& band)
{
    const auto hz = rowCentreHz (row);
    return hz >= band.lowHz && hz < band.highHz;
}

/** The rows from 30 Hz (bottom) to 2 kHz (top), for the sheets. */
int sheetFirstRow()  { return static_cast<int> (std::floor (rowPosition (labHighHz))); }
int sheetLastRow()   { return static_cast<int> (std::ceil (rowPosition (labLowHz))) - 1; }

// ================================================================================================
// Options
// ================================================================================================

juce::String lowsName (LowEnd lows)
{
    return lows == LowEnd::smooth ? "smooth" : "stripes";
}

struct Options
{
    juce::File outputDir;
    LowEnd lows = LowEnd::stripes;
    juce::StringArray cases { "sun", "text", "ramp", "photo" };
    std::vector<int> densities { 256, 1024 };
    std::vector<int> fftSizes { 4096, 8192, 16384, 32768 };
    float contrastDb = 40.0f;
    juce::File image;
    juce::File baseline;
    bool writeWav = false;
    bool showHelp = false;
};

void printUsage()
{
    std::cout << "ArtFreqLab <output-dir> [--lows stripes|smooth] [--cases sun,text,ramp,photo] [--densities 256,1024]\n"
                 "           [--ffts 4096,8192,16384,32768] [--contrast 40] [--image <picture>]\n"
                 "           [--baseline <metrics.json>] [--wav]\n\n"
                 "  --lows       the Lows under test (PlanSettings::lowEnd); default: the ARTFREQ_LOWS\n"
                 "               environment variable, else stripes (--method 0 / 3 also work)\n"
                 "  --baseline   an earlier run's metrics.json: metrics.txt shows the change\n"
                 "  --wav        also write each drawing's bounce\n";
}

std::vector<int> parseIntList (const juce::String& text)
{
    std::vector<int> values;

    for (const auto& item : juce::StringArray::fromTokens (text, ",", ""))
        if (item.trim().containsOnly ("0123456789"))
            values.push_back (item.trim().getIntValue());
        else
            return {};

    return values;
}

/** "smooth" or "stripes" (or the prototypes' numbers, 3 and 0). */
bool parseLows (const juce::String& text, LowEnd& lows)
{
    const auto t = text.trim().toLowerCase();

    if (t == "smooth" || t == "3")
        lows = LowEnd::smooth;
    else if (t == "stripes" || t == "0")
        lows = LowEnd::stripes;
    else
        return false;

    return true;
}

bool parseOptions (int argc, char* argv[], Options& options, juce::String& error)
{
    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable ("ARTFREQ_LOWS", {}).trim();

    if (fromEnvironment.isNotEmpty() && ! parseLows (fromEnvironment, options.lows))
    {
        error = "ARTFREQ_LOWS must be smooth or stripes";
        return false;
    }

    juce::StringArray arguments;
    for (int i = 1; i < argc; ++i)
        arguments.add (juce::String (argv[i]));

    for (int i = 0; i < arguments.size(); ++i)
    {
        const auto& argument = arguments[i];
        const auto takesValue = argument.startsWith ("--") && argument != "--wav" && argument != "--help";
        const auto value = takesValue && i + 1 < arguments.size() ? arguments[i + 1] : juce::String();

        if (takesValue && value.isEmpty())
        {
            error = argument + " needs a value";
            return false;
        }

        if (argument == "--help" || argument == "-h")
        {
            options.showHelp = true;
            return false;
        }
        else if (argument == "--wav")
        {
            options.writeWav = true;
        }
        else if (argument == "--lows" || argument == "--method" || argument == "--mode")
        {
            if (! parseLows (value, options.lows))
            {
                error = argument + " must be smooth or stripes (3 or 0)";
                return false;
            }
        }
        else if (argument == "--cases")
        {
            options.cases = juce::StringArray::fromTokens (value, ",", "");
            options.cases.trim();
            options.cases.removeEmptyStrings();
        }
        else if (argument == "--densities")
        {
            options.densities = parseIntList (value);
        }
        else if (argument == "--ffts")
        {
            options.fftSizes = parseIntList (value);
        }
        else if (argument == "--contrast")
        {
            options.contrastDb = value.getFloatValue();
        }
        else if (argument == "--image")
        {
            options.image = juce::File::getCurrentWorkingDirectory().getChildFile (value);
        }
        else if (argument == "--baseline")
        {
            options.baseline = juce::File::getCurrentWorkingDirectory().getChildFile (value);
        }
        else if (! argument.startsWith ("-") && options.outputDir == juce::File())
        {
            options.outputDir = juce::File::getCurrentWorkingDirectory().getChildFile (argument);
            continue;
        }
        else
        {
            error = "Unknown argument: " + argument;
            return false;
        }

        if (takesValue)
            ++i;
    }

    if (options.outputDir == juce::File())
        error = "No output folder given";
    else if (options.densities.empty() || std::any_of (options.densities.begin(), options.densities.end(),
                                                       [] (int d) { return d < 16 || d > 1024; }))
        error = "Densities must be 16 .. 1024";
    else if (options.fftSizes.empty() || std::any_of (options.fftSizes.begin(), options.fftSizes.end(),
                                                      [] (int n) { return n < 256 || n > largestFft || ! juce::isPowerOfTwo (n); }))
        error = "FFT sizes must be powers of two, 256 .. 32768";
    else if (! (options.contrastDb >= 12.0f && options.contrastDb <= 72.0f))
        error = "Contrast must be 12 .. 72 dB";
    else if (options.image != juce::File() && ! options.image.existsAsFile())
        error = "No such picture: " + options.image.getFullPathName();
    else if (options.baseline != juce::File() && ! options.baseline.existsAsFile())
        error = "No such baseline: " + options.baseline.getFullPathName();

    for (const auto& name : options.cases)
        if (! juce::StringArray { "sun", "text", "ramp", "photo" }.contains (name))
            error = "Unknown case: " + name;

    return error.isEmpty();
}

// ================================================================================================
// The test drawings
// ================================================================================================

struct TestDrawing
{
    juce::String name;
    juce::String description;
    Canvas canvas;
};

/** A filled disc over 60 Hz - 1.2 kHz, round on the canvas, centred in time, with a 1 px soft rim
    and a soft gradient from left (0.95) to right (0.65): a sun. */
void fillSun (Canvas& canvas)
{
    const auto top = canvasYForHz (1200.0);
    const auto bottom = canvasYForHz (60.0);
    const auto radius = 0.5 * (bottom - top);
    const auto centreY = 0.5 * (top + bottom);
    const auto centreX = 0.5 * Canvas::width;

    for (int y = 0; y < Canvas::height; ++y)
    {
        for (int x = 0; x < Canvas::width; ++x)
        {
            const auto dx = x + 0.5 - centreX;
            const auto dy = y + 0.5 - centreY;
            const auto rim = juce::jlimit (0.0, 1.0, radius - std::sqrt (dx * dx + dy * dy) + 0.5);

            if (rim <= 0.0)
                continue;

            const auto across = juce::jlimit (0.0, 1.0, (x + 0.5 - (centreX - radius)) / (2.0 * radius));
            canvas.at (x, y) = static_cast<float> ((0.95 - 0.3 * across) * rim);
        }
    }
}

/** "LOW" in the Low band and "MID" in the Mid band, through the real document and compositor. */
void drawText (Canvas& canvas)
{
    const FrequencyScale scale;
    ArtDocument document;

    for (const auto& [text, band] : { std::pair<const char*, Band> { "LOW", Band::low },
                                      std::pair<const char*, Band> { "MID", Band::mid } })
    {
        const auto range = bandRange (band, scale);
        auto object = ArtDocument::makeTextObject (text, static_cast<float> (range.low), static_cast<float> (range.high));
        object.startTime = 0.1f;
        object.endTime = 0.9f;
        document.objects.push_back (object);
    }

    Compositor compositor;
    compositor.render (document, scale, canvas);
}

/** A block over 30 Hz - 2 kHz from 10 % to 90 % of the time, 1.0 at the bottom fading evenly
    (in height) to 0.55 at the top: all of it drawn, every row steady. */
void fillRamp (Canvas& canvas)
{
    const auto top = canvasYForHz (labHighHz);
    const auto bottom = canvasYForHz (labLowHz);

    for (int y = 0; y < Canvas::height; ++y)
    {
        const auto centre = y + 0.5;

        if (centre < top || centre > bottom)
            continue;

        const auto up = (bottom - centre) / (bottom - top);  // 0 at 30 Hz, 1 at 2 kHz
        const auto value = static_cast<float> (1.0 - 0.45 * up);

        for (int x = Canvas::width / 10; x < Canvas::width * 9 / 10; ++x)
            canvas.at (x, y) = value;
    }
}

/** A stand-in for a photo of a face (the repository holds no photo): head and shoulders lit from
    the upper left against a dark backdrop, smooth shading, small features and film grain. That is
    what photos have and logos lack: soft gradients everywhere, fine detail, noise. */
juce::Image makePortrait()
{
    constexpr int width = 384, height = 512;
    juce::Image image (juce::Image::RGB, width, height, true);

    {
        juce::Graphics g (image);
        const auto grey = [] (float level) { return juce::Colour::greyLevel (level); };
        const juce::Colour skinLight (0xfff0d2bc), skinDark (0xff6e5040), skinShadow (0xff4a3428);

        // Backdrop: dark, a little lighter behind the head, as a studio light on the wall.
        g.setGradientFill (juce::ColourGradient (grey (0.30f), 150.0f, 150.0f, grey (0.04f), 10.0f, 520.0f, true));
        g.fillAll();

        // Shoulders in a dark jacket, lit from the left, and a light shirt collar.
        g.setGradientFill (juce::ColourGradient (grey (0.45f), 60.0f, 420.0f, grey (0.10f), 360.0f, 512.0f, false));
        g.fillEllipse (14.0f, 392.0f, 356.0f, 260.0f);

        juce::Path collar;
        collar.startNewSubPath (150.0f, 396.0f);
        collar.lineTo (192.0f, 476.0f);
        collar.lineTo (234.0f, 396.0f);
        collar.closeSubPath();
        g.setGradientFill (juce::ColourGradient (grey (0.92f), 150.0f, 400.0f, grey (0.55f), 234.0f, 470.0f, false));
        g.fillPath (collar);

        // Neck, in the shadow of the chin.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb08a74), 150.0f, 330.0f, skinShadow, 236.0f, 400.0f, false));
        g.fillRoundedRectangle (152.0f, 290.0f, 80.0f, 116.0f, 20.0f);

        // Hair behind the head, then the ears.
        g.setColour (grey (0.15f));
        g.fillEllipse (94.0f, 56.0f, 196.0f, 256.0f);
        g.setColour (skinDark.brighter (0.4f));
        g.fillEllipse (102.0f, 186.0f, 24.0f, 52.0f);
        g.setColour (skinDark);
        g.fillEllipse (258.0f, 186.0f, 24.0f, 52.0f);

        // The face, lit from the upper left, with a soft shadow down its right cheek.
        g.setGradientFill (juce::ColourGradient (skinLight, 160.0f, 170.0f, skinDark, 300.0f, 330.0f, true));
        g.fillEllipse (114.0f, 92.0f, 156.0f, 216.0f);
        g.setColour (skinShadow.withAlpha (0.35f));
        g.fillEllipse (228.0f, 200.0f, 40.0f, 90.0f);

        // Hair over the forehead, with strands of different lightness.
        juce::Path fringe;
        fringe.startNewSubPath (108.0f, 172.0f);
        fringe.cubicTo (96.0f, 56.0f, 292.0f, 46.0f, 278.0f, 172.0f);
        fringe.cubicTo (252.0f, 118.0f, 204.0f, 108.0f, 162.0f, 126.0f);
        fringe.cubicTo (136.0f, 138.0f, 118.0f, 152.0f, 108.0f, 172.0f);
        fringe.closeSubPath();
        g.setColour (grey (0.17f));
        g.fillPath (fringe);

        juce::Random strands (7);

        for (int i = 0; i < 90; ++i)
        {
            const auto x = 110.0f + 166.0f * static_cast<float> (i) / 90.0f;
            juce::Path strand;
            strand.startNewSubPath (x, 70.0f + 30.0f * std::abs (x - 192.0f) / 90.0f);
            strand.quadraticTo (x + 10.0f, 110.0f, x + (x < 192.0f ? -6.0f : 6.0f), 150.0f + strands.nextFloat() * 20.0f);
            g.setColour (grey (0.08f + 0.35f * strands.nextFloat()));
            g.strokePath (strand, juce::PathStrokeType (1.2f));
        }

        // Eyebrows, eyes (white, iris, pupil, a highlight), nose, mouth.
        g.setColour (grey (0.12f));
        juce::Path brows;
        brows.startNewSubPath (138.0f, 170.0f);
        brows.quadraticTo (158.0f, 158.0f, 180.0f, 164.0f);
        brows.startNewSubPath (204.0f, 164.0f);
        brows.quadraticTo (226.0f, 158.0f, 246.0f, 170.0f);
        g.strokePath (brows, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (const auto centre : { 160.0f, 224.0f })
        {
            g.setColour (grey (0.88f));
            g.fillEllipse (centre - 17.0f, 182.0f, 34.0f, 14.0f);
            g.setColour (grey (0.33f));
            g.fillEllipse (centre - 7.0f, 182.0f, 14.0f, 14.0f);
            g.setColour (grey (0.03f));
            g.fillEllipse (centre - 3.5f, 185.5f, 7.0f, 7.0f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (centre - 3.0f, 184.0f, 3.0f, 3.0f);
            g.setColour (skinShadow);
            g.drawLine (centre - 18.0f, 189.0f, centre + 18.0f, 186.0f, 2.0f);
        }

        g.setColour (skinLight.brighter (0.2f).withAlpha (0.6f));
        g.fillEllipse (186.0f, 198.0f, 7.0f, 34.0f);
        g.setColour (skinShadow.withAlpha (0.7f));
        juce::Path noseShadow;
        noseShadow.startNewSubPath (200.0f, 192.0f);
        noseShadow.quadraticTo (206.0f, 226.0f, 204.0f, 240.0f);
        g.strokePath (noseShadow, juce::PathStrokeType (4.0f));
        g.setColour (skinShadow.darker (0.5f));
        g.fillEllipse (178.0f, 243.0f, 10.0f, 6.0f);
        g.fillEllipse (198.0f, 243.0f, 10.0f, 6.0f);

        g.setColour (juce::Colour (0xff9a5050));
        g.fillEllipse (164.0f, 262.0f, 58.0f, 20.0f);
        g.setColour (juce::Colour (0xff3a1c1c));
        g.drawLine (166.0f, 272.0f, 220.0f, 271.0f, 2.0f);
    }

    // Film grain, the same every run.
    juce::Random grain (20261113);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto colour = data.getPixelColour (x, y);
            const auto noise = (grain.nextFloat() - 0.5f) * 0.08f;
            const auto shift = [noise] (float channel) { return juce::jlimit (0.0f, 1.0f, channel + noise); };
            data.setPixelColour (x, y, juce::Colour::fromFloatRGBA (shift (colour.getFloatRed()), shift (colour.getFloatGreen()),
                                                                    shift (colour.getFloatBlue()), 1.0f));
        }
    }

    return image;
}

/** The portrait (or the given picture) over 40 Hz - 2 kHz from 30 % to 70 % of the time, through
    the real document and compositor with the defaults a dropped picture gets (Auto, clean-up). */
bool drawPhoto (Canvas& canvas, const juce::File& file, juce::String& description)
{
    const auto raw = file != juce::File() ? loadImageFile (file) : makePortrait();

    if (! raw.isValid())
        return false;

    ArtDocument document;
    auto object = ArtDocument::makeImageObject (prepareImportedImage (raw), 40.0f, 2000.0f);
    object.startTime = 0.3f;
    object.endTime = 0.7f;
    document.objects.push_back (object);

    Compositor compositor;
    compositor.render (document, FrequencyScale {}, canvas);

    description = (file != juce::File() ? file.getFileName() : juce::String ("synthesised portrait"))
                + " over 40 Hz - 2 kHz, 30 - 70 % of the time, Auto mode";
    return true;
}

bool makeDrawing (const juce::String& name, const Options& options, TestDrawing& drawing)
{
    drawing.name = name;
    drawing.canvas.clear();

    if (name == "sun")
    {
        fillSun (drawing.canvas);
        drawing.description = "disc over 60 Hz - 1.2 kHz, gradient 0.95 to 0.65 left to right";
    }
    else if (name == "text")
    {
        drawText (drawing.canvas);
        drawing.description = "\"LOW\" in the Low band (40 - 250 Hz), \"MID\" in the Mid band (300 Hz - 3 kHz)";
    }
    else if (name == "ramp")
    {
        fillRamp (drawing.canvas);
        drawing.description = "block over 30 Hz - 2 kHz, 1.0 at the bottom to 0.55 at the top";
    }
    else
    {
        return drawPhoto (drawing.canvas, options.image, drawing.description);
    }

    return true;
}

// ================================================================================================
// The sound
// ================================================================================================

int padSamples()  { return static_cast<int> (std::lround (padSeconds * sampleRate)); }
int passSamples() { return static_cast<int> (std::lround (passSeconds * sampleRate)); }

/** The settings ArtFreqProcessor::getPlanSettings makes at the default analyser settings, with
    the density, contrast and Lows under test. */
PlanSettings planSettingsFor (int density, float contrastDb, LowEnd lows)
{
    PlanSettings settings;
    settings.density = density;
    settings.contrastDb = contrastDb;
    settings.maxFrequencyHz = 22000.0f;
    settings.lowEnd = lows;
    return settings;
}

/** What it costs to make the sound, and how hot it is. */
struct Cost
{
    int lines = 0, linesLow = 0, linesMid = 0;  // sines played; in 30 - 250 Hz; in 250 Hz - 2 kHz
    double planMs = 0.0;                        // buildPlan
    double renderMsPerSecond = 0.0;             // ArtEngine::render, per second of sound
    double peakDb = 0.0, rmsDb = 0.0;           // over the pass, at the plan's level (before Level)
    double crestDb = 0.0;                       // peak over RMS
    bool clipsAtDefaultLevel = false;           // the peak at Level -6 dB reaches the clipper
};

struct Render
{
    std::unique_ptr<ArtPlan> plan;
    std::vector<float> audio;  // pad + pass + pad, mono, at the plan's level
    Cost cost;
};

Render renderDrawing (const Canvas& canvas, const PlanSettings& settings)
{
    Render result;
    auto& cost = result.cost;

    const auto planStart = juce::Time::getMillisecondCounterHiRes();
    result.plan = buildPlan (canvas, settings);
    cost.planMs = juce::Time::getMillisecondCounterHiRes() - planStart;

    const auto& plan = *result.plan;

    // The sines that play: the lines, and the comb's gliding sines rather than its loudness grid
    // (its sines are spread evenly over the glide, so a range holds its share of them). The lows
    // the comb replaced count only if they play a thin stroke somewhere.
    const auto sounds = [&plan] (int line)
    {
        for (int c = 0; c < plan.numColumns; ++c)
            if (plan.amplitude (c, line) > 0.0f)
                return true;

        return false;
    };

    cost.lines = plan.combSines;

    for (int l = plan.combLines; l < plan.numLines; ++l)
    {
        if (l < plan.combLines + plan.combThinLines && ! sounds (l))
            continue;

        const auto hz = plan.frequencies[idx (l)];
        ++cost.lines;
        cost.linesLow += hz >= labLowHz && hz < labSplitHz ? 1 : 0;
        cost.linesMid += hz >= labSplitHz && hz < labHighHz ? 1 : 0;
    }

    if (plan.combLines > 0)
    {
        const auto bottom = static_cast<double> (plan.combGridLowHz - plan.combEdgeHz);
        const auto top = static_cast<double> (plan.combGridLowHz) + (plan.combLines - 1) * static_cast<double> (plan.combGridHz)
                       + static_cast<double> (plan.combEdgeHz);
        const auto share = [&] (double low, double high)
        {
            const auto overlap = std::max (0.0, std::min (high, top) - std::max (low, bottom));
            return static_cast<int> (std::lround (plan.combSines * overlap / (top - bottom)));
        };

        cost.linesLow += share (labLowHz, labSplitHz);
        cost.linesMid += share (labSplitHz, labHighHz);
    }

    const auto pad = padSamples();
    const auto pass = passSamples();
    const auto total = pass + 2 * pad;
    result.audio.assign (idx (total), 0.0f);

    ArtEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setPlan (&plan);

    // Looping, as a DAW plays it: the pass is preceded by its own end and followed by its start.
    // Each block's position is computed from the sample count, so no rounding builds up.
    const auto perSample = 1.0 / pass;
    const auto renderStart = juce::Time::getMillisecondCounterHiRes();

    for (int done = 0; done < total; done += blockSize)
    {
        PlaySegment segment;
        segment.length = std::min (blockSize, total - done);
        segment.playing = true;
        const auto position = (done - pad) * perSample;
        segment.fraction = position - std::floor (position);
        segment.fractionPerSample = perSample;
        segment.jump = done == 0;
        engine.render (result.audio.data() + done, segment.length, &segment, 1);
    }

    cost.renderMsPerSecond = (juce::Time::getMillisecondCounterHiRes() - renderStart) / (total / sampleRate);

    double peak = 0.0, power = 0.0;

    for (auto i = idx (pad); i < idx (pad + pass); ++i)
    {
        const auto sample = static_cast<double> (result.audio[i]);
        peak = std::max (peak, std::abs (sample));
        power += sample * sample;
    }

    const auto rms = std::sqrt (power / pass);
    cost.peakDb = juce::Decibels::gainToDecibels (peak, -200.0);
    cost.rmsDb = juce::Decibels::gainToDecibels (rms, -200.0);
    cost.crestDb = cost.peakDb - cost.rmsDb;
    cost.clipsAtDefaultLevel = peak * defaultLevel > clipperThreshold;
    return result;
}

// ================================================================================================
// The analyser
// ================================================================================================

enum class WindowShape
{
    hann,
    blackmanHarris  // 4-term, -92 dB side lobes, a main lobe twice as wide as Hann's
};

const char* windowName (WindowShape shape)
{
    return shape == WindowShape::hann ? "hann" : "bh4";
}

enum class Mapping
{
    interpolate,  // dB interpolated linearly between the bin centres around the row (MiniMeters, SPAN)
    nearest       // the nearest bin
};

const char* mappingName (Mapping mapping)
{
    return mapping == Mapping::interpolate ? "interp" : "nearest";
}

struct Spectrogram
{
    int fftSize = 0;
    int hop = 0;
    int numFrames = 0;
    int numBins = 0;
    std::vector<float> db;  // [frame * numBins + bin]; frame f is the moment (f + 0.5) * hop into the pass

    double frameRate() const { return sampleRate / hop; }
    const float* frame (int f) const { return db.data() + idx (f) * idx (numBins); }
};

Spectrogram analyse (const std::vector<float>& audio, int fftSize, WindowShape shape)
{
    std::vector<float> window (idx (fftSize));
    double windowSum = 0.0;

    for (int n = 0; n < fftSize; ++n)
    {
        const auto phase = twoPi * n / fftSize;
        const auto w = shape == WindowShape::hann
                         ? 0.5 - 0.5 * std::cos (phase)
                         : 0.35875 - 0.48829 * std::cos (phase) + 0.14128 * std::cos (2.0 * phase) - 0.01168 * std::cos (3.0 * phase);
        window[idx (n)] = static_cast<float> (w);
        windowSum += w;
    }

    // A sine of amplitude A peaks at A * sum (w) / 2 in its bin: read it as 20 log10 A.
    const auto toAmplitude = static_cast<float> (2.0 / windowSum);

    Spectrogram result;
    result.fftSize = fftSize;
    result.hop = fftSize / 8;
    result.numFrames = passSamples() / result.hop;
    result.numBins = fftSize / 2 + 1;
    result.db.resize (idx (result.numFrames) * idx (result.numBins));

    juce::dsp::FFT fft (juce::roundToInt (std::log2 (fftSize)));
    std::vector<float> work (idx (fftSize) * 2);

    for (int f = 0; f < result.numFrames; ++f)
    {
        const auto centre = padSamples() + f * result.hop + result.hop / 2;
        const auto* source = audio.data() + (centre - fftSize / 2);

        for (size_t n = 0; n < idx (fftSize); ++n)
            work[n] = source[n] * window[n];

        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        auto* out = result.db.data() + idx (f) * idx (result.numBins);

        for (size_t bin = 0; bin < idx (result.numBins); ++bin)
            out[bin] = std::max (floorDb, 20.0f * std::log10 (std::max (work[bin] * toAmplitude, 1.0e-12f)));
    }

    return result;
}

/** A spectrogram as an analyser draws it: displayRows rows (log, 20 Hz - 20 kHz), one column per
    STFT frame. */
struct Display
{
    int numFrames = 0;
    std::vector<float> db;  // [row * numFrames + frame]
    float topDb = floorDb;  // the loudest cell: the analyser's view is read relative to it

    float at (int row, int frame) const { return db[idx (row) * idx (numFrames) + idx (frame)]; }
};

Display toDisplay (const Spectrogram& spectrogram, Mapping mapping)
{
    Display display;
    display.numFrames = spectrogram.numFrames;
    display.db.resize (idx (displayRows) * idx (spectrogram.numFrames));

    const auto binsPerHz = spectrogram.fftSize / sampleRate;
    const auto lastBin = spectrogram.numBins - 1;

    for (int row = 0; row < displayRows; ++row)
    {
        const auto centre = rowCentreHz (row) * binsPerHz;
        const auto lowEdge = hzAtNorm (1.0 - (row + 1.0) / displayRows) * binsPerHz;
        const auto highEdge = hzAtNorm (1.0 - static_cast<double> (row) / displayRows) * binsPerHz;

        const auto below = juce::jlimit (0, lastBin - 1, static_cast<int> (std::floor (centre)));
        const auto fraction = static_cast<float> (juce::jlimit (0.0, 1.0, centre - below));
        const auto nearest = juce::jlimit (0, lastBin, static_cast<int> (std::lround (centre)));

        // Bins whose centres lie inside the row: a row covering several shows the loudest.
        const auto firstInside = juce::jlimit (0, lastBin + 1, static_cast<int> (std::ceil (lowEdge)));
        const auto lastInside = juce::jlimit (-1, lastBin, static_cast<int> (std::ceil (highEdge)) - 1);

        for (int f = 0; f < spectrogram.numFrames; ++f)
        {
            const auto* bins = spectrogram.frame (f);
            auto value = mapping == Mapping::interpolate
                           ? bins[below] + (bins[below + 1] - bins[below]) * fraction
                           : bins[nearest];

            for (int bin = firstInside; bin <= lastInside; ++bin)
                value = std::max (value, bins[bin]);

            display.db[idx (row) * idx (display.numFrames) + idx (f)] = value;
            display.topDb = std::max (display.topDb, value);
        }
    }

    return display;
}

// ================================================================================================
// The drawing on the display's grid
// ================================================================================================

/** The mean of the canvas over [x0, x1) x [y0, y1) in canvas pixels; pixels cut by an edge count
    in part. */
float canvasMean (const Canvas& canvas, double x0, double x1, double y0, double y1)
{
    double sum = 0.0, weight = 0.0;

    for (auto y = std::max (0, static_cast<int> (std::floor (y0))); y < std::min (Canvas::height, static_cast<int> (std::ceil (y1))); ++y)
    {
        const auto wy = std::min (y1, y + 1.0) - std::max (y0, static_cast<double> (y));

        for (auto x = std::max (0, static_cast<int> (std::floor (x0))); x < std::min (Canvas::width, static_cast<int> (std::ceil (x1))); ++x)
        {
            const auto wx = std::min (x1, x + 1.0) - std::max (x0, static_cast<double> (x));
            sum += wx * wy * canvas.get (x, y);
            weight += wx * wy;
        }
    }

    return weight > 0.0 ? static_cast<float> (sum / weight) : 0.0f;
}

double canvasTopOfRow (int row)    { return static_cast<double> (row) * Canvas::height / displayRows; }
double canvasBottomOfRow (int row) { return (row + 1.0) * Canvas::height / displayRows; }

/** The canvas on the grid of a display: what each cell should show. */
struct Reference
{
    std::vector<float> value;     // [row * numFrames + frame]: the canvas over the cell
    std::vector<uint8_t> steady;  // the canvas stays drawn and level under the frame's whole
                                  // window: any change in that cell over time is the sound's
};

Reference makeReference (const Canvas& canvas, const Spectrogram& spectrogram)
{
    const auto frames = spectrogram.numFrames;
    const auto cells = idx (displayRows) * idx (frames);
    const auto columnsPerSample = static_cast<double> (Canvas::width) / passSamples();

    Reference reference;
    reference.value.resize (cells);
    reference.steady.assign (cells, 0);

    std::vector<float> lowest (idx (Canvas::width)), highest (idx (Canvas::width));

    for (int row = 0; row < displayRows; ++row)
    {
        const auto top = canvasTopOfRow (row), bottom = canvasBottomOfRow (row);

        for (int f = 0; f < frames; ++f)
        {
            const auto x0 = f * spectrogram.hop * columnsPerSample;
            const auto x1 = (f + 1) * spectrogram.hop * columnsPerSample;
            reference.value[idx (row) * idx (frames) + idx (f)] = canvasMean (canvas, x0, x1, top, bottom);
        }

        // The lowest and highest value in each canvas column over the row's canvas rows ...
        const auto firstY = static_cast<int> (std::floor (top));
        const auto lastY = std::min (Canvas::height, static_cast<int> (std::ceil (bottom))) - 1;

        for (int x = 0; x < Canvas::width; ++x)
        {
            auto low = 1.0f, high = 0.0f;

            for (int y = firstY; y <= lastY; ++y)
            {
                low = std::min (low, canvas.get (x, y));
                high = std::max (high, canvas.get (x, y));
            }

            lowest[idx (x)] = low;
            highest[idx (x)] = high;
        }

        // ... and so over each frame's window (which wraps round the loop).
        for (int f = 0; f < frames; ++f)
        {
            const auto centre = f * spectrogram.hop + spectrogram.hop / 2;
            const auto first = static_cast<int> (std::floor ((centre - spectrogram.fftSize / 2) * columnsPerSample));
            const auto last = static_cast<int> (std::ceil ((centre + spectrogram.fftSize / 2) * columnsPerSample));
            auto low = 1.0f, high = 0.0f;

            for (int x = first; x < last; ++x)
            {
                const auto column = idx (((x % Canvas::width) + Canvas::width) % Canvas::width);
                low = std::min (low, lowest[column]);
                high = std::max (high, highest[column]);
            }

            reference.steady[idx (row) * idx (frames) + idx (f)] = low > drawnValue && high - low < steadyTolerance ? 1 : 0;
        }
    }

    return reference;
}

// ================================================================================================
// The measurements
// ================================================================================================

/** What one display shows in one band of rows. NaN where there was nothing to measure.

    Levels: a cell's level is its dB relative to the loudest cell of the whole display (topDb); the
    drawing asks for (v - 1) * contrast there, for canvas value v. The residual is the difference.
    Fill, gaps and flicker read levels clipped to the visible range (60 dB under the loudest, as
    on the PNGs): how much a gap the eye already sees as black wobbles does not matter. */
struct BandMetrics
{
    // Pearson correlation of the display (clipped to topDb - contrast .. topDb) with the canvas
    // over the band's cells: does the picture read at all.
    double fidelity = notMeasured;

    // Over the drawn cells (canvas > 0.5): the fraction whose residual is within 6 dB of the
    // band's median residual. Gaps between stripes are cells far below; a solid fill scores 1.
    // The residual takes out the shading the drawing asks for, so gradients are not gaps.
    double fill = notMeasured;

    // The same over the plain dB, without taking out the drawing's shading (the literal reading).
    double fillRaw = notMeasured;

    // How deep the gaps go: the median residual minus its 10th percentile, dB.
    double gapDb = notMeasured;

    // Over steady cells (the canvas drawn and level under the whole window, see Reference): per
    // row and steady stretch of at least 0.5 s, the standard deviation of the residual over its
    // frames, averaged over the cells, dB. A steady drawing should look steady.
    double flickerDb = notMeasured;

    // The same stretches' residual as a modulation spectrum (Hann, amplitude of a sinusoidal
    // swing, power-averaged over the stretches): its peak between 2 and 20 Hz (up to the frame
    // rate's Nyquist), dB of swing, and where. Periodic pulsing, which shows as blobs.
    double modulationDb = notMeasured;
    double modulationHz = notMeasured;

    // Mean power of the undrawn cells (canvas < 0.01) relative to the drawn cells, over the frames
    // where something is drawn in the band, dB. Sound where nothing was drawn. Measured only if at
    // least 1 % of the band's cells are undrawn.
    double leakageDb = notMeasured;

    int drawnCells = 0;
    int steadyCells = 0;
    int undrawnCells = 0;
};

double pearson (const std::vector<double>& a, const std::vector<double>& b)
{
    const auto n = static_cast<double> (a.size());

    if (a.size() < 2)
        return notMeasured;

    double meanA = 0.0, meanB = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        meanA += a[i];
        meanB += b[i];
    }

    meanA /= n;
    meanB /= n;
    double ab = 0.0, aa = 0.0, bb = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        ab += (a[i] - meanA) * (b[i] - meanB);
        aa += (a[i] - meanA) * (a[i] - meanA);
        bb += (b[i] - meanB) * (b[i] - meanB);
    }

    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt (aa * bb) : notMeasured;
}

double percentile (std::vector<double> values, double p)
{
    if (values.empty())
        return notMeasured;

    const auto k = static_cast<size_t> (std::lround (p * static_cast<double> (values.size() - 1)));
    std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (k), values.end());
    return values[k];
}

/** The modulation spectrum of a set of level series (dB over frames). */
class ModulationSpectrum
{
public:
    explicit ModulationSpectrum (double rate) : frameRate (rate)
    {
        for (auto hz = modulationLowHz; hz <= modulationHighHz + 1.0e-9 && hz < 0.5 * frameRate; hz += modulationStepHz)
            frequencies.push_back (hz);

        power.assign (frequencies.size(), 0.0);
    }

    void add (const std::vector<double>& series)
    {
        const auto n = series.size();
        double mean = 0.0;

        for (const auto v : series)
            mean += v;

        mean /= static_cast<double> (n);

        // Hann over the stretch, so its ends do not smear every frequency.
        std::vector<double> weighted (n);
        double windowSum = 0.0;

        for (size_t k = 0; k < n; ++k)
        {
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (static_cast<double> (k) + 0.5) / static_cast<double> (n));
            weighted[k] = w * (series[k] - mean);
            windowSum += w;
        }

        for (size_t i = 0; i < frequencies.size(); ++i)
        {
            const auto step = std::polar (1.0, -twoPi * frequencies[i] / frameRate);
            std::complex<double> phasor (1.0, 0.0), sum (0.0, 0.0);

            for (size_t k = 0; k < n; ++k)
            {
                sum += weighted[k] * phasor;
                phasor *= step;
            }

            const auto amplitude = 2.0 * std::abs (sum) / windowSum;  // of a sinusoidal swing
            power[i] += static_cast<double> (n) * amplitude * amplitude;
        }

        weight += static_cast<double> (n);
    }

    /** The strongest swing (dB) and its frequency; NaN if nothing was added or the frame rate
        cannot show the range. */
    std::pair<double, double> peak() const
    {
        if (frequencies.empty() || ! (weight > 0.0))
            return { notMeasured, notMeasured };

        const auto loudest = std::max_element (power.begin(), power.end());
        return { std::sqrt (*loudest / weight), frequencies[idx (loudest - power.begin())] };
    }

private:
    double frameRate;
    std::vector<double> frequencies, power;
    double weight = 0.0;
};

BandMetrics measure (const Display& display, const Reference& reference, const LabBand& band,
                     float contrastDb, double frameRate)
{
    BandMetrics metrics;
    const auto frames = display.numFrames;
    const auto top = static_cast<double> (display.topDb);
    const auto contrast = static_cast<double> (contrastDb);
    const auto visibleFloor = top - static_cast<double> (visibleRangeDb);
    const auto residualOf = [&] (double db, double value)
    {
        return std::max (db, visibleFloor) - (top + (value - 1.0) * contrast);
    };

    std::vector<double> shown, drawn, residuals, levels;
    std::vector<uint8_t> frameHasDrawing (idx (frames), 0);
    std::vector<int> rows;

    for (int row = 0; row < displayRows; ++row)
        if (rowInBand (row, band))
            rows.push_back (row);

    for (const auto row : rows)
    {
        for (int f = 0; f < frames; ++f)
        {
            const auto db = static_cast<double> (display.at (row, f));
            const auto value = static_cast<double> (reference.value[idx (row) * idx (frames) + idx (f)]);
            shown.push_back (juce::jlimit (top - contrast, top, db));
            drawn.push_back (value);

            if (value > drawnValue)
            {
                residuals.push_back (residualOf (db, value));
                levels.push_back (std::max (db, visibleFloor));
                frameHasDrawing[idx (f)] = 1;
            }
        }
    }

    metrics.fidelity = pearson (shown, drawn);
    metrics.drawnCells = static_cast<int> (residuals.size());

    if (! residuals.empty())
    {
        const auto withinOfMedian = [] (const std::vector<double>& values)
        {
            const auto median = percentile (values, 0.5);
            const auto within = std::count_if (values.begin(), values.end(),
                                               [median] (double v) { return std::abs (v - median) <= fillToleranceDb; });
            return static_cast<double> (within) / static_cast<double> (values.size());
        };

        metrics.fill = withinOfMedian (residuals);
        metrics.fillRaw = withinOfMedian (levels);
        metrics.gapDb = percentile (residuals, 0.5) - percentile (residuals, 0.1);
    }

    // Flicker: steady stretches of each row.
    const auto minRun = std::max (4, static_cast<int> (std::ceil (minSteadySeconds * frameRate)));
    ModulationSpectrum modulation (frameRate);
    double deviationSum = 0.0;
    int steadyCells = 0;

    for (const auto row : rows)
    {
        for (int f = 0; f < frames;)
        {
            if (reference.steady[idx (row) * idx (frames) + idx (f)] == 0)
            {
                ++f;
                continue;
            }

            auto end = f;
            while (end < frames && reference.steady[idx (row) * idx (frames) + idx (end)] != 0)
                ++end;

            if (end - f >= minRun)
            {
                std::vector<double> series;

                for (int k = f; k < end; ++k)
                    series.push_back (residualOf (static_cast<double> (display.at (row, k)),
                                                  static_cast<double> (reference.value[idx (row) * idx (frames) + idx (k)])));

                double mean = 0.0, squares = 0.0;
                for (const auto v : series)
                    mean += v;
                mean /= static_cast<double> (series.size());
                for (const auto v : series)
                    squares += (v - mean) * (v - mean);

                deviationSum += std::sqrt (squares / static_cast<double> (series.size())) * static_cast<double> (series.size());
                steadyCells += static_cast<int> (series.size());
                modulation.add (series);
            }

            f = end;
        }
    }

    metrics.steadyCells = steadyCells;

    if (steadyCells > 0)
    {
        metrics.flickerDb = deviationSum / steadyCells;
        std::tie (metrics.modulationDb, metrics.modulationHz) = modulation.peak();
    }

    // Leakage, over the frames where something is drawn in the band.
    double drawnPower = 0.0, undrawnPower = 0.0;
    int drawnCount = 0, undrawnCount = 0;

    for (const auto row : rows)
    {
        for (int f = 0; f < frames; ++f)
        {
            if (frameHasDrawing[idx (f)] == 0)
                continue;

            const auto value = reference.value[idx (row) * idx (frames) + idx (f)];
            const auto power = std::pow (10.0, static_cast<double> (display.at (row, f)) / 10.0);

            if (value > drawnValue)
            {
                drawnPower += power;
                ++drawnCount;
            }
            else if (value < silentValue)
            {
                undrawnPower += power;
                ++undrawnCount;
            }
        }
    }

    metrics.undrawnCells = undrawnCount;

    if (drawnCount > 0 && drawnPower > 0.0 && undrawnCount > 0
        && static_cast<double> (undrawnCount) >= minUndrawnShare * static_cast<double> (shown.size()))
        metrics.leakageDb = 10.0 * std::log10 (std::max (1.0e-30, (undrawnPower / undrawnCount) / (drawnPower / drawnCount)));

    return metrics;
}

// ================================================================================================
// Pictures
// ================================================================================================

juce::Colour colourAt (float t)
{
    const auto& table = ui::theme::spectrumTable();
    return juce::Colour (table[idx (juce::jlimit (0, 255, juce::roundToInt (t * 255.0f)))]);
}

/** Display rows [firstRow, firstRow + numRows), `width` pixels across the pass: each pixel column
    shows the frame playing at its moment. Colours as on ArtFreq's canvas and live view. */
juce::Image displayImage (const Display& display, int firstRow, int numRows, int width)
{
    juce::Image image (juce::Image::RGB, width, numRows, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < numRows; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto frame = std::min (display.numFrames - 1, static_cast<int> ((x + 0.5) / width * display.numFrames));
            const auto db = display.at (firstRow + y, frame) - display.topDb;
            data.setPixelColour (x, y, colourAt (1.0f + db / visibleRangeDb));
        }

    return image;
}

/** The canvas on the same grid and colours: the picture the display would ideally show. */
juce::Image canvasImage (const Canvas& canvas, float contrastDb, int firstRow, int numRows, int width)
{
    juce::Image image (juce::Image::RGB, width, numRows, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    const auto columnsPerPixel = static_cast<double> (Canvas::width) / width;

    for (int y = 0; y < numRows; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto row = firstRow + y;
            const auto value = canvasMean (canvas, x * columnsPerPixel, (x + 1) * columnsPerPixel,
                                           canvasTopOfRow (row), canvasBottomOfRow (row));
            const auto t = value > 0.001f ? 1.0f + (value - 1.0f) * contrastDb / visibleRangeDb : 0.0f;
            data.setPixelColour (x, y, colourAt (t));
        }

    return image;
}

/** The plan's lines at their heights, each display row showing the loudest line inside it (dark
    where no line falls): where the sound's energy is put, before any analyser smears it. The
    rising comb (Smooth) puts energy at every height of its band: there each row shows the grid
    cell it falls in, at the brightness the comb is played for (amplitude / combGain), as the
    canvas does. */
juce::Image planImage (const ArtPlan& plan, int firstRow, int numRows, int width)
{
    juce::Image image (juce::Image::RGB, width, numRows, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    const auto shown = [&plan] (int column, int line) { return plan.amplitude (column, line) / (line < plan.combLines ? combGain : 1.0f); };
    auto loudest = 0.0f;

    for (int c = 0; c < plan.numColumns; ++c)
        for (int l = 0; l < plan.numLines; ++l)
            loudest = std::max (loudest, shown (c, l));

    for (int y = 0; y < numRows; ++y)
    {
        const auto row = firstRow + y;
        const auto low = 1.0 - (row + 1.0) / displayRows;
        const auto high = 1.0 - static_cast<double> (row) / displayRows;
        std::vector<int> lines;

        for (int l = plan.combLines; l < plan.numLines; ++l)
        {
            const auto norm = static_cast<double> (plan.lineNorm[idx (l)]);
            if (norm >= low && norm < high)
                lines.push_back (l);
        }

        if (plan.combLines > 0)
        {
            const auto cell = std::lround ((hzAtNorm (0.5 * (low + high)) - static_cast<double> (plan.combGridLowHz))
                                           / static_cast<double> (plan.combGridHz));

            if (cell >= 0 && cell < plan.combLines)
                lines.push_back (static_cast<int> (cell));
        }

        for (int x = 0; x < width; ++x)
        {
            const auto column = std::min (plan.numColumns - 1, static_cast<int> ((x + 0.5) / width * plan.numColumns));
            auto amplitude = 0.0f;

            for (const auto l : lines)
                amplitude = std::max (amplitude, shown (column, l));

            const auto t = amplitude > 0.0f && loudest > 0.0f
                             ? 1.0f + juce::Decibels::gainToDecibels (amplitude / loudest) / visibleRangeDb : 0.0f;
            data.setPixelColour (x, y, colourAt (t));
        }
    }

    return image;
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}

struct Panel
{
    juce::Image image;
    juce::String label;
    juce::String note;
};

/** Panels in a grid (`columns` across), each labelled, with a Hz axis down the left of each row. */
juce::Image makeSheet (const juce::String& title, const std::vector<Panel>& panels, int columns, int firstRow)
{
    constexpr int margin = 14, titleHeight = 30, labelHeight = 34, axisWidth = 46, gap = 12;
    const auto panelWidth = sheetPanelWidth;
    const auto panelHeight = panels.front().image.getHeight();
    const auto numRows = (static_cast<int> (panels.size()) + columns - 1) / columns;

    const auto width = 2 * margin + axisWidth + columns * panelWidth + (columns - 1) * gap;
    const auto height = 2 * margin + titleHeight + numRows * (labelHeight + panelHeight) + (numRows - 1) * gap;
    juce::Image sheet (juce::Image::RGB, width, height, true);
    juce::Graphics g (sheet);
    g.fillAll (ui::theme::bg);

    g.setColour (ui::theme::text);
    g.setFont (fonts::ui (15.0f, fonts::UiWeight::semiBold));
    g.drawText (title, margin, margin, width - 2 * margin, titleHeight - 8, juce::Justification::centredLeft);

    for (size_t i = 0; i < panels.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + axisWidth + column * (panelWidth + gap);
        const auto y = margin + titleHeight + row * (labelHeight + panelHeight + gap);
        const auto& panel = panels[i];

        if (! panel.image.isValid())
            continue;

        g.setColour (ui::theme::text);
        g.setFont (fonts::ui (12.0f, fonts::UiWeight::medium));
        g.drawText (panel.label, x, y, panelWidth, 16, juce::Justification::centredLeft);
        g.setColour (ui::theme::textDim);
        g.setFont (fonts::mono (11.0f));
        g.drawText (panel.note, x, y + 16, panelWidth, 16, juce::Justification::centredLeft);
        g.drawImageAt (panel.image, x, y + labelHeight);

        if (column == 0)
        {
            g.setFont (fonts::mono (10.0f));

            for (const auto hz : { 30.0, 50.0, 100.0, labSplitHz, 500.0, 1000.0, 2000.0 })
            {
                // The split between the low and mid bands is marked in colour.
                const auto tickY = y + labelHeight + juce::roundToInt (rowPosition (hz) - firstRow);
                g.setColour (juce::exactlyEqual (hz, labSplitHz) ? ui::theme::accent2 : ui::theme::textFaint);
                g.fillRect (x - 6, tickY, 5, 1);
                g.drawText (formatHz (hz), x - axisWidth, tickY - 7, axisWidth - 9, 14, juce::Justification::centredRight);
            }
        }
    }

    return sheet;
}

// ================================================================================================
// The report
// ================================================================================================

struct Result
{
    juce::String drawing;
    int density = 0;
    int fftSize = 0;
    WindowShape window = WindowShape::hann;
    Mapping mapping = Mapping::interpolate;
    std::array<BandMetrics, labBands.size()> bands;
};

struct CostResult
{
    juce::String drawing;
    int density = 0;
    Cost cost;
};

juce::var number (double value)
{
    return std::isfinite (value) ? juce::var (value) : juce::var();
}

juce::var toVar (const BandMetrics& m)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("fidelity", number (m.fidelity));
    object->setProperty ("fill", number (m.fill));
    object->setProperty ("fillRaw", number (m.fillRaw));
    object->setProperty ("gapDb", number (m.gapDb));
    object->setProperty ("flickerDb", number (m.flickerDb));
    object->setProperty ("modulationDb", number (m.modulationDb));
    object->setProperty ("modulationHz", number (m.modulationHz));
    object->setProperty ("leakageDb", number (m.leakageDb));
    object->setProperty ("drawnCells", m.drawnCells);
    object->setProperty ("steadyCells", m.steadyCells);
    object->setProperty ("undrawnCells", m.undrawnCells);
    return juce::var (object);
}

juce::var toJson (const Options& options, const std::vector<TestDrawing>& drawings,
                  const std::vector<CostResult>& costs, const std::vector<Result>& results)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("tool", "ArtFreqLab");
    root->setProperty ("lows", lowsName (options.lows));
    root->setProperty ("sampleRate", sampleRate);
    root->setProperty ("passSeconds", passSeconds);
    root->setProperty ("contrastDb", options.contrastDb);
    root->setProperty ("displayRows", displayRows);
    root->setProperty ("displayHz", juce::Array<juce::var> { displayLowHz, displayHighHz });

    auto* bandsObject = new juce::DynamicObject();
    for (const auto& band : labBands)
        bandsObject->setProperty (band.name, juce::Array<juce::var> { band.lowHz, band.highHz });
    root->setProperty ("bands", juce::var (bandsObject));

    juce::Array<juce::var> drawingList;
    for (const auto& drawing : drawings)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("name", drawing.name);
        object->setProperty ("description", drawing.description);
        drawingList.add (juce::var (object));
    }
    root->setProperty ("drawings", drawingList);

    juce::Array<juce::var> costList;
    for (const auto& entry : costs)
    {
        const auto& c = entry.cost;
        auto* object = new juce::DynamicObject();
        object->setProperty ("drawing", entry.drawing);
        object->setProperty ("density", entry.density);
        object->setProperty ("lines", c.lines);
        object->setProperty ("linesLow", c.linesLow);
        object->setProperty ("linesMid", c.linesMid);
        object->setProperty ("planMs", number (c.planMs));
        object->setProperty ("renderMsPerSecond", number (c.renderMsPerSecond));
        object->setProperty ("realtimeFactor", number (1000.0 / c.renderMsPerSecond));
        object->setProperty ("peakDbfs", number (c.peakDb));
        object->setProperty ("rmsDbfs", number (c.rmsDb));
        object->setProperty ("crestDb", number (c.crestDb));
        object->setProperty ("clipsAtDefaultLevel", c.clipsAtDefaultLevel);
        costList.add (juce::var (object));
    }
    root->setProperty ("costs", costList);

    juce::Array<juce::var> resultList;
    for (const auto& result : results)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("drawing", result.drawing);
        object->setProperty ("density", result.density);
        object->setProperty ("fft", result.fftSize);
        object->setProperty ("window", windowName (result.window));
        object->setProperty ("mapping", mappingName (result.mapping));
        for (size_t b = 0; b < labBands.size(); ++b)
            object->setProperty (labBands[b].name, toVar (result.bands[b]));
        resultList.add (juce::var (object));
    }
    root->setProperty ("results", resultList);

    return juce::var (root);
}

juce::String cell (double value, int width, int decimals)
{
    if (! std::isfinite (value))
        return juce::String ("-").paddedLeft (' ', width);

    // Values that round to zero print without a sign (the JSON keeps 4 decimals, so a change
    // against a baseline can be a hair off zero).
    if (std::abs (value) < 0.5 * std::pow (10.0, -decimals))
        value = 0.0;

    // juce::String (double, 0) would print every digit.
    const auto digits = decimals > 0 ? juce::String (value, decimals) : juce::String (juce::roundToInt (value));
    return digits.paddedLeft (' ', width);
}

/** A number for a sheet's label: "-" where nothing was measured. */
juce::String note (double value, int decimals)
{
    return std::isfinite (value) ? juce::String (value, decimals) : juce::String ("-");
}

/** The mean of the finite values (NaN if none). */
double meanOf (const std::vector<double>& values)
{
    double sum = 0.0;
    int count = 0;

    for (const auto v : values)
        if (std::isfinite (v))
        {
            sum += v;
            ++count;
        }

    return count > 0 ? sum / count : notMeasured;
}

/** The metrics shown in the tables: name, how to read one band, width and decimals. */
struct Column
{
    const char* name;
    double (*read) (const BandMetrics&);
    int decimals;
};

const std::array<Column, 6> tableColumns { {
    { "fidelity", [] (const BandMetrics& m) { return m.fidelity; }, 2 },
    { "fill",     [] (const BandMetrics& m) { return m.fill; }, 2 },
    { "gap dB",   [] (const BandMetrics& m) { return m.gapDb; }, 1 },
    { "flicker",  [] (const BandMetrics& m) { return m.flickerDb; }, 2 },
    { "mod dB",   [] (const BandMetrics& m) { return m.modulationDb; }, 2 },
    { "leak dB",  [] (const BandMetrics& m) { return m.leakageDb; }, 1 },
} };

juce::String tableHeader (const juce::String& first)
{
    juce::String line (first), second (juce::String().paddedRight (' ', first.length()));

    for (const auto& column : tableColumns)
    {
        line << juce::String (column.name).paddedLeft (' ', 14) << "  ";
        second << "    low    mid  ";
    }

    return line.trimEnd() + "\n" + second.trimEnd() + "\n";
}

juce::String tableRow (const juce::String& first, const BandMetrics& low, const BandMetrics& mid)
{
    juce::String line (first);

    for (const auto& column : tableColumns)
        line << cell (column.read (low), 7, column.decimals) << cell (column.read (mid), 7, column.decimals) << "  ";

    return line.trimEnd() + "\n";
}

/** Band metrics averaged (each field over its finite values) over a set of results. */
BandMetrics averageOf (const std::vector<const BandMetrics*>& set)
{
    BandMetrics mean;
    const auto average = [&set] (double BandMetrics::* field)
    {
        std::vector<double> values;
        for (const auto* m : set)
            values.push_back (m->*field);
        return meanOf (values);
    };

    mean.fidelity = average (&BandMetrics::fidelity);
    mean.fill = average (&BandMetrics::fill);
    mean.fillRaw = average (&BandMetrics::fillRaw);
    mean.gapDb = average (&BandMetrics::gapDb);
    mean.flickerDb = average (&BandMetrics::flickerDb);
    mean.modulationDb = average (&BandMetrics::modulationDb);
    mean.modulationHz = average (&BandMetrics::modulationHz);
    mean.leakageDb = average (&BandMetrics::leakageDb);
    return mean;
}

BandMetrics fromVar (const juce::var& v)
{
    const auto read = [&v] (const char* name)
    {
        const auto value = v.getProperty (name, {});
        return value.isVoid() ? notMeasured : static_cast<double> (value);
    };

    BandMetrics m;
    m.fidelity = read ("fidelity");
    m.fill = read ("fill");
    m.fillRaw = read ("fillRaw");
    m.gapDb = read ("gapDb");
    m.flickerDb = read ("flickerDb");
    m.modulationDb = read ("modulationDb");
    m.modulationHz = read ("modulationHz");
    m.leakageDb = read ("leakageDb");
    return m;
}

BandMetrics difference (const BandMetrics& now, const BandMetrics& before)
{
    BandMetrics d;
    d.fidelity = now.fidelity - before.fidelity;
    d.fill = now.fill - before.fill;
    d.fillRaw = now.fillRaw - before.fillRaw;
    d.gapDb = now.gapDb - before.gapDb;
    d.flickerDb = now.flickerDb - before.flickerDb;
    d.modulationDb = now.modulationDb - before.modulationDb;
    d.leakageDb = now.leakageDb - before.leakageDb;
    return d;
}

juce::String resultKey (const juce::String& drawing, int density, int fftSize, const juce::String& window, const juce::String& mapping)
{
    return drawing + "|" + juce::String (density) + "|" + juce::String (fftSize) + "|" + window + "|" + mapping;
}

constexpr size_t lowBand = 1, midBand = 2;

juce::String makeReport (const Options& options, const std::vector<TestDrawing>& drawings,
                         const std::vector<CostResult>& costs, const std::vector<Result>& results)
{
    juce::String text;
    text << "ArtFreq lows lab, lows " << lowsName (options.lows) << "\n"
         << "48 kHz, one 8 s pass (4 bars at 120 BPM, looped), contrast " << juce::String (options.contrastDb, 0)
         << " dB; display 600 rows, log 20 Hz - 20 kHz; measured over 30 Hz - 2 kHz:\n"
         << "low = 30 - 250 Hz, mid = 250 Hz - 2 kHz. Drawings:\n";

    for (const auto& drawing : drawings)
        text << "  " << drawing.name.paddedRight (' ', 6) << drawing.description << "\n";

    const auto primary = [] (const Result& r) { return r.window == WindowShape::hann && r.mapping == Mapping::interpolate; };

    // ---- Summary: one line per density -------------------------------------------------------
    text << "\nSUMMARY (Hann, linear interpolation; mean over drawings and FFT sizes)\n" << tableHeader ("density   ");

    for (const auto density : options.densities)
    {
        std::vector<const BandMetrics*> low, mid;
        for (const auto& r : results)
            if (r.density == density && primary (r))
            {
                low.push_back (&r.bands[lowBand]);
                mid.push_back (&r.bands[midBand]);
            }

        text << tableRow (juce::String (density).paddedRight (' ', 10), averageOf (low), averageOf (mid));
    }

    // ---- Every drawing, density and FFT size -------------------------------------------------
    text << "\nPER DRAWING (Hann, linear interpolation)\n" << tableHeader ("drawing dens    fft   ");

    for (const auto& r : results)
        if (primary (r))
            text << tableRow (r.drawing.paddedRight (' ', 8) + juce::String (r.density).paddedLeft (' ', 4)
                                  + juce::String (r.fftSize).paddedLeft (' ', 7) + "   ",
                              r.bands[lowBand], r.bands[midBand]);

    // ---- The analyser variants ---------------------------------------------------------------
    text << "\nANALYSER VARIANTS (mean over drawings)\n" << tableHeader ("dens   fft  window mapping  ");

    for (const auto density : options.densities)
        for (const auto fftSize : options.fftSizes)
            for (const auto window : { WindowShape::hann, WindowShape::blackmanHarris })
                for (const auto mapping : { Mapping::interpolate, Mapping::nearest })
                {
                    std::vector<const BandMetrics*> low, mid;
                    for (const auto& r : results)
                        if (r.density == density && r.fftSize == fftSize && r.window == window && r.mapping == mapping)
                        {
                            low.push_back (&r.bands[lowBand]);
                            mid.push_back (&r.bands[midBand]);
                        }

                    text << tableRow (juce::String (density).paddedLeft (' ', 4) + juce::String (fftSize).paddedLeft (' ', 6) + "  "
                                          + juce::String (windowName (window)).paddedRight (' ', 7)
                                          + juce::String (mappingName (mapping)).paddedRight (' ', 9),
                                      averageOf (low), averageOf (mid));
                }

    // ---- Cost --------------------------------------------------------------------------------
    text << "\nCOST\n"
         << "drawing dens  lines  low  mid  plan ms  render ms/s  x realtime  peak dBFS  rms dBFS  crest dB  clips at -6 dB\n";

    for (const auto& entry : costs)
    {
        const auto& c = entry.cost;
        text << entry.drawing.paddedRight (' ', 8) << juce::String (entry.density).paddedLeft (' ', 4)
             << juce::String (c.lines).paddedLeft (' ', 7) << juce::String (c.linesLow).paddedLeft (' ', 5)
             << juce::String (c.linesMid).paddedLeft (' ', 5) << cell (c.planMs, 9, 1) << cell (c.renderMsPerSecond, 13, 1)
             << cell (1000.0 / c.renderMsPerSecond, 12, 0) << cell (c.peakDb, 11, 1) << cell (c.rmsDb, 10, 1)
             << cell (c.crestDb, 10, 1) << (c.clipsAtDefaultLevel ? "  yes" : "  no") << "\n";
    }

    // ---- Against a baseline ------------------------------------------------------------------
    if (options.baseline != juce::File())
    {
        const auto baseline = juce::JSON::parse (options.baseline);
        std::map<juce::String, juce::var> before;

        if (const auto* list = baseline.getProperty ("results", {}).getArray())
            for (const auto& entry : *list)
                before[resultKey (entry.getProperty ("drawing", {}).toString(), entry.getProperty ("density", {}),
                                  entry.getProperty ("fft", {}), entry.getProperty ("window", {}).toString(),
                                  entry.getProperty ("mapping", {}).toString())] = entry;

        // (Runs from before Lows had a name give their prototype method number: 0 = stripes.)
        const auto baselineLows = baseline.hasProperty ("lows") ? baseline.getProperty ("lows", {}).toString()
                                                                : "method " + baseline.getProperty ("method", "?").toString();
        text << "\nCHANGE AGAINST " << options.baseline.getFullPathName() << " (" << baselineLows
             << "), Hann, linear interpolation, mean over FFT sizes;\n"
             << "higher is better for fidelity and fill, lower for gap, flicker, mod and leak\n"
             << tableHeader ("drawing dens   ");

        for (const auto density : options.densities)
        {
            std::vector<BandMetrics> allLow, allMid;

            for (const auto& drawing : drawings)
            {
                std::vector<BandMetrics> lowChanges, midChanges;

                for (const auto& r : results)
                {
                    if (r.drawing != drawing.name || r.density != density || ! primary (r))
                        continue;

                    const auto found = before.find (resultKey (r.drawing, r.density, r.fftSize, windowName (r.window), mappingName (r.mapping)));
                    if (found == before.end())
                        continue;

                    lowChanges.push_back (difference (r.bands[lowBand], fromVar (found->second.getProperty ("low", {}))));
                    midChanges.push_back (difference (r.bands[midBand], fromVar (found->second.getProperty ("mid", {}))));
                }

                if (lowChanges.empty())
                    continue;

                std::vector<const BandMetrics*> low, mid;
                for (size_t i = 0; i < lowChanges.size(); ++i)
                {
                    low.push_back (&lowChanges[i]);
                    mid.push_back (&midChanges[i]);
                }

                const auto meanLow = averageOf (low), meanMid = averageOf (mid);
                allLow.push_back (meanLow);
                allMid.push_back (meanMid);
                text << tableRow (drawing.name.paddedRight (' ', 8) + juce::String (density).paddedLeft (' ', 4) + "   ", meanLow, meanMid);
            }

            std::vector<const BandMetrics*> low, mid;
            for (size_t i = 0; i < allLow.size(); ++i)
            {
                low.push_back (&allLow[i]);
                mid.push_back (&allMid[i]);
            }

            if (! low.empty())
                text << tableRow (juce::String ("mean").paddedRight (' ', 8) + juce::String (density).paddedLeft (' ', 4) + "   ",
                                  averageOf (low), averageOf (mid));
        }
    }

    text << "\nfidelity  Pearson r of the display (dB, clipped to the contrast range under its loudest cell) with\n"
            "          the canvas on the same rows and frames: 1 = the picture reads exactly\n"
            "fill      over drawn cells (canvas > 0.5), the fraction within 6 dB of the median, after taking\n"
            "          out the shading the drawing asks for: 1 = solid, stripes with gaps score low\n"
            "gap dB    median minus 10th percentile of the same: how deep the gaps go (fill, gap and\n"
            "          flicker see levels clipped to the visible 60 dB under the loudest cell, as the PNGs)\n"
            "flicker   over steady cells (drawn and level under the whole window), the standard deviation\n"
            "          over time, dB: 0 = steady\n"
            "mod dB    the peak of the steady cells' modulation spectrum over 2 - 20 Hz (to the frame rate's\n"
            "          Nyquist; long FFTs cannot see fast pulsing), dB of swing: periodic pulsing, blobs\n"
            "leak dB   mean power where nothing is drawn relative to where it is, in the same frames\n"
            "          (when at least 1 % of the band is undrawn)\n"
            "fillRaw (fill on the plain dB) and the modulation peaks' frequencies are in metrics.json\n";

    return text;
}

} // namespace

int main (int argc, char* argv[])
{
    // Fonts for the sheets' labels; no window is ever opened.
    const juce::ScopedJuceInitialiser_GUI gui;

    Options options;
    juce::String error;

    if (! parseOptions (argc, argv, options, error))
    {
        if (! options.showHelp)
            std::cerr << error << "\n\n";

        printUsage();
        return options.showHelp ? 0 : 1;
    }

    if (const auto made = options.outputDir.createDirectory(); made.failed())
    {
        std::cerr << "Cannot create " << options.outputDir.getFullPathName() << ": " << made.getErrorMessage() << std::endl;
        return 1;
    }

    std::cout << "ArtFreqLab: lows " << lowsName (options.lows) << " -> " << options.outputDir.getFullPathName() << std::endl;

    std::vector<TestDrawing> drawings;
    std::vector<CostResult> costs;
    std::vector<Result> results;
    bool written = true;

    const auto firstRow = sheetFirstRow();
    const auto sheetRows = sheetLastRow() - firstRow + 1;
    const auto fileName = [] (const juce::String& drawing, int density, const juce::String& rest)
    {
        return drawing + "-d" + juce::String (density) + rest;
    };

    for (const auto& name : options.cases)
    {
        drawings.emplace_back();
        auto& drawing = drawings.back();

        if (! makeDrawing (name, options, drawing))
        {
            std::cerr << "Cannot read the picture " << options.image.getFullPathName() << std::endl;
            return 1;
        }

        written = writePng (canvasImage (drawing.canvas, options.contrastDb, 0, displayRows, pngWidth),
                            options.outputDir.getChildFile (name + "-canvas.png")) && written;

        for (const auto density : options.densities)
        {
            const auto settings = planSettingsFor (density, options.contrastDb, options.lows);
            const auto render = renderDrawing (drawing.canvas, settings);
            costs.push_back ({ name, density, render.cost });

            std::cout << "  " << name << ", " << density << " lines: " << render.cost.lines << " sines played, render "
                      << juce::String (render.cost.renderMsPerSecond, 1) << " ms per second" << std::endl;

            if (options.writeWav)
            {
                ExportSettings bounce;
                bounce.seconds = passSeconds;
                bounce.gain = defaultLevel;
                juce::String wavError;

                if (! renderPlanToWav (*render.plan, bounce, options.outputDir.getChildFile (fileName (name, density, ".wav")), wavError))
                {
                    std::cerr << "Cannot write the bounce: " << wavError << std::endl;
                    written = false;
                }
            }

            // The sheet: the canvas and the plan's lines, then one row of FFT sizes per analyser.
            std::vector<Panel> sheet;
            const auto sheetColumns = static_cast<int> (options.fftSizes.size()) + 1;
            const auto addPanel = [&sheet] (juce::Image image, const juce::String& label, const juce::String& note)
            {
                sheet.push_back ({ std::move (image), label, note });
            };

            std::map<std::pair<int, int>, Panel> analysed;  // (variant, fft index) -> panel

            for (size_t n = 0; n < options.fftSizes.size(); ++n)
            {
                const auto fftSize = options.fftSizes[n];
                std::unique_ptr<Reference> reference;

                for (const auto window : { WindowShape::hann, WindowShape::blackmanHarris })
                {
                    const auto spectrogram = analyse (render.audio, fftSize, window);

                    // Both windows share the frame grid, so the canvas is resampled once per FFT size.
                    if (reference == nullptr)
                        reference = std::make_unique<Reference> (makeReference (drawing.canvas, spectrogram));

                    for (const auto mapping : { Mapping::interpolate, Mapping::nearest })
                    {
                        const auto display = toDisplay (spectrogram, mapping);

                        Result result;
                        result.drawing = name;
                        result.density = density;
                        result.fftSize = fftSize;
                        result.window = window;
                        result.mapping = mapping;

                        for (size_t b = 0; b < labBands.size(); ++b)
                            result.bands[b] = measure (display, *reference, labBands[b], options.contrastDb, spectrogram.frameRate());

                        results.push_back (result);

                        if (mapping == Mapping::interpolate)
                            written = writePng (displayImage (display, 0, displayRows, pngWidth),
                                                options.outputDir.getChildFile (fileName (name, density, "-fft" + juce::String (fftSize)
                                                                                                          + "-" + windowName (window) + ".png")))
                                    && written;

                        // Sheet rows: Hann interpolated, Blackman-Harris interpolated, Hann nearest bin
                        // (Blackman-Harris with the nearest bin adds little to look at; it is in the tables).
                        const auto variant = window == WindowShape::blackmanHarris ? 1 : (mapping == Mapping::nearest ? 2 : 0);

                        if (window == WindowShape::hann || mapping == Mapping::interpolate)
                        {
                            const auto& low = result.bands[lowBand];
                            const auto& mid = result.bands[midBand];
                            analysed[{ variant, static_cast<int> (n) }] = {
                                displayImage (display, firstRow, sheetRows, sheetPanelWidth),
                                juce::String (fftSize) + "  " + (window == WindowShape::hann ? "Hann" : "Blackman-Harris")
                                    + (mapping == Mapping::interpolate ? ", interpolated" : ", nearest bin"),
                                "fill " + note (low.fill, 2) + " | " + note (mid.fill, 2)
                                    + "   fid " + note (low.fidelity, 2) + " | " + note (mid.fidelity, 2)
                                    + "   flicker " + note (low.flickerDb, 1) + " | " + note (mid.flickerDb, 1)
                            };
                        }
                    }
                }
            }

            for (int variant = 0; variant < 3; ++variant)
            {
                if (variant == 0)
                    addPanel (canvasImage (drawing.canvas, options.contrastDb, firstRow, sheetRows, sheetPanelWidth),
                              "canvas", "the drawing, as it should look");
                else if (variant == 1)
                    addPanel (planImage (*render.plan, firstRow, sheetRows, sheetPanelWidth),
                              "plan lines", juce::String (render.cost.linesLow) + " in 30-250 Hz, "
                                                + juce::String (render.cost.linesMid) + " in 250 Hz-2 kHz");
                else
                    addPanel ({}, {}, {});

                for (int n = 0; n < static_cast<int> (options.fftSizes.size()); ++n)
                    sheet.push_back (analysed[{ variant, n }]);
            }

            written = writePng (makeSheet (name + ", density " + juce::String (density) + ", lows " + lowsName (options.lows)
                                               + "  (30 Hz - 2 kHz; notes: low | mid)",
                                           sheet, sheetColumns, firstRow),
                                options.outputDir.getChildFile (fileName (name, density, "-sheet.png")))
                    && written;
        }
    }

    const auto json = juce::JSON::toString (toJson (options, drawings, costs, results),
                                            juce::JSON::FormatOptions().withSpacing (juce::JSON::Spacing::multiLine)
                                                                       .withMaxDecimalPlaces (4));
    const auto report = makeReport (options, drawings, costs, results);

    written = options.outputDir.getChildFile ("metrics.json").replaceWithText (json) && written;
    written = options.outputDir.getChildFile ("metrics.txt").replaceWithText (report) && written;
    std::cout << "\n" << report << std::endl;

    if (! written)
    {
        std::cerr << "Some files could not be written" << std::endl;
        return 1;
    }

    return 0;
}
