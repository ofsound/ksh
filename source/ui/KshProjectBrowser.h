#pragma once

#include "KshStyle.h"

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

        explicit ProjectBrowser (juce::PropertiesFile&);
        ~ProjectBrowser() override;
        void open (juce::File root, juce::File current, std::optional<Details>, ListProjects, Submit,
                   std::function<void()> importExternal = {}, bool confirmOpen = false, ReadDetails = {});
        void dismiss();
        std::function<void()> onDismiss;

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
        int getNumRows() override;
        juce::String getNameForRow (int) override;
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
        void selectedRowsChanged (int) override;
        void listBoxItemClicked (int, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;
        void returnKeyPressed (int) override;
        const Entry* entryAt (int) const;
        void refresh (const juce::File& select = {});
        void navigate (juce::File);
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

        juce::PropertiesFile& preferences;
        ListProjects listProjects;
        ReadDetails readDetails;
        Submit onSubmit;
        std::function<void()> onImportExternal;
        juce::File libraryRoot, folderRoot, folder, currentFile, pendingOpen;
        std::vector<Entry> entries;
        juce::StringArray favoritePaths;
        juce::Rectangle<int> card, locationBounds, emptyBounds, previewBounds;
        juce::File previewFile;
        std::optional<Details> preview;
        juce::String previewMessage;
        uint64_t previewRequest = 0;
        bool saving = false, confirmOpen = false, favoritesOnly = false, addingFolder = false, busy = false;
        ProjectList list { *this };
        juce::TextEditor name, author, description, folderName, previewDescription;
        juce::Label status;
        juce::TextButton home { "Projects" }, up { "Up" }, favorites { "Favorites" }, newFolder { "New Folder" };
        juce::TextButton favorite { "Add Favorite" }, import { "Browse Files" }, reveal { "Show in Finder" };
        juce::TextButton cancel { "Cancel" }, primary { "Open" }, create { "Create" };
        juce::TimedCallback previewTimer { [this] { loadPreview(); } };
    };
}
