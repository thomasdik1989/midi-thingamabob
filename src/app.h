#pragma once

#include "midi/types.h"
#include "midi/harmony.h"
#include "midi/playback.h"
#include <memory>
#include <deque>

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

    void configureRecovery(const std::string& directory);
    bool autosave();
    bool recover(const std::string& path);
    void discardRecovery();
    const std::vector<std::string>& recoveryFiles() const { return recoveryFiles_; }

    // Project access
    midi::Project& getProject() { return project_; }
    const midi::Project& getProject() const { return project_; }

    // Track management
    void addTrack();
    void addDrumTrack();
    void removeTrack(int index);
    int getSelectedTrackIndex() const { return selectedTrack_; }
    void setSelectedTrack(int index);
    midi::Track* getSelectedTrack();

    // Playback state
    bool isPlaying() const { return playing_; }
    void setPlaying(bool playing) { if (playing && !playing_) playbackReset_ = true; playing_ = playing; }
    void togglePlayback() { setPlaying(!playing_); }
    void stop();

    uint32_t getPlayheadTick() const { return playheadTick_; }
    void setPlayheadTick(uint32_t tick) { playheadTick_ = tick; playheadPosition_ = tick; playbackReset_ = true; }
    const std::vector<midi::PlaybackSpan>& playbackSpans() const { return playbackSpans_; }
    void advancePlayhead(double deltaSeconds);

    // Editing state
    midi::GridSnap getGridSnap() const { return gridSnap_; }
    void setGridSnap(midi::GridSnap snap) { gridSnap_ = snap; }

    midi::HarmonyKind getHarmonyKind() const { return harmonyKind_; }
    void setHarmonyKind(midi::HarmonyKind kind) { harmonyKind_ = kind; }
    int getHarmonyTonic() const { return harmonyTonic_; }
    void setHarmonyTonic(int tonic) { harmonyTonic_ = ((tonic % 12) + 12) % 12; }
    bool getHarmonyMinor() const { return harmonyMinor_; }
    void setHarmonyMinor(bool minor) { harmonyMinor_ = minor; }

    void insertDrumGroove(int groove_index, int bar_count = 0);
    void harmonizeSelectedNotes();

    int getLengthBars() const { return project_.length_bars; }
    void setLengthBars(int bars);

    // An edit scope records exact before/after state, including track insertion/removal.
    // Selection and file path are not edits. A continuous gesture is one undo group.
    class Edit {
    public:
        explicit Edit(App& app);
        ~Edit();
        Edit(const Edit&) = delete;
        Edit& operator=(const Edit&) = delete;
    private:
        App& app_;
        midi::Project before_;
        int selection_;
    };
    Edit edit() { return Edit(*this); }
    void beginUndoGroup();
    void endUndoGroup();

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
    void moveSelectedNotes(int pitchDelta, int32_t tickDelta);
    void resizeSelectedNotes(int32_t tickDelta, bool fromRight);

    // Clipboard
    bool hasClipboard() const { return !clipboard_.empty(); }

private:
    std::string recoveryDirectory_;
    std::string recoveryPath_;
    std::vector<std::string> recoveryFiles_;
    midi::Project recoveryProject_;
    bool haveRecovery_ = false;
    midi::Project project_;
    int selectedTrack_ = 0;

    // Playback
    bool playing_ = false;
    uint32_t playheadTick_ = 0;
    double playheadPosition_ = 0;
    bool playbackReset_ = true;
    std::vector<midi::PlaybackSpan> playbackSpans_;

    // Editing
    midi::GridSnap gridSnap_ = midi::GridSnap::Sixteenth;
    midi::HarmonyKind harmonyKind_ = midi::HarmonyKind::Single;
    int harmonyTonic_ = 0;
    bool harmonyMinor_ = false;

    // Undo/Redo
    struct HistoryEntry {
        midi::Project before;
        midi::Project after;
        int selection_before;
        int selection_after;
    };
    std::deque<HistoryEntry> undoStack_;
    std::deque<HistoryEntry> redoStack_;
    midi::Project savedProject_;
    bool grouping_ = false;
    midi::Project groupBefore_;
    int groupSelection_ = 0;
    int editDepth_ = 0;
    void recordEdit(midi::Project before, int selection);
    void restoreHistory(const midi::Project& project, int selection);
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
};

class AddNotesCommand : public Command {
public:
    AddNotesCommand(App& app, int trackIndex, std::vector<midi::Note> notes);
    void execute() override;

private:
    App& app_;
    int trackIndex_;
    std::vector<midi::Note> notes_;
};

class DeleteNotesCommand : public Command {
public:
    DeleteNotesCommand(App& app, int trackIndex, std::vector<midi::Note> notes);
    void execute() override;

private:
    App& app_;
    int trackIndex_;
    std::vector<midi::Note> notes_;
};

class MoveNotesCommand : public Command {
public:
    MoveNotesCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                     int pitchDelta, int32_t tickDelta);
    void execute() override;

private:
    App& app_;
    int trackIndex_;
    std::vector<size_t> noteIndices_;
    int pitchDelta_;
    int32_t tickDelta_;
};

class ResizeNotesCommand : public Command {
public:
    ResizeNotesCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                       std::vector<uint32_t> newDurations);
    void execute() override;

private:
    App& app_;
    int trackIndex_;
    std::vector<size_t> noteIndices_;
    std::vector<uint32_t> newDurations_;
};

class ChangeVelocityCommand : public Command {
public:
    ChangeVelocityCommand(App& app, int trackIndex, std::vector<size_t> noteIndices,
                          std::vector<int> newVelocities);
    void execute() override;

private:
    App& app_;
    int trackIndex_;
    std::vector<size_t> noteIndices_;
    std::vector<int> newVelocities_;
};

class ChangeInstrumentCommand : public Command {
public:
    ChangeInstrumentCommand(App& app, int trackIndex, int newProgram);
    void execute() override;

private:
    App& app_;
    int trackIndex_;
    int newProgram_;
};

class QuantizeNotesCommand : public Command {
public:
    QuantizeNotesCommand(App& app, int trackIndex, int ticksPerQuarter, midi::GridSnap snap);
    void execute() override;

private:
    App& app_;
    int trackIndex_;
    int ticksPerQuarter_;
    midi::GridSnap snap_;
};
