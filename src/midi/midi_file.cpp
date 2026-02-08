#include "midi_file.h"
#include "MidiFile.h"
#include <map>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <climits>

namespace midi {

// RAII wrapper for temporary files -- ensures cleanup even on exceptions
// This broke earlier when the MIDI file saving functionality was added.
struct TempFileGuard {
    std::string path;
    TempFileGuard() = default;
    explicit TempFileGuard(std::string p) : path(std::move(p)) {}
    ~TempFileGuard() {
        if (!path.empty()) std::remove(path.c_str());
    }
    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;
    TempFileGuard(TempFileGuard&& other) noexcept : path(std::move(other.path)) { other.path.clear(); }
    TempFileGuard& operator=(TempFileGuard&& other) noexcept {
        if (this != &other) {
            if (!path.empty()) std::remove(path.c_str());
            path = std::move(other.path);
            other.path.clear();
        }
        return *this;
    }
};

// Check if file is RIFF-wrapped MIDI (RMID format)
static bool isRmidFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    char header[4];
    file.read(header, 4);
    return (file.gcount() == 4 && std::memcmp(header, "RIFF", 4) == 0);
}

// Extract MIDI data from RMID file to a temporary file
static std::string extractMidiFromRmid(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        fprintf(stderr, "Load error: Cannot open RMID file: %s\n", filepath.c_str());
        return "";
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0) return "";

    // Skip file size
    file.seekg(4, std::ios::cur);

    // Read RMID
    char rmid[4];
    file.read(rmid, 4);
    if (std::memcmp(rmid, "RMID", 4) != 0) return "";

    // Find the "data" chunk containing the MIDI
    while (file) {
        char chunkId[4];
        file.read(chunkId, 4);
        if (file.gcount() != 4) break;

        uint32_t chunkSize;
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (file.gcount() != 4) break;

        if (std::memcmp(chunkId, "data", 4) == 0) {
            // Found the MIDI data chunk
            // Check if it starts with MThd
            char mthd[4];
            file.read(mthd, 4);
            if (std::memcmp(mthd, "MThd", 4) != 0) {
                fprintf(stderr, "Load error: RMID data chunk does not contain valid MIDI\n");
                return "";
            }

            // Seek back to include MThd
            file.seekg(-4, std::ios::cur);

            // Create a temporary file with the MIDI data
            std::string tempPath = filepath + ".tmp.mid";
            std::ofstream tempFile(tempPath, std::ios::binary);
            if (!tempFile) {
                fprintf(stderr, "Load error: Cannot create temp file: %s\n", tempPath.c_str());
                return "";
            }

            // Copy the MIDI data
            std::vector<char> buffer(chunkSize);
            file.read(buffer.data(), chunkSize);
            tempFile.write(buffer.data(), file.gcount());
            tempFile.close();

            return tempPath;
        } else {
            // Skip this chunk
            file.seekg(chunkSize, std::ios::cur);
            // RIFF chunks are word-aligned
            if (chunkSize % 2 == 1) {
                file.seekg(1, std::ios::cur);
            }
        }
    }

    fprintf(stderr, "Load error: No MIDI data found in RMID file\n");
    return "";
}

