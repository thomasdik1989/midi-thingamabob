#!/bin/bash

# MIDI Editor Build Script
# Usage: ./build.sh [command] [options]

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
BUILD_TYPE="Release"

# ─── Color helpers ───────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

info()  { echo -e "${CYAN}[info]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
err()   { echo -e "${RED}[err]${NC}   $*"; }

# ─── Help ────────────────────────────────────────────────────────────────────

show_help() {
    echo -e "${BOLD}MIDI Editor Build Script${NC}"
    echo ""
    echo "Usage: ./build.sh [command] [options]"
    echo ""
    echo -e "${BOLD}Desktop commands:${NC}"
    echo "  (none)              Build desktop (Release)"
    echo "  debug               Build desktop (Debug)"
    echo "  release             Build desktop (Release)"
    echo "  run [file.mid]      Build and run desktop app"
    echo "  clean               Remove all build directories"
    echo ""
    echo -e "${BOLD}Mobile commands:${NC}"
    echo "  mobile              Build mobile desktop preview (SDL2 window)"
    echo "  mobile run          Build and run mobile desktop preview"
    echo "  ios                 Build for iOS Simulator"
    echo "  ios run             Build, install, and launch on iOS Simulator"
    echo "  ios device          Build for iOS device (requires signing)"
    echo "  android             Build Android native library"
    echo "  android apk         Build Android APK (requires Android SDK)"
    echo "  android run         Build APK and launch in Android emulator"
    echo ""
    echo -e "${BOLD}Options:${NC}"
    echo "  --debug             Use Debug build type"
    echo "  --simulator NAME    iOS simulator name (default: iPhone 16 Pro)"
    echo ""
    echo -e "${BOLD}Examples:${NC}"
    echo "  ./build.sh                          # Build desktop release"
    echo "  ./build.sh run                      # Build and run desktop"
    echo "  ./build.sh run song.mid             # Run with a MIDI file"
    echo "  ./build.sh mobile run               # Preview mobile UI on desktop"
    echo "  ./build.sh ios run                  # Build & run in iOS Simulator"
    echo "  ./build.sh ios run --simulator 'iPhone 16' "
    echo "  ./build.sh android                  # Build Android native lib"
    echo "  ./build.sh android run              # Build & run in Android emulator"
    exit 0
}

# ─── Desktop build ───────────────────────────────────────────────────────────

build_desktop() {
    local build_dir="$BUILD_DIR"
    mkdir -p "$build_dir"
    cd "$build_dir"

    info "Configuring desktop ($BUILD_TYPE)..."
    cmake "$PROJECT_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    info "Building..."
    cmake --build . --parallel

    ok "Desktop build complete: $build_dir/MidiEditor"
}

run_desktop() {
    local exe="$BUILD_DIR/MidiEditor"
    if [ ! -f "$exe" ]; then
        build_desktop
    fi
    info "Running MidiEditor..."
    "$exe" "$@"
}

# ─── Mobile desktop preview ─────────────────────────────────────────────────

build_mobile_preview() {
    local build_dir="$BUILD_DIR/mobile-preview"
    mkdir -p "$build_dir"
    cd "$build_dir"

    info "Configuring mobile desktop preview ($BUILD_TYPE)..."
    cmake "$PROJECT_DIR" \
        -DBUILD_MOBILE=ON \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    info "Building..."
    cmake --build . --parallel

    ok "Mobile preview build complete: $build_dir/MidiEditorMobile"
}

run_mobile_preview() {
    local exe="$BUILD_DIR/mobile-preview/MidiEditorMobile"
    if [ ! -f "$exe" ]; then
        build_mobile_preview
    fi
    info "Running MidiEditorMobile (desktop preview)..."
    "$exe" "$@"
}

# ─── iOS build ───────────────────────────────────────────────────────────────

IOS_SIMULATOR_NAME="iPhone 16 Pro"

build_ios_simulator() {
    local build_dir="$BUILD_DIR/ios-simulator"
    mkdir -p "$build_dir"
    cd "$build_dir"

    info "Configuring for iOS Simulator..."
    cmake "$PROJECT_DIR" -G Xcode \
        -DBUILD_MOBILE=ON \
        -DMOBILE_PLATFORM=iOS \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT=iphonesimulator \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5

    info "Building for iOS Simulator (arm64)..."
    cmake --build . \
        --config "$BUILD_TYPE" \
        -- \
        -sdk iphonesimulator \
        -arch arm64 \
        CODE_SIGNING_ALLOWED=NO \
        ONLY_ACTIVE_ARCH=YES

    local app_path
    app_path=$(find "$build_dir" -name "MidiEditorMobile.app" -type d 2>/dev/null | head -1)
    if [ -n "$app_path" ]; then
        ok "iOS Simulator build complete: $app_path"
    else
        ok "iOS Simulator build complete."
    fi
}

build_ios_device() {
    local build_dir="$BUILD_DIR/ios-device"
    mkdir -p "$build_dir"
    cd "$build_dir"

    info "Configuring for iOS device..."
    cmake "$PROJECT_DIR" -G Xcode \
        -DBUILD_MOBILE=ON \
        -DMOBILE_PLATFORM=iOS \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT=iphoneos \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5

    info "Building for iOS device (arm64)..."
    warn "You may need to set signing identity in Xcode or via cmake flags."
    cmake --build . \
        --config "$BUILD_TYPE" \
        -- \
        -sdk iphoneos \
        -arch arm64

    ok "iOS device build complete."
    info "Open $build_dir/MidiEditor.xcodeproj in Xcode to configure signing and deploy."
}

find_simulator_udid() {
    local sim_name="$1"
    # Find the most recent runtime's simulator matching the name
    local udid
    udid=$(xcrun simctl list devices available -j 2>/dev/null \
        | python3 -c "
import json, sys
data = json.load(sys.stdin)
name = '$sim_name'
# Iterate runtimes in reverse (newest first)
for runtime in sorted(data.get('devices', {}).keys(), reverse=True):
    for dev in data['devices'][runtime]:
        if dev['name'] == name and dev['isAvailable']:
            print(dev['udid'])
            sys.exit(0)
sys.exit(1)
" 2>/dev/null)
    echo "$udid"
}

run_ios_simulator() {
    build_ios_simulator

    local build_dir="$BUILD_DIR/ios-simulator"
    local app_path
    app_path=$(find "$build_dir" -name "MidiEditorMobile.app" -type d 2>/dev/null | head -1)

    if [ -z "$app_path" ]; then
        err "Could not find MidiEditorMobile.app in build output."
        err "Searching in: $build_dir"
        exit 1
    fi

    info "Looking for simulator: $IOS_SIMULATOR_NAME"
    local udid
    udid=$(find_simulator_udid "$IOS_SIMULATOR_NAME")

    if [ -z "$udid" ]; then
        err "Simulator '$IOS_SIMULATOR_NAME' not found."
        echo ""
        info "Available simulators:"
        xcrun simctl list devices available 2>/dev/null | grep -E "iPhone|iPad" | head -15
        echo ""
        info "Use: ./build.sh ios run --simulator 'iPhone 16'"
        exit 1
    fi

    ok "Found simulator: $IOS_SIMULATOR_NAME ($udid)"

    info "Booting simulator..."
    xcrun simctl boot "$udid" 2>/dev/null || true

    info "Opening Simulator.app..."
    open -a Simulator --args -CurrentDeviceUDID "$udid" 2>/dev/null || true

    # Wait a moment for the simulator to be ready
    sleep 2

    info "Installing app..."
    xcrun simctl install "$udid" "$app_path"

    info "Launching app..."
    # The bundle ID comes from the Info.plist; SDL on iOS uses the CMake project name
    local bundle_id
    bundle_id=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$app_path/Info.plist" 2>/dev/null || echo "")
    if [ -z "$bundle_id" ]; then
        # Fallback: try common patterns
        bundle_id=$(defaults read "$app_path/Info" CFBundleIdentifier 2>/dev/null || echo "com.midieditor.MidiEditorMobile")
    fi

    xcrun simctl launch "$udid" "$bundle_id"
    ok "App launched in simulator!"
    info "Bundle ID: $bundle_id"
}

# ─── Android build ───────────────────────────────────────────────────────────

detect_android_ndk() {
    if [ -n "$ANDROID_NDK_HOME" ]; then
        echo "$ANDROID_NDK_HOME"
        return 0
    fi
    if [ -n "$ANDROID_HOME" ]; then
        local ndk_dir="$ANDROID_HOME/ndk"
        if [ -d "$ndk_dir" ]; then
            # Pick the latest NDK version
            local latest
            latest=$(ls -1 "$ndk_dir" 2>/dev/null | sort -V | tail -1)
            if [ -n "$latest" ]; then
                echo "$ndk_dir/$latest"
                return 0
            fi
        fi
    fi
    # Try common paths
    for p in \
        "$HOME/Library/Android/sdk/ndk/"*/ \
        "$HOME/Android/Sdk/ndk/"*/ \
        "/usr/local/lib/android/sdk/ndk/"*/; do
        if [ -d "$p" ]; then
            echo "${p%/}"
            return 0
        fi
    done
    return 1
}

build_android() {
    local ndk_home
    ndk_home=$(detect_android_ndk) || true

    if [ -z "$ndk_home" ]; then
        err "Android NDK not found."
        echo ""
        info "Set ANDROID_NDK_HOME or ANDROID_HOME environment variable."
        info "Or install via Android Studio: SDK Manager > SDK Tools > NDK."
        exit 1
    fi

    info "Using Android NDK: $ndk_home"

    local build_dir="$BUILD_DIR/android"
    mkdir -p "$build_dir"
    cd "$build_dir"

    info "Configuring for Android (arm64-v8a)..."
    cmake "$PROJECT_DIR" \
        -DBUILD_MOBILE=ON \
        -DMOBILE_PLATFORM=Android \
        -DCMAKE_TOOLCHAIN_FILE="$ndk_home/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM=android-29 \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5

    info "Building..."
    cmake --build . --target MidiEditorMobile --parallel

    ok "Android native library built."
    local so_path
    so_path=$(find "$build_dir" -name "libMidiEditorMobile.so" 2>/dev/null | head -1)
    if [ -n "$so_path" ]; then
        info "Output: $so_path"
    fi
}

build_android_apk() {
    local gradle_dir="$PROJECT_DIR/platform/android"

    if [ ! -f "$gradle_dir/build.gradle" ]; then
        err "Android Gradle project not found at: $gradle_dir"
        exit 1
    fi

    if ! command -v gradle &>/dev/null && [ ! -f "$gradle_dir/gradlew" ]; then
        err "Gradle not found. Install it or use Android Studio."
        exit 1
    fi

    cd "$gradle_dir"

    local gradle_cmd="gradle"
    if [ -f "$gradle_dir/gradlew" ]; then
        gradle_cmd="$gradle_dir/gradlew"
    fi

    info "Building Android APK..."
    "$gradle_cmd" assembleDebug

    local apk_path
    apk_path=$(find "$gradle_dir" -name "*debug*.apk" 2>/dev/null | head -1)
    if [ -n "$apk_path" ]; then
        ok "APK built: $apk_path"
    else
        ok "Build finished. Check $gradle_dir/app/build/outputs/apk/"
    fi
}

run_android_emulator() {
    # Build the APK first
    build_android_apk

    local gradle_dir="$PROJECT_DIR/platform/android"
    local apk_path
    apk_path=$(find "$gradle_dir" -name "*debug*.apk" 2>/dev/null | head -1)

    if [ -z "$apk_path" ]; then
        err "No APK found. Build may have failed."
        exit 1
    fi

    # Check for adb
    local adb_cmd="adb"
    if ! command -v adb &>/dev/null; then
        # Try common SDK locations
        for candidate in \
            "$ANDROID_HOME/platform-tools/adb" \
            "$ANDROID_SDK_ROOT/platform-tools/adb" \
            "$HOME/Library/Android/sdk/platform-tools/adb" \
            "$HOME/Android/Sdk/platform-tools/adb"; do
            if [ -x "$candidate" ]; then
                adb_cmd="$candidate"
                break
            fi
        done
        if ! command -v "$adb_cmd" &>/dev/null && [ ! -x "$adb_cmd" ]; then
            err "adb not found. Install Android SDK Platform-Tools."
            exit 1
        fi
    fi

    # Check for emulator
    local emulator_cmd="emulator"
    if ! command -v emulator &>/dev/null; then
        for candidate in \
            "$ANDROID_HOME/emulator/emulator" \
            "$ANDROID_SDK_ROOT/emulator/emulator" \
            "$HOME/Library/Android/sdk/emulator/emulator" \
            "$HOME/Android/Sdk/emulator/emulator"; do
            if [ -x "$candidate" ]; then
                emulator_cmd="$candidate"
                break
            fi
        done
    fi

    # Check if an emulator is already running
    local device
    device=$("$adb_cmd" devices 2>/dev/null | grep -E "emulator-[0-9]+" | head -1 | awk '{print $1}')

    if [ -z "$device" ]; then
        info "No running emulator found. Starting one..."

        # List available AVDs
        local avd
        avd=$("$emulator_cmd" -list-avds 2>/dev/null | head -1)

        if [ -z "$avd" ]; then
            err "No Android Virtual Devices (AVDs) found."
            echo ""
            info "Create one in Android Studio: Tools > Device Manager > Create Device"
            info "Or via command line: avdmanager create avd -n Pixel_6 -k 'system-images;android-34;google_apis;arm64-v8a'"
            exit 1
        fi

        info "Booting emulator: $avd"
        "$emulator_cmd" -avd "$avd" -no-snapshot-load &
        local emulator_pid=$!

        # Wait for emulator to boot
        info "Waiting for emulator to boot..."
        local tries=0
        while [ $tries -lt 60 ]; do
            device=$("$adb_cmd" devices 2>/dev/null | grep -E "emulator-[0-9]+" | head -1 | awk '{print $1}')
            if [ -n "$device" ]; then
                # Check if device is fully booted
                local boot_complete
                boot_complete=$("$adb_cmd" -s "$device" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')
                if [ "$boot_complete" = "1" ]; then
                    break
                fi
            fi
            sleep 2
            tries=$((tries + 1))
        done

        if [ -z "$device" ] || [ $tries -ge 60 ]; then
            err "Emulator failed to boot within 120 seconds."
            kill "$emulator_pid" 2>/dev/null
            exit 1
        fi

        ok "Emulator booted: $device"
    else
        ok "Using running emulator: $device"
    fi

    # Install the APK
    info "Installing APK..."
    "$adb_cmd" -s "$device" install -r "$apk_path"
    ok "APK installed."

    # Launch the app
    info "Launching MIDI Editor..."
    "$adb_cmd" -s "$device" shell am start -n com.midieditor.app/.MainActivity
    ok "App launched on emulator!"
}

# ─── Clean ───────────────────────────────────────────────────────────────────

clean_all() {
    info "Cleaning all build directories..."
    rm -rf "$BUILD_DIR"
    ok "Done."
}

# ─── Main ────────────────────────────────────────────────────────────────────

# Parse global options from all args
POSITIONAL=()
for arg in "$@"; do
    case "$arg" in
        --debug)
            BUILD_TYPE="Debug"
            ;;
        --simulator)
            # Next arg will be caught below
            ;;
        *)
            POSITIONAL+=("$arg")
            ;;
    esac
