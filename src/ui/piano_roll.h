#pragma once

#include "../app.h"
#include "../midi/midi_player.h"
#include <imgui.h>

class PianoRoll {
public:
    PianoRoll(App& app, midi::MidiPlayer& player);
    
    void render();
    
private:
    // Drawing
    void drawGrid(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
    void drawKeyboard(ImDrawList* drawList, ImVec2 pos, ImVec2 size);
    void drawNotes(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
    void drawPlayhead(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
    void drawSelectionBox(ImDrawList* drawList, ImVec2 canvasPos);
    
    // Interaction
    void handleInput(ImVec2 canvasPos, ImVec2 canvasSize);
    void handleNoteCreation(ImVec2 canvasPos, ImVec2 canvasSize);
    void handleNoteSelection(ImVec2 canvasPos, ImVec2 canvasSize);
    void handleNoteDragging(ImVec2 canvasPos, ImVec2 canvasSize);
    void handleNoteResizing(ImVec2 canvasPos, ImVec2 canvasSize);
    void handleScrollAndZoom(ImVec2 canvasPos, ImVec2 canvasSize);
    
    // Coordinate conversion
    float tickToX(uint32_t tick, ImVec2 canvasPos, ImVec2 canvasSize) const;
    uint32_t xToTick(float x, ImVec2 canvasPos, ImVec2 canvasSize) const;
    float pitchToY(int pitch, ImVec2 canvasPos, ImVec2 canvasSize) const;
    int yToPitch(float y, ImVec2 canvasPos, ImVec2 canvasSize) const;
    
    // Note hit testing
    struct NoteHit {
        int noteIndex = -1;
        bool onLeftEdge = false;
        bool onRightEdge = false;
    };
    NoteHit hitTestNote(ImVec2 mousePos, ImVec2 canvasPos, ImVec2 canvasSize);
    
    // Utility
    ImU32 velocityToColor(int velocity) const;
    
    App& app_;
    midi::MidiPlayer& player_;
    
    // View state
    float pixelsPerTick_ = 0.1f;    // Horizontal zoom
    float noteHeight_ = 12.0f;      // Vertical zoom (pixels per semitone)
    float scrollX_ = 0.0f;          // Horizontal scroll in ticks
    float scrollY_ = 60.0f * noteHeight_; // Vertical scroll (start around middle C)
    
    // Keyboard width
    static constexpr float KEYBOARD_WIDTH = 80.0f;
    
    // Interaction state
    enum class InteractionMode {
        None,
        CreatingNote,
        SelectingBox,
        MovingNotes,
        ResizingNotes
    };
    InteractionMode mode_ = InteractionMode::None;
    
    // Note creation
    int creatingNotePitch_ = -1;
    uint32_t creatingNoteStart_ = 0;
    uint32_t creatingNoteEnd_ = 0;
    
    // Box selection
    ImVec2 selectionStart_;
    ImVec2 selectionEnd_;
    
    // Note dragging
    int dragStartPitch_ = 0;
    uint32_t dragStartTick_ = 0;
    ImVec2 dragStartMouse_;
    bool hasDragged_ = false;
    
    // Note resizing
    bool resizingFromRight_ = true;
    std::vector<uint32_t> originalDurations_;
    
    // Keyboard interaction
    int previewingPitch_ = -1;
};
