#pragma once
#include "BinaryData.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace ksh::ui
{
    inline juce::Path recordLockShape (bool locked)
    {
        juce::Path loop, shape, stroke;
        loop.startNewSubPath (9, 11);
        loop.lineTo (9, 8);
        loop.cubicTo (9, 4, 15, 4, 15, 8);
        loop.lineTo (15, locked ? 11.0f : 6.0f);
        juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroke, loop);
        shape.addRoundedRectangle (6, 11, 12, 10, 2);
        shape.addPath (stroke);
        return shape;
    }

    struct Palette
    {
        static inline const juce::Colour app { 0xff0b0d0e }, shell { 0xff121719 }, surface { 0xff192124 }, raised { 0xff223036 }, border { 0xff38535c }, text { 0xfff4fafb }, muted { 0xff95b4bb }, faint { 0xff6f8f97 }, accent { 0xfff1954a }, strong { accent }, info { 0xff67e8f9 }, danger { 0xfffb7185 };
        static inline const juce::Colour play { 0xff76d99b };
        static inline const juce::Colour recording { 0xffef4444 };
        static juce::Colour channel (int ch, bool colors = true)
        {
            // Track 2 shares the app and wordmark accent.
            static const std::array<juce::uint32, 8> values { 0xffdb4047, accent.getARGB(), 0xffd1a866, 0xff40add1, 0xff52b89c, 0xff8cb852, 0xffebad24, 0xffa66e3b };
            return colors ? juce::Colour (values[static_cast<size_t> (juce::jlimit (0, 7, ch))]) : accent;
        }
    };
    inline juce::Font font (float height, bool bold = false)
    {
        static auto regular = juce::Typeface::createSystemTypefaceFor (BinaryData::GantariRegular_ttf, BinaryData::GantariRegular_ttfSize);
        static auto heavy = juce::Typeface::createSystemTypefaceFor (BinaryData::GantariSemibold_ttf, BinaryData::GantariSemibold_ttfSize);
        auto f = juce::Font (juce::FontOptions (bold ? heavy : regular).withHeight (height * 1.18f));
        return f;
    }
    inline int dropdownWidth (const juce::String& label)
    {
        return juce::jmax (juce::GlyphArrangement::getStringWidthInt (font (13), label),
                          juce::GlyphArrangement::getStringWidthInt (font (13, true), label)) + 40;
    }
    inline void text (juce::Graphics& g, const juce::String& value, juce::Rectangle<float> bounds, float size = 12.0f, juce::Colour colour = Palette::text, bool bold = false, juce::Justification align = juce::Justification::centred)
    {
        g.setColour (colour);
        g.setFont (font (size, bold));
        g.drawText (value, bounds, align, true);
    }
    /** tone maps every colour drawn, e.g. to fade the surface toward a known backdrop. */
    template <typename Tone>
    void surface (juce::Graphics& g, juce::Rectangle<float> r, bool active, bool hover, Tone&& tone)
    {
        g.setGradientFill (juce::ColourGradient (tone (hover ? Palette::raised.brighter (0.12f) : Palette::raised), r.getTopLeft(), tone (Palette::app), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (tone (active ? Palette::accent : Palette::border.withAlpha (0.65f)));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (tone (Palette::text.withAlpha (0.06f)));
        g.drawLine (r.getX() + 5, r.getY() + 1, r.getRight() - 5, r.getY() + 1);
    }
    inline void surface (juce::Graphics& g, juce::Rectangle<float> r, bool active = false, bool hover = false)
    {
        surface (g, r, active, hover, [] (juce::Colour colour) { return colour; });
    }
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::TextEditor::backgroundColourId, Palette::app);
            setColour (juce::TextEditor::textColourId, Palette::text);
            setColour (juce::TextEditor::outlineColourId, Palette::border);
            setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
            setColour (juce::TextEditor::highlightColourId, Palette::accent.withAlpha (0.3f));
            setColour (juce::PopupMenu::backgroundColourId, Palette::shell);
            setColour (juce::PopupMenu::textColourId, Palette::text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::raised);
            setColour (juce::PopupMenu::highlightedTextColourId, Palette::accent);
        }
        void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            if (!editor.getProperties().contains ("projectMetadata"))
            {
                juce::LookAndFeel_V4::fillTextEditorBackground (g, width, height, editor);
                return;
            }
            g.setColour (Palette::app);
            g.fillRoundedRectangle (editor.getLocalBounds().toFloat().reduced (1.0f), 4.0f);
        }
        void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            if (!editor.getProperties().contains ("projectMetadata"))
            {
                juce::LookAndFeel_V4::drawTextEditorOutline (g, width, height, editor);
                return;
            }
            g.setColour (editor.hasKeyboardFocus (true) ? Palette::accent : Palette::border);
            g.drawRoundedRectangle (editor.getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.0f);
        }
        juce::Font getPopupMenuFont() override { return font (13); }
        void preparePopupMenuWindow (juce::Component& window) override
        {
            // Rounded corners must expose the content underneath, rather than
            // the white backing JUCE paints for an opaque menu window.
            window.setOpaque (false);
        }
        int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return 6; }
        void getIdealPopupMenuItemSizeWithOptions (const juce::String& label, bool separator, int standardHeight, int& width, int& height, const juce::PopupMenu::Options& options) override
        {
            width = dropdownWidth (label) - 2 * getPopupMenuBorderSizeWithOptions (options);
            height = separator ? 8 : standardHeight > 0 ? standardHeight : 30;
        }
        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            const auto bounds = juce::Rectangle<float> (0, 0, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
            g.setColour (Palette::shell);
            g.fillRoundedRectangle (bounds, 7.0f);
            g.setColour (Palette::border.withAlpha (0.8f));
            g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        }
        void drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted, const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&) override
        {
            const auto bounds = area.toFloat().reduced (6.0f, 1.0f);
            if (highlighted && item.isEnabled)
            {
                g.setColour (Palette::raised);
                g.fillRoundedRectangle (bounds, 4.0f);
            }
            else if (item.isTicked)
            {
                g.setColour (Palette::accent.withAlpha (0.08f));
                g.fillRoundedRectangle (bounds, 4.0f);
            }
            text (g, item.text, bounds.withTrimmedLeft (8).withTrimmedRight (20), 13,
                  !item.isEnabled ? Palette::faint : item.isTicked ? Palette::accent : Palette::text,
                  item.isTicked, juce::Justification::centredLeft);
            if (item.isTicked)
            {
                const float x = bounds.getRight() - 14, y = bounds.getCentreY();
                juce::Path tick;
                tick.startNewSubPath (x, y);
                tick.lineTo (x + 3, y + 3);
                tick.lineTo (x + 9, y - 4);
                g.setColour (Palette::accent);
                g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        juce::Font getTextButtonFont (juce::TextButton&, int height) override { return font (juce::jmin (14.0f, static_cast<float> (height) * 0.45f), true); }
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            if (b.getProperties().contains ("dropdown"))
            {
                surface (g, r, down || b.getToggleState(), hover);
                return;
            }
            if (b.getProperties().contains ("transport"))
            {
                const bool playing = b.getProperties()["icon"].toString() == "play";
                const auto colour = playing ? Palette::play : Palette::muted;
                g.setColour (Palette::surface.interpolatedWith (colour, down ? 0.35f : b.getToggleState() ? 0.22f : hover ? 0.16f : 0.07f));
                g.fillRoundedRectangle (r, 7);
                g.setColour (b.getToggleState() ? colour : Palette::border);
                g.drawRoundedRectangle (r, 7, 1);
                return;
            }
            if (b.getProperties().contains ("selectMode") || b.getProperties().contains ("layerSegment") || b.getProperties().contains ("historySegment"))
            {
                const bool history = b.getProperties().contains ("historySegment");
                const bool first = history ? static_cast<int> (b.getProperties()["historySegment"]) == 0
                                           : b.getProperties().contains ("layerSegment") && static_cast<int> (b.getProperties()["layerSegment"]) == 0;
                const bool last = history ? static_cast<int> (b.getProperties()["historySegment"]) == 1
                                          : b.getProperties().contains ("selectMode");
                juce::Path outline;
                outline.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 7, 7, first, last, first, last);
                g.setGradientFill (juce::ColourGradient (hover || down ? Palette::raised.brighter (0.12f) : Palette::raised, r.getTopLeft(), Palette::app, r.getBottomLeft(), false));
                g.fillPath (outline);
                if (b.getToggleState())
                {
                    g.setColour (Palette::accent.withAlpha (0.24f));
                    g.fillPath (outline);
                }
                g.setColour (b.getToggleState() ? Palette::accent : Palette::border);
                g.strokePath (outline, juce::PathStrokeType (1));
                return;
            }
            if (b.getProperties().contains ("icon"))
            {
                if (b.getProperties()["icon"].toString() == "info" || b.getProperties()["icon"].toString() == "recordLock")
                    return;
                if (b.isEnabled() && (hover || down) && static_cast<bool> (b.getProperties()["soloHint"]))
                {
                    g.setColour (Palette::accent.withAlpha (0.18f));
                    g.fillRoundedRectangle (r, 4);
                    g.setColour (Palette::accent);
                    g.drawRoundedRectangle (r, 4, 1);
                    return;
                }
                if (hover || down)
                {
                    g.setColour (Palette::raised);
                    g.fillRoundedRectangle (r, 4);
                }
                return;
            }
            if (b.getToggleState() || down)
            {
                g.setColour (Palette::strong);
                g.fillRoundedRectangle (r, 4);
            }
            else
            {
                g.setColour (hover ? Palette::raised.brighter (0.13f) : Palette::raised);
                g.fillRoundedRectangle (r, 4);
            }
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool hover, bool down) override
        {
            if (b.getProperties().contains ("dropdown"))
            {
                const auto bounds = b.getLocalBounds().toFloat();
                text (g, b.getButtonText(), bounds.withTrimmedLeft (14).withTrimmedRight (26), 13,
                      b.isEnabled() ? Palette::text : Palette::faint, true, juce::Justification::centredLeft);
                const float x = bounds.getRight() - 14, y = bounds.getCentreY();
                const float direction = b.getToggleState() ? -1.0f : 1.0f;
                juce::Path chevron;
                chevron.startNewSubPath (x - 4, y - direction * 2);
                chevron.lineTo (x, y + direction * 2);
                chevron.lineTo (x + 4, y - direction * 2);
                g.setColour (b.isEnabled() ? Palette::muted : Palette::faint);
                g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            const auto icon = b.getProperties()["icon"].toString();
            if (icon.isNotEmpty())
            {
                if (b.isEnabled() && (hover || down) && static_cast<bool> (b.getProperties()["soloHint"]))
                {
                    text (g, "S", b.getLocalBounds().toFloat(), juce::jmin (20.0f, b.getHeight() * 0.6f), Palette::accent, true);
                    return;
                }
                g.setColour (!b.isEnabled() ? Palette::faint.withAlpha (0.4f) : b.getToggleState() || down ? Palette::accent
                                                                                                           : Palette::muted);
                const auto r = b.getLocalBounds().toFloat();
                const bool directionIcon = icon == "forward" || icon == "reverse" || icon == "ping";
                const bool historyIcon = icon == "undo" || icon == "redo";
                // Match the compact power glyph without reducing the hit area.
                const float size = historyIcon ? juce::jmin (24.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.65f)
                                     : directionIcon ? juce::jmin (20.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.84f)
                                                 : juce::jmin (30.0f, r.getWidth() * 0.84f);
                g.addTransform (juce::AffineTransform::scale (size / 24).translated (r.getCentreX() - size / 2, r.getCentreY() - size / 2));
                juce::Path path;
                if (icon == "play")
                {
                    g.setColour (Palette::play);
                    path.addTriangle (7, 4.5f, 7, 19.5f, 20, 12);
                    g.fillPath (path);
                    return;
                }
                if (icon == "stop")
                {
                    g.setColour (b.getToggleState() ? Palette::text : Palette::muted);
                    g.fillRoundedRectangle (6, 6, 12, 12, 1.5f);
                    return;
                }
                if (icon == "clear")
                {
                    path.startNewSubPath (6, 6);
                    path.lineTo (18, 18);
                    path.startNewSubPath (18, 6);
                    path.lineTo (6, 18);
                }
                else if (icon == "record")
                {
                    const float pulse = b.getToggleState() && b.getProperties().contains ("recordPulse")
                                            ? static_cast<float> (b.getProperties()["recordPulse"]) : 1.0f;
                    g.setColour (Palette::recording.withAlpha (b.isEnabled() ? pulse : 0.3f));
                    g.fillEllipse (4, 4, 16, 16);
                    return;
                }
                else if (icon == "recordLock")
                {
                    const bool locked = b.getToggleState();
                    g.setColour (!b.isEnabled() ? Palette::faint : locked ? Palette::accent : Palette::text);
                    g.fillPath (recordLockShape (locked));
                    g.setColour (Palette::app);
                    g.fillEllipse (11, 14, 2, 2);
                    g.fillRect (11.5f, 15.0f, 1.0f, 3.0f);
                    return;
                }
                else if (icon == "previous" || icon == "next")
                {
                    const bool next = icon == "next";
                    path.addTriangle (next ? 6.0f : 18.0f, 4, next ? 6.0f : 18.0f, 20, next ? 20.0f : 4.0f, 12);
                    g.fillPath (path);
                    return;
                }
                else if (icon == "info")
                {
                    path.addEllipse (4, 4, 16, 16);
                    path.startNewSubPath (12, 11);
                    path.lineTo (12, 16);
                    g.fillEllipse (11, 7, 2, 2);
                }
                else if (icon == "rowOrder")
                {
                    g.setColour (Palette::text.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
                    const bool reversed = b.getToggleState();
                    path.startNewSubPath (5, reversed ? 19.0f : 5.0f);
                    path.lineTo (5, reversed ? 5.0f : 19.0f);
                    path.startNewSubPath (2, reversed ? 8.0f : 16.0f);
                    path.lineTo (5, reversed ? 5.0f : 19.0f);
                    path.lineTo (8, reversed ? 8.0f : 16.0f);
                    for (int row = 0; row < 3; ++row)
                    {
                        const float y = 6.0f + static_cast<float> (row) * 6.0f;
                        const float length = 4.0f + static_cast<float> (reversed ? 2 - row : row) * 3.0f;
                        path.startNewSubPath (11, y);
                        path.lineTo (11 + length, y);
                    }
                }
                else if (icon == "cellDisplay")
                {
                    path.addEllipse (4, 4, 16, 16);
                    juce::Path segment;
                    segment.addPieSegment (juce::Rectangle<float> (4, 4, 16, 16), 0, juce::MathConstants<float>::halfPi, 0);
                    g.fillPath (segment);
                }
                else if (icon == "power")
                {
                    path.addCentredArc (12, 12, 8, 8, 0, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
                    path.startNewSubPath (12, 2);
                    path.lineTo (12, 11);
                }
                else if (icon == "forward" || icon == "reverse")
                {
                    path.startNewSubPath (4, 12);
                    path.lineTo (20, 12);
                    path.startNewSubPath (15.5f, 7.5f);
                    path.lineTo (20, 12);
                    path.lineTo (15.5f, 16.5f);
                    if (icon == "reverse")
                        path.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi, 12, 12));
                }
                else if (icon == "ping")
                {
                    path.startNewSubPath (4, 11);
                    path.lineTo (4, 10);
                    path.quadraticTo (4, 7, 7, 7);
                    path.lineTo (20, 7);
                    path.startNewSubPath (17, 4);
                    path.lineTo (20, 7);
                    path.lineTo (17, 10);
                    path.startNewSubPath (20, 13);
                    path.lineTo (20, 14);
                    path.quadraticTo (20, 17, 17, 17);
                    path.lineTo (4, 17);
                    path.startNewSubPath (7, 14);
                    path.lineTo (4, 17);
                    path.lineTo (7, 20);
                }
                else
                {
                    path.startNewSubPath (6, 7);
                    path.cubicTo (9, 3, 15, 3, 18, 7);
                    path.cubicTo (22, 12, 18, 20, 12, 20);
                    path.cubicTo (9, 20, 7, 19, 5.5f, 17);
                    path.startNewSubPath (6, 2.5f);
                    path.lineTo (6, 7);
                    path.lineTo (10.5f, 7);
                    const auto centre = path.getBounds().getCentre();
                    path.applyTransform (juce::AffineTransform::translation (12 - centre.x, 12 - centre.y));
                    if (icon == "redo")
                        path.applyTransform (juce::AffineTransform::scale (-1, 1).translated (24, 0));
                }
                g.strokePath (path, juce::PathStrokeType (historyIcon ? 2.4f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            if (b.getProperties().contains ("selectMode") || b.getProperties().contains ("layerSegment"))
            {
                text (g, b.getButtonText(), b.getLocalBounds().toFloat(), 14.0f, Palette::text, true);
                return;
            }

            text (g, b.getButtonText(), b.getLocalBounds().toFloat(), b.getButtonText() == "M" ? 30.0f : 12.0f, !b.isEnabled() ? Palette::faint : (b.getToggleState() || down ? Palette::app : Palette::text), true);
        }
    };

    /** One hover-help destination for every TooltipClient in an editor's tree. */
    class FooterHelp : public juce::Label
    {
    public:
        explicit FooterHelp (juce::Component& editor) : owner (editor)
        {
            setTitle ("Control help");
            setFont (ksh::ui::font (13));
            setColour (juce::Label::textColourId, juce::Colours::white);
            setJustificationType (juce::Justification::centredLeft);
            setBorderSize (juce::BorderSize<int> (0));
            setMinimumHorizontalScale (1.0f);
            setInterceptsMouseClicks (false, false);
            owner.addMouseListener (this, true);
        }
        ~FooterHelp() override { owner.removeMouseListener (this); }
        void setHelpEnabled (bool enabled)
        {
            helpEnabled = enabled;
            if (!enabled)
                setText ({}, juce::dontSendNotification);
        }
        bool isHelpEnabled() const { return helpEnabled; }
        void showHelpFor (juce::Component* component)
        {
            juce::String help;
            if (helpEnabled && component != nullptr && owner.isParentOf (component))
                for (auto* target = component; target != &owner; target = target->getParentComponent())
                    if (auto* client = dynamic_cast<juce::TooltipClient*> (target))
                    {
                        help = client->getTooltip();
                        if (help.isNotEmpty())
                            break;
                    }
            setText (help, juce::dontSendNotification);
        }
        void refresh() { showHelpFor (juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse()); }
        void mouseEnter (const juce::MouseEvent& e) override { showHelpFor (e.eventComponent); }
        void mouseMove (const juce::MouseEvent& e) override { showHelpFor (e.eventComponent); }
        void mouseDown (const juce::MouseEvent& e) override { showHelpFor (e.eventComponent); }
        void mouseExit (const juce::MouseEvent& e) override { showHelpFor (e.source.getComponentUnderMouse()); }

    private:
        juce::Component& owner;
        bool helpEnabled = true;
    };

    class SettingsButton : public juce::TextButton
    {
    public:
        std::function<void()> showSettings;
        bool hitTest (int x, int y) override
        {
            if (getProperties()["icon"].toString() != "recordLock")
                return juce::TextButton::hitTest (x, y);
            const auto bounds = getLocalBounds().toFloat();
            const float size = juce::jmin (30.0f, bounds.getWidth() * 0.84f);
            if (size <= 0)
                return false;
            const auto point = (juce::Point<float> (static_cast<float> (x), static_cast<float> (y)) - bounds.getCentre()) * (24.0f / size) + juce::Point<float> (12, 12);
            return recordLockShape (getToggleState()).contains (point);
        }
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu() && showSettings)
                showSettings();
            else
                juce::TextButton::mouseDown (event);
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == juce::KeyPress::F10Key && key.getModifiers().isShiftDown() && showSettings)
            {
                showSettings();
                return true;
            }
            return juce::TextButton::keyPressed (key);
        }
    };

    /** Buttons with continuous gestures still use the native button accessibility handler. */
    class GestureButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void (const juce::MouseEvent&)> pressed, dragged, released;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (pressed)
                pressed (e);
            else
                juce::TextButton::mouseDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragged)
                dragged (e);
            else
                juce::TextButton::mouseDrag (e);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (released)
                released (e);
            else
                juce::TextButton::mouseUp (e);
        }
    };
    class AuditionLabel : public juce::Label
    {
    public:
        std::function<void()> audition;
        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::Label::mouseUp (e);
            if (e.getNumberOfClicks() == 1 && e.getDistanceFromDragStart() < 4 && audition)
                audition();
        }
    };

    /** Native numeric control: drag, arrows, wheel, double-click to type, context reset. */
    class ValueControl : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        ValueControl()
        {
            setWantsKeyboardFocus (true);
            input.setJustification (juce::Justification::centred);
            input.setInputRestrictions (12, "-0123456789");
            input.onReturnKey = [this] { acceptInput(); };
            input.onEscapeKey = [this] { input.setVisible (false); grabKeyboardFocus(); };
            input.onFocusLost = [this] { if (input.isVisible()) acceptInput(); };
            addChildComponent (input);
        }
        int value = 0, minimum = 0, maximum = 100, resetValue = 0;
        float sensitivity = 4;
        float valueFontSize = 14, captionFontSize = 10;
        int captionGap = 0;
        juce::String suffix, caption;
        bool mixed = false, showAdjustmentHintAtZero = false, horizontalAdjustment = false;
        std::function<juce::String (int)> format;
        std::function<void()> onBegin, onEnd, onClick;
        std::function<void()> onCancel;
        void cancelGesture()
        {
            const bool wasActive = dragging || wheeling || input.isVisible();
            dragging = wheeling = false;
            stopTimer();
            if (pendingWheel == this)
                pendingWheel = nullptr;
            input.setVisible (false);
            if (wasActive && onCancel)
                onCancel();
            repaint();
        }
        std::function<void (int)> onChange;
        void setValue (int v)
        {
            if (!dragging && !wheeling && !input.isVisible() && value != v)
            {
                value = v;
                repaint();
            }
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            if (caption.isNotEmpty())
            {
                text (g, caption, r.removeFromTop (18), captionFontSize, Palette::text, true, juce::Justification::centredLeft);
                r.removeFromTop (static_cast<float> (captionGap));
            }
            surface (g, r.reduced (0.5f), dragging || input.isVisible(), isMouseOver());
            if (horizontalAdjustment || (showAdjustmentHintAtZero && value == 0 && !mixed))
            {
                const auto centre = r.getCentre();
                const float halfHeight = juce::jmin (9.0f, r.getHeight() * 0.25f);
                const float wing = halfHeight * 0.45f;
                juce::Path arrows;
                arrows.startNewSubPath (centre.x, centre.y - halfHeight);
                arrows.lineTo (centre.x, centre.y + halfHeight);
                for (const float direction : { -1.0f, 1.0f })
                {
                    const float tip = centre.y + direction * halfHeight;
                    const float base = tip - direction * wing;
                    arrows.startNewSubPath (centre.x - wing, base);
                    arrows.lineTo (centre.x, tip);
                    arrows.lineTo (centre.x + wing, base);
                }
                if (horizontalAdjustment)
                    arrows.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, centre.x, centre.y));
                g.setColour (isEnabled() ? Palette::text : Palette::faint);
                g.strokePath (arrows, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            const auto label = mixed ? juce::String::fromUTF8 ("—") : format ? format (value) : juce::String (value) + suffix;
            // Reserve the whole numeric range for plain values, so suffixes stay
            // anchored even across 9/10 and 99/100. Formatted labels also use
            // equal-width digits without changing their nonnumeric text.
            const int slots = !mixed && !format ? juce::jmax (2, juce::String (minimum).length(), juce::String (maximum).length()) : 0;
            if (numericText != label || numericSlots != slots || std::abs (numericFontSize - valueFontSize) > 0.001f)
            {
                numericText = label;
                numericSlots = slots;
                numericFontSize = valueFontSize;
                const auto labelFont = font (valueFontSize, true);
                float digitWidth = 0;
                for (const auto digit : juce::String ("0123456789"))
                    digitWidth = juce::jmax (digitWidth, juce::GlyphArrangement::getStringWidth (labelFont, juce::String::charToString (digit)));
                numericBaseline = (labelFont.getAscent() - labelFont.getDescent()) * 0.5f;
                numericGlyphs.clear();
                numericWidth = slots > 0 ? static_cast<float> (juce::jmax (0, slots - juce::String (value).length())) * digitWidth : 0.0f;
                for (int i = 0; i < label.length();)
                {
                    const auto character = label[i];
                    if ((character >= '0' && character <= '9') || (slots > 0 && character == '-'))
                    {
                        juce::GlyphArrangement digit;
                        digit.addLineOfText (labelFont, juce::String::charToString (character), 0, 0);
                        const auto bounds = digit.getBoundingBox (0, -1, true);
                        digit.moveRangeOfGlyphs (0, -1, numericWidth + (digitWidth - bounds.getWidth()) * 0.5f - bounds.getX(), 0);
                        numericGlyphs.addGlyphArrangement (digit);
                        numericWidth += digitWidth;
                        ++i;
                    }
                    else
                    {
                        const int runStart = i++;
                        while (i < label.length() && !(label[i] >= '0' && label[i] <= '9'))
                            ++i;
                        const auto run = label.substring (runStart, i);
                        numericGlyphs.addLineOfText (labelFont, run, numericWidth, 0);
                        numericWidth += juce::GlyphArrangement::getStringWidth (labelFont, run);
                    }
                }
            }
            g.setColour (isEnabled() ? Palette::text : Palette::faint);
            numericGlyphs.draw (g, juce::AffineTransform::translation (r.getCentreX() - numericWidth * 0.5f, r.getCentreY() + numericBaseline));
        }
        void resized() override
        {
            input.setBounds (getLocalBounds().withTrimmedTop (caption.isNotEmpty() ? 18 + captionGap : 0));
            input.setFont (font (valueFontSize));
        }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            endWheelGesture();
            if (!isEnabled())
                return;
            if (e.mods.isPopupMenu())
            {
                if (onBegin)
                    onBegin();
                update (resetValue);
                if (onEnd)
                    onEnd();
                return;
            }
            if (isShowing())
                grabKeyboardFocus();
            start = value;
            dragging = true;
            if (onBegin)
                onBegin();
            repaint();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging)
            {
                const auto delta = horizontalAdjustment ? e.getDistanceFromDragStartX() : -e.getDistanceFromDragStartY();
                if (std::abs (delta) > 4)
                    update (start + (delta < 0 ? -1 : 1) * static_cast<int> (std::floor ((static_cast<float> (std::abs (delta)) - 4) / sensitivity + 0.5f)));
                else if (horizontalAdjustment)
                    update (start);
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (!dragging)
                return;
            dragging = false;
            if (onEnd)
                onEnd();
            if (e.getDistanceFromDragStart() < 3 && onClick)
                onClick();
            repaint();
        }
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (horizontalAdjustment)
                return;
            endWheelGesture();
            dragging = false;
            if (onEnd)
                onEnd();
            input.setText (juce::String (value), false);
            input.setVisible (true);
            input.grabKeyboardFocus();
            input.selectAll();
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            if (!isEnabled() || dragging || std::abs (w.deltaY) < 0.00001f)
                return;
            // A trackpad scroll arrives as many ticks: keep them in one undo step and host gesture.
            if (!wheeling)
            {
                if (onBegin)
                    onBegin();
                wheeling = true;
                pendingWheel = this;
            }
            update (value + (w.deltaY > 0 ? 1 : -1));
            startTimer (wheelGestureIdleMs);
        }
        /** Closes a pending scroll-wheel gesture now instead of after the idle timeout. */
        void endWheelGesture()
        {
            stopTimer();
            if (!wheeling)
                return;
            wheeling = false;
            if (pendingWheel == this)
                pendingWheel = nullptr;
            if (onEnd)
                onEnd();
        }
        /** Any new edit gesture must close an idle scroll first, or the two would share one undo step. */
        static void endPendingWheelGesture()
        {
            if (auto* control = pendingWheel.getComponent())
                control->endWheelGesture();
        }
        bool isWheelGestureActive() const { return wheeling; }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        {
            class Value : public juce::AccessibilityRangedNumericValueInterface
            {
            public:
                explicit Value (ValueControl& c) : control (c) {}
                bool isReadOnly() const override { return !control.isEnabled(); }
                double getCurrentValue() const override { return control.value; }
                void setValue (double v) override
                {
                    if (control.isEnabled())
                    {
                        control.endWheelGesture();
                        if (control.onBegin)
                            control.onBegin();
                        control.update (juce::roundToInt (v));
                        if (control.onEnd)
                            control.onEnd();
                    }
                }
                AccessibleValueRange getRange() const override { return { { static_cast<double> (control.minimum), static_cast<double> (control.maximum) }, 1 }; }

            private:
                ValueControl& control;
            };
            return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions {}, juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            const int increaseKey = horizontalAdjustment ? juce::KeyPress::rightKey : juce::KeyPress::upKey;
            const int decreaseKey = horizontalAdjustment ? juce::KeyPress::leftKey : juce::KeyPress::downKey;
            if (k == increaseKey || k == decreaseKey)
            {
                endWheelGesture();
                if (onBegin)
                    onBegin();
                update (value + (k == increaseKey ? 1 : -1));
                if (onEnd)
                    onEnd();
                return true;
            }
            return false;
        }

    private:
        static constexpr int wheelGestureIdleMs = 350;
        juce::TextEditor input;
        static inline juce::Component::SafePointer<ValueControl> pendingWheel;
        juce::GlyphArrangement numericGlyphs;
        juce::String numericText;
        int numericSlots = -1;
        float numericFontSize = 0, numericWidth = 0, numericBaseline = 0;
        bool dragging = false, wheeling = false;
        int start = 0;
        void timerCallback() override { endWheelGesture(); }
        void update (int next)
        {
            next = juce::jlimit (minimum, maximum, next);
            if (value != next)
            {
                value = next;
                mixed = false;
                if (onChange)
                    onChange (next);
                repaint();
            }
        }
        void acceptInput()
        {
            const int next = input.getText().getIntValue();
            input.setVisible (false);
            endWheelGesture();
            if (onBegin)
                onBegin();
            update (next);
            if (onEnd)
                onEnd();
        }
    };
}
