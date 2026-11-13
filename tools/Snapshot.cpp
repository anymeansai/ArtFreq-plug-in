// Renders the editor to PNG files, without a host, and checks the shell along the way.
//
//   FreqArtSnapshot <output-dir> [pixel-scale]
//
// Writes
//
//   freqart-default.png          a fresh instance (the default drawing)
//   freqart-rich.png             text "HELLO" in the high band, a picture in the low / mid band,
//                                brush strokes, density 384
//   freqart-playing.png          the same while previewing (playhead, Preview on)
//   freqart-text-selected.png    the text object selected
//   freqart-image-selected.png   the picture selected
//   freqart-brush.png            the Brush tool active (chosen with the B key)
//   freqart-live.png             the live view, filled from the plan
//   freqart-settings.png         the settings panel open
//   freqart-settings-reset.png   the same, asking to confirm "Reset drawing"
//   freqart-help.png             the help panel open
//   freqart-export.png           after "Bounce WAV": the chip and "Save..."
//   freqart-menu.png             the Length menu open
//   freqart-tooltip.png          a tooltip (Density's)
//   freqart-edit.png             typing a value into the Level knob
//   freqart-75.png               window at 75 %, one image pixel per logical pixel
//   freqart-150.png              window at 150 %, one image pixel per logical pixel
//
// All but the last two are rendered at <pixel-scale> image pixels per logical pixel (default 2, as
// on a HiDPI display). Along the way it checks keyboard shortcuts, undo / redo, the bounce (a
// WAV file of the right length) and that the window size is remembered. It exits with 1 if a
// file cannot be written or a check fails.
//
// On Linux run it under a virtual display:  xvfb-run -a FreqArtSnapshot out

#include "Parameters.h"
#include "PluginProcessor.h"
#include "core/FrequencyScale.h"
#include "ui/Editor.h"
#include "ui/LiveView.h"
#include "ui/ShellExport.h"
#include "ui/Widgets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>

namespace
{

bool failed = false;

void check (bool condition, const juce::String& what)
{
    if (! condition)
    {
        std::cerr << "CHECK FAILED: " << what << std::endl;
        failed = true;
    }
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}

void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
}

void setParameter (freqart::FreqArtProcessor& processor, const char* id, float plainValue)
{
    if (auto* parameter = processor.getParameters().getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
}

juce::Component* findComponentWithId (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findComponentWithId (*child, id))
            return found;

    return nullptr;
}

template <typename Type>
Type* find (juce::Component& root, const juce::String& id)
{
    auto* c = dynamic_cast<Type*> (findComponentWithId (root, id));
    check (c != nullptr, "component '" + id + "' exists");
    return c;
}

void setWindowScale (freqart::FreqArtEditor& editor, float scale)
{
    editor.setSize (juce::roundToInt (static_cast<float> (freqart::FreqArtEditor::baseWidth) * scale),
                    juce::roundToInt (static_cast<float> (freqart::FreqArtEditor::baseHeight) * scale));
}

/** A stand-in for an imported picture: a shaded disc and a frame on a white background, the
    kind of logo Auto turns into its negative. */
juce::Image makeTestPicture()
{
    juce::Image image (juce::Image::ARGB, 480, 300, true);
    juce::Graphics g (image);
    g.fillAll (juce::Colours::white);

    g.setColour (juce::Colours::black);
    g.drawRect (image.getBounds().reduced (12), 10);

    const auto disc = juce::Rectangle<float> (150.0f, 40.0f, 220.0f, 220.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, disc.getTopLeft(),
                                             juce::Colour (0xffc8c8c8), disc.getBottomRight(), false));
    g.fillEllipse (disc);

    g.setColour (juce::Colours::black);
    g.fillRect (50, 90, 60, 120);
    return image;
}

/** The "rich" drawing: text in the highs, a picture in the low / mid band, brush strokes. */
void makeRichDrawing (freqart::FreqArtProcessor& processor, juce::Uuid& textId, juce::Uuid& imageId)
{
    using namespace freqart;

    const auto scale = processor.getScale();
    ArtDocument document;

    const auto high = bandRange (Band::high, scale);
    auto text = ArtDocument::makeTextObject ("HELLO", static_cast<float> (high.low), static_cast<float> (high.high));
    text.startTime = 0.04f;
    text.endTime = 0.50f;
    textId = text.id;
    document.objects.push_back (text);

    auto picture = ArtDocument::makeImageObject (prepareImportedImage (makeTestPicture()), 70.0f, 2200.0f);
    picture.startTime = 0.56f;
    picture.endTime = 0.96f;
    imageId = picture.id;
    document.objects.push_back (picture);

    // A wave painted under the text, and a soft dab.
    juce::Point<float> last { 40.0f, 330.0f };

    for (int i = 1; i <= 48; ++i)
    {
        const auto x = 40.0f + static_cast<float> (i) * 10.0f;
        const juce::Point<float> next { x, 330.0f + 40.0f * std::sin (static_cast<float> (i) * 0.35f) };

        BrushStroke stroke;
        stroke.from = last;
        stroke.to = next;
        stroke.radiusX = stroke.radiusY = 5.0f;
        stroke.softness = 0.3f;
        stroke.strength = 0.9f;
        document.paintStroke (stroke);
        last = next;
    }

    BrushStroke dab;
    dab.from = dab.to = { 300.0f, 440.0f };
    dab.radiusX = dab.radiusY = 26.0f;
    dab.softness = 1.0f;
    dab.strength = 0.8f;
    document.paintStroke (dab);

    processor.setDocument (document);
    setParameter (processor, freqart::params::id::density, 384.0f);
}

