#include "Actions.h"

#include "LookAndFeel.h"
#include "core/Compositor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

// On a Mac, JUCE decodes pictures with macOS itself (ImageIO, through CoreImage), which reads
// more than PNG, JPEG and GIF; elsewhere JUCE's own decoders read only those three.
#if JUCE_MAC && USE_COREGRAPHICS_RENDERING && JUCE_USE_COREIMAGE_LOADER
 #define ARTFREQ_READS_PICTURES_WITH_MACOS 1
#else
 #define ARTFREQ_READS_PICTURES_WITH_MACOS 0
#endif

namespace artfreq::ui::actions
{
namespace
{
    constexpr float defaultStart = 0.04f;     // new objects span the length, less a small margin
    constexpr float defaultEnd = 0.96f;
    constexpr float droppedSpan = 0.4f;       // a dropped picture covers 40 % of the length
    constexpr double bandTolerance = 0.01;    // bandOf: 1 % of the canvas height
    constexpr float placementStep = 0.04f;    // newObjectPlacement: how far each fallback start moves
    constexpr int maxPlacementSteps = 11;     // ... up to 48 % of the length
    constexpr float overlapTolerance = 1.0e-4f;   // rectangles that only touch do not overlap

   #if ARTFREQ_READS_PICTURES_WITH_MACOS
    constexpr const char* pictureExtensions = "png;jpg;jpeg;gif;heic;heif;webp;tif;tiff;bmp";
    constexpr const char* pictureNames = "PNG, JPEG, GIF, HEIC, WebP, TIFF or BMP";
   #else
    constexpr const char* pictureExtensions = "png;jpg;jpeg;gif";
    constexpr const char* pictureNames = "PNG, JPEG or GIF";
   #endif

    const juce::Identifier chooserProperty { "artfreqImageChooser" };

    /** Keeps an async FileChooser alive in its owner's properties, so it lives exactly as long
        as the component that opened it (and a second "Add image..." replaces the first). */
    struct ChooserHolder final : public juce::ReferenceCountedObject
    {
        std::unique_ptr<juce::FileChooser> chooser;
    };

    float clamp01 (float x) noexcept { return juce::jlimit (0.0f, 1.0f, x); }

    /** Adds `object` to the document as one undo step, selects it and switches to the Select
        tool so it can be moved straight away. */
    void addAndSelect (ArtFreqProcessor& processor, EditorState& state, const ArtObject& object)
    {
        // At the end of the list: in front of everything already there.
        processor.editDocument ([object] (ArtDocument& document) { document.objects.push_back (object); });
        state.setTool (Tool::select);
        state.setSelected (object.id);
    }

    /** The band whose range contains `hz` on the scale, else the nearest one (by height). Full
        is never picked: a position always means one of the three bands. */
    Band bandAtHeight (double norm, const FrequencyScale& scale)
    {
        const auto s = scale.sanitised();
        auto best = Band::mid;
        auto bestDistance = std::numeric_limits<double>::max();

        for (auto band : { Band::low, Band::mid, Band::high })
        {
            const auto range = bandRange (band, s);
            const auto bottom = s.toNorm (range.low);
            const auto top = s.toNorm (range.high);
            const auto distance = norm < bottom ? bottom - norm : norm > top ? norm - top : 0.0;

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = band;
            }
        }

        return best;
    }

    /** Gives `object` the place newObjectPlacement found. */
    void placeNewObject (ArtObject& object, const ArtFreqProcessor& processor)
    {
        const auto place = newObjectPlacement (processor.getDocument(), processor.getScale());
        object.startTime = place.startTime;
        object.endTime = place.endTime;
        object.lowHz = place.lowHz;
        object.highHz = place.highHz;
    }
} // namespace

