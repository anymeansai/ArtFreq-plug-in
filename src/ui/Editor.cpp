#include "Editor.h"

#include "Actions.h"
#include "CanvasView.h"
#include "EditorState.h"
#include "EffectControls.h"
#include "Inspector.h"
#include "KeyFocus.h"
#include "LiveView.h"
#include "LookAndFeel.h"
#include "ShellFooter.h"
#include "ShellHeader.h"
#include "ShellPanels.h"
#include "ShellToolbar.h"

#include <cmath>

namespace artfreq
{
namespace theme = ui::theme;

namespace
{
    constexpr float minWindowScale = 0.75f;
    constexpr float maxWindowScale = 2.0f;

    namespace layout
    {
        using namespace ui::shell::metrics;

        constexpr int mainTop = headerHeight + toolbarHeight;
        constexpr int inspectorWidth = ui::Inspector::preferredWidth;   // includes its 1 px divider
        constexpr int inspectorLeft = ArtFreqEditor::baseWidth - inspectorWidth;

        // The canvas keeps a little air from the toolbar, the inspector and the footer; its own
        // axes supply the margin on the left and at the bottom.
        constexpr int canvasTop = 12;
        constexpr int canvasRight = 16;
        constexpr int canvasBottom = 6;
        constexpr int canvasLeft = 4;

        constexpr int settingsTop = headerHeight + 4;
    } // namespace layout

    constexpr float helpDim = 0.55f;
    constexpr int refreshHz = 30;
    constexpr int noticeMilliseconds = 6000;
    constexpr int noticeHeight = 32;

    //==============================================================================
    /** A short message at the bottom of the canvas (a picture that could not be read), gone
        after a few seconds. Clicks pass through to whatever is under it. */
    class Notice final : public juce::Component,
                         private juce::Timer
    {
    public:
        Notice()
        {
            setInterceptsMouseClicks (false, false);
            setComponentID ("notice");
        }

        void show (const juce::String& text)
        {
            message = text;
            setTitle (text);   // what a screen reader (and a test) reads
            setVisible (true);
            repaint();
            startTimer (noticeMilliseconds);
        }

        void paint (juce::Graphics& g) override
        {
            const auto font = fonts::ui (theme::controlSize, fonts::UiWeight::medium);
            const auto bounds = getLocalBounds().toFloat();
            const auto width = juce::jmin (bounds.getWidth(), juce::GlyphArrangement::getStringWidth (font, message) + 36.0f);
            const auto pill = juce::Rectangle<float> (width, bounds.getHeight()).withCentre (bounds.getCentre()).reduced (0.5f);

            g.setColour (theme::panel.withAlpha (0.96f));
            g.fillRoundedRectangle (pill, 8.0f);
            g.setColour (theme::danger.withAlpha (0.8f));
            g.drawRoundedRectangle (pill, 8.0f, 1.0f);
            g.setColour (theme::text);
            g.setFont (font);
            g.drawText (message, pill.reduced (12.0f, 0.0f), juce::Justification::centred, true);
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            setVisible (false);
        }

        juce::String message;
    };

