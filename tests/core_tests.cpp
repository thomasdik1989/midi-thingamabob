#include "app.h"
#include "midi/harmony.h"
#include "midi/patterns.h"
#include "midi/midi_file.h"
#include "midi/recovery.h"
#include "midi/atomic_file.h"
#include "midi/synth_presets.h"
#include "midi/audio_synth.h"
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

static midi::Note makeNote(int pitch, uint32_t start_tick, uint32_t duration = 480) {
    midi::Note result;
    result.pitch = pitch;
    result.start_tick = start_tick;
    result.duration = duration;
    return result;
}

static bool isNoteOn(const midi::PlaybackMessage& message) {
    return (message.data[0] & 0xf0) == 0x90;
}

static bool isNoteOff(const midi::PlaybackMessage& message) {
    return (message.data[0] & 0xf0) == 0x80;
}

static int countNoteOns(const std::vector<midi::PlaybackMessage>& messages) {
    int count = 0;
    for (const auto& message : messages) {
        if (isNoteOn(message)) ++count;
    }
    return count;
}

static int countNoteOffs(const std::vector<midi::PlaybackMessage>& messages) {
    int count = 0;
    for (const auto& message : messages) {
        if (isNoteOff(message)) ++count;
    }
    return count;
}

static std::string writeSourceMidiFixture(const std::filesystem::path& dir) {
    smf::MidiFile source;
    source.setTicksPerQuarterNote(480);
    source.addTrack();
    source.addTrackName(0, 0, "Conductor");
    source.addTrackName(1, 0, "Piano");
    source.addTempo(0, 0, 120);
    source.addTempo(0, 480, 60);
    source.addTimeSignature(0, 0, 3, 4);
    source.addTimeSignature(0, 960, 6, 8);
    source.addPatchChange(1, 0, 0, 5);
    source.addPatchChange(1, 960, 0, 12);
    source.addNoteOn(1, 0, 0, 60, 100);
    source.addNoteOff(1, 480, 0, 60);
    source.addNoteOn(1, 240, 2, 67, 90);
    source.addNoteOff(1, 720, 2, 67);
    std::vector<unsigned char> sysex{0xf0, 0x7d, 0x01, 0xf7};
    source.addEvent(1, 300, sysex);
    source.addMetaEvent(1, 310, 0x7f, std::string("Other sequencer metadata"));
    std::vector<unsigned char> end{0xff, 0x2f, 0};
    source.addEvent(1, 2000, end);
    source.addNoteOn(0, 0, 0, 64, 100);
    source.addNoteOff(0, 480, 0, 64);
    std::vector<unsigned char> sustain{0xb0, 64, 127};
    source.addEvent(1, 100, sustain);
    std::vector<unsigned char> bend{0xe0, 0, 96};
    source.addEvent(1, 200, bend);
    source.sortTracks();

    auto path = (dir / "source.mid").string();
    check(source.write(path), "fixture write");
    return path;
}

static void writeLittleEndianUint32(std::ostream& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.put(char(value >> (i * 8)));
    }
}

// --- Playhead and tempo ---

static void playheadDoesNotDriftAtVariousFrameRates() {
    const int tempoBpm = 123;
    const uint32_t expectedTick = 9840;
    const uint32_t longNoteDuration = 1000000;
    const int secondsToAdvance = 10;

    for (int fps : {30, 60, 144, 240}) {
        App app;
        app.getProject().tempo_bpm = tempoBpm;
        app.getProject().tracks[0].notes.push_back(makeNote(60, 0, longNoteDuration));
        app.setPlaying(true);
        for (int i = 0; i < fps * secondsToAdvance; ++i) {
            app.advancePlayhead(1.0 / fps);
        }
        check(app.getPlayheadTick() == expectedTick, "fractional timing drift");
    }
}

static void loopOvershootWrapsPlayheadIntoRegion() {
    const uint32_t loopStart = 480;
    const uint32_t loopEnd = 960;
    const uint32_t playheadBeforeAdvance = 900;
    const uint32_t expectedPlayhead = 660;

    App app;
    auto& project = app.getProject();
    project.loop_enabled = true;
    project.loop_start = loopStart;
    project.loop_end = loopEnd;
    app.setPlayheadTick(playheadBeforeAdvance);
    app.setPlaying(true);
    app.advancePlayhead(0.25);
    check(app.getPlayheadTick() == expectedPlayhead, "loop overshoot lost");
}

