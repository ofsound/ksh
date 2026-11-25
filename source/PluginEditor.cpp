#include "PluginEditor.h"
#include "engine/KshMath.h"
#include "engine/KshPersistence.h"
#include "ui/KshDrumComputerImport.h"
#include "ui/KshUiScale.h"
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
 #include <unistd.h>
#endif

namespace
{
    constexpr juce::int64 maximumProjectBytes = 16 * 1024 * 1024;

    std::optional<ksh::ui::ProjectBrowser::Details> projectBrowserDetails (const juce::File& file, const juce::MemoryBlock& data)
    {
        if (data.isEmpty())
            return {};
        const auto project = ksh::parsePersistencePayload (
            std::string_view (static_cast<const char*> (data.getData()), data.getSize()));
        if (!project)
            return {};
        const auto get = [&] (const char* key) -> juce::String
        {
            const auto value = project->find (key);
            return value != project->end() && value->is_string() ? juce::String (value->get_ref<const std::string&>()) : juce::String();
        };
        auto name = get ("projectName");
        if (name.isEmpty() || name == "Untitled Project")
            name = file.getFileNameWithoutExtension();
        return ksh::ui::ProjectBrowser::Details { name, get ("projectAuthor"), get ("projectDescription"),
                                                  get ("projectCreatedAt"), get ("projectModifiedAt") };
    }

    auto finishBrowserOperation (ksh::ui::ProjectBrowser::Completion done,
                                 std::function<void (juce::var)> complete, juce::String fallback)
    {
        return [notifyBrowser = std::move (done), notifyEditor = std::move (complete), defaultError = std::move (fallback)] (juce::var result)
        {
            if (static_cast<bool> (result["success"]))
            {
                notifyBrowser ({});
                notifyEditor (result);
            }
            else
            {
                const auto error = result["error"].toString();
                notifyBrowser (error.isNotEmpty() ? error : defaultError);
            }
        };
    }
}

