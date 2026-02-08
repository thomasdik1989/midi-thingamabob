#pragma once

#include "../app.h"
#include "../midi/midi_player.h"

class Toolbar {
public:
    Toolbar(App& app, midi::MidiPlayer& player);

    void render();

private:
    App& app_;
    midi::MidiPlayer& player_;
    int selectedMidiDevice_ = -1;
};
