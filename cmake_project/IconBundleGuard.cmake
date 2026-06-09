# When packaging/icon.png changes, JUCE regenerates AppIcon.icns at configure time and Ninja
# may copy the new resource into plugin bundles without relinking. That leaves a stale ad-hoc
# signature and macOS hosts will refuse to load the VST3/AU/CLAP. Force bundle targets to
# rebuild (and run JUCE's POST_BUILD re-sign step) whenever the icon source changes.

if (NOT APPLE)
    return()
endif()

set(_ksh_icon_source "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png")

if (NOT EXISTS "${_ksh_icon_source}")
    return()
endif()

set(_ksh_icon_stamp "${CMAKE_CURRENT_BINARY_DIR}/ksh_icon_rebuild.stamp")

add_custom_command(
    OUTPUT "${_ksh_icon_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_ksh_icon_stamp}"
    DEPENDS "${_ksh_icon_source}"
    COMMENT "Icon source changed — invalidating plugin bundles"
    VERBATIM)

add_custom_target(ksh_icon_rebuild_stamp DEPENDS "${_ksh_icon_stamp}")

foreach (_ksh_bundle_target IN ITEMS KSH_VST3 KSH_AU KSH_Standalone KSH_CLAP)
    if (TARGET "${_ksh_bundle_target}")
        add_dependencies("${_ksh_bundle_target}" ksh_icon_rebuild_stamp)
        set_property(TARGET "${_ksh_bundle_target}" APPEND PROPERTY LINK_DEPENDS "${_ksh_icon_stamp}")
    endif()
endforeach()
