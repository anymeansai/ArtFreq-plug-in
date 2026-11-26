// The sheet lab: side-by-side pictures of what the drawing code makes, for a person to judge.
//
//   ArtFreqSheetLab <output-dir> [--emoji-fonts a.ttf b.ttf ...]
//
// Everything goes through the real ArtDocument and Compositor on the default scale (log,
// 20 Hz - 20 kHz), and is shown in the canvas's colours:
//
//   sheet-emoji-text.png     a text with emoji in three art fonts, Solid over Outline
//   sheet-picture-modes.png  one photo-like picture as it is, then in Original, Outline and B&W
//   sheet-emoji-weights.png  with --emoji-fonts: a row of emoji from each font file given, at
//                            the size they have on the canvas, sharp and blurred the way a
//                            short FFT smears them (to choose the weight of the bundled font)
//
// Built only when asked for (--target ArtFreqSheetLab).

#include "core/Compositor.h"
#include "core/Fonts.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

using namespace artfreq;

namespace
{

constexpr int margin = 20;
constexpr int captionHeight = 26;

juce::String chars (std::initializer_list<juce::juce_wchar> codePoints)
{
    juce::String text;

    for (const auto c : codePoints)
        text += juce::String::charToString (c);

    return text;
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat png;
    const auto ok = stream.openedOk() && png.writeImageToStream (image, stream);
    std::printf ("%s %s (%d x %d)\n", ok ? "wrote" : "COULD NOT WRITE", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return ok;
}

/** A canvas in the canvas's colours, one image pixel per canvas pixel. */
juce::Image coloured (const Canvas& canvas)
{
    juce::Image image (juce::Image::ARGB, Canvas::width, Canvas::height, false, juce::SoftwareImageType());
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
    const auto& table = ui::theme::spectrumTable();

    for (int y = 0; y < Canvas::height; ++y)
    {
        auto* row = reinterpret_cast<juce::PixelARGB*> (data.getLinePointer (y));

        for (int x = 0; x < Canvas::width; ++x)
            row[x] = table[static_cast<size_t> (juce::jlimit (0, 255, juce::roundToInt (canvas.get (x, y) * 255.0f)))];
    }

    return image;
}

juce::Image render (const ArtObject& object)
{
    ArtDocument document;
    document.objects.push_back (object);

    Compositor compositor;
    Canvas canvas;
    compositor.render (document, FrequencyScale(), canvas);
    return coloured (canvas);
}

/** A sheet of panels in a grid, each with a caption over it. */
struct Sheet
{
    Sheet (int columns, int rows, int panelWidthToUse, int panelHeightToUse)
        : panelWidth (panelWidthToUse), panelHeight (panelHeightToUse),
          image (juce::Image::ARGB, margin + columns * (panelWidth + margin), margin + rows * (captionHeight + panelHeight + margin),
                 false, juce::SoftwareImageType())
    {
        juce::Graphics g (image);
        g.fillAll (ui::theme::spectrum (0.0f).brighter (0.08f));
    }

    juce::Rectangle<int> panel (int column, int row, const juce::String& caption)
    {
        const juce::Rectangle<int> cell (margin + column * (panelWidth + margin), margin + row * (captionHeight + panelHeight + margin),
                                         panelWidth, captionHeight + panelHeight);
        juce::Graphics g (image);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (fonts::ui (15.0f, fonts::UiWeight::semiBold));
        g.drawText (caption, cell.withHeight (captionHeight - 6), juce::Justification::centredLeft, true);
        return cell.withTrimmedTop (captionHeight);
    }

    void draw (const juce::Image& picture, juce::Rectangle<int> area)
    {
        juce::Graphics g (image);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (picture, area.toFloat());
    }

    int panelWidth, panelHeight;
    juce::Image image;
};

//==============================================================================
void writeEmojiText (const juce::File& folder)
{
    // "<fire> DROP <thumbs up, a skin tone>" over "I <heart> NY <keycap 1>".
    const auto text = chars ({ 0x1f525 }) + " DROP " + chars ({ 0x1f44d, 0x1f3fd }) + "\nI " + chars ({ 0x2764, 0xfe0f }) + " NY "
                      + chars ({ '1', 0xfe0f, 0x20e3 });
    const int fontIndices[] = { 0, 3, 4 };   // Inter Bold, Fraunces, DM Mono

    Sheet sheet (3, 2, 620, 310);

    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 2; ++row)
        {
            auto object = ArtDocument::makeTextObject (text, 150.0f, 14000.0f);
            object.startTime = 0.05f;
            object.endTime = 0.95f;
            object.fontIndex = fontIndices[column];
            object.textStyle = row == 0 ? TextStyle::solid : TextStyle::outline;

            const auto area = sheet.panel (column, row, fonts::artFontName (object.fontIndex) + (row == 0 ? ", Solid" : ", Outline"));
            sheet.draw (render (object), area);
        }
    }

