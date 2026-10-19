#include "PluginEditor.h"
#include "engine/KshPersistence.h"

PluginEditor::PluginEditor (PluginProcessor& p, bool fitStandaloneToViewport) : AudioProcessorEditor (&p), processorRef (p), fitViewport (fitStandaloneToViewport && p.hasStandaloneTransport())
{
    nativeView = std::make_unique<ksh::ui::NativeEditor> (p);
    if (fitViewport)
        nativeView->useViewportFit();
    addAndMakeVisible (*nativeView);
    nativeView->preferredSizeChanged = [this] { updateNativeSize(); };
    nativeView->projectAction = [this] (int action) {
        const juce::Component::SafePointer<PluginEditor> safe (this);
        auto completed = [safe] (juce::var result) {
            if (safe == nullptr)
                return;
            const auto error = result["error"].toString();
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "KSH project", error);
            safe->nativeView->controller.refresh();
            safe->updateNativeSize();
        };
        if (action == 2)
        {
            showSaveProjectDialog ({ nativeView->projectTitle(), nativeView->projectDescription() }, completed);
            return;
        }
        confirmDiscardingChanges ([safe, action, completed] {
            if (safe == nullptr)
                return;
            if (action == 0)
                safe->createNewProject (completed);
            else if (action == 1)
                safe->showLoadProjectDialog (completed);
            else
                safe->cycleProject (action == 3 ? -1 : 1, completed);
        });
    };
    setResizable (!fitViewport, !fitViewport);
    updateNativeSize();
}
PluginEditor::~PluginEditor()
{
    processorRef.setEditorResizeCallback ({});
    nativeView.reset();
}
void PluginEditor::updateNativeSize()
{
   #if JUCE_IOS
    if (!fitViewport && juce::SystemStats::getDeviceDescription().startsWithIgnoreCase ("iPhone"))
    {
        setResizeLimits (640, 300, 2048, 1536);
        if (getWidth() == 0 || getHeight() == 0)
            setSize (814, 409);
        resized();
        return;
    }
   #endif
    if (fitViewport)
    {
        // The mobile wrapper owns the fullscreen bounds. Never grow its content
        // in response to a saved project scale or a change in pattern height.
        if (getWidth() == 0 || getHeight() == 0)
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                setSize (juce::roundToInt (display->userBounds.getWidth()), juce::roundToInt (display->userBounds.getHeight()));
        resized();
        return;
    }
    const auto& layout = nativeView->layout;
    const float scale = static_cast<float> (processorRef.getProjectUiScalePercent()) / 100.0f;
    setResizeLimits (juce::roundToInt (layout.width() * 0.5f), juce::roundToInt (layout.height() * 0.5f), juce::roundToInt (layout.width() * 1.2f), juce::roundToInt (layout.height() * 1.2f));
    if (auto* limits = getConstrainer())
        limits->setFixedAspectRatio (static_cast<double> (layout.width() / layout.height()));
    setSize (juce::roundToInt (layout.width() * scale), juce::roundToInt (layout.height() * scale));
    resized();
}
void PluginEditor::paint (juce::Graphics& g) { g.fillAll (ksh::ui::Palette::app); }
void PluginEditor::resized()
{
    if (!nativeView)
        return;
    auto available = getLocalBounds().toFloat();
    if (fitViewport)
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        {
            const auto safeScreen = display->safeAreaInsets.subtractedFrom (display->userBounds.toNearestInt());
            available = available.getIntersection (getLocalArea (nullptr, safeScreen).toFloat());
        }
    if (available.isEmpty())
        return;
   #if JUCE_IOS
    constexpr bool mobile = true;
   #else
    const bool mobile = fitViewport;
   #endif
    const bool compact = mobile && available.getWidth() >= available.getHeight() && available.getHeight() <= 500.0f && available.getWidth() <= 1100.0f;
    nativeView->setCompactViewport (compact, juce::jmax (814, juce::roundToInt (available.getWidth())), juce::jmax (390, juce::roundToInt (available.getHeight())));
    const auto& layout = nativeView->layout;
    const float scale = juce::jmin (available.getWidth() / layout.width(), available.getHeight() / layout.height());
    const int percent = juce::jlimit (50, 120, juce::roundToInt (scale * 100));
    if (!fitViewport && !compact && percent != processorRef.getProjectUiScalePercent())
        processorRef.setProjectUiScalePercent (percent);
    const auto offset = (fitViewport || compact) ? available.getCentre() - juce::Point<float> (layout.width() * scale, layout.height() * scale) * 0.5f : juce::Point<float>();
    nativeView->setTransform (juce::AffineTransform::scale (scale).translated (offset));
    nativeView->setBounds (0, 0, juce::roundToInt (layout.width()), juce::roundToInt (layout.height()));
}

