#include "KshProjectBrowser.h"

namespace ksh::ui
{
    namespace
    {
        constexpr auto favoritePreference = "favoriteProjectPaths";
        constexpr int previewPadding = 12, previewHeadingHeight = 16, favoriteTarget = 44;

        void paintBrowserField (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const auto field = bounds.reduced (1.0f);
            g.setColour (Palette::controls.app);
            g.fillRoundedRectangle (field, 4.0f);
            g.setColour (Palette::controls.border);
            g.drawRoundedRectangle (field, 4.0f, 1.0f);
        }

        void paintFavoriteStar (juce::Graphics& g, juce::Point<float> centre, float radius, bool favorite, float opacity = 1.0f)
        {
            juce::Path star;
            star.addStar (centre, 5, radius * 0.46f, radius);
            g.setColour ((favorite ? juce::Colour (0xffefc34e) : Palette::muted).withMultipliedAlpha (opacity));
            if (favorite)
                g.fillPath (star);
            else
                g.strokePath (star, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved));
        }

        bool validComponentName (const juce::String& value)
        {
            return value.isNotEmpty() && !value.startsWithChar ('.')
                && !value.containsAnyOf ("/\\:\r\n") && juce::File::createLegalFileName (value) == value;
        }
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
        paintFavoriteStar (g, bounds.getCentre(), 11.0f, getToggleState(), isEnabled() ? 1.0f : 0.45f);
    }

    ProjectBrowser::ProjectBrowser (juce::PropertiesFile& prefs, PluginProcessor& processor)
        : preferences (prefs), currentDetails (processor)
    {
        setTitle ("Project browser");
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
        addAndMakeVisible (list);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTitle ("Projects and folders");
        addChildComponent (currentDetails);
        currentDetails.onClose = [this] { dismiss(); };
        currentDetails.onRestore = [this]
        {
            if (busy || !onRestore) return;
            dismiss();
            onRestore();
        };
       #if JUCE_IOS
        currentDetails.onShare = [this]
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
        name.onReturnKey = [this] { primary.onClick(); };
        folderName.onReturnKey = [this] { createFolder(); };
        folderName.onEscapeKey = [this] { setFolderEditing (false); };
        description.setMultiLine (true);
        description.setReturnKeyStartsNewLine (true);
        description.setInputRestrictions (4000);
        for (auto* field : { &name, &author })
        {
            field->setFont (font (20));
            field->setIndents (10, 0);
            field->setJustification (juce::Justification::centredLeft);
        }
        addChildComponent (previewDescription);
        previewDescription.setTitle ("Project description");
        previewDescription.setReadOnly (true);
        previewDescription.setMultiLine (true);
        previewDescription.setScrollbarsShown (true);
        previewDescription.setBorder ({});
        previewDescription.setIndents (0, 2);
        previewDescription.setFont (font (16));
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
        for (auto* button : { &home, &up, &favorites, &newFolder, &cloud, &import, &reveal, &cancel, &primary, &create })
        {
            button->getProperties().set ("insetProjectArtwork", true);
            addAndMakeVisible (*button);
        }
        currentProject.getProperties().set ("insetProjectArtwork", true);
        addChildComponent (currentProject);
        currentProject.onClick = [this]
        {
            list.deselectAllRows();
            if (isShowing()) list.grabKeyboardFocus();
        };
        primary.setToggleState (true, juce::dontSendNotification);

        home.onClick = [this] { navigateToRoot (libraryRoot); };
        cloud.onClick = [this]
        {
            if (busy) return;
            if (!locations.chooseCloudFolder)
            {
                navigateToRoot (locations.cloudFolder);
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
                    safe->navigateToRoot (selected);
                else
                    safe->updateActions();
            });
        };
        up.onClick = [this] { goUp(); };
        favorites.onClick = [this]
        {
            favoritesOnly = !favoritesOnly;
            setFolderEditing (false);
            showStatus ({});
            refresh();
            if (isShowing()) list.grabKeyboardFocus();
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
        reveal.onClick = [this]
        {
            if (const auto* entry = entryAt (list.getSelectedRow()))
                entry->path.revealToUser();
            else
                folder.revealToUser();
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
            const auto filename = value.endsWithIgnoreCase (".kshproject") ? value : value + ".kshproject";
            const auto target = folder.getChildFile (filename);
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
        folderRoot = libraryRoot;
        auto start = currentFile != juce::File() ? currentFile.getParentDirectory() : std::move (lastFolder);
        if (start == juce::File())
            start = !locations.chooseCloudFolder && locations.cloudFolder != juce::File() ? locations.cloudFolder : libraryRoot;
        if (!locations.chooseCloudFolder && locations.cloudFolder != juce::File()
            && (start == locations.cloudFolder || start.isAChildOf (locations.cloudFolder)))
            folderRoot = locations.cloudFolder;
        else if (start != libraryRoot && !start.isAChildOf (libraryRoot))
        {
           #if JUCE_MAC
            if (start.isDirectory()) folderRoot = start;
            else
           #endif
                start = libraryRoot;
        }
        listProjects = std::move (files);
        readDetails = std::move (detailsReader);
        onSubmit = std::move (action);
        onImportExternal = std::move (importExternal);
        saving = details.has_value();
        home.setButtonText (locations.cloudFolder != juce::File() && !locations.chooseCloudFolder ? "LOCAL PROJECTS" : "PROJECTS");
        cloud.setVisible (locations.chooseCloudFolder ? saving : locations.cloudFolder != juce::File());
        cloud.setTooltip (locations.chooseCloudFolder ? "Choose a folder in iCloud Drive" : "Show the shared KSH folder in iCloud Drive");
       #if JUCE_IOS
        import.setButtonText ("FILES / ICLOUD");
       #endif
        import.setTooltip ("Open a project from Files, including iCloud Drive");
        confirmOpen = !saving && protectEdits;
        pendingOpen = juce::File();
        if (details)
        {
            name.setText (details->name, false);
            author.setText (details->author, false);
            description.setText (details->description, false);
        }
        else
            currentDetails.load();
        favoritePaths.clear();
        const auto savedFavorites = juce::JSON::parse (preferences.getValue (favoritePreference));
        if (const auto* paths = savedFavorites.getArray())
            for (const auto& path : *paths)
                if (path.isString()) favoritePaths.add (path.toString());

        folder = std::move (start);
        favoritesOnly = busy = false;
        setFolderEditing (false);
        showStatus ({});
        refresh (currentFile, saving);
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
        currentDetails.setVisible (false);
        onImportExternal = {};
        if (onDismiss) onDismiss (folder);
    }

    void ProjectBrowser::refreshCurrentProject()
    {
        if (!isVisible() || saving || busy)
            return;
        if (currentDetails.matchesProject())
            currentDetails.refresh();
        else
            dismiss();
    }

    void ProjectBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        surface (g, card.toFloat());
        text (g, saving ? "Save Project" : "Projects", card.withTrimmedTop (12).withTrimmedLeft (16).withHeight (27).toFloat(),
              23, Palette::text, true, juce::Justification::centredLeft);
        auto shownPath = folder.getFileName();
        if (folder == libraryRoot || folder.isAChildOf (libraryRoot))
        {
            shownPath = locations.projectsName.isEmpty() ? juce::String ("Projects") : locations.projectsName;
            if (folder != libraryRoot)
                shownPath += " / " + folder.getRelativePathFrom (libraryRoot).replace ("/", " / ");
        }
        else if (locations.cloudFolder != juce::File() && (folder == locations.cloudFolder || folder.isAChildOf (locations.cloudFolder)))
        {
            shownPath = "iCloud Drive";
           #if JUCE_IOS
            shownPath += " / KSH";
           #endif
            if (folder != locations.cloudFolder)
                shownPath += " / " + folder.getRelativePathFrom (locations.cloudFolder).replace ("/", " / ");
        }
        text (g, favoritesOnly ? "Favorites" : shownPath, locationBounds.toFloat(), 16, Palette::muted, true, juce::Justification::centredLeft);
        paintBrowserField (g, list.getBounds().toFloat());
        if (entries.empty())
            text (g, favoritesOnly ? "No favorites yet" : "This folder is empty", emptyBounds.toFloat(), 17, Palette::muted);
        if (saving)
        {
            const auto label = [&g] (const char* value, const juce::TextEditor& field)
            {
                const auto bounds = field.getBounds();
                text (g, value, bounds.withY (bounds.getY() - 18).withHeight (18).toFloat(),
                      14, Palette::muted, true, juce::Justification::centredLeft);
            };
            label ("Name", name);
            label ("Author", author);
            label ("Description", description);
        }
        else if (!currentDetails.isVisible())
        {
            paintBrowserField (g, previewIdentityBounds.toFloat());
            auto identity = previewIdentityBounds.reduced (previewPadding, 0).toFloat();
            if (favorite.isVisible())
                identity.setRight (static_cast<float> (favorite.getX()));
            if (!preview)
            {
                text (g, previewMessage, identity, 16, Palette::muted, false, juce::Justification::centredLeft);
                return;
            }
            const auto& project = *preview;
            identity = identity.withSizeKeepingCentre (identity.getWidth(), 48.0f);
            text (g, project.name, identity.removeFromTop (28), 22, Palette::text, true, juce::Justification::centredLeft);
            text (g, project.author.isNotEmpty() ? project.author : "No author", identity,
                  16, Palette::muted, false, juce::Justification::centredLeft);
            paintBrowserField (g, previewDescriptionBounds.toFloat());
            const auto descriptionHeading = previewDescription.getBounds().translated (0, -previewHeadingHeight).withHeight (previewHeadingHeight);
            text (g, "Description", descriptionHeading.toFloat(), 12, Palette::muted, true, juce::Justification::centredLeft);

            auto dates = previewDatesBounds.toFloat();
            paintBrowserField (g, dates);
            const auto drawDate = [&g] (const char* heading, const juce::String& iso, juce::Rectangle<float> bounds)
            {
                bounds = bounds.reduced (previewPadding, 0.0f);
                bounds = bounds.withSizeKeepingCentre (bounds.getWidth(), 34.0f);
                text (g, heading, bounds.removeFromTop (previewHeadingHeight), 12, Palette::muted, true, juce::Justification::centredLeft);
                const auto value = iso.isNotEmpty() ? juce::Time::fromISO8601 (iso).formatted ("%d %b %Y, %H:%M") : juce::String ("—");
                text (g, value, bounds, 13, Palette::text, false, juce::Justification::centredLeft);
            };
            drawDate ("Created", project.createdAt, dates.removeFromLeft (dates.getWidth() * 0.5f).withTrimmedRight (4.0f));
            drawDate ("Last modified", project.modifiedAt, dates.withTrimmedLeft (4.0f));
        }
    }

    void ProjectBrowser::resized()
    {
        const bool compact = getHeight() < 500 || getWidth() < 900;
        const int target = compact ? 46 : 64;
        const int gap = 8;
        const auto actionFont = font (projectActionLabelSize (target), true);
        const auto buttonWidth = [&] (const juce::String& label, int minimum)
        {
            return juce::jmax (minimum, juce::GlyphArrangement::getStringWidthInt (actionFont, label) + 2 * footerArtworkInset + 16);
        };
        card = getLocalBounds().reduced (12).withSizeKeepingCentre (juce::jmin (960, getWidth() - 24), juce::jmin (630, getHeight() - 24));
        currentProject.setBounds (card.getRight() - 184, card.getY() + 6, 168, 36);
        auto content = card.reduced (16).withTrimmedTop (29);
        auto nav = content.removeFromTop (target);
        home.setBounds (nav.removeFromLeft (buttonWidth (home.getButtonText(), 104)));
        if (cloud.isVisible()) cloud.setBounds (nav.removeFromLeft (buttonWidth (cloud.getButtonText(), 132)));
        up.setBounds (nav.removeFromLeft (buttonWidth (up.getButtonText(), 58)));
        favorites.setBounds (nav.removeFromLeft (buttonWidth (favorites.getButtonText(), 120)));
        newFolder.setBounds (nav.removeFromRight (buttonWidth (newFolder.getButtonText(), 128)));
        content.removeFromTop (gap / 2);
        const int detailTop = content.getY();
        locationBounds = content.removeFromTop (25);
        auto actions = content.removeFromBottom (target);
        primary.setBounds (actions.removeFromRight (saving ? 104 : buttonWidth ("DISCARD AND OPEN", 148)));
        cancel.setBounds (actions.removeFromRight (buttonWidth (cancel.getButtonText(), 94)));
        if (reveal.isVisible()) reveal.setBounds (actions.removeFromLeft (buttonWidth (reveal.getButtonText(), 136)));
        if (import.isVisible()) import.setBounds (actions.removeFromLeft (buttonWidth (import.getButtonText(), 112)));
        content.removeFromBottom (gap);
        const int detailBottom = content.getBottom();
        status.setBounds (content.removeFromBottom (24));
        auto contents = content.removeFromLeft (content.getWidth() * (saving ? 50 : 54) / 100);
        content.removeFromLeft (14);
        if (saving)
        {
            content.removeFromTop (18);
            auto identity = content.removeFromTop (compact ? 38 : 44);
            name.setBounds (identity.removeFromLeft (identity.getWidth() * 3 / 5).withTrimmedRight (gap));
            author.setBounds (identity);
            content.removeFromTop (gap + 18);
            description.setBounds (content);
        }
        else
        {
            locationBounds.setWidth (contents.getWidth());
            status.setBounds (status.getBounds().withWidth (contents.getWidth()));
            previewBounds = content;
            currentDetails.setBounds (content.withTop (detailTop).withBottom (detailBottom));
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
        list.setRowHeight (compact ? 46 : 58);
    }

    void ProjectBrowser::layoutPreview()
    {
        if (currentDetails.isVisible())
        {
            favorite.setBounds (currentDetails.getBounds().removeFromTop (favoriteTarget).removeFromRight (favoriteTarget));
            return;
        }
        auto area = previewBounds;
        const bool compact = area.getHeight() < 190;
        const int gap = compact ? 6 : 10;
        const int descriptionPadding = compact ? 5 : 9;
        const int dateHeight = compact ? 42 : 54;
        previewIdentityBounds = area.removeFromTop (compact ? 54 : 70);
        auto favoriteSlot = previewIdentityBounds.withTrimmedRight (4);
        favorite.setBounds (favoriteSlot.removeFromRight (favoriteTarget).withSizeKeepingCentre (favoriteTarget, favoriteTarget));
        if (!preview)
            return;

        area.removeFromTop (gap);
        const auto textRoom = area.withTrimmedBottom (dateHeight + gap).reduced (previewPadding, descriptionPadding)
                                  .withTrimmedTop (previewHeadingHeight);
        const int minimumTextHeight = juce::jmin (22, textRoom.getHeight());
        previewDescription.setBounds (textRoom.withHeight (minimumTextHeight));
        const int descriptionHeight = juce::jlimit (minimumTextHeight, textRoom.getHeight(), previewDescription.getTextHeight());
        previewDescription.setBounds (textRoom.withHeight (descriptionHeight));
        previewDescriptionBounds = area.removeFromTop (descriptionHeight + previewHeadingHeight + 2 * descriptionPadding);
        area.removeFromTop (gap);
        previewDatesBounds = area.removeFromTop (dateHeight);
    }

    void ProjectBrowser::mouseDown (const juce::MouseEvent& event)
    {
        if (!card.contains (event.getPosition())) dismiss();
    }

    bool ProjectBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (addingFolder) setFolderEditing (false);
            else dismiss();
            return true;
        }
        return false;
    }

    bool ProjectBrowser::ProjectList::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::backspaceKey)
        {
            owner.goUp();
            return true;
        }
        if (key == juce::KeyPress::rightKey)
        {
            if (const auto* entry = owner.entryAt (getSelectedRow()); entry != nullptr && entry->folder)
                owner.navigate (entry->path);
            return true;
        }
        return juce::ListBox::keyPressed (key);
    }

    int ProjectBrowser::getNumRows() { return static_cast<int> (entries.size()); }

    juce::String ProjectBrowser::getNameForRow (int row)
    {
        if (const auto* entry = entryAt (row))
            return entry->folder ? entry->path.getFileName() : entry->path.getFileNameWithoutExtension();
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
        auto bounds = juce::Rectangle<int> (width, height).reduced (4, 2);
        if (selected)
        {
            g.setColour (Palette::accent.withAlpha (0.23f));
            g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        }
        const auto ink = selected ? Palette::text : Palette::text.withMultipliedAlpha (0.86f);
        const auto glyph = bounds.removeFromLeft (36).toFloat();
        if (entry->folder)
        {
            const auto icon = glyph.withSizeKeepingCentre (17.0f, 12.0f);
            g.setColour (Palette::accent.withMultipliedAlpha (0.8f));
            g.fillRoundedRectangle (icon.withWidth (8.0f).translated (0.0f, -3.0f), 2.0f);
            g.fillRoundedRectangle (icon, 2.0f);
        }
        else if (favoritePaths.contains (entry->path.getFullPathName()))
            paintFavoriteStar (g, glyph.getCentre(), 7.0f, true);
        text (g, getNameForRow (row), bounds.toFloat(), 18, ink, entry->folder, juce::Justification::centredLeft);
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
        const auto file = !saving && entry != nullptr && !entry->folder && entry->path != currentFile ? entry->path : juce::File();
        if (file == previewFile && file != juce::File())
            return;
        previewFile = file;
        ++previewRequest;
        previewTimer.stopTimer();
        preview.reset();
        previewDescription.setVisible (false);
        currentDetails.setVisible (!saving && file == juce::File());
        currentProject.setVisible (!saving && file != juce::File());
        previewMessage = "Select a project to see its details.";
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
        if (const auto* entry = entryAt (row); entry != nullptr && entry->folder && !event.source.isMouse())
            activate (row);
    }

    void ProjectBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& event)
    {
        if (event.source.isMouse()) activate (row);
    }

    void ProjectBrowser::returnKeyPressed (int row) { activate (row); }

    void ProjectBrowser::refresh (const juce::File& select, bool selectFirst)
    {
        entries.clear();
        if (favoritesOnly)
        {
            for (const auto& path : favoritePaths)
                entries.push_back ({ juce::File (path), false });
        }
        else
        {
            for (const auto& directory : folder.findChildFiles (juce::File::findDirectories | juce::File::ignoreHiddenFiles, false))
                if (!directory.isSymbolicLink()) entries.push_back ({ directory, true });
            if (listProjects)
                for (const auto& file : listProjects (folder))
                    entries.push_back ({ file, false });
        }
        std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
        {
            if (a.folder != b.folder) return a.folder;
            const auto compare = a.path.getFileName().compareNatural (b.path.getFileName(), true);
            return compare == 0 ? a.path.getFullPathName() < b.path.getFullPathName() : compare < 0;
        });
        list.deselectAllRows();
        list.updateContent();
        const auto match = std::find_if (entries.begin(), entries.end(), [&] (const Entry& entry) { return entry.path == select; });
        if (match != entries.end()) list.selectRow (static_cast<int> (std::distance (entries.begin(), match)));
        else if (!entries.empty() && (selectFirst || entries.front().folder)) list.selectRow (0);
        updatePreview (entryAt (list.getSelectedRow()));
        list.repaint();
        updateActions();
        repaint();
    }

    void ProjectBrowser::navigate (juce::File destination)
    {
        if (busy) return;
        auto previous = folder;
        folder = std::move (destination);
        favoritesOnly = false;
        setFolderEditing (false);
        showStatus ({});
        refresh (previous);
        if (isShowing()) list.grabKeyboardFocus();
    }

    void ProjectBrowser::navigateToRoot (const juce::File& destination)
    {
        if (busy || destination == juce::File()) return;
        folderRoot = destination;
        navigate (destination);
    }

    void ProjectBrowser::goUp()
    {
        if (!favoritesOnly && folder != folderRoot && folder.isAChildOf (folderRoot))
            navigate (folder.getParentDirectory());
    }

    void ProjectBrowser::activate (int row)
    {
        if (busy) return;
        if (const auto* entry = entryAt (row))
        {
            if (entry->folder) navigate (entry->path);
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
        const bool canFavorite = !saving && entry != nullptr && !entry->folder;
        const bool isFavorite = canFavorite && favoritePaths.contains (entry->path.getFullPathName());
        primary.setButtonText (saving ? "SAVE" : pendingOpen == juce::File() ? "OPEN" : "DISCARD AND OPEN");
        favorite.setVisible (canFavorite);
        favorite.setToggleState (isFavorite, juce::dontSendNotification);
        favorite.setTooltip (isFavorite ? "Remove from favorites" : "Add to favorites");
        favorite.setEnabled (!busy && canFavorite);
        home.setEnabled (!busy && (favoritesOnly || folder != libraryRoot));
        cloud.setEnabled (!busy && (locations.chooseCloudFolder || favoritesOnly || folder != locations.cloudFolder));
        up.setEnabled (!busy && !favoritesOnly && folder != folderRoot && folder.isAChildOf (folderRoot));
        favorites.setEnabled (!busy);
        favorites.setToggleState (favoritesOnly, juce::dontSendNotification);
        newFolder.setEnabled (!busy && !favoritesOnly);
        import.setVisible (!saving && static_cast<bool> (onImportExternal));
        import.setEnabled (!busy);
       #if JUCE_MAC
        reveal.setVisible (!saving || !favoritesOnly);
       #else
        reveal.setVisible (false);
       #endif
        reveal.setEnabled (!busy && (!favoritesOnly || entry != nullptr));
        primary.setEnabled (!busy && (saving ? !favoritesOnly : entry != nullptr));
        cancel.setEnabled (!busy);
        currentProject.setEnabled (!busy);
        currentDetails.setEnabled (!busy);
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
        const auto child = folder.getChildFile (value);
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
        navigate (child);
    }

    void ProjectBrowser::toggleFavorite()
    {
        const auto* entry = entryAt (list.getSelectedRow());
        if (busy || saving || entry == nullptr || entry->folder) return;
        const auto path = entry->path;
        const auto index = favoritePaths.indexOf (path.getFullPathName());
        if (index >= 0) favoritePaths.remove (index);
        else favoritePaths.add (path.getFullPathName());
        juce::Array<juce::var> values;
        for (const auto& value : favoritePaths) values.add (value);
        preferences.setValue (favoritePreference, juce::JSON::toString (juce::var (values), true));
        preferences.saveIfNeeded();
        if (favoritesOnly)
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

    void ProjectBrowser::submit (juce::File destination)
    {
        if (busy || !onSubmit || (saving && favoritesOnly)) return;
        busy = true;
        const Details details { destination.getFileNameWithoutExtension(), author.getText().trim(), description.getText().trim() };
       #if JUCE_IOS
        if (saving && isShowing()) grabKeyboardFocus();
       #endif
        showStatus (saving ? juce::String::fromUTF8 (u8"Saving…") : juce::String::fromUTF8 (u8"Opening…"));
        updateActions();
        const SafePointer<ProjectBrowser> safe (this);
        const auto action = onSubmit;
        action (destination, details, [safe] (juce::String error)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (error.isEmpty()) safe->dismiss();
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