static void loopBoundarySchedulesResetSpan() {
    const uint32_t loopStart = 480;
    const uint32_t loopEnd = 960;
    const uint32_t playheadBeforeAdvance = 900;

    App app;
    auto& project = app.getProject();
    project.loop_enabled = true;
    project.loop_start = loopStart;
    project.loop_end = loopEnd;
    app.setPlayheadTick(playheadBeforeAdvance);
    app.setPlaying(true);
    app.advancePlayhead(0.25);

    const auto& spans = app.playbackSpans();
    check(spans.size() == 2, "loop boundary not scheduled");
    check(spans[1].reset, "loop boundary not scheduled");
}

static void tempoMapConvertsTicksAndSeconds() {
    const uint32_t tempoChangeTick = 480;
    const uint32_t twoQuarters = 960;
    const double expectedSeconds = 1.5;

    App app;
    auto& project = app.getProject();
    project.tracks[0].events.push_back({tempoChangeTick, {0xff, 0x51, 3, 0x0f, 0x42, 0x40}});
    check(std::abs(project.ticksToSeconds(twoQuarters) - expectedSeconds) < 1e-9, "tempo map conversion");
    check(project.secondsToTicks(expectedSeconds) == twoQuarters, "tempo map inverse");
}

// --- Playback scheduler ---

static void schedulerEmitsTickZeroAndShortNotes() {
    midi::Project project;
    project.tracks.emplace_back();
    project.tracks[0].notes = {
        makeNote(60, 0, 480),
        makeNote(60, 480, 480),
        makeNote(64, 20, 2),
    };

    auto messages = midi::schedulePlayback(project, {0, 40, true});
    check(countNoteOns(messages) == 2, "tick zero or short notes lost");
    check(countNoteOffs(messages) == 1, "tick zero or short notes lost");
}

static void seekChasesSustainedNotes() {
    midi::Project project;
    project.tracks.emplace_back();
    project.tracks[0].notes = {
        makeNote(60, 0, 480),
        makeNote(60, 480, 480),
        makeNote(64, 20, 2),
    };

    auto messages = midi::schedulePlayback(project, {240, 250, true});
    check(countNoteOns(messages) == 1, "seek must chase sustained notes");
}

static void retriggerSendsNoteOffBeforeNoteOn() {
    midi::Project project;
    project.tracks.emplace_back();
    project.tracks[0].notes = {
        makeNote(60, 0, 480),
        makeNote(60, 480, 480),
        makeNote(64, 20, 2),
    };

    auto messages = midi::schedulePlayback(project, {470, 490, false});
    check(messages.size() == 2, "retrigger ordering");
    check(isNoteOff(messages[0]), "retrigger ordering");
    check(isNoteOn(messages[1]), "retrigger ordering");
}

static void loopEndExcludesNoteStartingAtBoundary() {
    midi::Project project;
    project.tracks.emplace_back();
    project.tracks[0].notes = {
        makeNote(60, 0, 480),
        makeNote(60, 480, 480),
        makeNote(64, 20, 2),
    };

    auto messages = midi::schedulePlayback(project, {470, 480, false, false});
    check(messages.size() == 1, "loop end played excluded note");
    check(isNoteOff(messages[0]), "loop end played excluded note");
}

static void seekRestoresControllersBeforeSustainedNotes() {
    midi::Project project;
    project.tracks.emplace_back();
    project.tracks[0].notes = {
        makeNote(60, 0, 480),
        makeNote(60, 480, 480),
        makeNote(64, 20, 2),
    };
    project.tracks[0].events.push_back({100, {0xb0, 64, 127}});
    project.tracks[0].events.push_back({200, {0xc0, 42}});

    auto messages = midi::schedulePlayback(project, {240, 250, true});
    int stateIndex = -1;
    int noteIndex = -1;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].data == std::vector<unsigned char>{0xc0, 42}) {
            stateIndex = int(i);
        }
        if (isNoteOn(messages[i])) {
            noteIndex = int(i);
        }
    }
    check(stateIndex >= 0 && stateIndex < noteIndex, "seek must restore state before sustained notes");
}

