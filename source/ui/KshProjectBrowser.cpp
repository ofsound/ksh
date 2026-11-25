#include "KshProjectBrowser.h"

namespace ksh::ui
{
    namespace
    {
        constexpr auto favoritePreference = "favoriteProjectPaths";

        bool validComponentName (const juce::String& value)
        {
            return value.isNotEmpty() && !value.startsWithChar ('.')
                && !value.containsAnyOf ("/\\:\r\n") && juce::File::createLegalFileName (value) == value;
        }
    }

    ProjectBrowser::ProjectBrowser (juce::PropertiesFile& prefs) : preferences (prefs)
    {
        setTitle ("Project browser");
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
        addAndMakeVisible (list);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTitle ("Projects and folders");
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
        for (auto* button : { &home, &up, &favorites, &newFolder, &favorite, &import, &reveal, &cancel, &primary, &create })
            addAndMakeVisible (*button);

        home.onClick = [this] { folderRoot = libraryRoot; navigate (libraryRoot); };
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
                               ListProjects files, Submit action, std::function<void()> importExternal, bool protectEdits, ReadDetails detailsReader)
    {
        libraryRoot = std::move (root);
        currentFile = std::move (current);
        folderRoot = libraryRoot;
        auto start = currentFile.getParentDirectory();
        if (start != libraryRoot && !start.isAChildOf (libraryRoot))
        {
           #if JUCE_MAC
            if (currentFile.existsAsFile() && start.isDirectory()) folderRoot = start;
            else
           #endif
                start = libraryRoot;
        }
        listProjects = std::move (files);
        readDetails = std::move (detailsReader);
        onSubmit = std::move (action);
        onImportExternal = std::move (importExternal);
        saving = details.has_value();
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

        folder = std::move (start);
        favoritesOnly = busy = false;
        setFolderEditing (false);
        showStatus ({});
        refresh (currentFile);
        resized();
        setVisible (true);
        toFront (true);
        if (isShowing())
        {
            if (saving)
            {
                name.grabKeyboardFocus();
                name.selectAll();
            }
            else
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
        updatePreview (nullptr);
        onImportExternal = {};
        if (onDismiss) onDismiss();
    }

    void ProjectBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        surface (g, card.toFloat());
        text (g, saving ? "Save Project" : "Open Project", card.withTrimmedTop (12).withTrimmedLeft (16).withHeight (27).toFloat(),
              23, Palette::text, true, juce::Justification::centredLeft);
        const auto shownPath = folder == libraryRoot ? juce::String ("Projects")
            : folder.isAChildOf (libraryRoot) ? "Projects / " + folder.getRelativePathFrom (libraryRoot).replace ("/", " / ")
            : folder.getFileName();
        text (g, favoritesOnly ? "Favorites" : shownPath, locationBounds.toFloat(), 16, Palette::muted, true, juce::Justification::centredLeft);
        controlSurface (g, list.getBounds().toFloat(), false, false);
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
        else
        {
            controlSurface (g, previewBounds.toFloat(), false, false);
            auto detail = previewBounds.reduced (12).toFloat();
            if (!preview)
            {
                text (g, previewMessage, detail.removeFromTop (27), 16, Palette::muted, false, juce::Justification::centredLeft);
                return;
            }
            const auto& project = *preview;
            text (g, project.name, detail.removeFromTop (30), 22, Palette::text, true, juce::Justification::centredLeft);
            text (g, project.author.isNotEmpty() ? project.author : "No author", detail.removeFromTop (23),
                  16, Palette::muted, false, juce::Justification::centredLeft);
            auto dates = detail.removeFromBottom (36);
            const auto drawDate = [&g] (const char* heading, const juce::String& iso, juce::Rectangle<float> bounds)
            {
                text (g, heading, bounds.removeFromTop (16), 12, Palette::muted, true, juce::Justification::centredLeft);
                const auto value = iso.isNotEmpty() ? juce::Time::fromISO8601 (iso).formatted ("%d %b %Y, %H:%M") : juce::String ("—");
                text (g, value, bounds, 13, Palette::text, false, juce::Justification::centredLeft);
            };
            drawDate ("Created", project.createdAt, dates.removeFromLeft (dates.getWidth() * 0.5f));
            drawDate ("Last modified", project.modifiedAt, dates.withTrimmedLeft (6));
        }
    }