void PluginEditor::confirmDiscardingChanges (std::function<void()> proceed)
{
    if (!processorRef.isDocumentModified())
    {
        proceed();
        return;
    }

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        "Unsaved changes",
        "\"" + processorRef.getProjectName() + "\" has changes that have not been saved. Discard them?",
        "Discard Changes",
        "Cancel",
        this,
        juce::ModalCallbackFunction::create ([discardAndProceed = std::move (proceed)] (int result) {
            if (result == 1)
                discardAndProceed();
        }));
}

juce::File PluginEditor::getDefaultProjectsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("ofsound")
        .getChildFile ("KSH");
}

juce::Array<juce::File> PluginEditor::getSiblingProjectFiles() const
{
    const auto directory = currentProjectFile.existsAsFile()
                               ? currentProjectFile.getParentDirectory()
                               : getDefaultProjectsDirectory();
    juce::Array<juce::File> files;

    if (directory.isDirectory())
        directory.findChildFiles (files, juce::File::findFiles, false, "*.kshproject");

    std::sort (files.begin(), files.end(), [] (const juce::File& left, const juce::File& right) {
        return left.getFileName().compareNatural (right.getFileName(), true) < 0;
    });
    return files;
}

bool PluginEditor::hasPreviousProject() const
{
    return getSiblingProjectFiles().size() > 1;
}

bool PluginEditor::hasNextProject() const
{
    return getSiblingProjectFiles().size() > 1;
}

juce::String PluginEditor::getCurrentProjectFileName() const
{
    return currentProjectFile.existsAsFile() ? currentProjectFile.getFileName() : juce::String();
}

juce::var PluginEditor::projectOperationResult (const bool success,
    const juce::String& errorMessage) const
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("success", success ? 1 : 0);
    object->setProperty ("error", errorMessage);

    if (success)
    {
        object->setProperty ("projectName", processorRef.getProjectName());
        object->setProperty ("projectDescription", processorRef.getProjectDescription());
        object->setProperty ("projectCreatedAt", processorRef.getProjectCreatedAt());
        object->setProperty ("projectModifiedAt", processorRef.getProjectModifiedAt());
        object->setProperty ("projectFileName", getCurrentProjectFileName());
        object->setProperty ("hasPreviousProject", hasPreviousProject() ? 1 : 0);
        object->setProperty ("hasNextProject", hasNextProject() ? 1 : 0);
        object->setProperty ("projectUiScalePercent", processorRef.getProjectUiScalePercent());
        object->setProperty ("projectThemeMode", processorRef.getProjectThemeMode());
    }

    return juce::var (object.release());
}

bool PluginEditor::loadProjectFile (const juce::File& file, juce::String& errorMessage)
{
    constexpr juce::uint64 maximumProjectBytes = 16 * 1024 * 1024;

    if (!file.existsAsFile())
    {
        errorMessage = "That project file no longer exists.";
        return false;
    }

    if (file.getSize() <= 0 || static_cast<juce::uint64> (file.getSize()) > maximumProjectBytes)
    {
        errorMessage = "The selected file is empty or too large to be a KSH project.";
        return false;
    }

    juce::MemoryBlock data;
    if (!file.loadFileAsData (data))
    {
        errorMessage = "The project file could not be read.";
        return false;
    }

    const auto parsed = ksh::parsePersistencePayload (
        std::string_view (static_cast<const char*> (data.getData()), data.getSize()));
    if (!parsed.has_value())
    {
        errorMessage = "The selected file is not a KSH project.";
        return false;
    }

    if (!processorRef.loadStateFromData (data.getData(), static_cast<int> (data.getSize())))
    {
        errorMessage = "The project file is damaged or was saved by an incompatible version of KSH.";
        return false;
    }

    currentProjectFile = file;

    if (!parsed->contains ("projectName"))
    {
        processorRef.setProjectMetadata (file.getFileNameWithoutExtension(),
            {},
            {},
            {});
    }
    else if (processorRef.getProjectName() == "Untitled Project"
             && file.getFileNameWithoutExtension().isNotEmpty()
             && file.getFileNameWithoutExtension() != "Untitled Project")
    {
        processorRef.setProjectMetadata (file.getFileNameWithoutExtension(),
            processorRef.getProjectDescription(),
            processorRef.getProjectCreatedAt(),
            processorRef.getProjectModifiedAt());
    }

    processorRef.markDocumentClean();
    return true;
}

bool PluginEditor::saveProjectFile (const juce::File& file, juce::String& errorMessage)
{
    if (!file.getParentDirectory().createDirectory())
    {
        errorMessage = "The project folder could not be created.";
        return false;
    }

    juce::MemoryBlock data;
    processorRef.getStateInformation (data);
    juce::TemporaryFile temporaryFile (file);

    if (!temporaryFile.getFile().replaceWithData (data.getData(), data.getSize())
        || !temporaryFile.overwriteTargetFileWithTemporary())
    {
        errorMessage = "The project file could not be written.";
        return false;
    }

    currentProjectFile = file;
    processorRef.markDocumentClean();
    return true;
}