//==============================================================================
ArtObject newObjectPlacement (const ArtDocument& document, const FrequencyScale& scale)
{
    const auto spot = [&scale] (Band band, float start, float end)
    {
        const auto range = bandRange (band, scale);
        ArtObject place;
        place.startTime = start;
        place.endTime = end;
        place.lowHz = static_cast<float> (range.low);
        place.highHz = static_cast<float> (range.high);
        return place;
    };

    const auto isFree = [&document, &scale] (const ArtObject& place)
    {
        const auto r = objectRectNorm (place, scale);

        for (const auto& object : document.objects)
        {
            const auto overlap = r.getIntersection (objectRectNorm (object, scale));

            if (overlap.getWidth() > overlapTolerance && overlap.getHeight() > overlapTolerance)
                return false;
        }

        return true;
    };

    // Across the length first (the mid band, where the default text sits, then the high band,
    // which shows the sharpest detail), then half the length.
    const auto middle = (defaultStart + defaultEnd) * 0.5f;
    const std::array<Band, 3> bands { Band::mid, Band::high, Band::low };

    for (const auto band : bands)
        if (const auto place = spot (band, defaultStart, defaultEnd); isFree (place))
            return place;

    for (const auto band : bands)
        for (const auto& [start, end] : { std::pair (defaultStart, middle - 0.02f), std::pair (middle + 0.02f, defaultEnd) })
            if (const auto place = spot (band, start, end); isFree (place))
                return place;

    // Everything is taken: never exactly on top of another object, so each can still be told
    // apart and clicked (its left edge shows). The start moves along, up to the middle.
    for (const auto band : bands)
    {
        for (int step = 1; step <= maxPlacementSteps; ++step)
        {
            const auto place = spot (band, defaultStart + placementStep * static_cast<float> (step), defaultEnd);
            const auto taken = std::any_of (document.objects.begin(), document.objects.end(), [&place] (const ArtObject& object)
            {
                return std::abs (object.startTime - place.startTime) < placementStep * 0.5f
                    && juce::approximatelyEqual (object.lowHz, place.lowHz)
                    && juce::approximatelyEqual (object.highHz, place.highHz);
            });

            if (! taken)
                return place;
        }
    }

    return spot (Band::mid, defaultStart, defaultEnd);
}

void addText (ArtFreqProcessor& processor, EditorState& state)
{
    auto object = ArtDocument::makeTextObject ("Text", 0.0f, 1.0f);
    placeNewObject (object, processor);

    addAndSelect (processor, state, object);

    if (state.focusTextField != nullptr)
        state.focusTextField();
}

void chooseImage (ArtFreqProcessor& processor, EditorState& state, juce::Component& owner, bool replaceSelected)
{
    juce::ReferenceCountedObjectPtr<ChooserHolder> holder (new ChooserHolder());
    const auto patterns = "*." + juce::String (pictureExtensions).replace (";", ";*.");
    holder->chooser = std::make_unique<juce::FileChooser> (replaceSelected ? "Replace the picture" : "Add a picture",
                                                           juce::File(), patterns);

    // Replacing the property deletes a chooser opened earlier from the same component.
    owner.getProperties().set (chooserProperty, juce::var (holder.get()));

    juce::Component::SafePointer<juce::Component> safeOwner (&owner);
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    holder->chooser->launchAsync (flags, [safeOwner, &processor, &state, replaceSelected] (const juce::FileChooser& chooser)
    {
        // The processor and editor state outlive the owner; if it has gone, so has the editor.
        if (safeOwner == nullptr)
            return;

        const auto file = chooser.getResult();

        if (file == juce::File())
            return;

        const auto image = readPicture (file);

        if (! image.isValid())
        {
            reportUnreadablePicture (state, file);
            return;
        }

        if (replaceSelected)
        {
            const auto id = state.getSelected();
            const auto* selected = processor.getDocument().find (id);

            if (selected != nullptr && selected->type == ArtObject::Type::image)
            {
                processor.editDocument ([id, image, name = file.getFileName()] (ArtDocument& document)
                {
                    if (auto* object = document.find (id))
                    {
                        object->image = image;
                        object->fileName = name;
                    }
                });
                return;
            }
        }

        auto object = ArtDocument::makeImageObject (image, 0.0f, 1.0f);
        object.fileName = file.getFileName();
        placeNewObject (object, processor);
        addAndSelect (processor, state, object);
    });
}

bool addImageFile (ArtFreqProcessor& processor, EditorState& state, const juce::File& file,
                   juce::Point<float> normPosition)
{
    const auto image = readPicture (file);

    if (! image.isValid())
        return false;

    const auto scale = processor.getScale();
    const auto x = std::isfinite (normPosition.x) ? clamp01 (normPosition.x) : 0.5f;
    const auto y = std::isfinite (normPosition.y) ? clamp01 (normPosition.y) : 0.5f;
    const auto band = bandRange (bandAtHeight (1.0 - static_cast<double> (y), scale), scale);

    auto object = ArtDocument::makeImageObject (image, static_cast<float> (band.low), static_cast<float> (band.high));
    object.fileName = file.getFileName();
    object.startTime = juce::jlimit (0.0f, 1.0f - droppedSpan, x - droppedSpan * 0.5f);
    object.endTime = object.startTime + droppedSpan;

    addAndSelect (processor, state, object);
    return true;
}

