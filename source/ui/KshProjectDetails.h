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
            const int buttonHeight = getHeight() > 500 ? 64 : 44;
            discard.setBounds (card.getRight() - 208, card.getBottom() - buttonHeight - 16, 184, buttonHeight);
            cancel.setBounds (discard.getX() - 120, discard.getY(), 104, buttonHeight);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.58f));
            surface (g, card.toFloat());
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

    class ProjectDetails : public juce::Component
    {
    public:
        explicit ProjectDetails (PluginProcessor& p) : processor (p)
        {
            setTitle ("Current project details");
            for (auto* field : { &name, &author, &description })
            {
                addAndMakeVisible (*field);
                field->setFont (font (labelFontSize));
                field->setIndents (10, 8);
                field->getProperties().set ("projectMetadata", true);
                field->onEscapeKey = [this] { if (onClose) onClose(); };
            }
            name.setTitle ("Project name");
            author.setTitle ("Project author");
            description.setTitle ("Project description");
            for (auto* field : { &name, &author })
            {
                field->setIndents (10, 0);
                field->setJustification (juce::Justification::centredLeft);
            }
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
                date->setColour (juce::Label::textColourId, Palette::text);
                date->setBorderSize ({});
                date->setEditable (false, false, false);
            }
            created.setTitle ("Created date, read only");
            modified.setTitle ("Last modified date, read only");
            apply.getProperties().set ("insetProjectArtwork", true);
            addAndMakeVisible (apply);
            addChildComponent (restore);
            restore.getProperties().set ("insetProjectArtwork", true);
            restore.setTooltip ("Recover the last replaced project as an unsaved copy");
            restore.onClick = [this]
            {
                if (onRestore && matchesProject())
                    onRestore();
            };
           #if JUCE_IOS
            share.getProperties().set ("insetProjectArtwork", true);
            addAndMakeVisible (share);
            share.setTooltip ("Share a project copy or use Save to Files to put it in iCloud Drive");
            share.onClick = [this]
            {
                if (onShare && matchesProject())
                    onShare();
            };
           #endif
            apply.onClick = [this]
            {
                if (applyChanges() && onClose)
                    onClose();
            };
        }
        std::function<void()> onClose;
        std::function<void()> onRestore;
       #if JUCE_IOS
        std::function<void()> onShare;
       #endif
        void load()
        {
            epoch = processor.getUiBridge().getDocumentEpoch();
            name.setText (processor.getProjectName(), false);
            author.setText (processor.getProjectAuthor(), false);
            description.setText (processor.getProjectDescription(), false);
            refresh();
        }
        bool matchesProject() const { return epoch == processor.getUiBridge().getDocumentEpoch(); }
        void refresh()
        {
            const auto dateText = [] (const juce::String& iso)
            {
                return iso.isEmpty() ? juce::String ("Not saved yet") : juce::Time::fromISO8601 (iso).formatted ("%d %b %Y, %H:%M");
            };
            created.setText (dateText (processor.getProjectCreatedAt()), juce::dontSendNotification);
            modified.setText (dateText (processor.getProjectModifiedAt()), juce::dontSendNotification);
            restore.setVisible (processor.canRestorePreviousProject());
        }
        void resized() override
        {
            const bool compact = getHeight() < 250;
            auto area = getLocalBounds();
            headingBounds = area.removeFromTop (compact ? 28 : 44);
            auto actions = area.removeFromBottom (compact ? 38 : 46);
            apply.setBounds (actions.removeFromRight (84));
            restore.setButtonText (getWidth() < 360 ? "RESTORE" : "RESTORE PREVIOUS PROJECT");
            restore.setBounds (actions.removeFromLeft (getWidth() < 360 ? 94 : 204));
           #if JUCE_IOS
            share.setBounds (actions.removeFromRight (84));
           #endif
            area.removeFromBottom (compact ? 2 : 6);
            dateBounds = area.removeFromBottom (compact ? 36 : 44);
            auto dates = dateBounds.reduced (10, 2).withTrimmedTop (14);
            created.setBounds (dates.removeFromLeft (dates.getWidth() / 2));
            modified.setBounds (dates.withTrimmedLeft (4));
            for (auto* date : { &created, &modified })
                date->setFont (font (compact ? 12.0f : 14.0f));
            area.removeFromBottom (compact ? 5 : 10);
            area.removeFromTop (labelHeight);
            auto identity = area.removeFromTop (compact ? 28 : 40);
            name.setBounds (identity.removeFromLeft (identity.getWidth() * 3 / 5).withTrimmedRight (8));
            author.setBounds (identity);
            name.setFont (font (compact ? 15.0f : 18.0f));
            author.setFont (name.getFont());
            description.setIndents (10, compact ? 4 : 8);
            description.setBounds (area.withTrimmedTop (labelHeight + (compact ? 2 : 8)));
        }
        void paint (juce::Graphics& g) override
        {
            text (g, "Current project", headingBounds.toFloat(), 19, Palette::text, true, juce::Justification::centredLeft);
            controlSurface (g, dateBounds.toFloat());
            const auto label = [&g] (const juce::String& value, juce::Rectangle<int> bounds)
            {
                text (g, value, bounds.translated (0, -labelHeight).withHeight (labelHeight).toFloat(),
                      12, Palette::muted, true, juce::Justification::centredLeft);
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
            if (!matchesProject())
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
        static constexpr int labelHeight = 14;
        juce::Rectangle<int> headingBounds, dateBounds;
        juce::TextEditor name, author, description;
        juce::Label created, modified;
        juce::TextButton apply { "APPLY" };
        juce::TextButton restore { "RESTORE PREVIOUS PROJECT" };
       #if JUCE_IOS
        juce::TextButton share { "SHARE" };
       #endif
    };
}
