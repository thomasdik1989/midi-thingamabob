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

- CMake 3.16+
- C++17 compiler
- OpenGL 3.3+
- GLFW (submodule)
- Dear ImGui (submodule)
- midifile (submodule)
- RtMidi (submodule)

## Building

### Clone with submodules

```bash
git clone --recursive https://github.com/your-repo/midi-editor.git
cd midi-editor
```

Or if already cloned:

```bash
git submodule update --init --recursive
```

### Build

```bash
mkdir build
cd build
cmake .. -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build .
```

### Run

```bash
./MidiEditor
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

## License

MIT License
