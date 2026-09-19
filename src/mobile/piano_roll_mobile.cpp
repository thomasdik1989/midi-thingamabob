#include "piano_roll_mobile.h"
#include "../midi/harmony.h"
#include "../midi/patterns.h"
#include "nine_slice.h"
#include "../midi/piano_roll_view.h"
#include "../midi/types.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
std::string truncateLabel(const char* text, float max_width) {
    if (!text || max_width <= 0.0f) return {};
    if (ImGui::CalcTextSize(text).x <= max_width) return text;
    std::string out(text);
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > max_width) {
        out.pop_back();
    }
    out += "...";
    return out;
}
}

PianoRollMobile::PianoRollMobile(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void PianoRollMobile::render(float width, float height) {
    ImVec2 windowPos = ImGui::GetCursorScreenPos();

    // Ruler strip at the top for bar numbers
    ImVec2 rulerPos(windowPos.x + KEYBOARD_WIDTH, windowPos.y);
    ImVec2 rulerSize(width - KEYBOARD_WIDTH, RULER_HEIGHT);

    // Keyboard area (below ruler)
    ImVec2 keyboardPos(windowPos.x, windowPos.y + RULER_HEIGHT);
    ImVec2 keyboardSize(KEYBOARD_WIDTH, height - RULER_HEIGHT);

    // Grid area (right of keyboard, below ruler)
    canvasPos_ = ImVec2(windowPos.x + KEYBOARD_WIDTH, windowPos.y + RULER_HEIGHT);
    canvasSize_ = ImVec2(width - KEYBOARD_WIDTH, height - RULER_HEIGHT);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    if (theme_ && theme_->hasPanel()) {
        DrawNineSlice(drawList, theme_->panel, windowPos, ImVec2(width, height));
    } else {
        drawList->AddRectFilled(windowPos, ImVec2(windowPos.x + width, windowPos.y + height),
                               IM_COL32(30, 30, 35, 255));
    }

    // Draw components
    drawRuler(drawList, rulerPos, rulerSize);
    drawGrid(drawList, canvasPos_, canvasSize_);
    drawKeyboard(drawList, keyboardPos, keyboardSize);
    drawLoopRegion(drawList, canvasPos_, canvasSize_);
    drawNotes(drawList, canvasPos_, canvasSize_);
    drawPlayhead(drawList, canvasPos_, canvasSize_);

    int selectedTrack = app_.getSelectedTrackIndex();
    if (selectedTrack != lastSelectedTrack_) {
        if (auto* track = app_.getSelectedTrack(); track && midi::isDrumTrack(*track)) {
            focusDrumMap(canvasPos_, canvasSize_);
        }
        lastSelectedTrack_ = selectedTrack;
    }

    // Auto-follow playhead during playback
    if (app_.isPlaying()) {
        autoFollowPlayhead(canvasPos_, canvasSize_);
    }

    // Tick down the preview note-off timer
    if (previewingPitch_ >= 0 && previewNoteOffTimer_ > 0) {
        previewNoteOffTimer_ -= ImGui::GetIO().DeltaTime;
        if (previewNoteOffTimer_ <= 0) {
            player_.previewNoteOff(previewingChannel_, previewingPitch_);
            previewingPitch_ = -1;
            previewNoteOffTimer_ = 0;
        }
    }
}

midi::PianoRollView PianoRollMobile::viewState() const {
    midi::PianoRollView view;
    view.pixels_per_tick = pixelsPerTick_;
    view.note_height = noteHeight_;
    view.drum_row_height = drumRowHeight_;
    view.scroll_x = scrollX_;
    view.scroll_y = scrollY_;
    view.use_drum_map = useDrumMap();
    return view;
}

bool PianoRollMobile::inPianoRollArea(const TouchGesture& gesture) const {
    float area_left = canvasPos_.x - KEYBOARD_WIDTH;
    float area_right = canvasPos_.x + canvasSize_.x;
    float area_top = canvasPos_.y - RULER_HEIGHT;
    float area_bottom = canvasPos_.y + canvasSize_.y;
    return gesture.x >= area_left && gesture.x <= area_right &&
           gesture.y >= area_top && gesture.y <= area_bottom;
}

void PianoRollMobile::processGesture(const TouchGesture& gesture) {
    switch (gesture.type) {
        case GestureType::Tap: handleTapGesture(gesture); break;
        case GestureType::LongPress: handleLongPressGesture(gesture); break;
        case GestureType::Drag: handleDragGesture(gesture); break;
        case GestureType::Pinch: handlePinchGesture(gesture); break;
        default: break;
    }
}

void PianoRollMobile::handleTapGesture(const TouchGesture& gesture) {
            // Stop any currently previewing note first
            if (previewingPitch_ >= 0) {
                player_.previewNoteOff(previewingChannel_, previewingPitch_);
                previewingPitch_ = -1;
                previewNoteOffTimer_ = 0;
            }

            if (!inPianoRollArea(gesture)) return;

            float ruler_top = canvasPos_.y - RULER_HEIGHT;
            if (gesture.y >= ruler_top && gesture.y < canvasPos_.y &&
                gesture.x >= canvasPos_.x) {
                uint32_t tick = xToTick(gesture.x, canvasPos_);
                app_.setPlayheadTick(tick);
                return;
            }

            // Check if tap is on the keyboard (left of grid)
            float keyboardLeft = canvasPos_.x - KEYBOARD_WIDTH;
            if (gesture.x >= keyboardLeft && gesture.x < canvasPos_.x) {
                // Tap on piano key: preview the note
                int pitch = yToPitch(gesture.y, canvasPos_);
                auto* track = app_.getSelectedTrack();
                if (track && pitch >= 0 && pitch <= 127) {
                    previewingPitch_ = pitch;
                    previewingChannel_ = track->channel;
                    player_.previewNoteOn(track->channel, pitch, 100);
                    previewNoteOffTimer_ = 0.3f;  // Auto note-off after 300ms
                }
                return;
            }

            // In scroll mode, ignore taps on the grid (no note creation/selection)
            if (scrollMode_) return;

            // Single tap on grid: if on a note, select it; if on empty space, create a note
            auto hit = hitTestNote(gesture.x, gesture.y, canvasPos_, canvasSize_);

            if (hit.noteIndex >= 0) {
                // Tap on note: select/deselect
                auto* track = app_.getSelectedTrack();
                if (track) {
                    app_.getProject().clearAllSelections();
                    track->notes[hit.noteIndex].selected = true;
                }
            } else if (gesture.x >= canvasPos_.x) {
                // Tap on empty space: create a note
                int pitch = yToPitch(gesture.y, canvasPos_);
                uint32_t tick = xToTick(gesture.x, canvasPos_);
                tick = midi::snapToGrid(tick, app_.getProject().ticks_per_quarter, app_.getGridSnap());

                auto* track = app_.getSelectedTrack();
                if (track && pitch >= 0 && pitch <= 127) {
                    app_.getProject().clearAllSelections();

                    midi::Note newNote;
                    newNote.pitch = pitch;
                    newNote.velocity = 100;
                    newNote.start_tick = tick;
                    // Default duration: one grid unit
                    int gridTicks = app_.getProject().ticks_per_quarter;
                    if (app_.getGridSnap() != midi::GridSnap::None) {
                        gridTicks = app_.getProject().ticks_per_quarter * 4 / static_cast<int>(app_.getGridSnap());
                    }
                    newNote.duration = gridTicks;
                    newNote.selected = true;

                    auto notes = midi::buildStampedNotes(
                        newNote, app_.getHarmonyKind(), app_.getHarmonyTonic(), app_.getHarmonyMinor());
                    auto cmd = std::make_unique<AddNotesCommand>(
                        app_, app_.getSelectedTrackIndex(), std::move(notes));
                    app_.executeCommand(std::move(cmd));

                    // Preview the note with auto note-off
                    previewingPitch_ = pitch;
                    previewingChannel_ = track->channel;
                    player_.previewNoteOn(track->channel, pitch, 100);
                    previewNoteOffTimer_ = 0.2f;  // Auto note-off after 200ms
                }
            }
}

void PianoRollMobile::handleLongPressGesture(const TouchGesture& gesture) {
            if (!inPianoRollArea(gesture) || scrollMode_) return;

            // Long press on note: start resizing
            auto hit = hitTestNote(gesture.x, gesture.y, canvasPos_, canvasSize_);
            if (hit.noteIndex >= 0) {
                auto* track = app_.getSelectedTrack();
                if (track) {
                    if (!track->notes[hit.noteIndex].selected) {
                        app_.getProject().clearAllSelections();
                        track->notes[hit.noteIndex].selected = true;
                    }
                    mode_ = InteractionMode::ResizingNotes;
                    resizingFromRight_ = true;
                    dragStartX_ = gesture.x;
                }
            } else {
                // Long press on empty: delete selected notes
                // We may need different UX for this.
                app_.deleteSelectedNotes();
            }
}

void PianoRollMobile::handleDragGesture(const TouchGesture& gesture) {
            if (gesture.fingerCount == 1) {
                if (mode_ == InteractionMode::MovingNotes) {
                    // Continue moving selected notes
                    auto* track = app_.getSelectedTrack();
                    if (track && (std::abs(gesture.deltaX) > 1 || std::abs(gesture.deltaY) > 1)) {
                        hasDragged_ = true;
                    }

                    if (gesture.ended && hasDragged_) {
                        // Apply the move
                        int currentPitch = yToPitch(gesture.y, canvasPos_);
                        uint32_t currentTick = xToTick(gesture.x, canvasPos_);

                        int pitchDelta = currentPitch - dragStartPitch_;
                        int32_t tickDelta = static_cast<int32_t>(currentTick) - static_cast<int32_t>(dragStartTick_);

                        if (app_.getGridSnap() != midi::GridSnap::None) {
                            int gridSize = app_.getProject().ticks_per_quarter * 4 / static_cast<int>(app_.getGridSnap());
                            if (gridSize > 0) tickDelta = (tickDelta / gridSize) * gridSize;
                        }

                        if (pitchDelta != 0 || tickDelta != 0) {
                            app_.moveSelectedNotes(pitchDelta, tickDelta);
                        }
                        mode_ = InteractionMode::None;
                    }

                } else if (mode_ == InteractionMode::ResizingNotes) {
                    // Resize selected notes by dragging
                    auto* track = app_.getSelectedTrack();
                    if (track && gesture.ended) {
                        std::vector<size_t> indices;
                        for (size_t i = 0; i < track->notes.size(); ++i) {
                            if (track->notes[i].selected) indices.push_back(i);
                        }
                        if (!indices.empty()) {
                            uint32_t currentTick = xToTick(gesture.x, canvasPos_);
                            currentTick = midi::snapToGrid(currentTick, app_.getProject().ticks_per_quarter, app_.getGridSnap());
                            uint32_t startTick = xToTick(dragStartX_, canvasPos_);
                            int32_t tickDelta = static_cast<int32_t>(currentTick) - static_cast<int32_t>(startTick);
                            if (tickDelta != 0) {
                                app_.resizeSelectedNotes(tickDelta, resizingFromRight_);
                            }
                        }
                        mode_ = InteractionMode::None;
                    }

                } else if (mode_ == InteractionMode::Scrolling) {
                    // One-finger pan: apply drag delta to scroll position
                    scrollX_ -= gesture.deltaX / pixelsPerTick_;
                    scrollY_ -= gesture.deltaY;
                    scrollX_ = std::max(0.0f, scrollX_);
                    float maxScrollY = useDrumMap()
                        ? static_cast<float>(midi::DRUM_PITCH_COUNT) * drumRowHeight_ - canvasSize_.y
                        : 127.0f * noteHeight_ - canvasSize_.y;
                    scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, maxScrollY));

                    if (gesture.ended) {
                        mode_ = InteractionMode::None;
                    }

                } else if (mode_ == InteractionMode::None) {
                    float startX = gesture.x - gesture.deltaX;
                    float startY = gesture.y - gesture.deltaY;
                    TouchGesture start_gesture = gesture;
                    start_gesture.x = startX;
                    start_gesture.y = startY;
                    if (!inPianoRollArea(start_gesture)) return;

                    if (scrollMode_) {
                        mode_ = InteractionMode::Scrolling;
                        return;
                    }

                    // Edit mode: check if drag starts on a note
                    auto hit = hitTestNote(startX, startY, canvasPos_, canvasSize_);

                    if (hit.noteIndex >= 0) {
                        auto* track = app_.getSelectedTrack();
                        if (track) {
                            // Select the note if not already selected
                            if (!track->notes[hit.noteIndex].selected) {
                                app_.getProject().clearAllSelections();
                                track->notes[hit.noteIndex].selected = true;
                            }

                            if (hit.onRightEdge) {
                                // Drag started on right edge -> resize from right
                                mode_ = InteractionMode::ResizingNotes;
                                resizingFromRight_ = true;
                                dragStartX_ = startX;
                            } else if (hit.onLeftEdge) {
                                // Drag started on left edge -> resize from left
                                mode_ = InteractionMode::ResizingNotes;
                                resizingFromRight_ = false;
                                dragStartX_ = startX;
                            } else if (track->notes[hit.noteIndex].selected) {
                                // Drag started in center of selected note -> move
                                mode_ = InteractionMode::MovingNotes;
                                dragStartPitch_ = yToPitch(startY, canvasPos_);
                                dragStartTick_ = xToTick(startX, canvasPos_);
                                dragStartX_ = startX;
                                dragStartY_ = startY;
                                hasDragged_ = false;
                            }
                        }
                    } else {
                        // Drag started on empty space -> scroll/pan
                        mode_ = InteractionMode::Scrolling;
                    }
                }
            }
}

