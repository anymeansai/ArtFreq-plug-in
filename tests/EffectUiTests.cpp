// FreqArt FX's editor: the same window as the instrument's, with Mode and Depth in the header,
// the sines' knobs greyed while nothing plays them, the export asking for a bounce in the DAW
// while carving, a help panel of its own that fits the window, and a canvas that shows what the
// analyser will show in each mode.
//
// These need JUCE's GUI classes, so on Linux run them under a virtual display (xvfb-run -a).

#include "ProcessorTestUtils.h"
#include "TestFramework.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "ui/CanvasPlanImage.h"
#include "ui/Editor.h"
#include "ui/EffectControls.h"
#include "ui/EffectPreview.h"
#include "ui/ShellExport.h"
#include "ui/ShellPanels.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

using namespace freqart;
using testutil::setParameter;

namespace
{

std::unique_ptr<FreqArtProcessor> makeProcessor (FreqArtProcessor::Variant variant)
{
    auto processor = std::make_unique<FreqArtProcessor> (variant);
    processor->setRateAndBufferSizeDetails (48000.0, 512);
    processor->prepareToPlay (48000.0, 512);
    return processor;
}

std::unique_ptr<FreqArtEditor> makeEditor (FreqArtProcessor& processor)
{
    return std::unique_ptr<FreqArtEditor> (dynamic_cast<FreqArtEditor*> (processor.createEditor()));
}

void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

juce::Component* findComponent (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findComponent (*child, id))
            return found;

    return nullptr;
}

/** The colour-map index of a pixel of an image drawn with the spectrogram colours. */
int levelOf (const juce::Image& image, int x, int y)
{
    const auto pixel = image.getPixelAt (x, y);
    const auto& table = ui::theme::spectrumTable();

    for (int i = 0; i < 256; ++i)
    {
        const auto c = juce::Colour (table[static_cast<size_t> (i)].getUnpremultiplied());

        if (c.getRed() == pixel.getRed() && c.getGreen() == pixel.getGreen() && c.getBlue() == pixel.getBlue())
            return i;
    }

    return -1;
}

} // namespace

FREQART_TEST (uiEffectHeaderHoldsModeAndDepth)
{
    auto effect = makeProcessor (FreqArtProcessor::Variant::effect);
    auto editor = makeEditor (*effect);
    pump (60);

    auto* header = findComponent (*editor, "header");
    auto* controls = findComponent (*editor, "effect-controls");
    auto* mode = dynamic_cast<ui::SegmentedControl*> (findComponent (*editor, "mode"));
    auto* depth = findComponent (*editor, "depth");
    auto* view = findComponent (*editor, "view-switch");
    auto* undo = findComponent (*editor, "undo");

    CHECK (header != nullptr && controls != nullptr && mode != nullptr && depth != nullptr && view != nullptr && undo != nullptr);

    if (controls == nullptr || mode == nullptr || depth == nullptr || view == nullptr || undo == nullptr)
        return;

    CHECK (controls->getParentComponent() == header);
    CHECK (view->getRight() < controls->getX() && controls->getRight() < undo->getX());
    CHECK (mode->getRight() <= controls->getWidth() && depth->getRight() <= controls->getWidth());
    CHECK (mode->getSelectedIndex() == 0);

    // Choosing a mode sets the parameter; the parameter (automation) moves the control.
    mode->setSelectedIndex (2, juce::sendNotificationSync);
    CHECK (effect->getEffectMode() == EffectMode::stencil);
    setParameter (*effect, params::id::mode, 3.0f);
    pump (30);
    CHECK (mode->getSelectedIndex() == 3);

    // The instrument's header has none of it.
    auto instrument = makeProcessor (FreqArtProcessor::Variant::instrument);
    auto instrumentEditor = makeEditor (*instrument);
    CHECK (findComponent (*instrumentEditor, "effect-controls") == nullptr);
    CHECK (findComponent (*instrumentEditor, "mode") == nullptr);
}

FREQART_TEST (uiEffectGreysWhatTheModeDoesNotUse)
{
    // The sines' Density, Contrast and Level are greyed in Carve and Stencil; Depth in Draw.
    auto effect = makeProcessor (FreqArtProcessor::Variant::effect);
    auto editor = makeEditor (*effect);
    pump (80);

    auto* density = findComponent (*editor, "density");
    auto* level = findComponent (*editor, "level");
    auto* depth = findComponent (*editor, "depth");

    if (density == nullptr || level == nullptr || depth == nullptr)
    {
        CHECK (false);
        return;
    }

    CHECK (! density->isEnabled() && ! level->isEnabled() && depth->isEnabled());

    setParameter (*effect, params::id::mode, 3.0f);   // Draw
    pump (120);
    CHECK (density->isEnabled() && level->isEnabled() && ! depth->isEnabled());

    setParameter (*effect, params::id::mode, 1.0f);   // Carve + Draw
    pump (120);
    CHECK (density->isEnabled() && depth->isEnabled());

    // In the instrument they are always on.
    auto instrument = makeProcessor (FreqArtProcessor::Variant::instrument);
    auto instrumentEditor = makeEditor (*instrument);
    pump (120);

    if (auto* knob = findComponent (*instrumentEditor, "density"))
        CHECK (knob->isEnabled());
}

