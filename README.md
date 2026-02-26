# MIDI Editor

A simple MIDI editor with a piano roll interface, built with C++ and Dear ImGui.

## Features

- Create and open MIDI files
- Piano roll editor for composing music
- Multi-track support with instrument switching (General MIDI)
- Real-time MIDI playback
- Note editing (create, move, resize, delete)
- Track mute/solo

## Dependencies

### All platforms

- CMake 3.16+
- C++17 compiler
- Git submodules (Dear ImGui, midifile, RtMidi)

### Desktop

- OpenGL 3.3+
- GLFW (submodule)

### Mobile (iOS, Android, desktop preview)

- SDL2 (submodule)

### iOS

- Xcode (with iOS SDK and Simulator runtimes)

### Android

- Android SDK with:
  - SDK Platform 34
  - Build-Tools 34
  - NDK 29.0.14033849
  - CMake 3.31.6 (`sdkmanager "cmake;3.31.6"`)
- An Android Virtual Device (AVD) for emulator testing

## Building

### Clone with submodules

```bash
git clone --recursive git@github.com:thomasdik1989/midi-thingamabob.git
cd midi-editor
```

Or if already cloned:

```bash
git submodule update --init --recursive
```

### Quick Build (Recommended)

```bash
./build.sh          # Build release version
./build.sh run      # Build and run
./build.sh debug    # Build debug version
./build.sh clean    # Clean build directory
```

### Mobile Builds

```bash
./build.sh mobile run       # Preview mobile UI on desktop
./build.sh ios run          # Build & run in iOS Simulator
./build.sh android run      # Build & run in Android emulator
```

#### iOS Simulator: "iphonesimulator is not an iOS SDK"

This error means your active developer tools are pointing to the standalone
Command Line Tools instead of Xcode. The Command Line Tools don't ship with
iOS SDKs. Fix it by switching to the full Xcode installation:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

### Manual Build

```bash
mkdir build
cd build
cmake .. -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build .
```

### Run

```bash
./build/MidiEditor
# Or with a MIDI file:
./build.sh run /path/to/file.mid
```

## Controls

### Piano Roll
- **Left Click + Drag**: Create note
- **Left Click on note**: Select note
- **Ctrl + Left Click**: Add to selection
- **Drag selected notes**: Move notes
- **Drag note edges**: Resize notes
- **Delete/Backspace**: Delete selected notes
- **Scroll wheel**: Vertical scroll (pitch)
- **Shift + Scroll**: Horizontal scroll (time)
- **Ctrl + Scroll**: Zoom

### Transport
- **Space**: Play/Pause
- **Enter**: Stop (return to start)

### File
- **Ctrl + N**: New project
- **Ctrl + O**: Open MIDI file
- **Ctrl + S**: Save
- **Ctrl + Shift + S**: Save As
- **Ctrl + Z**: Undo
- **Ctrl + Y**: Redo

## TODOS
(there are also available in github project)
- Allow for other soundfonts.
- Remove std; we should be able to make it work without.
- Add tests for each of the functionalities so we don't break things.
- Add more advanced UI for selecting and saving files.
- Test external midi devices (did copy from example but I need to test this with my ultranova :D)
- Make mobile UI rotate when the phone rotates.
- Improve mobile esthetic, it's purely functional now.
- Fix file browser + add file browser on desktop.

## License

MIT License
