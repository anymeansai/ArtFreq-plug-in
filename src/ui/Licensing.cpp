#include "Licensing.h"

#include "Emblem.h"
#include "KeyFocus.h"
#include "license/LicenseKey.h"

#include <cmath>
#include <vector>

namespace artfreq::ui
{
namespace
{
    using anymeans::license::LicenseKey;
    using anymeans::license::LicenseManager;
    using Result = LicenseManager::Result;
    using Outcome = Result::Outcome;

    juce::String fromUtf8 (const char* text)   { return juce::String (juce::CharPointer_UTF8 (text)); }
    juce::String middleDot()                    { return fromUtf8 ("\xc2\xb7"); }

    bool isKeyCharacter (juce::juce_wchar c) noexcept
    {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    /** What may stand between the groups of a key that is pasted: dashes (also typographic
        ones, which mail programs make of a hyphen) and blanks. */
    bool isKeySeparator (juce::juce_wchar c) noexcept
    {
        return c == '-' || (c >= 0x2010 && c <= 0x2015) || c == 0x2212 || juce::CharacterFunctions::isWhitespace (c);
    }

    /** A key in pasted text that holds more than a key ("Your key: 8K3QX-7M2PD-..."): runs of
        letters and digits joined only by dashes or blanks that make a well-formed key. */
    juce::String findKeyIn (const juce::String& text)
    {
        struct Run
        {
            juce::String characters;
            bool followsTheOneBefore = false;   // only dashes and blanks between it and the run before
        };

        std::vector<Run> runs;
        Run current;
        auto joined = false;

        for (auto c : text)
        {
            if (isKeyCharacter (c))
            {
                current.characters += juce::String::charToString (c);
                continue;
            }

            if (current.characters.isNotEmpty())
            {
                runs.push_back (current);
                current = {};
                joined = true;
            }

            joined = joined && isKeySeparator (c);
            current.followsTheOneBefore = joined;
        }

        if (current.characters.isNotEmpty())
            runs.push_back (current);

        for (size_t first = 0; first < runs.size(); ++first)
        {
            juce::String candidate;

            for (auto last = first; last < runs.size(); ++last)
            {
                if (last > first && ! runs[last].followsTheOneBefore)
                    break;

                candidate += runs[last].characters;
                const auto key = LicenseKey::normalise (candidate);

                if (key.length() > LicenseKey::length)
                    break;

                if (key.length() == LicenseKey::length && LicenseKey::isWellFormed (key))
                    return key;
            }
        }

        return {};
    }

    juce::Colour errorColour()     { return juce::Colour (0xffff8a95); }   // danger, lighter: text on the panel
    juce::Colour successColour()   { return theme::accent2; }

    /** A small arc going round, for a request on its way. */
    void drawSpinner (juce::Graphics& g, juce::Rectangle<float> area, float phase, juce::Colour colour)
    {
        const auto size = juce::jmin (area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float> (size, size).withCentre (area.getCentre()).reduced (1.5f);
        const auto start = phase * juce::MathConstants<float>::twoPi;

        g.setColour (colour.withAlpha (0.25f));
        g.drawEllipse (box, 1.6f);

        juce::Path arc;
        arc.addCentredArc (box.getCentreX(), box.getCentreY(), box.getWidth() * 0.5f, box.getHeight() * 0.5f, 0.0f,
                           start, start + juce::MathConstants<float>::pi * 0.6f, true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::TextLayout layoutText (const juce::String& text, const juce::Font& font, juce::Colour colour, float width,
                                 juce::Justification justification = juce::Justification::topLeft, float lineSpacing = 2.0f)
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.setJustification (justification);
        s.setLineSpacing (lineSpacing);
        s.append (text, font, colour);

        juce::TextLayout layout;
        layout.createLayout (s, width);
        return layout;
    }

    int textHeight (const juce::String& text, const juce::Font& font, int width, float lineSpacing = 2.0f)
    {
        return static_cast<int> (std::ceil (layoutText (text, font, theme::text, static_cast<float> (width),
                                                        juce::Justification::topLeft, lineSpacing).getHeight()));
    }

    //==============================================================================
    // The activation screen's card, at the interface's design size. It fits ArtFreq FX's window
    // (720 x 396 under its top bar) with room around it, with a message of up to six lines.
    namespace card
    {
        constexpr int width = 400;
        constexpr int pad = 24;
        constexpr int top = 22;
        constexpr int bottom = 18;
        constexpr int emblemHeight = 44;
        constexpr int titleHeight = 24;
        constexpr int subtitleHeight = 18;
        constexpr int fieldHeight = 34;
        constexpr int buttonHeight = 32;
        constexpr int linksHeight = 16;
        constexpr int minMessageLines = 2;
        constexpr int maxMessageLines = 6;
    } // namespace card

    juce::Font titleFont()      { return fonts::ui (18.0f, fonts::UiWeight::semiBold); }
    juce::Font subtitleFont()   { return fonts::ui (theme::controlSize); }
    juce::Font productFont()    { return fonts::ui (theme::controlSize, fonts::UiWeight::medium); }
    juce::Font detailFont()     { return fonts::ui (12.5f); }

    int messageLineHeight()
    {
        return static_cast<int> (std::ceil (SelectableText::messageFont().getHeight()));
    }
} // namespace

//==============================================================================
juce::String formatTypedKey (const juce::String& typed)
{
    auto key = LicenseKey::normalise (typed);

    // More than a key: a key among other words, if there is one; else what was typed, cut to a
    // key's length (the checksum then says whether it is one).
    if (key.length() > LicenseKey::length || (key.length() == LicenseKey::length && ! LicenseKey::isWellFormed (key)))
    {
        const auto found = findKeyIn (typed);

        if (found.isNotEmpty())
            key = found;
        else
            key = key.substring (0, LicenseKey::length);
    }

    juce::String shown;

    for (int i = 0; i < key.length(); ++i)
    {
        if (i > 0 && i % 5 == 0)
            shown << "-";

        shown << juce::String::charToString (key[i]);
    }

    return shown;
}

int countKeyCharacters (const juce::String& typed)
{
    return LicenseKey::normalise (typed).length();
}

int caretAfterKeyCharacters (int keyCharacters)
{
    // A dash comes before every group but the first, once the group has a character.
    return keyCharacters + (keyCharacters > 0 ? (keyCharacters - 1) / 5 : 0);
}

//==============================================================================
KeyTextBox::KeyTextBox()
{
    setComponentID ("licence-key");
    setTitle ("License key");
    setDescription ("The key from your purchase email: XXXXX-XXXXX-XXXXX-XXXXX");
    setMultiLine (false);
    setReturnKeyStartsNewLine (false);
    setSelectAllWhenFocused (false);
    setJustification (juce::Justification::centred);
    setIndents (8, 0);
    setFont (fonts::mono (15.0f, true));
    setTextToShowWhenEmpty ("XXXXX-XXXXX-XXXXX-XXXXX", theme::textFaint);
    setKeyboardType (juce::TextInputTarget::textKeyboard);
}

void KeyTextBox::setKey (const juce::String& typed)
{
    setText (formatTypedKey (typed), false);
    moveCaretToEnd();
}

bool KeyTextBox::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::returnKey))
    {
        // The typing is done (the focus goes back, so the next space bar is the DAW's), and
        // the key is sent.
        handKeyFocusBack (*this);

        if (onActivate != nullptr)
            onActivate();

        return true;
    }

    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        handKeyFocusBack (*this);
        return true;
    }