PluginEditor::PluginEditor (PluginProcessor& p, bool fitStandaloneToViewport) : AudioProcessorEditor (&p), processorRef (p), fitViewport (fitStandaloneToViewport && p.hasStandaloneTransport())
{
   #if JUCE_IOS
    iosLibrary = p.getIOSProjectLibrary();
    startTimerHz (10);
   #endif
    nativeView = std::make_unique<ksh::ui::NativeEditor> (p);
    addAndMakeVisible (*nativeView);
   #if JUCE_IOS
    iosFileDrop = std::make_unique<ksh::IOSFileDrop> (*this, [this]
    {
        const auto drop = beginFileDrop();
        const juce::Component::SafePointer<PluginEditor> safe (this);
        return [safe, drop] (std::shared_ptr<juce::TemporaryFile> staged, juce::String originalName)
        {
            if (safe == nullptr || drop.request != safe->fileDropRequest)
                return;
            if (staged == nullptr)
            {
                safe->showFileDropStatus ("Couldn't open the dropped file.");
                return;
            }
            const auto file = staged->getFile();
            safe->analyseFileDrop (drop, file, originalName, std::move (staged));
        };
    });
   #endif
    nativeView->preferredSizeChanged = [this] { updateNativeSize(); };
    nativeView->zenModeChanged = [this] (bool enabled) { setZenMode (enabled); };
    nativeView->projectAction = [this] (int action) {
        const juce::Component::SafePointer<PluginEditor> safe (this);
        auto completed = [safe, action] (juce::var result) {
            if (safe == nullptr)
                return;
            const auto error = result["error"].toString();
            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "KSH project", error);
            safe->nativeView->controller.refresh();
            if (action == 0 && static_cast<bool> (result["success"]))
                safe->nativeView->returnToPhoneGrid();
            if ((action == 2 || action == 5) && static_cast<bool> (result["success"]))
                safe->nativeView->showProjectSaved();
            safe->updateNativeSize();
        };
       #if JUCE_IOS
        if (action == 6)
        {
            iosProjectAction (6, completed);
            return;
        }
       #endif
        if (action == 1)
        {
            showLoadProjectDialog (completed);
            return;
        }
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
            else
                safe->cycleProject (action == 3 ? -1 : 1, completed);
        });
    };
    const bool hostedTablet = usesHostedTabletViewport();
    setResizable (!fitViewport, !fitViewport && !hostedTablet);
    if (hostedTablet)
    {
        // JUCE reads this initial size as the preferred AUv3 window size.
        using ksh::ui::Layout;
        constexpr float scale = ksh::ui::UiScale::baseline;
        setSize (juce::roundToInt (Layout::fullWidth * scale),
                 juce::roundToInt ((Layout::fullHeight - Layout::footerHeight) * scale));
    }
    else
        updateNativeSize();
    ksh::ui::Palette::changes().addChangeListener (this);
}
PluginEditor::~PluginEditor()
{
    ksh::ui::Palette::changes().removeChangeListener (this);
    stopTimer();
    fileDropStatusTimer.stopTimer();
   #if JUCE_IOS
    iosFileDrop.reset();
    iosLibrary->cancel();
   #endif
    fileDropWorker.reset();
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
bool PluginEditor::usesHostedTabletViewport() const
{
    return nativeView->layout.touchRows && processorRef.wrapperType == juce::AudioProcessor::wrapperType_AudioUnitv3
        && !juce::SystemStats::getDeviceDescription().startsWithIgnoreCase ("iPhone");
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
    if (usesHostedTabletViewport())
    {
        // Fit the content inside the host's viewport, including on project changes.
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
void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The scaled child editor only repaints inside its own bounds.
    repaint();
}
void PluginEditor::paintOverChildren (juce::Graphics& g)
{
    using namespace ksh::ui;
    if (fileDragOver)
    {
        g.setColour (Palette::accent.withAlpha (0.65f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 6.0f, 2.0f);
    }
    if (fileDropStatus.isNotEmpty())
    {
        const auto fontHeight = 15.0f;
        const auto width = juce::jmin (getWidth() - 24, 440);
        const auto area = juce::Rectangle<int> (width, 34).withCentre ({ getWidth() / 2, 27 });
        g.setColour (Palette::app.withAlpha (0.96f));
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        g.setColour (Palette::accent.withAlpha (0.65f));
        g.drawRoundedRectangle (area.toFloat(), 6.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.setFont (font (fontHeight));
        g.drawFittedText (fileDropStatus, area.reduced (10, 0), juce::Justification::centred, 1);
    }
}

bool PluginEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.size() != 1)
        return false;
    const juce::File file (files[0]);
    return file.hasFileExtension ("sbb") || ksh::ui::isDrumAudioFile (file)
        || ksh::ui::swingFileType (file) == ksh::ui::SwingFileType::midi;
}

void PluginEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragOver = true;
    repaint();
}

void PluginEditor::fileDragExit (const juce::StringArray&)
{
    fileDragOver = false;
    repaint();
}

void PluginEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragExit (files);
    if (isInterestedInFileDrag (files))
    {
        const juce::File file (files[0]);
        analyseFileDrop (beginFileDrop(), file, file.getFileName());
    }
}

PluginEditor::FileDrop PluginEditor::beginFileDrop()
{
    if (fileDropWorker != nullptr)
        fileDropWorker->removeAllJobs (false, 0);
    const auto state = processorRef.nativeUiState();
    showFileDropStatus ("Reading file...", true);
    return { ++fileDropRequest, state.documentEpoch, state.playback.beatsPerStep,
             processorRef.hasStandaloneTransport() ? processorRef.getStandaloneTempoBpm() : state.playback.tempo,
             state.playback.swing, state.playback.swingSubdivisionIndex };
}

