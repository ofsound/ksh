#pragma once
#include "BinaryData.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace ksh::ui
{
    struct Palette
    {
        static inline const juce::Colour app { 0xff0b0d0e }, shell { 0xff121719 }, surface { 0xff192124 }, raised { 0xff223036 }, border { 0xff38535c }, text { 0xfff4fafb }, muted { 0xff95b4bb }, faint { 0xff6f8f97 }, accent { 0xfffbc04e }, strong { 0xffe8ad38 }, info { 0xff67e8f9 }, danger { 0xfffb7185 };
        static juce::Colour channel (int ch, bool colors = true)
        {
            static constexpr std::array<juce::uint32, 8> values { 0xffdb4047, 0xffed8c5c, 0xffd1a866, 0xff40add1, 0xff52b89c, 0xff8cb852, 0xffebad24, 0xffa66e3b };
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
    inline void text (juce::Graphics& g, const juce::String& value, juce::Rectangle<float> bounds, float size = 12.0f, juce::Colour colour = Palette::text, bool bold = false, juce::Justification align = juce::Justification::centred)
    {
        g.setColour (colour);
        g.setFont (font (size, bold));
        g.drawText (value, bounds, align, true);
    }
    inline void surface (juce::Graphics& g, juce::Rectangle<float> r, bool active = false, bool hover = false)
    {
        g.setGradientFill (juce::ColourGradient (hover ? Palette::raised.brighter (0.12f) : Palette::raised, r.getTopLeft(), Palette::app, r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (active ? Palette::accent : Palette::border.withAlpha (0.65f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (Palette::text.withAlpha (0.06f));
        g.drawLine (r.getX() + 5, r.getY() + 1, r.getRight() - 5, r.getY() + 1);
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
            setColour (juce::TooltipWindow::backgroundColourId, Palette::raised);
            setColour (juce::TooltipWindow::textColourId, Palette::text);
        }
        void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            if (!editor.getProperties().contains ("projectMetadata"))
            {
                juce::LookAndFeel_V4::fillTextEditorBackground (g, width, height, editor);
                return;
            }
            g.setColour (Palette::app);
            g.fillRoundedRectangle (editor.getLocalBounds().toFloat().reduced (0.5f), 4.0f);
        }
        void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
        {
            if (!editor.getProperties().contains ("projectMetadata"))
            {
                juce::LookAndFeel_V4::drawTextEditorOutline (g, width, height, editor);
                return;
            }
            g.setColour (editor.hasKeyboardFocus (true) ? Palette::accent : Palette::border);
            g.drawRoundedRectangle (editor.getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
        }
        juce::Font getPopupMenuFont() override { return font (13); }
        juce::Font getTextButtonFont (juce::TextButton&, int height) override { return font (juce::jmin (14.0f, static_cast<float> (height) * 0.45f), true); }
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            if (b.getProperties().contains ("icon") || b.getProperties().contains ("heading"))
            {
                if (hover || down || b.hasKeyboardFocus (true))
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
            if (b.hasKeyboardFocus (true))
            {
                g.setColour (Palette::accent);
                g.drawRoundedRectangle (r, 4, 1);
            }
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
        {
            const auto icon = b.getProperties()["icon"].toString();
            if (icon.isNotEmpty())
            {
                g.setColour (!b.isEnabled() ? Palette::faint.withAlpha (0.4f) : b.getToggleState() || down ? Palette::accent
                                                                                                           : Palette::muted);
                const auto r = b.getLocalBounds().toFloat();
                const float size = juce::jmin (30.0f, r.getWidth() * 0.84f);
                g.addTransform (juce::AffineTransform::scale (size / 24).translated (r.getCentreX() - size / 2, r.getCentreY() - size / 2));
                juce::Path path;
                if (icon == "clear")
                {
                    path.startNewSubPath (6, 6);
                    path.lineTo (18, 18);
                    path.startNewSubPath (18, 6);
                    path.lineTo (6, 18);
                }
                else if (icon == "record")
                {
                    g.fillEllipse (4, 4, 16, 16);
                    return;
                }
                else if (icon == "previous" || icon == "next")
                {
                    const bool next = icon == "next";
                    path.addTriangle (next ? 6.0f : 18.0f, 4, next ? 6.0f : 18.0f, 20, next ? 20.0f : 4.0f, 12);
                    g.fillPath (path);
                    return;
                }
                else if (icon == "power")
                {
                    path.addCentredArc (12, 12, 8, 8, 0, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
                    path.startNewSubPath (12, 2);
                    path.lineTo (12, 11);
                }
                else if (icon == "forward" || icon == "reverse")
                {
                    path.startNewSubPath (3, 12);
                    path.lineTo (21, 12);
                    path.startNewSubPath (14, 5);
                    path.lineTo (21, 12);
                    path.lineTo (14, 19);
                    if (icon == "reverse")
                        path.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi, 12, 12));
                }
                else
                {
                    path.startNewSubPath (4, 9);
                    path.lineTo (15, 9);
                    path.quadraticTo (21, 9, 21, 15);
                    path.quadraticTo (21, 21, 15, 21);
                    path.lineTo (9, 21);
                    path.startNewSubPath (9, 3);
                    path.lineTo (3, 9);
                    path.lineTo (9, 15);
                    if (icon == "redo")
                        path.applyTransform (juce::AffineTransform::scale (-1, 1).translated (24, 0));
                }
                g.strokePath (path, juce::PathStrokeType (2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }
            if (b.getProperties().contains ("heading"))
            {
                static auto headingFace = juce::Typeface::createSystemTypefaceFor (BinaryData::GantariExtrabold_ttf, BinaryData::GantariExtrabold_ttfSize);
                g.setFont (juce::Font (juce::FontOptions (headingFace).withHeight (56.6f)));
                g.setColour (Palette::accent);
                g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centredLeft, 1, 0.6f);
                return;
            }
            text (g, b.getButtonText(), b.getLocalBounds().toFloat(), b.getButtonText() == "M" ? 30.0f : 12.0f, !b.isEnabled() ? Palette::faint : (b.getToggleState() || down ? Palette::app : Palette::text), true);
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
    class ValueControl : public juce::Component, public juce::SettableTooltipClient
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
        juce::String suffix, caption;
        bool mixed = false;
        std::function<juce::String (int)> format;
        std::function<void()> onBegin, onEnd, onClick;
        std::function<void (int)> onChange;
        void setValue (int v)
        {
            if (!dragging && !input.isVisible() && value != v)
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
                text (g, caption, r.removeFromTop (18), 10, Palette::text, true, juce::Justification::centredLeft);
            }
            surface (g, r.reduced (0.5f), dragging || hasKeyboardFocus (true), isMouseOver());
            text (g, mixed ? juce::String (juce::String::fromUTF8 ("—")) : format ? format (value)
                                                                                  : juce::String (value) + suffix,
                r,
                14,
                isEnabled() ? Palette::text : Palette::faint,
                true);
        }
        void resized() override
        {
            input.setBounds (getLocalBounds().withTrimmedTop (caption.isNotEmpty() ? 18 : 0));
            input.setFont (font (14));
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
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
                const auto delta = -e.getDistanceFromDragStartY();
                if (std::abs (delta) > 4)
                    update (start + (delta < 0 ? -1 : 1) * static_cast<int> (std::floor ((static_cast<float> (std::abs (delta)) - 4) / sensitivity + 0.5f)));
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
            if (!isEnabled() || std::abs (w.deltaY) < 0.00001f)
                return;
            if (onBegin)
                onBegin();
            update (value + (w.deltaY > 0 ? 1 : -1));
            if (onEnd)
                onEnd();
        }
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
            if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
            {
                if (onBegin)
                    onBegin();
                update (value + (k == juce::KeyPress::upKey ? 1 : -1));
                if (onEnd)
                    onEnd();
                return true;
            }
            return false;
        }

    private:
        juce::TextEditor input;
        bool dragging = false;
        int start = 0;
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
            if (onBegin)
                onBegin();
            update (next);
            if (onEnd)
                onEnd();
        }
    };
}
