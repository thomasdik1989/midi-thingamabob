#include "midi_file.h"
#include "atomic_file.h"
#include "MidiFile.h"

#include <algorithm>
#include <fstream>
#include <cstring>
#include <climits>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace midi {

// Read RIFF-wrapped MIDI in memory: importing must never create or overwrite a
// sibling file next to the user's source (which may also be read-only).
static bool readMidiStream(std::istream& input, smf::MidiFile& file) {
    const auto start = input.tellg();
    unsigned char header[14]{};
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!input || std::memcmp(header, "MThd", 4) != 0) return false;
    // Type-2 independent sequences and SMPTE divisions cannot be represented by
    // this editor's shared PPQ timeline. Reject instead of silently changing timing.
    if (header[8] != 0 || header[9] > 1 || (header[12] & 0x80) || (header[12] == 0 && header[13] == 0)) {
        fprintf(stderr, "Load error: Only type 0/1 MIDI with PPQ timing is supported\n");
        return false;
    }
    input.clear(); input.seekg(start);
    return file.read(input);
}

static bool readMidi(const std::string& path, smf::MidiFile& file) {
    std::ifstream input(path, std::ios::binary);
    char header[12]{};
    input.read(header, sizeof(header));
    if (!input) return false;
    if (std::memcmp(header, "RIFF", 4) != 0) {
        input.seekg(0);
        return readMidiStream(input, file);
    }
    if (std::memcmp(header + 8, "RMID", 4) != 0) return false;
    input.seekg(0, std::ios::end);
    auto size = input.tellg();
    input.seekg(12);
    while (input && input.tellg() + std::streamoff(8) <= size) {
        unsigned char chunk[8]{};
        input.read(reinterpret_cast<char*>(chunk), sizeof(chunk));
        uint32_t length = uint32_t(chunk[4]) | (uint32_t(chunk[5]) << 8) |
                          (uint32_t(chunk[6]) << 16) | (uint32_t(chunk[7]) << 24);
        if (std::streamoff(length) > size - input.tellg()) return false;
        if (std::memcmp(chunk, "data", 4) == 0) {
            if (length > 256 * 1024 * 1024) return false;
            std::string data(length, '\0');
            input.read(data.data(), length);
            if (!input) return false;
            std::istringstream stream(data, std::ios::binary);
            return readMidiStream(stream, file);
        }
        input.seekg(std::streamoff(length) + (length & 1), std::ios::cur);
    }
    return false;
}

