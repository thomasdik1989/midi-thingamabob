cmake_minimum_required(VERSION 3.16)
project(MidiEditor VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Silence OpenGL deprecation warnings on macOS
if(APPLE)
    add_definitions(-DGL_SILENCE_DEPRECATION)
endif()

# Find OpenGL
find_package(OpenGL REQUIRED)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

# Dear ImGui
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# midifile library
add_subdirectory(third_party/midifile)

# RtMidi
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/rtmidi)

# Main application sources
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/midi/types.cpp
    src/midi/midi_file.cpp
    src/midi/midi_player.cpp
    src/midi/audio_synth.cpp
    src/ui/main_window.cpp
    src/ui/piano_roll.cpp
    src/ui/track_panel.cpp
    src/ui/toolbar.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/midifile/include
    ${CMAKE_SOURCE_DIR}/third_party/rtmidi
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

# Copy assets if any
# file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})
