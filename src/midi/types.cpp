#include "types.h"
#include "general_midi.h"
#include <algorithm>

namespace midi {

void Track::sortNotes() {
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
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

std::vector<std::pair<uint32_t, double>> Project::tempoMap() const {
    std::vector<std::pair<uint32_t, double>> result{{0, tempo_bpm > 0 ? tempo_bpm : 120.0}};
    for (const auto& track : tracks) {
        for (const auto& event : track.events) {
            const auto& d = event.data;
            if (event.tick > 0 && d.size() == 6 && d[0] == 0xff && d[1] == 0x51 && d[2] == 3) {
                int us = (int(d[3]) << 16) | (int(d[4]) << 8) | d[5];
                if (us > 0) result.emplace_back(event.tick, 60000000.0 / us);
            }
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return result;
}

double Project::tickPositionToSeconds(double tick) const {
    const auto map = tempoMap();
    double seconds = 0;
    double previous = 0;
    double bpm = map.front().second;
    const int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    for (size_t i = 1; i < map.size() && map[i].first <= tick; ++i) {
        seconds += (map[i].first - previous) * 60.0 / (bpm * ppq);
        previous = map[i].first;
        bpm = map[i].second;
    }
    return seconds + (std::max(0.0, tick) - previous) * 60.0 / (bpm * ppq);
}

double Project::ticksToSeconds(uint32_t ticks) const { return tickPositionToSeconds(ticks); }

double Project::secondsToTickPosition(double seconds) const {
    const auto map = tempoMap();
    double previous = 0;
    double bpm = map.front().second;
    const int ppq = ticks_per_quarter > 0 ? ticks_per_quarter : 480;
    seconds = std::max(0.0, seconds);
    for (size_t i = 1; i < map.size(); ++i) {
        double duration = (map[i].first - previous) * 60.0 / (bpm * ppq);
        if (seconds < duration) break;
        seconds -= duration;
        previous = map[i].first;
        bpm = map[i].second;
    }
    return std::min(double(UINT32_MAX), previous + seconds * bpm * ppq / 60.0);
}

uint32_t Project::secondsToTicks(double seconds) const {
    return static_cast<uint32_t>(secondsToTickPosition(seconds) + 1e-8);
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
        maxTick = std::max(maxTick, track.end_tick);
        for (const auto& event : track.events) maxTick = std::max(maxTick, event.tick);
        for (const auto& note : track.notes) {
            uint32_t end = note.endTick();
            if (end > maxTick) maxTick = end;
        }
    }
    int bars = length_bars > 0 ? length_bars : 32;
    uint32_t lengthTicks = static_cast<uint32_t>(ticksPerBar()) * static_cast<uint32_t>(bars);
    return std::max(maxTick, lengthTicks);
}

void Project::clearAllSelections() {
    for (auto& track : tracks) {
        track.clearSelection();
    }
}

uint32_t snapToGrid(uint32_t tick, int ticks_per_quarter, GridSnap snap) {
    if (snap == GridSnap::None) return tick;
    
    int gridSize = std::max(1, ticks_per_quarter * 4 / static_cast<int>(snap));
    return (tick / gridSize) * gridSize;
}

std::string getNoteName(int pitch) {
    static const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (pitch / 12) - 1;
    int note = pitch % 12;
    return std::string(noteNames[note]) + std::to_string(octave);
}

std::string getTrackPitchLabel(int pitch, bool drum_track) {
    if (drum_track) {
        auto name = getPercussionName(pitch);
        if (name != "Unknown") return std::string(name);
    }
    return getNoteName(pitch);
}

int rowToPitch(int row, int lowest_pitch) {
    return 127 - row; // Row 0 is highest pitch (127)
}

int pitchToRow(int pitch, int lowest_pitch) {
    return 127 - pitch;
}

} // namespace midi
