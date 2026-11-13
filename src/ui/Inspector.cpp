#include "Inspector.h"

#include "Actions.h"
#include "CanvasPlanImage.h"
#include "Theme.h"
#include "Widgets.h"
#include "core/Compositor.h"

#include <cmath>

namespace freqart::ui
{
namespace
{
    // Logical pixels
    constexpr int pad = 16;
    constexpr int titleHeight = 22;
    constexpr int labelHeight = 18;
    constexpr int controlHeight = 28;
    constexpr int sectionGap = 10;
    constexpr int textFieldHeight = 64;
    constexpr int thumbnailHeight = 96;
    constexpr int thumbnailSource = 384;  // the thumbnail is worked out from a copy this size at most

    constexpr float fillStart = 0.04f;    // "Fill length"
    constexpr float fillEnd = 0.96f;

    constexpr juce::uint32 wheelGestureMs = 1000;   // wheel steps closer together are one undo step

    const juce::StringArray imageModeNames { "Auto", "Original", "Negative", "Outline" };

    juce::String percent (double value) { return juce::String (juce::roundToInt (value)) + "%"; }

    /** "3.5 kHz – 16 kHz" (an en dash: a range, not a minus). */
    juce::String hzRange (float low, float high)
    {
        return formatHz (low) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 ")) + formatHz (high);
    }

    /** A text field whose focus sessions are the undo steps of typing, and which leaves undo and
        redo to the document's history (its own would undo behind the document's back). */
    class InspectorTextField final : public juce::TextEditor
    {
    public:
        std::function<void()> onFocusGained;

        bool keyPressed (const juce::KeyPress& key) override
        {
            const auto code = key.getKeyCode();
            const auto mods = key.getModifiers();

            if (mods.isCommandDown() && (code == 'z' || code == 'Z' || code == 'y' || code == 'Y'))
                return false;  // up to the editor: the document's undo

            return juce::TextEditor::keyPressed (key);
        }

        void focusGained (FocusChangeType cause) override
        {
            juce::TextEditor::focusGained (cause);

            if (onFocusGained != nullptr)
                onFocusGained();
        }
    };

    /** A bar that edits the selected object. Each mouse-wheel step reaches the slider's listeners
        as a drag of its own; this tells them apart from real drags. */
    class ObjectSlider final : public juce::Slider
    {
    public:
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            const juce::ScopedValueSetter<bool> scope (wheelMoving, true);
            juce::Slider::mouseWheelMove (e, wheel);
        }

        bool isWheelMoving() const noexcept { return wheelMoving; }

    private:
        bool wheelMoving = false;
    };

    /** "Delete": quiet until hovered, then the danger colour. */
    class InspectorDeleteButton final : public juce::Button
    {
    public:
        InspectorDeleteButton() : juce::Button ("Delete")
        {
            setTooltip ("Delete the selected object (Delete key)");
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat();
            const auto hot = highlighted || down;

            g.setColour (hot ? theme::danger.withAlpha (down ? 0.24f : 0.14f) : theme::raised);
            g.fillRoundedRectangle (bounds, theme::controlCorner);
            g.setColour (hot ? theme::danger.withAlpha (0.8f) : theme::line);
            g.drawRoundedRectangle (bounds.reduced (0.5f), theme::controlCorner, 1.0f);

            const auto colour = hot ? theme::danger : theme::textDim;
            const auto font = fonts::ui (theme::controlSize, fonts::UiWeight::medium);
            const auto label = getButtonText();
            const auto textWidth = juce::GlyphArrangement::getStringWidth (font, label);
            constexpr float iconSize = 16.0f, gap = 6.0f;
            const auto left = bounds.getCentreX() - (iconSize + gap + textWidth) * 0.5f;

            auto icon = icons::trash();
            const auto iconBox = juce::Rectangle<float> (left, bounds.getCentreY() - iconSize * 0.5f, iconSize, iconSize);
            g.setColour (colour);
            g.fillPath (icon, icon.getTransformToScaleToFit (iconBox, true));

            g.setFont (font);
            g.drawText (label, juce::Rectangle<float> (left + iconSize + gap, bounds.getY(), textWidth + 2.0f, bounds.getHeight()),
                        juce::Justification::centredLeft, false);
        }
    };
} // namespace

