#pragma once

// Renders one pass of the drawing to a WAV file, for dragging into a DAW or saving.

#include "core/ArtPlan.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace freqart
{

struct ExportSettings
{
    double sampleRate = 48000.0;
    double seconds = 8.0;        // one pass of the drawing
    float gain = 0.5f;           // the user's output level, linear
    int bitDepth = 24;
    int numChannels = 2;         // the same signal on each channel
};

/** Renders the plan from position 0 to 1 over `seconds` with ArtEngine (gate on from the first
    sample, so the first 5 ms fade in, and a 5 ms fade-out at the end), applies `gain` and the
    same safety clipper as the plug-in (softClip), and writes a WAV file. Overwrites `target`.
    Returns false and fills `error` on failure. Safe to call from a background thread. */
bool renderPlanToWav (const ArtPlan& plan, const ExportSettings& settings,
                      const juce::File& target, juce::String& error);

/** The plug-in's safety clipper: transparent below 0.89 (-1 dBFS), then a smooth curve that
    never exceeds 1.0 in magnitude. */
float softClip (float x) noexcept;

} // namespace freqart