void PianoRollMobile::handlePinchGesture(const TouchGesture& gesture) {
            if (gesture.began) {
                // Start pinch-to-zoom
            } else if (gesture.active) {
                // Apply zoom
                float scale = gesture.pinchScale;
                if (scale != 1.0f) {
                    // Horizontal zoom (pinch scale maps to pixels-per-tick)
                    float oldPPT = pixelsPerTick_;
                    pixelsPerTick_ = std::clamp(pixelsPerTick_ * scale, 0.02f, 1.0f);

                    // Keep zoom centered on pinch center
                    float centerTick = scrollX_ + (gesture.pinchCenterX - canvasPos_.x) / oldPPT;
                    scrollX_ = centerTick - (gesture.pinchCenterX - canvasPos_.x) / pixelsPerTick_;

                    // Also zoom vertically
                    float oldNH = noteHeight_;
                    noteHeight_ = std::clamp(noteHeight_ * scale, 10.0f, 48.0f);

                    float centerPitch = (canvasPos_.y + canvasSize_.y * 0.5f - canvasPos_.y + scrollY_) / oldNH;
                    scrollY_ = centerPitch * noteHeight_ - canvasSize_.y * 0.5f;
                }

                // Two-finger pan
                if (gesture.deltaX != 0 || gesture.deltaY != 0) {
                    scrollX_ -= gesture.deltaX / pixelsPerTick_;
                    scrollY_ -= gesture.deltaY;
                }
            }

            // Clamp scroll
            scrollX_ = std::max(0.0f, scrollX_);
            float maxScrollY = useDrumMap()
                ? static_cast<float>(midi::DRUM_PITCH_COUNT) * drumRowHeight_ - canvasSize_.y
                : 127.0f * noteHeight_ - canvasSize_.y;
            scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, maxScrollY));
}

