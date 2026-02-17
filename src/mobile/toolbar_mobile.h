#pragma once

#include "../app.h"
#include "../midi/midi_player.h"
#include <imgui.h>

// Compact two-row mobile toolbar for the center screen.
// This will change in the near future as its all over the place.
// Row 1: Open, Save, Play, Pause, Stop, current time
// Row 2: BPM stepper, Grid snap selector, Scroll/Edit mode toggle.
class ToolbarMobile {
public:
    ToolbarMobile(App& app, midi::MidiPlayer& player);
    void render(float displayWidth);
    float getHeight() const { return height_; }
    bool isScrollMode() const { return scrollMode_; }

private:
    App& app_;
    midi::MidiPlayer& player_;
    float height_ = 0.0f;
    bool scrollMode_ = false;
    bool showFileInput_ = false;
    char filePathBuffer_[512] = {0};
};
