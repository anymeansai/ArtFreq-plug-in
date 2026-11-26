// Emoji in text objects: the bundled monochrome emoji font, the runs a line is split into, the
// sequences (skin tones, joined emoji, keycaps, flags), and that an emoji survives the text box,
// undo and a saved project, in ArtFreq and in ArtFreq FX.
//
// The emoji are written as code points (U+1F525 and so on), never as characters in the source.

#include "ProcessorTestUtils.h"
#include "TestFramework.h"
#include "WaitFor.h"

#include "PluginProcessor.h"
#include "core/Compositor.h"
#include "core/Fonts.h"
#include "core/MaskPlan.h"
#include "core/TextRaster.h"
#include "ui/Actions.h"
#include "ui/Editor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <initializer_list>
#include <memory>

using namespace artfreq;

namespace
{

using CodePoint = juce::juce_wchar;

constexpr CodePoint fire = 0x1f525;
constexpr CodePoint heart = 0x2764;
constexpr CodePoint thumbsUp = 0x1f44d;
constexpr CodePoint grinning = 0x1f600;
constexpr CodePoint blackSquare = 0x2b1b;      // fills the whole box every emoji is drawn in
constexpr CodePoint lightSkin = 0x1f3fb;
constexpr CodePoint mediumSkin = 0x1f3fd;
constexpr CodePoint joiner = 0x200d;
constexpr CodePoint asEmoji = 0xfe0f;
constexpr CodePoint keycap = 0x20e3;
constexpr CodePoint woman = 0x1f469;
constexpr CodePoint laptop = 0x1f4bb;

juce::String chars (std::initializer_list<CodePoint> codePoints)
{
    juce::String text;

    for (const auto c : codePoints)
        text += juce::String::charToString (c);

    return text;
}

juce::Rectangle<float> boundsOf (const juce::String& text, int font = 0)
{
    return makeTextPath (text, font).getBounds();
}

juce::Image singleChannel (int width, int height)
{
    return juce::Image (juce::Image::SingleChannel, width, height, true, juce::SoftwareImageType());
}

int valueAt (const juce::Image& image, int x, int y)
{
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    return data.getLinePointer (y)[x * data.pixelStride];
}

/** How many pixels of the area are above half. */
int inkIn (const juce::Image& image, juce::Rectangle<int> area)
{
    area = area.getIntersection (image.getBounds());
    auto count = 0;

    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            count += valueAt (image, x, y) > 127 ? 1 : 0;

    return count;
}

std::unique_ptr<ArtFreqProcessor> makeProcessor (ArtFreqProcessor::Variant variant, const ArtDocument& document)
{
    auto processor = std::make_unique<ArtFreqProcessor> (variant);
    processor->setDocument (document);
    processor->rebuildPlanIfNeeded();
    processor->setPlayConfigDetails (variant == ArtFreqProcessor::Variant::effect ? 2 : 0, 2, 48000.0, 512);
    processor->prepareToPlay (48000.0, 512);
    return processor;
}

ArtDocument documentWithText (const juce::String& text)
{
    ArtDocument document;
    document.objects.push_back (ArtDocument::makeTextObject (text, 3500.0f, 16000.0f));
    return document;
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

} // namespace

ARTFREQ_TEST (emojiFontIsBundled)
{
    const auto typeface = fonts::emojiTypeface();
    CHECK (typeface != nullptr);

    if (typeface == nullptr)
        return;

    CHECK (typeface->getName().containsIgnoreCase ("Noto Emoji"));
    CHECK (typeface->getColourGlyphFormats() == 0);   // outlines, not colour bitmaps

    // The common ones, each with an outline.
    for (const auto c : { fire, heart, thumbsUp, grinning, CodePoint (0x1f602), CodePoint (0x1f44b), CodePoint (0x1f3b5),
                          CodePoint (0x1f3b6), CodePoint (0x2b50), CodePoint (0x1f680), CodePoint (0x1f389), CodePoint (0x1f480),
                          CodePoint (0x1f47d), CodePoint (0x1f608), CodePoint (0x2665), CodePoint (0x263a), CodePoint (0x26a1),
                          CodePoint (0x1f308), CodePoint (0x1f355), CodePoint (0x1f3a7), CodePoint (0x1f399), CodePoint (0x1f941) })
    {
        const auto glyph = typeface->getNominalGlyphForCodepoint (c);
        CHECK (glyph.has_value());

        if (glyph.has_value())
        {
            juce::Path outline;
            typeface->getOutlineForGlyph (static_cast<int> (*glyph), outline);
            CHECK (! outline.isEmpty());
        }
    }

    // None of the art fonts has them, so they need it.
    for (int font = 0; font < fonts::numArtFonts(); ++font)
        CHECK (! fonts::artTypeface (font)->getNominalGlyphForCodepoint (fire).has_value());

    // Its own glyphs or none.
    CHECK (! fonts::emoji (40.0f).getFallbackEnabled());
}

ARTFREQ_TEST (emojiOnlyTextGivesAPath)
{
    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        const auto path = makeTextPath (chars ({ fire }), font);
        CHECK (! path.isEmpty());

        const auto bounds = path.getBounds();
        CHECK_NEAR (bounds.getX(), 0.0f, 1.0e-3f);
        CHECK_NEAR (bounds.getY(), 0.0f, 1.0e-3f);
        CHECK_GREATER (bounds.getWidth(), 20.0f);
        CHECK_GREATER (bounds.getHeight(), 20.0f);

        // Several of them, and a line of its own under ordinary text.
        CHECK_GREATER (boundsOf (chars ({ fire, heart, thumbsUp }), font).getWidth(), bounds.getWidth() * 2.5f);
        CHECK_GREATER (boundsOf ("HI\n" + chars ({ fire }), font).getHeight(), boundsOf ("HI", font).getHeight() * 1.8f);
    }

