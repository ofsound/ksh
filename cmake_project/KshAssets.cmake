# Native fonts and fixed vector branding. Keep licenses beside the source fonts.
juce_add_binary_data(Assets SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/KshWordmark.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/GantariRegular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/GantariSemibold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/GantariExtrabold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/OFL.txt")
set_target_properties(Assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