//==============================================================================
struct Inspector::Impl
{
    enum class Mode
    {
        drawing,
        text,
        image
    };

    Impl (Inspector& ownerToUse, FreqArtProcessor& processorToUse, EditorState& stateToUse)
        : owner (ownerToUse), processor (processorToUse), state (stateToUse),
          addTextButton ("Add text", icons::text(), "Add text"),
          addImageButton ("Add image", icons::image(), "Add image..."),
          styleControl ({ "Solid", "Outline" }),
          bandControl ({ "Low", "Mid", "High", "Full" }),
          modeControl (imageModeNames)
    {
        // ---- nothing selected ----
        addTextButton.setTooltip ("Add a text object (in the mid band, or another band if that is taken)");
        addTextButton.onClick = [this] { actions::addText (processor, state); };
        addImageButton.setTooltip ("Add a picture (" + actions::pictureFormatNames() + ")");
        addImageButton.onClick = [this] { actions::chooseImage (processor, state, owner, false); };

        setUpSlider (sizeSlider, 2.0, 64.0, 1.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " px"; });
        setUpSlider (softnessSlider, 0.0, 100.0, 1.0, percent);
        setUpSlider (strengthSlider, 0.0, 100.0, 1.0, percent);
        sizeSlider.onValueChange = [this] { if (! updating) state.setBrushSize (static_cast<float> (sizeSlider.getValue())); };
        softnessSlider.onValueChange = [this] { if (! updating) state.setBrushSoftness (static_cast<float> (softnessSlider.getValue() / 100.0)); };
        strengthSlider.onValueChange = [this] { if (! updating) state.setBrushStrength (static_cast<float> (strengthSlider.getValue() / 100.0)); };
        sizeSlider.setTitle ("Brush size");
        softnessSlider.setTitle ("Brush softness");
        strengthSlider.setTitle ("Brush strength");
        sizeSlider.setDoubleClickReturnValue (true, 16.0);
        softnessSlider.setDoubleClickReturnValue (true, 50.0);
        strengthSlider.setDoubleClickReturnValue (true, 100.0);

        // ---- text ----
        textField.setMultiLine (true, true);
        textField.setReturnKeyStartsNewLine (true);
        textField.setScrollbarsShown (true);
        textField.setFont (fonts::ui (theme::controlSize));
        textField.setIndents (8, 6);
        textField.setTitle ("Text");
        textField.onTextChange = [this] { textTyped(); };
        textField.onFocusGained = [this] { ++typingSession; };
        textField.onFocusLost = [this] { processor.endCoalescing(); };
        textField.onEscapeKey = [this] { textField.giveAwayKeyboardFocus(); };

        for (int i = 0; i < fonts::numArtFonts(); ++i)
            fontBox.addItem (fonts::artFontName (i), i + 1);

        fontBox.setTitle ("Font");
        fontBox.onChange = [this]
        {
            const auto index = fontBox.getSelectedId() - 1;

            if (! updating && index >= 0)
                editSelected ([index] (ArtObject& o) { o.fontIndex = index; });
        };

        styleControl.setTitle ("Style");
        styleControl.onChange = [this] (int index)
        {
            if (! updating && index >= 0)
                editSelected ([index] (ArtObject& o) { o.textStyle = static_cast<TextStyle> (index); });
        };

        // ---- shared by text and images ----
        bandControl.setTitle ("Band");
        bandControl.onChange = [this] (int index)
        {
            if (! updating && index >= 0)
                actions::setSelectedBand (processor, state, static_cast<Band> (index));
        };

        fillButton.setButtonText ("Fill length");
        fillButton.setTooltip ("Stretch across the whole length (4 % to 96 %)");
        fillButton.onClick = [this] { editSelected ([] (ArtObject& o) { o.startTime = fillStart; o.endTime = fillEnd; }); };

        setUpSlider (intensitySlider, 0.0, 100.0, 1.0, percent);
        intensitySlider.setTitle ("Intensity");
        intensitySlider.setDoubleClickReturnValue (true, 100.0);
        setUpObjectSlider (intensitySlider, [] (ArtObject& o, double v) { o.intensity = static_cast<float> (v / 100.0); });

        deleteButton.onClick = [this] { actions::deleteSelected (processor, state); };

