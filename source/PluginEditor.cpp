#include "PluginEditor.h"
#include "engine/KshPersistence.h"
#include "ui/KshUiScale.h"

PluginEditor::PluginEditor (PluginProcessor& p, bool fitStandaloneToViewport) : AudioProcessorEditor (&p), processorRef (p), fitViewport (fitStandaloneToViewport && p.hasStandaloneTransport())
{
   #if JUCE_IOS
    iosLibrary = p.getIOSProjectLibrary();
    startTimerHz (10);
   #endif
    nativeView = std::make_unique<ksh::ui::NativeEditor> (p);
    if (fitViewport)
        nativeView->useViewportFit();
    addAndMakeVisible (*nativeView);
    nativeView->preferredSizeChanged = [this] { updateNativeSize(); };
    nativeView->zenModeChanged = [this] (bool enabled) { setZenMode (enabled); };
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
       #if JUCE_IOS
        if (action == 6)
        {
            iosProjectAction (6, completed);
            return;
        }
       #endif
        if (action == 2 || action == 5)
        {
            if (action == 2)
                saveProject (completed);
            else
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
    stopTimer();
   #if JUCE_IOS
    iosLibrary->cancel();
   #endif
    nativeView.reset();
}
void PluginEditor::setZenMode (bool enabled)
{
    if (!processorRef.hasStandaloneTransport() || enabled == zenMode)
        return;
    if (fitViewport)
    {
        zenMode = enabled;
        nativeView->setZenMode (enabled);
        resized();
        repaint();
        return;
    }
   #if !JUCE_IOS && !JUCE_ANDROID
    if (enabled)
    {
        auto* window = findParentComponentOfClass<juce::DocumentWindow>();
        if (window == nullptr || !window->isOnDesktop() || zenWindow != nullptr)
            return;
        zenWindow = window;
        windowBoundsBeforeZen = window->getBounds();
        titleBarHeightBeforeZen = window->getTitleBarHeight();
        nativeTitleBarBeforeZen = window->isUsingNativeTitleBar();
        zenFullscreenSeen = zenExitRequested = false;
        zenMode = true;
        nativeView->setZenMode (true);
        updateNativeSize();
        window->setTitleBarHeight (0);
        window->setUsingNativeTitleBar (true);
        window->setFullScreen (true);
        startTimerHz (20);
    }
    else
    {
        zenMode = false;
        nativeView->setZenMode (false);
        if (zenWindow != nullptr && zenWindow->isFullScreen())
        {
            zenFullscreenSeen = zenExitRequested = true;
            zenWindow->setFullScreen (false);
        }
    }
    repaint();
   #endif
}
void PluginEditor::finishZenExit()
{
    const auto window = zenWindow;
    zenMode = false;
    stopTimer();
    nativeView->setZenMode (false);
    if (window != nullptr)
    {
        window->setUsingNativeTitleBar (nativeTitleBarBeforeZen);
        window->setTitleBarHeight (titleBarHeightBeforeZen);
    }
    // Restoring native chrome resizes the editor. Keep the centered viewport
    // active until that is finished so it cannot overwrite the chosen zoom.
    zenWindow = nullptr;
    updateNativeSize();
    if (window != nullptr)
        window->setTopLeftPosition (windowBoundsBeforeZen.getPosition());
    repaint();
}
void PluginEditor::timerCallback()
{
   #if JUCE_IOS
    if (iosLibrary->busy() && iosOperationEpoch != processorRef.getUiBridge().getDocumentEpoch())
        iosLibrary->cancel();
   #else
    if (zenWindow == nullptr)
    {
        finishZenExit();
        return;
    }
    if (zenWindow->isFullScreen())
    {
        zenFullscreenSeen = true;
        if (!zenMode && !zenExitRequested)
        {
            zenExitRequested = true;
            zenWindow->setFullScreen (false);
        }
    }
    else if (zenFullscreenSeen || zenExitRequested)
        finishZenExit();
   #endif
}
bool PluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (zenMode && key == juce::KeyPress::escapeKey)
    {
        setZenMode (false);
        return true;
    }
    return false;
}
void PluginEditor::updateNativeSize()
{
    if (zenMode || zenWindow != nullptr)
    {
        setResizeLimits (1, 1, 100000, 100000);
        if (auto* limits = getConstrainer())
            limits->setFixedAspectRatio (0);
        resized();
        return;
    }
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
    const float scale = ksh::ui::UiScale::factor (processorRef.getProjectUiScalePercent());
    setResizeLimits (juce::roundToInt (layout.width() * ksh::ui::UiScale::factor (ksh::ui::UiScale::minimumPercent)), juce::roundToInt (layout.height() * ksh::ui::UiScale::factor (ksh::ui::UiScale::minimumPercent)), juce::roundToInt (layout.width() * ksh::ui::UiScale::factor (ksh::ui::UiScale::maximumPercent)), juce::roundToInt (layout.height() * ksh::ui::UiScale::factor (ksh::ui::UiScale::maximumPercent)));
    if (auto* limits = getConstrainer())
        limits->setFixedAspectRatio (static_cast<double> (layout.width() / layout.height()));
    setSize (juce::roundToInt (layout.width() * scale), juce::roundToInt (layout.height() * scale));
    resized();
}
void PluginEditor::paint (juce::Graphics& g) { g.fillAll (zenMode || zenWindow != nullptr ? juce::Colours::black : ksh::ui::Palette::app); }
void PluginEditor::mouseDown (const juce::MouseEvent& event)
{
    // Include the margins around the scaled editor in standalone/mobile layouts.
    if (nativeView != nullptr)
        nativeView->mouseDown (event);
}
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
    const float fitScale = juce::jmin (available.getWidth() / layout.width(), available.getHeight() / layout.height());
    const bool centered = zenMode || zenWindow != nullptr;
    const float scale = centered ? juce::jmin (fitScale, ksh::ui::UiScale::factor (processorRef.getProjectUiScalePercent())) : fitScale;
    if (zenMode)
        nativeView->setZenScaleLimit (static_cast<int> (std::floor (fitScale / ksh::ui::UiScale::baseline * 100.0f)));
    const int percent = juce::jlimit (ksh::ui::UiScale::minimumPercent, ksh::ui::UiScale::maximumPercent, juce::roundToInt (scale / ksh::ui::UiScale::baseline * 100));
    if (!centered && !fitViewport && !compact && percent != processorRef.getProjectUiScalePercent())
        processorRef.setProjectUiScalePercent (percent);
    const auto offset = (centered || fitViewport || compact) ? available.getCentre() - juce::Point<float> (layout.width() * scale, layout.height() * scale) * 0.5f : juce::Point<float>();
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

    nativeView->confirmDiscardingChanges (std::move (proceed));
}