// --- Undo / redo / delete ---

static void undoMoveRestoresPitchAfterSortAndClamp() {
    App app;
    app.executeCommand(std::make_unique<AddNotesCommand>(
        app, 0, std::vector<midi::Note>{makeNote(60, 0), makeNote(64, 480)}));
    app.executeCommand(std::make_unique<MoveNotesCommand>(
        app, 0, std::vector<size_t>{0}, -70, 960));
    app.undo();

    const auto& first = app.getProject().tracks[0].notes[0];
    check(first.pitch == 60, "move undo after sort/clamp");
    check(first.start_tick == 0, "move undo after sort/clamp");
}

static void undoTrackAddRemoveRestoresNotes() {
    App app;
    app.executeCommand(std::make_unique<AddNotesCommand>(
        app, 0, std::vector<midi::Note>{makeNote(60, 0), makeNote(64, 480)}));
    app.addTrack();
    app.removeTrack(0);
    app.undo();
    app.undo();

    check(app.getProject().tracks.size() == 1, "track undo");
    check(app.getProject().tracks[0].notes.size() == 2, "track undo");
}

static void undoGroupRestoresVelocityAfterCoalescedGestures() {
    App app;
    app.executeCommand(std::make_unique<AddNotesCommand>(
        app, 0, std::vector<midi::Note>{makeNote(60, 0), makeNote(64, 480)}));
    app.getProject().tracks[0].notes[0].selected = true;
    app.beginUndoGroup();
    app.executeCommand(std::make_unique<ChangeVelocityCommand>(
        app, 0, std::vector<size_t>{0}, std::vector<int>{42}));
    app.executeCommand(std::make_unique<ChangeVelocityCommand>(
        app, 0, std::vector<size_t>{0}, std::vector<int>{12}));
    app.endUndoGroup();
    app.undo();

    check(app.getProject().tracks[0].notes[0].velocity == 100, "gesture coalescing");
}

static void undoToSaveClearsModifiedAndKeepsPath(const std::filesystem::path& dir) {
    App app;
    app.executeCommand(std::make_unique<AddNotesCommand>(
        app, 0, std::vector<midi::Note>{makeNote(60, 0), makeNote(64, 480)}));
    auto file = (dir / "saved.mid").string();
    check(app.saveFileAs(file), "save checkpoint");

    { auto edit = app.edit(); app.getProject().tracks[0].name = "Changed"; }
    app.undo();

    check(!app.getProject().modified, "undo to saved checkpoint");
    check(app.getProject().filepath == file, "undo changed save path");
}

static void redoAfterSaveMarksProjectDirty(const std::filesystem::path& dir) {
    App app;
    app.executeCommand(std::make_unique<AddNotesCommand>(
        app, 0, std::vector<midi::Note>{makeNote(60, 0), makeNote(64, 480)}));
    auto file = (dir / "saved.mid").string();
    check(app.saveFileAs(file), "save checkpoint");

    { auto edit = app.edit(); app.getProject().tracks[0].name = "Changed"; }
    app.undo();
    app.redo();

    check(app.getProject().modified, "redo dirty state");
}

static void deleteSelectedNotesLeavesUnselected() {
    App app;
    auto baseNote = makeNote(60, 0);
    auto selectedNote = makeNote(64, 480);
    selectedNote.selected = true;
    app.getProject().tracks[0].notes = {baseNote, selectedNote};
    app.deleteSelectedNotes();

    check(app.getProject().tracks[0].notes.size() == 1, "delete selected note only");
    check(app.getProject().tracks[0].notes[0].pitch == 60, "delete selected note only");
}

