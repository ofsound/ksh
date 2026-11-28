#pragma once
#include "KshStyle.h"
#include "../PluginProcessor.h"

namespace ksh::ui
{
    inline void paintProjectPane (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        g.setGradientFill (surfaceGradient (bounds, Palette::backgrounds, false, [] (juce::Colour colour) { return colour; }));
        g.fillRect (bounds);
    }

    class DiscardChanges : public juce::Component
    {
    public:
        DiscardChanges()
        {
            setTitle ("Unsaved changes");
            setAlwaysOnTop (true);
            setWantsKeyboardFocus (true);
            setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
            cancel.getProperties().set ("insetProjectArtwork", true);
            discard.getProperties().set ("insetProjectArtwork", true);
            addAndMakeVisible (cancel);
            addAndMakeVisible (discard);
            cancel.onClick = [this] { dismiss(); };
            discard.onClick = [this]
            {
                auto proceed = std::move (onDiscard);
                dismiss();
                if (proceed)
                    proceed();
            };
        }
        void open (const juce::String& projectName, std::function<void()> proceed)
        {
            if (isVisible())
                return;
            message = "\"" + projectName + "\" has changes that have not been saved. Discard them?";
            onDiscard = std::move (proceed);
            setVisible (true);
            toFront (false);
            enterModalState (false);
            if (isShowing())
                cancel.grabKeyboardFocus();
            repaint();
        }
        void dismiss()
        {
            onDiscard = {};
            exitModalState (0);
            setVisible (false);
            if (onClose)
                onClose();
        }
        std::function<void()> onClose;
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::escapeKey)
                dismiss();
            else
                return false;
            return true;
        }
        void resized() override
        {
            card = getLocalBounds().withSizeKeepingCentre (juce::jmin (560, getWidth() - 24), juce::jmin (224, getHeight() - 24));
            const int buttonHeight = getHeight() > 500 ? 64 : 44;
            discard.setBounds (card.getRight() - 208, card.getBottom() - buttonHeight - 16, 184, buttonHeight);
            cancel.setBounds (discard.getX() - 120, discard.getY(), 104, buttonHeight);
        }
        void paint (juce::Graphics& g) override
        {
            paintProjectPane (g, getLocalBounds().toFloat());
            const auto content = card.toFloat().reduced (24, 0);
            text (g, "Unsaved changes", content.withY (content.getY() + 20).withHeight (26), 20, Palette::text, true, juce::Justification::centredLeft);
            g.setColour (Palette::text);
            g.setFont (font (labelFontSize));
            g.drawFittedText (message, content.withY (content.getY() + 60).withHeight (static_cast<float> (discard.getY() - card.getY() - 72)).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
        }

    private:
        juce::Rectangle<int> card;
        juce::String message;
        std::function<void()> onDiscard;
        juce::TextButton cancel { "CANCEL" }, discard { "DISCARD CHANGES" };
    };

    class ProjectHeader : public juce::TextButton
    {
    public:
        ProjectHeader()
        {
            setWantsKeyboardFocus (false);
        }
        void setProjectName (const juce::String& value)
        {
            if (name == value)
                return;
            name = value;
            setTitle ("Project: " + name);
            setTooltip (name);
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            if (getProperties().contains ("phoneMenuIcon"))
            {
                auto bounds = getLocalBounds().toFloat().reduced (0.5f);
                controlSurface (g, bounds, isDown(), isOver());
                auto content = bounds.reduced (12, 0);
                const auto info = content.removeFromRight (24).withSizeKeepingCentre (20, 20);
                phoneMenu::drawIcon (g, phoneMenu::info, info, Palette::accent);
                ui::text (g, name, content.withTrimmedRight (8).withTrimmedTop (8).withHeight (28), 20, Palette::text, true, juce::Justification::centredLeft);
                ui::text (g, "Project details", content.withTrimmedTop (35).withHeight (22), 12, Palette::muted, false, juce::Justification::centredLeft);
                return;
            }
            controlSurface (g, getLocalBounds().toFloat().reduced (0.5f));
            g.setColour (Palette::text);
            g.setFont (font (getHeight() >= 60 ? 18.0f : 13.0f, true));
            g.drawText (name, getLocalBounds().withTrimmedLeft (18).withTrimmedRight (12),
                        juce::Justification::centredLeft, true);
        }

    private:
        juce::String name;
    };

}