// ========== Drawing ==========

void PianoRollMobile::drawRuler(ImDrawList* drawList, ImVec2 rulerPos, ImVec2 rulerSize) {
    const auto& project = app_.getProject();

    // Ruler background (slightly lighter than grid)
    drawList->AddRectFilled(rulerPos,
        ImVec2(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y),
        IM_COL32(38, 38, 45, 255));

    // Bottom border
    drawList->AddLine(
        ImVec2(rulerPos.x, rulerPos.y + rulerSize.y),
        ImVec2(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y),
        IM_COL32(60, 60, 70, 255));

    drawList->PushClipRect(rulerPos,
        ImVec2(rulerPos.x + rulerSize.x, rulerPos.y + rulerSize.y), true);

    int ppq = project.ticks_per_quarter > 0 ? project.ticks_per_quarter : 480;
    int ticksPerBar = project.ticksPerBar();
    if (ticksPerBar <= 0) ticksPerBar = ppq * 4;

    uint32_t startTick = static_cast<uint32_t>(std::max(0.0f, scrollX_));
    uint32_t endTick = static_cast<uint32_t>(scrollX_ + rulerSize.x / pixelsPerTick_);

    int startBar = static_cast<int>(startTick / ticksPerBar);
    int endBar = static_cast<int>(endTick / ticksPerBar) + 1;

    for (int bar = startBar; bar <= endBar; ++bar) {
        uint32_t barTick = static_cast<uint32_t>(bar * ticksPerBar);
        float x = rulerPos.x + (static_cast<float>(barTick) - scrollX_) * pixelsPerTick_;

        // Tick mark
        drawList->AddLine(
            ImVec2(x, rulerPos.y + rulerSize.y - 6),
            ImVec2(x, rulerPos.y + rulerSize.y),
            IM_COL32(80, 80, 90, 255));

        // Bar number
        char label[8];
        snprintf(label, sizeof(label), "%d", bar + 1);
        drawList->AddText(ImVec2(x + 4, rulerPos.y + 2),
            IM_COL32(160, 160, 175, 255), label);
    }

    drawList->PopClipRect();
}

