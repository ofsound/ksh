#include "PluginEditor.h"

#include "engine/KshPersistence.h"

#include <algorithm>

#if JUCE_WEB_BROWSER

namespace
{
constexpr int defaultEditorWidth = 1328;
constexpr int defaultPluginEditorHeight = 828;
constexpr int defaultStandaloneEditorHeight = 872;
} // namespace

KshWebBrowserComponent::KshWebBrowserComponent (const Options& options,
                                                std::function<void()> onPageLoadedIn)
    : juce::WebBrowserComponent (options),
      onPageLoaded (std::move (onPageLoadedIn))
{
}

void KshWebBrowserComponent::pageFinishedLoading (const juce::String& url)
{
    juce::WebBrowserComponent::pageFinishedLoading (url);

    if (onPageLoaded != nullptr)
        onPageLoaded();
}

#endif

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
#if JUCE_WEB_BROWSER
    processorRef.setEditorResizeCallback ([this] (int width, int height)
    {
        setSize (width, height);
    });

    auto onPageLoaded = [this]
    {
        processorRef.getUiBridge().syncAll();
    };

    webView = std::make_unique<KshWebBrowserComponent> (WebViewResources::makeBrowserOptions (processorRef, *this),
                                                        onPageLoaded);
    addAndMakeVisible (*webView);
    processorRef.getUiBridge().attachWebView (webView.get());

   #if JUCE_DEBUG
    webView->goToURL ("http://localhost:5173");
   #else
    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif
#else
    fallbackLabel.setText ("WebView is not available on this platform.\nEnable JUCE_WEB_BROWSER.",
                           juce::dontSendNotification);
    fallbackLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (fallbackLabel);
#endif

#if JUCE_WEB_BROWSER
    scaleMinimumWidth = defaultEditorWidth;
    scaleMinimumHeight = processorRef.hasStandaloneTransport() ? defaultStandaloneEditorHeight
                                                               : defaultPluginEditorHeight;
    applyNormalResizeLimits();
#else
    setResizeLimits (900, 480, 3200, 1400);
#endif
    setResizable (true, true);
    setSize (defaultEditorWidth,
             processorRef.hasStandaloneTransport() ? defaultStandaloneEditorHeight
                                                   : defaultPluginEditorHeight);
    startTimerHz (60);
}

PluginEditor::~PluginEditor()
{
#if JUCE_WEB_BROWSER
    processorRef.getUiBridge().detachWebView();
#endif
}

#if JUCE_WEB_BROWSER
void PluginEditor::applyNormalResizeLimits()
{
    setResizeLimits (scaleMinimumWidth, scaleMinimumHeight, 4096, 2400);
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

    std::sort (files.begin(), files.end(), [] (const juce::File& left, const juce::File& right)
    {
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
    }

    return juce::var (object.release());
}

bool PluginEditor::loadProjectFile (const juce::File& file, juce::String& errorMessage)
{
    constexpr juce::uint64 maximumProjectBytes = 16 * 1024 * 1024;

    if (! file.existsAsFile())
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
    if (! file.loadFileAsData (data))
    {
        errorMessage = "The project file could not be read.";
        return false;
    }

    const auto parsed = ksh::parsePersistencePayload (
        std::string_view (static_cast<const char*> (data.getData()), data.getSize()));
    if (! parsed.has_value())
    {
        errorMessage = "The selected file is not a KSH project.";
        return false;
    }

    processorRef.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
    currentProjectFile = file;

    if (! parsed->contains ("projectName"))
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

    return true;
}

bool PluginEditor::saveProjectFile (const juce::File& file, juce::String& errorMessage)
{
    if (! file.getParentDirectory().createDirectory())
    {
        errorMessage = "The project folder could not be created.";
        return false;
    }

    juce::MemoryBlock data;
    processorRef.getStateInformation (data);
    juce::TemporaryFile temporaryFile (file);

    if (! temporaryFile.getFile().replaceWithData (data.getData(), data.getSize())
        || ! temporaryFile.overwriteTargetFileWithTemporary())
    {
        errorMessage = "The project file could not be written.";
        return false;
    }

    currentProjectFile = file;
    return true;
}

void PluginEditor::showSaveProjectDialog (
    const juce::Array<juce::var>& args,
    juce::WebBrowserComponent::NativeFunctionCompletion complete)
{
    if (projectFileChooser != nullptr)
    {
        complete (projectOperationResult (false, "A project dialog is already open."));
        return;
    }

    const auto now = juce::Time::getCurrentTime().toISO8601 (true);
    const auto name = args.size() > 0 ? args[0].toString().trim() : processorRef.getProjectName();
    const auto description = args.size() > 1 ? args[1].toString() : processorRef.getProjectDescription();
    const auto scale = args.size() > 2 ? static_cast<int> (args[2]) : processorRef.getProjectUiScalePercent();
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
         now,
         scale] (const juce::FileChooser& chooser) mutable
        {
            if (safeThis == nullptr)
                return;

            auto file = chooser.getResult();
            safeThis->projectFileChooser.reset();
            if (file == juce::File())
            {
                completion (safeThis->projectOperationResult (false));
                return;
            }

            if (! file.hasFileExtension (".kshproject"))
                file = file.withFileExtension (".kshproject");

            const auto previousProjectFile = safeThis->currentProjectFile;
            const auto savedToNewPath = ! previousProjectFile.existsAsFile()
                                        || previousProjectFile != file;
            const auto projectNameToSave = savedToNewPath ? file.getFileNameWithoutExtension()
                                                          : name;

            safeThis->processorRef.setProjectMetadata (
                projectNameToSave, description, createdAt, now);
            safeThis->processorRef.setProjectUiScalePercent (scale);
            juce::String error;
            const auto saved = safeThis->saveProjectFile (file, error);
            completion (safeThis->projectOperationResult (saved, error));
        });
}

void PluginEditor::showLoadProjectDialog (
    juce::WebBrowserComponent::NativeFunctionCompletion complete)
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
        [safeThis, completion = std::move (complete)] (const juce::FileChooser& chooser) mutable
        {
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
    juce::WebBrowserComponent::NativeFunctionCompletion complete)
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
    juce::WebBrowserComponent::NativeFunctionCompletion complete)
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

juce::var PluginEditor::handleEditorScaleMinimumRequest (const int minWidth, const int minHeight)
{
    const auto previousMinimumWidth = scaleMinimumWidth;
    const auto previousMinimumHeight = scaleMinimumHeight;
    const auto followsScaleMinimum = getWidth() == previousMinimumWidth
                                  && getHeight() == previousMinimumHeight;

    scaleMinimumWidth = juce::jlimit (900, 2400, minWidth);
    scaleMinimumHeight = juce::jlimit (480, 1800, minHeight);

    applyNormalResizeLimits();
    const auto nextWidth = followsScaleMinimum ? scaleMinimumWidth
                                               : juce::jmax (getWidth(), scaleMinimumWidth);
    const auto nextHeight = followsScaleMinimum ? scaleMinimumHeight
                                                : juce::jmax (getHeight(), scaleMinimumHeight);
    setSize (nextWidth, nextHeight);

    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("available", 1);
    object->setProperty ("minWidth", scaleMinimumWidth);
    object->setProperty ("minHeight", scaleMinimumHeight);
    return juce::var (object.release());
}
#endif

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

void PluginEditor::resized()
{
#if JUCE_WEB_BROWSER
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
#else
    fallbackLabel.setBounds (getLocalBounds());
#endif
}

void PluginEditor::timerCallback()
{
    processorRef.getUiBridge().pollTransportUi();
}