    // No key holds a space: the typing ends and the space bar goes on to the DAW, as from a
    // number field (see ui/KeyFocus.h).
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        handKeyFocusBack (*this);
        return false;
    }

    const auto used = juce::TextEditor::keyPressed (key);
    reformat();
    return used;
}

void KeyTextBox::performPopupMenuAction (int menuItemID)
{
    juce::TextEditor::performPopupMenuAction (menuItemID);
    reformat();
}

void KeyTextBox::reformat()
{
    // Typed, pasted, deleted or undone: shown again in groups of five, the caret after the same
    // character of the key (a key found among other words: after the key).
    const auto text = getText();
    const auto formatted = formatTypedKey (text);

    if (formatted == text)
        return;

    const auto before = countKeyCharacters (text.substring (0, getCaretPosition()));
    const auto total = countKeyCharacters (formatted);
    const auto found = countKeyCharacters (text) > LicenseKey::length;

    setText (formatted, false);
    setCaretPosition (caretAfterKeyCharacters (found ? total : juce::jmin (before, total)));
}

//==============================================================================
SelectableText::SelectableText()
{
    setMultiLine (true, true);
    setReadOnly (true);
    setCaretVisible (false);
    setScrollbarsShown (false);
    setIndents (0, 0);
    setBorder ({});
    setJustification (juce::Justification::centredTop);
    setFont (messageFont());
    setLineSpacing (1.0f);

    setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
}

