---
name: MIDI Editor Application
overview: Build a C++ MIDI editor with Dear ImGui featuring piano roll composition, multi-track support with instrument switching, and MIDI file I/O.
todos:
  - id: setup-cmake
    content: Create CMakeLists.txt with ImGui, GLFW, OpenGL, midifile, and RtMidi dependencies
    status: pending
  - id: setup-main
    content: Create main.cpp with GLFW window initialization and ImGui context setup
    status: pending
  - id: core-types
    content: Define Note, Track, and Project data structures in types.h
    status: pending
  - id: core-midi-file
    content: Implement MIDI file loading and saving using midifile library
    status: pending
  - id: core-gm-instruments
    content: Create General MIDI instrument name lookup table
    status: pending
  - id: ui-main-window
    content: Create main window layout with dockable panels
    status: pending
  - id: ui-track-panel
    content: Implement track list panel with add/remove, instrument selector, mute/solo
    status: pending
  - id: ui-piano-roll-grid
    content: Draw piano roll grid with time/pitch axes and keyboard display
    status: pending
  - id: ui-piano-roll-notes
    content: Render notes as rectangles with zoom/scroll and grid snapping
    status: pending
  - id: edit-create-notes
    content: Implement note creation by clicking and dragging
    status: pending
  - id: edit-select-move
    content: Implement note selection (single, box) and moving
    status: pending
  - id: edit-resize-delete
    content: Implement note resizing and deletion
    status: pending
  - id: edit-velocity
    content: Add velocity editing with color intensity visualization
    status: pending
  - id: playback-rtmidi
    content: Initialize RtMidi and implement device selection
    status: pending
  - id: playback-transport
    content: Implement play/pause/stop with playback cursor
    status: pending
  - id: polish-undo
    content: Add undo/redo system with command pattern
    status: pending
  - id: polish-shortcuts
    content: Add keyboard shortcuts and file menu operations
    status: pending
isProject: false
---

# MIDI Editor Application

## Architecture Overview

```mermaid
graph TB
    subgraph ui [UI Layer - Dear ImGui]
        MainWindow[Main Window]
        PianoRoll[Piano Roll Editor]
        TrackList[Track List Panel]
        Toolbar[Transport Controls]
    end

    subgraph core [Core Layer]
        App[Application State]
        MidiFile[MIDI File Handler]
        Player[MIDI Player]
    end

    subgraph external [External Libraries]
        ImGui[Dear ImGui]
        GLFW[GLFW + OpenGL]
        MidiFileLib[midifile library]
        RtMidi[RtMidi]
    end

    MainWindow --> PianoRoll
    MainWindow --> TrackList
    MainWindow --> Toolbar

    PianoRoll --> App
    TrackList --> App
    Toolbar --> Player

    App --> MidiFile
    MidiFile --> MidiFileLib
    Player --> RtMidi

    MainWindow --> ImGui
    ImGui --> GLFW
```



## Project Structure

```
midi/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp                  # Entry point, GLFW/ImGui setup
│   ├── app.h/cpp                 # Application state, project management
│   ├── midi/
│   │   ├── types.h               # Note, Track, Project structs
│   │   ├── midi_file.h/cpp       # Load/save MIDI files
│   │   ├── midi_player.h/cpp     # Playback with RtMidi
│   │   └── general_midi.h        # GM instrument names (128)
│   └── ui/
│       ├── main_window.h/cpp     # Main ImGui window layout
│       ├── piano_roll.h/cpp      # Piano roll grid widget
│       ├── track_panel.h/cpp     # Track list, instrument selector
│       └── toolbar.h/cpp         # Play/stop, tempo, file menu
└── third_party/
    ├── imgui/                    # Dear ImGui (submodule)
    ├── midifile/                 # MIDI file I/O (submodule)
    └── rtmidi/                   # MIDI playback (submodule)
```

## Core Data Structures

```cpp
struct Note {
    int pitch;           // 0-127
    int velocity;        // 0-127
    uint32_t start_tick;
    uint32_t duration;
    bool selected = false;
};

struct Track {
    std::string name;
    int channel;         // 0-15
    int program;         // 0-127 (GM instrument)
    std::vector<Note> notes;
    bool muted = false;
    bool solo = false;
};

struct Project {
    std::vector<Track> tracks;
    int ticks_per_quarter = 480;
    float tempo_bpm = 120.0f;
    std::string filepath;
};
```

## Dependencies

- **Dear ImGui**: UI framework (git submodule)
- **GLFW**: Window/input handling (system package or submodule)
- **OpenGL**: Rendering backend
- **midifile**: MIDI file parsing (git submodule from github.com/craigsapp/midifile)
- **RtMidi**: Real-time MIDI output (git submodule from github.com/thestk/rtmidi)

## Implementation Phases

### Phase 1: Project Setup

- Create CMakeLists.txt with all dependencies
- Set up third_party submodules (ImGui, midifile, RtMidi)
- Create main.cpp with GLFW window and ImGui context
- Verify basic ImGui demo window renders

### Phase 2: Core Data Model

- Define Note, Track, Project structures in `types.h`
- Implement MIDI file loading using midifile library
- Implement MIDI file saving
- Create General MIDI instrument name lookup

### Phase 3: Basic UI Layout

- Create main window with dockable panels
- Implement track list panel (add/remove tracks)
- Add instrument dropdown selector (GM instruments)
- Add track mute/solo buttons

### Phase 4: Piano Roll Editor

- Draw grid with time (X) and pitch (Y) axes
- Render piano keyboard on left edge
- Display notes as colored rectangles
- Implement zoom and scroll controls
- Add grid snapping (1/4, 1/8, 1/16 notes)

### Phase 5: Note Editing

- Click to create notes
- Drag to move notes
- Resize notes by dragging edges
- Box selection for multiple notes
- Delete selected notes
- Velocity editing (note color intensity)

### Phase 6: Playback

- Initialize RtMidi output
- Implement play/pause/stop transport
- Draw playback cursor on piano roll
- Send note-on/note-off messages in real-time
- MIDI device selection dropdown

### Phase 7: Polish

- Undo/redo with command pattern
- Copy/paste notes
- Keyboard shortcuts
- File menu (New, Open, Save, Save As)
- Quantize notes to grid

