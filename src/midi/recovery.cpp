#include "recovery.h"
#include "atomic_file.h"
#include <fstream>
#include <cstring>
#include <cmath>

namespace midi {
namespace {
struct Writer {
    std::ostream& stream;

    void writeU32(uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            stream.put(char(value >> (i * 8)));
        }
    }

    void writeFloat(float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, 4);
        writeU32(bits);
    }

    void writeString(const std::string& value) {
        writeU32(uint32_t(value.size()));
        stream.write(value.data(), value.size());
    }
};

struct Reader {
    std::istream& stream;
    bool valid = true;

    uint32_t readU32() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            int byte = stream.get();
            if (byte < 0) {
                valid = false;
                return 0;
            }
            value |= uint32_t(byte) << (i * 8);
        }
        return value;
    }

    uint32_t readCount(uint32_t limit) {
        uint32_t count = readU32();
        if (count > limit) {
            valid = false;
            return 0;
        }
        return count;
    }

    float readFloat() {
        uint32_t bits = readU32();
        float value = 0.0f;
        std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value)) valid = false;
        return value;
    }

    std::string readString() {
        uint32_t length = readCount(16 * 1024 * 1024);
        std::string value(length, '\0');
        stream.read(value.data(), length);
        valid = valid && bool(stream);
        return value;
    }
};

bool writeTrack(Writer& writer, const Track& track) {
    writer.writeString(track.name);
    writer.writeU32(track.channel);
    writer.writeU32(track.program);
    writer.writeU32(track.source_channel + 1);
    writer.writeU32(track.source_program);
    writer.writeU32(track.end_tick);
    writer.writeU32(track.muted);
    writer.writeU32(track.solo);
    writer.writeFloat(track.volume);
    writer.writeFloat(track.pan);
    writer.writeU32(uint32_t(track.notes.size()));
    for (const auto& note : track.notes) {
        writer.writeU32(note.pitch);
        writer.writeU32(note.velocity);
        writer.writeU32(note.start_tick);
        writer.writeU32(note.duration);
        writer.writeU32(note.channel + 1);
        writer.writeU32(note.release_velocity);
        writer.writeU32(note.start_sequence);
        writer.writeU32(note.end_sequence);
    }
    writer.writeU32(uint32_t(track.events.size()));
    for (const auto& event : track.events) {
        writer.writeU32(event.tick);
        writer.writeU32(event.sequence);
        writer.writeString(std::string(event.data.begin(), event.data.end()));
    }
    return bool(writer.stream);
}

bool readTrack(Reader& reader, Track& track) {
    track.name = reader.readString();
    track.channel = reader.readU32();
    track.program = reader.readU32();
    track.source_channel = int(reader.readU32()) - 1;
    track.source_program = reader.readU32();
    track.end_tick = reader.readU32();
    track.muted = reader.readU32() != 0;
    track.solo = reader.readU32() != 0;
    track.volume = reader.readFloat();
    track.pan = reader.readFloat();
    if (track.channel < 0 || track.channel > 15 || track.program < 0 || track.program > 127 ||
        track.source_channel < -1 || track.source_channel > 15) {
        return false;
    }

    uint32_t noteCount = reader.readCount(10000000);
    for (uint32_t i = 0; i < noteCount && reader.valid; ++i) {
        Note note;
        note.pitch = reader.readU32();
        note.velocity = reader.readU32();
        note.start_tick = reader.readU32();
        note.duration = reader.readU32();
        note.channel = int(reader.readU32()) - 1;
        note.release_velocity = reader.readU32();
        note.start_sequence = reader.readU32();
        note.end_sequence = reader.readU32();
        if (note.pitch < 0 || note.pitch > 127 || note.velocity < 0 || note.velocity > 127 ||
            note.channel < -1 || note.channel > 15 || note.release_velocity < 0 || note.release_velocity > 127) {
            return false;
        }
        track.notes.push_back(note);
    }

    uint32_t eventCount = reader.readCount(10000000);
    for (uint32_t i = 0; i < eventCount && reader.valid; ++i) {
        uint32_t tick = reader.readU32();
        uint32_t sequence = reader.readU32();
        std::string bytes = reader.readString();
        track.events.push_back({tick, std::vector<unsigned char>(bytes.begin(), bytes.end()), int(sequence)});
    }
    return reader.valid;
}
}

bool saveRecovery(const std::string& path, const Project& project) {
    return atomicWrite(path, [&](std::ostream& stream) {
        Writer writer{stream};
        writer.writeString("MIDI Editor recovery 2");
        writer.writeString(project.filepath);
        writer.writeU32(project.ticks_per_quarter);
        writer.writeFloat(project.tempo_bpm);
        writer.writeFloat(project.source_tempo_bpm);
        writer.writeU32(project.beats_per_bar);
        writer.writeU32(project.beat_unit);
        writer.writeU32(project.source_beats_per_bar);
        writer.writeU32(project.source_beat_unit);
        writer.writeU32(project.loop_start);
        writer.writeU32(project.loop_end);
        writer.writeU32(project.loop_enabled);
        writer.writeU32(project.length_bars);
        writer.writeU32(uint32_t(project.tracks.size()));
        for (const auto& track : project.tracks) {
            if (!writeTrack(writer, track)) return false;
        }
        return bool(stream);
    });
}

bool loadRecovery(const std::string& path, Project& project) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    Reader reader{stream};
    std::string version = reader.readString();
    if (version != "MIDI Editor recovery 1" && version != "MIDI Editor recovery 2") return false;

    Project loaded;
    loaded.filepath = reader.readString();
    loaded.ticks_per_quarter = reader.readU32();
    loaded.tempo_bpm = reader.readFloat();
    loaded.source_tempo_bpm = reader.readFloat();
    loaded.beats_per_bar = reader.readU32();
    loaded.beat_unit = reader.readU32();
    loaded.source_beats_per_bar = reader.readU32();
    loaded.source_beat_unit = reader.readU32();
    loaded.loop_start = reader.readU32();
    loaded.loop_end = reader.readU32();
    loaded.loop_enabled = reader.readU32() != 0;
    if (version == "MIDI Editor recovery 2") {
        loaded.length_bars = static_cast<int>(reader.readU32());
    }

    if (loaded.ticks_per_quarter <= 0 || loaded.ticks_per_quarter > 32767 || loaded.tempo_bpm <= 0 ||
        loaded.beats_per_bar <= 0 || loaded.beats_per_bar > 255 || loaded.beat_unit <= 0 || loaded.beat_unit > 128 ||
        loaded.length_bars <= 0 || loaded.length_bars > 999) {
        return false;
    }

    uint32_t trackCount = reader.readCount(65536);
    for (uint32_t i = 0; i < trackCount && reader.valid; ++i) {
        Track track;
        if (!readTrack(reader, track)) return false;
        loaded.tracks.push_back(std::move(track));
    }

    if (!reader.valid || !stream || loaded.tracks.empty()) return false;
    loaded.modified = true;
    project = std::move(loaded);
    return true;
}
}