void PianoRollMobile::drawGrid(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();

    uint32_t startTick = static_cast<uint32_t>(std::max(0.0f, scrollX_));
    uint32_t endTick = static_cast<uint32_t>(scrollX_ + canvasSize.x / pixelsPerTick_);

    const float row_height = rowHeight();
    int startPitch = 0;
    int endPitch = 127;
    if (useDrumMap()) {
        startPitch = midi::drumYToPitch(canvasPos.y + canvasSize.y, canvasPos.y, drumRowHeight_, scrollY_);
        endPitch = midi::drumYToPitch(canvasPos.y, canvasPos.y, drumRowHeight_, scrollY_);
        if (startPitch > endPitch) std::swap(startPitch, endPitch);
    } else {
        startPitch = std::max(0, yToPitch(canvasPos.y + canvasSize.y, canvasPos));
        endPitch = std::min(127, yToPitch(canvasPos.y, canvasPos));
    }

    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        if (useDrumMap() && !midi::isDrumPitch(pitch)) continue;
        float y = pitchToY(pitch, canvasPos);

        if (useDrumMap()) {
            drawList->AddRectFilled(
                ImVec2(canvasPos.x, y),
                ImVec2(canvasPos.x + canvasSize.x, y + row_height),
                (pitch % 2 == 0) ? IM_COL32(24, 24, 30, 255) : IM_COL32(20, 20, 26, 255)
            );
            drawList->AddLine(
                ImVec2(canvasPos.x, y + row_height),
                ImVec2(canvasPos.x + canvasSize.x, y + row_height),
                IM_COL32(45, 45, 55, 255)
            );
            continue;
        }

        int noteInOctave = pitch % 12;
        bool isBlackKey = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                          noteInOctave == 8 || noteInOctave == 10);

        if (isBlackKey) {
            drawList->AddRectFilled(
                ImVec2(canvasPos.x, y),
                ImVec2(canvasPos.x + canvasSize.x, y + row_height),
                IM_COL32(20, 20, 25, 255)
            );
        }

        ImU32 lineColor = (noteInOctave == 0) ? IM_COL32(60, 60, 70, 255) : IM_COL32(40, 40, 50, 255);
        drawList->AddLine(
            ImVec2(canvasPos.x, y + row_height),
            ImVec2(canvasPos.x + canvasSize.x, y + row_height),
            lineColor
        );
    }

    // Vertical lines (bars/beats)
    int ppq = project.ticks_per_quarter > 0 ? project.ticks_per_quarter : 480;
    int bu = project.beat_unit > 0 ? project.beat_unit : 4;
    int ticksPerBeat = ppq * 4 / bu;
    int ticksPerBar = project.ticksPerBar();
    if (ticksPerBar <= 0) ticksPerBar = ppq * 4;
    if (ticksPerBeat <= 0) ticksPerBeat = ppq;

    int gridTicks = midi::gridSubdivisionTicks(
        ppq, bu, app_.getGridSnap(), pixelsPerTick_, ticksPerBar, ticksPerBeat);

    uint32_t tick = (startTick / gridTicks) * gridTicks;
    while (tick <= endTick) {
        float x = tickToX(tick, canvasPos);

        bool isBar = (tick % ticksPerBar == 0);
        bool isBeat = (tick % ticksPerBeat == 0);

        ImU32 color = isBar ? IM_COL32(80, 80, 90, 255)
                    : isBeat ? IM_COL32(50, 50, 60, 255)
                    : IM_COL32(40, 40, 50, 255);

        drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), color);

        tick += gridTicks;
    }
}

