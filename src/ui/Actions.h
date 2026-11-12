#pragma once

// Editing actions used from several places (toolbar, inspector, canvas, keyboard).

#include "EditorState.h"
#include "PluginProcessor.h"
#include "core/FrequencyScale.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace freqart::ui::actions
{

/** Adds a text object ("Text" placeholder content selected for typing) in the mid band across
    the length, selects it, switches to the Select tool and asks for the text field focus. */
void addText (FreqArtProcessor& processor, EditorState& state);

/** Opens an async file chooser for an image (png, jpg, jpeg, gif). The chosen file becomes a
    new image object in the mid band across the length (or replaces the picture of the
    selected image object if `replaceSelected`), which is then selected. The chooser is owned by
    `owner` and the callback is ignored if `owner` has been deleted. */
void chooseImage (FreqArtProcessor& processor, EditorState& state, juce::Component& owner, bool replaceSelected);

/** Adds an image file as an object centred on `normPosition` (x = time 0..1, y = 0 top..1
    bottom): the band containing that height, a time span of 40 % of the length (clamped to the
    canvas). Returns false if the file is not a readable image. */
bool addImageFile (FreqArtProcessor& processor, EditorState& state, const juce::File& file,
                   juce::Point<float> normPosition);

/** Removes the selected object (no-op without a selection). */
void deleteSelected (FreqArtProcessor& processor, EditorState& state);

/** Moves the selected object to a band (keeps its time span). */
void setSelectedBand (FreqArtProcessor& processor, EditorState& state, Band band);

/** Which band the object sits on exactly (within 1 %), or -1. */
int bandOf (const ArtObject& object, const FrequencyScale& scale);

/** Clears the paint layer and removes every object (one undo step). */
void clearDrawing (FreqArtProcessor& processor, EditorState& state);

} // namespace freqart::ui::actions