juce::Font SelectableText::messageFont()
{
    return fonts::ui (12.5f);
}

void SelectableText::setMessageFont (const juce::Font& newFont)
{
    font = newFont;
    setFont (font);
    applyFontToAllText (font, true);
}

int SelectableText::getLineHeight() const
{
    return static_cast<int> (std::ceil (font.getHeight()));
}

void SelectableText::setMessage (const juce::String& text, juce::Colour colour)
{
    message = text;
    setColour (juce::TextEditor::textColourId, colour);
    setText (text, false);
    applyColourToAllText (colour, true);
    setTitle (text);   // what a screen reader (and a test) reads
}

int SelectableText::getHeightFor (int width) const
{
    if (message.isEmpty())
        return 0;

    // As the text box wraps it: whole lines of the font.
    const auto layout = layoutText (message, font, theme::text, static_cast<float> (width));
    return layout.getNumLines() * getLineHeight();
}

void SelectableText::mouseDoubleClick (const juce::MouseEvent& e)
{
    // The address is three words to the text box (it stops at '@' and '.'): one, here.
    const juce::String address (anymeans::license::SUPPORT_EMAIL);
    const auto at = getTextIndexAt (e.getPosition());

    for (auto start = message.indexOf (address); start >= 0; start = message.indexOf (start + 1, address))
    {
        if (at >= start && at <= start + address.length())
        {
            setHighlightedRegion ({ start, start + address.length() });
            return;
        }
    }

    juce::TextEditor::mouseDoubleClick (e);
}

//==============================================================================
LinkButton::LinkButton (const juce::String& buttonText, const juce::String& linkUrl)
    : juce::Button (buttonText), url (linkUrl)
{
    setTitle (buttonText);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);

    onClick = [this]
    {
        if (onOpen != nullptr)
            onOpen (url);
        else
            juce::URL (url).launchInDefaultBrowser();
    };
}

juce::Font LinkButton::linkFont()
{
    return fonts::ui (12.0f, fonts::UiWeight::medium);
}

int LinkButton::getIdealWidth() const
{
    return shell::textWidth (linkFont(), getButtonText()) + 2;
}

void LinkButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto colour = down ? theme::accent : highlighted ? theme::accent.brighter (0.45f) : theme::accent.brighter (0.25f);
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (colour);
    g.setFont (linkFont());
    g.drawText (getButtonText(), bounds, juce::Justification::centred, false);

    if (highlighted)
    {
        const auto textW = static_cast<float> (shell::textWidth (linkFont(), getButtonText()));
        const auto x = bounds.getCentreX() - textW * 0.5f;
        g.fillRect (x, bounds.getCentreY() + 7.0f, textW, 1.0f);
    }
}

//==============================================================================
ActivationScreen::ActivationScreen (ArtFreqProcessor& p)
    : processor (p),
      license (p.getLicense()),
      effect (p.isEffect()),
      keyLink ("Where's my key?", anymeans::license::STORE_URL),
      buyLink (p.isEffect() ? "Buy ArtFreq Pro" : "Buy ArtFreq", anymeans::license::STORE_URL)
{
    setComponentID ("activation");
    setWantsKeyboardFocus (true);
    setKeepsKeyFocus (*this);   // while it is shown: Return activates, nothing else is ours

    keyField.onActivate = [this] { activate(); };
    addAndMakeVisible (keyField);

    activateButton.setComponentID ("activate");
    activateButton.onClick = [this]
    {
        if (showingSuccess)
            continuePressed();
        else
            activate();
    };
    addAndMakeVisible (activateButton);

    messageText.setComponentID ("activation-message");
    addAndMakeVisible (messageText);

    keyLink.setComponentID ("where-is-my-key");
    keyLink.setTooltip ("Your key is in the email you got with your purchase. Your orders are also on anymeansaudio.com");
    buyLink.setComponentID ("buy");
    buyLink.setTooltip (effect ? "ArtFreq FX comes with ArtFreq Pro: anymeansaudio.com" : "anymeansaudio.com");
    addAndMakeVisible (keyLink);
    addAndMakeVisible (buyLink);

    license.addListener (this);
    update();
}

ActivationScreen::~ActivationScreen()
{
    license.removeListener (this);

    // Closing the window stops the request this screen sent (its result would go nowhere).
    if (waitingForResult && license.getRequestInProgress() == LicenseManager::Request::activating)
        license.cancelRequest();
}