    //==============================================================================
    /** The whole interface at its design size (1120 x 720). The editor scales it to the window. */
    class Content final : public juce::Component,
                          public ui::KeyFocusHome,
                          private juce::ChangeListener,
                          private juce::FocusChangeListener,
                          private juce::Timer
    {
    public:
        explicit Content (ArtFreqProcessor& p)
            : processor (p),
              header (p.isEffect()),
              toolbar (state),
              canvas (p, state),
              liveView (p),
              inspector (p, state),
              footer (p, layout::inspectorLeft),
              settings (p),
              help (p.isEffect())
        {
            setOpaque (true);
            setWantsKeyboardFocus (true);
            setComponentID ("content");

            canvas.setComponentID ("canvas");
            liveView.setComponentID ("live-view");
            inspector.setComponentID ("inspector");
            header.setComponentID ("header");
            toolbar.setComponentID ("toolbar");
            footer.setComponentID ("footer");

            addAndMakeVisible (header);
            addAndMakeVisible (toolbar);
            addAndMakeVisible (canvas);
            addChildComponent (liveView);
            addAndMakeVisible (inspector);
            addAndMakeVisible (footer);
            addChildComponent (notice);
            addChildComponent (scrim);
            addChildComponent (settings);
            addChildComponent (help);

            // ArtFreq FX: Mode and Depth in the header.
            if (processor.isEffect())
            {
                effectControls = std::make_unique<ui::shell::EffectControls> (processor);
                header.setEffectControls (effectControls.get(), ui::shell::EffectControls::getIdealWidth());
            }

            // Header
            header.viewSwitch.onChange = [this] (int index) { setLiveView (index == 1); };
            header.undoButton.onClick = [this] { undo(); };
            header.redoButton.onClick = [this] { redo(); };
            header.helpButton.onClick = [this] { setHelpOpen (! help.isVisible()); };
            header.settingsButton.onClick = [this] { setSettingsOpen (! settings.isVisible()); };

            // Toolbar
            // Adding something from the live view goes back to the canvas, where it can be seen.
            toolbar.addTextButton.onClick = [this]
            {
                setLiveView (false);
                ui::actions::addText (processor, state);
            };
            toolbar.addImageButton.onClick = [this]
            {
                setLiveView (false);
                ui::actions::chooseImage (processor, state, *this, false);
            };
            toolbar.clearButton.onClick = [this] { ui::actions::clearDrawing (processor, state); };

            state.focusTextField = [this] { inspector.focusTextField(); };
            state.showNotice = [this] (const juce::String& text) { notice.show (text); };

            // Panels
            scrim.onClick = [this] { closePanels(); };
            settings.onClose = [this] { setSettingsOpen (false); };
            settings.onWindowScaleChosen = [this] (float scale)
            {
                if (onWindowScaleChosen != nullptr)
                    onWindowScaleChosen (scale);
            };
            settings.onResetDrawing = [this]
            {
                state.setSelected (juce::Uuid::null());
                processor.setDocument (ArtDocument::createDefault());
            };
            help.onClose = [this] { setHelpOpen (false); };

            // Tooltips open inside the interface, so they scale with it. Created last: on top.
            tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
            tooltips->setComponentID ("tooltips");

            // The keyboard focus: see "The keyboard focus" below.
            addMouseListener (&clickWatcher, true);
            juce::Desktop::getInstance().addFocusChangeListener (this);

            processor.addChangeListener (this);
            state.addChangeListener (this);
            updateFromProcessor();
            startTimerHz (refreshHz);
        }

        ~Content() override
        {
            stopTimer();
            juce::Desktop::getInstance().removeFocusChangeListener (this);
            removeMouseListener (&clickWatcher);
            state.removeChangeListener (this);
            processor.removeChangeListener (this);
            state.focusTextField = nullptr;
            state.showNotice = nullptr;
        }

        //==============================================================================
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::bg);

            // The inspector's column, with its divider (the inspector fills the rest).
            const auto main = getMainArea();
            g.setColour (theme::panel);
            g.fillRect (main.withLeft (layout::inspectorLeft));
            ui::shell::drawVerticalDivider (g, layout::inspectorLeft, main.getY(), main.getBottom());
        }

        void resized() override
        {
            header.setBounds (0, 0, getWidth(), layout::headerHeight);
            toolbar.setBounds (0, layout::headerHeight, getWidth(), layout::toolbarHeight);
            footer.setBounds (0, getHeight() - layout::footerHeight, getWidth(), layout::footerHeight);

            const auto main = getMainArea();
            inspector.setBounds (main.withLeft (layout::inspectorLeft + 1));

            const auto view = main.withRight (layout::inspectorLeft)
                                  .withTrimmedLeft (layout::canvasLeft)
                                  .withTrimmedTop (layout::canvasTop)
                                  .withTrimmedRight (layout::canvasRight)
                                  .withTrimmedBottom (layout::canvasBottom);
            canvas.setBounds (view);
            liveView.setBounds (view);

            // Over the bottom of the drawing, clear of its axes.
            notice.setBounds (view.withTrimmedLeft (ui::CanvasView::axisWidth)
                                  .withTrimmedBottom (ui::CanvasView::axisHeight + 14)
                                  .removeFromBottom (noticeHeight)
                                  .reduced (16, 0));

            scrim.setBounds (getLocalBounds());
            layoutPanels();
        }