    // Still nothing for nothing.
    CHECK (makeTextPath ("  \n\t ", 0).isEmpty());
    CHECK (makeTextPath (chars ({ asEmoji }), 0).getBounds().getWidth() < 1.0f);
}

ARTFREQ_TEST (emojiSitsBetweenTheLetters)
{
    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        const auto plain = boundsOf ("AB", font);
        const auto mixed = boundsOf ("A" + chars ({ fire }) + "B", font);
        const auto alone = boundsOf (chars ({ blackSquare }), font);

        // Wider by about the emoji's advance (its box and the space on either side of it).
        CHECK_GREATER (mixed.getWidth(), plain.getWidth() + alone.getWidth());
        CHECK_LESS (mixed.getWidth(), plain.getWidth() + alone.getWidth() * 1.6f);

        // A little taller than the capitals, by as much above them as below the baseline.
        const auto capital = boundsOf ("H", font).getHeight();
        CHECK_NEAR (alone.getHeight(), capital * 1.3f, capital * 0.03f);
        CHECK_NEAR (mixed.getHeight(), alone.getHeight(), capital * 0.03f);
        CHECK_NEAR (boundsOf ("H" + chars ({ blackSquare }), font).getHeight(), alone.getHeight(), 0.5f);   // the capital is inside it

        // Where it is: the capitals leave the top of the line empty, the emoji does not. The
        // ink up there is between the two letters.
        const auto scale = 0.5f;
        const auto path = makeTextPath ("A" + chars ({ blackSquare }) + "B", font);
        const auto bounds = path.getBounds();
        auto image = singleChannel (juce::roundToInt (bounds.getWidth() * scale) + 2, juce::roundToInt (bounds.getHeight() * scale) + 2);
        {
            juce::Graphics g (image);
            g.setColour (juce::Colours::white);
            g.fillPath (path, juce::AffineTransform::scale (scale).translated (1.0f, 1.0f));
        }

        const auto topRows = juce::roundToInt (bounds.getHeight() * scale * 0.06f);
        auto left = image.getWidth(), right = -1;

        for (int x = 0; x < image.getWidth(); ++x)
        {
            if (inkIn (image, { x, 0, 1, topRows }) > 0)
            {
                left = std::min (left, x);
                right = std::max (right, x);
            }
        }

        const auto letterA = boundsOf ("A", font).getWidth() * scale;
        const auto letterB = boundsOf ("B", font).getWidth() * scale;
        CHECK_GREATER (right, left);
        CHECK_GREATER (static_cast<float> (left), letterA);
        CHECK_LESS (static_cast<float> (right), static_cast<float> (image.getWidth()) - letterB);
        CHECK_GREATER (static_cast<float> (right - left), alone.getWidth() * scale * 0.85f);   // (its corners are round)
        CHECK_LESS (static_cast<float> (right - left), alone.getWidth() * scale + 3.0f);

        // And the letters are where they would be in a line of their own: ink at the left and
        // right ends, at the height of the capitals' middle.
        const auto middle = image.getHeight() / 2;
        CHECK (inkIn (image, { 0, middle - 6, juce::roundToInt (letterA), 12 }) > 0);
        CHECK (inkIn (image, { image.getWidth() - juce::roundToInt (letterB), middle - 6, juce::roundToInt (letterB), 12 }) > 0);
    }
}

