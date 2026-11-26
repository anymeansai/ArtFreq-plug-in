#pragma once

#include "core/Compositor.h"
#include "core/ImageProcessing.h"

#include <cstring>

// The drawing the "rotation 0 renders as before" test hashes (shared with a probe built at the
// commit before rotation, which recorded the hash).
inline juce::Image referencePicture()
{
    juce::Image image (juce::Image::ARGB, 120, 80, true);

    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto dx = x - 70, dy = y - 40;
            const auto inside = dx * dx + dy * dy < 30 * 30;
            const auto level = static_cast<juce::uint8> (inside ? 255 : (x * 2 + y) % 256);
            image.setPixelAt (x, y, juce::Colour (level, static_cast<juce::uint8> (255 - level), static_cast<juce::uint8> ((x * y) % 256)));
        }

    return artfreq::prepareImportedImage (image);
}

inline artfreq::ArtDocument referenceDrawing()
{
    using namespace artfreq;
    auto document = ArtDocument::createDefault();

    auto outline = ArtDocument::makeTextObject ("Hi there", 4000.0f, 12000.0f);
    outline.startTime = 0.1f;
    outline.endTime = 0.5f;
    outline.fontIndex = 1;
    outline.textStyle = TextStyle::outline;
    document.objects.push_back (outline);

    auto picture = ArtDocument::makeImageObject (referencePicture(), 100.0f, 900.0f);
    picture.startTime = 0.55f;
    picture.endTime = 0.98f;
    picture.imageMode = ImageMode::original;
    picture.intensity = 0.8f;
    document.objects.push_back (picture);

    auto offCanvas = ArtDocument::makeImageObject (referencePicture(), 50.0f, 300.0f);
    offCanvas.startTime = -0.2f;
    offCanvas.endTime = 0.3f;
    document.objects.push_back (offCanvas);

    auto edge = ArtDocument::makeTextObject ("Edge", 10000.0f, 30000.0f);
    edge.startTime = 0.8f;
    edge.endTime = 1.3f;
    document.objects.push_back (edge);
    return document;
}

inline juce::uint64 hashOf (const artfreq::Canvas& canvas)
{
    juce::uint64 hash = 14695981039346656037ull;

    for (const auto value : canvas.values)
    {
        juce::uint32 bits = 0;
        std::memcpy (&bits, &value, sizeof (bits));

        for (int i = 0; i < 4; ++i)
        {
            hash ^= (bits >> (8 * i)) & 0xffu;
            hash *= 1099511628211ull;
        }
    }

    return hash;
}
