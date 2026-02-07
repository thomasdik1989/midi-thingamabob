#include "midi_file.h"
#include "MidiFile.h"
#include <map>
#include <algorithm>
#include <fstream>
#include <cstring>

namespace midi {

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
    if (!file) return "";
    
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
                return "";
            }
            
            // Seek back to include MThd
            file.seekg(-4, std::ios::cur);
            
            // Create a temporary file with the MIDI data
            std::string tempPath = filepath + ".tmp.mid";
            std::ofstream tempFile(tempPath, std::ios::binary);
            if (!tempFile) return "";
            
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
    
    return "";
}

bool loadMidiFile(const std::string& filepath, Project& project) {
    std::string actualPath = filepath;
    std::string tempPath;
    
    // Check if this is an RMID file
    if (isRmidFile(filepath)) {
        tempPath = extractMidiFromRmid(filepath);
        if (tempPath.empty()) {
            return false;
        }
        actualPath = tempPath;
    }
    
    smf::MidiFile midifile;
    bool loaded = midifile.read(actualPath);
    
    // Clean up temp file
    if (!tempPath.empty()) {
        std::remove(tempPath.c_str());
    }
    
    if (!loaded) {
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

bool saveMidiFile(const std::string& filepath, const Project& project) {
    smf::MidiFile midifile;
    midifile.setTicksPerQuarterNote(project.ticks_per_quarter);
    
    // Track 0: tempo and metadata
    midifile.addTrack();
    midifile.addTempo(0, 0, project.tempo_bpm);
    
    // One track per channel
    for (const auto& track : project.tracks) {
        int trackIndex = midifile.addTrack();
        
        // Program change at the beginning
        midifile.addPatchChange(trackIndex, 0, track.channel, track.program);
        
        // Add all notes
        for (const auto& note : track.notes) {
            midifile.addNoteOn(trackIndex, note.start_tick, track.channel, note.pitch, note.velocity);
            midifile.addNoteOff(trackIndex, note.start_tick + note.duration, track.channel, note.pitch);
        }
    }
    
    // Sort events by time
    midifile.sortTracks();
    
    return midifile.write(filepath);
}

} // namespace midi
