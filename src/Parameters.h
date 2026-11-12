#pragma once

// Parameter ids, ranges and defaults, shared by the processor, the editor and the tests.

#include "core/FrequencyScale.h"
#include "dsp/PlayheadTracker.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace freqart::params
{

namespace id
{
    // Sound
    inline constexpr const char* level       = "level";        // dB, -60..0, default -6
    inline constexpr const char* density     = "density";      // lines, 16..1024 (int), default 256
    inline constexpr const char* contrast    = "contrast";     // dB, 12..72, default 40

    // Timing
    inline constexpr const char* length      = "length";       // choice, see lengthNames(), default "4 bars"
    inline constexpr const char* trigger     = "trigger";      // choice: Transport, MIDI note, Free loop
    inline constexpr const char* tempoSource = "tempoSource";  // choice: Host, Manual
    inline constexpr const char* bpm         = "bpm";          // 20..300, default 120

    // Analyser match (not automatable): how the user's spectrogram draws frequency
    inline constexpr const char* scale       = "scale";        // choice: Log, Linear, Mel
    inline constexpr const char* rangeLow    = "rangeLow";     // Hz, 10..1000, default 20
    inline constexpr const char* rangeHigh   = "rangeHigh";    // Hz, 2000..24000, default 20000
    inline constexpr const char* slope       = "slope";        // choice: 0, 3, 4.5, 6 dB/oct
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Choices, in parameter order.
juce::StringArray lengthNames();       // "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars"
juce::StringArray triggerNames();      // "Transport", "MIDI note", "Free loop"
juce::StringArray tempoSourceNames();  // "Host", "Manual"
juce::StringArray scaleNames();        // "Log", "Linear", "Mel"
juce::StringArray slopeNames();        // "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct"

constexpr int defaultLengthIndex = 4;  // "4 bars"

/** The drawing's length in quarter notes for a length choice. A bar is beatsPerBar quarter
    notes (4 in 4/4, 3 in 3/4, 3 in 6/8, ...). */
double lengthInQuarterNotes (int lengthIndex, double quarterNotesPerBar = 4.0) noexcept;

float slopeForIndex (int slopeIndex) noexcept;      // dB per octave
ScaleType scaleForIndex (int scaleIndex) noexcept;
TriggerMode triggerForIndex (int triggerIndex) noexcept;

} // namespace freqart::params