juce::File PluginEditor::getDefaultProjectsDirectory() const
{
   #if JUCE_IOS
    return iosLibrary->directory();
   #else
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("ofsound")
        .getChildFile ("KSH");
   #endif
}

juce::Array<juce::File> PluginEditor::getSiblingProjectFiles() const
{
    const auto currentProjectFile = processorRef.getProjectFile();
    const auto directory = currentProjectFile.existsAsFile()
                               ? currentProjectFile.getParentDirectory()
                               : getDefaultProjectsDirectory();
    juce::Array<juce::File> files;

   #if JUCE_IOS
    // Metadata queries include iCloud placeholders which aren't local files yet.
    if (directory == iosLibrary->directory())
        files = iosLibrary->files();
    else
   #endif
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
    const auto currentProjectFile = processorRef.getProjectFile();
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
        object->setProperty ("projectAuthor", processorRef.getProjectAuthor());
        object->setProperty ("projectCreatedAt", processorRef.getProjectCreatedAt());
        object->setProperty ("projectModifiedAt", processorRef.getProjectModifiedAt());
        object->setProperty ("projectFileName", getCurrentProjectFileName());
        object->setProperty ("hasPreviousProject", hasPreviousProject() ? 1 : 0);
        object->setProperty ("hasNextProject", hasNextProject() ? 1 : 0);
        object->setProperty ("projectUiScalePercent", processorRef.getProjectUiScalePercent());
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

    return loadProjectData (file, data, errorMessage);
}