void PluginEditor::showSaveProjectDialog (
    const juce::Array<juce::var>& args,
    std::function<void (juce::var)> complete)
{
    if (projectFileChooser != nullptr)
    {
        complete (projectOperationResult (false, "A project dialog is already open."));
        return;
    }

    const auto now = juce::Time::getCurrentTime().toISO8601 (true);
    const auto name = args.size() > 0 ? args[0].toString().trim() : processorRef.getProjectName();
    const auto description = args.size() > 1 ? args[1].toString() : processorRef.getProjectDescription();
    const auto createdAt = processorRef.getProjectCreatedAt().isNotEmpty()
                               ? processorRef.getProjectCreatedAt()
                               : now;
    auto directory = currentProjectFile.existsAsFile() ? currentProjectFile.getParentDirectory()
                                                       : getDefaultProjectsDirectory();
    directory.createDirectory();

    auto legalName = juce::File::createLegalFileName (
        name.isNotEmpty() ? name : juce::String ("Untitled Project"));
    if (legalName.isEmpty())
        legalName = "Untitled Project";

    const auto suggestedFile = currentProjectFile.existsAsFile()
                                   ? currentProjectFile
                                   : directory.getChildFile (legalName + ".kshproject");

    projectFileChooser = std::make_unique<juce::FileChooser> (
        "Save KSH Project", suggestedFile, "*.kshproject", true);
    const juce::Component::SafePointer<PluginEditor> safeThis (this);
    projectFileChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis,
            completion = std::move (complete),
            name,
            description,
            createdAt,
            now] (const juce::FileChooser& chooser) mutable {
            if (safeThis == nullptr)
                return;

            auto file = chooser.getResult();
            safeThis->projectFileChooser.reset();
            if (file == juce::File())
            {
                completion (safeThis->projectOperationResult (false));
                return;
            }

            if (!file.hasFileExtension (".kshproject"))
                file = file.withFileExtension (".kshproject");

            const auto previousProjectFile = safeThis->currentProjectFile;
            const auto savedToNewPath = !previousProjectFile.existsAsFile()
                                        || previousProjectFile != file;
            const auto projectNameToSave = savedToNewPath ? file.getFileNameWithoutExtension()
                                                          : name;

            safeThis->processorRef.setProjectMetadata (
                projectNameToSave, description, createdAt, now);
            juce::String error;
            const auto saved = safeThis->saveProjectFile (file, error);
            completion (safeThis->projectOperationResult (saved, error));
        });
}

void PluginEditor::showLoadProjectDialog (
    std::function<void (juce::var)> complete)
{
    if (projectFileChooser != nullptr)
    {
        complete (projectOperationResult (false, "A project dialog is already open."));
        return;
    }

    const auto start = currentProjectFile.existsAsFile() ? currentProjectFile
                                                         : getDefaultProjectsDirectory();
    projectFileChooser = std::make_unique<juce::FileChooser> (
        "Load KSH Project", start, "*.kshproject", true);
    const juce::Component::SafePointer<PluginEditor> safeThis (this);
    projectFileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, completion = std::move (complete)] (const juce::FileChooser& chooser) mutable {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            safeThis->projectFileChooser.reset();
            if (file == juce::File())
            {
                completion (safeThis->projectOperationResult (false));
                return;
            }

            juce::String error;
            const auto loaded = safeThis->loadProjectFile (file, error);
            completion (safeThis->projectOperationResult (loaded, error));
        });
}

void PluginEditor::createNewProject (
    std::function<void (juce::var)> complete)
{
    if (projectFileChooser != nullptr)
    {
        complete (projectOperationResult (false, "Close the open project dialog first."));
        return;
    }

    processorRef.resetProject();
    currentProjectFile = juce::File();
    complete (projectOperationResult (true));
}

void PluginEditor::cycleProject (
    const int direction,
    std::function<void (juce::var)> complete)
{
    const auto files = getSiblingProjectFiles();

    if (files.size() <= 1)
    {
        complete (projectOperationResult (
            false,
            files.isEmpty() ? juce::String ("No saved projects were found in the projects folder.")
                            : juce::String()));
        return;
    }

    auto currentIndex = files.indexOf (currentProjectFile);
    int nextIndex = 0;

    if (currentIndex < 0)
    {
        nextIndex = direction < 0 ? files.size() - 1 : 0;
    }
    else
    {
        nextIndex = currentIndex + (direction < 0 ? -1 : 1);

        if (nextIndex < 0)
            nextIndex = files.size() - 1;
        else if (nextIndex >= files.size())
            nextIndex = 0;
    }

    if (currentIndex >= 0 && nextIndex == currentIndex)
    {
        complete (projectOperationResult (false));
        return;
    }

    juce::String error;
    const auto loaded = loadProjectFile (files[nextIndex], error);
    complete (projectOperationResult (loaded, error));
}
