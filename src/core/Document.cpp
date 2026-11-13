#include "Document.h"

#include "Fonts.h"
#include "FrequencyScale.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>

namespace freqart
{

namespace
{

namespace ids
{
    const juce::Identifier document ("FreqArtDocument");
    const juce::Identifier object ("Object");
    const juce::Identifier version ("version");
    const juce::Identifier paint ("paint");
    const juce::Identifier id ("id");
    const juce::Identifier type ("type");
    const juce::Identifier startTime ("startTime");
    const juce::Identifier endTime ("endTime");
    const juce::Identifier lowHz ("lowHz");
    const juce::Identifier highHz ("highHz");
    const juce::Identifier intensity ("intensity");
    const juce::Identifier text ("text");
    const juce::Identifier fontIndex ("fontIndex");
    const juce::Identifier textStyle ("textStyle");
    const juce::Identifier imageMode ("imageMode");
    const juce::Identifier cleanup ("cleanup");
    const juce::Identifier image ("image");
}

constexpr int documentVersion = 1;
constexpr size_t paintSize = static_cast<size_t> (ArtDocument::paintWidth) * static_cast<size_t> (ArtDocument::paintHeight);

// Bounds for loaded placements: generous (objects may hang off the canvas) but finite.
constexpr float minTime = -1.0f, maxTime = 2.0f, minTimeSpan = 0.001f;
constexpr float minHzLimit = 1.0f, maxHzLimit = 100000.0f, minHzRatio = 1.001f;
constexpr int maxObjects = 1000;

/** A finite number from a property, or the fallback if it is missing or not a number. */
float readFloat (const juce::ValueTree& tree, const juce::Identifier& name, float fallback)
{
    const auto& value = tree[name];

    if (value.isVoid() || ! (value.isDouble() || value.isInt() || value.isInt64() || value.isBool() || value.isString()))
        return fallback;

    if (value.isString())
    {
        const auto text = value.toString().trim();

        if (text.isEmpty() || ! text.containsOnly ("0123456789.-+eE"))
            return fallback;
    }

    // Bounded before narrowing, so absurd values clamp later instead of overflowing here.
    const auto number = static_cast<double> (value);
    return std::isfinite (number) ? static_cast<float> (std::clamp (number, -1.0e30, 1.0e30)) : fallback;
}

int readInt (const juce::ValueTree& tree, const juce::Identifier& name, int fallback, int low, int high)
{
    const auto number = readFloat (tree, name, static_cast<float> (fallback));
    return static_cast<int> (std::lround (juce::jlimit (static_cast<float> (low), static_cast<float> (high), number)));
}

juce::String encodePaint (const std::vector<uint8_t>& paint)
{
    juce::MemoryOutputStream compressed;

    {
        juce::GZIPCompressorOutputStream zip (compressed);  // zlib format, default level
        zip.write (paint.data(), paint.size());
        zip.flush();
    }

    return juce::Base64::toBase64 (compressed.getData(), compressed.getDataSize());
}

/** The paint bytes, or an empty vector if the text is not a valid paint layer. */
std::vector<uint8_t> decodePaint (const juce::String& base64)
{
    juce::MemoryOutputStream compressed;

    if (! juce::Base64::convertFromBase64 (compressed, base64) || compressed.getDataSize() == 0)
        return {};

    juce::MemoryInputStream source (compressed.getData(), compressed.getDataSize(), false);
    juce::GZIPDecompressorInputStream unzip (&source, false, juce::GZIPDecompressorInputStream::zlibFormat);

    // One byte more than expected is asked for, so a layer of the wrong size is caught without
    // ever inflating garbage beyond that.
    std::vector<uint8_t> bytes (paintSize + 1);
    size_t total = 0;

    while (total < bytes.size())
    {
        const auto got = unzip.read (bytes.data() + total, static_cast<int> (bytes.size() - total));

        if (got <= 0)
            break;

        total += static_cast<size_t> (got);
    }

    if (total != paintSize)
        return {};

    bytes.resize (paintSize);
    return bytes;
}

juce::String encodePng (const juce::Image& image)
{
    juce::MemoryOutputStream png;
    juce::PNGImageFormat format;

    if (! format.writeImageToStream (image, png))
        return {};

    return juce::Base64::toBase64 (png.getData(), png.getDataSize());
}

/** The PNG + base64 text of every picture still in use: encoding a large photo takes
    ~100-400 ms, and hosts may ask for the state often (some after every edit, for their own
    undo history). Pictures are never modified in place and each entry keeps its picture alive,
    so the picture's pixel data identifies the content. There is no fixed number of entries (a
    limit below the number of pictures in use made every save encode every picture again);
    instead an entry goes once no document, undo step or copy refers to its picture, i.e. once
    the entry holds the only reference. The state can be saved from any thread, hence the lock
    (never taken on the audio thread). */
struct EncodedPictures
{
    struct Entry
    {
        juce::Image image;
        juce::String encoded;
    };

