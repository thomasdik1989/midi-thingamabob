#pragma once

#include "../app.h"
#include "../midi/midi_player.h"
#include "nine_slice.h"
#include <imgui.h>

// Compact two-row mobile toolbar for the center screen.
// Row 1: Open, Save, [Play|Pause|Stop] group, current time
// Row 2: [-|BPM|+] group, Grid snap selector, Scroll/Edit toggle.
class ToolbarMobile {
public:
    ToolbarMobile(App& app, midi::MidiPlayer& player);
    void render(float displayWidth);
    float getHeight() const { return height_; }
    bool isScrollMode() const { return scrollMode_; }
    void setTheme(const NineSliceTheme* theme) { theme_ = theme; }

private:
    // Standalone themed button (all corners rounded)
    bool themedButton(const char* label, ImVec2 size, bool highlighted = false);

    // Grouped themed button at a specific position within a button group
    bool themedButton(const char* label, ImVec2 size, ButtonGroupPos pos, bool highlighted = false);

    // Non-interactive label rendered with a group-center nine-slice background
    void themedGroupLabel(const char* text, ImVec2 size);

    App& app_;
    midi::MidiPlayer& player_;
    const NineSliceTheme* theme_ = nullptr;
    float height_ = 0.0f;
    bool scrollMode_ = false;
    bool showFileInput_ = false;
    char filePathBuffer_[512] = {0};
};
