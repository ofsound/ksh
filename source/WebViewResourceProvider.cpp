#include "WebViewResourceProvider.h"

#include "BinaryData.h"
#include "KshUiBridge.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstring>
#include <memory>

#if JUCE_WEB_BROWSER

namespace
{
std::unique_ptr<juce::ZipFile> uiZip;

juce::ZipFile* getUiZip()
{
    if (BinaryData::ui_zipSize == 0)
        return nullptr;

    if (uiZip == nullptr)
    {
        auto stream = std::make_unique<juce::MemoryInputStream> (BinaryData::ui_zip,
                                                                 BinaryData::ui_zipSize,
                                                                 false);
        uiZip = std::make_unique<juce::ZipFile> (std::move (stream));
    }

    return uiZip.get();
}

juce::String normalisePath (juce::String path)
{
    if (path.isEmpty() || path == "/")
        return "index.html";

    if (path.startsWithChar ('/'))
        return path.substring (1);

    return path;
}

juce::String mimeTypeForPath (const juce::String& path)
{
    if (path.endsWithIgnoreCase (".html"))
        return "text/html";

    if (path.endsWithIgnoreCase (".js"))
        return "text/javascript";

    if (path.endsWithIgnoreCase (".css"))
        return "text/css";

    if (path.endsWithIgnoreCase (".svg"))
        return "image/svg+xml";

    if (path.endsWithIgnoreCase (".png"))
        return "image/png";

    if (path.endsWithIgnoreCase (".woff2"))
        return "font/woff2";

    return "application/octet-stream";
}
} // namespace

std::optional<juce::WebBrowserComponent::Resource> WebViewResources::getResource (const juce::String& url)
{
    auto* zip = getUiZip();

    if (zip == nullptr)
        return std::nullopt;

    const auto path = normalisePath (url);
    const auto entryIndex = zip->getIndexOfFileName (path);

    if (entryIndex < 0)
        return std::nullopt;

    std::unique_ptr<juce::InputStream> stream (zip->createStreamForEntry (entryIndex));

    if (stream == nullptr)
        return std::nullopt;

    juce::MemoryBlock data;
    stream->readIntoMemoryBlock (data);

    juce::WebBrowserComponent::Resource resource;
    resource.mimeType = mimeTypeForPath (path);
    resource.data.resize (data.getSize());
    std::memcpy (resource.data.data(), data.getData(), data.getSize());

    return resource;
}

namespace
{
int varToInt (const juce::var& value)
{
    if (value.isBool())
        return static_cast<bool> (value) ? 1 : 0;

    if (value.isInt())
        return static_cast<int> (value);

    if (value.isInt64())
        return static_cast<int> (value);

    if (value.isDouble())
        return static_cast<int> (static_cast<double> (value));

    return static_cast<int> (value);
}

juce::var createProjectStateVar (PluginProcessor& processor, const PluginEditor& editor)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("projectName", processor.getProjectName());
    object->setProperty ("projectDescription", processor.getProjectDescription());
    object->setProperty ("projectCreatedAt", processor.getProjectCreatedAt());
    object->setProperty ("projectModifiedAt", processor.getProjectModifiedAt());
    object->setProperty ("projectFileName", editor.getCurrentProjectFileName());
    object->setProperty ("hasPreviousProject", editor.hasPreviousProject() ? 1 : 0);
    object->setProperty ("hasNextProject", editor.hasNextProject() ? 1 : 0);
    object->setProperty ("projectUiScalePercent", processor.getProjectUiScalePercent());
    return juce::var (object.release());
}
} // namespace

juce::WebBrowserComponent::Options WebViewResources::makeBrowserOptions (PluginProcessor& processor,
                                                                         PluginEditor& editor)
{
    using Options = juce::WebBrowserComponent::Options;

    auto options = Options{}
                       .withNativeIntegrationEnabled()
                       .withInitialisationData ("pluginName", juce::var { PRODUCT_NAME_WITHOUT_VERSION })
                       .withInitialisationData ("version", juce::var { VERSION })
                       .withInitialisationData ("projectName", processor.getProjectName())
                       .withInitialisationData ("projectDescription", processor.getProjectDescription())
                       .withInitialisationData ("projectCreatedAt", processor.getProjectCreatedAt())
                       .withInitialisationData ("projectModifiedAt", processor.getProjectModifiedAt())
                       .withInitialisationData ("projectFileName", editor.getCurrentProjectFileName())
                       .withInitialisationData ("hasPreviousProject", editor.hasPreviousProject() ? 1 : 0)
                       .withInitialisationData ("hasNextProject", editor.hasNextProject() ? 1 : 0)
                       .withInitialisationData ("projectUiScalePercent", processor.getProjectUiScalePercent())
                       .withNativeFunction ("kshSendCommand",
                                            [&processor] (const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                if (args.isEmpty() || ! args[0].isString())
                                                {
                                                    complete (juce::var { false });
                                                    return;
                                                }

                                                complete (juce::var {
                                                    processor.getUiBridge().handleCommand (args[0].toString())
                                                });
                                            })
                       .withNativeFunction ("kshSetViewSize",
                                            [&processor] (const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                if (args.size() >= 2)
                                                {
                                                    if (args.size() >= 3)
                                                        processor.setPatternViewScale ((double) args[2]);

                                                    processor.requestEditorSize ((int) args[0], (int) args[1]);
                                                    complete (juce::var { true });
                                                    return;
                                                }

                                                complete (juce::var { false });
                                            })
                       .withNativeFunction ("setEditorScaleMinimum",
                                            [&editor] (const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                if (args.size() < 2)
                                                {
                                                    complete (juce::var { false });
                                                    return;
                                                }

                                                complete (editor.handleEditorScaleMinimumRequest (
                                                    varToInt (args[0]),
                                                    varToInt (args[1])));
                                            })
                       .withNativeFunction ("setProjectUiScalePercent",
                                            [&processor] (const juce::Array<juce::var>& args,
                                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                processor.setProjectUiScalePercent (
                                                    args.size() > 0 ? varToInt (args[0])
                                                                    : processor.getProjectUiScalePercent());
                                                complete (processor.getProjectUiScalePercent());
                                            })
                       .withNativeFunction ("kshGetProjectState",
                                            [&processor, &editor] (const juce::Array<juce::var>&,
                                                                   juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                complete (createProjectStateVar (processor, editor));
                                            })
                       .withNativeFunction ("kshNewProject",
                                            [&editor] (const juce::Array<juce::var>&,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                editor.createNewProject (std::move (complete));
                                            })
                       .withNativeFunction ("kshSaveProject",
                                            [&editor] (const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                editor.showSaveProjectDialog (args, std::move (complete));
                                            })
                       .withNativeFunction ("kshLoadProject",
                                            [&editor] (const juce::Array<juce::var>&,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                editor.showLoadProjectDialog (std::move (complete));
                                            })
                       .withNativeFunction ("kshCycleProject",
                                            [&editor] (const juce::Array<juce::var>& args,
                                                       juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                            {
                                                editor.cycleProject (args.size() > 0 ? varToInt (args[0]) : 1,
                                                                     std::move (complete));
                                            })
                       .withUserScript (juce::String { R"(
                           document.documentElement.classList.add('juce-ready');
                       )" });

#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
    options = options.withResourceProvider (
        [] (const juce::String& url) { return getResource (url); },
        juce::URL { "http://localhost:5173" }.getOrigin());
#endif

#if JUCE_WINDOWS
    options = options.withBackend (Options::Backend::webview2)
                     .withWinWebView2Options (
                         Options::WinWebView2{}
                             .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)));
#endif

    return options;
}

#endif
