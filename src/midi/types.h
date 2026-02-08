#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace midi {

struct Note {
    int pitch = 60;           // 0-127 (MIDI note number, 60 = C4)
    int velocity = 100;       // 0-127
    uint32_t start_tick = 0;  // Position in MIDI ticks
    uint32_t duration = 480;  // Length in ticks (480 = quarter note at 480 PPQ)
    bool selected = false;
    
    uint32_t endTick() const { return start_tick + duration; }
};

struct Track {
    std::string name = "Track";
    int channel = 0;          // 0-15 (MIDI channel)
    int program = 0;          // 0-127 (General MIDI instrument)
    std::vector<Note> notes;
    bool muted = false;
    bool solo = false;
    float volume = 1.0f;      // 0.0-1.0
    float pan = 0.5f;         // 0.0 (left) - 1.0 (right), 0.5 = center
    
    void sortNotes();
    void clearSelection();
    int selectedCount() const;
};

struct Project {
    std::vector<Track> tracks;
    int ticks_per_quarter = 480;  // Resolution (PPQ)
    float tempo_bpm = 120.0f;     // Beats per minute
    std::string filepath;
    bool modified = false;
    
    // Time signature
    int beats_per_bar = 4;        // Numerator (e.g., 4 in 4/4)
    int beat_unit = 4;            // Denominator (e.g., 4 in 4/4)
    
    // Loop region (0 = no loop set)
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    bool loop_enabled = false;
    
    // Time conversion helpers
    double ticksToSeconds(uint32_t ticks) const;
    uint32_t secondsToTicks(double seconds) const;
    double ticksToBeats(uint32_t ticks) const;
    uint32_t beatsToTicks(double beats) const;
    
    // Bar/beat helpers using time signature
    int ticksPerBar() const;
    int tickToBar(uint32_t tick) const;
    int tickToBeatInBar(uint32_t tick) const;
    
    // Get total duration
    uint32_t getTotalTicks() const;
    
    void clearAllSelections();
};

// Grid snap values (in fractions of a beat)
enum class GridSnap {
    None = 0,
    Whole = 1,        // Whole note
    Half = 2,         // Half note
    Quarter = 4,      // Quarter note
    Eighth = 8,       // Eighth note
    Sixteenth = 16,   // Sixteenth note
    ThirtySecond = 32 // Thirty-second note
};

// Snap a tick value to the nearest grid position
uint32_t snapToGrid(uint32_t tick, int ticks_per_quarter, GridSnap snap);

// Get the name of a MIDI note (e.g., "C4", "F#5")
std::string getNoteName(int pitch);

// Get pitch from row in piano roll (assuming row 0 is highest note)
int rowToPitch(int row, int lowest_pitch = 0);
int pitchToRow(int pitch, int lowest_pitch = 0);

} // namespace midi
