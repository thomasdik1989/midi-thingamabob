#pragma once

#include "../app.h"
#include "../midi/midi_player.h"
#include "touch_input.h"
#include <imgui.h>
#include <string>

// Touch-optimized track panel with large cards, swipe-to-delete, Mute/Solo buttons.
// Tapping a card opens a detail editor with instrument, channel, name, and pan controls.
class TrackPanelMobile {
public:
    TrackPanelMobile(App& app, midi::MidiPlayer& player);
    void render(float width, float height);
    void processGesture(const TouchGesture& gesture);

private:
    void renderTrackList(float width, float height);
    void renderTrackEditor(float width, float height);
    void renderTrackCard(int index, midi::Track& track, float cardWidth);

    App& app_;
    midi::MidiPlayer& player_;

    // Track editing state (-1 = showing list, >= 0 = editing that track index)
    int editingTrackIndex_ = -1;
    char editNameBuf_[128] = {};  // Buffer for track name input

    // Swipe-to-delete state
    int swipingTrackIndex_ = -1;
    float swipeOffset_ = 0.0f;        // How far the card has been swiped (negative = left)
    bool swipeDeleteRevealed_ = false; // Is the delete button fully revealed?

    // Card layout constants
    static constexpr float CARD_HEIGHT = 100.0f;
    static constexpr float CARD_MARGIN = 8.0f;
    static constexpr float CARD_PADDING = 12.0f;
    static constexpr float DELETE_BUTTON_WIDTH = 80.0f;
    static constexpr float SWIPE_THRESHOLD = -60.0f; // Pixels to reveal delete button

    // Track card Y positions (for gesture hit testing)
    struct CardBounds {
        float y;
        float height;
    };
    std::vector<CardBounds> cardBounds_;
};
