# HEADS UP: Pamplejuce assumes anything you stick in the assets folder you want included in your binary.
file(GLOB_RECURSE AssetFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
list(FILTER AssetFiles EXCLUDE REGEX "/\\.DS_Store$")
list(FILTER AssetFiles EXCLUDE REGEX "/webview/dist/")

juce_add_binary_data(Assets SOURCES ${AssetFiles})

# Required for Linux happiness:
# See https://forum.juce.com/t/loading-pytorch-model-using-binarydata/39997/2
set_target_properties(Assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