        //==============================================================================
        bool keyPressed (const juce::KeyPress& key) override
        {
            return handleKey (key);
        }

        /** The editor's shortcuts. Keys arrive here only when the focused control had no use
            for them (a text field keeps its own typing, undo and Delete). Whatever comes back
            unused from here goes on to the DAW: its play key (the space bar, mostly) and all its
            other shortcuts work while this window has the focus. */
        bool handleKey (const juce::KeyPress& key)
        {
            const auto mods = key.getModifiers();
            const auto code = key.getKeyCode();
            const auto letter = juce::CharacterFunctions::toUpperCase (static_cast<juce::juce_wchar> (code));

            if (key.isKeyCode (juce::KeyPress::tabKey))
                return handleTab();

            if (key.isKeyCode (juce::KeyPress::escapeKey))
            {
                if (isPanelOpen())
                {
                    closePanels();
                    return true;
                }

                // Esc lets go of the selection, wherever the focus is in the window.
                return canvas.isVisible() && canvas.keyPressed (key);
            }

            if (mods.isCommandDown())
            {
                if (letter == 'Z' && ! mods.isAltDown())
                {
                    if (mods.isShiftDown())
                        redo();
                    else
                        undo();

                    return true;
                }

               #if ! JUCE_MAC
                if (letter == 'Y' && ! mods.isShiftDown() && ! mods.isAltDown())
                {
                    redo();
                    return true;
                }
               #endif

                return false;   // anything else with Cmd is for the host
            }

            if (isPanelOpen() || mods.isAltDown() || mods.isCtrlDown())
                return false;

            // Choosing a tool from the live view goes back to the canvas.
            if (! mods.isShiftDown() && (letter == 'V' || letter == 'B' || letter == 'E'))
            {
                setLiveView (false);
                state.setTool (letter == 'V' ? ui::Tool::select : letter == 'B' ? ui::Tool::brush : ui::Tool::erase);
                return true;
            }

            if (liveView.isVisible())
                return false;

            // Delete and the arrows belong to the canvas; it handles them while it has the focus.
            // From anywhere else in the window, hand them over.
            if (key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey)
                || key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey)
                || key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::downKey))
            {
                if (canvas.keyPressed (key))
                    return true;

                if ((key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey))
                    && state.hasSelection())
                {
                    ui::actions::deleteSelected (processor, state);
                    return true;
                }
            }

            return false;
        }

        /** Keys going up or down anywhere in the window: letting go of the arrows ends a nudge. */
        void handleKeyStateChange (bool isKeyDown)
        {
            canvas.keyStateChanged (isKeyDown);
        }

        //==============================================================================
        // The keyboard focus (see ui/KeyFocus.h). The DAW only gets a key that nothing here used,
        // so the focus rests where the editor's own few keys are handled, and with a text box
        // only while it is being typed in.

        void takeKeyFocusBack() override
        {
            if (! windowHasKeyboard())
                return;

            auto& home = settings.isVisible() ? static_cast<juce::Component&> (settings)
                       : help.isVisible()     ? static_cast<juce::Component&> (help)
                       : canvas.isVisible()   ? static_cast<juce::Component&> (canvas)
                                              : static_cast<juce::Component&> (*this);
            home.grabKeyboardFocus();
        }

        /** After a press (or a release) anywhere in the interface, once the component under the
            mouse has dealt with it. JUCE gives a clicked button, menu or chip the focus, and
            leaves it in a text box when the click was on something that takes none: neither
            may keep it. */
        void tidyKeyFocusAfterClick (const juce::MouseEvent& e, bool pressed)
        {
            auto* focused = juce::Component::getCurrentlyFocusedComponent();

            // (A menu that is open, or a value being typed into a label, is modal and deals
            // with clicks itself.)
            if (focused == nullptr || ! isParentOf (focused) || juce::Component::getCurrentlyModalComponent() != nullptr)
                return;

            if (ui::isTextBox (focused))
            {
                // A press anywhere but in the text box ends the typing. Not a release: that is
                // when a click gives a text box the focus (Add text, a number field, a
                // double-click on text).
                if (pressed && e.eventComponent != focused && ! focused->isParentOf (e.eventComponent))
                    takeKeyFocusBack();

                return;
            }

            if (! mayKeepKeyFocus (*focused))
                takeKeyFocusBack();
        }

        //==============================================================================
        void setLiveView (bool live)
        {
            header.viewSwitch.setSelectedIndex (live ? 1 : 0, juce::dontSendNotification);

            if (live == liveView.isVisible())
                return;

            // The live view turns the analyser on while it is visible.
            canvas.setVisible (! live);
            liveView.setVisible (live);

            // The drawing tools have nothing to act on in the live view.
            toolbar.setDrawingToolsEnabled (! live);
        }

        bool isLiveViewShown() const noexcept   { return liveView.isVisible(); }

        void setSettingsOpen (bool open)
        {
            if (open == settings.isVisible())
                return;

            if (open)
            {
                setHelpOpen (false);
                settings.cancelReset();

                if (getWindowScale != nullptr)
                    settings.setCurrentWindowScale (getWindowScale());
            }

            settings.setVisible (open);
            header.settingsButton.setToggleState (open, juce::dontSendNotification);
            updateScrim();

            // The panel has the keys while it is open (Esc closes it); then the canvas again.
            if (open && isShowing())
                settings.grabKeyboardFocus();
            else
                takeKeyFocusBack();
        }

        void setHelpOpen (bool open)
        {
            if (open == help.isVisible())
                return;

            if (open)
                setSettingsOpen (false);

            help.setVisible (open);
            header.helpButton.setToggleState (open, juce::dontSendNotification);
            updateScrim();

            if (open && isShowing())
                help.grabKeyboardFocus();
            else
                takeKeyFocusBack();
        }

        bool isPanelOpen() const noexcept   { return settings.isVisible() || help.isVisible(); }

        void closePanels()
        {
            setSettingsOpen (false);
            setHelpOpen (false);
            takeKeyFocusBack();
        }

        void selectObject (const juce::Uuid& id)
        {
            if (! id.isNull())
                state.setTool (ui::Tool::select);

            state.setSelected (id);
        }

        juce::Uuid getSelectedObject() const   { return state.getSelected(); }
        bool isLayerListShown() const          { return state.isLayerListShown(); }

        /** Called when the window's scale changes, so the settings show it. */
        void windowScaleChanged (float scale)
        {
            settings.setCurrentWindowScale (scale);
        }

        std::function<void (float)> onWindowScaleChosen;
        std::function<float()> getWindowScale;

    private:
        /** Clicks anywhere in the interface, after the component clicked has dealt with them. */
        struct ClickWatcher final : public juce::MouseListener
        {
            explicit ClickWatcher (Content& c) : content (c) {}

            void mouseDown (const juce::MouseEvent& e) override   { content.tidyKeyFocusAfterClick (e, true); }
            void mouseUp (const juce::MouseEvent& e) override     { content.tidyKeyFocusAfterClick (e, false); }

            Content& content;
        };

        /** Whether this window has the keyboard: only then is the focus taken (or taken back),
            never from another window. */
        bool windowHasKeyboard() const
        {
            if (! isShowing())
                return false;

            const auto* peer = getPeer();
            return peer != nullptr && peer->isFocused();
        }

        bool isInThisWindow (const juce::Component* component) const
        {
            return component != nullptr && (component == this || isParentOf (component) || component->isParentOf (this));
        }

        /** Where the focus may rest: with something that handles keys of its own, or in a text box. */
        bool mayKeepKeyFocus (const juce::Component& component) const
        {
            return &component == this || ui::keepsKeyFocus (component) || ui::isTextBox (&component);
        }

        /** Ends the typing in a text box: what was typed stays (a value typed into a label is
            set, and the label stops being modal), and the focus goes back to the canvas. */
        void finishTyping (juce::Component& textBox)
        {
            if (auto* label = dynamic_cast<juce::Label*> (textBox.getParentComponent()))
                label->hideEditor (false);

            takeKeyFocusBack();
        }

        /** Tab is no key of ours, but JUCE takes it wherever a component has the focus: it
            moves the focus to the next control and calls the key used, so the DAW never sees
            it (and the focus would sit on a button nobody clicked, where Return presses it). */
        bool handleTab()
        {
            auto* focused = juce::Component::getCurrentlyFocusedComponent();

            // In a text box, Tab ends the typing as Return does.
            if (ui::isTextBox (focused) && isInThisWindow (focused))
            {
                finishTyping (*focused);
                return true;
            }

            // (An open menu has the keys.)
            if (focused == nullptr || ! isInThisWindow (focused) || juce::Component::getCurrentlyModalComponent() != nullptr)
                return false;

            // So the focus is let go of for the moment of the key press, which then comes back
            // unused, and taken up again right after, if the window still has the keyboard (the
            // DAW may have done something with its Tab).
            juce::Component::SafePointer<juce::Component> previous (focused);
            juce::Component::unfocusAllComponents();

            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Content> (this), previous]
            {
                if (safe == nullptr || juce::Component::getCurrentlyFocusedComponent() != nullptr || ! safe->windowHasKeyboard())
                    return;

                if (previous != nullptr && previous->isShowing())
                    previous->grabKeyboardFocus();
                else
                    safe->takeKeyFocusBack();
            });

            return false;
        }

        /** The focus has moved (JUCE reports it a moment later). Two things a click does not
            cover are put right here, both about a text box that is done with. */
        void globalFocusChanged (juce::Component* focused) override
        {
            const auto wasTyping = typing;
            const auto textBox = typingBox;

            typing = ui::isTextBox (focused) && isInThisWindow (focused);
            typingBox = typing ? focused : nullptr;

            if (! wasTyping || typing)
                return;

            if (textBox != nullptr && textBox->isShowing())
            {
                // The text box is still there and the focus went nowhere: the window lost the
                // keyboard (the user clicked in the DAW). A value being typed into a label is
                // set: the label is modal while it is edited, and a modal component left behind
                // would swallow every key once the window has the keyboard again.
                if (focused == nullptr)
                    if (auto* label = dynamic_cast<juce::Label*> (textBox->getParentComponent()))
                        label->hideEditor (false);

                return;
            }

            // The text box has gone (a value typed into a bar was set, the object it belonged to
            // was deleted): JUCE gives the focus to the first control it finds, a button or a
            // menu, where it must not stay.
            if (focused == nullptr || (isParentOf (focused) && ! mayKeepKeyFocus (*focused)))
                takeKeyFocusBack();
        }

        juce::Rectangle<int> getMainArea() const
        {
            return { 0, layout::mainTop, getWidth(), getHeight() - layout::mainTop - layout::footerHeight };
        }

        void layoutPanels()
        {
            const auto settingsHeight = settings.getIdealHeight();
            settings.setBounds (getWidth() - layout::pad - ui::shell::SettingsPanel::width, layout::settingsTop,
                                ui::shell::SettingsPanel::width, settingsHeight);

            const auto helpHeight = juce::jmin (help.getIdealHeight(), getHeight() - 32);
            help.setBounds ((getWidth() - ui::shell::HelpPanel::width) / 2, (getHeight() - helpHeight) / 2,
                            ui::shell::HelpPanel::width, helpHeight);
        }

        void updateScrim()
        {
            scrim.setDim (help.isVisible() ? helpDim : 0.0f);
            scrim.setVisible (isPanelOpen());
            scrim.repaint();
        }

        void undo()
        {
            processor.endCoalescing();
            processor.undo();
        }

        void redo()
        {
            processor.endCoalescing();
            processor.redo();
        }

        void updateFromProcessor()
        {
            header.undoButton.setEnabled (processor.canUndo());
            header.redoButton.setEnabled (processor.canRedo());
            toolbar.clearButton.setEnabled (! processor.getDocument().isEmpty());

            // An undo can take the selected object away.
            if (state.hasSelection() && processor.getDocument().find (state.getSelected()) == nullptr)
                state.setSelected (juce::Uuid::null());
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            updateFromProcessor();
        }

        void timerCallback() override
        {
            footer.refresh();

            if (effectControls != nullptr)
            {
                effectControls->refresh();
                inspector.refreshEffectMode();
            }
        }

        ArtFreqProcessor& processor;
        ui::EditorState state;

        ui::shell::Header header;
        ui::shell::Toolbar toolbar;
        ui::CanvasView canvas;
        ui::LiveView liveView;
        ui::Inspector inspector;
        ui::shell::Footer footer;
        Notice notice;
        ui::shell::Scrim scrim;
        ui::shell::SettingsPanel settings;
        ui::shell::HelpPanel help;
        std::unique_ptr<ui::shell::EffectControls> effectControls;   // ArtFreq FX only
        std::unique_ptr<juce::TooltipWindow> tooltips;

        // The keyboard focus: the text box that had it when it last moved (see globalFocusChanged).
        ClickWatcher clickWatcher { *this };
        bool typing = false;
        juce::Component::SafePointer<juce::Component> typingBox;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };
} // namespace

