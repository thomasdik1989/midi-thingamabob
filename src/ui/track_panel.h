#pragma once

#include "../app.h"
#include "../midi/midi_player.h"

class TrackPanel {
public:
    TrackPanel(App& app, midi::MidiPlayer& player);
    
    void render();
    
private:
    void renderTrackItem(int index, midi::Track& track);
    
    App& app_;
    midi::MidiPlayer& player_;
};
