#include "app.h"
#include "midi/midi_file.h"
#include "midi/patterns.h"
#include "midi/harmony.h"
#include "midi/recovery.h"
#include <filesystem>
#include <random>
#include <algorithm>
#include <cmath>

App::App() {
    newProject();
}

App::~App() = default;

void App::newProject() {
    discardRecovery();
    project_ = midi::Project();
    project_.tracks.clear();
    
    // Create one default track
    midi::Track track;
    track.name = "Track 1";
    track.channel = 0;
    track.program = 0; // Acoustic Grand Piano
    project_.tracks.push_back(track);
    
    selectedTrack_ = 0;
    setPlayheadTick(0);
    playing_ = false;
    
    undoStack_.clear();
    redoStack_.clear();
    clipboard_.clear();
    grouping_ = false;
    savedProject_ = project_;
}

bool App::loadFile(const std::string& filepath) {
    midi::Project loadedProject;
    if (midi::loadMidiFile(filepath, loadedProject)) {
        discardRecovery();
        project_ = std::move(loadedProject);
        project_.filepath = filepath;
        project_.modified = false;
        selectedTrack_ = project_.tracks.empty() ? -1 : 0;
        setPlayheadTick(0);
        playing_ = false;
        undoStack_.clear();
        redoStack_.clear();
        grouping_ = false;
        savedProject_ = project_;
        return true;
    }
    return false;
}

bool App::saveFile() {
    if (project_.filepath.empty()) {
        return false;
    }
    return saveFileAs(project_.filepath);
}

bool App::saveFileAs(const std::string& filepath) {
    endUndoGroup();
    if (midi::saveMidiFile(filepath, project_)) {
        project_.filepath = filepath;
        project_.modified = false;
        savedProject_ = project_;
        discardRecovery();
        return true;
    }
    return false;
}

void App::addTrack() {
    auto transaction = edit();
    midi::Track track;
    track.name = "Track " + std::to_string(project_.tracks.size() + 1);
    track.channel = std::min(static_cast<int>(project_.tracks.size()), 15);
    track.program = 0;
    project_.tracks.push_back(track);
    selectedTrack_ = static_cast<int>(project_.tracks.size()) - 1;
    project_.modified = true;
}

void App::addDrumTrack() {
    auto transaction = edit();
    midi::Track track;
    track.name = "Drums";
    track.channel = 9;
    track.program = 0;
    project_.tracks.push_back(track);
    selectedTrack_ = static_cast<int>(project_.tracks.size()) - 1;
    project_.modified = true;
}

void App::setLengthBars(int bars) {
    bars = std::clamp(bars, 1, 999);
    if (project_.length_bars == bars) return;

    auto transaction = edit();
    project_.length_bars = bars;
    const uint32_t length_ticks = static_cast<uint32_t>(project_.ticksPerBar()) * static_cast<uint32_t>(bars);
    if (project_.loop_end > length_ticks) {
        project_.loop_end = length_ticks;
        if (project_.loop_start >= project_.loop_end) {
            project_.loop_start = 0;
            project_.loop_end = 0;
            project_.loop_enabled = false;
        }
    }
    project_.modified = true;
}

void App::insertDrumGroove(int groove_index, int bar_count) {
    int drum_track_index = midi::drumTrackIndex(project_);
    if (drum_track_index < 0) {
        addDrumTrack();
        drum_track_index = midi::drumTrackIndex(project_);
    }
    if (drum_track_index < 0) return;

    uint32_t start_tick = playheadTick_;
    int bars = bar_count;
    if (bars <= 0) {
        if (project_.loop_enabled && project_.loop_end > project_.loop_start) {
            const uint32_t span = project_.loop_end - project_.loop_start;
            const uint32_t bar_ticks = static_cast<uint32_t>(project_.ticksPerBar());
            bars = bar_ticks > 0 ? static_cast<int>((span + bar_ticks - 1) / bar_ticks) : 4;
            start_tick = project_.loop_start;
        } else {
            const uint32_t bar_ticks = static_cast<uint32_t>(project_.ticksPerBar());
            const uint32_t length_ticks = static_cast<uint32_t>(project_.length_bars) * bar_ticks;
            if (length_ticks > playheadTick_ && bar_ticks > 0) {
                bars = static_cast<int>((length_ticks - playheadTick_ + bar_ticks - 1) / bar_ticks);
            } else {
                bars = project_.length_bars > 0 ? project_.length_bars : 32;
            }
        }
    }

    const midi::DrumGroove& groove = midi::getDrumGroove(groove_index);
    std::vector<midi::Note> notes = midi::expandDrumGroove(
        groove, start_tick, bars, project_.ticks_per_quarter);
    if (notes.empty()) return;

    auto cmd = std::make_unique<AddNotesCommand>(*this, drum_track_index, std::move(notes));
    executeCommand(std::move(cmd));
}