//==============================================================================
struct ArtFreqEditor::Impl final : private juce::KeyListener
{
    Impl (ArtFreqEditor& owner, ArtFreqProcessor& p)
        : editor (owner), processor (p), content (p)
    {
        // Setting the look-and-feel after the content has been added tells every child about
        // it, which is when the sliders create their value boxes.
        editor.addAndMakeVisible (content);
        editor.setLookAndFeel (lookAndFeel.get());
        ui::ArtFreqLookAndFeel::setPopupParent (content);

        content.onWindowScaleChosen = [this] (float scale) { setWindowScale (scale); };
        content.getWindowScale = [this] { return currentScale; };

        // Shortcuts work wherever the focus is in the window (keys bubble up to the editor).
        editor.addKeyListener (this);
    }

    ~Impl() override
    {
        editor.removeKeyListener (this);

        // A combo box menu still open belongs to this window. It is deleted a moment later by the
        // message loop and uses the look-and-feel until then, so that stays alive a moment too.
        if (juce::PopupMenu::dismissAllActiveMenus())
            juce::MessageManager::callAsync ([keepAlive = lookAndFeel] { juce::ignoreUnused (keepAlive); });

        // Detach the interface before letting go of the look-and-feel.
        editor.removeChildComponent (&content);
        editor.setLookAndFeel (nullptr);
    }