    writePng (sheet.image, folder.getChildFile ("sheet-emoji-text.png"));
}

//==============================================================================
/** A photo-like head and shoulders: soft light, a darker background, hair, eyes and a mouth, so
    it has every grey level and soft as well as sharp edges. */
juce::Image portrait()
{
    constexpr int width = 384, height = 512;
    juce::Image image (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    juce::Graphics g (image);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3a), 0.0f, 0.0f, juce::Colour (0xff141414), 0.0f, static_cast<float> (height), false));
    g.fillAll();

    // Shoulders and neck.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6e6e6e), 120.0f, 380.0f, juce::Colour (0xff2c2c2c), 330.0f, 512.0f, false));
    g.fillEllipse (20.0f, 400.0f, 344.0f, 260.0f);
    g.setColour (juce::Colour (0xff8a8a8a));
    g.fillRect (160.0f, 330.0f, 64.0f, 90.0f);

    // The face, lit from the upper left.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e8e8), 150.0f, 170.0f, juce::Colour (0xff5a5a5a), 300.0f, 360.0f, true));
    g.fillEllipse (102.0f, 110.0f, 180.0f, 240.0f);

    // Hair.
    juce::Path hair;
    hair.startNewSubPath (92.0f, 230.0f);
    hair.cubicTo (70.0f, 80.0f, 300.0f, 40.0f, 294.0f, 230.0f);
    hair.cubicTo (280.0f, 150.0f, 210.0f, 120.0f, 190.0f, 132.0f);
    hair.cubicTo (150.0f, 125.0f, 105.0f, 160.0f, 92.0f, 230.0f);
    g.setColour (juce::Colour (0xff1c1c1c));
    g.fillPath (hair);

    // Eyes, brows, the nose's shadow, the mouth.
    for (const auto x : { 150.0f, 214.0f })
    {
        g.setColour (juce::Colour (0xffd0d0d0));
        g.fillEllipse (x - 4.0f, 208.0f, 30.0f, 14.0f);
        g.setColour (juce::Colour (0xff202020));
        g.fillEllipse (x + 6.0f, 209.0f, 11.0f, 12.0f);
        g.setColour (juce::Colour (0xff303030));
        g.fillRoundedRectangle (x - 6.0f, 192.0f, 34.0f, 6.0f, 3.0f);
    }

    g.setColour (juce::Colour (0x66000000));
    g.fillEllipse (184.0f, 232.0f, 16.0f, 50.0f);
    g.setColour (juce::Colour (0xff3c3c3c));
    g.fillEllipse (160.0f, 290.0f, 66.0f, 18.0f);
    g.setColour (juce::Colour (0xff9a9a9a));
    g.fillEllipse (170.0f, 292.0f, 46.0f, 6.0f);
    return image;
}

void writePictureModes (const juce::File& folder)
{
    const auto picture = prepareImportedImage (portrait());

    struct Panel
    {
        const char* caption;
        ImageMode mode;
    };

    const Panel panels[] = { { "Original", ImageMode::original },
                             { "Outline", ImageMode::outline },
                             { "B&W (black and white)", ImageMode::blackAndWhite } };

    Sheet sheet (4, 1, 460, 520);
    sheet.draw (picture, sheet.panel (0, 0, "The picture").reduced (35, 0));

    for (int i = 0; i < 3; ++i)
    {
        auto object = ArtDocument::makeImageObject (picture, 25.0f, 18000.0f);
        object.startTime = 0.08f;
        object.endTime = 0.92f;
        object.imageMode = panels[i].mode;
        sheet.draw (render (object), sheet.panel (i + 1, 0, panels[i].caption));
    }

    writePng (sheet.image, folder.getChildFile ("sheet-picture-modes.png"));
}

//==============================================================================
/** A separable box blur, three passes of it (close to a Gaussian). */
void blur (juce::Image& image, int radiusX, int radiusY)
{
    const auto width = image.getWidth(), height = image.getHeight();
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
    std::vector<float> values (static_cast<size_t> (width * height)), scratch (values.size());

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            values[static_cast<size_t> (y * width + x)] = data.getLinePointer (y)[x * data.pixelStride];

    const auto pass = [&] (int dx, int dy, int radius)
    {
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                auto sum = 0.0f;

                for (int k = -radius; k <= radius; ++k)
                    sum += values[static_cast<size_t> (juce::jlimit (0, height - 1, y + k * dy) * width + juce::jlimit (0, width - 1, x + k * dx))];

                scratch[static_cast<size_t> (y * width + x)] = sum / static_cast<float> (2 * radius + 1);
            }
        }

        values.swap (scratch);
    };

    for (int i = 0; i < 3; ++i)
    {
        pass (1, 0, radiusX);
        pass (0, 1, radiusY);
    }

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            data.getLinePointer (y)[x * data.pixelStride] = static_cast<juce::uint8> (juce::jlimit (0, 255, juce::roundToInt (values[static_cast<size_t> (y * width + x)])));
}