void App::harmonizeSelectedNotes() {
    if (harmonyKind_ == midi::HarmonyKind::Single) return;

    auto* track = getSelectedTrack();
    if (!track) return;

    std::vector<midi::Note> new_notes;
    for (const auto& note : track->notes) {
        if (!note.selected) continue;
        auto companions = midi::companionNotes(note, harmonyKind_, harmonyTonic_, harmonyMinor_);
        for (auto& companion : companions) {
            companion.selected = true;
            new_notes.push_back(companion);
        }
    }
    if (new_notes.empty()) return;

    auto cmd = std::make_unique<AddNotesCommand>(*this, selectedTrack_, std::move(new_notes));
    executeCommand(std::move(cmd));
}

void App::removeTrack(int index) {
    auto transaction = edit();
    if (index >= 0 && index < static_cast<int>(project_.tracks.size())) {
        project_.tracks.erase(project_.tracks.begin() + index);
        if (selectedTrack_ > index) --selectedTrack_;
        else if (selectedTrack_ == index) selectedTrack_ = std::min(index, static_cast<int>(project_.tracks.size()) - 1);
        project_.modified = true;
    }
}

void App::setSelectedTrack(int index) {
    if (index >= 0 && index < static_cast<int>(project_.tracks.size())) {
        selectedTrack_ = index;
    }
}

midi::Track* App::getSelectedTrack() {
    if (selectedTrack_ >= 0 && selectedTrack_ < static_cast<int>(project_.tracks.size())) {
        return &project_.tracks[selectedTrack_];
    }
    return nullptr;
}

void App::stop() {
    playing_ = false;
    setPlayheadTick(0);
}

void App::advancePlayhead(double deltaSeconds) {
    if (!playing_) return;
    
    playbackSpans_.clear();
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) return;
    const bool region = project_.loop_enabled && project_.loop_end > project_.loop_start;
    const double start = region ? project_.loop_start : 0;
    const double end = region ? project_.loop_end : project_.getTotalTicks();
    if (playheadPosition_ >= end) { playheadPosition_ = start; playbackReset_ = true; }
    double seconds = project_.tickPositionToSeconds(playheadPosition_);
    const double endSeconds = project_.tickPositionToSeconds(end);
    const double startSeconds = project_.tickPositionToSeconds(start);
    double remaining = deltaSeconds;
    while (remaining >= endSeconds - seconds && endSeconds > startSeconds) {
        playbackSpans_.push_back({playheadPosition_, end, playbackReset_, false});
        remaining -= endSeconds - seconds;
        playheadPosition_ = start;
        seconds = startSeconds;
        playbackReset_ = true;
        // Avoid unbounded work after a long suspend. Resume at the correct phase.
        if (playbackSpans_.size() >= 1024) remaining = std::fmod(remaining, endSeconds - startSeconds);
    }
    double next = project_.secondsToTickPosition(seconds + remaining);
    playbackSpans_.push_back({playheadPosition_, next, playbackReset_});
    playheadPosition_ = next;
    playheadTick_ = static_cast<uint32_t>(std::min(double(UINT32_MAX), next + 1e-8));
    playbackReset_ = false;
}

App::Edit::Edit(App& app) : app_(app), selection_(app.selectedTrack_) {
    ++app_.editDepth_;
    if (!app_.grouping_) {
        before_ = app.project_;
    }
}

App::Edit::~Edit() {
    if (--app_.editDepth_ == 0) {
        if (app_.grouping_) {
            app_.project_.modified = !(app_.project_ == app_.savedProject_);
            app_.playbackReset_ = true;
        } else {
            app_.recordEdit(std::move(before_), selection_);
        }
    }
}

void App::recordEdit(midi::Project before, int selection) {
    if (before == project_) { project_.modified = !(project_ == savedProject_); return; }
    project_.modified = !(project_ == savedProject_);
    playbackReset_ = true;
    if (grouping_) return;
    undoStack_.push_back({std::move(before), project_, selection, selectedTrack_});
    redoStack_.clear();
    while (undoStack_.size() > MAX_UNDO_HISTORY) undoStack_.pop_front();
}