void PluginEditor::analyseFileDrop (FileDrop drop, const juce::File& file, const juce::String& originalName,
                                   std::shared_ptr<juce::TemporaryFile> stagedFile)
{
    if (fileDropWorker == nullptr)
        fileDropWorker = std::make_unique<juce::ThreadPool> (1);

    const juce::Component::SafePointer<PluginEditor> safe (this);
    const bool isPreset = file.hasFileExtension ("sbb");
    if (isPreset || ksh::ui::isDrumAudioFile (file) || (stagedFile != nullptr && file.hasFileExtension ("audio")))
    {
        showFileDropStatus (isPreset ? "Reading DrumComputer preset..." : "Transcribing drums...", true);
        fileDropWorker->addJob ([safe, drop, file, originalName, isPreset, staged = std::move (stagedFile)]
        {
            ProjectDrop converted;
            if (isPreset)
            {
                auto preset = ksh::ui::importDrumComputerPreset (file, originalName, drop.bpm);
                auto summary = juce::String ("Imported ") + juce::String (preset.populatedPatterns)
                             + (preset.populatedPatterns == 1 ? " pattern" : " patterns");
                if (preset.approximateTiming)
                    summary += " (timing approximated)";
                converted = { std::move (preset.projectData), preset.error, summary, preset.bpm };
            }
            else
            {
                auto drums = ksh::ui::importDrumAudio (file, originalName, drop.bpm);
                auto summary = juce::String ("Transcribed ") + juce::String (drums.bars)
                             + (drums.bars == 1 ? " bar" : " bars") + ", " + juce::String (drums.hits) + " hits";
                if (!drums.estimatedTempo)
                    summary += " (current tempo)";
                converted = { std::move (drums.projectData), drums.error, summary, drums.estimatedTempo ? std::optional<double> (drums.bpm) : std::nullopt };
            }
            juce::MessageManager::callAsync ([safe, drop, result = std::move (converted)] () mutable
            {
                if (safe != nullptr)
                    safe->completeProjectDrop (drop, std::move (result));
            });
        });
        return;
    }

    const auto type = ksh::ui::swingFileType (file);
    if (type != ksh::ui::SwingFileType::midi)
    {
        showFileDropStatus ("Drop a DrumComputer preset, MIDI file, or drum recording.");
        return;
    }

    showFileDropStatus ("Reading groove...", true);
    const auto referenceBpm = processorRef.hasStandaloneTransport() ? drop.bpm : 120.0;
    fileDropWorker->addJob ([safe, drop, file, fileType = *type, referenceBpm, staged = std::move (stagedFile)]
    {
        const auto result = ksh::ui::analyseSwingFile (file, fileType, drop.beatsPerStep, referenceBpm);
        juce::MessageManager::callAsync ([safe, drop, result]
        {
            if (safe != nullptr)
                safe->completeSwingDrop (drop, result);
        });
    });
}

void PluginEditor::completeProjectDrop (FileDrop drop, ProjectDrop converted)
{
    if (drop.request != fileDropRequest)
        return;
    if (converted.error.isNotEmpty())
    {
        showFileDropStatus (converted.error);
        return;
    }
    if (drop.document != processorRef.getUiBridge().getDocumentEpoch())
    {
        showFileDropStatus ("The project changed. Drop the file again to import it.");
        return;
    }

    showFileDropStatus ({});
    const juce::Component::SafePointer<PluginEditor> safe (this);
    confirmDiscardingChanges ([safe, drop, result = std::move (converted)]
    {
        if (safe == nullptr || drop.request != safe->fileDropRequest)
            return;
        auto& processor = safe->processorRef;
        const bool busy = safe->projectFileChooser != nullptr || safe->nativeView->isProjectBrowserOpen()
       #if JUCE_IOS
                       || safe->iosLibrary->busy()
       #endif
            ;
        if (drop.document != processor.getUiBridge().getDocumentEpoch() || safe->nativeView->controller.editing() || busy)
        {
            safe->showFileDropStatus ("Finish the current project operation and drop the file again.");
            return;
        }
        if (!processor.loadStateFromData (result.data.data(), static_cast<int> (result.data.size()), true))
        {
            safe->showFileDropStatus ("Couldn't open the converted KSH project.");
            return;
        }

        if (result.bpm && processor.hasStandaloneTransport())
            processor.setStandaloneTempoBpm (*result.bpm);
        safe->nativeView->controller.refresh();
        safe->updateNativeSize();

        auto status = result.summary;
        if (result.bpm)
            status += juce::String::fromUTF8 ("  ·  ") + juce::String (*result.bpm, 1)
                    + (processor.hasStandaloneTransport() ? " BPM" : " BPM in preset");
        safe->showFileDropStatus (status);
    });
}