    static float scaleForWidth (int width)
    {
        return juce::jlimit (minWindowScale, maxWindowScale,
                             static_cast<float> (width) / static_cast<float> (baseWidth));
    }

    void setWindowScale (float scale)
    {
        scale = juce::jlimit (minWindowScale, maxWindowScale, scale);
        editor.setSize (juce::roundToInt (static_cast<float> (baseWidth) * scale),
                        juce::roundToInt (static_cast<float> (baseHeight) * scale));
    }

    /** Scales the interface to fill the window. If a host forces another aspect ratio the
        interface is centred and the rest is filled with the window colour. */
    void updateLayout()
    {
        const auto width = static_cast<float> (editor.getWidth());
        const auto height = static_cast<float> (editor.getHeight());

        currentScale = juce::jmax (0.1f, juce::jmin (width / static_cast<float> (baseWidth),
                                                     height / static_cast<float> (baseHeight)));

        const auto offsetX = std::round ((width - static_cast<float> (baseWidth) * currentScale) * 0.5f);
        const auto offsetY = std::round ((height - static_cast<float> (baseHeight) * currentScale) * 0.5f);

        content.setTransform ({});
        content.setBounds (0, 0, baseWidth, baseHeight);
        content.setTransform (juce::AffineTransform::scale (currentScale).translated (offsetX, offsetY));
        content.windowScaleChanged (currentScale);
    }

