#pragma once

// A dense project, for what the fast preview costs on the message thread (the tests and the truth
// lab's --bench): nine words across the three bands, a picture and a brush stroke of 48 dabs, at
// Density 1024. First used by a review that drove the editor like a user.

#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/FrequencyScale.h"
#include "core/ImageProcessing.h"

#include <cmath>

namespace testutil
{

inline juce::Image densePicture()
{
    juce::Image image (juce::Image::ARGB, 480, 300, true);
    juce::Graphics g (image);
    g.fillAll (juce::Colours::white);
    g.setColour (juce::Colours::black);
    g.drawRect (image.getBounds().reduced (12), 10);
    g.fillEllipse (150.0f, 40.0f, 220.0f, 220.0f);
    return image;
}

/** Draws the dense project into `processor` (replacing its drawing) and returns its first word. */
inline juce::Uuid makeDenseProject (artfreq::ArtFreqProcessor& processor)
{
    using namespace artfreq;
    const auto scale = processor.getScale();
    ArtDocument document;
    juce::Uuid first;
    const Band bands[] { Band::high, Band::mid, Band::low };
    int i = 0;

    for (auto band : bands)
    {
        const auto r = bandRange (band, scale);

        for (int k = 0; k < 3; ++k)
        {
            auto text = ArtDocument::makeTextObject ("WORD" + juce::String (i), static_cast<float> (r.low), static_cast<float> (r.high));
            text.startTime = 0.02f + 0.32f * static_cast<float> (k);
            text.endTime = text.startTime + 0.28f;

            if (i == 0)
                first = text.id;

            document.objects.push_back (text);
            ++i;
        }
    }

    auto picture = ArtDocument::makeImageObject (prepareImportedImage (densePicture()), 70.0f, 2200.0f);
    picture.startTime = 0.4f;
    picture.endTime = 0.9f;
    document.objects.push_back (picture);

    juce::Point<float> last { 40.0f, 330.0f };

    for (int s = 1; s <= 48; ++s)
    {
        const juce::Point<float> next { 40.0f + static_cast<float> (s) * 10.0f, 330.0f + 40.0f * std::sin (static_cast<float> (s) * 0.35f) };
        BrushStroke stroke;
        stroke.from = last;
        stroke.to = next;
        stroke.radiusX = stroke.radiusY = 5.0f;
        stroke.softness = 0.3f;
        stroke.strength = 0.9f;
        document.paintStroke (stroke);
        last = next;
    }

    processor.setDocument (document);

    if (auto* parameter = processor.getParameters().getParameter (params::id::density))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (1024.0f));

    processor.rebuildPlanIfNeeded();
    return first;
}

} // namespace testutil
