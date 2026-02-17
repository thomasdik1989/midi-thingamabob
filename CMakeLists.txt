cmake_minimum_required(VERSION 3.16)
project(MidiEditor VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Mobile build option
option(BUILD_MOBILE "Build mobile version instead of desktop" OFF)
set(MOBILE_PLATFORM "" CACHE STRING "Mobile platform: iOS or Android")

# Silence OpenGL deprecation warnings on macOS
if(APPLE)
    add_definitions(-DGL_SILENCE_DEPRECATION)
endif()

# Dear ImGui core sources (shared between desktop and mobile)
# Use CMAKE_CURRENT_LIST_DIR so paths resolve correctly even when
# this file is included as a subdirectory (e.g. Android Gradle build).
set(IMGUI_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui)
set(IMGUI_CORE_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# midifile library
add_subdirectory(third_party/midifile)

# RtMidi
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(BUILD_MOBILE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    if(MOBILE_PLATFORM STREQUAL "Android")
        # Disable AMIDI backend on Android: it requires JNI (JNI_GetCreatedJavaVMs)
        # which is only available at runtime when loaded from a Java app, not at link time.
        # The mobile app uses the built-in audio synth by default.
        set(RTMIDI_API_AMIDI OFF CACHE BOOL "" FORCE)
    endif()
endif()
add_subdirectory(third_party/rtmidi)

# Shared application core sources (used by both desktop and mobile)
set(CORE_SOURCES
    src/app.cpp
    src/midi/types.cpp
    src/midi/midi_file.cpp
    src/midi/midi_player.cpp
    src/midi/audio_synth.cpp
)

if(BUILD_MOBILE)
    # =========================================================================
    # Mobile Build (SDL2 + ImGui + OpenGL ES)
    # =========================================================================

    # SDL2
    set(SDL2_DISABLE_SDL2MAIN OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/SDL)

    # ImGui with SDL2 + OpenGL ES backends
    set(IMGUI_MOBILE_SOURCES
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    add_library(imgui_mobile STATIC ${IMGUI_MOBILE_SOURCES})
    target_include_directories(imgui_mobile PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    target_link_libraries(imgui_mobile PUBLIC SDL2-static)

    # Only use OpenGL ES2 on actual mobile platforms, not desktop preview
    if(MOBILE_PLATFORM STREQUAL "iOS" OR MOBILE_PLATFORM STREQUAL "Android")
        target_compile_definitions(imgui_mobile PUBLIC IMGUI_IMPL_OPENGL_ES2)
    endif()

    # Mobile application sources
    set(MOBILE_SOURCES
        src/mobile/main_mobile.cpp
        src/mobile/mobile_app.cpp
        src/mobile/touch_input.cpp
        src/mobile/swipe_nav.cpp
        src/mobile/toolbar_mobile.cpp
        src/mobile/piano_roll_mobile.cpp
        src/mobile/track_panel_mobile.cpp
        src/mobile/settings_screen.cpp
        src/mobile/file_ops_mobile.cpp
    )

    set(MOBILE_TARGET MidiEditorMobile)

    if(MOBILE_PLATFORM STREQUAL "iOS")
        # iOS build
        enable_language(OBJCXX)

        # miniaudio includes Apple frameworks that require Objective-C++ on iOS
        set_source_files_properties(src/midi/audio_synth.cpp PROPERTIES
            LANGUAGE OBJCXX
            COMPILE_FLAGS "-x objective-c++ -std=c++17"
        )

        set(IOS_SOURCES
            platform/ios/AppDelegate.m
        )

        add_executable(${MOBILE_TARGET} MACOSX_BUNDLE
            ${CORE_SOURCES}
            ${MOBILE_SOURCES}
            ${IOS_SOURCES}
        )

        set_target_properties(${MOBILE_TARGET} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/platform/ios/Info.plist
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.midieditor.mobile"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
        )

        target_link_libraries(${MOBILE_TARGET} PRIVATE
            imgui_mobile
            midifile
            rtmidi
            SDL2-static
            SDL2main
            "-framework Foundation"
            "-framework UIKit"
            "-framework OpenGLES"
            "-framework CoreFoundation"
            "-framework CoreMIDI"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AVFoundation"
            "-framework GameController"
            "-framework CoreMotion"
            "-framework CoreGraphics"
            "-framework CoreBluetooth"
            "-framework Metal"
            "-framework QuartzCore"
            "-framework CoreHaptics"
        )

    elseif(MOBILE_PLATFORM STREQUAL "Android")
        # Android build (NDK shared library)
        add_library(${MOBILE_TARGET} SHARED
            ${CORE_SOURCES}
            ${MOBILE_SOURCES}
        )

        set_target_properties(${MOBILE_TARGET} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )

        target_link_libraries(${MOBILE_TARGET} PRIVATE
            imgui_mobile
            midifile
            rtmidi
            SDL2-static
            SDL2main
            GLESv2
            EGL
            android
            log
        )

    else()
        # Desktop mobile preview build (for testing on desktop with SDL2)
        find_package(OpenGL REQUIRED)

        add_executable(${MOBILE_TARGET}
            ${CORE_SOURCES}
            ${MOBILE_SOURCES}
        )

        set_target_properties(${MOBILE_TARGET} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
        )

        target_link_libraries(${MOBILE_TARGET} PRIVATE
            imgui_mobile
            midifile
            rtmidi
            SDL2-static
            SDL2main
            OpenGL::GL
        )

        if(APPLE)
            target_link_libraries(${MOBILE_TARGET} PRIVATE
                "-framework CoreFoundation"
                "-framework CoreMIDI"
                "-framework CoreAudio"
                "-framework AudioToolbox"
                "-framework AudioUnit"
            )
        elseif(UNIX)
            find_package(ALSA)
            if(ALSA_FOUND)
                target_link_libraries(${MOBILE_TARGET} PRIVATE ${ALSA_LIBRARIES})
            endif()
        endif()
    endif()

    target_include_directories(${MOBILE_TARGET} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/third_party/midifile/include
        ${CMAKE_CURRENT_LIST_DIR}/third_party/rtmidi
    )

    target_compile_definitions(${MOBILE_TARGET} PRIVATE BUILD_MOBILE=1)

else()
    # =========================================================================
    # Desktop Build (GLFW + ImGui + OpenGL) -- unchanged
    # =========================================================================

    find_package(OpenGL REQUIRED)

    # GLFW
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw)

    # ImGui with GLFW + OpenGL backends
    set(IMGUI_DESKTOP_SOURCES
        ${IMGUI_CORE_SOURCES}
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    add_library(imgui STATIC ${IMGUI_DESKTOP_SOURCES})
    target_include_directories(imgui PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    # Desktop application sources
    set(DESKTOP_SOURCES
        src/main.cpp
        ${CORE_SOURCES}
        src/ui/main_window.cpp
        src/ui/piano_roll.cpp
        src/ui/track_panel.cpp
        src/ui/toolbar.cpp
    )

    add_executable(${PROJECT_NAME} ${DESKTOP_SOURCES})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/third_party/midifile/include
        ${CMAKE_CURRENT_LIST_DIR}/third_party/rtmidi
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        imgui
        midifile
        rtmidi
        OpenGL::GL
    )

    # Platform-specific settings
    if(APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "-framework CoreFoundation"
            "-framework CoreMIDI"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AudioUnit"
        )
    elseif(UNIX)
        find_package(ALSA)
        if(ALSA_FOUND)
            target_link_libraries(${PROJECT_NAME} PRIVATE ${ALSA_LIBRARIES})
        endif()
    endif()

endif()

# Copy assets if any
# file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})