bool ActivationScreen::isBusy() const noexcept
{
    return license.getRequestInProgress() == LicenseManager::Request::activating;
}

void ActivationScreen::activate()
{
    if (showingSuccess || license.isBusy())
        return;

    setMessage ({}, MessageKind::none);

    // Set first: starting the request tells the listeners (the editor among them) at once.
    waitingForResult = true;

    const auto started = license.activate (keyField.getText(), [safe = juce::Component::SafePointer<ActivationScreen> (this)] (const Result& result)
    {
        if (safe != nullptr)
            safe->finished (result);
    });

    if (! started)
        waitingForResult = false;

    update();
}

void ActivationScreen::finished (const Result& result)
{
    waitingForResult = false;

    if (result.outcome == Outcome::activated)
    {
        showingSuccess = true;
        keyField.clear();
        setMessage (result.message, MessageKind::success);
    }
    else
    {
        setMessage (result.message, MessageKind::error);
    }

    update();

    if (onStateChanged != nullptr)
        onStateChanged();
}

void ActivationScreen::continuePressed()
{
    showingSuccess = false;
    setMessage ({}, MessageKind::none);
    update();

    if (onStateChanged != nullptr)
        onStateChanged();
}

void ActivationScreen::showResult (const Result& result)
{
    showingSuccess = false;
    setMessage (result.message, result.succeeded() ? MessageKind::info : MessageKind::error);
    update();
}

void ActivationScreen::focusKeyField()
{
    if (keyField.isShowing() && ! keyField.isReadOnly())
        keyField.grabKeyboardFocus();
}

bool ActivationScreen::handleKey (const juce::KeyPress& key)
{
    const auto returnKey = key.isKeyCode (juce::KeyPress::returnKey);

    if (showingSuccess && (returnKey || key.isKeyCode (juce::KeyPress::escapeKey)))
    {
        continuePressed();
        return true;
    }

    // Return sends what is typed (also with the focus on the screen rather than in the field).
    if (returnKey && ! showingSuccess && keyField.getText().isNotEmpty())
    {
        activate();
        return true;
    }

    return false;
}

void ActivationScreen::setMessage (const juce::String& text, MessageKind kind)
{
    messageKind = text.isEmpty() ? MessageKind::none : kind;

    const auto colour = messageKind == MessageKind::error   ? errorColour()
                      : messageKind == MessageKind::success ? successColour()
                                                            : theme::textDim;
    messageText.setMessage (text, colour);
}

void ActivationScreen::licenseStateChanged (LicenseManager&)
{
    // Locked again (deactivated here or elsewhere) while the success was still shown: that is
    // over.
    if (showingSuccess && ! license.isActivated())
    {
        showingSuccess = false;
        setMessage ({}, MessageKind::none);
    }

    update();
}

void ActivationScreen::update()
{
    const auto busy = isBusy();

    keyField.setReadOnly (busy || showingSuccess);
    keyField.setVisible (! showingSuccess);
    keyLink.setVisible (! showingSuccess);
    buyLink.setVisible (! showingSuccess);

    activateButton.setButtonText (showingSuccess ? juce::String ("Continue")
                                : busy           ? license.getStatusText()
                                                 : juce::String ("Activate"));
    activateButton.setTooltip (showingSuccess ? juce::String() : juce::String ("Activate this computer with your key (Return)"));

    if (busy && ! isTimerRunning())
        startTimerHz (30);
    else if (! busy && isTimerRunning())
        stopTimer();

    resized();
    repaint();
}

void ActivationScreen::timerCallback()
{
    spinnerPhase = std::fmod (spinnerPhase + 0.035f, 1.0f);
    repaint (activateButton.getBounds());
}

