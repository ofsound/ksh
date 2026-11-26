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

    private:
        struct Entry
        {
            juce::File path;
            bool folder = false;
        };
        class ProjectList : public juce::ListBox
        {
        public:
            explicit ProjectList (ProjectBrowser& browser) : juce::ListBox ("Projects", &browser), owner (browser) {}
            bool keyPressed (const juce::KeyPress&) override;
        private:
            ProjectBrowser& owner;
        };
        class FavoriteButton final : public juce::Button
        {
        public:
            FavoriteButton();
            void paintButton (juce::Graphics&, bool hovered, bool down) override;
        };
        int getNumRows() override;
        juce::String getNameForRow (int) override;
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged (int) override;
        void listBoxItemClicked (int, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
        void returnKeyPressed (int) override;
        const Entry* entryAt (int) const;
        void refresh (const juce::File& select = {}, bool selectFirst = true);
        void navigate (juce::File);
        void navigateToRoot (const juce::File&);
        void goUp();
        void activate (int);
        void updateActions();
        void setFolderEditing (bool);
        void createFolder();
        void toggleFavorite();
        void submit (juce::File);
        void showStatus (const juce::String&);
        void updatePreview (const Entry*);
        void loadPreview();
        void layoutPreview();

        juce::PropertiesFile& preferences;
        ListProjects listProjects;
        ReadDetails readDetails;
        Submit onSubmit;
        std::function<void()> onImportExternal;
        StorageLocations locations;
        juce::File libraryRoot, folderRoot, folder, currentFile, pendingOpen;
        std::vector<Entry> entries;
        juce::StringArray favoritePaths;
        juce::Rectangle<int> card, locationBounds, emptyBounds, previewBounds;
        juce::Rectangle<int> previewIdentityBounds, previewDescriptionBounds, previewDatesBounds;
        juce::File previewFile;
        std::optional<Details> preview;
        juce::String previewMessage;
        uint64_t previewRequest = 0;
        bool saving = false, confirmOpen = false, favoritesOnly = false, addingFolder = false, busy = false;
        ProjectList list { *this };
        ProjectDetails currentDetails;
        juce::TextEditor name, author, description, folderName, previewDescription;
        juce::Label status;
        FavoriteButton favorite;
        juce::TextButton home { "PROJECTS" }, up { "UP" }, favorites { "FAVORITES" }, newFolder { "NEW FOLDER" };
        juce::TextButton cloud { "ICLOUD DRIVE" };
        juce::TextButton import { "BROWSE FILES" }, reveal { "SHOW IN FINDER" };
        juce::TextButton currentProject { "CURRENT PROJECT" };
        juce::TextButton cancel { "CANCEL" }, primary { "OPEN" }, create { "CREATE" };
        juce::TimedCallback previewTimer { [this] { loadPreview(); } };
    };
}