done

# Parse --simulator value
for i in "${!@}"; do
    if [ "${!i}" = "--simulator" ]; then
        next=$((i + 1))
        IOS_SIMULATOR_NAME="${!next}"
    fi
done
# Simpler parsing: iterate through original args
args=("$@")
for ((i=0; i<${#args[@]}; i++)); do
    if [ "${args[$i]}" = "--simulator" ] && [ $((i+1)) -lt ${#args[@]} ]; then
        IOS_SIMULATOR_NAME="${args[$((i+1))]}"
    fi
done

COMMAND="${POSITIONAL[0]:-}"
SUBCOMMAND="${POSITIONAL[1]:-}"

case "$COMMAND" in
    ""|release)
        build_desktop
        ;;
    debug)
        BUILD_TYPE="Debug"
        build_desktop
        ;;
    run)
        build_desktop
        run_desktop "${POSITIONAL[@]:1}"
        ;;
    clean)
        clean_all
        ;;
    mobile)
        case "$SUBCOMMAND" in
            run)
                build_mobile_preview
                run_mobile_preview "${POSITIONAL[@]:2}"
                ;;
            *)
                build_mobile_preview
                ;;
        esac
        ;;
    ios)
        case "$SUBCOMMAND" in
            run)
                run_ios_simulator
                ;;
            device)
                build_ios_device
                ;;
            *)
                build_ios_simulator
                ;;
        esac
        ;;
    android)
        case "$SUBCOMMAND" in
            apk)
                build_android_apk
                ;;
            run)
                run_android_emulator
                ;;
            *)
                build_android
                ;;
        esac
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        err "Unknown command: $COMMAND"
        echo ""
        show_help
        ;;
esac
