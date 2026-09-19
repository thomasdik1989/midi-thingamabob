# Android NDK build configuration for MIDI Editor Mobile.
# This is used by the Android Gradle plugin to build the native library.
# It delegates to the root CMakeLists.txt with BUILD_MOBILE=ON.

cmake_minimum_required(VERSION 3.24)

# Allow old third-party CMakeLists (midifile uses cmake_minimum_required < 3.5)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

project(MidiEditorAndroid LANGUAGES C CXX)

# Set mobile build options
set(BUILD_MOBILE ON CACHE BOOL "" FORCE)
set(MOBILE_PLATFORM "Android" CACHE STRING "" FORCE)

# Include the root CMakeLists.txt
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/midi)