ARTFREQ_TEST (emojiMixWithTextInEveryFont)
{
    const auto loveNewYork = "I " + chars ({ heart, asEmoji }) + " NY";
    const auto drop = chars ({ fire }) + " DROP " + chars ({ fire });

    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        // The emoji takes room between the words, spaces kept on either side of it.
        const auto withHeart = boundsOf (loveNewYork, font);
        CHECK_GREATER (withHeart.getWidth(), boundsOf ("I  NY", font).getWidth() + boundsOf (chars ({ heart }), font).getWidth());
        CHECK_GREATER (withHeart.getWidth(), boundsOf ("I" + chars ({ heart }) + "NY", font).getWidth() + 10.0f);

        const auto word = boundsOf ("DROP", font);
        const auto framed = boundsOf (drop, font);
        CHECK_GREATER (framed.getWidth(), word.getWidth() + 2.0f * boundsOf (chars ({ fire }), font).getWidth());

        // Both styles draw it.
        for (const auto style : { TextStyle::solid, TextStyle::outline })
        {
            auto image = singleChannel (600, 160);
            renderTextInto (image, image.getBounds().toFloat(), drop, font, style);

            // Ink at both ends (the emoji) and in the middle (the word).
            CHECK_GREATER (inkIn (image, { 0, 0, 110, 160 }), 300);
            CHECK_GREATER (inkIn (image, { 490, 0, 110, 160 }), 300);
            CHECK_GREATER (inkIn (image, { 200, 0, 200, 160 }), 300);
        }
    }

    // Outline: the inside of an emoji is silent, like a letter's.
    auto solid = singleChannel (300, 300), outline = singleChannel (300, 300);
    renderTextInto (solid, solid.getBounds().toFloat(), chars ({ blackSquare }), 0, TextStyle::solid);
    renderTextInto (outline, outline.getBounds().toFloat(), chars ({ blackSquare }), 0, TextStyle::outline);
    CHECK (valueAt (solid, 150, 150) == 255);
    CHECK (valueAt (outline, 150, 150) == 0);
    CHECK_GREATER (inkIn (outline, outline.getBounds()), 500);
}

ARTFREQ_TEST (emojiLeaveOrdinaryTextAlone)
{
    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        // Digits, # and * are in the emoji font too (for keycaps): on their own they are text,
        // as tall as the font's figures and not as an emoji.
        const auto digits = boundsOf ("0123456789#*", font);
        const auto capitals = boundsOf ("H", font);
        CHECK_LESS (digits.getHeight(), capitals.getHeight() * 1.25f);
        CHECK_LESS (digits.getWidth(), 12.0f * boundsOf (chars ({ blackSquare }), font).getWidth() * 0.7f);

        // A keycap is an emoji.
        const auto key = boundsOf (chars ({ '1', asEmoji, keycap }), font);
        CHECK_GREATER (key.getHeight(), boundsOf ("1", font).getHeight() * 1.2f);
    }

    // (c) is a letter where the font has one, an emoji when it is asked to be.
    CHECK_LESS (boundsOf (chars ({ 0xa9 }), 0).getWidth(), boundsOf (chars ({ 0xa9, asEmoji }), 0).getWidth());
}