bool isPictureFile (const juce::File& file)
{
    return file.hasFileExtension (pictureExtensions);
}

juce::String pictureFormatNames()
{
    return pictureNames;
}

juce::Image readPicture (const juce::File& file)
{
    auto image = loadImageFile (file);

   #if ARTFREQ_READS_PICTURES_WITH_MACOS
    // JUCE passes a file to macOS's decoder only once its first bytes say PNG, JPEG or GIF.
    // macOS reads the rest too (HEIC, WebP, TIFF, BMP, or a picture saved under the wrong
    // extension), and on a Mac every JUCE decoder is that same decoder, so hand it over directly.
    if (! image.isValid() && file.existsAsFile())
    {
        juce::FileInputStream stream (file);

        if (stream.openedOk())
            image = juce::PNGImageFormat().decodeImage (stream);
    }
   #endif

    return prepareImportedImage (image);
}

void reportUnreadablePicture (EditorState& state, const juce::File& file)
{
    if (state.showNotice != nullptr)
        state.showNotice ("Could not read " + file.getFileName() + ". Try a PNG or JPEG.");
}

void deleteSelected (ArtFreqProcessor& processor, EditorState& state)
{
    const auto id = state.getSelected();

    if (id.isNull())
        return;

    if (processor.getDocument().find (id) != nullptr)
        processor.editDocument ([id] (ArtDocument& document) { document.remove (id); });

    state.setSelected (juce::Uuid::null());
}

//==============================================================================
juce::String layerName (const ArtObject& object)
{
    if (object.type == ArtObject::Type::image)
        return object.fileName.isNotEmpty() ? object.fileName : juce::String ("Picture");

    // The first line with any words on it.
    for (const auto& line : juce::StringArray::fromLines (object.text))
        if (line.trim().isNotEmpty())
            return line.trim();

    return "Empty text";
}

void setObjectVisible (ArtFreqProcessor& processor, const juce::Uuid& id, bool visible)
{
    const auto* object = processor.getDocument().find (id);

    if (object == nullptr || object->visible == visible)
        return;

    processor.editDocument ([id, visible] (ArtDocument& document)
    {
        if (auto* o = document.find (id))
            o->visible = visible;
    });
}

void setPaintVisible (ArtFreqProcessor& processor, bool visible)
{
    if (processor.getDocument().paintVisible != visible)
        processor.editDocument ([visible] (ArtDocument& document) { document.paintVisible = visible; });
}

void moveObject (ArtFreqProcessor& processor, const juce::Uuid& id, int newIndex)
{
    const auto& objects = processor.getDocument().objects;
    const auto it = std::find_if (objects.begin(), objects.end(), [&id] (const ArtObject& o) { return o.id == id; });

    if (it == objects.end())
        return;

    const auto from = static_cast<int> (std::distance (objects.begin(), it));
    const auto to = juce::jlimit (0, static_cast<int> (objects.size()) - 1, newIndex);

    if (from == to)
        return;

    processor.editDocument ([from, to] (ArtDocument& document)
    {
        auto& list = document.objects;

        // A rotation keeps every other object in its order.
        if (from < to)
            std::rotate (list.begin() + from, list.begin() + from + 1, list.begin() + to + 1);
        else
            std::rotate (list.begin() + to, list.begin() + from, list.begin() + from + 1);
    });
}

void bringToFront (ArtFreqProcessor& processor, const juce::Uuid& id)
{
    moveObject (processor, id, static_cast<int> (processor.getDocument().objects.size()) - 1);
}

void sendToBack (ArtFreqProcessor& processor, const juce::Uuid& id)
{
    moveObject (processor, id, 0);
}

void deleteObject (ArtFreqProcessor& processor, EditorState& state, const juce::Uuid& id)
{
    if (processor.getDocument().find (id) != nullptr)
        processor.editDocument ([id] (ArtDocument& document) { document.remove (id); });

    if (state.getSelected() == id)
        state.setSelected (juce::Uuid::null());
}

void clearPaint (ArtFreqProcessor& processor)
{
    if (processor.getDocument().hasPaint())
        processor.editDocument ([] (ArtDocument& document) { document.clearPaint(); });
}