    bool contentFillsWindow() const
    {
        return std::abs (static_cast<float> (baseWidth) * currentScale - static_cast<float> (editor.getWidth())) < 0.75f
            && std::abs (static_cast<float> (baseHeight) * currentScale - static_cast<float> (editor.getHeight())) < 0.75f;
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        return content.handleKey (key);
    }

    bool keyStateChanged (bool isKeyDown, juce::Component*) override
    {
        content.handleKeyStateChange (isKeyDown);
        return false;
    }

    ArtFreqEditor& editor;
    ArtFreqProcessor& processor;
    std::shared_ptr<ui::ArtFreqLookAndFeel> lookAndFeel = std::make_shared<ui::ArtFreqLookAndFeel>();   // outlives the content
    Content content;
    float currentScale = 1.0f;
    bool rememberSize = false;   // off while the constructor sets the window up

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Impl)
};

//==============================================================================
ArtFreqEditor::ArtFreqEditor (ArtFreqProcessor& p)
    : juce::AudioProcessorEditor (p)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // The size this instance had last time (0 = never resized: 100 %). Read first: setting the
    // limits below resizes the window, which would overwrite it.
    const auto savedWidth = p.getEditorWidth();

    impl = std::make_unique<Impl> (*this, p);

    setOpaque (true);
    setWantsKeyboardFocus (true);

    const auto limit = [] (int base, float scale) { return juce::roundToInt (static_cast<float> (base) * scale); };

    setResizable (true, true);
    setResizeLimits (limit (baseWidth, minWindowScale), limit (baseHeight, minWindowScale),
                     limit (baseWidth, maxWindowScale), limit (baseHeight, maxWindowScale));

    if (auto* limits = getConstrainer())
        limits->setFixedAspectRatio (static_cast<double> (baseWidth) / static_cast<double> (baseHeight));

    impl->setWindowScale (savedWidth > 0 ? Impl::scaleForWidth (savedWidth) : 1.0f);
    impl->updateLayout();
    impl->rememberSize = true;
}

ArtFreqEditor::~ArtFreqEditor()
{
    impl.reset();
}

void ArtFreqEditor::paint (juce::Graphics& g)
{
    // The interface covers the window; this only shows if a host imposes another aspect ratio.
    if (impl == nullptr || ! impl->contentFillsWindow())
        g.fillAll (theme::bg);
}

void ArtFreqEditor::resized()
{
    if (impl == nullptr)
        return;

    impl->updateLayout();

    if (impl->rememberSize)
        impl->processor.setEditorSize (getWidth(), getHeight());
}

void ArtFreqEditor::showLiveView (bool live)
{
    impl->content.setLiveView (live);
}

void ArtFreqEditor::showHelp (bool shouldShow)
{
    impl->content.setHelpOpen (shouldShow);
}

void ArtFreqEditor::showSettings (bool shouldShow)
{
    impl->content.setSettingsOpen (shouldShow);
}

void ArtFreqEditor::selectObject (const juce::Uuid& id)
{
    impl->content.selectObject (id);
}

juce::Uuid ArtFreqEditor::getSelectedObject() const
{
    return impl->content.getSelectedObject();
}

bool ArtFreqEditor::isLayerListShown() const
{
    return impl->content.isLayerListShown();
}

} // namespace artfreq
