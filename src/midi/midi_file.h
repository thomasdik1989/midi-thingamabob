#pragma once

#include "types.h"
#include <string>

namespace midi {

// Load a MIDI file into a Project structure
bool loadMidiFile(const std::string& filepath, Project& project);

// Save a Project to a MIDI file
bool saveMidiFile(const std::string& filepath, const Project& project);

} // namespace midi
