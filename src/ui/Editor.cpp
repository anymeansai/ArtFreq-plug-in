#include "Editor.h"

#include "Actions.h"
#include "CanvasView.h"
#include "EditorState.h"
#include "EffectShell.h"
#include "Inspector.h"
#include "KeyFocus.h"
#include "LiveView.h"
#include "LookAndFeel.h"
#include "ShellFooter.h"
#include "ShellHeader.h"
#include "ShellPanels.h"
#include "ShellToolbar.h"

#include <cmath>
#include <optional>

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

        // ArtFreq FX: two bars and the canvas between them (and, while they apply, the strips);
        // the inspector is a drawer over the canvas.
        namespace fx = ui::shell::effectMetrics;
        constexpr int effectCanvasTop = 8;
        constexpr int effectCanvasRight = 10;
        constexpr int effectCanvasBottom = 2;
        constexpr int effectCanvasLeft = 2;
        constexpr int drawerWidth = ui::Inspector::compactWidth;
        constexpr int drawerShadow = 14;
        constexpr int effectSettingsTop = fx::topBarHeight + 6;
    } // namespace layout

    // The drawer keeps clear of a canvas click for this long after it (see chooseDrawerSide).
    constexpr juce::uint32 canvasPressMilliseconds = 1500;

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
    /** The soft edge ArtFreq FX's drawer casts on the canvas beside it. Clicks pass through. */
    class DrawerShadow final : public juce::Component
    {
    public:
        DrawerShadow()   { setInterceptsMouseClicks (false, false); }

        void setOnLeft (bool shouldBeOnLeft)
        {
            if (onLeft != shouldBeOnLeft)
            {
                onLeft = shouldBeOnLeft;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            // Darkest against the drawer, gone at the far edge.
            const auto bounds = getLocalBounds().toFloat();
            const auto dark = juce::Colours::black.withAlpha (0.42f);
            const auto clear = juce::Colours::transparentBlack;
            g.setGradientFill (onLeft ? juce::ColourGradient (dark, bounds.getX(), 0.0f, clear, bounds.getRight(), 0.0f, false)
                                      : juce::ColourGradient (clear, bounds.getX(), 0.0f, dark, bounds.getRight(), 0.0f, false));
            g.fillRect (bounds);
        }

    private:
        bool onLeft = false;
    };

    //==============================================================================
    /** The whole interface at its design size (1120 x 720; ArtFreq FX's, 720 x 440). The editor
        scales it to the window.

        ArtFreq (the instrument): header, toolbar, canvas with the inspector beside it, footer.
        ArtFreq FX (`compact`): a top bar, the canvas, a bottom bar; the sines' settings and the
        brush's in strips that are there only while they apply; the inspector as a drawer over
        the canvas, open while the layer list is asked for or an object is selected. */
    class Content final : public juce::Component,
                          public ui::KeyFocusHome,
                          private juce::ChangeListener,
                          private juce::FocusChangeListener,
                          private juce::Timer
    {
    public:
        explicit Content (ArtFreqProcessor& p)
            : processor (p),
              compact (p.isEffect()),
              header (compact ? nullptr : std::make_unique<ui::shell::Header>()),
              toolbar (compact ? nullptr : std::make_unique<ui::shell::Toolbar> (state)),
              canvas (p, state),
              liveView (p),
              inspector (p, state, compact),
              footer (compact ? nullptr : std::make_unique<ui::shell::Footer> (p, layout::inspectorLeft)),
              settings (p),
              help (compact)
        {
            setOpaque (true);
            setWantsKeyboardFocus (true);
            setComponentID ("content");

            canvas.setComponentID ("canvas");
            liveView.setComponentID ("live-view");
            inspector.setComponentID ("inspector");

            if (compact)
            {
                buildEffectShell();
            }
            else
            {
                header->setComponentID ("header");
                toolbar->setComponentID ("toolbar");
                footer->setComponentID ("footer");

                addAndMakeVisible (*header);
                addAndMakeVisible (*toolbar);
                addAndMakeVisible (canvas);
                addChildComponent (liveView);
                addAndMakeVisible (inspector);
                addAndMakeVisible (*footer);
            }

            addChildComponent (notice);
            addChildComponent (scrim);
            addChildComponent (settings);
            addChildComponent (help);

            if (! compact)
            {
                // Header
                header->viewSwitch.onChange = [this] (int index) { setLiveView (index == 1); };
                header->undoButton.onClick = [this] { undo(); };
                header->redoButton.onClick = [this] { redo(); };
                header->helpButton.onClick = [this] { setHelpOpen (! help.isVisible()); };
                header->settingsButton.onClick = [this] { setSettingsOpen (! settings.isVisible()); };

                // Toolbar
                // Adding something from the live view goes back to the canvas, where it can be seen.
                toolbar->addTextButton.onClick = [this]
                {
                    setLiveView (false);
                    ui::actions::addText (processor, state);
                };
                toolbar->addImageButton.onClick = [this]
                {
                    setLiveView (false);
                    ui::actions::chooseImage (processor, state, *this, false);
                };
                toolbar->clearButton.onClick = [this] { ui::actions::clearDrawing (processor, state); };
            }

            state.focusTextField = [this]
            {
                // Typing needs the text box: ArtFreq FX's drawer opens for it, right away (the
                // selection's own message is still on its way).
                if (compact)
                {
                    settingsDismissed = false;
                    state.showLayerList (false);
                    updateDrawer();
                }

                inspector.focusTextField();
            };
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

            if (compact)
                return;

            // The inspector's column, with its divider (the inspector fills the rest).
            const auto main = getMainArea();
            g.setColour (theme::panel);
            g.fillRect (main.withLeft (layout::inspectorLeft));
            ui::shell::drawVerticalDivider (g, layout::inspectorLeft, main.getY(), main.getBottom());
        }

        void resized() override
        {
            if (compact)
            {
                layoutEffectShell();
                return;
            }

            header->setBounds (0, 0, getWidth(), layout::headerHeight);
            toolbar->setBounds (0, layout::headerHeight, getWidth(), layout::toolbarHeight);
            footer->setBounds (0, getHeight() - layout::footerHeight, getWidth(), layout::footerHeight);

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
                if (canvas.isVisible() && canvas.keyPressed (key))
                    return true;

                // ArtFreq FX's drawer: Esc lets go of the selection there too (in the live view),
                // and closes the layer list once nothing is selected.
                if (compact && inspector.isVisible())
                {
                    if (state.hasSelection())
                        state.setSelected (juce::Uuid::null());
                    else
                        setLayersOpen (false);

                    return true;
                }

                return false;
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

            // Delete and the arrows belong to the canvas; it handles them while it has the focus.
            // From anywhere else in the window, hand them over: also from the live view, where
            // the selection is still shown (the inspector, ArtFreq FX's drawer) and acted on.
            // With nothing selected they are no keys of ours, and the DAW gets them.
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
            if (compact)
                topBar->liveButton.setToggleState (live, juce::dontSendNotification);
            else
                header->viewSwitch.setSelectedIndex (live ? 1 : 0, juce::dontSendNotification);

            if (live == liveView.isVisible())
                return;

            // The live view turns the analyser on while it is visible.
            canvas.setVisible (! live);
            liveView.setVisible (live);

            // The drawing tools have nothing to act on in the live view: the instrument greys
            // them, ArtFreq FX does not show them (nor the brush's settings).
            if (compact)
            {
                bottomBar->setToolsShown (! live);
                updateStrips();
            }
            else
            {
                toolbar->setDrawingToolsEnabled (! live);
            }
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

                settings.refresh();
            }

            settings.setVisible (open);
            settingsButton().setToggleState (open, juce::dontSendNotification);
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
            helpButton().setToggleState (open, juce::dontSendNotification);
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

        bool isLayerListShown() const
        {
            return state.isLayerListShown() && (! compact || inspector.isVisible());
        }

        //==============================================================================
        // ArtFreq FX's drawer: the inspector over one side of the canvas. It is open while the
        // layer list is asked for (the Layers button) or an object is selected, and goes away
        // on deselect, Esc, the button or its own close button.

        bool isDrawerOpen() const noexcept     { return compact && inspector.isVisible(); }
        bool isDrawerOnLeft() const noexcept   { return compact && drawerOnLeft; }

        /** The Layers button: opens the drawer on the list, or closes it. An object selected in
            the list stays selected (its handles on the canvas are then free of the drawer). */
        void setLayersOpen (bool open)
        {
            if (! compact)
                return;

            layersWanted = open;

            if (open)
            {
                if (state.hasSelection())
                    state.showLayerList (true);
            }
            else
            {
                settingsDismissed = state.hasSelection();
            }

            updateDrawer();
        }

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

            void mouseDown (const juce::MouseEvent& e) override
            {
                content.noteCanvasPress (e);
                content.tidyKeyFocusAfterClick (e, true);
            }

            void mouseUp (const juce::MouseEvent& e) override
            {
                content.tidyKeyFocusAfterClick (e, false);
                content.noteCanvasRelease (e);
            }

            Content& content;
        };

        //==============================================================================
        // ArtFreq FX's shell

        void buildEffectShell()
        {
            topBar = std::make_unique<ui::shell::EffectTopBar> (processor);
            bottomBar = std::make_unique<ui::shell::EffectBottomBar> (processor, state);
            soundStrip = std::make_unique<ui::shell::SoundStrip> (processor);
            brushStrip = std::make_unique<ui::shell::BrushStrip> (state);

            addAndMakeVisible (*topBar);
            addChildComponent (*soundStrip);
            addAndMakeVisible (canvas);
            addChildComponent (liveView);
            addChildComponent (*brushStrip);
            addAndMakeVisible (*bottomBar);
            addChildComponent (drawerShadow);
            addChildComponent (inspector);

            topBar->liveButton.onClick = [this] { setLiveView (! liveView.isVisible()); };
            topBar->undoButton.onClick = [this] { undo(); };
            topBar->redoButton.onClick = [this] { redo(); };
            topBar->helpButton.onClick = [this] { setHelpOpen (! help.isVisible()); };
            topBar->settingsButton.onClick = [this] { setSettingsOpen (! settings.isVisible()); };

            // Adding something from the live view goes back to the canvas, where it can be seen.
            bottomBar->addTextButton.onClick = [this]
            {
                setLiveView (false);
                ui::actions::addText (processor, state);
            };
            bottomBar->addImageButton.onClick = [this]
            {
                setLiveView (false);
                ui::actions::chooseImage (processor, state, *this, false);
            };
            bottomBar->layersButton.onClick = [this] { setLayersOpen (! isLayerListShown()); };

            // The drawer's own close button: out of the way, whatever it shows. What is selected
            // stays selected.
            inspector.onClose = [this] { setLayersOpen (false); };
        }

        void layoutEffectShell()
        {
            const auto width = getWidth(), height = getHeight();

            topBar->setBounds (0, 0, width, layout::fx::topBarHeight);
            bottomBar->setBounds (0, height - layout::fx::bottomBarHeight, width, layout::fx::bottomBarHeight);
            soundStrip->setBounds (0, topBar->getBottom(), width, layout::fx::stripHeight);
            brushStrip->setBounds (0, bottomBar->getY() - layout::fx::stripHeight, width, layout::fx::stripHeight);

            // The canvas has what the bars, and the strips that are there, leave.
            effectMain = { 0, topBar->getBottom(), width, bottomBar->getY() - topBar->getBottom() };

            if (soundStrip->isVisible())
                effectMain.removeFromTop (layout::fx::stripHeight);

            if (brushStrip->isVisible())
                effectMain.removeFromBottom (layout::fx::stripHeight);

            const auto view = effectMain.withTrimmedLeft (layout::effectCanvasLeft)
                                        .withTrimmedTop (layout::effectCanvasTop)
                                        .withTrimmedRight (layout::effectCanvasRight)
                                        .withTrimmedBottom (layout::effectCanvasBottom);
            canvas.setBounds (view);
            liveView.setBounds (view);
            layoutDrawer();

            notice.setBounds (view.withTrimmedLeft (ui::CanvasView::axisWidth)
                                  .withTrimmedBottom (ui::CanvasView::axisHeight + 10)
                                  .removeFromBottom (noticeHeight)
                                  .reduced (12, 0));

            scrim.setBounds (getLocalBounds());
            layoutPanels();
        }

        /** The strips are there only while they have something to act on: the sines' settings in
            the modes that play the sines, the brush's while the brush or the eraser is the tool
            (and the canvas is shown). */
        void updateStrips()
        {
            const auto sound = playsSines (processor.getEffectMode());
            const auto brush = state.getTool() != ui::Tool::select && ! liveView.isVisible();

            if (soundStrip->isVisible() == sound && brushStrip->isVisible() == brush)
                return;

            soundStrip->setVisible (sound);
            brushStrip->setVisible (brush);
            resized();
        }

        juce::Rectangle<int> drawerBounds (bool onLeft) const
        {
            return onLeft ? effectMain.withWidth (layout::drawerWidth)
                          : effectMain.withLeft (effectMain.getRight() - layout::drawerWidth);
        }

        void layoutDrawer()
        {
            const auto bounds = drawerBounds (drawerOnLeft);
            inspector.setOnLeft (drawerOnLeft);
            inspector.setBounds (bounds);
            drawerShadow.setOnLeft (drawerOnLeft);
            drawerShadow.setBounds (drawerOnLeft ? bounds.withX (bounds.getRight()).withWidth (layout::drawerShadow)
                                                 : bounds.withX (bounds.getX() - layout::drawerShadow).withWidth (layout::drawerShadow));
        }

        /** Whether the drawer is open, what it shows, and on which side. Called on every change
            of the drawing or the selection, and by whatever opens or closes it. */
        void updateDrawer()
        {
            const auto selected = state.getSelected();
            const auto selectionChanged = selected != drawerSelection;
            drawerSelection = selected;

            // A dismissal (the close button) lasts as long as its selection.
            if (selectionChanged || selected.isNull())
                settingsDismissed = false;

            // Picked in the list, or back to it from an object's settings: the list is wanted, and
            // stays when the object is let go of.
            if (state.hasSelection() && state.isLayerListShown() && ! settingsDismissed)
                layersWanted = true;

            const auto open = layersWanted || (state.hasSelection() && ! settingsDismissed);
            const auto showsSettings = open && state.hasSelection() && ! state.isLayerListShown();
            const auto wasOpen = inspector.isVisible();

            if (showsSettings)
                settingsDismissed = false;

            if (! open)
                drawerOnLeft = false;                // it opens on the right
            else if (showsSettings && (! wasOpen || selectionChanged))
                chooseDrawerSide (true, {});

            const auto hadFocus = wasOpen && inspector.hasKeyboardFocus (true);

            layoutDrawer();
            inspector.setVisible (open);
            drawerShadow.setVisible (open);
            bottomBar->layersButton.setToggleState (open && state.isLayerListShown(), juce::dontSendNotification);

            // (JUCE hands the focus of something hidden to its parent: the canvas is its home.)
            if (hadFocus && ! open)
                takeKeyFocusBack();
        }

        /** The drawer gets out of the way of what it is for. An object's settings open on the
            right, unless that would cover the object's middle or the spot just clicked on the
            canvas: then on the left. So the object stays in view while its settings change it,
            the handles near the click stay free, and the drawer never opens under the mouse,
            where the second click of a double-click would land on one of its controls. The
            layer list stays where it is while it is used. `mouse` (in this component's
            coordinates), if given, is somewhere the drawer must not move to. Returns whether
            the side changed. */
        bool chooseDrawerSide (bool usePress, std::optional<juce::Point<int>> mouse)
        {
            const auto* object = processor.getDocument().find (state.getSelected());

            if (object == nullptr)
                return false;

            const auto drawing = canvas.getDrawingArea();
            const auto centreX = juce::roundToInt (static_cast<float> (canvas.getX()) + drawing.getX()
                                                   + 0.5f * (object->startTime + object->endTime) * drawing.getWidth());
            const auto pressed = usePress && pressOnCanvas
                              && juce::Time::getMillisecondCounter() - canvasPressTime < canvasPressMilliseconds;

            const auto covers = [&] (bool onLeft)
            {
                const auto bounds = drawerBounds (onLeft);
                return (bounds.getX() <= centreX && centreX < bounds.getRight()) || (pressed && bounds.contains (canvasPress));
            };

            if (! covers (drawerOnLeft) || covers (! drawerOnLeft))
                return false;

            if (mouse.has_value() && drawerBounds (! drawerOnLeft).contains (*mouse))
                return false;

            drawerOnLeft = ! drawerOnLeft;
            return true;
        }

        /** A press on the canvas: where, and when (see chooseDrawerSide). */
        void noteCanvasPress (const juce::MouseEvent& e)
        {
            if (! compact)
                return;

            pressOnCanvas = e.eventComponent == &canvas;

            if (pressOnCanvas)
            {
                canvasPress = getLocalPoint (&canvas, e.position).roundToInt();
                canvasPressTime = juce::Time::getMillisecondCounter();
            }
        }

        /** The canvas let go of, after it has dealt with the release. A click on the selected
            object brings its settings back if they were closed; after a drag the drawer moves
            aside if the object went under it. */
        void noteCanvasRelease (const juce::MouseEvent& e)
        {
            if (! compact || e.eventComponent != &canvas || ! state.hasSelection())
                return;

            if (! e.mouseWasDraggedSinceMouseDown())
            {
                if (settingsDismissed && ! layersWanted)
                {
                    settingsDismissed = false;
                    updateDrawer();
                }
            }
            else if (inspector.isVisible() && ! state.isLayerListShown()
                     && chooseDrawerSide (false, getLocalPoint (&canvas, e.position).roundToInt()))
            {
                layoutDrawer();
            }
        }

        juce::Button& helpButton()       { return compact ? static_cast<juce::Button&> (topBar->helpButton) : header->helpButton; }
        juce::Button& settingsButton()   { return compact ? static_cast<juce::Button&> (topBar->settingsButton) : header->settingsButton; }

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

            // ArtFreq FX's settings are nearly as wide as its window: centred under the top bar.
            if (compact)
                settings.setBounds ((getWidth() - settings.getPanelWidth()) / 2, layout::effectSettingsTop,
                                    settings.getPanelWidth(), settingsHeight);
            else
                settings.setBounds (getWidth() - layout::pad - ui::shell::SettingsPanel::width, layout::settingsTop,
                                    ui::shell::SettingsPanel::width, settingsHeight);

            const auto helpHeight = juce::jmin (help.getIdealHeight(), getHeight() - 32);
            help.setBounds ((getWidth() - help.getPanelWidth()) / 2, (getHeight() - helpHeight) / 2,
                            help.getPanelWidth(), helpHeight);
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
            if (compact)
            {
                topBar->setUndoState (processor.canUndo(), processor.canRedo());
            }
            else
            {
                header->undoButton.setEnabled (processor.canUndo());
                header->redoButton.setEnabled (processor.canRedo());
                toolbar->clearButton.setEnabled (! processor.getDocument().isEmpty());
            }

            // An undo can take the selected object away.
            if (state.hasSelection() && processor.getDocument().find (state.getSelected()) == nullptr)
                state.setSelected (juce::Uuid::null());

            if (compact)
            {
                updateStrips();
                updateDrawer();
            }
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            updateFromProcessor();
        }

        void timerCallback() override
        {
            if (! compact)
            {
                footer->refresh();
                return;
            }

            // What follows the parameters, which automation moves too.
            topBar->refresh();
            bottomBar->refresh();
            updateStrips();

            if (settings.isVisible())
                settings.refresh();
        }

        ArtFreqProcessor& processor;
        const bool compact;   // ArtFreq FX's window
        ui::EditorState state;

        std::unique_ptr<ui::shell::Header> header;     // the instrument's shell: header, toolbar ...
        std::unique_ptr<ui::shell::Toolbar> toolbar;
        ui::CanvasView canvas;
        ui::LiveView liveView;
        ui::Inspector inspector;
        std::unique_ptr<ui::shell::Footer> footer;     // ... and footer
        Notice notice;
        ui::shell::Scrim scrim;
        ui::shell::SettingsPanel settings;
        ui::shell::HelpPanel help;

        // ArtFreq FX's shell.
        std::unique_ptr<ui::shell::EffectTopBar> topBar;
        std::unique_ptr<ui::shell::EffectBottomBar> bottomBar;
        std::unique_ptr<ui::shell::SoundStrip> soundStrip;
        std::unique_ptr<ui::shell::BrushStrip> brushStrip;
        DrawerShadow drawerShadow;
        juce::Rectangle<int> effectMain;                 // between the bars and strips: the canvas's room
        bool layersWanted = false;                       // the Layers button is on
        bool settingsDismissed = false;                  // the drawer was closed on the current selection
        bool drawerOnLeft = false;
        juce::Uuid drawerSelection = juce::Uuid::null(); // the selection the drawer last saw
        bool pressOnCanvas = false;                      // the last press was on the canvas, there and then:
        juce::Point<int> canvasPress;
        juce::uint32 canvasPressTime = 0;

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
        : editor (owner), processor (p),
          baseW (p.isEffect() ? effectBaseWidth : baseWidth),
          baseH (p.isEffect() ? effectBaseHeight : baseHeight),
          content (p)
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

    void setWindowScale (float scale)
    {
        scale = juce::jlimit (minWindowScale, maxWindowScale, scale);
        editor.setSize (juce::roundToInt (static_cast<float> (baseW) * scale),
                        juce::roundToInt (static_cast<float> (baseH) * scale));
    }

    /** Scales the interface to fill the window. If a host forces another aspect ratio the
        interface is centred and the rest is filled with the window colour. */
    void updateLayout()
    {
        const auto width = static_cast<float> (editor.getWidth());
        const auto height = static_cast<float> (editor.getHeight());

        currentScale = juce::jmax (0.1f, juce::jmin (width / static_cast<float> (baseW),
                                                     height / static_cast<float> (baseH)));

        const auto offsetX = std::round ((width - static_cast<float> (baseW) * currentScale) * 0.5f);
        const auto offsetY = std::round ((height - static_cast<float> (baseH) * currentScale) * 0.5f);

        content.setTransform ({});
        content.setBounds (0, 0, baseW, baseH);
        content.setTransform (juce::AffineTransform::scale (currentScale).translated (offsetX, offsetY));
        content.windowScaleChanged (currentScale);
    }

    bool contentFillsWindow() const
    {
        return std::abs (static_cast<float> (baseW) * currentScale - static_cast<float> (editor.getWidth())) < 0.75f
            && std::abs (static_cast<float> (baseH) * currentScale - static_cast<float> (editor.getHeight())) < 0.75f;
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
    const int baseW, baseH;   // this plug-in's window at 100 %
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
    const auto savedHeight = p.getEditorHeight();

    impl = std::make_unique<Impl> (*this, p);

    setOpaque (true);
    setWantsKeyboardFocus (true);

    const auto limit = [] (int base, float scale) { return juce::roundToInt (static_cast<float> (base) * scale); };

    setResizable (true, true);
    setResizeLimits (limit (impl->baseW, minWindowScale), limit (impl->baseH, minWindowScale),
                     limit (impl->baseW, maxWindowScale), limit (impl->baseH, maxWindowScale));

    if (auto* limits = getConstrainer())
        limits->setFixedAspectRatio (static_cast<double> (impl->baseW) / static_cast<double> (impl->baseH));

    impl->setWindowScale (scaleForSavedSize (p.isEffect(), savedWidth, savedHeight));
    impl->updateLayout();
    impl->rememberSize = true;
}