        // ---- image ----
        modeControl.setTitle ("Mode");
        modeControl.onChange = [this] (int index)
        {
            if (! updating && index >= 0)
                editSelected ([index] (ArtObject& o) { o.imageMode = static_cast<ImageMode> (index); });
        };

        setUpSlider (cleanupSlider, 0.0, 90.0, 1.0, percent);
        cleanupSlider.setTitle ("Clean-up");
        cleanupSlider.setDoubleClickReturnValue (true, 8.0);
        cleanupSlider.setTooltip ("Silences faint backgrounds, grain and JPEG noise");
        setUpObjectSlider (cleanupSlider, [] (ArtObject& o, double v) { o.cleanup = static_cast<float> (v / 100.0); });

        replaceButton.setButtonText ("Replace...");
        replaceButton.setTooltip ("Choose another picture for this object");
        replaceButton.onClick = [this] { actions::chooseImage (processor, state, owner, true); };

        // Ids for the snapshot tool and tests, which drive the inspector like a user would.
        addTextButton.setComponentID ("inspector-add-text");
        addImageButton.setComponentID ("inspector-add-image");
        sizeSlider.setComponentID ("inspector-size");
        softnessSlider.setComponentID ("inspector-softness");
        strengthSlider.setComponentID ("inspector-strength");
        textField.setComponentID ("inspector-text");
        fontBox.setComponentID ("inspector-font");
        styleControl.setComponentID ("inspector-style");
        bandControl.setComponentID ("inspector-band");
        fillButton.setComponentID ("inspector-fill");
        intensitySlider.setComponentID ("inspector-intensity");
        deleteButton.setComponentID ("inspector-delete");
        modeControl.setComponentID ("inspector-mode");
        cleanupSlider.setComponentID ("inspector-cleanup");
        replaceButton.setComponentID ("inspector-replace");

