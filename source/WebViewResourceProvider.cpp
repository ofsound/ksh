#include "WebViewResourceProvider.h"

#include "BinaryData.h"
#include "KshUiBridge.h"
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

juce::WebBrowserComponent::Options WebViewResources::makeBrowserOptions (PluginProcessor& processor)
{
    using Options = juce::WebBrowserComponent::Options;

    auto options = Options{}
                       .withNativeIntegrationEnabled()
                       .withInitialisationData ("pluginName", juce::var { PRODUCT_NAME_WITHOUT_VERSION })
                       .withInitialisationData ("version", juce::var { VERSION })
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
                                                    processor.requestEditorSize ((int) args[0], (int) args[1]);
                                                    complete (juce::var { true });
                                                    return;
                                                }

                                                complete (juce::var { false });
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