bool PluginEditor::loadProjectData (const juce::File& file, const juce::MemoryBlock& data, juce::String& errorMessage)
{
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

    processorRef.setProjectFile (file);

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

    processorRef.setProjectFile (file);
    processorRef.markDocumentClean();
    return true;
}

void PluginEditor::saveProject (std::function<void (juce::var)> complete)
{
   #if JUCE_IOS
    iosProjectAction (2, std::move (complete));
   #else
    const auto currentProjectFile = processorRef.getProjectFile();
    if (projectFileChooser != nullptr)
    {
        complete (projectOperationResult (false, "A project dialog is already open."));
        return;
    }
    if (!currentProjectFile.existsAsFile())
    {
        showSaveProjectDialog ({ processorRef.getProjectName(), processorRef.getProjectDescription() }, std::move (complete));
        return;
    }
    const auto now = juce::Time::getCurrentTime().toISO8601 (true);
    const auto created = processorRef.getProjectCreatedAt().isEmpty() ? now : processorRef.getProjectCreatedAt();
    processorRef.setProjectMetadata (processorRef.getProjectName(), processorRef.getProjectDescription(), created, now);
    juce::String error;
    const bool saved = saveProjectFile (currentProjectFile, error);
    complete (projectOperationResult (saved, error));
   #endif
}

void PluginEditor::showSaveProjectDialog (
    const juce::Array<juce::var>& args,
    std::function<void (juce::var)> complete)
{
   #if JUCE_IOS
    iosProjectAction (5, std::move (complete), {}, args.size() > 0 ? args[0].toString() : processorRef.getProjectName(),
                      args.size() > 1 ? args[1].toString() : processorRef.getProjectDescription());
   #else
    const auto currentProjectFile = processorRef.getProjectFile();
    if (projectFileChooser != nullptr)
    {
        complete (projectOperationResult (false, "A project dialog is already open."));
        return;
    }

    const auto name = args.size() > 0 ? args[0].toString().trim() : processorRef.getProjectName();
    const auto description = args.size() > 1 ? args[1].toString() : processorRef.getProjectDescription();
    const auto createdAt = processorRef.getProjectCreatedAt();
    const auto epoch = processorRef.getUiBridge().getDocumentEpoch();
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
            epoch] (const juce::FileChooser& chooser) mutable {
            if (safeThis == nullptr)
                return;

            auto file = chooser.getResult();
            safeThis->projectFileChooser.reset();
            if (file == juce::File())
            {
                completion (safeThis->projectOperationResult (false));
                return;
            }
            if (epoch != safeThis->processorRef.getUiBridge().getDocumentEpoch())
            {
                completion (safeThis->projectOperationResult (false, "The project changed while the save dialog was open. Please save again."));
                return;
            }

            if (!file.hasFileExtension (".kshproject"))
                file = file.withFileExtension (".kshproject");

            const auto previousProjectFile = safeThis->processorRef.getProjectFile();
            const auto savedToNewPath = !previousProjectFile.existsAsFile()
                                        || previousProjectFile != file;
            const auto projectNameToSave = savedToNewPath ? file.getFileNameWithoutExtension()
                                                          : name;

            const auto now = juce::Time::getCurrentTime().toISO8601 (true);
            safeThis->processorRef.setProjectMetadata (
                projectNameToSave, description, createdAt.isEmpty() ? now : createdAt, now);
            juce::String error;
            const auto saved = safeThis->saveProjectFile (file, error);
            completion (safeThis->projectOperationResult (saved, error));
        });
   #endif
}

void PluginEditor::showLoadProjectDialog (
    std::function<void (juce::var)> complete)
{
   #if JUCE_IOS
    iosProjectAction (1, std::move (complete));
   #else
    const auto currentProjectFile = processorRef.getProjectFile();
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
   #endif
}

