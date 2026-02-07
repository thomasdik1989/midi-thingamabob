#pragma once

#include "../app.h"
#include "toolbar.h"
#include "track_panel.h"
#include "piano_roll.h"
#include "../midi/midi_player.h"
#include <string>
#include <chrono>

class MainWindow {
public:
    MainWindow(App& app);
    ~MainWindow();
    
    void render();
    
private:
    void renderMenuBar();
    void renderDockspace();
    void handleKeyboardShortcuts();
    void handleFileDialogs();
    
    // File dialog helpers
    void showOpenDialog();
    void showSaveDialog();
    
    App& app_;
    Toolbar toolbar_;
    TrackPanel trackPanel_;
    PianoRoll pianoRoll_;
    midi::MidiPlayer midiPlayer_;
    
    // Timing
    std::chrono::steady_clock::time_point lastFrame_;
    
    // File dialog state
    bool showOpenFileDialog_ = false;
    bool showSaveFileDialog_ = false;
    std::string fileDialogPath_;
    char filePathBuffer_[512] = {0};
    
    // UI state
    bool firstFrame_ = true;
};
