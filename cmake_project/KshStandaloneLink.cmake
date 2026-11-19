# Keep third-party SDKs outside the source tree and link them only into the app.
set(KSH_ABLETON_LINK_SOURCE_DIR "" CACHE PATH "Licensed Ableton Link C++ checkout, including the Asio submodule")
set(ksh_local_linkkit "")
if (CMAKE_SYSTEM_NAME STREQUAL "iOS" AND EXISTS "${PROJECT_SOURCE_DIR}/_deps/linkkit/LinkKit.xcframework/Info.plist")
    set(ksh_local_linkkit "${PROJECT_SOURCE_DIR}/_deps/linkkit")
endif()
set(KSH_LINKKIT_SDK_DIR "${ksh_local_linkkit}" CACHE PATH "Extracted Ableton LinkKit SDK (iOS)")

function(ksh_configure_standalone_link target)
    if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if (NOT KSH_LINKKIT_SDK_DIR)
            return()
        endif()
        if (CMAKE_VERSION VERSION_LESS 3.28)
            message(FATAL_ERROR "LinkKit XCFramework support requires CMake 3.28 or newer")
        endif()
        set(framework "${KSH_LINKKIT_SDK_DIR}/LinkKit.xcframework")
        if (NOT EXISTS "${framework}/Info.plist")
            message(FATAL_ERROR "LinkKit.xcframework is missing from KSH_LINKKIT_SDK_DIR")
        endif()
        target_link_libraries(${target} PRIVATE "${framework}")
        target_link_options(${target} PRIVATE "-ObjC")
        set(resources "${KSH_LINKKIT_SDK_DIR}/LinkKitResources.bundle")
        if (EXISTS "${resources}")
            target_sources(${target} PRIVATE "${resources}")
            set_source_files_properties("${resources}" PROPERTIES
                MACOSX_PACKAGE_LOCATION Resources
                XCODE_EXPLICIT_FILE_TYPE wrapper.cfbundle)
        endif()

        get_target_property(plist ${target} MACOSX_BUNDLE_INFO_PLIST)
        execute_process(COMMAND /usr/libexec/PlistBuddy -c "Add :ABLLinkStartStopSyncSupported bool true" "${plist}"
                        COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND /usr/libexec/PlistBuddy -c
                        "Set :NSLocalNetworkUsageDescription Sync with Ableton Link apps and send MIDI on your local network."
                        "${plist}" COMMAND_ERROR_IS_FATAL ANY)

        get_target_property(entitlements ${target} XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS)
        set(link_entitlements "${CMAKE_CURRENT_BINARY_DIR}/KSH_StandaloneLink.entitlements")
        configure_file("${entitlements}" "${link_entitlements}" COPYONLY)
        execute_process(COMMAND /usr/libexec/PlistBuddy -c
                        "Add :com.apple.developer.networking.multicast bool true" "${link_entitlements}"
                        COMMAND_ERROR_IS_FATAL ANY)
        set_target_properties(${target} PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${link_entitlements}")
    elseif (APPLE AND KSH_ABLETON_LINK_SOURCE_DIR)
        if (NOT EXISTS "${KSH_ABLETON_LINK_SOURCE_DIR}/AbletonLinkConfig.cmake")
            message(FATAL_ERROR "An Ableton Link checkout is required at KSH_ABLETON_LINK_SOURCE_DIR")
        endif()
        include("${KSH_ABLETON_LINK_SOURCE_DIR}/AbletonLinkConfig.cmake")
        target_link_libraries(${target} PRIVATE Ableton::Link)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/standalone/StandaloneIOSMidi.mm")
        get_target_property(plist ${target} MACOSX_BUNDLE_INFO_PLIST)
        execute_process(COMMAND /usr/libexec/PlistBuddy -c
                        "Add :NSLocalNetworkUsageDescription string Sync tempo and beat with Ableton Link apps on your local network."
                        "${plist}" COMMAND_ERROR_IS_FATAL ANY)
    else()
        return()
    endif()

    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/standalone/StandaloneLink.mm")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/standalone/StandaloneLink.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(${target} PRIVATE KSH_STANDALONE_LINK=1)
endfunction()
