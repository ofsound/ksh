#pragma once

#include "KshProjectDetails.h"

namespace ksh::ui
{
    class ProjectBrowser : public juce::Component, private juce::ListBoxModel
    {
    public:
        struct Details
        {
            juce::String name, author, description;
            juce::String createdAt {}, modifiedAt {};
        };
        using ListProjects = std::function<juce::Array<juce::File> (const juce::File&)>;
        using ReadDetails = std::function<void (const juce::File&, std::function<void (std::optional<Details>)>)>;
        using Completion = std::function<void (juce::String)>;
        using Submit = std::function<void (const juce::File&, const Details&, Completion)>;
        struct StorageLocations
        {
            juce::String projectsName;
            juce::File cloudFolder;
            std::function<void (std::function<void (juce::File)>)> chooseCloudFolder;
        };

        ProjectBrowser (juce::PropertiesFile&, PluginProcessor&);
        ~ProjectBrowser() override;
        void open (juce::File root, juce::File current, std::optional<Details>, ListProjects, Submit,
                   std::function<void()> importExternal = {}, bool confirmOpen = false, ReadDetails = {}, juce::File lastFolder = {},
                   StorageLocations = {});
        void dismiss();
        void setRowArea (juce::Rectangle<int>);
        void refreshCurrentProject();
        std::function<void (const juce::File&)> onDismiss;
        std::function<void()> onRestore;
       #if JUCE_IOS
        std::function<void()> onShare;
       #endif

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusOfChildComponentChanged (FocusChangeType) override;

    private:
        struct Entry
        {
            juce::File path;
            bool folder = false;
            juce::String parentLabel;
            juce::String label() const { return folder ? path.getFileName() : path.getFileNameWithoutExtension(); }
        };
        enum class Place { local, cloud, other, favorites };
        struct Location
        {
            juce::File root, folder;
            Place place = Place::local;
            bool operator== (const Location&) const = default;
        };
        struct Visit
        {
            Location location;
            juce::File selection;
        };
        enum class Glyph { none, back, forward, folder, cloud, favorite, add, more, project };
        class BrowserButton final : public juce::TextButton
        {
        public:
            BrowserButton (const juce::String&, Glyph = Glyph::none, bool breadcrumb = false);
            void paintButton (juce::Graphics&, bool hovered, bool down) override;
            int preferredWidth (int maximum) const;
        private:
            Glyph glyph;
            bool breadcrumb;
        };
        class ProjectList : public juce::ListBox
        {
        public:
            explicit ProjectList (ProjectBrowser&);
            bool keyPressed (const juce::KeyPress&) override;
            void mouseEnter (const juce::MouseEvent&) override;
            void mouseExit (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;
            void setHoveredRow (int);
            int hoveredRow() const { return hover; }
        private:
            ProjectBrowser& owner;
            int hover = -1;
        };
        class FavoriteButton final : public juce::Button
        {
        public:
            FavoriteButton();
            void paintButton (juce::Graphics&, bool hovered, bool down) override;
        };
        int getNumRows() override;
        juce::String getNameForRow (int) override;
        juce::String getTooltipForRow (int) override;
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged (int) override;
        void listBoxItemClicked (int, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
        void listWasScrolled() override;
        void returnKeyPressed (int) override;
        const Entry* entryAt (int) const;
        void refresh (const juce::File& select = {}, bool selectFirst = true);
        void navigate (Location, const juce::File& select = {});
        void navigateToFolder (const juce::File&);
        Location locationForFolder (const juce::File&) const;
        void changeLocation (Location, const juce::File& select);
        void goThroughHistory (int direction);
        juce::File selectedPath() const;
        void rebuildBreadcrumbs();
        void layoutBreadcrumbs();
        void showHiddenBreadcrumbs();
        void showEntryMenu (int, const juce::MouseEvent&);
        void goUp();
        void activate (int);
        void updateActions();
        void setFolderEditing (bool);
        void createFolder();
        void toggleFavorite();
        juce::File saveDestination() const;
        void setFavorite (const juce::File&, bool);
        void submit (juce::File);
        void showStatus (const juce::String&);
        void updatePreview (const Entry*);
        void loadPreview();
        void layoutPreview();
        static void drawGlyph (juce::Graphics&, Glyph, juce::Rectangle<float>, juce::Colour);

        juce::PropertiesFile& preferences;
        PluginProcessor& processor;
        ListProjects listProjects;
        ReadDetails readDetails;
        Submit onSubmit;
        std::function<void()> onImportExternal;
        StorageLocations locations;
        juce::File libraryRoot, currentFile, pendingOpen;
        Location location;
        std::vector<Visit> history;
        size_t historyIndex = 0;
        std::vector<Entry> entries;
        juce::StringArray favoritePaths;
        std::vector<std::unique_ptr<BrowserButton>> breadcrumbs;
        juce::Array<int> hiddenBreadcrumbs;
        juce::Rectangle<int> rowArea, breadcrumbBounds, locationGroupBounds, emptyBounds, previewBounds;
        juce::Rectangle<int> panelBounds() const { return rowArea.isEmpty() ? getLocalBounds() : rowArea; }
        juce::Rectangle<int> previewIdentityBounds, previewDescriptionBounds, previewDatesBounds;
        juce::File previewFile;
        std::optional<Details> preview;
        juce::String previewMessage;
        uint64_t previewRequest = 0, documentEpoch = 0;
        std::optional<bool> saveFavorite;
        int targetSize() const;
        bool saving = false, confirmOpen = false, addingFolder = false, busy = false;
        bool showingFavorites() const { return location.place == Place::favorites; }
        ProjectList list { *this };
        juce::TextEditor name, author, description, folderName, previewDescription;
        juce::Label status;
        FavoriteButton favorite;
        BrowserButton back { {}, Glyph::back }, forward { {}, Glyph::forward };
        BrowserButton home { "Local Projects", Glyph::folder }, cloud { "iCloud Drive", Glyph::cloud };
        BrowserButton favorites { "Favorites", Glyph::favorite }, newFolder { "New Folder", Glyph::add };
        BrowserButton breadcrumbOverflow { {}, Glyph::more, true };
        BrowserButton import { "Browse Files", Glyph::folder };
        juce::TextButton restore { "RESTORE PREVIOUS" };
       #if JUCE_IOS
        juce::TextButton share { "SHARE" };
       #endif
        juce::TextButton cancel { "CANCEL" }, primary { "OPEN" }, create { "CREATE" };
        juce::TimedCallback previewTimer { [this] { loadPreview(); } };
    };
}