void PianoRollMobile::drawKeyboard(ImDrawList* drawList, ImVec2 pos, ImVec2 size) {
    ImVec2 gridPos(pos.x + KEYBOARD_WIDTH, pos.y);
    const float row_height = rowHeight();
    const float text_height = ImGui::GetTextLineHeight();
    const bool drum_map = useDrumMap();

    int startPitch = 0;
    int endPitch = 127;
    if (drum_map) {
        startPitch = midi::drumYToPitch(pos.y + size.y, gridPos.y, drumRowHeight_, scrollY_);
        endPitch = midi::drumYToPitch(pos.y, gridPos.y, drumRowHeight_, scrollY_);
        if (startPitch > endPitch) std::swap(startPitch, endPitch);
    } else {
        startPitch = std::max(0, yToPitch(pos.y + size.y, gridPos) - 1);
        endPitch = std::min(127, yToPitch(pos.y, gridPos) + 1);
    }

    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        if (drum_map && !midi::isDrumPitch(pitch)) continue;
        float y = pitchToY(pitch, gridPos);

        bool isPreviewing = (pitch == previewingPitch_ && previewNoteOffTimer_ > 0);
        ImU32 keyColor = isPreviewing
            ? IM_COL32(100, 160, 255, 255)
            : (drum_map ? IM_COL32(42, 42, 48, 255) : IM_COL32(200, 200, 210, 255));
        if (!drum_map) {
            int noteInOctave = pitch % 12;
            bool isBlackKey = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                              noteInOctave == 8 || noteInOctave == 10);
            if (isBlackKey) keyColor = IM_COL32(30, 30, 35, 255);
        }

        float keyWidth = drum_map ? KEYBOARD_WIDTH : KEYBOARD_WIDTH;
        drawList->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + keyWidth, y + row_height), keyColor);
        drawList->AddRect(ImVec2(pos.x, y), ImVec2(pos.x + keyWidth, y + row_height), IM_COL32(50, 50, 60, 255));

        bool show_label = drum_map
            ? (row_height >= text_height + 2.0f)
            : ((pitch % 12 == 0 || pitch % 12 == 2 || pitch % 12 == 4 || pitch % 12 == 5 ||
                pitch % 12 == 7 || pitch % 12 == 9 || pitch % 12 == 11) && row_height >= 14.0f);
        if (show_label) {
            std::string label = truncateLabel(
                midi::getTrackPitchLabel(pitch, drum_map).c_str(), KEYBOARD_WIDTH - 6.0f);
            ImU32 labelColor = drum_map
                ? IM_COL32(235, 235, 245, 255)
                : IM_COL32(100, 100, 115, 255);
            drawList->AddText(ImVec2(pos.x + 3, y + 2), labelColor, label.c_str());
        }
    }

    // Keyboard border
    drawList->AddLine(
        ImVec2(pos.x + KEYBOARD_WIDTH, pos.y),
        ImVec2(pos.x + KEYBOARD_WIDTH, pos.y + size.y),
        IM_COL32(80, 80, 90, 255)
    );
}