static bool loadMidiFileImpl(const std::string& filepath, Project& project) {
    smf::MidiFile midifile;
    if (!readMidi(filepath, midifile)) return false;

    // Keep source track boundaries and all events that are not represented by editable notes.
    if (midifile.getTicksPerQuarterNote() <= 0) return false;
    midifile.absoluteTicks();
    midifile.markSequence();
    midifile.linkNotePairs();
    Project loadedProject;
    loadedProject.filepath = filepath;
    loadedProject.ticks_per_quarter = midifile.getTicksPerQuarterNote();
    for (int t = 0; t < midifile.getTrackCount(); ++t) {
        Track track;
        track.name = "Track " + std::to_string(t + 1);
        bool haveChannel = false;
        for (int e = 0; e < midifile[t].size(); ++e) {
            const auto& event = midifile[t][e];
            if (event.tick < 0) return false;
            if (event.isEndOfTrack()) {
                track.end_tick = static_cast<uint32_t>(event.tick);
                continue;
            }
            if (!event.empty() && event[0] >= 0x80 && event[0] < 0xf0 && !haveChannel) {
                track.channel = event.getChannel();
                track.source_channel = track.channel;
                haveChannel = true;
            }
            if (event.size() > 3 && event[0] == 0xff && event[1] == 0x7f && event.tick == 0) {
                const std::string content = event.getMetaContent();
                const std::string prefix = "\x7d" "MidiEditor:1 ";
                if (content.compare(0, prefix.size(), prefix) == 0) {
                    std::istringstream state(content.substr(prefix.size()));
                    char kind = 0; state >> kind;
                    if (kind == 'G') {
                        uint32_t start, end; int enabled;
                        if (state >> start >> end >> enabled) {
                            loadedProject.loop_start = start; loadedProject.loop_end = end;
                            loadedProject.loop_enabled = enabled != 0;
                            continue;
                        }
                    } else if (kind == 'L') {
                        int bars = 0;
                        if (state >> bars && bars > 0) {
                            loadedProject.length_bars = bars;
                            continue;
                        }
                    } else if (kind == 'T') {
                        int muted, solo; float volume, pan;
                        if ((state >> muted >> solo >> volume >> pan) && std::isfinite(volume) && std::isfinite(pan)) {
                            track.muted = muted != 0; track.solo = solo != 0;
                            track.volume = std::clamp(volume, 0.0f, 1.0f); track.pan = std::clamp(pan, 0.0f, 1.0f);
                            continue;
                        }
                    }
                }
            }
            if (event.isTrackName() && event.tick == 0) {
                track.name = event.getMetaContent();
                continue;
            }
            if (event.isTempo() && event.tick == 0) {
                loadedProject.tempo_bpm = static_cast<float>(event.getTempoBPM());
                if (!std::isfinite(loadedProject.tempo_bpm) || loadedProject.tempo_bpm <= 0) return false;
                loadedProject.source_tempo_bpm = loadedProject.tempo_bpm;
            }
            if (event.isTimeSignature() && event.tick == 0 && event.size() >= 7) {
                loadedProject.beats_per_bar = event[3];
                loadedProject.beat_unit = 1 << std::min(int(event[4]), 7);
                loadedProject.source_beats_per_bar = loadedProject.beats_per_bar;
                loadedProject.source_beat_unit = loadedProject.beat_unit;
            }
            if (event.isPatchChange() && event.tick == 0 && event.getChannel() == track.channel) {
                track.program = track.source_program = event[1];
            }
            if (event.isNoteOn() && event.getLinkedEvent()) {
                const auto* off = event.getLinkedEvent();
                if (off->tick < event.tick) return false;
                Note note;
                note.pitch = event.getKeyNumber();
                note.velocity = event.getVelocity();
                note.channel = event.getChannel();
                note.release_velocity = off->getVelocity();
                note.start_sequence = event.seq;
                note.end_sequence = off->seq;
                note.start_tick = static_cast<uint32_t>(event.tick);
                note.duration = static_cast<uint32_t>(off->tick - event.tick);
                track.notes.push_back(note);
            } else if (!(event.isNoteOff() && event.getLinkedEvent())) {
                track.events.push_back({static_cast<uint32_t>(event.tick),
                                        std::vector<unsigned char>(event.begin(), event.end()), event.seq});
            }
        }
        track.sortNotes();
        loadedProject.tracks.push_back(std::move(track));
    }
    if (loadedProject.tracks.empty()) loadedProject.tracks.emplace_back();
    project = std::move(loadedProject);
    return true;
}

