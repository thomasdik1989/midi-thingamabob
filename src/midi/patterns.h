#pragma once

#include "types.h"
#include <vector>

namespace midi {

struct DrumHit {
    int pitch = 36;
    int sixteenth = 0;
    int duration_sixteenths = 1;
    int velocity = 100;
};

struct DrumGroove {
    const char* name = "";
    int beats_per_bar = 4;
    const DrumHit* hits = nullptr;
    int hit_count = 0;
};

constexpr int DRUM_PITCH_MIN = 35;
constexpr int DRUM_PITCH_MAX = 81;
constexpr int DRUM_PITCH_COUNT = DRUM_PITCH_MAX - DRUM_PITCH_MIN + 1;

bool isDrumTrack(const Track& track);
bool isDrumPitch(int pitch);
int drumPitchFromRowTop(int row);
int drumRowFromTop(int pitch);
int drumYToPitch(float y, float canvas_y, float row_height, float scroll_y);
float drumPitchToY(int pitch, float canvas_y, float row_height, float scroll_y);
int drumTrackIndex(const Project& project);

const DrumGroove& getDrumGroove(int index);
int drumGrooveCount();

std::vector<Note> expandDrumGroove(const DrumGroove& groove, uint32_t start_tick,
                                   int bar_count, int ppq);

} // namespace midi
