#include "app.h"
#include "midi/midi_file.h"
#include <algorithm>

App::App() {
    newProject();
}

App::~App() = default;

void App::newProject() {
    project_ = midi::Project();
    project_.tracks.clear();
    
    // Create one default track
    midi::Track track;
    track.name = "Track 1";
    track.channel = 0;
    track.program = 0; // Acoustic Grand Piano
    project_.tracks.push_back(track);
    
    selectedTrack_ = 0;
    playheadTick_ = 0;
    playing_ = false;
    
    undoStack_.clear();
    redoStack_.clear();
    clipboard_.clear();
}

bool App::loadFile(const std::string& filepath) {
    midi::Project loadedProject;
    if (midi::loadMidiFile(filepath, loadedProject)) {
        project_ = std::move(loadedProject);
        project_.filepath = filepath;
        project_.modified = false;
        selectedTrack_ = project_.tracks.empty() ? -1 : 0;
        playheadTick_ = 0;
        playing_ = false;
        undoStack_.clear();
        redoStack_.clear();
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
    if (midi::saveMidiFile(filepath, project_)) {
        project_.filepath = filepath;
        project_.modified = false;
        return true;
    }
    return false;
}

void App::addTrack() {
    midi::Track track;
    track.name = "Track " + std::to_string(project_.tracks.size() + 1);
    track.channel = std::min(static_cast<int>(project_.tracks.size()), 15);
    track.program = 0;
    project_.tracks.push_back(track);
    selectedTrack_ = static_cast<int>(project_.tracks.size()) - 1;
    project_.modified = true;
}

void App::removeTrack(int index) {
    if (index >= 0 && index < static_cast<int>(project_.tracks.size())) {
        project_.tracks.erase(project_.tracks.begin() + index);
        if (selectedTrack_ >= static_cast<int>(project_.tracks.size())) {
            selectedTrack_ = static_cast<int>(project_.tracks.size()) - 1;
        }
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
    playheadTick_ = 0;
}

void App::advancePlayhead(double deltaSeconds) {
    if (!playing_) return;
    
    uint32_t deltaTicks = project_.secondsToTicks(deltaSeconds);
    playheadTick_ += deltaTicks;
    
    // Loop region support
    if (project_.loop_enabled && project_.loop_end > project_.loop_start) {
        if (playheadTick_ >= project_.loop_end) {
            playheadTick_ = project_.loop_start;
        }
    } else {
        // Loop back to start if we reach the end
        uint32_t totalTicks = project_.getTotalTicks();
        if (playheadTick_ > totalTicks) {
            playheadTick_ = 0;
        }
    }
}

void App::executeCommand(std::unique_ptr<Command> cmd) {
    cmd->execute();
    undoStack_.push_back(std::move(cmd));
    
    // Clear redo stack
    redoStack_.clear();
    
    // Limit undo history
    while (undoStack_.size() > MAX_UNDO_HISTORY) {
        undoStack_.pop_front();
    }
    
    project_.modified = true;
}

void App::undo() {
    if (!canUndo()) return;
    
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo();
    redoStack_.push_back(std::move(cmd));
    project_.modified = true;
}

void App::redo() {
    if (!canRedo()) return;
    
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->execute();
    undoStack_.push_back(std::move(cmd));
    project_.modified = true;
}

bool App::canUndo() const {
    return !undoStack_.empty();
}

bool App::canRedo() const {
    return !redoStack_.empty();
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
        newNotes.push_back(note);
    }
    
    auto cmd = std::make_unique<AddNotesCommand>(*this, selectedTrack_, newNotes);
    executeCommand(std::move(cmd));
}

void App::quantizeSelectedNotes() {
    auto* track = getSelectedTrack();
    if (!track || gridSnap_ == midi::GridSnap::None) return;
    
    for (auto& note : track->notes) {
        if (note.selected) {
            note.start_tick = midi::snapToGrid(note.start_tick, project_.ticks_per_quarter, gridSnap_);
        }
    }
    project_.modified = true;
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

void AddNotesCommand::undo() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (const auto& note : notes_) {
            auto it = std::find_if(trackNotes.begin(), trackNotes.end(), [&](const midi::Note& n) {
                return n.pitch == note.pitch && n.start_tick == note.start_tick && 
                       n.duration == note.duration && n.velocity == note.velocity;
            });
            if (it != trackNotes.end()) {
                trackNotes.erase(it);
            }
        }
    }
}

DeleteNotesCommand::DeleteNotesCommand(App& app, int trackIndex, std::vector<midi::Note> notes)
    : app_(app), trackIndex_(trackIndex), notes_(std::move(notes)) {}

void DeleteNotesCommand::execute() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (const auto& note : notes_) {
            auto it = std::find_if(trackNotes.begin(), trackNotes.end(), [&](const midi::Note& n) {
                return n.pitch == note.pitch && n.start_tick == note.start_tick && 
                       n.duration == note.duration;
            });
            if (it != trackNotes.end()) {
                trackNotes.erase(it);
            }
        }
    }
}

void DeleteNotesCommand::undo() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        for (const auto& note : notes_) {
            tracks[trackIndex_].notes.push_back(note);
        }
        tracks[trackIndex_].sortNotes();
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

void MoveNotesCommand::undo() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (size_t idx : noteIndices_) {
            if (idx < trackNotes.size()) {
                trackNotes[idx].pitch = std::clamp(trackNotes[idx].pitch - pitchDelta_, 0, 127);
                int32_t newTick = static_cast<int32_t>(trackNotes[idx].start_tick) - tickDelta_;
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

void ResizeNotesCommand::undo() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (size_t i = 0; i < noteIndices_.size(); ++i) {
            if (noteIndices_[i] < trackNotes.size() && i < oldDurations_.size()) {
                trackNotes[noteIndices_[i]].duration = oldDurations_[i];
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

void ChangeVelocityCommand::undo() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        auto& trackNotes = tracks[trackIndex_].notes;
        for (size_t i = 0; i < noteIndices_.size(); ++i) {
            if (noteIndices_[i] < trackNotes.size() && i < oldVelocities_.size()) {
                trackNotes[noteIndices_[i]].velocity = oldVelocities_[i];
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

void ChangeInstrumentCommand::undo() {
    auto& tracks = app_.getProject().tracks;
    if (trackIndex_ >= 0 && trackIndex_ < static_cast<int>(tracks.size())) {
        tracks[trackIndex_].program = oldProgram_;
    }
}
