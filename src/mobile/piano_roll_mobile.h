#pragma once

#include "../app.h"
#include "../midi/midi_player.h"
#include "touch_input.h"
#include <imgui.h>

class PianoRollMobile {
public:
    PianoRollMobile(App& app, midi::MidiPlayer& player);
    void render(float width, float height);
    void processGesture(const TouchGesture& gesture);
    void setScrollMode(bool enabled) { scrollMode_ = enabled; }
    bool isScrollMode() const { return scrollMode_; }

private:
    // Drawing
    void drawGrid(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
    void drawKeyboard(ImDrawList* drawList, ImVec2 pos, ImVec2 size);
    void drawNotes(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
    void drawPlayhead(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
    void drawLoopRegion(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);

    // Coordinate conversion
    float tickToX(uint32_t tick, ImVec2 canvasPos) const;
    uint32_t xToTick(float x, ImVec2 canvasPos) const;
    float pitchToY(int pitch, ImVec2 canvasPos) const;
    int yToPitch(float y, ImVec2 canvasPos) const;

    // Note hit testing (with larger touch targets)
    struct NoteHit {
        int noteIndex = -1;
        bool onRightEdge = false;
        bool onLeftEdge = false;
    };
    NoteHit hitTestNote(float touchX, float touchY, ImVec2 canvasPos, ImVec2 canvasSize);

    // Auto-follow playhead
    void autoFollowPlayhead(ImVec2 canvasPos, ImVec2 canvasSize);

    // Track color (reused from desktop logic)
    static ImU32 getTrackColor(int trackIndex, int velocity, bool isSelected, bool isActiveTrack);

    App& app_;
    midi::MidiPlayer& player_;

    // View state (larger defaults for mobile)
    float pixelsPerTick_ = 0.2f;       // Horizontal zoom (2x desktop)
    float noteHeight_ = 24.0f;         // Vertical zoom (2x desktop)
    float scrollX_ = 0.0f;             // Horizontal scroll in ticks
    float scrollY_ = 60.0f * 24.0f;    // Vertical scroll (start near middle C)

    // Layout constants
    static constexpr float KEYBOARD_WIDTH = 50.0f;

    // Touch interaction state
    enum class InteractionMode {
        None,
        Scrolling,
        CreatingNote,
        MovingNotes,
        ResizingNotes,
    };
    InteractionMode mode_ = InteractionMode::None;

    // Note creation
    int creatingNotePitch_ = -1;
    uint32_t creatingNoteStart_ = 0;

    // Note dragging
    int dragStartPitch_ = 0;
    uint32_t dragStartTick_ = 0;
    float dragStartX_ = 0.0f;
    float dragStartY_ = 0.0f;
    bool hasDragged_ = false;

    // Note resizing
    bool resizingFromRight_ = true;

    // Piano key preview
    int previewingPitch_ = -1;      // Currently previewing pitch (-1 = none)
    int previewingChannel_ = 0;     // Channel of the previewing note
    float previewNoteOffTimer_ = 0; // Countdown to auto note-off (seconds)

    // Scroll mode toggle
    bool scrollMode_ = false;

    // Canvas position cache (for gesture handling)
    ImVec2 canvasPos_ = {0, 0};
    ImVec2 canvasSize_ = {0, 0};
};