/** Runs the processor for a while with Preview on, as a host would, so there is a playhead. */
void playFor (freqart::FreqArtProcessor& processor, double seconds)
{
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    const auto blocks = static_cast<int> (seconds * 48000.0 / blockSize);

    for (int i = 0; i < blocks; ++i)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
    }
}

bool checkWav (const juce::File& file, double expectedSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return false;

    const auto seconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    std::cout << "bounce: " << file.getFullPathName() << ", " << seconds << " s, " << reader->numChannels << " ch, "
              << reader->bitsPerSample << " bit" << std::endl;
    return std::abs (seconds - expectedSeconds) < 0.01 && reader->numChannels == 2;
}

} // namespace

int main (int argc, char* argv[])
{
   #if JUCE_LINUX || JUCE_BSD
    // As a standalone app JUCE installs X error handlers that tolerate a virtual display without
    // a window manager (Xlib's own would end the process). No application object is created.
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return nullptr; };
   #endif

    const juce::ScopedJuceInitialiser_GUI gui;

    const auto outputDir = juce::File::getCurrentWorkingDirectory()
                               .getChildFile (argc > 1 ? juce::String (argv[1]) : juce::String ("snapshots"));
    const auto pixelScale = juce::jlimit (0.5f, 4.0f, argc > 2 ? juce::String (argv[2]).getFloatValue() : 2.0f);

    if (! outputDir.createDirectory())
    {
        std::cerr << "Cannot create " << outputDir.getFullPathName() << std::endl;
        return 1;
    }

    using namespace freqart;

    FreqArtProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    {
        const std::unique_ptr<FreqArtEditor> editor (dynamic_cast<FreqArtEditor*> (processor.createEditor()));

        if (editor == nullptr)
        {
            std::cerr << "The processor made no FreqArtEditor" << std::endl;
            return 1;
        }

        check (editor->getWidth() == FreqArtEditor::baseWidth && editor->getHeight() == FreqArtEditor::baseHeight,
               "a new editor opens at 1120 x 720");

        const auto save = [&] (const juce::String& name, float scale, bool settle = true)
        {
            processor.rebuildPlanIfNeeded();

            if (settle)
                pump (60);

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
            const auto file = outputDir.getChildFile ("freqart-" + name + ".png");

            if (writePng (image, file))
                std::cout << file.getFullPathName() << std::endl;
            else
                check (false, "write " + file.getFullPathName());
        };

        auto* content = findComponentWithId (*editor, "content");
        check (content != nullptr, "the editor has its content");

        if (content == nullptr)
            return 1;

        // Let attachments, the first plan and any async updates settle.
        pump (150);
        save ("default", pixelScale);

        // ---- the rich drawing -----------------------------------------------------------------
        juce::Uuid textId, imageId;
        makeRichDrawing (processor, textId, imageId);
        pump (100);
        save ("rich", pixelScale);

        processor.setPreview (true);
        playFor (processor, 2.2);
        pump (80);
        check (processor.isSounding(), "Preview makes the processor sound");
        save ("playing", pixelScale);
        processor.setPreview (false);
        playFor (processor, 0.1);
        pump (60);

        editor->selectObject (textId);
        save ("text-selected", pixelScale);

        editor->selectObject (imageId);
        save ("image-selected", pixelScale);

        editor->selectObject (juce::Uuid::null());

        // ---- shortcuts ------------------------------------------------------------------------
        content->keyPressed (juce::KeyPress ('b', juce::ModifierKeys(), 'b'));
        pump (30);

        if (auto* brush = find<juce::Button> (*editor, "tool-brush"))
            check (brush->getToggleState(), "B selects the Brush tool");

        save ("brush", pixelScale);

        content->keyPressed (juce::KeyPress ('e', juce::ModifierKeys(), 'e'));
        pump (30);

        if (auto* erase = find<juce::Button> (*editor, "tool-erase"))
            check (erase->getToggleState(), "E selects the Erase tool");

        content->keyPressed (juce::KeyPress ('v', juce::ModifierKeys(), 'v'));
        pump (30);

        if (auto* select = find<juce::Button> (*editor, "tool-select"))
            check (select->getToggleState(), "V selects the Select tool");

        // Undo takes the rich drawing back to the default one; redo restores it.
        check (processor.canUndo(), "the rich drawing can be undone");
        const auto objectsBefore = processor.getDocument().objects.size();
        content->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (processor.getDocument().objects.size() != objectsBefore && processor.canRedo(), "Cmd+Z undoes");
        pump (30);

        if (auto* redoButton = find<juce::Button> (*editor, "redo"))
            check (redoButton->isEnabled(), "Redo is enabled after an undo");

        content->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
        check (processor.getDocument().objects.size() == objectsBefore, "Cmd+Shift+Z redoes");

        // Delete removes the selection (from anywhere in the window); undo brings it back.
        editor->selectObject (textId);
        content->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        check (processor.getDocument().find (textId) == nullptr, "Delete removes the selected object");
        content->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        check (processor.getDocument().find (textId) != nullptr, "undo brings the deleted object back");
        editor->selectObject (juce::Uuid::null());
        pump (60);

        // ---- the look of standard controls: a menu, a tooltip, typing a value --------------------------
        if (auto* length = find<juce::ComboBox> (*editor, "length"))
        {
            // Off screen, an open menu closes itself at its first timer tick: draw it at once.
            length->showPopup();
            save ("menu", pixelScale, false);
            juce::PopupMenu::dismissAllActiveMenus();
            pump (60);
        }

        if (auto* tips = find<juce::TooltipWindow> (*editor, "tooltips"))
        {
            if (auto* density = dynamic_cast<juce::Slider*> (findComponentWithId (*editor, "density")))
            {
                tips->displayTip (density->localPointToGlobal (juce::Point<int> (38, 60)), density->getTooltip());
                save ("tooltip", pixelScale);
                tips->hideTip();
            }
        }

        if (auto* level = findComponentWithId (*editor, "level"))
        {
            for (auto* child : level->getChildren())
            {
                if (auto* valueBox = dynamic_cast<juce::Label*> (child))
                {
                    valueBox->showEditor();
                    check (valueBox->getCurrentTextEditor() != nullptr, "a knob's value can be typed");
                    save ("edit", pixelScale);
                    valueBox->hideEditor (true);
                }
            }
        }

        // ---- live view --------------------------------------------------------------------------
        editor->showLiveView (true);

        if (auto* live = find<ui::LiveView> (*editor, "live-view"))
        {
            check (live->isVisible(), "the live view is shown");
            live->fillFromPlanForSnapshot (6.0);
        }

        save ("live", pixelScale);
        editor->showLiveView (false);

        // ---- panels -------------------------------------------------------------------------------
        editor->showSettings (true);
        save ("settings", pixelScale);

        if (auto* reset = find<juce::Button> (*editor, "reset-drawing"))
        {
            reset->onClick();
            save ("settings-reset", pixelScale);
        }

        content->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* panel = findComponentWithId (*editor, "settings-panel"))
            check (! panel->isVisible(), "Esc closes the settings");

        editor->showHelp (true);
        save ("help", pixelScale);
        editor->showHelp (false);

        // ---- export -------------------------------------------------------------------------------
        if (auto* exporter = find<ui::shell::ExportPanel> (*editor, "export"))
        {
            exporter->getBounceFile().deleteFile();

            if (auto* bounce = find<juce::Button> (*editor, "bounce"))
            {
                check (bounce->isEnabled(), "Bounce WAV is enabled for a drawing");
                bounce->onClick();
            }

            check (exporter->isBouncing(), "the bounce runs in the background");
            check (exporter->waitForBounce (60000), "the bounce finishes");
            check (exporter->hasCurrentFile(), "the bounce made a file: " + exporter->getLastError());
            check (checkWav (exporter->getBounceFile(), processor.getLengthSeconds()), "the bounce is one pass, stereo");
            save ("export", pixelScale);

            // A different drawing makes the file stale: back to "Bounce WAV".
            setParameter (processor, params::id::density, 200.0f);
            processor.rebuildPlanIfNeeded();
            pump (80);
            exporter->refresh();
            check (! exporter->hasCurrentFile(), "changing the drawing retires the bounce");
        }

        // ---- window sizes -------------------------------------------------------------------------
        setWindowScale (*editor, 1.5f);
        pump (60);
        check (processor.getEditorWidth() == 1680 && processor.getEditorHeight() == 1080, "the window size is remembered");
        save ("150", 1.0f);

        setWindowScale (*editor, 0.75f);
        pump (60);
        save ("75", 1.0f);

        setWindowScale (*editor, 1.25f);
        pump (30);
    }

    // A new editor (the window closed and opened again) comes back at the remembered size.
    {
        const std::unique_ptr<juce::AudioProcessorEditor> again (processor.createEditor());
        check (again != nullptr && again->getWidth() == 1400 && again->getHeight() == 900,
               "a reopened editor has the remembered size");
    }

    // Deliver anything still queued before JUCE shuts down.
    pump (50);
    processor.releaseResources();

    if (failed)
        std::cerr << "Some checks failed" << std::endl;

    return failed ? 1 : 0;
}