ImU32 PianoRollMobile::getTrackColor(int trackIndex, int velocity, bool isSelected, bool isActiveTrack) {
    auto rgb = midi::trackColorRgb(trackIndex, velocity, isSelected, isActiveTrack);
    return IM_COL32(rgb.r, rgb.g, rgb.b, 255);
}

void PianoRollMobile::drawNotes(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();
    int selectedTrackIndex = app_.getSelectedTrackIndex();
    const float row_height = rowHeight();

    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    // Non-selected tracks (behind)
    for (int trackIdx = 0; trackIdx < static_cast<int>(project.tracks.size()); ++trackIdx) {
        if (trackIdx == selectedTrackIndex) continue;
        const auto& track = project.tracks[trackIdx];
        if (track.muted) continue;

        for (const auto& note : track.notes) {
            float x1 = tickToX(note.start_tick, canvasPos);
            float x2 = tickToX(note.endTick(), canvasPos);
            float y = pitchToY(note.pitch, canvasPos);

            if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) continue;
            if (y + row_height < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;

            ImU32 noteColor = getTrackColor(trackIdx, note.velocity, false, false);
            drawList->AddRectFilled(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), noteColor, 3.0f);
            drawList->AddRect(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), IM_COL32(0, 0, 0, 50), 3.0f);
        }
    }

    // Selected track (on top)
    if (selectedTrackIndex >= 0 && selectedTrackIndex < static_cast<int>(project.tracks.size())) {
        const auto& track = project.tracks[selectedTrackIndex];

        for (size_t i = 0; i < track.notes.size(); ++i) {
            const auto& note = track.notes[i];

            float x1 = tickToX(note.start_tick, canvasPos);
            float x2 = tickToX(note.endTick(), canvasPos);
            float y = pitchToY(note.pitch, canvasPos);

            if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) continue;
            if (y + row_height < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;

            ImU32 noteColor = getTrackColor(selectedTrackIndex, note.velocity, note.selected, true);
            drawList->AddRectFilled(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), noteColor, 3.0f);

            ImU32 borderColor = note.selected ? IM_COL32(255, 255, 200, 255) : IM_COL32(0, 0, 0, 100);
            drawList->AddRect(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), borderColor, 3.0f);

            if (note.selected && row_height >= 18 && (x2 - x1) > 30) {
                std::string name = midi::getTrackPitchLabel(note.pitch, midi::isDrumTrack(track));
                drawList->AddText(ImVec2(x1 + 4, y + 3), IM_COL32(0, 0, 0, 200), name.c_str());
            }

            // Draw resize handles on selected notes (visual grab indicators)
            if (note.selected && (x2 - x1) > 40.0f) {
                float handleW = 4.0f;
                float handleInset = 2.0f;
                float handleTop = y + 4;
                float handleBot = y + row_height - 4;
                ImU32 handleColor = IM_COL32(255, 255, 255, 180);

                // Left handle
                drawList->AddRectFilled(
                    ImVec2(x1 + handleInset, handleTop),
                    ImVec2(x1 + handleInset + handleW, handleBot),
                    handleColor, 2.0f
                );
                // Right handle
                drawList->AddRectFilled(
                    ImVec2(x2 - handleInset - handleW, handleTop),
                    ImVec2(x2 - handleInset, handleBot),
                    handleColor, 2.0f
                );
            }
        }
    }

    drawList->PopClipRect();
}

