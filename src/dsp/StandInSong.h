#pragma once

// The stand-in for a song, which ArtFreq FX's canvas needs to show what carving will look like
// (the effect works on the user's song, which the editor does not have).
//
// It has a song's shape on an analyser: a spectrum falling towards the highs, sixteen beats
// across the drawing's length with a kick in the lows and a hat in the highs, held chords of five
// partials changing every four beats, and grain. standInSongDb says how loud it is at each place
// of the canvas, as an analyser of 4096 points shows it (dB of a cell; a full-scale sine reads 0):
// about where a loud master (-10 dBFS RMS) sits, -32 dB a cell at 1 kHz.
//
// The canvas's fast preview draws that level as a picture (ui/EffectPreview.h). StandInSong makes
// it into sound, so the measured preview (ui/MeasuredPreview.h) can carve it with the real carver
// and show what an analyser makes of the result: noise whose spectrogram is that picture, made by
// overlap-adding frames of random phase with the picture's levels as their spectrum. The noise
// is the same for the same place on the timeline, whatever is drawn, so the preview does not
// shimmer from one edit to the next.

#include "core/FrequencyScale.h"

#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace artfreq
{

/** The stand-in's level at a place on the canvas in dB of an analyser cell at FFT 4096: x 0..1
    along the drawing's length (it repeats beyond), y 0..1 from the bottom of the scale to the
    top. */
float standInSongDb (double x, double y) noexcept;

/** standInSongDb worked out once on a grid of textureWidth x textureHeight (row 0 at the top,
    each texel at its centre), shared: resampled from here, it costs a fraction of working it out
    pixel by pixel at a canvas's size. */
const std::vector<float>& standInSongTexture();
constexpr int standInTextureWidth = 768;
constexpr int standInTextureHeight = 384;

/** The texture's level at (x, y) as above, linear between the texels. */
float standInSongTextureDb (double x, double y) noexcept;

/** The stand-in as sound. Allocates in the constructor only; any thread. */
class StandInSong
{
public:
    /** `passSamples`: the drawing's length at `sampleRate` (x of the texture runs over it, and on
        before and after it); `firstSample`: where on that timeline render() starts (negative:
        before the pass). */
    StandInSong (const FrequencyScale& scale, double sampleRate, int64_t passSamples, int64_t firstSample);

    /** The next `numSamples` samples (mono). */
    void render (float* out, int numSamples) noexcept;

    /** The frames' size at a sample rate: 4096 at 48 kHz, the same time at other rates. */
    static int frameSizeFor (double sampleRate) noexcept;

private:
    void addFrame() noexcept;

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
};

} // namespace artfreq
