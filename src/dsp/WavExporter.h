#pragma once

// Renders one pass of the drawing to a WAV file, for dragging into a DAW or saving, and names
// the files the editor's bounces go to.

#include "core/ArtPlan.h"
#include "core/Document.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

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
    Returns false and fills `error` on failure. Safe to call from a background thread.

    `shouldCancel`, if given, is asked once per block; when it returns true the render stops,
    returns false (error "Cancelled") and leaves `target` as it was. `onProgress`, if given, is
    told the fraction written (0..1] after each block. */
bool renderPlanToWav (const ArtPlan& plan, const ExportSettings& settings,
                      const juce::File& target, juce::String& error,
                      const std::function<bool()>& shouldCancel = nullptr,
                      const std::function<void (double)>& onProgress = nullptr);

/** The plug-in's safety clipper: transparent below 0.89 (-1 dBFS), then a smooth curve that
    never exceeds 1.0 in magnitude. */
float softClip (float x) noexcept;

//==============================================================================
// Bounce file names. DAWs know dropped audio by its path and name (Logic, Live and FL Studio
// keep using what they read the first time), so every bounce gets a name of its own, one a
// person can also read in the DAW's clip and file lists:
//
//     FreqArt HELLO 14-03-27.wav      the first text of the drawing, the time of the bounce
//     FreqArt Drawing 14-03-27 2.wav  no text; a second bounce within the same second

/** `text` reduced to what every file system and DAW accepts in a file name: ASCII letters and
    digits, Latin letters with accents, spaces and - _ ( ) ' & + ! (anything else becomes a
    space), runs of spaces collapsed, trimmed, and at most `maxLength` characters (cut after
    a whole word where there is one). May be empty. */
juce::String fileNamePart (const juce::String& text, int maxLength);

/** What a bounce of `document` is called: its first text object's text that leaves something
    after fileNamePart (up to 20 characters), else "Drawing". */
juce::String bounceLabel (const ArtDocument& document);

/** Creates a new, empty file in `folder` (made if missing) named "FreqArt <label> <HH-MM-SS>.wav"
    from the local time `when` ("<label> <HH-MM-SS>.wav" if the label already starts with
    "FreqArt"), with " 2", " 3" ... before ".wav" while the name is taken. Creating it reserves
    the name, so two bounces never get the same file, even from two instances in the same
    second. Returns File() if the folder cannot be written. */
juce::File reserveBounceFile (const juce::File& folder, const juce::String& label, juce::Time when);

} // namespace freqart