juce::Image colouredMask (const juce::Image& mask)
{
    juce::Image image (juce::Image::ARGB, mask.getWidth(), mask.getHeight(), false, juce::SoftwareImageType());
    const juce::Image::BitmapData in (mask, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData out (image, juce::Image::BitmapData::writeOnly);
    const auto& table = ui::theme::spectrumTable();

    for (int y = 0; y < mask.getHeight(); ++y)
        for (int x = 0; x < mask.getWidth(); ++x)
            reinterpret_cast<juce::PixelARGB*> (out.getLinePointer (y))[x] = table[in.getLinePointer (y)[x * in.pixelStride]];

    return image;
}

void writeEmojiWeights (const juce::File& folder, const juce::StringArray& fontFiles)
{
    // Fire, a grin, thumbs up, notes, a heart, a rocket, a ghost, a keyboard.
    const juce::juce_wchar emoji[] = { 0x1f525, 0x1f600, 0x1f44d, 0x1f3b6, 0x2764, 0x1f680, 0x1f47b, 0x1f3b9 };

    // A text of eight emoji over 4 - 96 % of the length and the high band (3.5 - 16 kHz) is 942
    // canvas columns by 225 rows; a window shows the 1024 x 1024 canvas about 760 x 380.
    constexpr int canvasWidth = 942, canvasHeight = 225;
    constexpr int shownWidth = 700, shownHeight = 84;

    Sheet sheet (2, fontFiles.size(), 2 * shownWidth / 2 + 220, 2 * shownHeight);

    for (int row = 0; row < fontFiles.size(); ++row)
    {
        const juce::File file (fontFiles[row]);
        juce::MemoryBlock bytes;
        file.loadFileAsData (bytes);
        const auto typeface = juce::Typeface::createSystemTypefaceFor (bytes.getData(), bytes.getSize());

        if (typeface == nullptr)
        {
            std::printf ("could not load %s\n", file.getFullPathName().toRawUTF8());
            continue;
        }

        juce::Image mask (juce::Image::SingleChannel, canvasWidth, canvasHeight, true, juce::SoftwareImageType());
        {
            juce::Graphics g (mask);
            g.setColour (juce::Colours::white);
            const auto cell = static_cast<float> (canvasWidth) / static_cast<float> (std::size (emoji));

            for (size_t i = 0; i < std::size (emoji); ++i)
            {
                if (const auto glyph = typeface->getNominalGlyphForCodepoint (emoji[i]))
                {
                    juce::Path outline;
                    typeface->getOutlineForGlyph (static_cast<int> (*glyph), outline);

                    // The emoji's box (2100 units of a 2048 unit em, from 350 under the baseline)
                    // fills the rows; its advance (2600 units) the cell.
                    const auto scaleY = static_cast<float> (canvasHeight) * 0.92f / (2100.0f / 2048.0f);
                    const auto scaleX = cell / (2600.0f / 2048.0f);
                    g.fillPath (outline, juce::AffineTransform::scale (scaleX, scaleY)
                                             .translated (cell * static_cast<float> (i),
                                                          static_cast<float> (canvasHeight) * 0.04f + scaleY * 1750.0f / 2048.0f));
                }
            }
        }

        // As the analyser shows it at best, and through a short FFT (about 20 ms in time, a
        // twentieth of an octave in frequency).
        auto smeared = mask.createCopy();
        blur (smeared, 4, 3);

        sheet.draw (colouredMask (mask), sheet.panel (0, row, file.getFileNameWithoutExtension() + ", as drawn"));
        sheet.draw (colouredMask (smeared), sheet.panel (1, row, file.getFileNameWithoutExtension() + ", smeared by the analyser"));
    }

    writePng (sheet.image, folder.getChildFile ("sheet-emoji-weights.png"));
}

} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juce;

    if (argc < 2)
    {
        std::printf ("usage: ArtFreqSheetLab <output-dir> [--emoji-fonts a.ttf b.ttf ...]\n");
        return 1;
    }

    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    folder.createDirectory();

    juce::StringArray fontFiles;

    for (int i = 2; i < argc; ++i)
        if (juce::String (argv[i]) != "--emoji-fonts")
            fontFiles.add (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[i])).getFullPathName());

    writeEmojiText (folder);
    writePictureModes (folder);

    if (! fontFiles.isEmpty())
        writeEmojiWeights (folder, fontFiles);

    return 0;
}
