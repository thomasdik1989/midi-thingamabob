#pragma once

#include "../app.h"
#include "../midi/midi_player.h"

class Toolbar {
public:
    Toolbar(App& app, midi::MidiPlayer& player);

    void render();

private:
    void renderSongLengthPopup();

    App& app_;
    midi::MidiPlayer& player_;
    int selectedMidiDevice_ = -1;
    bool showSongLengthPopup_ = false;
    int songLengthDraft_ = 32;
    bool showSoundFontPopup_ = false;
    char soundFontPath_[512] = {};
};
