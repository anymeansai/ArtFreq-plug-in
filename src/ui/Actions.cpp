#include "Actions.h"

#include "core/Compositor.h"

#include <array>
#include <cmath>
#include <limits>

namespace freqart::ui::actions
{
namespace
{
    constexpr float defaultStart = 0.04f;     // new objects span the length, less a small margin
    constexpr float defaultEnd = 0.96f;
    constexpr float droppedSpan = 0.4f;       // a dropped picture covers 40 % of the length
    constexpr double bandTolerance = 0.01;    // bandOf: 1 % of the canvas height

    const juce::Identifier chooserProperty { "freqartImageChooser" };

    /** Keeps an async FileChooser alive in its owner's properties, so it lives exactly as long
        as the component that opened it (and a second "Add image..." replaces the first). */
    struct ChooserHolder final : public juce::ReferenceCountedObject
    {
        std::unique_ptr<juce::FileChooser> chooser;
    };

    float clamp01 (float x) noexcept { return juce::jlimit (0.0f, 1.0f, x); }

    /** Adds `object` to the document as one undo step, selects it and switches to the Select
        tool so it can be moved straight away. */
    void addAndSelect (FreqArtProcessor& processor, EditorState& state, const ArtObject& object)
    {
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
} // namespace

//==============================================================================
void addText (FreqArtProcessor& processor, EditorState& state)
{
    const auto band = bandRange (Band::mid, processor.getScale());
    auto object = ArtDocument::makeTextObject ("Text", static_cast<float> (band.low), static_cast<float> (band.high));
    object.startTime = defaultStart;
    object.endTime = defaultEnd;

    addAndSelect (processor, state, object);

    if (state.focusTextField != nullptr)
        state.focusTextField();
}

void chooseImage (FreqArtProcessor& processor, EditorState& state, juce::Component& owner, bool replaceSelected)
{
    juce::ReferenceCountedObjectPtr<ChooserHolder> holder (new ChooserHolder());
    holder->chooser = std::make_unique<juce::FileChooser> (replaceSelected ? "Replace the picture" : "Add a picture",
                                                           juce::File(), "*.png;*.jpg;*.jpeg;*.gif");

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

        const auto image = prepareImportedImage (loadImageFile (file));

        if (! image.isValid())
            return;

        if (replaceSelected)
        {
            const auto id = state.getSelected();
            const auto* selected = processor.getDocument().find (id);

            if (selected != nullptr && selected->type == ArtObject::Type::image)
            {
                processor.editDocument ([id, image] (ArtDocument& document)
                {
                    if (auto* object = document.find (id))
                        object->image = image;
                });
                return;
            }
        }

        const auto band = bandRange (Band::mid, processor.getScale());
        auto object = ArtDocument::makeImageObject (image, static_cast<float> (band.low), static_cast<float> (band.high));
        object.startTime = defaultStart;
        object.endTime = defaultEnd;
        addAndSelect (processor, state, object);
    });
}

bool addImageFile (FreqArtProcessor& processor, EditorState& state, const juce::File& file,
                   juce::Point<float> normPosition)
{
    const auto image = prepareImportedImage (loadImageFile (file));

    if (! image.isValid())
        return false;

    const auto scale = processor.getScale();
    const auto x = std::isfinite (normPosition.x) ? clamp01 (normPosition.x) : 0.5f;
    const auto y = std::isfinite (normPosition.y) ? clamp01 (normPosition.y) : 0.5f;
    const auto band = bandRange (bandAtHeight (1.0 - static_cast<double> (y), scale), scale);

    auto object = ArtDocument::makeImageObject (image, static_cast<float> (band.low), static_cast<float> (band.high));
    object.startTime = juce::jlimit (0.0f, 1.0f - droppedSpan, x - droppedSpan * 0.5f);
    object.endTime = object.startTime + droppedSpan;

    addAndSelect (processor, state, object);
    return true;
}

void deleteSelected (FreqArtProcessor& processor, EditorState& state)
{
    const auto id = state.getSelected();

    if (id.isNull())
        return;

    if (processor.getDocument().find (id) != nullptr)
        processor.editDocument ([id] (ArtDocument& document) { document.remove (id); });

    state.setSelected (juce::Uuid::null());
}

void setSelectedBand (FreqArtProcessor& processor, EditorState& state, Band band)
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

void clearDrawing (FreqArtProcessor& processor, EditorState& state)
{
    processor.editDocument ([] (ArtDocument& document)
    {
        document.clearPaint();
        document.objects.clear();
    });

    state.setSelected (juce::Uuid::null());
}

} // namespace freqart::ui::actions
