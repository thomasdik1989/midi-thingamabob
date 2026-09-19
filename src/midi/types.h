#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace midi {

struct MidiEvent {
    uint32_t tick = 0;
    std::vector<unsigned char> data;
    int sequence = 0;
    bool operator==(const MidiEvent& other) const { return tick == other.tick && data == other.data; }
};

struct Note {
    int pitch = 60;           // 0-127 (MIDI note number, 60 = C4)
    int velocity = 100;       // 0-127
    uint32_t start_tick = 0;  // Position in MIDI ticks
    uint32_t duration = 480;  // Length in ticks (480 = quarter note at 480 PPQ)
    bool selected = false;
    int channel = -1; // -1 follows the track; imported multi-channel notes retain their channel
    int release_velocity = 0;
    int start_sequence = 0;
    int end_sequence = 0;
    
    bool operator==(const Note& n) const {
        return pitch == n.pitch && velocity == n.velocity && start_tick == n.start_tick &&
               duration == n.duration && channel == n.channel && release_velocity == n.release_velocity;
    }
    uint32_t endTick() const { return duration > UINT32_MAX - start_tick ? UINT32_MAX : start_tick + duration; }
};

struct Track {
    std::string name = "Track";
    int channel = 0;          // 0-15 (MIDI channel)
    int program = 0;          // 0-127 (General MIDI instrument)
    std::vector<Note> notes;
    std::vector<MidiEvent> events; // Non-note events, including automation and metadata
    uint32_t end_tick = 0;
    int source_channel = -1;
    int source_program = 0;
    bool muted = false;
    bool solo = false;
    float volume = 1.0f;      // 0.0-1.0
    float pan = 0.5f;         // 0.0 (left) - 1.0 (right), 0.5 = center
    
    bool operator==(const Track& t) const {
        return name == t.name && channel == t.channel && program == t.program && notes == t.notes &&
               events == t.events && end_tick == t.end_tick && source_channel == t.source_channel &&
               source_program == t.source_program && muted == t.muted && solo == t.solo &&
               volume == t.volume && pan == t.pan;
    }
    void sortNotes();
    void clearSelection();
    int selectedCount() const;
};

struct Project {
    std::vector<Track> tracks;
    int ticks_per_quarter = 480;  // Resolution (PPQ)
    float source_tempo_bpm = 120.0f;
    float tempo_bpm = 120.0f;     // Beats per minute
    std::string filepath;
    bool modified = false;
    
    // Time signature
    int source_beats_per_bar = 4;
    int source_beat_unit = 4;
    int beats_per_bar = 4;        // Numerator (e.g., 4 in 4/4)
    int beat_unit = 4;            // Denominator (e.g., 4 in 4/4)
    
    // Loop region (0 = no loop set)
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    bool loop_enabled = false;

    // Editable grid extent in bars
    int length_bars = 32;
    
    bool operator==(const Project& p) const {
        return tracks == p.tracks && ticks_per_quarter == p.ticks_per_quarter && tempo_bpm == p.tempo_bpm &&
               beats_per_bar == p.beats_per_bar && beat_unit == p.beat_unit && loop_start == p.loop_start &&
               loop_end == p.loop_end && loop_enabled == p.loop_enabled && length_bars == p.length_bars;
    }

    // Time conversion helpers
    double ticksToSeconds(uint32_t ticks) const;
    uint32_t secondsToTicks(double seconds) const;
    double secondsToTickPosition(double seconds) const;
    double tickPositionToSeconds(double tick) const;
    std::vector<std::pair<uint32_t, double>> tempoMap() const;
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

// Tick spacing for one grid-snap division (0 when snap is None)
int gridSnapTicks(int ticks_per_quarter, GridSnap snap);

// Subdivision spacing for piano-roll vertical lines (snap when set, else zoom-based)
int gridSubdivisionTicks(int ticks_per_quarter, int beat_unit, GridSnap snap,
                           float pixels_per_tick, int ticks_per_bar, int ticks_per_beat);

// Get the name of a MIDI note (e.g., "C4", "F#5")
std::string getNoteName(int pitch);

// Label for piano-roll keys and notes (drum map on channel-10 tracks).
std::string getTrackPitchLabel(int pitch, bool drum_track);

// Get pitch from row in piano roll (assuming row 0 is highest note)
int rowToPitch(int row, int lowest_pitch = 0);
int pitchToRow(int pitch, int lowest_pitch = 0);

} // namespace midi