void PianoRollMobile::drawPlayhead(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    uint32_t tick = app_.getPlayheadTick();
    float x = tickToX(tick, canvasPos);

    if (x >= canvasPos.x && x <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(
            ImVec2(x, canvasPos.y),
            ImVec2(x, canvasPos.y + canvasSize.y),
            IM_COL32(255, 100, 100, 255), 2.0f
        );

        drawList->AddTriangleFilled(
            ImVec2(x - 8, canvasPos.y),
            ImVec2(x + 8, canvasPos.y),
            ImVec2(x, canvasPos.y + 12),
            IM_COL32(255, 100, 100, 255)
        );
    }
}

void PianoRollMobile::drawLoopRegion(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();
    if (!project.loop_enabled || project.loop_end <= project.loop_start) return;

    float x1 = tickToX(project.loop_start, canvasPos);
    float x2 = tickToX(project.loop_end, canvasPos);

    if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) return;

    x1 = std::max(x1, canvasPos.x);
    x2 = std::min(x2, canvasPos.x + canvasSize.x);

    drawList->AddRectFilled(
        ImVec2(x1, canvasPos.y), ImVec2(x2, canvasPos.y + canvasSize.y),
        IM_COL32(50, 120, 50, 30)
    );
    drawList->AddLine(ImVec2(x1, canvasPos.y), ImVec2(x1, canvasPos.y + canvasSize.y), IM_COL32(80, 200, 80, 200), 2.0f);
    drawList->AddLine(ImVec2(x2, canvasPos.y), ImVec2(x2, canvasPos.y + canvasSize.y), IM_COL32(80, 200, 80, 200), 2.0f);
}