void PluginEditor::createNewProject (
    std::function<void (juce::var)> complete)
{
   #if JUCE_IOS
    if (iosLibrary->busy())
    {
        complete (projectOperationResult (false, "Close the open project operation first."));
        return;
    }
   #endif
    if (projectFileChooser != nullptr)
    {
        complete (projectOperationResult (false, "Close the open project dialog first."));
        return;
    }

    processorRef.resetProject();
    processorRef.setProjectFile ({});
    complete (projectOperationResult (true));
}

void PluginEditor::cycleProject (
    const int direction,
    std::function<void (juce::var)> complete)
{
    const auto currentProjectFile = processorRef.getProjectFile();
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

   #if JUCE_IOS
    iosProjectAction (1, std::move (complete), files[nextIndex]);
   #else
    juce::String error;
    const auto loaded = loadProjectFile (files[nextIndex], error);
    complete (projectOperationResult (loaded, error));
   #endif
}

#if JUCE_IOS
void PluginEditor::iosProjectAction (int action, std::function<void (juce::var)> done,
                                    juce::File target, juce::String name, juce::String description)
{
    if (iosLibrary->busy())
    {
        done (projectOperationResult (false, "A project operation is already open."));
        return;
    }
    iosOperationEpoch = processorRef.getUiBridge().getDocumentEpoch();
    const auto epoch = iosOperationEpoch;
    const auto revision = processorRef.getUiBridge().getRevision();
    const juce::Component::SafePointer<PluginEditor> safe (this);
    if (action == 1)
    {
        auto loaded = [safe, epoch, revision, complete = std::move (done)] (ksh::IOSProjectLibrary::Result result) {
            if (safe == nullptr) return;
            juce::String error = result.error;
            bool success = false;
            if (!result.cancelled && error.isEmpty())
            {
                if (epoch != safe->processorRef.getUiBridge().getDocumentEpoch()
                    || revision != safe->processorRef.getUiBridge().getRevision())
                    error = "The project changed while opening the file. Please open it again.";
                else
                    success = safe->loadProjectData (result.file, result.data, error);
                if (success) safe->iosLibrary->acceptFile (result.file);
            }
            complete (safe->projectOperationResult (success, error));
        };
        if (target == juce::File()) iosLibrary->chooseOpen (*this, std::move (loaded));
        else iosLibrary->read (target, std::move (loaded));
        return;
    }
    if (name.isEmpty()) name = processorRef.getProjectName();
    if (action != 5) description = processorRef.getProjectDescription();
    const auto now = juce::Time::getCurrentTime().toISO8601 (true);
    const auto created = processorRef.getProjectCreatedAt().isEmpty() ? now : processorRef.getProjectCreatedAt();
    auto snapshot = processorRef.enginePersistenceState();
    snapshot["projectName"] = name.toStdString();
    snapshot["projectDescription"] = description.toStdString();
    snapshot["projectCreatedAt"] = created.toStdString();
    snapshot["projectModifiedAt"] = now.toStdString();
    const auto text = snapshot.dump();
    juce::MemoryBlock data (text.data(), text.size());
    auto saved = [safe, epoch, action, name, description, created, now, revision,
                  complete = std::move (done)] (ksh::IOSProjectLibrary::Result result) {
        if (safe == nullptr) return;
        if (result.cancelled || result.error.isNotEmpty())
        {
            complete (safe->projectOperationResult (false, result.error));
            return;
        }
        if (epoch != safe->processorRef.getUiBridge().getDocumentEpoch())
        {
            complete (safe->projectOperationResult (false, "The project changed while the file operation was open."));
            return;
        }
        if (action != 6)
        {
            safe->iosLibrary->acceptFile (result.file);
            safe->processorRef.setProjectFile (result.file);
            // A file operation may finish after more edits or recording. Keep those edits dirty.
            if (revision == safe->processorRef.getUiBridge().getRevision())
            {
                safe->processorRef.setProjectMetadata (name, description, created, now);
                safe->processorRef.markDocumentClean();
            }
        }
        complete (safe->projectOperationResult (true));
    };
    if (action == 6) iosLibrary->share (*this, name, std::move (data), std::move (saved));
    else iosLibrary->save (*this, processorRef.getProjectFile(), name, std::move (data), action == 5, std::move (saved));
}
#endif
