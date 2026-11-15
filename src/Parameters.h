#pragma once

// Parameter ids, ranges and defaults, shared by the processor, the editor and the tests. ArtFreq
// (the instrument) and ArtFreq FX (the effect) share every parameter; the effect adds Mode and
// Depth.

#include "core/ArtPlan.h"
#include "core/FrequencyScale.h"
#include "dsp/PlayheadTracker.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace artfreq
{

/** What ArtFreq FX does with the audio coming in (the Mode parameter, in its order). */
enum class EffectMode
{
    carve = 0,      // the drawing's frequencies are muted: it shows as dark shapes in the song
    carveDraw = 1,  // carved a little wider than the drawing, and the drawing played in the hole
    stencil = 2,    // the opposite of Carve: only the drawing's frequencies pass
    draw = 3        // the drawing played on top of the audio, which is left as it is
};

/** Whether a mode changes the audio coming in, and whether it plays the drawing's sines. */
constexpr bool carvesAudio (EffectMode mode) noexcept { return mode != EffectMode::draw; }
constexpr bool playsSines (EffectMode mode) noexcept { return mode == EffectMode::carveDraw || mode == EffectMode::draw; }

} // namespace artfreq

namespace artfreq::params
{

namespace id
{
    // Sound
    inline constexpr const char* level       = "level";        // dB, -60..0, default -6
    inline constexpr const char* density     = "density";      // lines, 16..1024 (int), default 256
    inline constexpr const char* contrast    = "contrast";     // dB, 12..72, default 40
    inline constexpr const char* lowEnd      = "lowEnd";       // choice: Smooth, Stripes (not automatable)

    // Timing
    inline constexpr const char* length      = "length";       // choice, see lengthNames(), default "4 bars"
    inline constexpr const char* trigger     = "trigger";      // choice: Transport, MIDI note
    inline constexpr const char* repeat      = "repeat";       // bool, default off: loop rather than play once
    inline constexpr const char* startBar    = "startBar";     // int 1..999, default 1: where a single Transport pass starts
    inline constexpr const char* tempoSource = "tempoSource";  // choice: Host, Manual
    inline constexpr const char* bpm         = "bpm";          // 20..300, default 120

    // Analyser match (not automatable): how the user's spectrogram draws frequency
    inline constexpr const char* scale       = "scale";        // choice: Log, Linear, Mel
    inline constexpr const char* rangeLow    = "rangeLow";     // Hz, 10..1000, default 20
    inline constexpr const char* rangeHigh   = "rangeHigh";    // Hz, 2000..24000, default 20000
    inline constexpr const char* slope       = "slope";        // choice: 0, 3, 4.5, 6 dB/oct

    // ArtFreq FX only: what happens to the audio coming in
    inline constexpr const char* mode        = "mode";         // choice: Carve, Carve + Draw, Stencil, Draw
    inline constexpr const char* depth       = "depth";        // dB, -120..0, default -80: how far carved frequencies drop
} // namespace id

/** Every parameter of the instrument; with forEffect also Mode and Depth (ArtFreq FX). */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout (bool forEffect = false);

// Choices, in parameter order.
juce::StringArray lengthNames();       // "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars"
juce::StringArray triggerNames();      // "Transport", "MIDI note"
juce::StringArray tempoSourceNames();  // "Host", "Manual"
juce::StringArray scaleNames();        // "Log", "Linear", "Mel"
juce::StringArray slopeNames();        // "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct"
juce::StringArray lowEndNames();       // "Smooth", "Stripes"
juce::StringArray modeNames();         // "Carve", "Carve + Draw", "Stencil", "Draw"

constexpr int defaultLengthIndex = 4;  // "4 bars"

/** Depth: deep enough by default that carved shapes look black on an analyser even over a
    master's bass and kick (about -15 dB there, against a floor near -95 dB). */
constexpr float minDepthDb = -120.0f;
constexpr float defaultDepthDb = -80.0f;
constexpr int firstStartBar = 1;
constexpr int lastStartBar = 999;

/** Lows: Smooth for new instances; Stripes, ArtFreq 0.1 and 0.2's only look, for states saved
    before the parameter existed, so projects made with them do not change. */
constexpr int smoothLowEndIndex = 0;
constexpr int stripesLowEndIndex = 1;

/** The index of ArtFreq 0.1's third trigger, "Free loop" (it played by itself, looping). States
    saved with it load as Transport with Repeat on. */
constexpr int legacyFreeLoopIndex = 2;
inline constexpr const char* legacyFreeLoopName = "Free loop";

/** The drawing's length in quarter notes for a length choice. A bar is beatsPerBar quarter
    notes (4 in 4/4, 3 in 3/4, 3 in 6/8, ...). */
double lengthInQuarterNotes (int lengthIndex, double quarterNotesPerBar = 4.0) noexcept;

float slopeForIndex (int slopeIndex) noexcept;      // dB per octave
ScaleType scaleForIndex (int scaleIndex) noexcept;
TriggerMode triggerForIndex (int triggerIndex) noexcept;
LowEnd lowEndForIndex (int lowEndIndex) noexcept;
EffectMode effectModeForIndex (int modeIndex) noexcept;

} // namespace artfreq::params