ARTFREQ_TEST (emojiSequencesAreOneEmoji)
{
    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        const auto box = boundsOf (chars ({ blackSquare }), font);
        const auto one = boundsOf (chars ({ thumbsUp }), font);

        // A skin tone joins its emoji: the same outline, not a second one beside it.
        for (const auto tone : { lightSkin, mediumSkin, CodePoint (0x1f3ff) })
        {
            const auto toned = boundsOf (chars ({ thumbsUp, tone }), font);
            CHECK_NEAR (toned.getWidth(), one.getWidth(), 0.5f);
            CHECK_NEAR (toned.getHeight(), one.getHeight(), 0.5f);
        }

        // ... also where the emoji cannot take one (a face): it is left out, not drawn as a square.
        CHECK_NEAR (boundsOf (chars ({ grinning, lightSkin }), font).getWidth(), boundsOf (chars ({ grinning }), font).getWidth(), 0.5f);

        // A tone on its own is what was typed: its square.
        CHECK_GREATER (boundsOf (chars ({ mediumSkin }), font).getWidth(), box.getWidth() * 0.5f);

        // Keycaps, with or without the selector: one box, not a digit and a frame side by side.
        for (const auto& key : { chars ({ '1', asEmoji, keycap }), chars ({ '1', keycap }), chars ({ '#', asEmoji, keycap }),
                                 chars ({ '*', asEmoji, keycap }) })
        {
            const auto bounds = boundsOf (key, font);
            CHECK_GREATER (bounds.getWidth(), box.getWidth() * 0.8f);
            CHECK_LESS (bounds.getWidth(), box.getWidth() * 1.1f);
        }

        // Joined emoji (a woman, a joiner, a laptop: a woman at a laptop), and a heart with the
        // selector in the middle of the sequence (a heart on fire).
        // (One picture may be a little wider than the box; two side by side are twice it.)
        CHECK_LESS (boundsOf (chars ({ woman, joiner, laptop }), font).getWidth(), box.getWidth() * 1.25f);
        CHECK_LESS (boundsOf (chars ({ woman, mediumSkin, joiner, laptop }), font).getWidth(), box.getWidth() * 1.25f);
        CHECK_LESS (boundsOf (chars ({ heart, asEmoji, joiner, fire }), font).getWidth(), box.getWidth() * 1.25f);
        CHECK_GREATER (boundsOf (chars ({ woman, laptop }), font).getWidth(), box.getWidth() * 1.8f);

        // A flag: two regional indicators, one picture. Three: a flag and a letter.
        const auto flag = boundsOf (chars ({ 0x1f1fa, 0x1f1f8 }), font);
        CHECK_GREATER (flag.getWidth(), box.getWidth() * 0.5f);
        CHECK_LESS (flag.getWidth(), box.getWidth() * 1.25f);
        CHECK_GREATER (boundsOf (chars ({ 0x1f1fa, 0x1f1f8, 0x1f1fa }), font).getWidth(), flag.getWidth() * 1.3f);

        // Two emoji are two.
        CHECK_GREATER (boundsOf (chars ({ thumbsUp, thumbsUp }), font).getWidth(), one.getWidth() * 1.8f);
    }
}

