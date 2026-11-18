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

    /** What findKeyIn looks for: a key whose checksum holds, or anything in a key's shape. */
    enum class KeySearch { wellFormed, keyShaped };

    /** A key in pasted text that holds more than a key ("Your key: 8K3QX-7M2PD-..."): runs of
        letters and digits joined only by dashes or blanks that make a well-formed key. Or, with
        `keyShaped`, that look like one (groups of five, or all twenty together) whatever its
        checksum: a key with a typo, pasted with the email's words, is then shown as the key it
        is, and the checksum says it has a typo. */
    juce::String findKeyIn (const juce::String& text, KeySearch search = KeySearch::wellFormed)
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

                // In a key's shape, every run is a whole number of groups ("Key Y1F2F-..." is not
                // a key starting with "KEY").
                if (search == KeySearch::keyShaped && runs[last].characters.length() % 5 != 0)
                    break;

                candidate += runs[last].characters;
                const auto key = LicenseKey::normalise (candidate);

                if (key.length() > LicenseKey::length)
                    break;

                if (key.length() == LicenseKey::length && (search == KeySearch::keyShaped || LicenseKey::isWellFormed (key)))
                    return key;
            }
        }

        return {};
    }

    /** What the field holds after a paste. A paste is a key, or a piece of one: a key (or most
        of one, or text with a key in it) replaces whatever the field held, so a key pasted after
        a failed attempt is the one sent, never a mix of the two; a piece that completes the key
        typed so far, or is too short to be a key by itself, goes where the caret is. */
    juce::String textAfterPaste (const juce::String& current, juce::Range<int> replaced, const juce::String& pasted)
    {
        const auto inserted = current.replaceSection (replaced.getStart(), replaced.getLength(), pasted);

        if (findKeyIn (pasted).isNotEmpty())
            return pasted;

        if (LicenseKey::isWellFormed (LicenseKey::normalise (inserted)))
            return inserted;

        if (findKeyIn (pasted, KeySearch::keyShaped).isNotEmpty() || LicenseKey::normalise (pasted).length() >= 15)
            return pasted;

        return inserted;
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
    namespace cardMetrics
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
    } // namespace cardMetrics

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

    // More than a key: a key among other words, if there is one; else something in a key's
    // shape among them (a key with a typo); else what was typed, cut to a key's length (the
    // checksum then says whether it is one).
    if (key.length() > LicenseKey::length || (key.length() == LicenseKey::length && ! LicenseKey::isWellFormed (key)))
    {
        auto found = findKeyIn (typed);

        if (found.isEmpty() && key.length() > LicenseKey::length)
            found = findKeyIn (typed, KeySearch::keyShaped);

        key = found.isNotEmpty() ? found : key.substring (0, LicenseKey::length);
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

void KeyTextBox::insertTextAtCaret (const juce::String& text)
{
    // Typing comes one character at a time; more at once is a paste (Cmd+V, Shift+Insert, the
    // right-click menu), which may replace the whole key (see textAfterPaste).
    if (text.length() > 1 && ! isReadOnly())
    {
        const auto selected = getHighlightedRegion();
        const auto replaced = selected.isEmpty() ? juce::Range<int>::emptyRange (getCaretPosition()) : selected;
        setKey (textAfterPaste (getText(), replaced, text));
        return;
    }

    juce::TextEditor::insertTextAtCaret (text);
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
    : license (p.getLicense()),
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

    showingUnfinished = license.getInterruptedDeactivation().has_value() && ! license.isActivated();
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

    // A deactivation that didn't finish has just come to light: its explanation (in the
    // message's place) says more about the state than an older result does.
    const auto finishing = license.getInterruptedDeactivation().has_value() && ! license.isActivated();

    if (finishing && ! showingUnfinished && ! waitingForResult)
        setMessage ({}, MessageKind::none);

    showingUnfinished = finishing;
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

juce::String ActivationScreen::getInfoText() const
{
    const juce::String name (effect ? "ArtFreq FX" : "ArtFreq");

    // After a deactivation that was cut off (the project closed while it ran): why this computer
    // is locked, and the two ways on.
    if (license.getInterruptedDeactivation().has_value())
        return "Deactivating didn't finish, so this computer is locked. To free your key, deactivate in Settings; "
               "to use " + name + " here, activate again.";

    return "Once activated, " + name + " works offline. A key activates two computers.";
}

void ActivationScreen::resized()
{
    const auto inner = cardMetrics::width - 2 * cardMetrics::pad;
    const auto shownLines = messageText.getMessage().isNotEmpty()
                              ? messageText.getHeightFor (inner) / messageLineHeight()
                              : layoutText (getInfoText(), SelectableText::messageFont(), theme::text, static_cast<float> (inner),
                                            juce::Justification::centredTop, 1.0f).getNumLines();
    const auto lines = juce::jlimit (cardMetrics::minMessageLines, cardMetrics::maxMessageLines, shownLines);
    const auto messageHeight = lines * messageLineHeight();

    auto height = cardMetrics::top + cardMetrics::emblemHeight + 10 + cardMetrics::titleHeight;

    if (showingSuccess)
        height += 12 + messageHeight + 12 + cardMetrics::buttonHeight;
    else
        height += 2 + cardMetrics::subtitleHeight + 16 + cardMetrics::fieldHeight + 10 + cardMetrics::buttonHeight + 12 + messageHeight + 6 + cardMetrics::linksHeight;

    height += cardMetrics::bottom;

    card = juce::Rectangle<int> (cardMetrics::width, height).withCentre (getLocalBounds().getCentre());
    card.setY (juce::jmax (8, card.getY()));

    const auto x = card.getX() + cardMetrics::pad;
    auto y = card.getY() + cardMetrics::top;

    emblemArea = { card.getCentreX() - 26, y, 52, cardMetrics::emblemHeight };
    y += cardMetrics::emblemHeight + 10;
    titleArea = { x, y, inner, cardMetrics::titleHeight };
    y += cardMetrics::titleHeight;

    if (showingSuccess)
    {
        subtitleArea = {};
        y += 12;
        messageText.setBounds (x, y, inner, messageHeight);
        y += messageHeight + 12;
        activateButton.setBounds (card.getCentreX() - 80, y, 160, cardMetrics::buttonHeight);
        linksArea = {};
        return;
    }

    y += 2;
    subtitleArea = { x, y, inner, cardMetrics::subtitleHeight };
    y += cardMetrics::subtitleHeight + 16;

    keyField.setBounds (x, y, inner, cardMetrics::fieldHeight);
    y += cardMetrics::fieldHeight + 10;

    activateButton.setBounds (x, y, inner, cardMetrics::buttonHeight);
    y += cardMetrics::buttonHeight + 12;

    messageText.setBounds (x, y, inner, messageHeight);
    y += messageHeight + 6;

    // "Where's my key?", a dot, "Buy ArtFreq": centred.
    linksArea = { x, y, inner, cardMetrics::linksHeight };
    const auto dotWidth = 18;
    const auto total = keyLink.getIdealWidth() + dotWidth + buyLink.getIdealWidth();
    const auto linksX = card.getCentreX() - total / 2;
    keyLink.setBounds (linksX, y, keyLink.getIdealWidth(), cardMetrics::linksHeight);
    buyLink.setBounds (keyLink.getRight() + dotWidth, y, buyLink.getIdealWidth(), cardMetrics::linksHeight);
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

        // Before anything has been tried: what activating means, where the message will go (or
        // why it is locked after a deactivation that didn't finish).
        if (messageText.getMessage().isEmpty() && ! isBusy())
        {
            const auto finishing = license.getInterruptedDeactivation().has_value();
            const auto info = layoutText (getInfoText(), SelectableText::messageFont(), finishing ? theme::textDim : theme::textFaint,
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
    : license (p.getLicense()),
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
    // A deactivation on its way is not stopped (see the editor's Content): the manager finishes
    // it, and its result, which would come here, is dropped.
    license.removeListener (this);
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
    confirming = shouldConfirm && license.getActivation().has_value();

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

        if (result.succeeded() && safe->license.isActivated())
        {
            // Another key keeps this plug-in activated (an ArtFreq key, once an ArtFreq Pro key
            // went): the panel stays, and the card says what went and what stays.
            safe->messageText.setMessage (result.message, theme::textDim);
            safe->update();
        }
        else if (result.succeeded())
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

bool LicenceCard::isFinishingADeactivation() const
{
    // Not activated, with a deactivation that was cut off (the project closed while it ran): the
    // card offers to finish it, beside Activate...
    return ! license.getActivation().has_value() && license.getInterruptedDeactivation().has_value();
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
    const auto buttonsWidth = isFinishingADeactivation() ? confirmButton.getIdealWidth() + 8 + activateButton.getIdealWidth()
                            : ! activation.has_value()   ? activateButton.getIdealWidth()
                            : confirming                 ? cancelButton.getIdealWidth() + 8 + confirmButton.getIdealWidth()
                                                         : deactivateButton.getIdealWidth();

    if (! activation.has_value() || confirming)
    {
        // A sentence (the status, or the question) and the buttons: beside it where there is
        // room (wide), else under it, on the right. Not activated, a failed attempt to finish a
        // deactivation says why under the sentence.
        const auto sentence = ! activation.has_value() ? license.getStatusText() : license.getDeactivationWarning();
        const auto withMessage = hasMessage && ! activation.has_value();

        if (wide)
        {
            const auto textWidth = room - buttonsWidth - 16;
            const auto sentenceHeight = textHeight (sentence, sentenceFont(), textWidth, sentenceSpacing());
            const auto messageHeight = withMessage ? juce::jmin (messageText.getHeightFor (textWidth),
                                                                 licence::wideMessageLines * messageText.getLineHeight())
                                                   : 0;
            const auto contentHeight = sentenceHeight + (withMessage ? 2 + messageHeight : 0);
            const auto h = juce::jmax (licence::buttonHeight, contentHeight);
            const auto top = (h - contentHeight) / 2;
            l.text = { x, top, textWidth, sentenceHeight };

            if (withMessage)
                l.message = { x, l.text.getBottom() + 2, textWidth, messageHeight };

            // The caption on the sentence's first line.
            l.caption.setY (top);
            l.caption.setHeight (static_cast<int> (std::ceil (sentenceFont().getHeight())) + 1);
            l.buttons = { width - buttonsWidth, (h - licence::buttonHeight) / 2, buttonsWidth, licence::buttonHeight };
            l.height = h;
        }
        else
        {
            const auto h = textHeight (sentence, sentenceFont(), room, sentenceSpacing());
            l.text = { x, 0, room, h };
            auto y = h;

            if (withMessage)
            {
                const auto messageHeight = juce::jmin (messageText.getHeightFor (room), licence::narrowMessageLines * messageText.getLineHeight());
                l.message = { x, y + 4, room, messageHeight };
                y = l.message.getBottom();
            }

            l.buttons = { width - buttonsWidth, y + licence::gap, buttonsWidth, licence::buttonHeight };
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
    const auto finishing = isFinishingADeactivation();

    activateButton.setVisible (! activated && ! busy);
    deactivateButton.setVisible (activated && ! confirming && ! busy);
    cancelButton.setVisible (activated && confirming && ! busy);
    confirmButton.setVisible (((activated && confirming) || finishing) && ! busy);
    messageText.setVisible (! confirming && messageText.getMessage().isNotEmpty());

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

        // Finishing a deactivation that was cut off: Deactivate beside Activate...
        if (isFinishingADeactivation())
            confirmButton.setBounds (activateButton.getX() - 8 - confirmButton.getIdealWidth(), l.buttons.getY(),
                                     confirmButton.getIdealWidth(), l.buttons.getHeight());
        else
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