void PianoRollMobile::autoFollowPlayhead(ImVec2 canvasPos, ImVec2 canvasSize) {
    float playheadX = tickToX(app_.getPlayheadTick(), canvasPos);
    float rightEdge = canvasPos.x + canvasSize.x;
    float leftEdge = canvasPos.x;

    float threshold = leftEdge + (rightEdge - leftEdge) * 0.8f;

    if (playheadX > threshold) {
        float targetX = leftEdge + (rightEdge - leftEdge) * 0.3f;
        float tickAtTarget = scrollX_ + (targetX - canvasPos.x) / pixelsPerTick_;
        float playheadTick = static_cast<float>(app_.getPlayheadTick());
        scrollX_ += (playheadTick - tickAtTarget);
    }

    if (playheadX < leftEdge) {
        scrollX_ = static_cast<float>(app_.getPlayheadTick()) - (canvasSize.x / pixelsPerTick_) * 0.1f;
    }

    scrollX_ = std::max(0.0f, scrollX_);
}

// ========== Coordinate Conversion ==========

float PianoRollMobile::tickToX(uint32_t tick, ImVec2 canvasPos) const {
    return midi::tickToX(tick, canvasPos.x, viewState());
}

uint32_t PianoRollMobile::xToTick(float x, ImVec2 canvasPos) const {
    return midi::xToTick(x, canvasPos.x, viewState());
}

float PianoRollMobile::pitchToY(int pitch, ImVec2 canvasPos) const {
    return midi::pitchToY(pitch, canvasPos.y, viewState());
}

int PianoRollMobile::yToPitch(float y, ImVec2 canvasPos) const {
    return midi::yToPitch(y, canvasPos.y, viewState());
}

PianoRollMobile::NoteHit PianoRollMobile::hitTestNote(float touchX, float touchY,
                                                        ImVec2 canvasPos, ImVec2 canvasSize) {
    NoteHit result;
    auto* track = app_.getSelectedTrack();
    if (!track) return result;

    auto hit = midi::hitTestNote(*track, touchX, touchY, canvasPos.x, canvasPos.y,
                                 viewState(), 6.0f, 12.0f);
    result.noteIndex = hit.note_index;
    result.onLeftEdge = hit.on_left_edge;
    result.onRightEdge = hit.on_right_edge;
    return result;
}

bool PianoRollMobile::useDrumMap() const {
    auto* track = app_.getSelectedTrack();
    return track && midi::isDrumTrack(*track);
}

float PianoRollMobile::rowHeight() const {
    return useDrumMap() ? drumRowHeight_ : noteHeight_;
}

void PianoRollMobile::focusDrumMap(ImVec2 canvasPos, ImVec2 canvasSize) {
    midi::focusDrumMap(canvasPos.y, canvasSize.y, scrollY_, drumRowHeight_);
}
