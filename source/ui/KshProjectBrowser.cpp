#include "KshProjectBrowser.h"

namespace ksh::ui
{
    namespace
    {
        constexpr auto favoritePreference = "favoriteProjectPaths";
        constexpr int previewPadding = 12, previewHeadingHeight = 16;
        constexpr int metadataLabelHeight = 24, metadataLabelGap = 6;
        constexpr float metadataLabelSize = labelFontSize + 2.0f;
        constexpr float identityTextSize = 24.0f, descriptionTextSize = 22.0f;
        constexpr int browserTarget = 42;
        constexpr float browserLabelSize = 15.0f;

        void paintBrowserField (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const auto field = bounds.reduced (1.0f);
            g.setColour (Palette::controls.app);
            g.fillRoundedRectangle (field, 4.0f);
            g.setColour (Palette::controls.border);
            g.drawRoundedRectangle (field, 4.0f, 1.0f);
        }

        void paintFavoriteHiHat (juce::Graphics& g, juce::Point<float> centre, float size, bool favorite, float opacity = 1.0f)
        {
            const juce::Graphics::ScopedSaveState saved (g);
            g.addTransform (juce::AffineTransform::scale (size / 24.0f).translated (centre.x - size * 0.5f, centre.y - size * 0.5f));
            g.setColour ((favorite ? juce::Colour (0xffefc34e) : Palette::muted).withMultipliedAlpha (opacity));

            constexpr float strokeWidth = 1.55f;
            g.drawEllipse (2.5f, 6.5f, 19.0f, 7.5f, strokeWidth);
            g.drawEllipse (9.0f, 7.8f, 6.0f, 3.2f, 1.2f);

            juce::Path rimAndRod;
            rimAndRod.startNewSubPath (2.7f, 11.0f);
            rimAndRod.cubicTo (2.7f, 17.3f, 21.3f, 17.3f, 21.3f, 11.0f);
            rimAndRod.startNewSubPath (12.0f, 2.0f);
            rimAndRod.lineTo (12.0f, 8.8f);
            rimAndRod.startNewSubPath (12.0f, 15.7f);
            rimAndRod.lineTo (12.0f, 22.0f);
            g.strokePath (rimAndRod, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        bool validComponentName (const juce::String& value)
        {
            return value.isNotEmpty() && !value.startsWithChar ('.')
                && !value.containsAnyOf ("/\\:\r\n") && juce::File::createLegalFileName (value) == value;
        }

        bool isWithin (const juce::File& file, const juce::File& root)
        {
            return root != juce::File() && (file == root || file.isAChildOf (root));
        }
    }

    void ProjectBrowser::drawGlyph (juce::Graphics& g, Glyph glyph, juce::Rectangle<float> icon, juce::Colour ink)
    {
        if (glyph == Glyph::none) return;
        if (glyph == Glyph::folder || glyph == Glyph::add)
        {
            phoneMenu::drawIcon (g, glyph == Glyph::folder ? phoneMenu::folder : phoneMenu::add, icon, ink);
            return;
        }
        if (glyph == Glyph::favorite)
        {
            paintFavoriteHiHat (g, icon.getCentre(), icon.getWidth(), false, ink.getFloatAlpha());
            return;
        }
        const juce::Graphics::ScopedSaveState saved (g);
        g.addTransform (juce::AffineTransform::scale (icon.getWidth() / 24.0f, icon.getHeight() / 24.0f)
                            .translated (icon.getX(), icon.getY()));
        g.setColour (ink);
        juce::Path path;
        switch (glyph)
        {
            case Glyph::back:
            case Glyph::forward:
            {
                const float direction = glyph == Glyph::back ? -1.0f : 1.0f;
                path.startNewSubPath (12.0f - direction * 3.0f, 6.0f);
                path.lineTo (12.0f + direction * 3.0f, 12.0f);
                path.lineTo (12.0f - direction * 3.0f, 18.0f);
                break;
            }
            case Glyph::cloud:
                path.startNewSubPath (6.0f, 18.0f);
                path.cubicTo (0.5f, 18.0f, 1.0f, 9.5f, 6.5f, 10.0f);
                path.cubicTo (7.2f, 3.5f, 16.5f, 3.2f, 17.8f, 10.0f);
                path.cubicTo (23.5f, 9.5f, 24.0f, 18.0f, 18.0f, 18.0f);
                path.closeSubPath();
                break;
            case Glyph::project:
                path.addRoundedRectangle (4.0f, 4.0f, 16.0f, 16.0f, 3.0f);
                for (float x : { 8.5f, 12.0f, 15.5f })
                {
                    g.fillEllipse (x - 0.9f, 13.3f, 1.8f, 1.8f);
                    path.startNewSubPath (x, 9.0f);
                    path.lineTo (x, 11.0f);
                }
                break;
            case Glyph::more:
                for (float x : { 5.0f, 12.0f, 19.0f }) g.fillEllipse (x - 1.3f, 10.7f, 2.6f, 2.6f);
                break;
            case Glyph::none:
            case Glyph::folder:
            case Glyph::favorite:
            case Glyph::add:
                break;
        }
        g.strokePath (path, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    ProjectBrowser::BrowserButton::BrowserButton (const juce::String& label, Glyph icon, bool isBreadcrumb)
        : TextButton (label), glyph (icon), breadcrumb (isBreadcrumb)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int ProjectBrowser::BrowserButton::preferredWidth (int maximum) const
    {
        const int labelWidth = juce::GlyphArrangement::getStringWidthInt (font (browserLabelSize, true), getButtonText());
        const int padding = breadcrumb ? 16 : 22;
        const int iconWidth = glyph == Glyph::none ? 0 : 23;
        return juce::jmin (maximum, juce::jmax (browserTarget, labelWidth + padding + iconWidth));
    }

    void ProjectBrowser::BrowserButton::paintButton (juce::Graphics& g, bool hovered, bool down)
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto face = bounds.reduced (2.0f, breadcrumb ? 5.0f : 4.0f);
        const bool active = getToggleState();
        const bool focused = hasKeyboardFocus (false);
        const bool highlighted = isEnabled() && (hovered || down || focused);
        if ((active && !breadcrumb) || highlighted)
        {
            g.setColour (active ? Palette::accent.withAlpha (highlighted ? 0.24f : 0.15f)
                                : Palette::text.withAlpha (down ? 0.13f : 0.07f));
            g.fillRoundedRectangle (face, 6.0f);
        }
        if (focused)
        {
            g.setColour (Palette::accent.withAlpha (0.7f));
            g.drawRoundedRectangle (face.reduced (0.5f), 6.0f, 1.0f);
        }
        const auto ink = active || highlighted ? Palette::text
                         : isEnabled() ? Palette::muted : Palette::muted.withAlpha (0.38f);
        if (getButtonText().isEmpty())
        {
            drawGlyph (g, glyph, bounds.withSizeKeepingCentre (20.0f, 20.0f), ink);
            return;
        }
        auto content = bounds.reduced (breadcrumb ? 8.0f : 11.0f, 0.0f);
        if (glyph != Glyph::none)
        {
            drawGlyph (g, glyph, content.removeFromLeft (20.0f).withSizeKeepingCentre (17.0f, 17.0f), ink);
            content.removeFromLeft (3.0f);
        }
        ui::text (g, getButtonText(), content, browserLabelSize, ink, true, juce::Justification::centredLeft);
    }

    ProjectBrowser::FavoriteButton::FavoriteButton() : Button ("Favorite project")
    {
        setTitle ("Favorite project");
        setToggleable (true);
    }

    void ProjectBrowser::FavoriteButton::paintButton (juce::Graphics& g, bool hovered, bool down)
    {
        const auto bounds = getLocalBounds().toFloat();
        if (isEnabled() && (hovered || down || hasKeyboardFocus (false)))
            controlSurface (g, bounds.reduced (4.0f), down, hovered || hasKeyboardFocus (false));
        paintFavoriteHiHat (g, bounds.getCentre(), 24.0f, getToggleState(), isEnabled() ? 1.0f : 0.45f);
    }

    ProjectBrowser::ProjectBrowser (juce::PropertiesFile& prefs, PluginProcessor& projectProcessor)
        : preferences (prefs), processor (projectProcessor)
    {
        setTitle ("Project browser");
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
        addAndMakeVisible (list);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTitle ("Projects and folders");
        addChildComponent (restore);
        restore.getProperties().set ("insetProjectArtwork", true);
        restore.setTooltip ("Recover the last replaced project as an unsaved copy");
        restore.onClick = [this]
        {
            if (busy || !onRestore) return;
            dismiss();
            onRestore();
        };
       #if JUCE_IOS
        addChildComponent (share);
        share.getProperties().set ("insetProjectArtwork", true);
        share.setTooltip ("Share a copy of this project");
        share.onClick = [this]
        {
            if (busy || !onShare) return;
            dismiss();
            onShare();
        };
       #endif
        for (auto* field : { &name, &author, &description, &folderName })
        {
            addChildComponent (*field);
            field->setIndents (10, 8);
            field->setFont (font (18));
            field->getProperties().set ("projectMetadata", true);
            field->setInputRestrictions (160);
            field->onEscapeKey = [this] { dismiss(); };
        }
        name.setTitle ("Project name");
        author.setTitle ("Project author");
        description.setTitle ("Project description");
        folderName.setTitle ("Folder name");
        for (auto* field : { &name, &author, &description, &folderName })
            field->setTextToShowWhenEmpty (field->getTitle(), Palette::faint);
        name.onTextChange = [this] { if (saving && isVisible()) updateActions(); };
        name.onReturnKey = [this] { primary.onClick(); };
        folderName.onReturnKey = [this] { createFolder(); };
        folderName.onEscapeKey = [this] { setFolderEditing (false); };
        description.setMultiLine (true);
        description.setReturnKeyStartsNewLine (true);
        description.setInputRestrictions (4000);
        description.setFont (font (descriptionTextSize));
        description.setIndents (0, 0);
        description.setBorder (juce::BorderSize<int> (16, 18, 16, 18));
        for (auto* field : { &name, &author })
        {
            field->setFont (font (identityTextSize));
            field->setIndents (14, 0);
            field->setJustification (juce::Justification::centredLeft);
        }
        addChildComponent (previewDescription);
        previewDescription.setTitle ("Project description");
        previewDescription.setReadOnly (true);
        previewDescription.setCaretVisible (false);
        previewDescription.setPopupMenuEnabled (false);
        previewDescription.setMultiLine (true);
        previewDescription.setScrollbarsShown (true);
        previewDescription.setBorder ({});
        previewDescription.setIndents (0, 0);
        previewDescription.setFont (font (descriptionTextSize));
        previewDescription.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        previewDescription.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        previewDescription.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (status);
        status.setFont (font (15));
        status.setColour (juce::Label::textColourId, Palette::text);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setBorderSize ({});
        status.setMinimumHorizontalScale (0.8f);
        addChildComponent (favorite);
        for (auto* button : { &cancel, &primary, &create })
        {
            button->getProperties().set ("insetProjectArtwork", true);
            addAndMakeVisible (*button);
        }
        for (auto* button : { &back, &forward, &home, &cloud, &import, &favorites, &newFolder })
            addAndMakeVisible (*button);
        for (auto* button : { &home, &cloud, &favorites })
            button->setToggleable (true);
        addChildComponent (breadcrumbOverflow);
        breadcrumbOverflow.setTitle ("Parent folders");
        breadcrumbOverflow.setTooltip ("Show parent folders");
        breadcrumbOverflow.onClick = [this] { showHiddenBreadcrumbs(); };
        primary.setToggleState (true, juce::dontSendNotification);

        back.setTitle ("Back");
        forward.setTitle ("Forward");
        back.setTooltip ("Previous location");
        forward.setTooltip ("Next location");
        back.onClick = [this] { goThroughHistory (-1); };
        forward.onClick = [this] { goThroughHistory (1); };
        home.onClick = [this] { navigate ({ libraryRoot, libraryRoot, Place::local }); };
        cloud.onClick = [this]
        {
            if (busy) return;
            if (!locations.chooseCloudFolder)
            {
                navigate ({ locations.cloudFolder, locations.cloudFolder, Place::cloud });
                return;
            }
            busy = true;
            updateActions();
            const SafePointer<ProjectBrowser> safe (this);
            locations.chooseCloudFolder ([safe] (juce::File selected)
            {
                if (safe == nullptr) return;
                safe->busy = false;
                if (selected != juce::File())
                    safe->navigate ({ selected, selected, Place::cloud });
                else
                    safe->updateActions();
            });
        };
        favorites.onClick = [this]
        {
            if (!showingFavorites())
                navigate ({ location.root, location.folder, Place::favorites });
        };
        newFolder.onClick = [this] { setFolderEditing (!addingFolder); };
        create.onClick = [this] { createFolder(); };
        favorite.onClick = [this] { toggleFavorite(); };
        cancel.onClick = [this] { dismiss(); };
        import.onClick = [this]
        {
            auto action = onImportExternal;
            dismiss();
            if (action) action();
        };
        primary.onClick = [this]
        {
            if (!saving)
            {
                activate (list.getSelectedRow());
                return;
            }
            const auto value = name.getText().trim();
            if (!validComponentName (value))
            {
                showStatus ("Enter a project name without slashes or other file path characters.");
                if (isShowing()) name.grabKeyboardFocus();
                return;
            }
            const auto target = saveDestination();
            if (target.isDirectory() || target.isSymbolicLink() || (target.exists() && target != currentFile))
            {
                showStatus ("That name is already in use in this folder. Choose another name.");
                return;
            }
            submit (target);
        };
    }

    ProjectBrowser::~ProjectBrowser() { list.setModel (nullptr); }

    void ProjectBrowser::open (juce::File root, juce::File current, std::optional<Details> details,
                               ListProjects files, Submit action, std::function<void()> importExternal, bool protectEdits, ReadDetails detailsReader,
                               juce::File lastFolder, StorageLocations storage)
    {
        locations = std::move (storage);
        libraryRoot = std::move (root);
        currentFile = std::move (current);
        auto start = currentFile != juce::File() ? currentFile.getParentDirectory() : std::move (lastFolder);
        if (start == juce::File())
            start = !locations.chooseCloudFolder && locations.cloudFolder != juce::File() ? locations.cloudFolder : libraryRoot;
        location = locationForFolder (start);
        listProjects = std::move (files);
        readDetails = std::move (detailsReader);
        onSubmit = std::move (action);
        onImportExternal = std::move (importExternal);
        saving = details.has_value();
        documentEpoch = processor.getUiBridge().getDocumentEpoch();
        saveFavorite.reset();
        const bool hasCloud = locations.chooseCloudFolder || locations.cloudFolder != juce::File();
        home.setButtonText (hasCloud ? "Local Projects" : "Projects");
        home.setTooltip (locations.projectsName.isNotEmpty() ? locations.projectsName : libraryRoot.getFullPathName());
        cloud.setVisible (hasCloud);
        cloud.setTooltip (locations.chooseCloudFolder ? "Choose a folder in iCloud Drive" : "Show the shared KSH folder in iCloud Drive");
        import.setTooltip ("Open a project from Files, including iCloud Drive");
        confirmOpen = !saving && protectEdits;
        pendingOpen = juce::File();
        if (details)
        {
            name.setText (details->name, false);
            author.setText (details->author, false);
            description.setText (details->description, false);
        }
        favoritePaths.clear();
        const auto savedFavorites = juce::JSON::parse (preferences.getValue (favoritePreference));
        if (const auto* paths = savedFavorites.getArray())
            for (const auto& path : *paths)
                if (path.isString()) favoritePaths.add (path.toString());

        busy = false;
        history = { { location, saving ? currentFile : juce::File() } };
        historyIndex = 0;
        setFolderEditing (false);
        showStatus ({});
        rebuildBreadcrumbs();
        refresh (saving ? currentFile : juce::File(), false);
        resized();
        setVisible (true);
        toFront (true);
        if (isShowing())
        {
           #if !JUCE_IOS
            if (saving)
            {
                name.grabKeyboardFocus();
                name.selectAll();
            }
            else
           #endif
                list.grabKeyboardFocus();
        }
    }

    void ProjectBrowser::dismiss()
    {
        if (busy || !isVisible()) return;
        setVisible (false);
        onSubmit = {};
        listProjects = {};
        readDetails = {};
        ++previewRequest;
        previewTimer.stopTimer();
        previewFile = juce::File();
        preview.reset();
        previewDescription.setVisible (false);
        onImportExternal = {};
        if (onDismiss) onDismiss (location.folder);
    }

    void ProjectBrowser::refreshCurrentProject()
    {
        if (isVisible() && !busy && documentEpoch != processor.getUiBridge().getDocumentEpoch())
            dismiss();
    }

    void ProjectBrowser::paint (juce::Graphics& g)
    {
        paintProjectPane (g, panelBounds().toFloat());
        g.setColour (Palette::text.withAlpha (0.035f));
        g.fillRoundedRectangle (locationGroupBounds.toFloat().reduced (0.5f, 4.0f), 7.0f);
        const auto paintSeparator = [&] (const BrowserButton& crumb)
        {
            if (crumb.isVisible() && crumb.getX() > breadcrumbBounds.getX())
                drawGlyph (g, Glyph::forward, juce::Rectangle<float> (12.0f, 12.0f)
                               .withCentre ({ static_cast<float> (crumb.getX() - 6), static_cast<float> (crumb.getBounds().getCentreY()) }),
                           Palette::muted.withAlpha (0.66f));
        };
        for (const auto& crumb : breadcrumbs) paintSeparator (*crumb);
        paintSeparator (breadcrumbOverflow);
        const auto listFrame = list.getBounds().expanded (1).toFloat();
        paintBrowserField (g, listFrame);
        if (list.hasKeyboardFocus (true))
        {
            g.setColour (Palette::accent.withAlpha (0.32f));
            g.drawRoundedRectangle (listFrame.reduced (1.0f), 4.0f, 1.0f);
        }
        if (entries.empty())
        {
            auto message = emptyBounds.toFloat().reduced (12.0f, 0.0f)
                               .withSizeKeepingCentre (juce::jmax (0.0f, emptyBounds.getWidth() - 24.0f), 64.0f);
            const auto glyph = message.removeFromTop (24.0f);
            drawGlyph (g, showingFavorites() ? Glyph::favorite : Glyph::folder,
                       glyph.withSizeKeepingCentre (22.0f, 22.0f), Palette::muted);
            text (g, showingFavorites() ? "No favorites yet" : "This folder is empty",
                  message.removeFromTop (23.0f), 16, Palette::text, true);
            if (showingFavorites())
                text (g, "Favorite a project to find it here.", message, 13, Palette::muted);
        }
        if (saving)
        {
            const auto label = [&g] (const char* value, const juce::TextEditor& field)
            {
                const auto bounds = field.getBounds();
                text (g, value, bounds.withY (bounds.getY() - metadataLabelHeight - metadataLabelGap).withHeight (metadataLabelHeight).toFloat(),
                      metadataLabelSize, Palette::muted, true, juce::Justification::centredLeft);
            };
            label ("Name", name);
            label ("Author", author);
            label ("Description", description);
        }
        else if (previewFile != juce::File())
        {
            paintBrowserField (g, previewIdentityBounds.toFloat());
            auto identity = previewIdentityBounds.reduced (previewPadding, 0).toFloat();
            if (!preview)
            {
                text (g, previewMessage, identity, 16, Palette::muted, false, juce::Justification::centredLeft);
                return;
            }
            const auto& project = *preview;
            identity = identity.withSizeKeepingCentre (identity.getWidth(), 48.0f);
            text (g, project.name, identity.removeFromTop (28), identityTextSize, Palette::text, true, juce::Justification::centredLeft);
            text (g, project.author.isNotEmpty() ? project.author : "No author", identity,
                  18, Palette::muted, false, juce::Justification::centredLeft);
            paintBrowserField (g, previewDescriptionBounds.toFloat());
            const auto descriptionHeading = previewDescription.getBounds().translated (0, -metadataLabelHeight - metadataLabelGap).withHeight (metadataLabelHeight);
            text (g, "Description", descriptionHeading.toFloat(), metadataLabelSize, Palette::muted, true, juce::Justification::centredLeft);

            auto dates = previewDatesBounds.toFloat();
            paintBrowserField (g, dates);
            const auto drawDate = [&g] (const char* heading, const juce::String& iso, juce::Rectangle<float> bounds)
            {
                bounds = bounds.reduced (previewPadding, 0.0f);
                bounds = bounds.withSizeKeepingCentre (bounds.getWidth(), 34.0f);
                text (g, heading, bounds.removeFromTop (previewHeadingHeight), 12, Palette::muted, true, juce::Justification::centredLeft);
                const auto value = iso.isNotEmpty() ? juce::Time::fromISO8601 (iso).formatted ("%d %b %Y, %H:%M") : juce::String::fromUTF8 (u8"—");
                text (g, value, bounds, 13, Palette::text, false, juce::Justification::centredLeft);
            };
            drawDate ("Created", project.createdAt, dates.removeFromLeft (dates.getWidth() * 0.5f).withTrimmedRight (4.0f));
            drawDate ("Last modified", project.modifiedAt, dates.withTrimmedLeft (4.0f));
        }
    }

    void ProjectBrowser::setRowArea (juce::Rectangle<int> area)
    {
        if (std::exchange (rowArea, area) != area)
        {
            resized();
            repaint();
        }
    }

    int ProjectBrowser::targetSize() const
    {
        return getWidth() < 900 || getHeight() <= 500 ? browserTarget : 64;
    }

    void ProjectBrowser::resized()
    {
        const int target = targetSize();
        const bool compact = target == browserTarget;
        const int gap = compact ? 8 : 16;
        const auto actionFont = font (projectActionLabelSize (target), true);
        const auto buttonWidth = [&] (const juce::String& label, int minimum)
        {
            return juce::jmax (minimum, juce::GlyphArrangement::getStringWidthInt (actionFont, label) + 2 * footerArtworkInset + 16);
        };
        auto content = panelBounds().reduced (compact ? 14 : 28, compact ? 10 : 22);
        auto nav = content.removeFromTop (target);
        back.setBounds (nav.removeFromLeft (target));
        forward.setBounds (nav.removeFromLeft (target));
        nav.removeFromLeft (gap);
        const int locationCount = 2 + static_cast<int> (cloud.isVisible()) + static_cast<int> (import.isVisible());
        const int maxLocationWidth = juce::jmax (target, nav.getWidth() / locationCount);
        const int groupLeft = nav.getX();
        home.setBounds (nav.removeFromLeft (juce::jmax (target, home.preferredWidth (maxLocationWidth))));
        if (cloud.isVisible()) cloud.setBounds (nav.removeFromLeft (juce::jmax (target, cloud.preferredWidth (maxLocationWidth))));
        if (import.isVisible()) import.setBounds (nav.removeFromLeft (juce::jmax (target, import.preferredWidth (maxLocationWidth))));
        favorites.setBounds (nav.removeFromLeft (juce::jmax (target, favorites.preferredWidth (maxLocationWidth))));
        locationGroupBounds = nav.withX (groupLeft).withWidth (nav.getX() - groupLeft);
        content.removeFromTop (gap);

        auto actions = content.removeFromBottom (target);
        primary.setBounds (actions.removeFromRight (saving ? 104 : buttonWidth ("DISCARD AND OPEN", 148)));
        cancel.setBounds (actions.removeFromRight (buttonWidth (cancel.getButtonText(), 94)));
        if (saving)
        {
            if (restore.isVisible()) restore.setBounds (actions.removeFromLeft (buttonWidth (restore.getButtonText(), 104)));
           #if JUCE_IOS
            if (share.isVisible()) share.setBounds (actions.removeFromLeft (buttonWidth (share.getButtonText(), 76)));
           #endif
        }
        const auto statusRow = content.removeFromBottom (compact ? 20 : 26);
        auto contents = content.removeFromLeft (content.getWidth() * (saving ? 50 : 54) / 100);
        content.removeFromLeft (gap);
        auto pathBar = contents.removeFromTop (target);
        newFolder.setBounds (pathBar.removeFromRight (newFolder.preferredWidth (144)));
        breadcrumbBounds = pathBar.withTrimmedRight (gap);
        layoutBreadcrumbs();
        contents.removeFromTop (compact ? 2 : 6);
        status.setBounds (statusRow);
        if (saving)
        {
            content.removeFromTop (metadataLabelHeight + metadataLabelGap);
            auto identity = content.removeFromTop (target);
            favorite.setBounds (identity.removeFromRight (target));
            identity.removeFromRight (gap);
            name.setBounds (identity.removeFromLeft (identity.getWidth() * 3 / 5).withTrimmedRight (gap));
            author.setBounds (identity);
            content.removeFromTop (gap + metadataLabelHeight + metadataLabelGap);
            description.setBounds (content);
        }
        else
        {
            previewBounds = content;
            layoutPreview();
        }
        if (addingFolder)
        {
            auto addRow = contents.removeFromTop (target);
            create.setBounds (addRow.removeFromRight (buttonWidth (create.getButtonText(), 86)));
            folderName.setBounds (addRow.withTrimmedRight (gap));
            contents.removeFromTop (gap / 2);
        }
        emptyBounds = contents;
        list.setBounds (contents.reduced (1));
        list.setRowHeight (juce::jmax (44, target));
        list.getViewport()->setScrollBarThickness (compact ? 16 : 24);
    }

    void ProjectBrowser::layoutPreview()
    {
        auto area = previewBounds;
        const bool compact = targetSize() == browserTarget;
        const int gap = compact ? 8 : 14;
        const int descriptionPadding = compact ? 12 : 16;
        const int dateHeight = compact ? 42 : 60;
        previewIdentityBounds = area.removeFromTop (compact ? 54 : 76);
        favorite.setBounds (previewIdentityBounds.removeFromRight (targetSize()).withSizeKeepingCentre (targetSize(), targetSize()));
        previewIdentityBounds.removeFromRight (gap);
        if (!preview)
            return;

        area.removeFromTop (gap);
        previewDatesBounds = area.removeFromBottom (dateHeight);
        area.removeFromBottom (gap);
        previewDescriptionBounds = area;
        previewDescription.setBounds (area.reduced (18, descriptionPadding).withTrimmedTop (metadataLabelHeight + metadataLabelGap));
    }

    void ProjectBrowser::mouseDown (const juce::MouseEvent& event)
    {
        if (!panelBounds().contains (event.getPosition())) dismiss();
    }

    void ProjectBrowser::focusOfChildComponentChanged (FocusChangeType)
    {
        repaint (list.getBounds().expanded (1));
    }

    bool ProjectBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (addingFolder) setFolderEditing (false);
            else dismiss();
            return true;
        }
        const auto modifiers = key.getModifiers();
        const auto code = key.getKeyCode();
        if ((modifiers.isCommandDown() && code == '[') || (modifiers.isAltDown() && code == juce::KeyPress::leftKey))
        {
            goThroughHistory (-1);
            return true;
        }
        if ((modifiers.isCommandDown() && code == ']') || (modifiers.isAltDown() && code == juce::KeyPress::rightKey))
        {
            goThroughHistory (1);
            return true;
        }
        return false;
    }