juce::PopupMenu objectMenu (const ArtFreqProcessor& processor, const juce::Uuid& id)
{
    juce::PopupMenu menu;
    const auto& objects = processor.getDocument().objects;
    const auto* object = processor.getDocument().find (id);

    if (object == nullptr)
        return menu;

    const auto inFront = objects.back().id == id;
    const auto atBack = objects.front().id == id;

    // An item that would change nothing is greyed out; in ArtFreq FX, whose window greys
    // nothing, it is left out (with one object the menu is Hide and Delete).
    const auto leaveOut = processor.isEffect();

    if (! (leaveOut && inFront))
        menu.addItem (menuItems::bringToFront, "Bring to front", ! inFront);

    if (! (leaveOut && atBack))
        menu.addItem (menuItems::sendToBack, "Send to back", ! atBack);

    if (menu.getNumItems() > 0)
        menu.addSeparator();

    if (object->visible)
        menu.addItem (menuItems::hide, "Hide");
    else
        menu.addItem (menuItems::show, "Show");

    menu.addItem (menuItems::remove, "Delete");
    return menu;
}

void performObjectMenuItem (ArtFreqProcessor& processor, EditorState& state, const juce::Uuid& id, int itemId)
{
    switch (itemId)
    {
        case menuItems::bringToFront: bringToFront (processor, id); break;
        case menuItems::sendToBack:   sendToBack (processor, id); break;
        case menuItems::hide:         setObjectVisible (processor, id, false); break;
        case menuItems::show:         setObjectVisible (processor, id, true); break;
        case menuItems::remove:       deleteObject (processor, state, id); break;
        default: break;
    }
}

void showObjectMenu (ArtFreqProcessor& processor, EditorState& state, const juce::Uuid& id,
                     juce::Component& target, juce::Point<int> position)
{
    auto menu = objectMenu (processor, id);

    if (menu.getNumItems() == 0)
        return;

    // A one-pixel target at the click: the menu opens beside the mouse, as a context menu does.
    const auto at = target.localPointToGlobal (position);
    auto options = juce::PopupMenu::Options()
                       .withTargetScreenArea ({ at.x, at.y, 1, 1 })
                       .withStandardItemHeight (28);

    // Inside the window: scaled like the rest, never a separate desktop window.
    if (auto* parent = ArtFreqLookAndFeel::findPopupParent (target))
        options = options.withParentComponent (parent);

    // The window's look, from the start: a menu looks its look-and-feel up before it joins its
    // parent, and would otherwise be opaque, with square corners left unpainted.
    menu.setLookAndFeel (&target.getLookAndFeel());

    // The processor and editor state outlive the target; if it has gone, so has the editor.
    juce::Component::SafePointer<juce::Component> safeTarget (&target);

    menu.showMenuAsync (options, [safeTarget, &processor, &state, id] (int result)
    {
        if (safeTarget != nullptr && result != 0)
            performObjectMenuItem (processor, state, id, result);
    });
}

void setSelectedBand (ArtFreqProcessor& processor, EditorState& state, Band band)
{
    const auto id = state.getSelected();

    if (id.isNull() || processor.getDocument().find (id) == nullptr)
        return;

    const auto range = bandRange (band, processor.getScale());

    processor.editDocument ([id, range] (ArtDocument& document)
    {
        if (auto* object = document.find (id))
        {
            object->lowHz = static_cast<float> (range.low);
            object->highHz = static_cast<float> (range.high);
        }
    });
}

int bandOf (const ArtObject& object, const FrequencyScale& scale)
{
    // Compared as heights on the canvas, so "exactly on the band" means the same thing on
    // every scale (1 % of the height is a few pixels).
    const auto s = scale.sanitised();
    const auto low = s.toNorm (static_cast<double> (object.lowHz));
    const auto high = s.toNorm (static_cast<double> (object.highHz));

    for (auto band : { Band::low, Band::mid, Band::high, Band::full })
    {
        const auto range = bandRange (band, s);

        if (std::abs (low - s.toNorm (range.low)) <= bandTolerance
            && std::abs (high - s.toNorm (range.high)) <= bandTolerance)
            return static_cast<int> (band);
    }

    return -1;
}

void clearDrawing (ArtFreqProcessor& processor, EditorState& state)
{
    processor.editDocument ([] (ArtDocument& document)
    {
        document.clearPaint();
        document.objects.clear();
    });

    state.setSelected (juce::Uuid::null());
}

} // namespace artfreq::ui::actions