        for (auto* c : allControls())
            owner.addChildComponent (c);
    }

    std::vector<juce::Component*> allControls()
    {
        return { &addTextButton, &addImageButton, &sizeSlider, &softnessSlider, &strengthSlider,
                 &textField, &fontBox, &styleControl, &bandControl, &fillButton, &intensitySlider,
                 &deleteButton, &modeControl, &cleanupSlider, &replaceButton };
    }

    void setUpSlider (juce::Slider& slider, double low, double high, double step, std::function<juce::String (double)> format)
    {
        slider.setSliderStyle (juce::Slider::LinearBar);
        slider.setRange (low, high, step);
        slider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
        slider.textFromValueFunction = std::move (format);
        slider.valueFromTextFunction = [] (const juce::String& text) { return text.retainCharacters ("0123456789.-").getDoubleValue(); };
        slider.setVelocityBasedMode (false);
        slider.updateText();
    }

    /** A slider editing the selected object: a drag (or a click) is one undo step, and so is a
        run of mouse-wheel steps on the same bar and object without a pause (a trackpad swipe
        sends dozens). Any other edit in between starts a new step, as its own key ends the
        coalescing. */
    void setUpObjectSlider (ObjectSlider& slider, std::function<void (ArtObject&, double)> apply)
    {
        slider.onDragStart = [this, &slider]
        {
            const auto now = juce::Time::getMillisecondCounter();
            const auto wheel = slider.isWheelMoving();
            const auto sameWheelGesture = wheel && wheelKey.isNotEmpty() && wheelSlider == &slider
                                       && wheelObject == state.getSelected() && now - lastWheelTime < wheelGestureMs;

            sliderKey = sameWheelGesture ? wheelKey : "inspector-slider-" + juce::String (++editCounter);
            wheelKey = wheel ? sliderKey : juce::String();
            wheelSlider = &slider;
            wheelObject = state.getSelected();
            lastWheelTime = now;
        };
        slider.onDragEnd = [this, &slider]
        {
            // A wheel gesture stays open for the next step; a drag ends here.
            if (! slider.isWheelMoving())
                processor.endCoalescing();

            sliderKey.clear();
        };
        slider.onValueChange = [this, &slider, apply]
        {
            if (updating)
                return;

            const auto value = slider.getValue();
            editSelected ([apply, value] (ArtObject& o) { apply (o, value); }, sliderKey);
        };
    }

    // ---- edits -------------------------------------------------------------------------

    const ArtObject* selectedObject() const
    {
        return state.hasSelection() ? processor.getDocument().find (state.getSelected()) : nullptr;
    }

    void editSelected (std::function<void (ArtObject&)> change, const juce::String& key = {})
    {
        const auto id = state.getSelected();

        if (id.isNull() || processor.getDocument().find (id) == nullptr)
            return;

        processor.editDocument ([id, change] (ArtDocument& document)
        {
            if (auto* object = document.find (id))
                change (*object);
        }, key);
    }

    void textTyped()
    {
        if (updating)
            return;

        const auto text = textField.getText();
        lastWrittenText = text;
        editSelected ([text] (ArtObject& o) { o.text = text; }, "inspector-text-" + juce::String (typingSession));
    }

    // ---- keeping in step with the document and the editor state -------------------------

    void refresh()
    {
        const auto* object = selectedObject();
        const auto newMode = object == nullptr ? Mode::drawing
                           : object->type == ArtObject::Type::text ? Mode::text : Mode::image;
        const auto newId = object != nullptr ? object->id : juce::Uuid::null();
        const auto tool = state.getTool();

        if (needsLayout || newMode != mode || newId != shownId || tool != shownTool)
        {
            needsLayout = false;

            if (newId != shownId)
            {
                ++typingSession;  // another object: another undo step
                lastWrittenText = object != nullptr ? object->text : juce::String();
            }

            mode = newMode;
            shownId = newId;
            shownTool = tool;
            brushControlsShown = newMode == Mode::drawing && tool != Tool::select;
            updateVisibility();
            layout();
            owner.repaint();
        }

        const juce::ScopedValueSetter<bool> guard (updating, true);

        switch (mode)
        {
            case Mode::drawing: updateDrawing(); break;
            case Mode::text:    updateText (*object); break;
            case Mode::image:   updateImage (*object); break;
        }
    }

    void updateVisibility()
    {
        const auto drawing = mode == Mode::drawing;
        const auto text = mode == Mode::text;
        const auto image = mode == Mode::image;

        addTextButton.setVisible (drawing);
        addImageButton.setVisible (drawing);
        sizeSlider.setVisible (drawing && brushControlsShown);
        softnessSlider.setVisible (drawing && brushControlsShown);
        strengthSlider.setVisible (drawing && brushControlsShown);

        textField.setVisible (text);
        fontBox.setVisible (text);
        styleControl.setVisible (text);

        modeControl.setVisible (image);
        cleanupSlider.setVisible (image);
        replaceButton.setVisible (image);

        bandControl.setVisible (! drawing);
        fillButton.setVisible (! drawing);
        intensitySlider.setVisible (! drawing);
        deleteButton.setVisible (! drawing);
    }

    void setTitleRow (const juce::String& newTitle, const juce::String& newReadout)
    {
        if (newTitle != title || newReadout != readout)
        {
            title = newTitle;
            readout = newReadout;
            owner.repaint (titleArea);
        }
    }

    static void setSliderValue (juce::Slider& slider, double value)
    {
        // Not while it is being dragged: the drag is what changes the value.
        if (! slider.isMouseButtonDown() && ! juce::approximatelyEqual (slider.getValue(), value))
            slider.setValue (value, juce::dontSendNotification);
    }

    void updateDrawing()
    {
        const auto count = static_cast<int> (processor.getDocument().objects.size());
        const auto painted = processor.getDocument().hasPaint();
        auto summary = count == 0 ? juce::String() : juce::String (count) + (count == 1 ? " object" : " objects");

        if (painted)
            summary << (summary.isEmpty() ? "" : " + ") << "paint";

        setTitleRow ("Drawing", summary);

        // Brush settings mirror the toolbar's.
        setSliderValue (sizeSlider, static_cast<double> (state.getBrushSize()));
        setSliderValue (softnessSlider, static_cast<double> (state.getBrushSoftness()) * 100.0);
        setSliderValue (strengthSlider, static_cast<double> (state.getBrushStrength()) * 100.0);
    }

    void updateShared (const ArtObject& object)
    {
        bandControl.setSelectedIndex (actions::bandOf (object, processor.getScale()), juce::dontSendNotification);
        setSliderValue (intensitySlider, static_cast<double> (object.intensity) * 100.0);

        const auto time = percent (object.startTime * 100.0) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 "))
                        + percent (object.endTime * 100.0);

        if (time != timeReadout)
        {
            timeReadout = time;
            owner.repaint (timeReadoutArea);
        }
    }

    void updateText (const ArtObject& object)
    {
        setTitleRow ("Text", hzRange (object.lowHz, object.highHz));

        // Only an outside change (undo, redo) replaces the field's text, and the caret stays
        // where it was: typing never round-trips through here.
        if (textField.getText() != object.text)
        {
            const auto caret = textField.getCaretPosition();
            textField.setText (object.text, juce::dontSendNotification);
            textField.setCaretPosition (juce::jmin (caret, object.text.length()));

            if (object.text != lastWrittenText)
                ++typingSession;  // typing after an undo starts a new step

            lastWrittenText = object.text;
        }

        fontBox.setSelectedId (juce::jlimit (0, fonts::numArtFonts() - 1, object.fontIndex) + 1, juce::dontSendNotification);
        styleControl.setSelectedIndex (static_cast<int> (object.textStyle), juce::dontSendNotification);
        updateShared (object);
    }

    void updateImage (const ArtObject& object)
    {
        setTitleRow ("Image", hzRange (object.lowHz, object.highHz));
        updateThumbnail (object);

        const auto hint = object.imageMode == ImageMode::automatic ? autoHint : juce::String();

        if (hint != shownModeHint)
        {
            shownModeHint = hint;
            owner.repaint (modeCaptionArea);
        }

        modeControl.setSelectedIndex (static_cast<int> (object.imageMode), juce::dontSendNotification);
        setSliderValue (cleanupSlider, static_cast<double> (object.cleanup) * 100.0);
        updateShared (object);
    }

    // ---- the thumbnail -------------------------------------------------------------------

    void updateThumbnail (const ArtObject& object)
    {
        const auto* pixels = object.image.isValid() ? static_cast<const void*> (object.image.getPixelData().get()) : nullptr;
        const auto cleanupMillis = juce::roundToInt (object.cleanup * 1000.0f);

        if (pixels != thumbnailPixels)
        {
            thumbnailPixels = pixels;
            thumbnailOriginal = object.image;  // keeps the pixels (and so the key) alive
            thumbnailSmall = {};
            autoHint = {};

            if (object.image.isValid())
            {
                // What Auto picks, shown beside the Mode caption (the segment itself stays "Auto",
                // so the four options read evenly): a shape on transparency is drawn by its alpha,
                // a light background is inverted, anything else is kept.
                autoHint = hasMeaningfulTransparency (object.image) ? "Auto uses Transparency"
                         : resolveAutoMode (object.image) == ImageMode::negative ? "Auto uses Negative"
                                                                                  : "Auto uses Original";

                const auto w = object.image.getWidth(), h = object.image.getHeight();
                const auto shrink = juce::jmin (1.0, static_cast<double> (thumbnailSource) / juce::jmax (w, h));
                thumbnailSmall = shrink < 1.0 ? object.image.rescaled (juce::jmax (1, juce::roundToInt (w * shrink)),
                                                                       juce::jmax (1, juce::roundToInt (h * shrink)),
                                                                       juce::Graphics::highResamplingQuality)
                                              : object.image;
            }

            thumbnailMode = -1;  // forces the picture below
        }

        // The loudness values: worked out again only when the mode or the clean-up changes.
        if (static_cast<int> (object.imageMode) != thumbnailMode || cleanupMillis != thumbnailCleanup)
        {
            thumbnailMode = static_cast<int> (object.imageMode);
            thumbnailCleanup = cleanupMillis;
            thumbnailValues = thumbnailSmall.isValid() ? processImage (thumbnailSmall, object.imageMode, object.cleanup)
                                                       : ProcessedImage();
            thumbnailContrast = -1.0f;  // forces the colours below
        }

        // The colours, as the canvas shows them: a value becomes decibels through the Contrast,
        // seen through the canvas's colour window. So the thumbnail and the canvas agree.
        const auto contrast = processor.getPlanSettings().contrastDb;

        if (juce::exactlyEqual (contrast, thumbnailContrast))
            return;

        thumbnailContrast = contrast;
        thumbnail = {};

        if (thumbnailValues.isValid())
        {
            const auto& processed = thumbnailValues;
            thumbnail = juce::Image (juce::Image::ARGB, processed.width, processed.height, false);
            juce::Image::BitmapData data (thumbnail, juce::Image::BitmapData::writeOnly);
            const auto& table = theme::spectrumTable();
            const auto dbPerUnit = contrast / CanvasPlanImage::windowDb;

            for (int y = 0; y < processed.height; ++y)
            {
                auto* row = reinterpret_cast<juce::PixelARGB*> (data.getLinePointer (y));

                for (int x = 0; x < processed.width; ++x)
                {
                    const auto v = processed.at (x, y);
                    const auto t = v > 0.001f ? 1.0f + (v - 1.0f) * dbPerUnit : 0.0f;
                    row[x] = table[static_cast<size_t> (juce::jlimit (0, 255, juce::roundToInt (t * 255.0f)))];
                }
            }
        }

        owner.repaint (thumbnailArea);
    }

    // ---- layout and painting -------------------------------------------------------------

    struct Caption
    {
        juce::String text;
        juce::Rectangle<int> area;
        bool heading = false;  // a section title (with a divider) rather than a control's label
    };

    void layout()
    {
        captions.clear();
        helpArea = {};
        shortcutsArea = {};
        thumbnailArea = {};
        timeReadoutArea = {};
        modeCaptionArea = {};

        auto area = owner.getLocalBounds().reduced (pad);
        titleArea = area.removeFromTop (titleHeight).expanded (pad, 4);
        area.removeFromTop (14);

        const auto section = [&] (const juce::String& caption, int height)
        {
            captions.push_back ({ caption, area.removeFromTop (labelHeight) });
            auto r = area.removeFromTop (height);
            area.removeFromTop (sectionGap);
            return r;
        };

        if (mode == Mode::drawing)
        {
            auto row = area.removeFromTop (controlHeight);
            const auto half = (row.getWidth() - 8) / 2;
            addTextButton.setBounds (row.removeFromLeft (half));
            addImageButton.setBounds (row.removeFromRight (half));
            area.removeFromTop (14);
            helpArea = area.removeFromTop (76);
            area.removeFromTop (sectionGap);

            if (! brushControlsShown)
            {
                captions.push_back ({ "Keys", area.removeFromTop (34), true });
                shortcutsArea = area.removeFromTop (static_cast<int> (std::size (shortcuts)) * shortcutRowHeight);
            }
            else
            {
                captions.push_back ({ state.getTool() == Tool::erase ? "Eraser" : "Brush", area.removeFromTop (34), true });
                area.removeFromTop (4);
                sizeSlider.setBounds (section ("Size", controlHeight));
                softnessSlider.setBounds (section ("Softness", controlHeight));
                strengthSlider.setBounds (section ("Strength", controlHeight));  // Erase: how much it removes
            }

            return;
        }

        if (mode == Mode::text)
        {
            textField.setBounds (section ("Text", textFieldHeight));
            fontBox.setBounds (section ("Font", controlHeight));
            styleControl.setBounds (section ("Style", controlHeight));
        }
        else
        {
            thumbnailArea = area.removeFromTop (thumbnailHeight);
            area.removeFromTop (sectionGap + 2);
            modeControl.setBounds (section ("Mode", controlHeight));
            modeCaptionArea = captions.back().area;
            cleanupSlider.setBounds (section ("Clean-up", controlHeight));
        }

        bandControl.setBounds (section ("Band", controlHeight));

        auto position = section ("Position", controlHeight);
        fillButton.setBounds (position.removeFromLeft (104));
        timeReadoutArea = position.withTrimmedLeft (10);

        intensitySlider.setBounds (section ("Intensity", controlHeight));

        auto buttons = area.removeFromTop (controlHeight);

        if (mode == Mode::image)
        {
            const auto half = (buttons.getWidth() - 8) / 2;
            replaceButton.setBounds (buttons.removeFromLeft (half));
            deleteButton.setBounds (buttons.removeFromRight (half));
        }
        else
        {
            deleteButton.setBounds (buttons);
        }
    }

    void paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);
        g.setColour (theme::line);
        g.fillRect (0, 0, 1, owner.getHeight());

        auto titleRow = owner.getLocalBounds().reduced (pad).removeFromTop (titleHeight);
        g.setColour (theme::text);
        g.setFont (fonts::ui (theme::titleSize, fonts::UiWeight::semiBold));
        g.drawText (title, titleRow, juce::Justification::centredLeft, false);

        if (readout.isNotEmpty())
        {
            // Frequencies in mono (numbers line up); the drawing's summary is words.
            g.setColour (mode == Mode::drawing ? theme::textFaint : theme::textDim);
            g.setFont (mode == Mode::drawing ? fonts::ui (12.0f) : fonts::mono (11.0f));
            g.drawText (readout, titleRow, juce::Justification::centredRight, false);
        }

        g.setFont (theme::labelFont());

        for (const auto& caption : captions)
        {
            if (caption.heading)
            {
                g.setColour (theme::line);
                g.fillRect (caption.area.getX(), caption.area.getY(), caption.area.getWidth(), 1);
                g.setColour (theme::text);
                g.setFont (fonts::ui (theme::controlSize, fonts::UiWeight::semiBold));
                g.drawText (caption.text, caption.area.withTrimmedTop (8), juce::Justification::centredLeft, false);
                g.setFont (theme::labelFont());
                continue;
            }

            g.setColour (theme::textDim);
            g.drawText (caption.text.toUpperCase(), caption.area.withTrimmedBottom (4), juce::Justification::bottomLeft, false);
        }

        if (! helpArea.isEmpty())
        {
            g.setColour (theme::textDim);
            g.setFont (fonts::ui (12.5f));
            g.drawFittedText ("Click an object on the canvas to change it: drag to move, drag a handle to resize. "
                              "Brush and Erase paint freely. Drop a picture on the canvas to add it there.",
                              helpArea, juce::Justification::topLeft, 5, 1.0f);
        }

        if (! shortcutsArea.isEmpty())
            paintShortcuts (g);

        if (! timeReadoutArea.isEmpty())
        {
            // "4% – 50% of the length": the numbers in mono, the words quieter.
            const auto numbers = fonts::mono (11.0f);
            const auto words = fonts::ui (11.5f);
            const auto numbersWidth = juce::GlyphArrangement::getStringWidth (numbers, timeReadout);
            auto r = timeReadoutArea.toFloat();

            g.setColour (theme::textDim);
            g.setFont (numbers);
            g.drawText (timeReadout, r.removeFromLeft (numbersWidth + 1.0f), juce::Justification::centredLeft, false);
            g.setColour (theme::textFaint);
            g.setFont (words);
            g.drawText (" of the length", r, juce::Justification::centredLeft, true);
        }

        if (! modeCaptionArea.isEmpty() && shownModeHint.isNotEmpty())
        {
            g.setColour (theme::textFaint);
            g.setFont (fonts::ui (11.5f));
            g.drawText (shownModeHint, modeCaptionArea.withTrimmedBottom (4), juce::Justification::bottomRight, false);
        }

        if (! thumbnailArea.isEmpty())
            paintThumbnail (g);
    }

    void paintShortcuts (juce::Graphics& g)
    {
        // Key caps drawn like the help panel's, so the two lists read as one system.
        const auto keyFont = fonts::ui (theme::labelSize, fonts::UiWeight::semiBold);
        auto keyColumn = 0.0f;

        for (const auto& shortcut : shortcuts)
            keyColumn = juce::jmax (keyColumn, juce::GlyphArrangement::getStringWidth (keyFont, shortcut.keys) + 12.0f);

        auto area = shortcutsArea.toFloat();

        for (const auto& shortcut : shortcuts)
        {
            auto row = area.removeFromTop (static_cast<float> (shortcutRowHeight));
            const auto keyWidth = juce::GlyphArrangement::getStringWidth (keyFont, shortcut.keys) + 12.0f;
            const auto keyBox = juce::Rectangle<float> (row.getX(), row.getCentreY() - 10.0f, keyWidth, 20.0f);

            g.setColour (theme::raised);
            g.fillRoundedRectangle (keyBox, 4.0f);
            g.setColour (theme::line);
            g.drawRoundedRectangle (keyBox.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (theme::text);
            g.setFont (keyFont);
            g.drawText (shortcut.keys, keyBox, juce::Justification::centred, false);

            g.setColour (theme::textDim);
            g.setFont (fonts::ui (12.5f));
            g.drawText (shortcut.action, row.withTrimmedLeft (keyColumn + 10.0f), juce::Justification::centredLeft, true);
        }
    }

    void paintThumbnail (juce::Graphics& g)
    {
        const auto box = thumbnailArea.toFloat();
        g.setColour (theme::spectrum (0.0f));
        g.fillRoundedRectangle (box, theme::controlCorner);

        if (thumbnail.isValid())
        {
            const auto target = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                    .appliedTo (thumbnail.getBounds().toFloat(), box.reduced (6.0f));
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (thumbnail, target);
        }

        g.setColour (theme::line);
        g.drawRoundedRectangle (box.reduced (0.5f), theme::controlCorner, 1.0f);
    }

    // ---- state ---------------------------------------------------------------------------

    Inspector& owner;
    FreqArtProcessor& processor;
    EditorState& state;

    Mode mode = Mode::drawing;
    juce::Uuid shownId = juce::Uuid::null();
    Tool shownTool = Tool::select;
    bool brushControlsShown = false;
    bool updating = false;
    bool needsLayout = true;  // nothing is shown until the first refresh
    bool installedFocusCallback = false;

    juce::String title { "Drawing" }, readout, timeReadout;
    juce::Rectangle<int> titleArea, helpArea, thumbnailArea, timeReadoutArea, shortcutsArea;

    struct Shortcut
    {
        const char* keys;
        const char* action;
    };

    static constexpr int shortcutRowHeight = 28;
    static constexpr Shortcut shortcuts[] {
        { "Delete", "Remove the selection" },
        { "Arrows", "Nudge (Shift: 10 px)" },
        { "Shift-drag", "Move on one axis" },
        { "Esc", "Deselect" },
        { "Double-click", "Edit the text" },
    };
    std::vector<Caption> captions;

    int typingSession = 0;
    int editCounter = 0;
    juce::String lastWrittenText;
    juce::String sliderKey;

    // The open mouse-wheel gesture (see setUpObjectSlider): its undo key, bar, object and time.
    juce::String wheelKey;
    const juce::Slider* wheelSlider = nullptr;
    juce::Uuid wheelObject = juce::Uuid::null();
    juce::uint32 lastWheelTime = 0;

    // Thumbnail cache: rebuilt only when the picture, its mode or its clean-up change.
    const void* thumbnailPixels = nullptr;
    juce::Image thumbnailOriginal, thumbnailSmall, thumbnail;
    ProcessedImage thumbnailValues;
    int thumbnailMode = -1;
    int thumbnailCleanup = -1;
    float thumbnailContrast = -1.0f;
    juce::String autoHint, shownModeHint;
    juce::Rectangle<int> modeCaptionArea;

    // Controls
    IconButton addTextButton, addImageButton;
    juce::Slider sizeSlider, softnessSlider, strengthSlider;
    InspectorTextField textField;
    juce::ComboBox fontBox;
    SegmentedControl styleControl, bandControl, modeControl;
    juce::TextButton fillButton, replaceButton;
    ObjectSlider intensitySlider, cleanupSlider;
    InspectorDeleteButton deleteButton;
};