    ProjectBrowser::ProjectList::ProjectList (ProjectBrowser& browser)
        : juce::ListBox ("Projects", &browser), owner (browser)
    {
        addMouseListener (this, true);
        auto* viewport = getViewport();
        auto& scrollbar = viewport->getVerticalScrollBar();
        scrollbar.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff546166));
        scrollbar.setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    }

    bool ProjectBrowser::ProjectList::keyPressed (const juce::KeyPress& key)
    {
        if (owner.keyPressed (key)) return true;
        if (!key.getModifiers().isAnyModifierKeyDown())
        {
            if (key == juce::KeyPress::leftKey || key == juce::KeyPress::backspaceKey)
            {
                owner.goUp();
                return true;
            }
            if (key == juce::KeyPress::rightKey)
            {
                if (const auto* entry = owner.entryAt (getSelectedRow()); entry != nullptr && entry->folder)
                    owner.navigateToFolder (entry->path);
                return true;
            }
        }
        return juce::ListBox::keyPressed (key);
    }

    void ProjectBrowser::ProjectList::mouseEnter (const juce::MouseEvent& event) { mouseMove (event); }
    void ProjectBrowser::ProjectList::mouseExit (const juce::MouseEvent&) { setHoveredRow (-1); }

    void ProjectBrowser::ProjectList::mouseMove (const juce::MouseEvent& event)
    {
        if (!event.source.canHover()) return;
        const auto point = event.getEventRelativeTo (this).getPosition();
        const auto* viewport = getViewport();
        setHoveredRow (point.x < viewport->getMaximumVisibleWidth() ? getRowContainingPosition (point.x, point.y) : -1);
    }

    void ProjectBrowser::ProjectList::setHoveredRow (int row)
    {
        if (hover == row) return;
        const auto old = std::exchange (hover, row);
        repaintRow (old);
        repaintRow (row);
    }

    int ProjectBrowser::getNumRows() { return static_cast<int> (entries.size()); }

    juce::String ProjectBrowser::getNameForRow (int row)
    {
        if (const auto* entry = entryAt (row))
            return entry->label();
        return {};
    }

    juce::String ProjectBrowser::getTooltipForRow (int row)
    {
        if (const auto* entry = entryAt (row)) return entry->path.getFullPathName();
        return {};
    }

    const ProjectBrowser::Entry* ProjectBrowser::entryAt (int row) const
    {
        return juce::isPositiveAndBelow (row, static_cast<int> (entries.size())) ? &entries[static_cast<size_t> (row)] : nullptr;
    }

    void ProjectBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        const auto* entry = entryAt (row);
        if (entry == nullptr) return;
        auto bounds = juce::Rectangle<float> (static_cast<float> (width), static_cast<float> (height)).reduced (5.0f, 2.0f);
        const bool hovered = row == list.hoveredRow();
        if (selected || hovered)
        {
            g.setColour (selected ? Palette::accent.withAlpha (0.18f) : Palette::text.withAlpha (0.055f));
            g.fillRoundedRectangle (bounds, 6.0f);
            if (selected)
            {
                g.setColour (Palette::accent.withAlpha (0.38f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
            }
        }
        const auto ink = selected || hovered ? Palette::text : Palette::text.withMultipliedAlpha (0.9f);
        const auto icon = bounds.removeFromLeft (38.0f).withSizeKeepingCentre (21.0f, 21.0f);
        drawGlyph (g, entry->folder ? Glyph::folder : Glyph::project, icon,
                   entry->folder ? Palette::accent.interpolatedWith (Palette::text, 0.28f) : Palette::muted);
        bounds.removeFromRight (6.0f);
        if (entry->folder)
        {
            const auto next = bounds.removeFromRight (20.0f).withSizeKeepingCentre (18.0f, 18.0f);
            drawGlyph (g, Glyph::forward, next, Palette::muted);
        }
        else
        {
            if (favoritePaths.contains (entry->path.getFullPathName()))
                paintFavoriteHiHat (g, bounds.removeFromRight (24.0f).getCentre(), 16.0f, true);
        }
        if (showingFavorites())
        {
            bounds = bounds.withSizeKeepingCentre (bounds.getWidth(), 36.0f);
            const auto label = bounds.removeFromTop (20.0f);
            text (g, entry->label(), label, 16, ink, false, juce::Justification::centredLeft);
            text (g, entry->parentLabel, bounds, 12, Palette::muted, false, juce::Justification::centredLeft);
        }
        else
            text (g, entry->label(), bounds, 16, ink, entry->folder, juce::Justification::centredLeft);
    }

    void ProjectBrowser::selectedRowsChanged (int row)
    {
        const auto* entry = entryAt (row);
        if (pendingOpen != juce::File() && (entry == nullptr || entry->path != pendingOpen))
        {
            pendingOpen = juce::File();
            showStatus ({});
        }
        updatePreview (entry);
        updateActions();
    }

    void ProjectBrowser::updatePreview (const Entry* entry)
    {
        const auto file = !saving && entry != nullptr && !entry->folder ? entry->path : juce::File();
        if (file == previewFile && file != juce::File())
            return;
        previewFile = file;
        ++previewRequest;
        previewTimer.stopTimer();
        preview.reset();
        previewDescription.setVisible (false);
        previewMessage = "Project details are unavailable.";
        if (readDetails && file != juce::File())
        {
            previewMessage = juce::String::fromUTF8 (u8"Loading project details…");
            previewTimer.startTimer (120);
        }
        if (!saving)
            layoutPreview();
        repaint();
    }

    void ProjectBrowser::loadPreview()
    {
        previewTimer.stopTimer();
        const auto request = previewRequest;
        const SafePointer<ProjectBrowser> safe (this);
        readDetails (previewFile, [safe, request] (std::optional<Details> details)
        {
            if (safe == nullptr || request != safe->previewRequest)
                return;
            safe->preview = std::move (details);
            safe->previewMessage = "Project details are unavailable.";
            if (safe->preview)
            {
                const auto& text = safe->preview->description;
                safe->previewDescription.setColour (juce::TextEditor::textColourId, text.isNotEmpty() ? Palette::text : Palette::muted);
                safe->previewDescription.setText (text.isNotEmpty() ? text : "No description", false);
                safe->layoutPreview();
                safe->previewDescription.moveCaretToTop (false);
                safe->previewDescription.setVisible (true);
            }
            safe->repaint (safe->previewBounds);
        });
    }

    void ProjectBrowser::listBoxItemClicked (int row, const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
            showEntryMenu (row, event);
        else if (const auto* entry = entryAt (row); entry != nullptr && entry->folder && !event.source.isMouse())
            activate (row);
    }

    void ProjectBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& event)
    {
        if (event.source.isMouse() && !event.mods.isPopupMenu()) activate (row);
    }

    void ProjectBrowser::listWasScrolled() { list.setHoveredRow (-1); }
    void ProjectBrowser::returnKeyPressed (int row) { activate (row); }

    void ProjectBrowser::refresh (const juce::File& select, bool selectFirst)
    {
        entries.clear();
        if (showingFavorites())
        {
            const auto userHome = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            for (const auto& path : favoritePaths)
            {
                const juce::File file (path);
                const auto parent = file.getParentDirectory();
                const auto relativeTo = [&] (const juce::File& root, const juce::String& label)
                {
                    return parent == root ? label : label + " / " + parent.getRelativePathFrom (root).replace ("/", " / ");
                };
                const auto label = isWithin (parent, libraryRoot) ? relativeTo (libraryRoot, home.getButtonText())
                                 : isWithin (parent, locations.cloudFolder) ? relativeTo (locations.cloudFolder, cloud.getButtonText())
                                 : parent.isAChildOf (userHome) ? "~/" + parent.getRelativePathFrom (userHome) : parent.getFullPathName();
                entries.push_back ({ file, false, label });
            }
        }
        else
        {
            for (const auto& directory : location.folder.findChildFiles (juce::File::findDirectories | juce::File::ignoreHiddenFiles, false))
                if (!directory.isSymbolicLink()) entries.push_back ({ directory, true, {} });
            if (listProjects)
                for (const auto& file : listProjects (location.folder))
                    entries.push_back ({ file, false, {} });
        }
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
        {
            if (a.folder != b.folder) return a.folder;
            const auto compare = a.path.getFileName().compareNatural (b.path.getFileName(), true);
            return compare == 0 ? a.path.getFullPathName() < b.path.getFullPathName() : compare < 0;
        });
        list.setHoveredRow (-1);
        list.deselectAllRows();
        list.updateContent();
        const auto match = std::find_if (entries.begin(), entries.end(), [&] (const Entry& entry) { return entry.path == select; });
        if (match != entries.end())
            list.selectRow (static_cast<int> (std::distance (entries.begin(), match)));
        else
        {
            list.getViewport()->setViewPosition (0, 0);
            if (!entries.empty() && (selectFirst || entries.front().folder)) list.selectRow (0);
        }
        updatePreview (entryAt (list.getSelectedRow()));
        list.repaint();
        updateActions();
        repaint();
    }

    juce::File ProjectBrowser::selectedPath() const
    {
        if (const auto* entry = entryAt (list.getSelectedRow())) return entry->path;
        return {};
    }

    ProjectBrowser::Location ProjectBrowser::locationForFolder (const juce::File& folder) const
    {
        if (isWithin (folder, libraryRoot))
            return { libraryRoot, folder, Place::local };
        for (auto visit = history.rbegin(); visit != history.rend(); ++visit)
        {
            const auto& previous = visit->location;
            if ((previous.place == Place::cloud || previous.place == Place::other) && isWithin (folder, previous.root))
                return { previous.root, folder, previous.place };
        }
        if (isWithin (folder, locations.cloudFolder))
            return { locations.chooseCloudFolder ? folder : locations.cloudFolder, folder, Place::cloud };
       #if JUCE_MAC
        if (folder.isDirectory()) return { folder, folder, Place::other };
       #endif
        return { libraryRoot, libraryRoot, Place::local };
    }

    void ProjectBrowser::navigate (Location destination, const juce::File& select)
    {
        if (busy || destination.root == juce::File() || destination == location) return;
        history[historyIndex].selection = selectedPath();
        history.resize (historyIndex + 1);
        history.push_back ({ destination, select });
        ++historyIndex;
        changeLocation (std::move (destination), select);
    }

    void ProjectBrowser::navigateToFolder (const juce::File& destination)
    {
        if (!isWithin (destination, location.root) || showingFavorites()) return;
        auto selection = location.folder;
        while (selection != destination && selection.isAChildOf (destination) && selection.getParentDirectory() != destination)
            selection = selection.getParentDirectory();
        navigate ({ location.root, destination, location.place }, selection);
    }

    void ProjectBrowser::changeLocation (Location destination, const juce::File& select)
    {
        location = std::move (destination);
        setFolderEditing (false);
        showStatus ({});
        rebuildBreadcrumbs();
        refresh (select);
    }

    void ProjectBrowser::goThroughHistory (int direction)
    {
        if (busy || history.empty() || (direction < 0 ? historyIndex == 0 : historyIndex + 1 >= history.size())) return;
        history[historyIndex].selection = selectedPath();
        if (direction < 0) --historyIndex;
        else ++historyIndex;
        const auto& visit = history[historyIndex];
        changeLocation (visit.location, visit.selection);
    }

    void ProjectBrowser::rebuildBreadcrumbs()
    {
        breadcrumbs.clear();
        std::vector<juce::File> ancestors;
        if (!showingFavorites())
        {
            for (auto file = location.folder; isWithin (file, location.root); file = file.getParentDirectory())
            {
                ancestors.push_back (file);
                if (file == location.root) break;
            }
            std::reverse (ancestors.begin(), ancestors.end());
        }
        else
            ancestors.emplace_back();

        for (size_t index = 0; index < ancestors.size(); ++index)
        {
            const auto& file = ancestors[index];
            auto label = file.getFileName();
            if (index == 0)
            {
                if (location.place == Place::local) label = home.getButtonText();
                else if (showingFavorites()) label = favorites.getButtonText();
                else if (location.place == Place::cloud && !locations.chooseCloudFolder) label = cloud.getButtonText();
            }
            const bool current = index + 1 == ancestors.size();
            auto crumb = std::make_unique<BrowserButton> (label, Glyph::none, true);
            crumb->setTitle (current ? label + " (current location)" : label);
            crumb->setTooltip (file != juce::File() ? file.getFullPathName() : label);
            crumb->setToggleState (current, juce::dontSendNotification);
            crumb->setEnabled (!current && !busy);
            if (!current) crumb->onClick = [this, file] { navigateToFolder (file); };
            addChildComponent (*crumb);
            breadcrumbs.push_back (std::move (crumb));
        }
        layoutBreadcrumbs();
    }

    void ProjectBrowser::layoutBreadcrumbs()
    {
        constexpr int separator = 12;
        hiddenBreadcrumbs.clear();
        breadcrumbOverflow.setVisible (false);
        for (const auto& crumb : breadcrumbs) crumb->setVisible (false);
        if (breadcrumbs.empty() || breadcrumbBounds.isEmpty()) return;

        auto area = breadcrumbBounds;
        const int count = static_cast<int> (breadcrumbs.size());
        const int target = targetSize();
        const auto preferred = [&] (int index) { return juce::jmax (target, breadcrumbs[static_cast<size_t> (index)]->preferredWidth (index == count - 1 ? 220 : 160)); };
        const auto add = [&] (BrowserButton& crumb, int width)
        {
            if (area.getX() != breadcrumbBounds.getX()) area.removeFromLeft (separator);
            crumb.setBounds (area.removeFromLeft (width));
            crumb.setVisible (true);
        };
        int total = (count - 1) * separator;
        for (int i = 0; i < count; ++i) total += preferred (i);
        if (total <= area.getWidth() || count <= 2)
        {
            if (count == 2 && total > area.getWidth())
                add (*breadcrumbs.front(), juce::jmin (preferred (0), (area.getWidth() - separator) / 2));
            else
                for (int i = 0; i < count - 1; ++i) add (*breadcrumbs[static_cast<size_t> (i)], preferred (i));
            add (*breadcrumbs.back(), preferred (count - 1));
            return;
        }

        const bool showRoot = area.getWidth() >= 4 * target;
        int firstHidden = 0;
        if (showRoot)
        {
            add (*breadcrumbs.front(), juce::jmin (preferred (0), (area.getWidth() - target - 2 * separator) / 2));
            firstHidden = 1;
        }
        add (breadcrumbOverflow, target);
        int budget = area.getWidth() - separator - preferred (count - 1);
        int firstTail = count - 1;
        while (firstTail > firstHidden + 1 && preferred (firstTail - 1) + separator <= budget)
        {
            --firstTail;
            budget -= preferred (firstTail) + separator;
        }
        for (int i = firstHidden; i < firstTail; ++i) hiddenBreadcrumbs.add (i);
        for (int i = firstTail; i < count; ++i) add (*breadcrumbs[static_cast<size_t> (i)], preferred (i));
    }

    void ProjectBrowser::showHiddenBreadcrumbs()
    {
        if (busy || hiddenBreadcrumbs.isEmpty()) return;
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        for (const auto index : hiddenBreadcrumbs)
            menu.addItem (index + 1, breadcrumbs[static_cast<size_t> (index)]->getButtonText());
        const SafePointer<ProjectBrowser> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&breadcrumbOverflow).withParentComponent (this)
                                .withStandardItemHeight (targetSize()),
                            [safe, origin = location] (int result)
        {
            if (safe != nullptr && safe->isVisible() && !safe->busy && safe->location == origin
                && juce::isPositiveAndBelow (result - 1, static_cast<int> (safe->breadcrumbs.size())))
            {
                auto action = safe->breadcrumbs[static_cast<size_t> (result - 1)]->onClick;
                if (action) action();
            }
        });
    }

    void ProjectBrowser::showEntryMenu (int row, const juce::MouseEvent& event)
    {
        const auto* entry = entryAt (row);
        if (busy || entry == nullptr) return;
        list.selectRow (row);
        constexpr int openItem = 1, favoriteItem = 2, enclosingItem = 3;
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        if (entry->folder || !saving) menu.addItem (openItem, entry->folder ? "Open Folder" : "Open Project");
        if (!entry->folder && !saving)
            menu.addItem (favoriteItem, favoritePaths.contains (entry->path.getFullPathName()) ? "Remove from Favorites" : "Add to Favorites");
        if (showingFavorites()) menu.addItem (enclosingItem, "Go to Enclosing Folder");
        if (menu.getNumItems() == 0) return;
        const SafePointer<ProjectBrowser> safe (this);
        const auto anchor = juce::Rectangle<int> (1, 1).withPosition (event.getScreenPosition());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (anchor)
                                .withParentComponent (this).withStandardItemHeight (targetSize()),
                            [safe, origin = location, path = entry->path] (int result)
        {
            if (safe == nullptr || !safe->isVisible() || safe->busy || safe->location != origin || safe->selectedPath() != path) return;
            if (result == openItem) safe->activate (safe->list.getSelectedRow());
            else if (result == favoriteItem) safe->toggleFavorite();
            else if (result == enclosingItem) safe->navigate (safe->locationForFolder (path.getParentDirectory()), path);
        });
    }

    void ProjectBrowser::goUp()
    {
        if (!showingFavorites() && location.folder != location.root)
            navigateToFolder (location.folder.getParentDirectory());
    }

    void ProjectBrowser::activate (int row)
    {
        if (busy) return;
        if (const auto* entry = entryAt (row))
        {
            if (entry->folder) navigateToFolder (entry->path);
            else if (!saving)
            {
                if (confirmOpen && pendingOpen != entry->path)
                {
                    pendingOpen = entry->path;
                    showStatus ("Opening this project will discard your unsaved changes.");
                    updateActions();
                    return;
                }
                submit (entry->path);
            }
        }
    }

    void ProjectBrowser::updateActions()
    {
        const auto* entry = entryAt (list.getSelectedRow());
        const bool canFavorite = saving ? !showingFavorites() : entry != nullptr && !entry->folder;
        const bool isFavorite = canFavorite && (saving
            ? saveFavorite.value_or (favoritePaths.contains (saveDestination().getFullPathName()))
            : favoritePaths.contains (entry->path.getFullPathName()));
        primary.setButtonText (saving ? "SAVE" : pendingOpen == juce::File() ? "OPEN" : "DISCARD AND OPEN");
        favorite.setVisible (canFavorite);
        favorite.setToggleState (isFavorite, juce::dontSendNotification);
        favorite.setTooltip (saving ? (isFavorite ? "Remove from favorites on save" : "Add to favorites on save")
                                    : (isFavorite ? "Remove from favorites" : "Add to favorites"));
        favorite.setEnabled (!busy && canFavorite);
        back.setEnabled (!busy && historyIndex > 0);
        forward.setEnabled (!busy && historyIndex + 1 < history.size());
        home.setEnabled (!busy);
        cloud.setEnabled (!busy);
        favorites.setEnabled (!busy);
        home.setToggleState (location.place == Place::local, juce::dontSendNotification);
        cloud.setToggleState (location.place == Place::cloud, juce::dontSendNotification);
        favorites.setToggleState (showingFavorites(), juce::dontSendNotification);
        newFolder.setEnabled (!busy && !showingFavorites());
        list.setEnabled (!busy);
        breadcrumbOverflow.setEnabled (!busy);
        for (size_t i = 0; i + 1 < breadcrumbs.size(); ++i)
            breadcrumbs[i]->setEnabled (!busy);
        import.setVisible (!saving && static_cast<bool> (onImportExternal));
        import.setEnabled (!busy);
        restore.setVisible (saving && processor.canRestorePreviousProject());
        restore.setEnabled (!busy);
       #if JUCE_IOS
        share.setVisible (saving && static_cast<bool> (onShare));
        share.setEnabled (!busy);
       #endif
        primary.setEnabled (!busy && (saving ? !showingFavorites() : entry != nullptr));
        cancel.setEnabled (!busy);
        for (auto* field : { &name, &author, &description })
        {
            field->setVisible (saving);
            field->setEnabled (!busy);
        }
        create.setEnabled (!busy);
        folderName.setEnabled (!busy);
    }

    void ProjectBrowser::setFolderEditing (bool editing)
    {
        addingFolder = editing;
        folderName.setVisible (editing);
        create.setVisible (editing);
        folderName.setText ({}, false);
        resized();
        if (isShowing()) (editing ? static_cast<juce::Component&> (folderName) : list).grabKeyboardFocus();
    }

    void ProjectBrowser::createFolder()
    {
        if (busy || !addingFolder) return;
        const auto value = folderName.getText().trim();
        if (!validComponentName (value))
        {
            showStatus ("Enter a folder name without slashes or other file path characters.");
            return;
        }
        const auto child = location.folder.getChildFile (value);
        if (child.exists() || child.isSymbolicLink())
        {
            showStatus ("That name is already in use in this folder.");
            return;
        }
        if (const auto result = child.createDirectory(); result.failed())
        {
            showStatus (result.getErrorMessage());
            return;
        }
        navigateToFolder (child);
    }

    void ProjectBrowser::toggleFavorite()
    {
        if (busy) return;
        if (saving)
        {
            saveFavorite = !favorite.getToggleState();
            updateActions();
            return;
        }
        const auto* entry = entryAt (list.getSelectedRow());
        if (entry == nullptr || entry->folder) return;
        const auto path = entry->path;
        setFavorite (path, !favoritePaths.contains (path.getFullPathName()));
        if (showingFavorites())
        {
            refresh (path);
            if (isShowing()) list.grabKeyboardFocus();
        }
        else
        {
            updateActions();
            list.repaint();
        }
    }

    juce::File ProjectBrowser::saveDestination() const
    {
        const auto value = name.getText().trim();
        if (!validComponentName (value) || location.folder == juce::File())
            return {};
        return location.folder.getChildFile (value.endsWithIgnoreCase (".kshproject") ? value : value + ".kshproject");
    }

    void ProjectBrowser::setFavorite (const juce::File& path, bool enabled)
    {
        const auto value = path.getFullPathName();
        if (enabled)
            favoritePaths.addIfNotAlreadyThere (value);
        else
            favoritePaths.removeString (value);
        juce::Array<juce::var> values;
        for (const auto& favoritePath : favoritePaths) values.add (favoritePath);
        preferences.setValue (favoritePreference, juce::JSON::toString (juce::var (values), true));
        preferences.saveIfNeeded();
    }

    void ProjectBrowser::submit (juce::File destination)
    {
        if (busy || !onSubmit || (saving && showingFavorites())) return;
        busy = true;
        const Details details { destination.getFileNameWithoutExtension(), author.getText().trim(), description.getText().trim() };
       #if JUCE_IOS
        if (saving && isShowing()) grabKeyboardFocus();
       #endif
        showStatus (saving ? juce::String::fromUTF8 (u8"Saving…") : juce::String::fromUTF8 (u8"Opening…"));
        updateActions();
        const SafePointer<ProjectBrowser> safe (this);
        const auto action = onSubmit;
        action (destination, details, [safe, destination] (juce::String error)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (error.isEmpty())
            {
                if (safe->saving && safe->saveFavorite)
                    safe->setFavorite (destination, *safe->saveFavorite);
                safe->dismiss();
            }
            else
            {
                safe->showStatus (error);
                safe->updateActions();
               #if !JUCE_IOS
                if (safe->isShowing())
                    (safe->saving ? static_cast<juce::Component&> (safe->name) : safe->list).grabKeyboardFocus();
               #endif
            }
        });
    }

    void ProjectBrowser::showStatus (const juce::String& message)
    {
        status.setText (message, juce::dontSendNotification);
    }
}
