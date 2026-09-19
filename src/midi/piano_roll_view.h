#pragma once

#include "types.h"
#include <cstdint>

namespace midi {

struct Rgb8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct PianoRollView {
    float pixels_per_tick = 0.1f;
    float note_height = 12.0f;
    float drum_row_height = 18.0f;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    bool use_drum_map = false;
};

struct NoteHit {
    int note_index = -1;
    bool on_left_edge = false;
    bool on_right_edge = false;
    bool hit() const { return note_index >= 0; }
};

float tickToX(uint32_t tick, float canvas_x, const PianoRollView& view);
uint32_t xToTick(float x, float canvas_x, const PianoRollView& view);
float pitchToY(int pitch, float canvas_y, const PianoRollView& view);
int yToPitch(float y, float canvas_y, const PianoRollView& view);
float rowHeight(const PianoRollView& view);

NoteHit hitTestNote(const Track& track, float pointer_x, float pointer_y, float canvas_x, float canvas_y,
                    const PianoRollView& view, float edge_threshold, float touch_padding = 0.0f);

Rgb8 trackColorRgb(int track_index, int velocity, bool selected, bool active_track);

void focusDrumMap(float canvas_y, float canvas_height, float& scroll_y, float drum_row_height);

} // namespace midi