    std::mutex lock;
    std::vector<Entry> entries;

    static EncodedPictures& get()
    {
        static EncodedPictures pictures;
        return pictures;
    }

    // Call these with the lock held.
    void forgetUnused()
    {
        entries.erase (std::remove_if (entries.begin(), entries.end(),
                                       [] (const Entry& entry) { return entry.image.getReferenceCount() <= 1; }),
                       entries.end());
    }

    const Entry* find (const juce::Image& image) const
    {
        for (const auto& entry : entries)
            if (entry.image == image)  // the same pixel data
                return &entry;

        return nullptr;
    }
};

void forgetUnusedEncodings()
{
    auto& pictures = EncodedPictures::get();
    const std::lock_guard<std::mutex> guard (pictures.lock);
    pictures.forgetUnused();
}

void rememberEncoding (const juce::Image& image, const juce::String& encoded)
{
    auto& pictures = EncodedPictures::get();
    const std::lock_guard<std::mutex> guard (pictures.lock);
    pictures.forgetUnused();

    // Another thread may have encoded the same picture meanwhile.
    if (pictures.find (image) == nullptr)
        pictures.entries.push_back ({ image, encoded });
}

juce::String encodeImage (const juce::Image& image)
{
    {
        auto& pictures = EncodedPictures::get();
        const std::lock_guard<std::mutex> guard (pictures.lock);

        if (const auto* entry = pictures.find (image))
            return entry->encoded;
    }

    auto encoded = encodePng (image);

    if (encoded.isNotEmpty())
        rememberEncoding (image, encoded);

    return encoded;
}

juce::Image decodeImage (const juce::String& base64)
{
    juce::MemoryOutputStream data;

    if (base64.isEmpty() || ! juce::Base64::convertFromBase64 (data, base64) || data.getDataSize() == 0)
        return {};

    const auto raw = juce::ImageFileFormat::loadFrom (data.getData(), data.getDataSize());
    auto image = prepareImportedImage (raw);

    // Decoding this text again gives exactly these pixels, so it serves as the picture's
    // encoding and saving a drawing that was just loaded encodes nothing. Not when the text is
    // not a PNG (the saved format) or the picture was shrunk on the way in (the text is then
    // larger than it needs to be).
    juce::MemoryInputStream header (data.getData(), data.getDataSize(), false);

    if (image.isValid() && image.getBounds() == raw.getBounds() && juce::PNGImageFormat().canUnderstand (header))
        rememberEncoding (image, base64);

    return image;
}

/** 0 (text) or 1 (image), from the number or the words "text" / "image"; -1 if neither. */
int readType (const juce::ValueTree& tree)
{
    const auto& value = tree[ids::type];

    if (value.isString())
    {
        const auto word = value.toString().trim().toLowerCase();

        if (word == "text" || word == "0")
            return 0;

        if (word == "image" || word == "1")
            return 1;

        return -1;
    }

    if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
    {
        const auto number = static_cast<double> (value);

        if (std::abs (number) < 0.5)
            return 0;

        if (std::abs (number - 1.0) < 0.5)
            return 1;
    }

    return -1;
}

float smoothstep (float t) noexcept
{
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

// ------------------------------------------------------------------------------------------------

ArtDocument::ArtDocument()
    : paint (paintSize, 0)
{
}

ArtDocument ArtDocument::createDefault()
{
    ArtDocument document;
    const auto band = bandRange (Band::mid, FrequencyScale {});
    auto object = makeTextObject ("FreqArt", static_cast<float> (band.low), static_cast<float> (band.high));
    object.startTime = 0.04f;
    object.endTime = 0.96f;
    document.objects.push_back (std::move (object));
    return document;
}

bool ArtDocument::isEmpty() const noexcept
{
    return objects.empty() && ! hasPaint();
}

bool ArtDocument::hasPaint() const noexcept
{
    // Eight bytes at a time: this runs on every render and every edit.
    const auto* data = paint.data();
    const auto size = paint.size();
    size_t i = 0;

    for (; i + 8 <= size; i += 8)
    {
        uint64_t word = 0;
        std::memcpy (&word, data + i, sizeof (word));

        if (word != 0)
            return true;
    }

    for (; i < size; ++i)
        if (data[i] != 0)
            return true;

    return false;
}

ArtObject* ArtDocument::find (const juce::Uuid& id) noexcept
{
    for (auto& object : objects)
        if (object.id == id)
            return &object;

    return nullptr;
}

const ArtObject* ArtDocument::find (const juce::Uuid& id) const noexcept
{
    for (const auto& object : objects)
        if (object.id == id)
            return &object;

    return nullptr;
}

bool ArtDocument::remove (const juce::Uuid& id)
{
    const auto it = std::find_if (objects.begin(), objects.end(), [&id] (const ArtObject& o) { return o.id == id; });

    if (it == objects.end())
        return false;

    objects.erase (it);
    return true;
}

void ArtDocument::paintStroke (const BrushStroke& stroke)
{
    if (paint.size() != paintSize)
        paint.assign (paintSize, 0);

    const auto finite = [] (juce::Point<float> p) { return std::isfinite (p.x) && std::isfinite (p.y); };

    if (! finite (stroke.from) || ! finite (stroke.to) || ! std::isfinite (stroke.radiusX) || ! std::isfinite (stroke.radiusY))
        return;

    const auto rx = juce::jlimit (0.5f, 1024.0f, stroke.radiusX);
    const auto ry = juce::jlimit (0.5f, 1024.0f, stroke.radiusY);
    const auto softness = std::isfinite (stroke.softness) ? juce::jlimit (0.0f, 1.0f, stroke.softness) : 0.5f;
    const auto strength = std::isfinite (stroke.strength) ? juce::jlimit (0.0f, 1.0f, stroke.strength) : 1.0f;
    const auto core = 1.0f - softness;  // hard core, as a fraction of the radius

    // The strongest profile any dab of this stroke gives each pixel, over the stroke's bounds.
    // Applying it once (rather than dab by dab) keeps an erase stroke's soft edge as soft as
    // its profile however closely the dabs overlap; painting (a max) is the same either way.
    const auto left   = std::max (0, static_cast<int> (std::floor (std::min (stroke.from.x, stroke.to.x) - rx)));
    const auto right  = std::min (paintWidth - 1, static_cast<int> (std::ceil (std::max (stroke.from.x, stroke.to.x) + rx)));
    const auto top    = std::max (0, static_cast<int> (std::floor (std::min (stroke.from.y, stroke.to.y) - ry)));
    const auto bottom = std::min (paintHeight - 1, static_cast<int> (std::ceil (std::max (stroke.from.y, stroke.to.y) + ry)));

    if (left > right || top > bottom)
        return;

    const auto maskWidth = right - left + 1;
    std::vector<float> mask (static_cast<size_t> (maskWidth) * static_cast<size_t> (bottom - top + 1), 0.0f);

    const auto spacing = std::max (1.0f, std::min (rx, ry) / 4.0f);
    const auto length = stroke.from.getDistanceFrom (stroke.to);
    const auto steps = std::max (1, static_cast<int> (std::ceil (length / spacing)));
    const auto numDabs = length > 0.0f ? steps + 1 : 1;

    for (int dab = 0; dab < numDabs; ++dab)
    {
        const auto t = numDabs > 1 ? static_cast<float> (dab) / static_cast<float> (steps) : 0.0f;
        const auto centre = stroke.from + (stroke.to - stroke.from) * t;

        const auto x0 = std::max (left, static_cast<int> (std::floor (centre.x - rx)));
        const auto x1 = std::min (right, static_cast<int> (std::ceil (centre.x + rx)));
        const auto y0 = std::max (top, static_cast<int> (std::floor (centre.y - ry)));
        const auto y1 = std::min (bottom, static_cast<int> (std::ceil (centre.y + ry)));

        for (int y = y0; y <= y1; ++y)
        {
            // Pixel (x, y) covers [x, x + 1) x [y, y + 1): its centre is measured.
            const auto dy = (static_cast<float> (y) + 0.5f - centre.y) / ry;
            auto* row = mask.data() + static_cast<size_t> (y - top) * static_cast<size_t> (maskWidth);

            for (int x = x0; x <= x1; ++x)
            {
                const auto dx = (static_cast<float> (x) + 0.5f - centre.x) / rx;
                const auto distanceSquared = dx * dx + dy * dy;

                if (distanceSquared >= 1.0f)
                    continue;

                float profile = 1.0f;

                if (distanceSquared > core * core)
                    profile = 1.0f - smoothstep ((std::sqrt (distanceSquared) - core) / (1.0f - core));

                auto& m = row[x - left];
                m = std::max (m, profile);
            }
        }
    }

    for (int y = top; y <= bottom; ++y)
    {
        const auto* row = mask.data() + static_cast<size_t> (y - top) * static_cast<size_t> (maskWidth);
        auto* pixels = paint.data() + static_cast<size_t> (y) * static_cast<size_t> (paintWidth);

        for (int x = left; x <= right; ++x)
        {
            const auto profile = row[x - left];

            if (profile <= 0.0f)
                continue;

            auto& pixel = pixels[x];

            if (stroke.erase)
                pixel = static_cast<uint8_t> (juce::roundToInt (static_cast<float> (pixel) * (1.0f - profile)));
            else
                pixel = std::max (pixel, static_cast<uint8_t> (juce::roundToInt (strength * profile * 255.0f)));
        }
    }
}

void ArtDocument::clearPaint()
{
    paint.assign (paintSize, 0);
}

// ------------------------------------------------------------------------------------------------

juce::ValueTree ArtDocument::toValueTree() const
{
    // Lets go of the encodings of pictures that were deleted (with their undo steps) since the
    // last save, even when this drawing has no pictures left to encode.
    forgetUnusedEncodings();

    juce::ValueTree tree (ids::document);
    tree.setProperty (ids::version, documentVersion, nullptr);

    if (paint.size() == paintSize && hasPaint())
        tree.setProperty (ids::paint, encodePaint (paint), nullptr);

    for (const auto& object : objects)
    {
        juce::ValueTree child (ids::object);
        child.setProperty (ids::id, object.id.toString(), nullptr);
        child.setProperty (ids::type, static_cast<int> (object.type), nullptr);
        child.setProperty (ids::startTime, object.startTime, nullptr);
        child.setProperty (ids::endTime, object.endTime, nullptr);
        child.setProperty (ids::lowHz, object.lowHz, nullptr);
        child.setProperty (ids::highHz, object.highHz, nullptr);
        child.setProperty (ids::intensity, object.intensity, nullptr);

        if (object.type == ArtObject::Type::text)
        {
            child.setProperty (ids::text, object.text, nullptr);
            child.setProperty (ids::fontIndex, object.fontIndex, nullptr);
            child.setProperty (ids::textStyle, static_cast<int> (object.textStyle), nullptr);
        }
        else
        {
            child.setProperty (ids::imageMode, static_cast<int> (object.imageMode), nullptr);
            child.setProperty (ids::cleanup, object.cleanup, nullptr);

            if (object.image.isValid())
                child.setProperty (ids::image, encodeImage (object.image), nullptr);
        }

        tree.appendChild (child, nullptr);
    }

    return tree;
}

ArtDocument ArtDocument::fromValueTree (const juce::ValueTree& tree)
{
    ArtDocument document;

    if (! tree.isValid() || ! tree.hasType (ids::document))
        return document;

    if (tree.hasProperty (ids::paint))
    {
        auto bytes = decodePaint (tree[ids::paint].toString());

        if (bytes.size() == paintSize)
            document.paint = std::move (bytes);
    }

    std::set<juce::String> usedIds;

    for (const auto& child : tree)
    {
        if (static_cast<int> (document.objects.size()) >= maxObjects)
            break;

        if (! child.hasType (ids::object))
            continue;

        const auto type = readType (child);

        if (type < 0)
            continue;

        ArtObject object;
        object.type = type == 0 ? ArtObject::Type::text : ArtObject::Type::image;

        // Ids must be unique within a document (selection and editing find objects by id).
        const juce::Uuid parsed (child[ids::id].toString());

        if (! parsed.isNull() && usedIds.count (parsed.toString()) == 0)
            object.id = parsed;

        usedIds.insert (object.id.toString());

        // Time: ordered, finite, at least a sliver long.
        auto start = juce::jlimit (minTime, maxTime, readFloat (child, ids::startTime, object.startTime));
        auto end = juce::jlimit (minTime, maxTime, readFloat (child, ids::endTime, object.endTime));

        if (end < start)
            std::swap (start, end);

        if (end - start < minTimeSpan)
        {
            if (start + minTimeSpan <= maxTime)
                end = start + minTimeSpan;
            else
                start = end - minTimeSpan;
        }

        object.startTime = start;
        object.endTime = end;

        // Frequency: ordered, positive, not collapsed.
        auto low = juce::jlimit (minHzLimit, maxHzLimit, readFloat (child, ids::lowHz, object.lowHz));
        auto high = juce::jlimit (minHzLimit, maxHzLimit, readFloat (child, ids::highHz, object.highHz));

        if (high < low)
            std::swap (low, high);

        if (high < low * minHzRatio)
        {
            if (low * minHzRatio <= maxHzLimit)
                high = low * minHzRatio;
            else
                low = high / minHzRatio;
        }

        object.lowHz = low;
        object.highHz = high;
        object.intensity = juce::jlimit (0.0f, 1.0f, readFloat (child, ids::intensity, 1.0f));

        if (object.type == ArtObject::Type::text)
        {
            object.text = child[ids::text].toString();
            object.fontIndex = readInt (child, ids::fontIndex, 0, 0, fonts::numArtFonts() - 1);
            object.textStyle = static_cast<TextStyle> (readInt (child, ids::textStyle, 0, 0, 1));
        }
        else
        {
            object.image = decodeImage (child[ids::image].toString());

            if (! object.image.isValid())
                continue;  // a picture without its pixels is of no use

            object.imageMode = static_cast<ImageMode> (readInt (child, ids::imageMode, 0, 0, 3));
            object.cleanup = juce::jlimit (0.0f, 0.9f, readFloat (child, ids::cleanup, object.cleanup));
        }

        document.objects.push_back (std::move (object));
    }

    return document;
}

ArtObject ArtDocument::makeTextObject (const juce::String& text, float lowHz, float highHz)
{
    ArtObject object;
    object.type = ArtObject::Type::text;
    object.text = text;
    object.lowHz = std::min (lowHz, highHz);
    object.highHz = std::max (lowHz, highHz);
    object.fontIndex = 0;
    object.textStyle = TextStyle::solid;
    return object;
}

ArtObject ArtDocument::makeImageObject (const juce::Image& preparedImage, float lowHz, float highHz)
{
    ArtObject object;
    object.type = ArtObject::Type::image;
    object.image = preparedImage;
    object.lowHz = std::min (lowHz, highHz);
    object.highHz = std::max (lowHz, highHz);
    object.imageMode = ImageMode::automatic;
    return object;
}

// ------------------------------------------------------------------------------------------------

void DocumentHistory::clear()
{
    undoSteps.clear();
    redoSteps.clear();
}

void DocumentHistory::push (const ArtDocument& before)
{
    redoSteps.clear();

    if (limit <= 0)
        return;

    undoSteps.push_back (before);

    // The oldest steps go first when the limit is reached.
    if (static_cast<int> (undoSteps.size()) > limit)
        undoSteps.erase (undoSteps.begin(), undoSteps.begin() + (static_cast<std::ptrdiff_t> (undoSteps.size()) - limit));
}

bool DocumentHistory::undo (ArtDocument& current)
{
    if (undoSteps.empty())
        return false;

    redoSteps.push_back (std::move (current));
    current = std::move (undoSteps.back());
    undoSteps.pop_back();
    return true;
}

bool DocumentHistory::redo (ArtDocument& current)
{
    if (redoSteps.empty())
        return false;

    undoSteps.push_back (std::move (current));
    current = std::move (redoSteps.back());
    redoSteps.pop_back();
    return true;
}

} // namespace freqart