void ActivationScreen::resized()
{
    const auto inner = card::width - 2 * card::pad;
    const auto lines = juce::jlimit (card::minMessageLines, card::maxMessageLines,
                                     messageText.getHeightFor (inner) / messageLineHeight());
    const auto messageHeight = lines * messageLineHeight();

    auto height = card::top + card::emblemHeight + 10 + card::titleHeight;

    if (showingSuccess)
        height += 12 + messageHeight + 12 + card::buttonHeight;
    else
        height += 2 + card::subtitleHeight + 16 + card::fieldHeight + 10 + card::buttonHeight + 12 + messageHeight + 6 + card::linksHeight;

    height += card::bottom;

    card = juce::Rectangle<int> (card::width, height).withCentre (getLocalBounds().getCentre());
    card.setY (juce::jmax (8, card.getY()));

    const auto x = card.getX() + card::pad;
    auto y = card.getY() + card::top;

    emblemArea = { card.getCentreX() - 26, y, 52, card::emblemHeight };
    y += card::emblemHeight + 10;
    titleArea = { x, y, inner, card::titleHeight };
    y += card::titleHeight;

    if (showingSuccess)
    {
        subtitleArea = {};
        y += 12;
        messageText.setBounds (x, y, inner, messageHeight);
        y += messageHeight + 12;
        activateButton.setBounds (card.getCentreX() - 80, y, 160, card::buttonHeight);
        linksArea = {};
        return;
    }

    y += 2;
    subtitleArea = { x, y, inner, card::subtitleHeight };
    y += card::subtitleHeight + 16;

    keyField.setBounds (x, y, inner, card::fieldHeight);
    y += card::fieldHeight + 10;

    activateButton.setBounds (x, y, inner, card::buttonHeight);
    y += card::buttonHeight + 12;

    messageText.setBounds (x, y, inner, messageHeight);
    y += messageHeight + 6;

    // "Where's my key? · Buy ArtFreq", centred.
    linksArea = { x, y, inner, card::linksHeight };
    const auto dotWidth = 18;
    const auto total = keyLink.getIdealWidth() + dotWidth + buyLink.getIdealWidth();
    const auto linksX = card.getCentreX() - total / 2;
    keyLink.setBounds (linksX, y, keyLink.getIdealWidth(), card::linksHeight);
    buyLink.setBounds (keyLink.getRight() + dotWidth, y, buyLink.getIdealWidth(), card::linksHeight);
}

void ActivationScreen::paint (juce::Graphics& g)
{
    // The window behind, dimmed: in sight, out of reach.
    g.fillAll (theme::bg.withAlpha (0.84f));

    const auto bounds = card.toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (bounds, theme::panelCorner);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 30, { 0, 12 }).drawForPath (g, shape);

    g.setColour (theme::panel);
    g.fillPath (shape);
    g.setColour (theme::line.brighter (0.25f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), theme::panelCorner, 1.0f);

    drawEmblem (g, emblemArea.toFloat());

    const juce::String name (effect ? "ArtFreq FX" : "ArtFreq");
    g.setColour (theme::text);
    g.setFont (titleFont());
    g.drawText (showingSuccess ? name + " is activated" : "Activate " + name, titleArea, juce::Justification::centred, false);

    if (! showingSuccess)
    {
        g.setColour (theme::textDim);
        g.setFont (subtitleFont());
        g.drawText (effect ? "Enter the ArtFreq Pro key from your purchase email." : "Enter the key from your purchase email.",
                    subtitleArea, juce::Justification::centred, false);

        // Before anything has been tried: what activating means, where the message will go.
        if (messageText.getMessage().isEmpty() && ! isBusy())
        {
            const auto info = layoutText ("Once activated, " + name + " works offline. A key activates two computers.",
                                          SelectableText::messageFont(), theme::textFaint,
                                          static_cast<float> (messageText.getWidth()), juce::Justification::centredTop, 1.0f);
            info.draw (g, messageText.getBounds().toFloat());
        }

        g.setColour (theme::textFaint);
        g.setFont (LinkButton::linkFont());
        g.drawText (middleDot(), juce::Rectangle<int> (keyLink.getRight(), linksArea.getY(), buyLink.getX() - keyLink.getRight(), linksArea.getHeight()),
                    juce::Justification::centred, false);
    }
}

void ActivationScreen::paintOverChildren (juce::Graphics& g)
{
    // The request on its way: a spinner on the button, before its words.
    if (isBusy())
    {
        const auto button = activateButton.getBounds().toFloat();
        const auto textW = static_cast<float> (shell::textWidth (fonts::ui (theme::controlSize, fonts::UiWeight::medium), activateButton.getButtonText()));
        const auto spinner = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ button.getCentreX() - textW * 0.5f - 16.0f, button.getCentreY() });
        drawSpinner (g, spinner, spinnerPhase, juce::Colours::white);
    }
}

//==============================================================================
struct LicenceCard::Layout
{
    juce::Rectangle<int> caption;           // wide: "LICENCE", in the panel's label column
    juce::Rectangle<int> text;              // the status, the question, or the product's line
    juce::Rectangle<int> detail;            // the key (and "Activated on this computer")
    juce::Rectangle<int> status;            // narrow: "Activated on this computer" or the request on its way
    juce::Rectangle<int> message;           // a deactivation that failed: why
    juce::Rectangle<int> buttons;           // the row the buttons are right-aligned in
    int height = 0;
};

