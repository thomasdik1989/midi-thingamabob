#pragma once

#include "midi/types.h"
#include <memory>
#include <deque>
#include <functional>

// Forward declarations
class Command;

class App {
public:
    App();
    ~App();

    // File operations
    void newProject();
    bool loadFile(const std::string& filepath);
    bool saveFile();
    bool saveFileAs(const std::string& filepath);
    
    // Project access
    midi::Project& getProject() { return project_; }
    const midi::Project& getProject() const { return project_; }
    
    // Track management
    void addTrack();
    void removeTrack(int index);
    int getSelectedTrackIndex() const { return selectedTrack_; }
    void setSelectedTrack(int index);
    midi::Track* getSelectedTrack();
    
    // Playback state
    bool isPlaying() const { return playing_; }
    void setPlaying(bool playing) { playing_ = playing; }
    void togglePlayback() { playing_ = !playing_; }
    void stop();
    
    uint32_t getPlayheadTick() const { return playheadTick_; }
    void setPlayheadTick(uint32_t tick) { playheadTick_ = tick; }
    void advancePlayhead(double deltaSeconds);
    
    // Editing state
    midi::GridSnap getGridSnap() const { return gridSnap_; }
    void setGridSnap(midi::GridSnap snap) { gridSnap_ = snap; }
    
    // Undo/Redo system
    void executeCommand(std::unique_ptr<Command> cmd);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    
    // Note editing helpers
    void deleteSelectedNotes();
    void selectAllNotes();
    void copySelectedNotes();
    void pasteNotes();
    void quantizeSelectedNotes();
    
    // Clipboard
    bool hasClipboard() const { return !clipboard_.empty(); }
    
private:
    midi::Project project_;
    int selectedTrack_ = 0;
    
    // Playback
    bool playing_ = false;
    uint32_t playheadTick_ = 0;
    
    // Editing
    midi::GridSnap gridSnap_ = midi::GridSnap::Sixteenth;
    
    // Undo/Redo
    std::deque<std::unique_ptr<Command>> undoStack_;
    std::deque<std::unique_ptr<Command>> redoStack_;
    static const size_t MAX_UNDO_HISTORY = 100;
    
    // Clipboard (for copy/paste)
    std::vector<midi::Note> clipboard_;
    uint32_t clipboardBaseTime_ = 0;
};

// Command pattern for undo/redo
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string getName() const = 0;
};

// Add notes command
class AddNotesCommand : public Command {
public:
    AddNotesCommand(App& app, int trackIndex, std::vector<midi::Note> notes);
    void execute() override;
    void undo() override;
    std::string getName() const override { return "Add Notes"; }
    
private:
    App& app_;
    int trackIndex_;
    std::vector<midi::Note> notes_;
};

// Delete notes command
class DeleteNotesCommand : public Command {
public:
    DeleteNotesCommand(App& app, int trackIndex, std::vector<midi::Note> notes);
    void execute() override;
    void undo() override;
    std::string getName() const override { return "Delete Notes"; }
    
private:
    App& app_;
    int trackIndex_;
    std::vector<midi::Note> notes_;
};

// Move notes command
class MoveNotesCommand : public Command {
public:
    MoveNotesCommand(App& app, int trackIndex, std::vector<size_t> noteIndices, 
                     int pitchDelta, int32_t tickDelta);
    void execute() override;
    void undo() override;
    std::string getName() const override { return "Move Notes"; }
    
private:
    App& app_;
    int trackIndex_;
    std::vector<size_t> noteIndices_;
    int pitchDelta_;
    int32_t tickDelta_;
};

// Resize notes command
class ResizeNotesCommand : public Command {
public:
    ResizeNotesCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                       std::vector<uint32_t> oldDurations, std::vector<uint32_t> newDurations);
    void execute() override;
    void undo() override;
    std::string getName() const override { return "Resize Notes"; }
    
private:
    App& app_;
    int trackIndex_;
    std::vector<size_t> noteIndices_;
    std::vector<uint32_t> oldDurations_;
    std::vector<uint32_t> newDurations_;
};

// Change velocity command
class ChangeVelocityCommand : public Command {
public:
    ChangeVelocityCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                          std::vector<int> oldVelocities, std::vector<int> newVelocities);
    void execute() override;
    void undo() override;
    std::string getName() const override { return "Change Velocity"; }
    
private:
    App& app_;
    int trackIndex_;
    std::vector<size_t> noteIndices_;
    std::vector<int> oldVelocities_;
    std::vector<int> newVelocities_;
};

// Change track instrument command
class ChangeInstrumentCommand : public Command {
public:
    ChangeInstrumentCommand(App& app, int trackIndex, int oldProgram, int newProgram);
    void execute() override;
    void undo() override;
    std::string getName() const override { return "Change Instrument"; }
    
private:
    App& app_;
    int trackIndex_;
    int oldProgram_;
    int newProgram_;
};