ARTFREQ_TEST (emojiUnknownCharactersDoNoHarm)
{
    // Characters no font here has, and pieces of sequences with nothing to belong to: none may
    // crash or hang, and the text around them is still drawn.
    const juce::String oddities[] = {
        chars ({ 0x10fffd }),                                   // private use, plane 16
        chars ({ 0xe000 }),                                     // private use
        chars ({ 0x1fb00 }),                                    // a block of legacy computing symbols
        chars ({ joiner }),
        chars ({ joiner, joiner, asEmoji }),
        chars ({ 'A', joiner, 'B' }),
        chars ({ 'A', asEmoji }),
        chars ({ fire, joiner }),
        chars ({ fire, joiner, 'A' }),
        chars ({ fire, joiner, 0x10fffd }),
        chars ({ keycap }),
        chars ({ '1', asEmoji }),
        chars ({ 0x1f3f4, 0xe0075, 0xe0073, 0xe0074, 0xe0078, 0xe007f }),   // a flag the font has no picture for
        chars ({ 0xe0067, 0xe007f }),
        chars ({ lightSkin, lightSkin, lightSkin }),
        chars ({ 0x1f1fa }),
        chars ({ fire, 0x0301 }),                               // an accent on an emoji
        chars ({ 0x05d0, fire, 0x05d1 }),                       // right-to-left letters around one
    };

    for (int font = 0; font < fonts::numArtFonts(); ++font)
    {
        for (const auto& odd : oddities)
        {
            const auto alone = makeTextPath (odd, font);
            const auto bounds = alone.getBounds();
            CHECK (bounds.isFinite());

            const auto framed = boundsOf ("AB" + odd + "CD", font);
            CHECK (framed.isFinite());
            CHECK_GREATER (framed.getWidth(), boundsOf ("AB", font).getWidth());

            auto image = singleChannel (200, 80);
            renderTextInto (image, image.getBounds().toFloat(), "AB" + odd, font, TextStyle::outline);
            CHECK_GREATER (inkIn (image, image.getBounds()), 50);
        }
    }

    // A flag with tags the font lacks degrades to its base (the black flag), never a wide row
    // of boxes.
    CHECK_LESS (boundsOf (chars ({ 0x1f3f4, 0xe0075, 0xe0073, 0xe0074, 0xe0078, 0xe007f })).getWidth(),
                boundsOf (chars ({ blackSquare })).getWidth() * 1.25f);

    // Ordinary text is what it always was: one run, shaped in one piece.
    const auto path = makeTextPath ("ArtFreq", 0);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (fonts::art (0, 200.0f), "ArtFreq", 0.0f, 0.0f);
    juce::Path expected;
    glyphs.createPath (expected);
    CHECK_NEAR (path.getBounds().getWidth(), expected.getBounds().getWidth(), 1.0e-3f);
    CHECK_NEAR (path.getBounds().getHeight(), expected.getBounds().getHeight(), 1.0e-3f);
}

ARTFREQ_TEST (emojiSurviveUndoAndASavedProject)
{
    // Emoji outside the basic plane, a sequence, and a second line.
    const auto text = chars ({ fire }) + " DROP " + chars ({ thumbsUp, mediumSkin }) + "\n" + chars ({ '1', asEmoji, keycap, woman, joiner, laptop })
                      + " " + chars ({ heart, asEmoji });

    for (const auto variant : { ArtFreqProcessor::Variant::instrument, ArtFreqProcessor::Variant::effect })
    {
        auto processor = makeProcessor (variant, documentWithText ("before"));
        const auto id = processor->getDocument().objects.front().id;

        processor->editDocument ([&] (ArtDocument& document) { document.find (id)->text = text; });
        CHECK (processor->getDocument().find (id)->text == text);

        CHECK (processor->undo());
        CHECK (processor->getDocument().find (id)->text == "before");
        CHECK (processor->redo());
        CHECK (processor->getDocument().find (id)->text == text);

        juce::MemoryBlock state;
        processor->getStateInformation (state);

        auto loaded = makeProcessor (variant, ArtDocument());
        loaded->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        CHECK (loaded->getDocument().objects.size() == 1);

        if (loaded->getDocument().objects.size() == 1)
        {
            const auto& object = loaded->getDocument().objects.front();
            CHECK (object.text == text);
            CHECK (object.text.length() == text.length());
            CHECK (ui::actions::layerName (object) == text.upToFirstOccurrenceOf ("\n", false, false));
        }

        // The document's own round trip, too.
        const auto again = ArtDocument::fromValueTree (processor->getDocument().toValueTree());
        CHECK (again.objects.size() == 1 && again.objects.front().text == text);
    }
}

