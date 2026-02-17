#pragma once

#include "../app.h"
#include "../midi/midi_player.h"
#include "touch_input.h"
#include "swipe_nav.h"
#include "toolbar_mobile.h"
#include "piano_roll_mobile.h"
#include "track_panel_mobile.h"
#include "settings_screen.h"

#include <SDL.h>
#include <chrono>

class MobileApp {
public:
    MobileApp();
    ~MobileApp();
    App& getApp() { return app_; }
    void processEvent(const SDL_Event& event);
    void update(float deltaTime);
    void render(float displayWidth, float displayHeight);

private:
    App app_;
    midi::MidiPlayer midiPlayer_;
    TouchInput touchInput_;
    SwipeNav swipeNav_;
    ToolbarMobile toolbar_;
    PianoRollMobile pianoRoll_;
    TrackPanelMobile trackPanel_;
    SettingsScreen settings_;
    std::chrono::steady_clock::time_point lastFrame_;

    // Display size cache
    float displayWidth_ = 0.0f;
    float displayHeight_ = 0.0f;
};