void App::beginUndoGroup() {
    if (grouping_) return;
    groupBefore_ = project_;
    groupSelection_ = selectedTrack_;
    grouping_ = true;
}

void App::endUndoGroup() {
    if (!grouping_) return;
    grouping_ = false;
    recordEdit(std::move(groupBefore_), groupSelection_);
}

void App::executeCommand(std::unique_ptr<Command> cmd) {
    auto transaction = edit();
    cmd->execute();
}

void App::restoreHistory(const midi::Project& project, int selection) {
    const auto path = project_.filepath;
    project_ = project;
    project_.filepath = path;
    selectedTrack_ = selection;
    project_.modified = !(project_ == savedProject_);
    playbackReset_ = true;
}

void App::undo() {
    endUndoGroup();
    if (!canUndo()) return;
    auto entry = std::move(undoStack_.back());
    undoStack_.pop_back();
    restoreHistory(entry.before, entry.selection_before);
    redoStack_.push_back(std::move(entry));
}

void App::redo() {
    endUndoGroup();
    if (!canRedo()) return;
    auto entry = std::move(redoStack_.back());
    redoStack_.pop_back();
    restoreHistory(entry.after, entry.selection_after);
    undoStack_.push_back(std::move(entry));
}

bool App::canUndo() const {
    return !undoStack_.empty();
}

bool App::canRedo() const {
    return !redoStack_.empty();
}

namespace {
class QuantizeNotesCommand final : public Command {
public:
    QuantizeNotesCommand(App& app, int trackIndex, int ticksPerQuarter, midi::GridSnap snap)
        : app_(app), trackIndex_(trackIndex), ticksPerQuarter_(ticksPerQuarter), snap_(snap) {}
    void execute() override {
        auto& tracks = app_.getProject().tracks;
        if (trackIndex_ < 0 || trackIndex_ >= static_cast<int>(tracks.size())) return;
        for (auto& note : tracks[trackIndex_].notes) {
            if (note.selected) {
                note.start_tick = midi::snapToGrid(note.start_tick, ticksPerQuarter_, snap_);
            }
        }
        tracks[trackIndex_].sortNotes();
    }
    std::string getName() const override { return "Quantize Notes"; }
private:
    App& app_;
    int trackIndex_;
    int ticksPerQuarter_;
    midi::GridSnap snap_;
};
}

void App::deleteSelectedNotes() {
    auto* track = getSelectedTrack();
    if (!track) return;
    
    std::vector<midi::Note> deleted;
    for (const auto& note : track->notes) {
        if (note.selected) {
            deleted.push_back(note);
        }
    }
    
    if (!deleted.empty()) {
        auto cmd = std::make_unique<DeleteNotesCommand>(*this, selectedTrack_, deleted);
        executeCommand(std::move(cmd));
    }
}

void App::selectAllNotes() {
    auto* track = getSelectedTrack();
    if (!track) return;
    
    for (auto& note : track->notes) {
        note.selected = true;
    }
}

void App::copySelectedNotes() {
    auto* track = getSelectedTrack();
    if (!track) return;
    
    clipboard_.clear();
    clipboardBaseTime_ = UINT32_MAX;
    
    for (const auto& note : track->notes) {
        if (note.selected) {
            clipboard_.push_back(note);
            if (note.start_tick < clipboardBaseTime_) {
                clipboardBaseTime_ = note.start_tick;
            }
        }
    }
}

void App::pasteNotes() {
    if (clipboard_.empty()) return;
    auto* track = getSelectedTrack();
    if (!track) return;
    
    // Clear current selection
    track->clearSelection();
    
    // Paste at playhead position
    std::vector<midi::Note> newNotes;
    for (auto note : clipboard_) {
        note.start_tick = playheadTick_ + (note.start_tick - clipboardBaseTime_);
        note.selected = true;
        note.channel = -1; // Pasting uses the destination track's routing.
        note.start_sequence = note.end_sequence = 0;
        newNotes.push_back(note);
    }
    
    auto cmd = std::make_unique<AddNotesCommand>(*this, selectedTrack_, newNotes);
    executeCommand(std::move(cmd));
}

void App::quantizeSelectedNotes() {
    if (gridSnap_ == midi::GridSnap::None) return;
    executeCommand(std::make_unique<QuantizeNotesCommand>(
        *this, selectedTrack_, project_.ticks_per_quarter, gridSnap_));
}

// Command implementations

AddNotesCommand::AddNotesCommand(App& app, int trackIndex, std::vector<midi::Note> notes)
    : app_(app), trackIndex_(trackIndex), notes_(std::move(notes)) {}