    void ProjectBrowser::resized()
    {
        const bool compact = getHeight() < 500;
        const int target = compact ? 46 : 58;
        const int gap = 8;
        card = getLocalBounds().reduced (12).withSizeKeepingCentre (juce::jmin (960, getWidth() - 24), juce::jmin (630, getHeight() - 24));
        auto content = card.reduced (16).withTrimmedTop (29);
        auto nav = content.removeFromTop (target);
        home.setBounds (nav.removeFromLeft (90));
        up.setBounds (nav.removeFromLeft (54).withTrimmedLeft (gap));
        favorites.setBounds (nav.removeFromLeft (108).withTrimmedLeft (gap));
        newFolder.setBounds (nav.removeFromRight (114));
        content.removeFromTop (gap / 2);
        locationBounds = content.removeFromTop (25);
        auto actions = content.removeFromBottom (target);
        primary.setBounds (actions.removeFromRight (saving ? 104 : 148));
        cancel.setBounds (actions.removeFromRight (94).withTrimmedRight (gap));
        favorite.setBounds (actions.removeFromLeft (126));
        if (reveal.isVisible()) reveal.setBounds (actions.removeFromLeft (138).withTrimmedLeft (gap));
        if (import.isVisible()) import.setBounds (actions.removeFromLeft (108).withTrimmedLeft (gap));
        content.removeFromBottom (gap);
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
            previewBounds = content;
            previewDescription.setBounds (content.reduced (12).withTrimmedTop (59).withTrimmedBottom (42));
        }
        if (addingFolder)
        {
            auto addRow = contents.removeFromTop (target);
            create.setBounds (addRow.removeFromRight (86));
            folderName.setBounds (addRow.withTrimmedRight (gap));
            contents.removeFromTop (gap / 2);
        }
        emptyBounds = contents;
        list.setBounds (contents.reduced (1));
        list.setRowHeight (target);
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
            text (g, juce::String::fromUTF8 (u8"★"), glyph, 16, Palette::accent);
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
        const auto file = !saving && entry != nullptr && !entry->folder ? entry->path : juce::File();
        if (file == previewFile && file != juce::File())
            return;
        previewFile = file;
        ++previewRequest;
        previewTimer.stopTimer();
        preview.reset();
        previewDescription.setVisible (false);
        previewMessage = "Select a project to see its details.";
        if (readDetails && file != juce::File())
        {
            previewMessage = juce::String::fromUTF8 (u8"Loading project details…");
            previewTimer.startTimer (120);
        }
        repaint (previewBounds);
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
                safe->previewDescription.setColour (juce::TextEditor::textColourId, Palette::text);
                const auto& text = safe->preview->description;
                safe->previewDescription.setText (text.isNotEmpty() ? text : "No description", false);
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

    void ProjectBrowser::refresh (const juce::File& select)
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
        else if (!entries.empty()) list.selectRow (0);
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
        const bool canFavorite = entry != nullptr && !entry->folder;
        const bool isFavorite = canFavorite && favoritePaths.contains (entry->path.getFullPathName());
        primary.setButtonText (saving ? "Save" : pendingOpen == juce::File() ? "Open" : "Discard and Open");
        favorite.setButtonText (isFavorite ? "Remove Favorite" : "Add Favorite");
        favorite.setEnabled (!busy && canFavorite);
        home.setEnabled (!busy && (favoritesOnly || folder != libraryRoot));
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
        if (busy || entry == nullptr || entry->folder) return;
        const auto path = entry->path;
        const auto index = favoritePaths.indexOf (path.getFullPathName());
        if (index >= 0) favoritePaths.remove (index);
        else favoritePaths.add (path.getFullPathName());
        juce::Array<juce::var> values;
        for (const auto& value : favoritePaths) values.add (value);
        preferences.setValue (favoritePreference, juce::JSON::toString (juce::var (values), true));
        preferences.saveIfNeeded();
        refresh (path);
    }

    void ProjectBrowser::submit (juce::File destination)
    {
        if (busy || !onSubmit || (saving && favoritesOnly)) return;
        busy = true;
        showStatus (saving ? juce::String::fromUTF8 (u8"Saving…") : juce::String::fromUTF8 (u8"Opening…"));
        updateActions();
        const SafePointer<ProjectBrowser> safe (this);
        const auto action = onSubmit;
        action (destination, { destination.getFileNameWithoutExtension(), author.getText().trim(), description.getText().trim() }, [safe] (juce::String error)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (error.isEmpty()) safe->dismiss();
            else
            {
                safe->showStatus (error);
                safe->updateActions();
                if (safe->isShowing())
                    (safe->saving ? static_cast<juce::Component&> (safe->name) : safe->list).grabKeyboardFocus();
            }
        });
    }

    void ProjectBrowser::showStatus (const juce::String& message)
    {
        status.setText (message, juce::dontSendNotification);
    }
}