FREQART_TEST (uiEffectExportAsksForTheDawWhileCarving)
{
    auto effect = makeProcessor (FreqArtProcessor::Variant::effect);
    auto editor = makeEditor (*effect);
    pump (80);

    auto* exporter = dynamic_cast<ui::shell::ExportPanel*> (findComponent (*editor, "export"));
    auto* bounce = findComponent (*editor, "bounce");

    if (exporter == nullptr || bounce == nullptr)
    {
        CHECK (false);
        return;
    }

    for (const auto& [mode, carving] : { std::pair<float, bool> { 0.0f, true }, { 1.0f, false }, { 2.0f, true }, { 3.0f, false } })
    {
        setParameter (*effect, params::id::mode, mode);
        exporter->refresh();
        CHECK (exporter->isAskingForDawBounce() == carving);
        CHECK (bounce->isVisible() == ! carving);
    }

    // The instrument always bounces.
    auto instrument = makeProcessor (FreqArtProcessor::Variant::instrument);
    auto instrumentEditor = makeEditor (*instrument);

    if (auto* other = dynamic_cast<ui::shell::ExportPanel*> (findComponent (*instrumentEditor, "export")))
    {
        other->refresh();
        CHECK (! other->isAskingForDawBounce());
    }
}

FREQART_TEST (uiEffectHelpFitsTheWindow)
{
    // Both help panels fit in the window with room around them (the editor centres them).
    ui::shell::HelpPanel instrumentHelp (false), effectHelp (true);
    CHECK_LESS (instrumentHelp.getIdealHeight(), FreqArtEditor::baseHeight - 24);
    CHECK_LESS (effectHelp.getIdealHeight(), FreqArtEditor::baseHeight - 24);
}

FREQART_TEST (uiEffectCanvasShowsEachMode)
{
    // A band drawn in the middle of the canvas (columns 300 to 700), its sines' picture at full
    // brightness there.
    Canvas canvas;

    for (int y = 400; y < 600; ++y)
        for (int x = 300; x < 700; ++x)
            canvas.at (x, y) = 1.0f;

    auto masks = std::make_shared<MaskPlanSet>();
    masks->exact = buildMaskPlan (canvas, MaskSettings());
    masks->halo = widenMaskPlan (*masks->exact, 4, 8);

    constexpr int width = 200, height = 100;
    std::vector<uint8_t> sines (static_cast<size_t> (width * height), 0);

    for (int y = 40; y < 59; ++y)
        for (int x = 59; x < 137; ++x)
            sines[static_cast<size_t> (y * width + x)] = 255;

    ui::EffectPreviewImage preview;
    const auto inside = juce::Point<int> (100, 50), outside = juce::Point<int> (100, 15);
    const std::shared_ptr<const MaskPlanSet> shared = masks;

    // Carve at -80 dB: the drawing is black, the song around it is not (also just before it).
    const auto justBefore = juce::Point<int> (57, 50);   // column 294: 6 before the band
    auto image = preview.get (sines, 1, EffectMode::carve, -80.0f, shared, width, height);
    CHECK (levelOf (image, inside.x, inside.y) == 0);
    CHECK_GREATER (levelOf (image, outside.x, outside.y), 40);
    CHECK_GREATER (levelOf (image, justBefore.x, justBefore.y), 20);

    // At -24 dB it is darker than the song but not black.
    const auto song = levelOf (image, outside.x, outside.y);
    image = preview.get (sines, 1, EffectMode::carve, -24.0f, shared, width, height);
    CHECK_GREATER (levelOf (image, inside.x, inside.y), 0);

    // Stencil: the other way round.
    image = preview.get (sines, 1, EffectMode::stencil, -80.0f, shared, width, height);
    CHECK_GREATER (levelOf (image, inside.x, inside.y), 40);
    CHECK (levelOf (image, outside.x, outside.y) == 0);

    // Carve + Draw: the sines bright in the band, a dark edge just outside it (the halo).
    image = preview.get (sines, 1, EffectMode::carveDraw, -80.0f, shared, width, height);
    CHECK (levelOf (image, inside.x, inside.y) == 255);
    CHECK (levelOf (image, justBefore.x, justBefore.y) == 0);
    CHECK (levelOf (image, outside.x, outside.y) == song);

    // Draw: the sines over the song, nothing carved.
    image = preview.get (sines, 1, EffectMode::draw, -80.0f, shared, width, height);
    CHECK (levelOf (image, inside.x, inside.y) == 255);
    CHECK (levelOf (image, outside.x, outside.y) == song);

    // Nothing drawn: every mode shows the song as it is (Stencil does not black it out).
    auto empty = std::make_shared<MaskPlanSet>();
    empty->exact = buildMaskPlan (Canvas(), MaskSettings());
    empty->halo = widenMaskPlan (*empty->exact, 4, 8);
    image = preview.get (std::vector<uint8_t> (static_cast<size_t> (width * height), 0), 2, EffectMode::stencil, -80.0f, empty, width, height);
    CHECK (levelOf (image, outside.x, outside.y) == song);
    CHECK_GREATER (levelOf (image, inside.x, inside.y), 20);

    // The stand-in song is louder in the lows than in the highs, and never above the top.
    CHECK_GREATER (ui::EffectPreviewImage::songDb (0.3, 0.1), ui::EffectPreviewImage::songDb (0.3, 0.9));
    CHECK_LESS (ui::EffectPreviewImage::songDb (0.0, 0.0), 0.001f);
}
