#pragma once

#include "../app.h"
#include "../midi/midi_player.h"
#include <imgui.h>

// Advanced settings screen (right swipe screen).
// Card-based sections: Time Signature, Loop Region, Master Volume,
// Quantize, MIDI Output, Export.
class SettingsScreen {
public:
    SettingsScreen(App& app, midi::MidiPlayer& player);

    void render(float width, float height);

private:
    // Section renderers (each renders a card)
    void renderTimeSignature(float cardWidth);
    void renderLoopRegion(float cardWidth);
    void renderMasterVolume(float cardWidth);
    void renderQuantize(float cardWidth);
    void renderMidiOutput(float cardWidth);
    void renderExport(float cardWidth);

    // Helper: draw a card background and return inner position
    void beginCard(const char* title, float cardWidth);
    void endCard();

    App& app_;
    midi::MidiPlayer& player_;

    static constexpr float CARD_MARGIN = 8.0f;
    static constexpr float CARD_PADDING = 14.0f;
    static constexpr float BUTTON_HEIGHT = 44.0f;
};
