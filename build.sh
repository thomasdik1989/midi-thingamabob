#!/bin/bash

# MIDI Editor Build Script
# Usage: ./build.sh [clean|release|debug|run]

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BUILD_TYPE="Release"

# Parse arguments
case "$1" in
    clean)
        echo "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        echo "Done."
        exit 0
        ;;
    debug)
        BUILD_TYPE="Debug"
        ;;
    release)
        BUILD_TYPE="Release"
        ;;
    run)
        if [ -f "$BUILD_DIR/MidiEditor" ]; then
            echo "Running MidiEditor..."
            "$BUILD_DIR/MidiEditor" "${@:2}"
            exit 0
        else
            echo "MidiEditor not built yet. Building first..."
        fi
        ;;
    help|--help|-h)
        echo "MIDI Editor Build Script"
        echo ""
        echo "Usage: ./build.sh [command]"
        echo ""
        echo "Commands:"
        echo "  (none)    Build the project (Release mode)"
        echo "  debug     Build in Debug mode"
        echo "  release   Build in Release mode"
        echo "  clean     Remove build directory"
        echo "  run       Run the application (build first if needed)"
        echo "  help      Show this help message"
        echo ""
        echo "Examples:"
        echo "  ./build.sh              # Build release"
        echo "  ./build.sh debug        # Build debug"
        echo "  ./build.sh run          # Build and run"
        echo "  ./build.sh run file.mid # Run with a MIDI file"
        exit 0
        ;;
esac

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring ($BUILD_TYPE)..."
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo "Building..."
cmake --build . --parallel

echo ""
echo "Build complete!"
echo "Run with: ./build/MidiEditor"
echo "Or use:   ./build.sh run"

# If 'run' was specified, run the app after building
if [ "$1" = "run" ]; then
    echo ""
    echo "Starting MidiEditor..."
    "$BUILD_DIR/MidiEditor" "${@:2}"
fi
