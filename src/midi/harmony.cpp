#include "harmony.h"
#include <algorithm>
#include <cmath>

namespace midi {

namespace {

constexpr int kMajorScale[] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kMinorScale[] = {0, 2, 3, 5, 7, 8, 10};

const char* kHarmonyNames[] = {
    "Single",
    "Octave",
    "5th Below",
    "3rd Below",
    "6th Below",
    "Triad Below"
};

int clampPitch(int pitch) {
    return std::clamp(pitch, 0, 127);
}

const int* scaleFor(bool minor) {
    return minor ? kMinorScale : kMajorScale;
}

int nearestScaleDegree(int pitch_class, int tonic, bool minor) {
    const int* scale = scaleFor(minor);
    int best_degree = 0;
    int best_distance = 128;
    for (int degree = 0; degree < 7; ++degree) {
        int scale_pc = (tonic + scale[degree]) % 12;
        int distance = std::abs(pitch_class - scale_pc);
        distance = std::min(distance, 12 - distance);
        if (distance < best_distance) {
            best_distance = distance;
            best_degree = degree;
        }
    }
    return best_degree;
}

int pitchFromScaleDegree(int melody_pitch, int degree, int tonic, bool minor) {
    const int* scale = scaleFor(minor);
    while (degree < 0) degree += 7;
    degree %= 7;

    int melody_pc = ((melody_pitch % 12) + 12) % 12;
    int target_pc = (tonic + scale[degree]) % 12;
    int octave = melody_pitch / 12;
    int pitch = octave * 12 + target_pc;
    if (pitch > melody_pitch || (pitch == melody_pitch && degree != nearestScaleDegree(melody_pc, tonic, minor))) {
        pitch -= 12;
    }
    while (pitch >= melody_pitch) pitch -= 12;
    return clampPitch(pitch);
}

Note makeCompanion(const Note& melody, int pitch) {
    Note note = melody;
    note.pitch = clampPitch(pitch);
    note.selected = true;
    note.channel = -1;
    note.start_sequence = note.end_sequence = 0;
    return note;
}

} // namespace

const char* harmonyKindName(HarmonyKind kind) {
    int index = static_cast<int>(kind);
    if (index < 0 || index >= harmonyKindCount()) return "Single";
    return kHarmonyNames[index];
}

int harmonyKindCount() {
    return static_cast<int>(sizeof(kHarmonyNames) / sizeof(kHarmonyNames[0]));
}

int harmonyPitch(int melody_pitch, HarmonyKind kind, int tonic, bool minor) {
    tonic = ((tonic % 12) + 12) % 12;
    switch (kind) {
        case HarmonyKind::Single:
            return melody_pitch;
        case HarmonyKind::Octave: {
            int pitch = melody_pitch - 12;
            if (pitch < 0) pitch = melody_pitch + 12;
            return clampPitch(pitch);
        }
        case HarmonyKind::FifthBelow:
            return clampPitch(melody_pitch - 7);
        case HarmonyKind::DiatonicThirdBelow: {
            int melody_pc = ((melody_pitch % 12) + 12) % 12;
            int degree = nearestScaleDegree(melody_pc, tonic, minor);
            return pitchFromScaleDegree(melody_pitch, degree - 2, tonic, minor);
        }
        case HarmonyKind::DiatonicSixthBelow: {
            int melody_pc = ((melody_pitch % 12) + 12) % 12;
            int degree = nearestScaleDegree(melody_pc, tonic, minor);
            return pitchFromScaleDegree(melody_pitch, degree - 5, tonic, minor);
        }
        case HarmonyKind::TriadBelow: {
            int melody_pc = ((melody_pitch % 12) + 12) % 12;
            int degree = nearestScaleDegree(melody_pc, tonic, minor);
            return pitchFromScaleDegree(melody_pitch, degree - 2, tonic, minor);
        }
    }
    return melody_pitch;
}

std::vector<Note> companionNotes(const Note& melody, HarmonyKind kind, int tonic, bool minor) {
    std::vector<Note> notes;
    if (kind == HarmonyKind::Single) return notes;

    tonic = ((tonic % 12) + 12) % 12;
    int melody_pc = ((melody.pitch % 12) + 12) % 12;
    int degree = nearestScaleDegree(melody_pc, tonic, minor);

    if (kind == HarmonyKind::TriadBelow) {
        int third = pitchFromScaleDegree(melody.pitch, degree - 2, tonic, minor);
        int fifth = pitchFromScaleDegree(melody.pitch, degree - 4, tonic, minor);
        int root = pitchFromScaleDegree(melody.pitch, degree - 6, tonic, minor);
        if (root < melody.pitch) notes.push_back(makeCompanion(melody, root));
        if (third < melody.pitch && third != root) notes.push_back(makeCompanion(melody, third));
        if (fifth < melody.pitch && fifth != root && fifth != third) notes.push_back(makeCompanion(melody, fifth));
        return notes;
    }

    int pitch = harmonyPitch(melody.pitch, kind, tonic, minor);
    if (pitch != melody.pitch) {
        notes.push_back(makeCompanion(melody, pitch));
    }
    return notes;
}

std::vector<Note> buildStampedNotes(const Note& melody, HarmonyKind kind, int tonic, bool minor) {
    std::vector<Note> notes;
    Note lead = melody;
    lead.selected = true;
    notes.push_back(lead);

    auto companions = companionNotes(melody, kind, tonic, minor);
    for (auto& note : companions) {
        notes.push_back(note);
    }
    return notes;
}

} // namespace midi