void PluginEditor::completeSwingDrop (FileDrop drop, const ksh::ui::SwingImportResult& result)
{
    if (drop.request != fileDropRequest)
        return;
    if (result.error.isNotEmpty() || (! result.bpm && ! result.swing))
    {
        showFileDropStatus (result.error.isNotEmpty() ? result.error : "Couldn't find a consistent swing pattern.");
        return;
    }

    auto& controller = nativeView->controller;
    const auto current = processorRef.nativeUiState();
    const bool applyTempo = result.bpm.has_value() && processorRef.hasStandaloneTransport();
    const auto tempoChange = applyTempo ? std::abs (processorRef.getStandaloneTempoBpm() - *result.bpm) : 0.0;
    if (drop.document != current.documentEpoch || controller.editing()
        || std::abs (drop.beatsPerStep - current.playback.beatsPerStep) > 0.00001
        || (result.swing && (drop.swing != current.playback.swing || drop.target != current.playback.swingSubdivisionIndex))
        || (applyTempo && std::abs (drop.bpm - processorRef.getStandaloneTempoBpm()) > 0.01))
    {
        showFileDropStatus ("Groove controls changed. Drop the file again to apply it.");
        return;
    }

    controller.begin ("Match file groove", result.swing ? juce::String ("swing") : juce::String());
    if (result.swing)
    {
        if (result.swing->amount != current.playback.swing)
            controller.command ("swing", { result.swing->amount });
        if (result.swing->amount > 0 && result.swing->target != current.playback.swingSubdivisionIndex)
            controller.command ("swing_subdivision", { result.swing->target });
    }
    if (tempoChange > 0.01)
        controller.command ("standalone_tempo", { *result.bpm });
    controller.commit();

    juce::String status = "Swing unclear";
    if (result.swing)
    {
        const int target = static_cast<int> (ksh::swingSubdivisionValueForIndex (result.swing->target));
        if (result.swing->amount == 0)
            status = current.playback.swing == 0 ? "No swing detected in dropped file" : "Dropped file set swing to 0%";
        else
            status = "Swing matched (" + juce::String (target) + "x)";
    }
    if (result.bpm)
        status += juce::String::fromUTF8 ("  ·  ") + juce::String (*result.bpm, 1) + (applyTempo ? " BPM" : " BPM in file");
    showFileDropStatus (status);
}

void PluginEditor::showFileDropStatus (const juce::String& message, bool pending)
{
    fileDropStatus = message;
    if (pending || message.isEmpty())
        fileDropStatusTimer.stopTimer();
    else
    {
        fileDropStatusTimer.startTimer (3600);
        juce::AccessibilityHandler::postAnnouncement (message, juce::AccessibilityHandler::AnnouncementPriority::low);
    }
    repaint();
}

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
    layoutNativeView (available, juce::SystemStats::getDeviceDescription().startsWithIgnoreCase ("iPhone"));
}