ArtFreqEditor::~ArtFreqEditor()
{
    impl.reset();
}

int ArtFreqEditor::getBaseWidth() const noexcept    { return impl->baseW; }
int ArtFreqEditor::getBaseHeight() const noexcept   { return impl->baseH; }

float ArtFreqEditor::scaleForSavedSize (bool effect, int width, int height) noexcept
{
    if (width <= 0)
        return 1.0f;   // never resized

    auto base = effect ? effectBaseWidth : baseWidth;

    // ArtFreq FX had the instrument's window until 0.4, and what is saved is a size in pixels,
    // not a scale. The two windows have different shapes (1120 : 720 and 720 : 440), so the
    // shape says which one a size was saved with: one saved with the large window opens at the
    // same scale of the small one, rather than as large as it was.
    if (effect && height > 0)
    {
        const auto shape = static_cast<float> (width) / static_cast<float> (height);
        const auto large = static_cast<float> (baseWidth) / static_cast<float> (baseHeight);
        const auto small = static_cast<float> (effectBaseWidth) / static_cast<float> (effectBaseHeight);

        if (std::abs (shape - large) < std::abs (shape - small))
            base = baseWidth;
    }

    return juce::jlimit (minWindowScale, maxWindowScale, static_cast<float> (width) / static_cast<float> (base));
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

void ArtFreqEditor::showLayers (bool shouldShow)
{
    impl->content.setLayersOpen (shouldShow);
}

bool ArtFreqEditor::isDrawerOpen() const
{
    return impl->content.isDrawerOpen();
}

bool ArtFreqEditor::isDrawerOnLeft() const
{
    return impl->content.isDrawerOnLeft();
}

} // namespace artfreq
