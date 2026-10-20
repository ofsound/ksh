#pragma once
#include "KshStyle.h"
#include "../PluginProcessor.h"

namespace ksh::ui
{
    class DiscardChanges : public juce::Component
    {
    public:
        DiscardChanges()
        {
            setTitle ("Unsaved changes");
            setWantsKeyboardFocus (true);
            setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
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
        }
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
            discard.setBounds (card.getRight() - 184, card.getBottom() - 56, 160, 32);
            cancel.setBounds (discard.getX() - 96, discard.getY(), 80, 32);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.58f));
            surface (g, card.toFloat());
            const auto content = card.toFloat().reduced (24, 0);
            text (g, "Unsaved changes", content.withY (content.getY() + 20).withHeight (26), 20, Palette::text, true, juce::Justification::centredLeft);
            g.setColour (Palette::text);
            g.setFont (font (14));
            g.drawFittedText (message, content.withY (content.getY() + 66).withHeight (card.getHeight() - 138).toNearestInt(), juce::Justification::topLeft, 3);
        }

    private:
        juce::Rectangle<int> card;
        juce::String message;
        std::function<void()> onDiscard;
        juce::TextButton cancel { "Cancel" }, discard { "Discard Changes" };
    };

    class ProjectHeader : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ProjectHeader()
        {
            setWantsKeyboardFocus (false);
            addAndMakeVisible (info);
            info.setTitle ("Project details");
            info.setTooltip ("View project dates and edit the name, author and description");
            info.getProperties().set ("icon", "info");
            info.onClick = [this] { if (onDetails) onDetails(); };
        }
        std::function<void()> onDetails;
        void setProjectName (const juce::String& value)
        {
            if (name == value)
                return;
            name = value;
            setTitle ("Project: " + name);
            setTooltip (name);
            repaint();
        }
        void resized() override { info.setBounds (getWidth() - getHeight(), 0, getHeight(), getHeight()); }
        void paint (juce::Graphics& g) override
        {
            surface (g, getLocalBounds().toFloat().reduced (0.5f));
            g.setColour (Palette::text);
            g.setFont (font (13, true));
            g.drawText (name, getLocalBounds().withTrimmedRight (getHeight() + 8).reduced (12, 0), juce::Justification::centredLeft, true);
        }

    private:
        juce::String name;
        juce::TextButton info;
    };

    class ProjectDetails : public juce::Component
    {
    public:
        explicit ProjectDetails (PluginProcessor& p) : processor (p)
        {
            setTitle ("Project details");
            setWantsKeyboardFocus (true);
            setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
            for (auto* field : { &name, &author, &description })
            {
                addAndMakeVisible (*field);
                field->setFont (font (14));
                field->setIndents (10, 8);
                field->getProperties().set ("projectMetadata", true);
                field->onEscapeKey = [this] { dismiss(); };
            }
            name.setTitle ("Project name");
            author.setTitle ("Project author");
            description.setTitle ("Project description");
            name.setInputRestrictions (160);
            author.setInputRestrictions (160);
            description.setMultiLine (true);
            description.setReturnKeyStartsNewLine (true);
            description.setScrollbarsShown (true);
            description.setInputRestrictions (4000);
            author.setTextToShowWhenEmpty ("Author", Palette::faint);
            description.setTextToShowWhenEmpty ("Add a description", Palette::faint);
            for (auto* date : { &created, &modified })
            {
                addAndMakeVisible (*date);
                date->setFont (font (13));
                date->setColour (juce::Label::textColourId, Palette::text);
                date->setBorderSize ({});
                date->setEditable (false, false, false);
            }
            created.setTitle ("Created date, read only");
            modified.setTitle ("Last modified date, read only");
            addAndMakeVisible (cancel);
            addAndMakeVisible (apply);
            cancel.onClick = [this] { dismiss(); };
            apply.onClick = [this]
            {
                applyChanges();
                dismiss();
            };
        }
        std::function<void()> onClose;
        void open()
        {
            epoch = processor.getUiBridge().getDocumentEpoch();
            name.setText (processor.getProjectName(), false);
            author.setText (processor.getProjectAuthor(), false);
            description.setText (processor.getProjectDescription(), false);
            refresh();
            setVisible (true);
            toFront (true);
            if (isShowing())
                name.grabKeyboardFocus();
        }
        void refresh()
        {
            if (isVisible() && epoch != processor.getUiBridge().getDocumentEpoch())
            {
                dismiss();
                return;
            }
            const auto dateText = [] (const juce::String& iso)
            {
                return iso.isEmpty() ? juce::String ("Not saved yet") : juce::Time::fromISO8601 (iso).formatted ("%d %b %Y, %H:%M");
            };
            created.setText (dateText (processor.getProjectCreatedAt()), juce::dontSendNotification);
            modified.setText (dateText (processor.getProjectModifiedAt()), juce::dontSendNotification);
        }
        void dismiss()
        {
            setVisible (false);
            if (onClose)
                onClose();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key != juce::KeyPress::escapeKey)
                return false;
            dismiss();
            return true;
        }
        void resized() override
        {
            card = getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 24), juce::jmin (400, getHeight() - 24));
            const int x = card.getX() + 24, y = card.getY(), width = card.getWidth() - 48;
            const int nameWidth = (width - 16) * 3 / 5;
            name.setBounds (x, y + 80, nameWidth, 32);
            author.setBounds (x + nameWidth + 16, y + 80, width - nameWidth - 16, 32);
            description.setBounds (x, y + 148, width, card.getHeight() - 300);
            const int datesY = description.getBottom() + 36;
            created.setBounds (x, datesY, (width - 16) / 2, 22);
            modified.setBounds (x + (width + 16) / 2, datesY, (width - 16) / 2, 22);
            apply.setBounds (card.getRight() - 144, card.getBottom() - 52, 120, 32);
            cancel.setBounds (apply.getX() - 96, apply.getY(), 80, 32);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.58f));
            surface (g, card.toFloat());
            const float x = static_cast<float> (card.getX() + 24), y = static_cast<float> (card.getY());
            text (g, "Project details", { x, y + 20, 400, 26 }, 20, Palette::text, true, juce::Justification::centredLeft);
            const auto label = [&] (const juce::String& value, juce::Rectangle<int> bounds)
            {
                text (g, value, bounds.withY (bounds.getY() - 22).withHeight (18).toFloat(), 12, Palette::muted, true, juce::Justification::centredLeft);
            };
            label ("Name", name.getBounds());
            label ("Author", author.getBounds());
            label ("Description", description.getBounds());
            label ("Created", created.getBounds());
            label ("Last modified", modified.getBounds());
        }

    private:
        bool applyChanges()
        {
            if (epoch != processor.getUiBridge().getDocumentEpoch())
                return false;
            const auto nextName = name.getText().trim().isEmpty() ? juce::String ("Untitled Project") : name.getText().trim();
            const bool changed = nextName != processor.getProjectName()
                                 || author.getText().trim() != processor.getProjectAuthor()
                                 || description.getText().trim() != processor.getProjectDescription();
            if (changed)
                processor.setProjectMetadata (nextName, description.getText(), processor.getProjectCreatedAt(),
                                               juce::Time::getCurrentTime().toISO8601 (true), author.getText());
            return true;
        }
        PluginProcessor& processor;
        uint64_t epoch = 0;
        juce::Rectangle<int> card;
        juce::TextEditor name, author, description;
        juce::Label created, modified;
        juce::TextButton cancel { "Cancel" }, apply { "Apply" };
    };
}