void PluginEditor::layoutNativeView (juce::Rectangle<float> available, bool onPhone)
{
    const bool hostedTablet = usesHostedTabletViewport();
   #if JUCE_IOS
    constexpr bool mobile = true;
   #else
    const bool mobile = fitViewport || hostedTablet;
   #endif
    const bool compact = mobile && available.getWidth() >= available.getHeight() && available.getHeight() <= 500.0f && available.getWidth() <= 1100.0f;
    if (compact && onPhone)
    {
        available.removeFromTop (juce::jmin (10.0f, available.getHeight() * 0.05f));
        // Centre within the full viewport without crossing either edge of the safe area.
        const float centreY = getLocalBounds().toFloat().getCentreY();
        const float halfHeight = juce::jmin (centreY - available.getY(), available.getBottom() - centreY);
        if (halfHeight > 0.0f)
            available = available.withY (centreY - halfHeight).withHeight (2.0f * halfHeight);
    }
    nativeView->setCompactViewport (compact, juce::jmax (814, juce::roundToInt (available.getWidth())), juce::jmax (390, juce::roundToInt (available.getHeight())),
                                   onPhone);
    const auto& layout = nativeView->layout;
    const float fitScale = juce::jmin (available.getWidth() / layout.width(), available.getHeight() / layout.height());
    const bool centered = zenMode || zenWindow != nullptr;
    const float scale = centered ? juce::jmin (fitScale, ksh::ui::UiScale::factor (processorRef.getProjectUiScalePercent())) : fitScale;
    if (zenMode)
        nativeView->setZenScaleLimit (static_cast<int> (std::floor (fitScale / ksh::ui::UiScale::baseline * 100.0f)));
    const int percent = juce::jlimit (ksh::ui::UiScale::minimumPercent, ksh::ui::UiScale::maximumPercent, juce::roundToInt (scale / ksh::ui::UiScale::baseline * 100));
    if (!centered && !fitViewport && !compact && !hostedTablet && percent != processorRef.getProjectUiScalePercent())
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
   #elif JUCE_MAC
    return ksh::MacProjectFiles::documentsDirectory().getChildFile ("ofsound").getChildFile ("KSH");
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
    auto files = getProjectFiles (directory);
    std::sort (files.begin(), files.end(), [] (const juce::File& left, const juce::File& right) {
        return left.getFileName().compareNatural (right.getFileName(), true) < 0;
    });
    return files;
}

juce::Array<juce::File> PluginEditor::getProjectFiles (const juce::File& directory) const
{
   #if JUCE_IOS
    return iosLibrary->files (directory);
   #else
    return directory.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, false, "*.kshproject");
   #endif
}

juce::String PluginEditor::getCurrentProjectFileName() const
{
    return processorRef.getProjectFile().getFileName();
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
        object->setProperty ("projectUiScalePercent", processorRef.getProjectUiScalePercent());
    }

    return juce::var (object.release());
}

