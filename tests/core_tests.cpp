#include "app.h"
#include "midi/harmony.h"
#include "midi/patterns.h"
#include "midi/midi_file.h"
#include "midi/recovery.h"
#include "midi/atomic_file.h"
#include "MidiFile.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <random>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static midi::Note note(int pitch, uint32_t tick, uint32_t duration = 480) {
    midi::Note result;
    result.pitch = pitch; result.start_tick = tick; result.duration = duration;
    return result;
}
static void timing() {
    for (int fps : {30, 60, 144, 240}) {
        App app;
        app.getProject().tempo_bpm = 123;
        app.getProject().tracks[0].notes.push_back(note(60, 0, 1000000));
        app.setPlaying(true);
        for (int i = 0; i < fps * 10; ++i) app.advancePlayhead(1.0 / fps);
        check(app.getPlayheadTick() == 9840, "fractional timing drift");
    }
    App app;
    auto& project = app.getProject();
    project.loop_enabled = true; project.loop_start = 480; project.loop_end = 960;
    app.setPlayheadTick(900); app.setPlaying(true); app.advancePlayhead(0.25);
    check(app.getPlayheadTick() == 660, "loop overshoot lost");
    check(app.playbackSpans().size() == 2 && app.playbackSpans()[1].reset, "loop boundary not scheduled");
    project.tracks[0].events.push_back({480, {0xff, 0x51, 3, 0x0f, 0x42, 0x40}}); // 60 BPM
    check(std::abs(project.ticksToSeconds(960) - 1.5) < 1e-9, "tempo map conversion");
    check(project.secondsToTicks(1.5) == 960, "tempo map inverse");
}
static void scheduler() {
    midi::Project p; p.tracks.emplace_back();
    p.tracks[0].notes = {note(60, 0, 480), note(60, 480, 480), note(64, 20, 2)};
    auto messages = midi::schedulePlayback(p, {0, 40, true});
    int ons = 0, offs = 0;
    for (const auto& m : messages) { ons += (m.data[0] & 0xf0) == 0x90; offs += (m.data[0] & 0xf0) == 0x80; }
    check(ons == 2 && offs == 1, "tick zero or short notes lost");
    messages = midi::schedulePlayback(p, {240, 250, true});
    ons = 0; for (const auto& m : messages) ons += (m.data[0] & 0xf0) == 0x90;
    check(ons == 1, "seek must chase sustained notes");
    messages = midi::schedulePlayback(p, {470, 490, false});
    check(messages.size() == 2 && (messages[0].data[0] & 0xf0) == 0x80 &&
          (messages[1].data[0] & 0xf0) == 0x90, "retrigger ordering");
    messages = midi::schedulePlayback(p, {470, 480, false, false});
    check(messages.size() == 1 && (messages[0].data[0] & 0xf0) == 0x80, "loop end played excluded note");
    p.tracks[0].events.push_back({100, {0xb0, 64, 127}});
    p.tracks[0].events.push_back({200, {0xc0, 42}});
    messages = midi::schedulePlayback(p, {240, 250, true});
    int stateIndex = -1, noteIndex = -1;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].data == std::vector<unsigned char>{0xc0, 42}) stateIndex = int(i);
        if ((messages[i].data[0] & 0xf0) == 0x90) noteIndex = int(i);
    }
    check(stateIndex >= 0 && stateIndex < noteIndex, "seek must restore state before sustained notes");
}
static void history(const std::filesystem::path& dir) {
    App app;
    app.executeCommand(std::make_unique<AddNotesCommand>(app, 0, std::vector<midi::Note>{note(60, 0), note(64, 480)}));
    app.executeCommand(std::make_unique<MoveNotesCommand>(app, 0, std::vector<size_t>{0}, -70, 960));
    app.undo();
    check(app.getProject().tracks[0].notes[0].pitch == 60 && app.getProject().tracks[0].notes[0].start_tick == 0,
          "move undo after sort/clamp");
    app.redo(); app.undo();
    app.addTrack(); app.removeTrack(0); app.undo(); app.undo();
    check(app.getProject().tracks.size() == 1 && app.getProject().tracks[0].notes.size() == 2, "track undo");
    auto& firstNote = app.getProject().tracks[0].notes[0];
    firstNote.selected = true;
    app.beginUndoGroup();
    app.executeCommand(std::make_unique<ChangeVelocityCommand>(
        app, 0, std::vector<size_t>{0}, std::vector<int>{100}, std::vector<int>{42}));
    app.executeCommand(std::make_unique<ChangeVelocityCommand>(
        app, 0, std::vector<size_t>{0}, std::vector<int>{42}, std::vector<int>{12}));
    app.endUndoGroup();
    app.undo();
    check(app.getProject().tracks[0].notes[0].velocity == 100, "gesture coalescing");
    auto file = (dir / "saved.mid").string();
    check(app.saveFileAs(file), "save checkpoint");
    { auto edit = app.edit(); app.getProject().tracks[0].name = "Changed"; }
    app.undo(); check(!app.getProject().modified, "undo to saved checkpoint");
    check(app.getProject().filepath == file, "undo changed save path");
    app.redo(); check(app.getProject().modified, "redo dirty state");
    App duplicate;
    auto baseNote = note(60, 0);
    auto selectedNote = baseNote;
    selectedNote.velocity = 12;
    selectedNote.selected = true;
    duplicate.getProject().tracks[0].notes = {baseNote, selectedNote};
    duplicate.deleteSelectedNotes();
    check(duplicate.getProject().tracks[0].notes.size() == 1 && duplicate.getProject().tracks[0].notes[0].velocity == 100,
          "delete wrong duplicate");
}
static void files(const std::filesystem::path& dir) {
    smf::MidiFile source;
    source.setTicksPerQuarterNote(480); source.addTrack();
    source.addTrackName(0, 0, "Conductor"); source.addTrackName(1, 0, "Piano");
    source.addTempo(0, 0, 120); source.addTempo(0, 480, 60);
    source.addTimeSignature(0, 0, 3, 4); source.addTimeSignature(0, 960, 6, 8);
    source.addPatchChange(1, 0, 0, 5); source.addPatchChange(1, 960, 0, 12);
    source.addNoteOn(1, 0, 0, 60, 100); source.addNoteOff(1, 480, 0, 60);
    source.addNoteOn(1, 240, 2, 67, 90); source.addNoteOff(1, 720, 2, 67);
    std::vector<unsigned char> sysex{0xf0, 0x7d, 0x01, 0xf7}; source.addEvent(1, 300, sysex);
    source.addMetaEvent(1, 310, 0x7f, std::string("Other sequencer metadata"));
    std::vector<unsigned char> end{0xff, 0x2f, 0}; source.addEvent(1, 2000, end);
    source.addNoteOn(0, 0, 0, 64, 100); source.addNoteOff(0, 480, 0, 64); // same channel, different track
    std::vector<unsigned char> sustain{0xb0, 64, 127}; source.addEvent(1, 100, sustain);
    std::vector<unsigned char> bend{0xe0, 0, 96}; source.addEvent(1, 200, bend);
    source.sortTracks();
    auto original = (dir / "source.mid").string(), output = (dir / "output.mid").string();
    check(source.write(original), "fixture write");
    midi::Project p, reloaded;
    check(midi::loadMidiFile(original, p), "fixture import");
    check(p.tracks.size() == 2 && p.tracks[0].name == "Conductor" && p.beats_per_bar == 3, "track/metadata loss");
    check(midi::saveMidiFile(output, p), "round-trip write");
    check(midi::loadMidiFile(output, reloaded), "round-trip read");
    check(reloaded.tracks.size() == 2 && reloaded.tracks[1].notes == p.tracks[1].notes, "note/track round-trip");
    check(reloaded.tracks[1].events == p.tracks[1].events, "controller/program/SysEx round-trip");
    check(reloaded.tracks[1].end_tick == p.tracks[1].end_tick, "trailing silence lost");
    check(reloaded.tracks[0].events == p.tracks[0].events, "tempo/signature round-trip");
    p.loop_enabled = true; p.loop_start = 100; p.loop_end = 800; p.tracks[0].muted = true; p.tracks[1].pan = .2f;
    check(midi::saveMidiFile(output, p) && midi::loadMidiFile(output, reloaded), "editor state save");
    check(reloaded.loop_enabled && reloaded.loop_start == 100 && reloaded.loop_end == 800 &&
          reloaded.tracks[0].muted && reloaded.tracks[1].pan == .2f, "editor settings lost on MIDI save");
    auto recovery = (dir / "session.recovery").string();
    check(midi::saveRecovery(recovery, p) && midi::loadRecovery(recovery, reloaded) && p == reloaded, "recovery fidelity");
    std::ofstream existing(dir / "atomic.txt"); existing << "original"; existing.close();
    check(!midi::atomicWrite((dir / "atomic.txt").string(), [](std::ostream& out) { out << "partial"; return false; }), "failed write accepted");
    std::ifstream retained(dir / "atomic.txt"); std::string text; retained >> text;
    check(text == "original", "failed save destroyed destination");
    check(midi::atomicWrite((dir / "atomic.txt").string(), [](std::ostream& out) { out << "replacement"; return true; }), "atomic replacement failed");
    std::ifstream replacement(dir / "atomic.txt"); replacement >> text;
    check(text == "replacement", "atomic replacement has wrong content");
    // RIFF import must leave a pre-existing .tmp.mid sibling untouched.
    std::ifstream input(original, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    auto rmid = (dir / "wrapped.rmi").string();
    std::ofstream wrapped(rmid, std::ios::binary);
    auto little = [&](uint32_t n) { for (int i = 0; i < 4; ++i) wrapped.put(char(n >> (i * 8))); };
    wrapped << "RIFF"; little(uint32_t(bytes.size()) + 12); wrapped << "RMIDdata";
    little(uint32_t(bytes.size())); wrapped.write(bytes.data(), bytes.size()); wrapped.close();
    std::ofstream sibling(rmid + ".tmp.mid"); sibling << "keep"; sibling.close();
    check(midi::loadMidiFile(rmid, reloaded) && reloaded.tracks.size() == 2, "RIFF import");
    std::ifstream kept(rmid + ".tmp.mid"); kept >> text; check(text == "keep", "RIFF import overwrote sibling");
    auto before = reloaded;
    std::ofstream invalid(dir / "invalid.mid", std::ios::binary); invalid << "not a MIDI file"; invalid.close();
    check(!midi::loadMidiFile((dir / "invalid.mid").string(), reloaded) && before == reloaded, "failed import replaced project");
    bytes[12] = char(0xe7); bytes[13] = char(40);
    std::ofstream smpte(dir / "smpte.mid", std::ios::binary); smpte.write(bytes.data(), bytes.size()); smpte.close();
    check(!midi::loadMidiFile((dir / "smpte.mid").string(), reloaded), "SMPTE silently converted to PPQ");
    App app; app.configureRecovery((dir / "recovery").string());
    app.executeCommand(std::make_unique<AddNotesCommand>(app, 0, std::vector<midi::Note>{note(60, 0)}));
    check(app.autosave(), "autosave");
    App recovered; recovered.configureRecovery((dir / "recovery").string());
    check(recovered.recoveryFiles().size() == 1 && recovered.recover(recovered.recoveryFiles()[0]), "recover previous session");
    check(recovered.getProject().modified && recovered.getProject().tracks[0].notes.size() == 1, "recovered state");
}
static void length_and_drum_map() {
    midi::Project project;
    project.length_bars = 32;
    check(project.getTotalTicks() == static_cast<uint32_t>(project.ticksPerBar()) * 32,
          "length_bars sets empty project extent");

    project.tracks.emplace_back();
    project.tracks[0].notes.push_back(note(60, 0, static_cast<uint32_t>(project.ticksPerBar()) * 40));
    project.length_bars = 16;
    check(project.getTotalTicks() == static_cast<uint32_t>(project.ticksPerBar()) * 40,
          "content end wins over shorter length_bars");

    check(midi::drumYToPitch(midi::drumPitchToY(35, 0.0f, 18.0f, 0.0f), 0.0f, 18.0f, 0.0f) == 35,
          "drum map round-trip low pitch");
    check(midi::drumYToPitch(midi::drumPitchToY(42, 100.0f, 18.0f, 0.0f), 100.0f, 18.0f, 0.0f) == 42,
          "drum map round-trip mid pitch");
    check(midi::drumYToPitch(midi::drumPitchToY(81, 200.0f, 18.0f, 0.0f), 200.0f, 18.0f, 0.0f) == 81,
          "drum map round-trip high pitch");

    App app;
    app.setLengthBars(64);
    check(app.getLengthBars() == 64, "setLengthBars");
}

static void patterns_and_harmony() {
    const auto& battle = midi::getDrumGroove(0);
    auto notes = midi::expandDrumGroove(battle, 0, 2, 480);
    check(!notes.empty(), "battle groove produced no notes");
    bool kick_bar1 = false;
    bool kick_bar2 = false;
    bool snare_bar1 = false;
    for (const auto& n : notes) {
        if (n.pitch == 36 && n.start_tick == 0) kick_bar1 = true;
        if (n.pitch == 36 && n.start_tick == 1920) kick_bar2 = true;
        if (n.pitch == 38 && n.start_tick == 480) snare_bar1 = true;
    }
    check(kick_bar1 && kick_bar2 && snare_bar1, "battle groove tiling at 480 PPQ");

    const auto& waltz = midi::getDrumGroove(3);
    auto waltz_notes = midi::expandDrumGroove(waltz, 0, 1, 480);
    check(waltz.beats_per_bar == 3, "waltz is 3/4");
    bool waltz_span = false;
    for (const auto& n : waltz_notes) {
        if (n.start_tick >= 1440) waltz_span = true;
    }
    check(!waltz_span, "waltz bar length is 12 sixteenths");

    check(midi::harmonyPitch(60, midi::HarmonyKind::DiatonicThirdBelow, 0, false) == 57,
          "C major third below C");
    check(midi::harmonyPitch(64, midi::HarmonyKind::DiatonicThirdBelow, 0, false) == 60,
          "C major third below E");
    check(midi::harmonyPitch(67, midi::HarmonyKind::DiatonicThirdBelow, 0, false) == 64,
          "C major third below G");

    App app;
    app.addDrumTrack();
    check(app.getProject().tracks.size() == 2, "addDrumTrack added track");
    check(midi::isDrumTrack(app.getProject().tracks[1]), "drum track uses channel 9");
    check(app.getProject().tracks[1].channel == 9, "drum track channel is 9");

    app.insertDrumGroove(0, 1);
    check(app.getProject().tracks[1].notes.size() > 0, "insertDrumGroove added notes");
}

int main() {
    auto dir = std::filesystem::temp_directory_path() / ("midi-tests-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    try {
        timing(); scheduler(); history(dir); files(dir); length_and_drum_map(); patterns_and_harmony();
        std::filesystem::remove_all(dir);
        std::cout << "All core regression tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (fixtures: " << dir << ")\n";
        return 1;
    }
}