namespace
{
    namespace licence
    {
        constexpr int labelWidth = 76;   // the settings panel's label column
        constexpr int buttonHeight = 28;
        constexpr int lineHeight = 16;
        constexpr int gap = 8;

        // A message longer than this (a long folder path) scrolls in its place: the panel has to
        // fit the window. Three lines in ArtFreq FX's row, four in the instrument's column.
        constexpr int wideMessageLines = 3;
        constexpr int narrowMessageLines = 4;
    } // namespace licence
} // namespace

juce::Font LicenceCard::sentenceFont() const
{
    // ArtFreq FX's row has the height of its small window to keep to: a three-line question in
    // a slightly smaller, tighter text.
    return wide ? fonts::ui (12.0f) : detailFont();
}

float LicenceCard::sentenceSpacing() const
{
    return wide ? 0.0f : 2.0f;
}

LicenceCard::LicenceCard (ArtFreqProcessor& p, bool isWide)
    : processor (p),
      license (p.getLicense()),
      wide (isWide)
{
    setComponentID ("licence-card");

    deactivateButton.setComponentID ("deactivate");
    deactivateButton.setTooltip ("Free your key for another computer");
    deactivateButton.onClick = [this] { confirm (true); };

    cancelButton.setComponentID ("deactivate-cancel");
    cancelButton.onClick = [this] { confirm (false); };

    confirmButton.setComponentID ("deactivate-confirm");
    confirmButton.onClick = [this] { deactivate(); };

    activateButton.setComponentID ("licence-activate");
    activateButton.setTooltip ("Enter the key from your purchase email");
    activateButton.onClick = [this]
    {
        if (onActivateWanted != nullptr)
            onActivateWanted();
    };

    messageText.setComponentID ("licence-message");
    messageText.setJustification (juce::Justification::topLeft);

    // (ArtFreq FX's row: the smaller text of its question, so most messages take two lines.)
    if (wide)
        messageText.setMessageFont (sentenceFont());

    for (auto* child : std::initializer_list<juce::Component*> { &deactivateButton, &cancelButton, &confirmButton, &activateButton, &messageText })
        addChildComponent (child);

    license.addListener (this);
    update();
}

LicenceCard::~LicenceCard()
{
    license.removeListener (this);

    if (waitingForResult && license.getRequestInProgress() == LicenseManager::Request::deactivating)
        license.cancelRequest();
}

void LicenceCard::reset()
{
    confirming = false;

    if (! waitingForResult)
        messageText.setMessage ({}, errorColour());

    update();
}

void LicenceCard::confirm (bool shouldConfirm)
{
    confirming = shouldConfirm;

    if (shouldConfirm)
        messageText.setMessage ({}, errorColour());

    update();
}

void LicenceCard::deactivate()
{
    confirming = false;
    messageText.setMessage ({}, errorColour());
    waitingForResult = true;

    const auto started = license.deactivate ([safe = juce::Component::SafePointer<LicenceCard> (this)] (const Result& result)
    {
        if (safe == nullptr)
            return;

        safe->waitingForResult = false;

        if (result.succeeded())
        {
            safe->update();

            // Locked now: the editor shows it on the activation screen.
            if (safe->onDeactivated != nullptr)
                safe->onDeactivated (result);
        }
        else
        {
            // Nothing changed here: the activation stays, and the card says why.
            safe->messageText.setMessage (result.message, errorColour());
            safe->update();
        }
    });

    if (! started)
        waitingForResult = false;

    update();
}

void LicenceCard::licenseStateChanged (LicenseManager&)
{
    // Activated or deactivated elsewhere meanwhile: a question about the old state is moot.
    if (! license.getActivation().has_value())
        confirming = false;

    update();
}

