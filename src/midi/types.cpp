#include "types.h"
#include <algorithm>

namespace midi {

void Track::sortNotes() {
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.start_tick < b.start_tick;
    });
}

void Track::clearSelection() {
    for (auto& note : notes) {
        note.selected = false;
    }
}

int Track::selectedCount() const {
    int count = 0;
    for (const auto& note : notes) {
        if (note.selected) count++;
    }
    return count;
}

double Project::ticksToSeconds(uint32_t ticks) const {
    int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    double bpm = tempo_bpm > 0.0f ? tempo_bpm : 120.0;
    double beats = static_cast<double>(ticks) / ppq;
    return beats * 60.0 / bpm;
}

uint32_t Project::secondsToTicks(double seconds) const {
    int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    double bpm = tempo_bpm > 0.0f ? tempo_bpm : 120.0;
    double beats = seconds * bpm / 60.0;
    return static_cast<uint32_t>(beats * ppq);
}

double Project::ticksToBeats(uint32_t ticks) const {
    int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    return static_cast<double>(ticks) / ppq;
}

uint32_t Project::beatsToTicks(double beats) const {
    int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    return static_cast<uint32_t>(beats * ppq);
}

int Project::ticksPerBar() const {
    int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    int bpb = beats_per_bar > 0 ? beats_per_bar : 4;
    int bu = beat_unit > 0 ? beat_unit : 4;
    // A quarter note = ppq ticks. One beat = ppq * (4 / beat_unit) ticks.
    return ppq * 4 * bpb / bu;
}

int Project::tickToBar(uint32_t tick) const {
    int tpb = ticksPerBar();
    if (tpb <= 0) tpb = 1;
    return static_cast<int>(tick / tpb) + 1;
}

int Project::tickToBeatInBar(uint32_t tick) const {
    int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    int bu = beat_unit > 0 ? beat_unit : 4;
    int ticksPerBeat = ppq * 4 / bu;
    if (ticksPerBeat <= 0) ticksPerBeat = 1;
    int tpb = ticksPerBar();
    if (tpb <= 0) tpb = 1;
    uint32_t tickInBar = tick % tpb;
    return static_cast<int>(tickInBar / ticksPerBeat) + 1;
}

uint32_t Project::getTotalTicks() const {
    uint32_t maxTick = 0;
    for (const auto& track : tracks) {
        for (const auto& note : track.notes) {
            uint32_t end = note.endTick();
            if (end > maxTick) maxTick = end;
        }
    }
    // At minimum, return 4 bars worth of ticks
    uint32_t minTicks = static_cast<uint32_t>(ticksPerBar()) * 4;
    return std::max(maxTick, minTicks);
}

void Project::clearAllSelections() {
    for (auto& track : tracks) {
        track.clearSelection();
    }
}

uint32_t snapToGrid(uint32_t tick, int ticks_per_quarter, GridSnap snap) {
    if (snap == GridSnap::None) return tick;
    
    int gridSize = ticks_per_quarter * 4 / static_cast<int>(snap);
    return (tick / gridSize) * gridSize;
}

std::string getNoteName(int pitch) {
    static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (pitch / 12) - 1;
    int note = pitch % 12;
    return std::string(noteNames[note]) + std::to_string(octave);
}

int rowToPitch(int row, int lowest_pitch) {
    return 127 - row; // Row 0 is highest pitch (127)
}

int pitchToRow(int pitch, int lowest_pitch) {
    return 127 - pitch;
}

} // namespace midi