static void deleteNotesCommandMatchesIdentityNotSelection() {
    App app;
    auto noteA = makeNote(60, 0);
    auto noteB = makeNote(64, 480);
    noteB.selected = true;
    app.getProject().tracks[0].notes = {noteA, noteB};

    app.executeCommand(std::make_unique<DeleteNotesCommand>(
        app, 0, std::vector<midi::Note>{noteB}));
    check(app.getProject().tracks[0].notes.size() == 1, "delete by stored identity");
    check(app.getProject().tracks[0].notes[0].pitch == 60, "delete by stored identity");

    app.getProject().tracks[0].notes[0].selected = false;
    app.executeCommand(std::make_unique<DeleteNotesCommand>(
        app, 0, std::vector<midi::Note>{noteA}));
    check(app.getProject().tracks[0].notes.empty(), "delete ignores selection flag after command creation");
}

// --- MIDI file I/O ---

static void midiImportPreservesTracksAndMetadata(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);
    midi::Project project;
    check(midi::loadMidiFile(original, project), "fixture import");
    check(project.tracks.size() == 2, "track/metadata loss");
    check(project.tracks[0].name == "Conductor", "track/metadata loss");
    check(project.beats_per_bar == 3, "track/metadata loss");
}

static void midiRoundTripPreservesNotesEventsAndSilence(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);
    auto output = (dir / "output.mid").string();

    midi::Project project;
    midi::Project reloaded;
    check(midi::loadMidiFile(original, project), "fixture import");
    check(midi::saveMidiFile(output, project), "round-trip write");
    check(midi::loadMidiFile(output, reloaded), "round-trip read");

    check(reloaded.tracks.size() == 2, "note/track round-trip");
    check(reloaded.tracks[1].notes == project.tracks[1].notes, "note/track round-trip");
    check(reloaded.tracks[1].events == project.tracks[1].events, "controller/program/SysEx round-trip");
    check(reloaded.tracks[1].end_tick == project.tracks[1].end_tick, "trailing silence lost");
    check(reloaded.tracks[0].events == project.tracks[0].events, "tempo/signature round-trip");
}

static void midiSavePreservesEditorLoopMuteAndPan(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);
    auto output = (dir / "output.mid").string();

    midi::Project project;
    midi::Project reloaded;
    check(midi::loadMidiFile(original, project), "fixture import");

    project.loop_enabled = true;
    project.loop_start = 100;
    project.loop_end = 800;
    project.tracks[0].muted = true;
    project.tracks[1].pan = 0.2f;

    check(midi::saveMidiFile(output, project), "editor state save");
    check(midi::loadMidiFile(output, reloaded), "editor state save");
    check(reloaded.loop_enabled, "editor settings lost on MIDI save");
    check(reloaded.loop_start == 100, "editor settings lost on MIDI save");
    check(reloaded.loop_end == 800, "editor settings lost on MIDI save");
    check(reloaded.tracks[0].muted, "editor settings lost on MIDI save");
    check(reloaded.tracks[1].pan == 0.2f, "editor settings lost on MIDI save");
}

static void recoveryRoundTripEqualsProject(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);

    midi::Project project;
    midi::Project reloaded;
    check(midi::loadMidiFile(original, project), "fixture import");
    project.loop_enabled = true;
    project.loop_start = 100;
    project.loop_end = 800;
    project.tracks[0].muted = true;
    project.tracks[1].pan = 0.2f;

    auto recovery = (dir / "session.recovery").string();
    check(midi::saveRecovery(recovery, project), "recovery fidelity");
    check(midi::loadRecovery(recovery, reloaded), "recovery fidelity");
    check(project == reloaded, "recovery fidelity");
}

static void failedAtomicWriteLeavesOriginalFile(const std::filesystem::path& dir) {
    auto path = (dir / "atomic.txt").string();
    std::ofstream existing(dir / "atomic.txt");
    existing << "original";
    existing.close();

    check(!midi::atomicWrite(path, [](std::ostream& out) {
        out << "partial";
        return false;
    }), "failed write accepted");

    std::ifstream retained(dir / "atomic.txt");
    std::string text;
    retained >> text;
    check(text == "original", "failed save destroyed destination");
}

static void atomicWriteReplacesDestination(const std::filesystem::path& dir) {
    auto path = (dir / "atomic.txt").string();
    std::ofstream existing(dir / "atomic.txt");
    existing << "original";
    existing.close();

    check(midi::atomicWrite(path, [](std::ostream& out) {
        out << "replacement";
        return true;
    }), "atomic replacement failed");

    std::ifstream replacement(dir / "atomic.txt");
    std::string text;
    replacement >> text;
    check(text == "replacement", "atomic replacement has wrong content");
}

