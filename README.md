# MIDI Editor

A simple MIDI editor with a piano roll interface, built with C++ and Dear ImGui.

## Features

- Create and open MIDI files
- Piano roll editor for composing music
- Multi-track support with instrument switching (General MIDI)
- Dedicated drum tracks (MIDI channel 10) with a GM percussion map
- Insert Beat: table-driven grooves (battle, overworld, waltz, fanfare, fill)
- Harmony stamp while drawing, and Harmonize on a selection
- Configurable song length in bars
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

### Tracks
- **Track → Add Track**: new melody track
- **Track → Add Drum Track**: channel-10 drums; the piano roll switches to a percussion map
- **+ Add Drums** in the track panel does the same

### Insert Beat
- **Edit → Insert Beat**: pick a groove (Battle 8ths, Battle Drive, Overworld Pulse, Waltz, Fanfare March, Phrase Fill) and a length (1–64 bars, or To song end)
- Creates a drum track if none exists, then tiles the groove from the playhead
- Mobile: Settings → Insert Beat

### Harmony
Toolbar **Stamp** (default **Single**) plus **Key** / **Min** control companion notes:

| Stamp | Extra notes |
|---|---|
| Single | none |
| Octave | one octave below |
| 5th Below | perfect fifth below |
| 3rd Below | diatonic third below (follows Key) |
| 6th Below | diatonic sixth below (follows Key) |
| Triad Below | chord tones under the melody note |

- With Stamp set, drawing a note also adds the companions in one undo step
- **Edit → Harmonize** (`H`) adds companions under **selected** notes. Stamp must not be Single, or nothing is added
- Mobile: Settings → Harmony Stamp

### Song length
- Toolbar **Length: N bars** opens a popup (presets 16 / 32 / 64 / 128, or any value 1–999)
- Extends the piano-roll grid beyond the notes already placed (default 32 bars)
- Mobile: Settings → Song Length

### File
- **Ctrl + N**: New project
- **Ctrl + O**: Open MIDI file
- **Ctrl + S**: Save
- **Ctrl + Shift + S**: Save As
- **Ctrl + Z**: Undo
- **Ctrl + Y**: Redo

### Regression tests

```bash
cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -C Release --output-on-failure
```

Tests exercise timing at several frame rates, tempo maps, seeking and loop
boundaries, history after sorting/clamping/deleting tracks, MIDI round trips,
recovery, atomic-save failures, the Save/Discard/Cancel workflow, drum-groove
tiling, song length, harmony stamps, mute/solo playback filtering, resize/copy/paste,
and grid snap helpers.

Run tests with:

```bash
./build.sh test
```

## TODOS
(there are also available in github project)
- Test external midi devices (did copy from example but I need to test this with my ultranova :D)
- Improve mobile esthetic, it's purely functional now.
- Native iOS/Android file pickers (in-app browser exists on desktop and mobile preview).

## License

MIT License