//==============================================================================
Inspector::Inspector (FreqArtProcessor& processor, EditorState& state)
    : impl (std::make_unique<Impl> (*this, processor, state))
{
    setOpaque (true);
    setTitle ("Inspector");

    if (state.focusTextField == nullptr)
    {
        state.focusTextField = [this] { focusTextField(); };
        impl->installedFocusCallback = true;
    }

    processor.addChangeListener (this);
    state.addChangeListener (this);
    impl->refresh();
}

Inspector::~Inspector()
{
    impl->processor.removeChangeListener (this);
    impl->state.removeChangeListener (this);

    // A wheel gesture (or typing) left open must not join an edit made in the next editor,
    // whose keys are counted from 1 again.
    impl->processor.endCoalescing();

    if (impl->installedFocusCallback)
        impl->state.focusTextField = nullptr;
}

void Inspector::focusTextField()
{
    // The selection may have changed a moment ago (its change message is still on its way).
    impl->refresh();

    if (impl->mode == Impl::Mode::text && impl->textField.isShowing())
    {
        impl->textField.grabKeyboardFocus();
        impl->textField.selectAll();
    }
}

void Inspector::paint (juce::Graphics& g)
{
    impl->paint (g);
}

void Inspector::resized()
{
    impl->layout();
}

void Inspector::changeListenerCallback (juce::ChangeBroadcaster*)
{
    impl->refresh();
}

} // namespace freqart::ui