static void riffImportLoadsWrappedMidi(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);
    std::ifstream input(original, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});

    auto rmid = (dir / "wrapped.rmi").string();
    std::ofstream wrapped(rmid, std::ios::binary);
    wrapped << "RIFF";
    writeLittleEndianUint32(wrapped, uint32_t(bytes.size()) + 12);
    wrapped << "RMIDdata";
    writeLittleEndianUint32(wrapped, uint32_t(bytes.size()));
    wrapped.write(bytes.data(), bytes.size());
    wrapped.close();

    midi::Project reloaded;
    check(midi::loadMidiFile(rmid, reloaded), "RIFF import");
    check(reloaded.tracks.size() == 2, "RIFF import");
}

static void riffImportDoesNotOverwriteSiblingTemp(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);
    std::ifstream input(original, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});

    auto rmid = (dir / "wrapped.rmi").string();
    std::ofstream wrapped(rmid, std::ios::binary);
    wrapped << "RIFF";
    writeLittleEndianUint32(wrapped, uint32_t(bytes.size()) + 12);
    wrapped << "RMIDdata";
    writeLittleEndianUint32(wrapped, uint32_t(bytes.size()));
    wrapped.write(bytes.data(), bytes.size());
    wrapped.close();

    std::ofstream sibling(rmid + ".tmp.mid");
    sibling << "keep";
    sibling.close();

    midi::Project reloaded;
    check(midi::loadMidiFile(rmid, reloaded), "RIFF import");

    std::ifstream kept(rmid + ".tmp.mid");
    std::string text;
    kept >> text;
    check(text == "keep", "RIFF import overwrote sibling");
}

static void failedImportLeavesProjectUnchanged(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);
    midi::Project before;
    check(midi::loadMidiFile(original, before), "fixture import");

    std::ofstream invalid(dir / "invalid.mid", std::ios::binary);
    invalid << "not a MIDI file";
    invalid.close();

    midi::Project reloaded = before;
    check(!midi::loadMidiFile((dir / "invalid.mid").string(), reloaded), "failed import replaced project");
    check(before == reloaded, "failed import replaced project");
}

