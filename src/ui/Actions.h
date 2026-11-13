#pragma once

// Editing actions used from several places (toolbar, inspector, canvas, keyboard).

#include "EditorState.h"
#include "PluginProcessor.h"
#include "core/FrequencyScale.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace freqart::ui::actions
{

// New objects go in front of the others (the end of the document's list) and are selected, so
// the inspector shows their settings straight away.

/** Adds a text object ("Text" placeholder content selected for typing) at newObjectPlacement,
    selects it, switches to the Select tool and asks for the text field focus. */
void addText (FreqArtProcessor& processor, EditorState& state);

/** Opens an async file chooser for a picture (isPictureFile). The chosen file becomes a new
    image object at newObjectPlacement (or replaces the picture of the selected image object if
    `replaceSelected`), which is then selected; a file that cannot be read is reported with
    state.showNotice. The chooser is owned by `owner` and the callback is ignored if `owner`
    has been deleted. */
void chooseImage (FreqArtProcessor& processor, EditorState& state, juce::Component& owner, bool replaceSelected);

/** Adds an image file as an object centred on `normPosition` (x = time 0..1, y = 0 top..1
    bottom): the band containing that height, a time span of 40 % of the length (clamped to the
    canvas). Returns false if the file is not a readable image. */
bool addImageFile (FreqArtProcessor& processor, EditorState& state, const juce::File& file,
                   juce::Point<float> normPosition);

/** Where a new object goes (startTime, endTime, lowHz, highHz in an ArtObject): the first of
    the mid, high and low bands across the length (4 % to 96 %), then the halves of each, that
    no object overlaps yet; if every spot is taken, a band across the length with its start
    moved along until no object in that band starts there. Always inside the canvas. */
ArtObject newObjectPlacement (const ArtDocument& document, const FrequencyScale& scale);

/** Whether a file looks like a picture FreqArt can open (by its extension): PNG, JPEG or GIF,
    and on a Mac also what macOS reads itself (HEIC, WebP, TIFF, BMP). */
bool isPictureFile (const juce::File& file);

/** The same formats for people: "PNG, JPEG or GIF" (more on a Mac). */
juce::String pictureFormatNames();

/** Reads a picture file, ready for the document (an invalid image if it cannot be read). */
juce::Image readPicture (const juce::File& file);

/** Tells the user (state.showNotice) that a picture file could not be read. */
void reportUnreadablePicture (EditorState& state, const juce::File& file);

/** Removes the selected object (no-op without a selection). */
void deleteSelected (FreqArtProcessor& processor, EditorState& state);

// ---- layers ----------------------------------------------------------------------------------
// Each of these is one undo step (and does nothing if the object is gone or nothing changes).

/** What the layer list calls an object: a text's first line, or the picture's file name
    ("Picture" if it has none, "Empty text" for text with no words). */
juce::String layerName (const ArtObject& object);

/** Shows or hides an object. A hidden object stays in the drawing, silent and not drawn. */
void setObjectVisible (FreqArtProcessor& processor, const juce::Uuid& id, bool visible);

/** Shows or hides the paint layer. */
void setPaintVisible (FreqArtProcessor& processor, bool visible);

/** Moves an object to `newIndex` in the document's order (0 = the back, the last = the front),
    which decides which of two overlapping objects the canvas picks. */
void moveObject (FreqArtProcessor& processor, const juce::Uuid& id, int newIndex);
void bringToFront (FreqArtProcessor& processor, const juce::Uuid& id);
void sendToBack (FreqArtProcessor& processor, const juce::Uuid& id);

/** Removes an object; if it was selected, nothing is selected afterwards. */
void deleteObject (FreqArtProcessor& processor, EditorState& state, const juce::Uuid& id);

/** Empties the paint layer (text and pictures stay). */
void clearPaint (FreqArtProcessor& processor);

/** The menu an object's right-click opens (on the canvas and in the layer list): Bring to
    front, Send to back, Hide (Show for a hidden object), Delete. Items that would change
    nothing are greyed out. performObjectMenuItem carries out the item with that id. */
juce::PopupMenu objectMenu (const FreqArtProcessor& processor, const juce::Uuid& id);
void performObjectMenuItem (FreqArtProcessor& processor, EditorState& state, const juce::Uuid& id, int itemId);

/** Opens objectMenu at `position` (in `target`'s coordinates), inside the editor's window. The
    chosen item is ignored if `target` has been deleted meanwhile. */
void showObjectMenu (FreqArtProcessor& processor, EditorState& state, const juce::Uuid& id,
                     juce::Component& target, juce::Point<int> position);

namespace menuItems
{
    enum
    {
        bringToFront = 1,
        sendToBack,
        hide,
        show,
        remove
    };
} // namespace menuItems

/** Moves the selected object to a band (keeps its time span). */
void setSelectedBand (FreqArtProcessor& processor, EditorState& state, Band band);

/** Which band the object sits on exactly (within 1 %), or -1. */
int bandOf (const ArtObject& object, const FrequencyScale& scale);

/** Clears the paint layer and removes every object (one undo step). */
void clearDrawing (FreqArtProcessor& processor, EditorState& state);

} // namespace freqart::ui::actions