void AddNotesCommand::execute() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        for (const auto& note : notes_) {
            tracks[trackIndex_].notes.push_back(note);
        }
        tracks[trackIndex_].sortNotes();
    }
}

DeleteNotesCommand::DeleteNotesCommand(App& app, int trackIndex, std::vector<midi::Note> notes)
    : app_(app), trackIndex_(trackIndex), notes_(std::move(notes)) {}

void DeleteNotesCommand::execute() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& notes = tracks[trackIndex_].notes;
        notes.erase(std::remove_if(notes.begin(), notes.end(), [](const midi::Note& note) {
            return note.selected;
        }), notes.end());
    }
}

MoveNotesCommand::MoveNotesCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                                   int pitchDelta, int32_t tickDelta)
    : app_(app), trackIndex_(trackIndex), noteIndices_(std::move(noteIndices)),
      pitchDelta_(pitchDelta), tickDelta_(tickDelta) {}

void MoveNotesCommand::execute() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (size_t idx : noteIndices_) {
            if (idx < trackNotes.size()) {
                trackNotes[idx].pitch = std::clamp(trackNotes[idx].pitch + pitchDelta_, 0, 127);
                int32_t newTick = static_cast<int32_t>(trackNotes[idx].start_tick) + tickDelta_;
                trackNotes[idx].start_tick = static_cast<uint32_t>(std::max(0, newTick));
            }
        }
        tracks[trackIndex_].sortNotes();
    }
}

ResizeNotesCommand::ResizeNotesCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                                       std::vector<uint32_t> oldDurations, std::vector<uint32_t> newDurations)
    : app_(app), trackIndex_(trackIndex), noteIndices_(std::move(noteIndices)),
      oldDurations_(std::move(oldDurations)), newDurations_(std::move(newDurations)) {}

void ResizeNotesCommand::execute() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (size_t i = 0; i < noteIndices_.size(); ++i) {
            if (noteIndices_[i] < trackNotes.size() && i < newDurations_.size()) {
                trackNotes[noteIndices_[i]].duration = newDurations_[i];
            }
        }
    }
}

ChangeVelocityCommand::ChangeVelocityCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                                             std::vector<int> oldVelocities, std::vector<int> newVelocities)
    : app_(app), trackIndex_(trackIndex), noteIndices_(std::move(noteIndices)),
      oldVelocities_(std::move(oldVelocities)), newVelocities_(std::move(newVelocities)) {}

void ChangeVelocityCommand::execute() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (size_t i = 0; i < noteIndices_.size(); ++i) {
            if (noteIndices_[i] < trackNotes.size() && i < newVelocities_.size()) {
                trackNotes[noteIndices_[i]].velocity = newVelocities_[i];
            }
        }
    }
}

ChangeInstrumentCommand::ChangeInstrumentCommand(App& app, int trackIndex, int oldProgram, int newProgram)
    : app_(app), trackIndex_(trackIndex), oldProgram_(oldProgram), newProgram_(newProgram) {}

void ChangeInstrumentCommand::execute() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        tracks[trackIndex_].program = newProgram_;
    }
}

void App::configureRecovery(const std::string& directory) {
    recoveryDirectory_ = directory;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return;
    recoveryFiles_.clear();
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (entry.path().extension() == ".recovery") recoveryFiles_.push_back(entry.path().string());
    }
    recoveryPath_ = (std::filesystem::path(directory) /
        ("session-" + std::to_string(std::random_device{}()) + ".recovery")).string();
}

bool App::autosave() {
    if (!project_.modified) return true;
    if (recoveryPath_.empty()) return false;
    if (haveRecovery_ && project_ == recoveryProject_) return true;
    if (!midi::saveRecovery(recoveryPath_, project_)) return false;
    recoveryProject_ = project_;
    haveRecovery_ = true;
    return true;
}

void App::discardRecovery() {
    if (!recoveryPath_.empty()) {
        std::error_code error;
        std::filesystem::remove(recoveryPath_, error);
    }
    haveRecovery_ = false;
}

bool App::recover(const std::string& path) {
    midi::Project recovered;
    if (!midi::loadRecovery(path, recovered)) return false;
    project_ = std::move(recovered);
    selectedTrack_ = 0;
    stop();
    undoStack_.clear(); redoStack_.clear(); grouping_ = false;
    // Preserve recovery until a new atomic recovery snapshot has succeeded.
    if (autosave()) {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    return true;
}