static void smpteMidiIsRejected(const std::filesystem::path& dir) {
    auto original = writeSourceMidiFixture(dir);
    std::ifstream input(original, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    bytes[12] = char(0xe7);
    bytes[13] = char(40);

    std::ofstream smpte(dir / "smpte.mid", std::ios::binary);
    smpte.write(bytes.data(), bytes.size());
    smpte.close();

    midi::Project reloaded;
    check(!midi::loadMidiFile((dir / "smpte.mid").string(), reloaded), "SMPTE silently converted to PPQ");
}

static void autosaveRecoversPreviousSession(const std::filesystem::path& dir) {
    App app;
    app.configureRecovery((dir / "recovery").string());
    app.executeCommand(std::make_unique<AddNotesCommand>(
        app, 0, std::vector<midi::Note>{makeNote(60, 0)}));
    check(app.autosave(), "autosave");

    App recovered;
    recovered.configureRecovery((dir / "recovery").string());
    check(recovered.recoveryFiles().size() == 1, "recover previous session");
    check(recovered.recover(recovered.recoveryFiles()[0]), "recover previous session");
    check(recovered.getProject().modified, "recovered state");
    check(recovered.getProject().tracks[0].notes.size() == 1, "recovered state");
}

// --- Project length and drum map ---

static void emptyProjectExtentFollowsLengthBars() {
    const int lengthBars = 32;

    midi::Project project;
    project.length_bars = lengthBars;
    check(project.getTotalTicks() == static_cast<uint32_t>(project.ticksPerBar()) * lengthBars,
          "length_bars sets empty project extent");
}

static void contentEndWinsOverShorterLengthBars() {
    const int contentBars = 40;
    const int shorterLengthBars = 16;

    midi::Project project;
    project.tracks.emplace_back();
    project.tracks[0].notes.push_back(
        makeNote(60, 0, static_cast<uint32_t>(project.ticksPerBar()) * contentBars));
    project.length_bars = shorterLengthBars;
    check(project.getTotalTicks() == static_cast<uint32_t>(project.ticksPerBar()) * contentBars,
          "content end wins over shorter length_bars");
}

static void drumMapRoundTripsLowMidHighPitch() {
    const float rowHeight = 18.0f;

    check(midi::drumYToPitch(midi::drumPitchToY(35, 0.0f, rowHeight, 0.0f), 0.0f, rowHeight, 0.0f) == 35,
          "drum map round-trip low pitch");
    check(midi::drumYToPitch(midi::drumPitchToY(42, 100.0f, rowHeight, 0.0f), 100.0f, rowHeight, 0.0f) == 42,
          "drum map round-trip mid pitch");
    check(midi::drumYToPitch(midi::drumPitchToY(81, 200.0f, rowHeight, 0.0f), 200.0f, rowHeight, 0.0f) == 81,
          "drum map round-trip high pitch");
}

static void setLengthBarsUpdatesLength() {
    const int newLengthBars = 64;

    App app;
    app.setLengthBars(newLengthBars);
    check(app.getLengthBars() == newLengthBars, "setLengthBars");
}

// --- Drum grooves and harmony ---

static void battleGrooveTilesAt480Ppq() {
    const int ticksPerQuarter = 480;
    const int bars = 2;
    const uint32_t barTwoKickTick = 1920;
    const uint32_t barOneSnareTick = 480;

    const auto& battle = midi::getDrumGroove(0);
    auto notes = midi::expandDrumGroove(battle, 0, bars, ticksPerQuarter);
    check(!notes.empty(), "battle groove produced no notes");

    bool kickBar1 = false;
    bool kickBar2 = false;
    bool snareBar1 = false;
    for (const auto& n : notes) {
        if (n.pitch == 36 && n.start_tick == 0) kickBar1 = true;
        if (n.pitch == 36 && n.start_tick == barTwoKickTick) kickBar2 = true;
        if (n.pitch == 38 && n.start_tick == barOneSnareTick) snareBar1 = true;
    }
    check(kickBar1 && kickBar2 && snareBar1, "battle groove tiling at 480 PPQ");
}

static void waltzGrooveStaysWithinOneBar() {
    const int ticksPerQuarter = 480;
    const uint32_t waltzBarTicks = 1440;

    const auto& waltz = midi::getDrumGroove(3);
    auto waltzNotes = midi::expandDrumGroove(waltz, 0, 1, ticksPerQuarter);
    check(waltz.beats_per_bar == 3, "waltz is 3/4");

    bool pastBarEnd = false;
    for (const auto& n : waltzNotes) {
        if (n.start_tick >= waltzBarTicks) pastBarEnd = true;
    }
    check(!pastBarEnd, "waltz bar length is 12 sixteenths");
}

static void diatonicThirdBelowInCMajor() {
    check(midi::harmonyPitch(60, midi::HarmonyKind::DiatonicThirdBelow, 0, false) == 57,
          "C major third below C");
    check(midi::harmonyPitch(64, midi::HarmonyKind::DiatonicThirdBelow, 0, false) == 60,
          "C major third below E");
    check(midi::harmonyPitch(67, midi::HarmonyKind::DiatonicThirdBelow, 0, false) == 64,
          "C major third below G");
}

static void addDrumTrackUsesChannel9() {
    App app;
    app.addDrumTrack();
    check(app.getProject().tracks.size() == 2, "addDrumTrack added track");
    check(midi::isDrumTrack(app.getProject().tracks[1]), "drum track uses channel 9");
    check(app.getProject().tracks[1].channel == 9, "drum track channel is 9");
}

static void insertDrumGrooveAddsNotes() {
    App app;
    app.addDrumTrack();
    app.insertDrumGroove(0, 1);
    check(app.getProject().tracks[1].notes.size() > 0, "insertDrumGroove added notes");
}

static void everyDrumGrooveProducesNotes() {
    const int ticksPerQuarter = 480;
    for (int i = 0; i < midi::drumGrooveCount(); ++i) {
        auto grooveNotes = midi::expandDrumGroove(midi::getDrumGroove(i), 0, 1, ticksPerQuarter);
        check(!grooveNotes.empty(), "drum groove produced notes");
    }
}

// --- Harmony stamps and grid ---

static void octaveFifthAndSixthBelowMiddleC() {
    check(midi::harmonyPitch(60, midi::HarmonyKind::Octave, 0, false) == 48, "octave below");
    check(midi::harmonyPitch(60, midi::HarmonyKind::FifthBelow, 0, false) == 53, "fifth below");
    check(midi::harmonyPitch(60, midi::HarmonyKind::DiatonicSixthBelow, 0, false) == 52,
          "diatonic sixth below C");
}

static void triadStampAddsCompanions() {
    midi::Note melody = makeNote(60, 0);
    auto triad = midi::buildStampedNotes(melody, midi::HarmonyKind::TriadBelow, 0, false);
    check(triad.size() >= 2, "triad stamp adds companions");
}

static void harmonizeSelectedNotesAddsCompanion() {
    App app;
    app.getProject().tracks[0].notes = {makeNote(60, 0), makeNote(64, 480)};
    app.getProject().tracks[0].notes[0].selected = true;
    app.setHarmonyKind(midi::HarmonyKind::Octave);
    app.harmonizeSelectedNotes();
    check(app.getProject().tracks[0].notes.size() == 3, "harmonize adds companion");
}

static void gridSnapTicksAndSnapToGrid() {
    const int ticksPerQuarter = 480;
    const uint32_t unsnappedTick = 125;
    const uint32_t sixteenthTick = 120;

    check(midi::gridSnapTicks(ticksPerQuarter, midi::GridSnap::Sixteenth) == sixteenthTick, "grid snap ticks");
    check(midi::snapToGrid(unsnappedTick, ticksPerQuarter, midi::GridSnap::Sixteenth) == sixteenthTick,
          "snap to grid");
}

// --- Mute, solo, and editing ---

static void mutedTrackIsExcludedFromPlayback() {
    midi::Project project;
    project.tracks.emplace_back();
    project.tracks.emplace_back();
    project.tracks[0].notes = {makeNote(60, 0, 480)};
    project.tracks[1].notes = {makeNote(64, 0, 480)};
    project.tracks[0].muted = true;

    auto messages = midi::schedulePlayback(project, {0, 480, true});
    bool mutedNote = false;
    for (const auto& message : messages) {
        if (isNoteOn(message) && message.data[1] == 60) mutedNote = true;
    }
    check(!mutedNote, "muted track excluded from playback");
}

static void soloExcludesNonSoloTracks() {
    midi::Project project;
    project.tracks.emplace_back();
    project.tracks.emplace_back();
    project.tracks[0].notes = {makeNote(60, 0, 480)};
    project.tracks[1].notes = {makeNote(64, 0, 480)};
    project.tracks[1].solo = true;

    auto messages = midi::schedulePlayback(project, {0, 480, true});
    bool hasMelody = false;
    bool hasBass = false;
    for (const auto& message : messages) {
        if (!isNoteOn(message)) continue;
        if (message.data[1] == 60) hasMelody = true;
        if (message.data[1] == 64) hasBass = true;
    }
    check(!hasMelody && hasBass, "solo excludes non-solo tracks");
}

static void resizeSelectedNotesExtendsDuration() {
    const uint32_t initialDuration = 480;
    const int resizeDelta = 240;
    const uint32_t expectedDuration = 720;

    App app;
    app.getProject().tracks[0].notes = {makeNote(60, 0, initialDuration)};
    app.getProject().tracks[0].notes[0].selected = true;
    app.resizeSelectedNotes(resizeDelta, true);
    check(app.getProject().tracks[0].notes[0].duration == expectedDuration, "resize selected notes");
}

static void pasteNotesAtPlayhead() {
    const uint32_t pasteTick = 960;

    App app;
    app.getProject().tracks[0].notes = {makeNote(60, 0, 480)};
    app.getProject().tracks[0].notes[0].selected = true;
    app.copySelectedNotes();
    app.setPlayheadTick(pasteTick);
    app.pasteNotes();
    check(app.getProject().tracks[0].notes.size() == 2, "paste notes at playhead");
}

static void changeInstrumentUpdatesProgram() {
    const int newProgram = 5;

    App app;
    app.getProject().tracks[0].notes = {makeNote(60, 0, 480)};
    app.executeCommand(std::make_unique<ChangeInstrumentCommand>(app, 0, newProgram));
    check(app.getProject().tracks[0].program == newProgram, "change instrument");
}

// --- Synth presets ---

static void rockOrganAndHarmonicaPresetsDiffer() {
    check(!midi::synthPresetsEqual(midi::getSynthPreset(18), midi::getSynthPreset(22)),
          "rock organ and harmonica presets differ");
}

static void pianoAndViolinPresetsDiffer() {
    check(!midi::synthPresetsEqual(midi::getSynthPreset(0), midi::getSynthPreset(40)),
          "piano and violin presets differ");
}

static void harmonicaUsesReedWaveform() {
    check(midi::getSynthPreset(22).waveform == midi::WaveformKind::Reed,
          "harmonica uses reed waveform");
}

static void programChangeStoresChannel() {
    const int harmonicaProgram = 22;

    midi::AudioSynth synth;
    if (!synth.init()) return;
    synth.programChange(0, harmonicaProgram);
    check(synth.getChannelProgram(0) == harmonicaProgram, "program change stores channel");
    synth.shutdown();
}

int main() {
    auto dir = std::filesystem::temp_directory_path() /
               ("midi-tests-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    try {
        playheadDoesNotDriftAtVariousFrameRates();
        loopOvershootWrapsPlayheadIntoRegion();
        loopBoundarySchedulesResetSpan();
        tempoMapConvertsTicksAndSeconds();

        schedulerEmitsTickZeroAndShortNotes();
        seekChasesSustainedNotes();
        retriggerSendsNoteOffBeforeNoteOn();
        loopEndExcludesNoteStartingAtBoundary();
        seekRestoresControllersBeforeSustainedNotes();

        undoMoveRestoresPitchAfterSortAndClamp();
        undoTrackAddRemoveRestoresNotes();
        undoGroupRestoresVelocityAfterCoalescedGestures();
        undoToSaveClearsModifiedAndKeepsPath(dir);
        redoAfterSaveMarksProjectDirty(dir);
        deleteSelectedNotesLeavesUnselected();
        deleteNotesCommandMatchesIdentityNotSelection();

        midiImportPreservesTracksAndMetadata(dir);
        midiRoundTripPreservesNotesEventsAndSilence(dir);
        midiSavePreservesEditorLoopMuteAndPan(dir);
        recoveryRoundTripEqualsProject(dir);
        failedAtomicWriteLeavesOriginalFile(dir);
        atomicWriteReplacesDestination(dir);
        riffImportLoadsWrappedMidi(dir);
        riffImportDoesNotOverwriteSiblingTemp(dir);
        failedImportLeavesProjectUnchanged(dir);
        smpteMidiIsRejected(dir);
        autosaveRecoversPreviousSession(dir);

        emptyProjectExtentFollowsLengthBars();
        contentEndWinsOverShorterLengthBars();
        drumMapRoundTripsLowMidHighPitch();
        setLengthBarsUpdatesLength();

        battleGrooveTilesAt480Ppq();
        waltzGrooveStaysWithinOneBar();
        diatonicThirdBelowInCMajor();
        addDrumTrackUsesChannel9();
        insertDrumGrooveAddsNotes();
        everyDrumGrooveProducesNotes();

        octaveFifthAndSixthBelowMiddleC();
        triadStampAddsCompanions();
        harmonizeSelectedNotesAddsCompanion();
        gridSnapTicksAndSnapToGrid();

        mutedTrackIsExcludedFromPlayback();
        soloExcludesNonSoloTracks();
        resizeSelectedNotesExtendsDuration();
        pasteNotesAtPlayhead();
        changeInstrumentUpdatesProgram();

        rockOrganAndHarmonicaPresetsDiffer();
        pianoAndViolinPresetsDiffer();
        harmonicaUsesReedWaveform();
        programChangeStoresChannel();

        std::filesystem::remove_all(dir);
        std::cout << "All core regression tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (fixtures: " << dir << ")\n";
        return 1;
    }
}
