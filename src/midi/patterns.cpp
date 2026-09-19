#include "patterns.h"
#include <algorithm>

namespace midi {

namespace {

constexpr int kKick = 36;
constexpr int kSnare = 38;
constexpr int kClosedHat = 42;
constexpr int kCrash = 49;
constexpr int kLowTom = 45;
constexpr int kMidTom = 47;
constexpr int kHighTom = 48;

static const DrumHit kBattle8ths[] = {
    {kKick, 0, 1, 110}, {kKick, 8, 1, 100},
    {kSnare, 4, 1, 105}, {kSnare, 12, 1, 100},
    {kClosedHat, 0, 1, 80}, {kClosedHat, 2, 1, 70},
    {kClosedHat, 4, 1, 80}, {kClosedHat, 6, 1, 70},
    {kClosedHat, 8, 1, 80}, {kClosedHat, 10, 1, 70},
    {kClosedHat, 12, 1, 80}, {kClosedHat, 14, 1, 70},
    {kCrash, 0, 2, 115},
};

static const DrumHit kBattleDrive[] = {
    {kKick, 0, 1, 110}, {kKick, 6, 1, 95}, {kKick, 8, 1, 105}, {kKick, 14, 1, 95},
    {kSnare, 4, 1, 105}, {kSnare, 12, 1, 100},
    {kClosedHat, 0, 1, 80}, {kClosedHat, 2, 1, 70},
    {kClosedHat, 4, 1, 80}, {kClosedHat, 6, 1, 70},
    {kClosedHat, 8, 1, 80}, {kClosedHat, 10, 1, 70},
    {kClosedHat, 12, 1, 80}, {kClosedHat, 14, 1, 70},
};

static const DrumHit kOverworldPulse[] = {
    {kKick, 0, 1, 100},
    {kSnare, 8, 1, 95},
    {kClosedHat, 0, 1, 75}, {kClosedHat, 4, 1, 70},
    {kClosedHat, 8, 1, 75}, {kClosedHat, 12, 1, 70},
};

static const DrumHit kWaltz[] = {
    {kKick, 0, 1, 100},
    {kSnare, 4, 1, 90}, {kSnare, 8, 1, 85},
};

static const DrumHit kFanfareMarch[] = {
    {kKick, 0, 1, 110},
    {kSnare, 4, 1, 105}, {kSnare, 12, 1, 100},
    {kClosedHat, 0, 1, 80}, {kClosedHat, 4, 1, 75},
    {kClosedHat, 8, 1, 80}, {kClosedHat, 12, 1, 75},
    {kCrash, 0, 2, 120},
};

static const DrumHit kPhraseFill[] = {
    {kLowTom, 0, 1, 100}, {kMidTom, 4, 1, 95},
    {kHighTom, 8, 1, 90}, {kMidTom, 12, 1, 95},
    {kLowTom, 14, 1, 100}, {kCrash, 15, 4, 120},
};

static const DrumGroove kGrooves[] = {
    {"Battle 8ths", 4, kBattle8ths, static_cast<int>(sizeof(kBattle8ths) / sizeof(kBattle8ths[0]))},
    {"Battle Drive", 4, kBattleDrive, static_cast<int>(sizeof(kBattleDrive) / sizeof(kBattleDrive[0]))},
    {"Overworld Pulse", 4, kOverworldPulse, static_cast<int>(sizeof(kOverworldPulse) / sizeof(kOverworldPulse[0]))},
    {"Waltz", 3, kWaltz, static_cast<int>(sizeof(kWaltz) / sizeof(kWaltz[0]))},
    {"Fanfare March", 4, kFanfareMarch, static_cast<int>(sizeof(kFanfareMarch) / sizeof(kFanfareMarch[0]))},
    {"Phrase Fill", 4, kPhraseFill, static_cast<int>(sizeof(kPhraseFill) / sizeof(kPhraseFill[0]))},
};

} // namespace

bool isDrumTrack(const Track& track) {
    return track.channel == 9;
}

bool isDrumPitch(int pitch) {
    return pitch >= DRUM_PITCH_MIN && pitch <= DRUM_PITCH_MAX;
}

int drumPitchFromRowTop(int row) {
    return DRUM_PITCH_MAX - std::clamp(row, 0, DRUM_PITCH_COUNT - 1);
}

int drumRowFromTop(int pitch) {
    return DRUM_PITCH_MAX - std::clamp(pitch, DRUM_PITCH_MIN, DRUM_PITCH_MAX);
}

float drumPitchToY(int pitch, float canvas_y, float row_height, float scroll_y) {
    int row = drumRowFromTop(pitch);
    return canvas_y + static_cast<float>(row) * row_height - scroll_y;
}

int drumYToPitch(float y, float canvas_y, float row_height, float scroll_y) {
    if (row_height <= 0.0f) return DRUM_PITCH_MIN;
    int row = static_cast<int>((y - canvas_y + scroll_y) / row_height);
    return drumPitchFromRowTop(row);
}

int drumTrackIndex(const Project& project) {
    for (int i = 0; i < static_cast<int>(project.tracks.size()); ++i) {
        if (isDrumTrack(project.tracks[i])) return i;
    }
    return -1;
}

const DrumGroove& getDrumGroove(int index) {
    if (index < 0 || index >= static_cast<int>(sizeof(kGrooves) / sizeof(kGrooves[0]))) {
        return kGrooves[0];
    }
    return kGrooves[index];
}

int drumGrooveCount() {
    return static_cast<int>(sizeof(kGrooves) / sizeof(kGrooves[0]));
}

std::vector<Note> expandDrumGroove(const DrumGroove& groove, uint32_t start_tick,
                                   int bar_count, int ppq) {
    std::vector<Note> notes;
    if (bar_count <= 0 || !groove.hits || groove.hit_count <= 0) return notes;

    const int sixteenths_per_bar = groove.beats_per_bar * 4;
    const uint32_t sixteenth_ticks = static_cast<uint32_t>(ppq / 4);
    const uint32_t bar_ticks = sixteenth_ticks * static_cast<uint32_t>(sixteenths_per_bar);

    for (int bar = 0; bar < bar_count; ++bar) {
        for (int i = 0; i < groove.hit_count; ++i) {
            const DrumHit& hit = groove.hits[i];
            if (hit.sixteenth >= sixteenths_per_bar) continue;

            // Crash and other bar-1-only accents: skip after the first bar.
            if (bar > 0 && hit.pitch == kCrash && hit.sixteenth == 0) continue;

            Note note;
            note.pitch = hit.pitch;
            note.velocity = hit.velocity;
            note.start_tick = start_tick + static_cast<uint32_t>(bar) * bar_ticks +
                              static_cast<uint32_t>(hit.sixteenth) * sixteenth_ticks;
            note.duration = std::max(sixteenth_ticks, static_cast<uint32_t>(hit.duration_sixteenths) * sixteenth_ticks);
            note.channel = -1;
            notes.push_back(note);
        }
    }

    return notes;
}

} // namespace midi