LicenceCard::Layout LicenceCard::layoutFor (int width) const
{
    Layout l;
    const auto& activation = license.getActivation();
    const auto busy = license.getRequestInProgress() == LicenseManager::Request::deactivating;
    const auto hasMessage = messageText.getMessage().isNotEmpty();
    const auto x = wide ? licence::labelWidth : 0;
    const auto room = width - x;

    if (wide)
        l.caption = { 0, 0, licence::labelWidth, licence::lineHeight };

    // The buttons on the right, in the width they need.
    const auto buttonsWidth = ! activation.has_value() ? activateButton.getIdealWidth()
                            : confirming               ? cancelButton.getIdealWidth() + 8 + confirmButton.getIdealWidth()
                                                       : deactivateButton.getIdealWidth();

    if (! activation.has_value() || confirming)
    {
        // A sentence (the status, or the question) and the buttons: beside it where there is
        // room (wide), else under it, on the right.
        const auto sentence = ! activation.has_value() ? license.getStatusText() : license.getDeactivationWarning();

        if (wide)
        {
            const auto textWidth = room - buttonsWidth - 16;
            const auto sentenceHeight = textHeight (sentence, sentenceFont(), textWidth, sentenceSpacing());
            const auto h = juce::jmax (licence::buttonHeight, sentenceHeight);
            l.text = { x, 0, textWidth, h };

            // The caption on the sentence's first line (it is centred in the row).
            l.caption.setY ((h - sentenceHeight) / 2);
            l.caption.setHeight (static_cast<int> (std::ceil (sentenceFont().getHeight())) + 1);
            l.buttons = { width - buttonsWidth, (h - licence::buttonHeight) / 2, buttonsWidth, licence::buttonHeight };
            l.height = h;
        }
        else
        {
            const auto h = textHeight (sentence, sentenceFont(), room, sentenceSpacing());
            l.text = { x, 0, room, h };
            l.buttons = { width - buttonsWidth, h + licence::gap, buttonsWidth, licence::buttonHeight };
            l.height = l.buttons.getBottom();
        }

        return l;
    }

    // Activated: the product, the key, the state, Deactivate.
    if (wide)
    {
        const auto textWidth = room - buttonsWidth - 16;
        l.text = { x, 0, textWidth, licence::lineHeight };
        const auto messageHeight = hasMessage ? juce::jmin (messageText.getHeightFor (textWidth), licence::wideMessageLines * messageText.getLineHeight()) : 0;
        const auto detailHeight = juce::jmax (licence::lineHeight, messageHeight);
        l.detail = { x, licence::lineHeight, textWidth, detailHeight };

        if (hasMessage)
        {
            // A long one takes the product's line too, so the row stays as low as the window
            // needs (it says the computer stays activated).
            l.message = messageHeight > 2 * messageText.getLineHeight() ? juce::Rectangle<int> (x, 0, textWidth, messageHeight)
                                                                         : l.detail;
            l.detail = l.message;
        }

        l.height = juce::jmax (licence::buttonHeight + 4, l.detail.getBottom());
        l.buttons = busy ? juce::Rectangle<int>() : juce::Rectangle<int> (width - buttonsWidth, (l.height - licence::buttonHeight) / 2,
                                                                          buttonsWidth, licence::buttonHeight);
        return l;
    }

    auto y = 0;
    l.text = { x, y, room, licence::lineHeight };
    y += licence::lineHeight;

    if (hasMessage)
    {
        // Why it failed, in the key's place (the panel has to fit the window).
        y += 4;
        const auto h = juce::jmin (messageText.getHeightFor (room), licence::narrowMessageLines * messageText.getLineHeight());
        l.message = { x, y, room, h };
        y += h + licence::gap;
    }
    else
    {
        l.detail = { x, y, room, licence::lineHeight };
        y += licence::lineHeight + 4;
    }

    // The state on the left, Deactivate on the right, in one row.
    l.status = { x, y, room - buttonsWidth - 12, licence::buttonHeight };
    l.buttons = busy ? juce::Rectangle<int>() : juce::Rectangle<int> (width - buttonsWidth, y, buttonsWidth, licence::buttonHeight);
    l.height = y + licence::buttonHeight;
    return l;
}

int LicenceCard::getIdealHeight (int width) const
{
    return layoutFor (width).height;
}

void LicenceCard::update()
{
    const auto activated = license.getActivation().has_value();
    const auto busy = license.getRequestInProgress() == LicenseManager::Request::deactivating;

    activateButton.setVisible (! activated);
    deactivateButton.setVisible (activated && ! confirming && ! busy);
    cancelButton.setVisible (activated && confirming && ! busy);
    confirmButton.setVisible (activated && confirming && ! busy);
    messageText.setVisible (activated && ! confirming && messageText.getMessage().isNotEmpty());

    if (busy && ! isTimerRunning())
        startTimerHz (30);
    else if (! busy && isTimerRunning())
        stopTimer();

    resized();
    repaint();

    // A question or a message changes the card's height: the panel lays itself out again.
    const auto height = getWidth() > 0 ? getIdealHeight (getWidth()) : -1;

    if (height != lastHeight)
    {
        lastHeight = height;

        if (onHeightChanged != nullptr)
            onHeightChanged();
    }
}

