#pragma once

#include "types.h"
#include <vector>

namespace midi {

enum class HarmonyKind {
    Single,
    Octave,
    FifthBelow,
    DiatonicThirdBelow,
    DiatonicSixthBelow,
    TriadBelow
};

const char* harmonyKindName(HarmonyKind kind);
int harmonyKindCount();

int harmonyPitch(int melody_pitch, HarmonyKind kind, int tonic, bool minor);
std::vector<Note> companionNotes(const Note& melody, HarmonyKind kind, int tonic, bool minor);
std::vector<Note> buildStampedNotes(const Note& melody, HarmonyKind kind, int tonic, bool minor);

} // namespace midi