ARTFREQ_TEST (emojiAreDrawnAndCarved)
{
    // An emoji-only text: on the canvas (what the instrument plays), and in the masks ArtFreq FX
    // carves with.
    const auto document = documentWithText (chars ({ fire, heart, asEmoji }));

    Compositor compositor;
    Canvas canvas;
    compositor.render (document, FrequencyScale(), canvas);
    CHECK_GREATER (std::count_if (canvas.values.begin(), canvas.values.end(), [] (float v) { return v > 0.5f; }), 5000);

    auto instrument = makeProcessor (ArtFreqProcessor::Variant::instrument, document);
    const auto plan = instrument->getCurrentPlan();
    CHECK (plan != nullptr);

    auto effect = makeProcessor (ArtFreqProcessor::Variant::effect, document);
    const auto masks = effect->getCurrentMasks();
    CHECK (masks != nullptr && masks->exact != nullptr && ! masks->exact->empty);

    if (masks != nullptr && masks->exact != nullptr)
    {
        const auto& values = masks->exact->values;
        CHECK_GREATER (std::count_if (values.begin(), values.end(), [] (float v) { return v > 0.5f; }), 5000);
    }

    // Nothing to carve without the emoji: the mask is the emoji.
    auto blank = makeProcessor (ArtFreqProcessor::Variant::effect, documentWithText (" "));
    CHECK (blank->getCurrentMasks() != nullptr && blank->getCurrentMasks()->exact->empty);
}

ARTFREQ_TEST (emojiTypedAndPastedStayInTheTextBox)
{
    for (const auto variant : { ArtFreqProcessor::Variant::instrument, ArtFreqProcessor::Variant::effect })
    {
        auto processor = makeProcessor (variant, documentWithText ("Hi"));
        const auto id = processor->getDocument().objects.front().id;
        const auto textNow = [&] { return processor->getDocument().find (id)->text; };

        std::unique_ptr<ArtFreqEditor> editor (dynamic_cast<ArtFreqEditor*> (processor->createEditor()));
        CHECK (editor != nullptr);

        if (editor == nullptr)
            continue;

        editor->selectObject (id);
        auto* field = dynamic_cast<juce::TextEditor*> (findComponent (*editor, "inspector-text"));
        CHECK (field != nullptr);

        if (field == nullptr)
            continue;

        CHECK_EVENTUALLY (field->isVisible() && field->getText() == "Hi");

        // Pasted (a paste inserts at the caret): a sequence and an emoji outside the basic plane.
        const auto pasted = " " + chars ({ thumbsUp, mediumSkin, fire });
        field->moveCaretToEnd();
        field->insertTextAtCaret (pasted);
        CHECK_EVENTUALLY (textNow() == "Hi" + pasted);
        CHECK (field->getText() == "Hi" + pasted);

        // Typed: a key press carries the character.
        field->keyPressed (juce::KeyPress (0, juce::ModifierKeys(), heart));
        CHECK_EVENTUALLY (textNow() == "Hi" + pasted + chars ({ heart }));

        // Typed on Windows: the two halves of U+1F525's surrogate pair, one key press each.
        field->keyPressed (juce::KeyPress (0, juce::ModifierKeys(), static_cast<juce::juce_wchar> (0xd83d)));
        field->keyPressed (juce::KeyPress (0, juce::ModifierKeys(), static_cast<juce::juce_wchar> (0xdd25)));
        const auto typed = "Hi" + pasted + chars ({ heart, fire });
        CHECK_EVENTUALLY (textNow() == typed);
        CHECK (field->getText() == typed);
        CHECK (textNow().length() == 2 + 1 + 3 + 2);

        // Half a pair on its own is nothing; what follows it is typed as usual.
        field->keyPressed (juce::KeyPress (0, juce::ModifierKeys(), static_cast<juce::juce_wchar> (0xdd25)));
        field->keyPressed (juce::KeyPress (0, juce::ModifierKeys(), static_cast<juce::juce_wchar> (0xd83d)));
        field->keyPressed (juce::KeyPress ('!', juce::ModifierKeys(), '!'));
        CHECK_EVENTUALLY (textNow() == typed + "!");

        // The layer list's label is the text, and the drawing has the emoji in it.
        CHECK (ui::actions::layerName (*processor->getDocument().find (id)) == typed + "!");

        // Undo and redo, through the document's history.
        processor->endCoalescing();
        CHECK (processor->undo());
        CHECK_EVENTUALLY (field->getText() == textNow());
        CHECK (textNow() != typed + "!");
        CHECK (processor->redo());
        CHECK_EVENTUALLY (field->getText() == typed + "!");
    }
}