bool loadMidiFile(const std::string& filepath, Project& project) {
    try { return loadMidiFileImpl(filepath, project); }
    catch (...) {
        fprintf(stderr, "Load error: Could not read MIDI file: %s\n", filepath.c_str());
        return false;
    }
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

        // Track zero already exists. Preserve track count, order, channels and metadata.
        bool haveTempo = false;
        bool haveSignature = false;
        for (size_t i = 0; i < project.tracks.size(); ++i) {
            const auto& track = project.tracks[i];
            int index = i == 0 ? 0 : midifile.addTrack();
            midifile.addTrackName(index, 0, track.name);
            std::ostringstream state;
            state << "\x7d" "MidiEditor:1 T " << track.muted << ' ' << track.solo << ' '
                  << std::setprecision(9) << track.volume << ' ' << track.pan;
            midifile.addMetaEvent(index, 0, 0x7f, state.str());
            bool haveProgram = false;
            for (const auto& event : track.events) {
                auto data = event.data;
                if (data.empty()) continue;
                if (data.size() >= 3 && data[0] == 0xff && data[1] == 0x51 && event.tick == 0) {
                    if (project.tempo_bpm == project.source_tempo_bpm)
                        midifile.addEvent(index, 0, data)->seq = event.sequence;
                    else midifile.addTempo(index, 0, std::max(1.0, double(project.tempo_bpm)))->seq = event.sequence;
                    haveTempo = true;
                    continue;
                }
                if (data.size() >= 7 && data[0] == 0xff && data[1] == 0x58 && event.tick == 0) {
                    if (project.beats_per_bar != project.source_beats_per_bar || project.beat_unit != project.source_beat_unit) {
                        data[3] = static_cast<unsigned char>(project.beats_per_bar);
                        int power = 0;
                        for (int unit = project.beat_unit; unit > 1; unit >>= 1) ++power;
                        data[4] = static_cast<unsigned char>(power);
                    }
                    midifile.addEvent(index, 0, data)->seq = event.sequence;
                    haveSignature = true;
                    continue;
                }
                if (data[0] >= 0x80 && data[0] < 0xf0) {
                    int channel = data[0] & 15;
                    if (channel == track.source_channel) data[0] = (data[0] & 0xf0) | track.channel;
                    if ((data[0] & 0xf0) == 0xc0 && event.tick == 0 && (data[0] & 15) == track.channel) {
                        if (track.program != track.source_program)
                            data[1] = static_cast<unsigned char>(std::clamp(track.program, 0, 127));
                        haveProgram = true;
                    }
                }
                midifile.addEvent(index, safeTickToInt(event.tick), data)->seq = event.sequence;
            }
            if (!haveProgram && (track.source_channel < 0 || track.program != track.source_program))
                midifile.addPatchChange(index, 0, track.channel, std::clamp(track.program, 0, 127));
            for (const auto& note : track.notes) {
                int channel = note.channel < 0 || note.channel == track.source_channel ? track.channel : note.channel;
                midifile.addNoteOn(index, safeTickToInt(note.start_tick), channel,
                                  std::clamp(note.pitch, 0, 127), std::clamp(note.velocity, 1, 127))->seq = note.start_sequence;
                std::vector<unsigned char> off{static_cast<unsigned char>(0x80 | channel),
                    static_cast<unsigned char>(std::clamp(note.pitch, 0, 127)),
                    static_cast<unsigned char>(std::clamp(note.release_velocity, 0, 127))};
                midifile.addEvent(index, safeTickToInt(note.endTick()), off)->seq = note.end_sequence;
            }
            std::vector<unsigned char> end{0xff, 0x2f, 0};
            uint32_t endTick = track.end_tick;
            for (const auto& note : track.notes) endTick = std::max(endTick, note.endTick());
            for (const auto& event : track.events) endTick = std::max(endTick, event.tick);
            midifile.addEvent(index, safeTickToInt(endTick), end);
        }
        std::ostringstream loopState;
        loopState << "\x7d" "MidiEditor:1 G " << project.loop_start << ' ' << project.loop_end << ' ' << project.loop_enabled;
        midifile.addMetaEvent(0, 0, 0x7f, loopState.str());
        std::ostringstream lengthState;
        lengthState << "\x7d" "MidiEditor:1 L " << project.length_bars;
        midifile.addMetaEvent(0, 0, 0x7f, lengthState.str());
        if (!haveTempo) midifile.addTempo(0, 0, std::max(1.0, double(project.tempo_bpm)));
        if (!haveSignature) midifile.addTimeSignature(0, 0, project.beats_per_bar, project.beat_unit);

        // Sort events by time
        midifile.sortTracks();

        // Try to write the file
        bool success = atomicWrite(filepath, [&](std::ostream& stream) { return midifile.write(stream); });

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