bool PluginEditor::loadProjectFile (const juce::File& file, juce::String& errorMessage)
{
    if (!file.existsAsFile())
    {
        errorMessage = "That project file no longer exists.";
        return false;
    }

    const auto size = file.getSize();
    if (size <= 0 || size > maximumProjectBytes)
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

bool PluginEditor::writeProjectFile (const juce::File& file, const juce::MemoryBlock& data,
                                     bool createOnly, juce::String& errorMessage)
{
    if (file.getParentDirectory().createDirectory().failed())
    {
        errorMessage = "The project folder could not be created.";
        return false;
    }

    juce::TemporaryFile temporaryFile (file);
    if (!temporaryFile.getFile().replaceWithData (data.getData(), data.getSize()))
    {
        errorMessage = "The project file could not be written.";
        return false;
    }
    if (createOnly)
    {
       #if JUCE_MAC || JUCE_LINUX || JUCE_BSD
        // Publish the completed file only if the destination is still unoccupied.
        if (::link (temporaryFile.getFile().getFullPathName().toRawUTF8(), file.getFullPathName().toRawUTF8()) == 0)
            return true;
       #else
        if (!file.exists() && temporaryFile.getFile().moveFileTo (file))
            return true;
       #endif
        errorMessage = file.exists() ? "That project name is already in use." : "The project file could not be created.";
        return false;
    }
    if (temporaryFile.overwriteTargetFileWithTemporary())
        return true;
    errorMessage = "The project file could not be written.";
    return false;
}

void PluginEditor::saveCurrentProject()
{
    if (nativeView->isProjectBrowserOpen())
        return;
    nativeView->controller.commit();
    nativeView->projectAction (2);
}

void PluginEditor::saveProject (std::function<void (juce::var)> complete)
{
    if (projectFileChooser != nullptr || nativeView->isProjectBrowserOpen())
        return;
    const auto file = processorRef.getProjectFile();
   #if JUCE_IOS
    const bool hasDestination = file != juce::File();
   #else
    const bool hasDestination = file.existsAsFile();
   #endif
    if (hasDestination)
        saveProjectTo (file, { processorRef.getProjectName(), processorRef.getProjectAuthor(), processorRef.getProjectDescription() },
                       false, std::move (complete));
    else
        showSaveProjectDialog ({ processorRef.getProjectName(), processorRef.getProjectDescription() }, std::move (complete));
}

void PluginEditor::saveProjectTo (const juce::File& file, const ksh::ui::ProjectBrowser::Details& details,
                                  bool createOnly, std::function<void (juce::var)> complete)
{
   #if JUCE_IOS
    if (iosLibrary->busy())
    {
        complete (projectOperationResult (false, "A project operation is already open."));
        return;
    }
    iosOperationEpoch = processorRef.getUiBridge().getDocumentEpoch();
    const auto epoch = iosOperationEpoch;
    const auto revision = processorRef.getUiBridge().getRevision();
   #endif
    const auto now = juce::Time::getCurrentTime().toISO8601 (true);
    const auto created = processorRef.getProjectCreatedAt().isEmpty() ? now : processorRef.getProjectCreatedAt();
    auto snapshot = processorRef.enginePersistenceState();
    snapshot["projectName"] = details.name.toStdString();
    snapshot["projectAuthor"] = details.author.toStdString();
    snapshot["projectDescription"] = details.description.toStdString();
    snapshot["projectCreatedAt"] = created.toStdString();
    snapshot["projectModifiedAt"] = now.toStdString();
    const auto text = snapshot.dump();
    juce::MemoryBlock data (text.data(), text.size());
   #if JUCE_IOS
    const juce::Component::SafePointer<PluginEditor> safe (this);
    iosLibrary->save (file, std::move (data), createOnly,
        [safe, epoch, revision, details, created, now, completion = std::move (complete)] (ksh::IOSProjectLibrary::Result result)
    {
        if (safe == nullptr) return;
        auto& processor = safe->processorRef;
        bool success = !result.cancelled && result.error.isEmpty();
        if (success && epoch != processor.getUiBridge().getDocumentEpoch())
        {
            result.error = "The project changed while saving.";
            success = false;
        }
        if (success)
        {
            safe->iosLibrary->acceptFile (result.file);
            processor.setProjectFile (result.file);
            if (revision == processor.getUiBridge().getRevision())
            {
                processor.setProjectMetadata (details.name, details.description, created, now, details.author);
                processor.markDocumentClean();
            }
        }
        completion (safe->projectOperationResult (success, result.error));
    });
   #else
    juce::String error;
    bool saved = writeProjectFile (file, data, createOnly, error);
   #if JUCE_MAC
    if (!saved && !createOnly)
    {
        const auto result = ksh::MacProjectFiles::replace (file, data);
        error = result.getErrorMessage();
        saved = result.wasOk();
    }
   #endif
    if (saved)
    {
        processorRef.setProjectMetadata (details.name, details.description, created, now, details.author);
        processorRef.setProjectFile (file);
        processorRef.markDocumentClean();
    }
    complete (projectOperationResult (saved, error));
   #endif
}

void PluginEditor::showSaveProjectDialog (const juce::Array<juce::var>& args,
                                          std::function<void (juce::var)> complete)
{
    if (projectFileChooser != nullptr || nativeView->isProjectBrowserOpen()) return;
    const juce::Component::SafePointer<PluginEditor> safe (this);
    const auto epoch = processorRef.getUiBridge().getDocumentEpoch();
    const auto current = processorRef.getProjectFile();
    auto name = args.size() > 0 ? args[0].toString().trim() : processorRef.getProjectName();
    if (name.isEmpty()) name = "Untitled Project";
    const auto description = args.size() > 1 ? args[1].toString() : processorRef.getProjectDescription();
    nativeView->showProjectBrowser (getDefaultProjectsDirectory(), current,
        ksh::ui::ProjectBrowser::Details { name, processorRef.getProjectAuthor(), description },
        [this] (const juce::File& folder) { return getProjectFiles (folder); },
        [safe, epoch, current, projectComplete = std::move (complete)] (const juce::File& file, const ksh::ui::ProjectBrowser::Details& details,
                                                                       ksh::ui::ProjectBrowser::Completion done)
        {
            if (safe == nullptr) return;
            if (epoch != safe->processorRef.getUiBridge().getDocumentEpoch())
            {
                done ("The project changed. Close this window and try again.");
                return;
            }
            safe->saveProjectTo (file, details, file != current,
                                 finishBrowserOperation (std::move (done), projectComplete, "The project could not be saved."));
        });
}

void PluginEditor::showLoadProjectDialog (std::function<void (juce::var)> complete)
{
    if (projectFileChooser != nullptr || nativeView->isProjectBrowserOpen()) return;
    const juce::Component::SafePointer<PluginEditor> safe (this);
    const auto epoch = processorRef.getUiBridge().getDocumentEpoch();
    const auto revision = processorRef.getUiBridge().getRevision();
   #if JUCE_IOS
    auto readDetails = [this] (const juce::File& file, auto done)
    {
        iosLibrary->readPreview (file, [completePreview = std::move (done)] (ksh::IOSProjectLibrary::Result result)
        {
            completePreview (result.error.isEmpty() ? projectBrowserDetails (result.file, result.data) : std::nullopt);
        });
    };
   #else
    auto readDetails = [] (const juce::File& file, auto done)
    {
        const auto size = file.getSize();
        juce::MemoryBlock data;
        done (size > 0 && size <= maximumProjectBytes && file.loadFileAsData (data)
                  ? projectBrowserDetails (file, data) : std::nullopt);
    };
   #endif
    nativeView->showProjectBrowser (getDefaultProjectsDirectory(), processorRef.getProjectFile(), std::nullopt,
        [this] (const juce::File& folder) { return getProjectFiles (folder); },
        [safe, epoch, revision, complete] (const juce::File& file, const ksh::ui::ProjectBrowser::Details&,
                                           ksh::ui::ProjectBrowser::Completion done)
        {
            if (safe == nullptr) return;
            if (epoch != safe->processorRef.getUiBridge().getDocumentEpoch() || revision != safe->processorRef.getUiBridge().getRevision())
            {
                done ("The project changed while browsing. Close this window and try again.");
                return;
            }
            auto finish = finishBrowserOperation (std::move (done), complete, "The project could not be opened.");
           #if JUCE_IOS
            safe->iosProjectAction (1, std::move (finish), file);
           #else
            juce::String error;
            const bool loaded = safe->loadProjectFile (file, error);
            finish (safe->projectOperationResult (loaded, error));
           #endif
        },
        [safe, complete] { if (safe != nullptr) safe->confirmDiscardingChanges ([safe, complete]
            { if (safe != nullptr) safe->showExternalProjectDialog (complete); }); }, processorRef.isDocumentModified(),
        std::move (readDetails));
}

void PluginEditor::showExternalProjectDialog (
    std::function<void (juce::var)> complete)
{
   #if JUCE_IOS
    iosProjectAction (1, std::move (complete));
   #else
    const auto currentProjectFile = processorRef.getProjectFile();
    if (projectFileChooser != nullptr || nativeView->isProjectBrowserOpen())
    {
        complete (projectOperationResult (false, "A project dialog is already open."));
        return;
    }

    auto start = currentProjectFile;
    if (!start.existsAsFile())
    {
        start = getDefaultProjectsDirectory();
        start.createDirectory();
    }
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
    if (projectFileChooser != nullptr || nativeView->isProjectBrowserOpen())
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
void PluginEditor::iosProjectAction (int action, std::function<void (juce::var)> done, juce::File target)
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
    auto snapshot = processorRef.enginePersistenceState();
    const auto now = juce::Time::getCurrentTime().toISO8601 (true);
    if (processorRef.getProjectCreatedAt().isEmpty()) snapshot["projectCreatedAt"] = now.toStdString();
    snapshot["projectModifiedAt"] = now.toStdString();
    const auto text = snapshot.dump();
    iosLibrary->share (*this, processorRef.getProjectName(), juce::MemoryBlock (text.data(), text.size()),
        [safe, epoch, complete = std::move (done)] (ksh::IOSProjectLibrary::Result result)
    {
        if (safe == nullptr) return;
        if (epoch != safe->processorRef.getUiBridge().getDocumentEpoch() && !result.cancelled && result.error.isEmpty())
            result.error = "The project changed while sharing.";
        complete (safe->projectOperationResult (!result.cancelled && result.error.isEmpty(), result.error));
    });
}
#endif