bool loadMidiFile(const std::string& filepath, Project& project) {
    std::string actualPath = filepath;
    TempFileGuard tempGuard;

    // Check if this is an RMID file
    if (isRmidFile(filepath)) {
        std::string tempPath = extractMidiFromRmid(filepath);
        if (tempPath.empty()) {
            fprintf(stderr, "Load error: Failed to extract MIDI from RMID: %s\n", filepath.c_str());
            return false;
        }
        tempGuard = TempFileGuard(tempPath);
        actualPath = tempPath;
    }

    smf::MidiFile midifile;
    bool loaded = midifile.read(actualPath);

    if (!loaded) {
        fprintf(stderr, "Load error: Failed to parse MIDI file: %s\n", actualPath.c_str());
        return false;
    }

    // Convert to absolute ticks and link note-ons with note-offs
    midifile.doTimeAnalysis();
    midifile.linkNotePairs();

    project = Project();
    project.filepath = filepath;  // Store original filepath
    project.ticks_per_quarter = midifile.getTicksPerQuarterNote();

    // Get tempo from first tempo event, default to 120 BPM
    project.tempo_bpm = 120.0f;
    for (int track = 0; track < midifile.getTrackCount(); ++track) {
        for (int event = 0; event < midifile[track].size(); ++event) {
            if (midifile[track][event].isTempo()) {
                project.tempo_bpm = static_cast<float>(midifile[track][event].getTempoBPM());
                break;
            }
        }
    }

    // Group events by channel
    std::map<int, Track> channelTracks;

    for (int track = 0; track < midifile.getTrackCount(); ++track) {
        for (int event = 0; event < midifile[track].size(); ++event) {
            const auto& mev = midifile[track][event];

            if (mev.isNoteOn() && mev.getLinkedEvent() != nullptr) {
                int channel = mev.getChannel();

                // Create track for this channel if it doesn't exist
                if (channelTracks.find(channel) == channelTracks.end()) {
                    Track newTrack;
                    newTrack.name = "Channel " + std::to_string(channel + 1);
                    newTrack.channel = channel;
                    channelTracks[channel] = newTrack;
                }

                // Create note
                Note note;
                note.pitch = mev.getKeyNumber();
                note.velocity = mev.getVelocity();
                note.start_tick = static_cast<uint32_t>(mev.tick);

                const auto* linkedOff = mev.getLinkedEvent();
                note.duration = static_cast<uint32_t>(linkedOff->tick - mev.tick);
                if (note.duration == 0) note.duration = 1;

                channelTracks[channel].notes.push_back(note);
            }
            else if (mev.isPatchChange()) {
                int channel = mev.getChannel();
                if (channelTracks.find(channel) != channelTracks.end()) {
                    channelTracks[channel].program = mev[1];
                } else {
                    Track newTrack;
                    newTrack.name = "Channel " + std::to_string(channel + 1);
                    newTrack.channel = channel;
                    newTrack.program = mev[1];
                    channelTracks[channel] = newTrack;
                }
            }
        }
    }

    // Convert map to vector, sorted by channel
    project.tracks.clear();
    for (auto& [channel, track] : channelTracks) {
        track.sortNotes();
        project.tracks.push_back(std::move(track));
    }

    // If no tracks were found, create a default one
    if (project.tracks.empty()) {
        Track defaultTrack;
        defaultTrack.name = "Track 1";
        defaultTrack.channel = 0;
        defaultTrack.program = 0;
        project.tracks.push_back(defaultTrack);
    }

    project.modified = false;
    return true;
}

// Safely clamp a uint32_t tick to a positive int range for midifile library
static int safeTickToInt(uint32_t tick) {
    if (tick > static_cast<uint32_t>(INT32_MAX)) {
        return INT32_MAX;
    }
    return static_cast<int>(tick);
}

bool saveMidiFile(const std::string& filepath, const Project& project) {
    // Ensure we have a valid filepath
    if (filepath.empty()) {
        fprintf(stderr, "Save error: Empty filepath\n");
        return false;
    }

    try {
        smf::MidiFile midifile;

        // Use absolute ticks (will be converted to delta when writing)
        midifile.absoluteTicks();
        midifile.setTicksPerQuarterNote(project.ticks_per_quarter);

        // Track 0 is created by default -- add tempo there
        midifile.addTempo(0, 0, std::max(1.0, static_cast<double>(project.tempo_bpm)));

        // Add one track per project track using addTrack() per iteration
        for (size_t i = 0; i < project.tracks.size(); ++i) {
            const auto& track = project.tracks[i];
            int trackIndex = midifile.addTrack();

            // Program change at the beginning
            midifile.addPatchChange(trackIndex, 0, track.channel,
                                    std::clamp(track.program, 0, 127));

            // Add all notes with safe tick clamping
            for (const auto& note : track.notes) {
                int startTick = safeTickToInt(note.start_tick);
                uint64_t endTick64 = static_cast<uint64_t>(note.start_tick) + note.duration;
                int endTick = safeTickToInt(endTick64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(endTick64));

                midifile.addNoteOn(trackIndex, startTick, track.channel,
                                   std::clamp(note.pitch, 0, 127),
                                   std::clamp(note.velocity, 1, 127));
                midifile.addNoteOff(trackIndex, endTick, track.channel,
                                    std::clamp(note.pitch, 0, 127));
            }
        }

        // Sort events by time
        midifile.sortTracks();

        // Try to write the file
        bool success = midifile.write(filepath);

        if (!success) {
            fprintf(stderr, "Failed to write MIDI file: %s\n", filepath.c_str());
        }

        return success;

    } catch (const std::exception& e) {
        fprintf(stderr, "Exception while saving MIDI file: %s\n", e.what());
        return false;
    } catch (...) {
        fprintf(stderr, "Unknown exception while saving MIDI file\n");
        return false;
    }
}

} // namespace midi
