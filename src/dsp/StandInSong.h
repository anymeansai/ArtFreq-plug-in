#pragma once

// The stand-in for a song, which ArtFreq FX's canvas needs to show what carving will look like
// (the effect works on the user's song, which the editor does not have).
//
// It has a song's shape on an analyser: a spectrum falling towards the highs as a mastered mix's
// does (5 dB an octave in an analyser's cells: a little steeper than the 4.5 dB of a typical
// current master, and the tilt of the truth lab's dense test master, tools/TruthLab.cpp, within a
// few dB in every band), little
// below 30 Hz and above 16 kHz, sixteen beats across the drawing's length with a kick in the lows
// and a hat in the highs, held chords of five partials changing every four beats, and grain; in
// the sound, the kick is a falling tone and a bass line plays under it, as in a mix.
// standInSongDb says how loud it is at each moment and frequency, as an analyser of 4096 points
// shows it (dB of a cell; a full-scale sine reads 0): where a loud master (-10 dBFS RMS) sits,
// about -27 dB a cell at 70 Hz, -46 at 1 kHz, -61 at 8 kHz. (Until the measured preview, the
// stand-in fell only 1.8 dB an octave: carves in the mids and highs looked 13 to 25 dB deeper
// against it than against the test master.) It is defined in Hz, not by the canvas's height, so
// it is the same song on every scale.
//
// The canvas's fast preview draws that level as a picture (ui/EffectPreview.h). StandInSong makes
// it into sound, so the measured preview (ui/MeasuredPreview.h) can carve it with the real carver
// and show what an analyser makes of the result: noise whose spectrogram is that picture, made by
// overlap-adding frames of random phase with the picture's levels as their spectrum. The noise
// is the same for the same place on the timeline, whatever is drawn, so the preview does not
// shimmer from one edit to the next.

#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace artfreq
{

/** The stand-in's level in dB of an analyser cell at FFT 4096: x 0..1 along the drawing's length
    (it repeats beyond), at `hz`. */
float standInSongDb (double x, double hz) noexcept;

/** standInSongDb worked out once on a grid of textureWidth x textureHeight, x across, the rows
    evenly in log frequency from textureTopHz (row 0) down to textureBottomHz, each texel at its
    centre; shared: resampled from here, it costs a fraction of working it out pixel by pixel at a
    canvas's size. */
const std::vector<float>& standInSongTexture();
constexpr int standInTextureWidth = 768;
constexpr int standInTextureHeight = 384;
constexpr double standInTextureBottomHz = 10.0;
constexpr double standInTextureTopHz = 24000.0;

/** The texture's row (fractional, row 0 at the top) for a frequency (clamped to its range). */
double standInTextureRow (double hz) noexcept;

/** The texture's level at (x, hz) as above, linear between the texels. */
float standInSongTextureDb (double x, double hz) noexcept;

/** The stand-in as sound. Allocates in the constructor only; any thread. */
class StandInSong
{
public:
    /** `passSamples`: the drawing's length at `sampleRate` (x of the texture runs over it, and on
        before and after it); `firstSample`: where on that timeline render() starts (negative:
        before the pass). */
    StandInSong (double sampleRate, int64_t passSamples, int64_t firstSample);

    /** The next `numSamples` samples (mono). */
    void render (float* out, int numSamples) noexcept;

    /** The frames' size at a sample rate: 4096 at 48 kHz, the same time at other rates. */
    static int frameSizeFor (double sampleRate) noexcept;

private:
    void addFrame() noexcept;
    void addLows (float* out, int numSamples) noexcept;

    int frameSize = 4096, hop = 1024;
    int64_t passSamples = 1;
    int64_t nextFrame = 0;       // the index of the next frame to add (frame k starts at k * hop)
    int64_t olaStart = 0;        // the timeline sample at ola[0]
    int64_t position = 0;        // the next sample render() gives
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, spectrum, ola;
    std::vector<int> binRow;     // per bin: the texture row above it ...
    std::vector<float> binFraction;  // ... and how far down to the next
    float amplitudeScale = 1.0f;

    // The kick and the bass line: tones, as a song's are (the noise alone filled carved holes in
    // the lows far less than a master's bright bass lines do).
    double sampleRate = 48000.0;
    double beatSamples = 1.0;        // a sixteenth of the pass
    int64_t tonalPosition = 0;       // the next sample addLows makes
    int64_t kickBeat = -1;           // the beat the kick's state is for
    double kickPhase = 0.0, kickFall = 0.0, kickDecay = 0.0, kickSeconds = 0.0;
    int64_t bassNote = -1;           // the half-beat the bass's state is for
    double bassRe = 1.0, bassIm = 0.0, bassStepRe = 1.0, bassStepIm = 0.0, bassSeconds = 0.0, bassNoteSeconds = 0.0;
};

} // namespace artfreq