void LicenceCard::timerCallback()
{
    spinnerPhase = std::fmod (spinnerPhase + 0.035f, 1.0f);
    repaint();
}

void LicenceCard::resized()
{
    const auto l = layoutFor (getWidth());

    if (! l.buttons.isEmpty())
    {
        auto row = l.buttons;
        activateButton.setBounds (row.removeFromRight (activateButton.getIdealWidth()));
        deactivateButton.setBounds (l.buttons.withLeft (l.buttons.getRight() - deactivateButton.getIdealWidth()));
        confirmButton.setBounds (l.buttons.withLeft (l.buttons.getRight() - confirmButton.getIdealWidth()));
        cancelButton.setBounds (confirmButton.getX() - 8 - cancelButton.getIdealWidth(), l.buttons.getY(),
                                cancelButton.getIdealWidth(), l.buttons.getHeight());
    }

    if (! l.message.isEmpty())
        messageText.setBounds (l.message);
}

void LicenceCard::paint (juce::Graphics& g)
{
    const auto l = layoutFor (getWidth());
    const auto& activation = license.getActivation();
    const auto busy = license.getRequestInProgress() == LicenseManager::Request::deactivating;

    if (wide)
        shell::drawCaps (g, "Licence", l.caption, juce::Justification::centredLeft, theme::textDim);

    if (! activation.has_value() || confirming)
    {
        const auto sentence = ! activation.has_value() ? license.getStatusText() : license.getDeactivationWarning();
        const auto layout = layoutText (sentence, sentenceFont(), confirming ? theme::text : theme::textDim,
                                        static_cast<float> (l.text.getWidth()), juce::Justification::topLeft, sentenceSpacing());
        layout.draw (g, l.text.toFloat().withSizeKeepingCentre (static_cast<float> (l.text.getWidth()), layout.getHeight()));
        return;
    }

    // "ArtFreq Pro: ArtFreq and ArtFreq FX" (a product that unlocks only itself: its name).
    const auto product = activation->productName == activation->pluginsUnlocked
                           ? activation->productName
                           : activation->productName + ": " + activation->pluginsUnlocked;
    if (! (messageText.isVisible() && messageText.getY() == l.text.getY()))
    {
        g.setColour (theme::text);
        g.setFont (productFont());
        g.drawText (product, l.text, juce::Justification::centredLeft, true);
    }

    // The key, masked, and where it is activated.
    const auto keyFont = fonts::mono (12.0f);
    const auto keyWidth = shell::textWidth (keyFont, activation->maskedKey);
    const juce::String here ("Activated on this computer");
    const auto busyText = license.getStatusText();

    // (A failure's message takes the key's place.)
    if (! messageText.isVisible())
    {
        g.setColour (theme::textDim);
        g.setFont (keyFont);
        g.drawText (activation->maskedKey, l.detail.withHeight (licence::lineHeight), juce::Justification::centredLeft, false);
    }

    if (wide)
    {
        // The second line: the key and its state, or the request on its way.
        if (! messageText.isVisible())
        {
            auto rest = l.detail.withHeight (licence::lineHeight).withTrimmedLeft (keyWidth + 8);

            if (busy)
            {
                drawSpinner (g, rest.removeFromLeft (14).toFloat(), spinnerPhase, theme::textDim);
                rest.removeFromLeft (6);
                g.setFont (detailFont());
                g.drawText (busyText, rest, juce::Justification::centredLeft, true);
            }
            else
            {
                g.setFont (detailFont());
                g.drawText (middleDot() + "  " + here, rest, juce::Justification::centredLeft, true);
            }
        }

        return;
    }

    auto status = l.status;
    g.setFont (detailFont());

    if (busy)
    {
        drawSpinner (g, status.removeFromLeft (14).toFloat(), spinnerPhase, theme::textDim);
        status.removeFromLeft (6);
        g.drawText (busyText, status, juce::Justification::centredLeft, true);
    }
    else if (! messageText.isVisible())
    {
        g.setColour (successColour().withAlpha (0.9f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ static_cast<float> (status.getX()) + 3.0f,
                                                                           static_cast<float> (status.getCentreY()) }));
        g.setColour (theme::textDim);
        g.drawText (here, status.withTrimmedLeft (12), juce::Justification::centredLeft, true);
    }
}

} // namespace artfreq::ui
