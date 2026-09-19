#include "piano_roll_view.h"
#include "patterns.h"
#include <algorithm>
#include <cmath>

namespace midi {

float tickToX(uint32_t tick, float canvas_x, const PianoRollView& view) {
    return canvas_x + (tick - view.scroll_x) * view.pixels_per_tick;
}

uint32_t xToTick(float x, float canvas_x, const PianoRollView& view) {
    float tick = view.scroll_x + (x - canvas_x) / view.pixels_per_tick;
    return static_cast<uint32_t>(std::max(0.0f, tick));
}

float pitchToY(int pitch, float canvas_y, const PianoRollView& view) {
    if (view.use_drum_map) {
        return drumPitchToY(pitch, canvas_y, view.drum_row_height, view.scroll_y);
    }
    return canvas_y + (127 - pitch) * view.note_height - view.scroll_y;
}

int yToPitch(float y, float canvas_y, const PianoRollView& view) {
    if (view.use_drum_map) {
        return drumYToPitch(y, canvas_y, view.drum_row_height, view.scroll_y);
    }
    int pitch = 127 - static_cast<int>((y - canvas_y + view.scroll_y) / view.note_height);
    return std::clamp(pitch, 0, 127);
}

float rowHeight(const PianoRollView& view) {
    return view.use_drum_map ? view.drum_row_height : view.note_height;
}

NoteHit hitTestNote(const Track& track, float pointer_x, float pointer_y, float canvas_x, float canvas_y,
                    const PianoRollView& view, float edge_threshold, float touch_padding) {
    NoteHit result;
    const float row_height = rowHeight(view);

    for (int i = static_cast<int>(track.notes.size()) - 1; i >= 0; --i) {
        const auto& note = track.notes[i];

        float x1 = tickToX(note.start_tick, canvas_x, view);
        float x2 = tickToX(note.endTick(), canvas_x, view);
        float y = pitchToY(note.pitch, canvas_y, view);

        if (pointer_x < x1 - touch_padding || pointer_x > x2 + touch_padding ||
            pointer_y < y - touch_padding || pointer_y > y + row_height + touch_padding) {
            continue;
        }

        result.note_index = i;
        float note_width = x2 - x1;
        float min_width = edge_threshold * 3.0f;
        if (touch_padding > 0.0f) min_width = 40.0f;

        if (note_width > min_width) {
            float left_edge = touch_padding > 0.0f ? 20.0f : edge_threshold;
            float right_edge = touch_padding > 0.0f ? 20.0f : edge_threshold;
            result.on_left_edge = (pointer_x - x1 < left_edge);
            result.on_right_edge = (x2 - pointer_x < right_edge);
        }

        if (note.selected) return result;
    }

    return result;
}

Rgb8 trackColorRgb(int track_index, int velocity, bool selected, bool active_track) {
    if (selected) return {255, 200, 100};

    static const float hues[] = {0.6f, 0.0f, 0.3f, 0.15f, 0.45f, 0.75f, 0.9f, 0.55f};
    float hue = hues[track_index % 8];

    float saturation = active_track ? 0.7f : 0.4f;
    float value = 0.5f + (velocity / 127.0f) * 0.4f;

    if (!active_track) {
        value *= 0.6f;
        saturation *= 0.7f;
    }

    float h = hue * 6.0f;
    int i = static_cast<int>(h);
    float f = h - i;
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - saturation * f);
    float t = value * (1.0f - saturation * (1.0f - f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch (i % 6) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }

    return {
        static_cast<uint8_t>(r * 255.0f),
        static_cast<uint8_t>(g * 255.0f),
        static_cast<uint8_t>(b * 255.0f)
    };
}

void focusDrumMap(float canvas_y, float canvas_height, float& scroll_y, float drum_row_height) {
    const int focus_pitch = 39;
    float y = drumPitchToY(focus_pitch, canvas_y, drum_row_height, 0.0f);
    scroll_y = y - canvas_y - canvas_height * 0.5f + drum_row_height * 0.5f;
    float max_scroll_y = static_cast<float>(DRUM_PITCH_COUNT) * drum_row_height - canvas_height;
    scroll_y = std::clamp(scroll_y, 0.0f, std::max(0.0f, max_scroll_y));
}

} // namespace midi
