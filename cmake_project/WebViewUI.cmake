# Builds the Svelte/Vite frontend and packages it as assets/webview/ui.zip for BinaryData.

set(WEBVIEW_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(WEBVIEW_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/webview/dist")
set(WEBVIEW_ZIP "${CMAKE_CURRENT_SOURCE_DIR}/assets/webview/ui.zip")

file(GLOB_RECURSE WEBVIEW_UI_SOURCES CONFIGURE_DEPENDS
    "${WEBVIEW_UI_DIR}/src/*"
    "${WEBVIEW_UI_DIR}/index.html"
    "${WEBVIEW_UI_DIR}/vite.config.js"
    "${WEBVIEW_UI_DIR}/svelte.config.js"
    "${WEBVIEW_UI_DIR}/package.json")

find_program(NPM_EXECUTABLE npm)

if (NPM_EXECUTABLE)
    add_custom_command(
        OUTPUT "${WEBVIEW_ZIP}"
        COMMAND "${NPM_EXECUTABLE}" install --prefix "${WEBVIEW_UI_DIR}"
        COMMAND "${NPM_EXECUTABLE}" run build --prefix "${WEBVIEW_UI_DIR}"
        DEPENDS ${WEBVIEW_UI_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building WebView UI (Svelte/Vite) and ui.zip"
        VERBATIM)

    add_custom_target(WebViewUIBuild DEPENDS "${WEBVIEW_ZIP}")
else()
    message(WARNING "npm not found — WebView UI zip will not be rebuilt. Run 'npm run build' in ui/ manually.")
endif()
