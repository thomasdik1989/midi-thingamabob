#include "piano_roll_mobile.h"
#include "../midi/types.h"
#include <algorithm>
#include <cmath>

PianoRollMobile::PianoRollMobile(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void PianoRollMobile::render(float width, float height) {
    ImVec2 windowPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize(width, height);

    // Keyboard area
    ImVec2 keyboardPos = windowPos;
    ImVec2 keyboardSize(KEYBOARD_WIDTH, height);

    // Grid area (right of keyboard)
    canvasPos_ = ImVec2(windowPos.x + KEYBOARD_WIDTH, windowPos.y);
    canvasSize_ = ImVec2(width - KEYBOARD_WIDTH, height);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    drawList->AddRectFilled(windowPos, ImVec2(windowPos.x + width, windowPos.y + height),
                           IM_COL32(30, 30, 35, 255));

    // Draw components
    drawGrid(drawList, canvasPos_, canvasSize_);
    drawKeyboard(drawList, keyboardPos, keyboardSize);
    drawLoopRegion(drawList, canvasPos_, canvasSize_);
    drawNotes(drawList, canvasPos_, canvasSize_);
    drawPlayhead(drawList, canvasPos_, canvasSize_);

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

void PianoRollMobile::processGesture(const TouchGesture& gesture) {
    // Check if the gesture is within the piano roll area (keyboard + grid).
    // This prevents toolbar taps and other out-of-bounds touches from
    // being processed as piano roll interactions.
    float areaLeft = canvasPos_.x - KEYBOARD_WIDTH;
    float areaRight = canvasPos_.x + canvasSize_.x;
    float areaTop = canvasPos_.y;
    float areaBottom = canvasPos_.y + canvasSize_.y;
    bool inPianoRollArea = (gesture.x >= areaLeft && gesture.x <= areaRight &&
                            gesture.y >= areaTop && gesture.y <= areaBottom);

    switch (gesture.type) {
        case GestureType::Tap: {
            // Stop any currently previewing note first
            if (previewingPitch_ >= 0) {
                player_.previewNoteOff(previewingChannel_, previewingPitch_);
                previewingPitch_ = -1;
                previewNoteOffTimer_ = 0;
            }

            // Ignore taps outside the piano roll area
            if (!inPianoRollArea) break;

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
                break;
            }

            // In scroll mode, ignore taps on the grid (no note creation/selection)
            if (scrollMode_) break;

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

                    auto cmd = std::make_unique<AddNotesCommand>(
                        app_, app_.getSelectedTrackIndex(),
                        std::vector<midi::Note>{newNote}
                    );
                    app_.executeCommand(std::move(cmd));

                    // Preview the note with auto note-off
                    previewingPitch_ = pitch;
                    previewingChannel_ = track->channel;
                    player_.previewNoteOn(track->channel, pitch, 100);
                    previewNoteOffTimer_ = 0.2f;  // Auto note-off after 200ms
                }
            }
            break;
        }

        case GestureType::LongPress: {
            // Ignore long presses outside the piano roll area or in scroll mode
            if (!inPianoRollArea || scrollMode_) break;

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
            break;
        }

        case GestureType::Drag: {
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

                        if (track && (pitchDelta != 0 || tickDelta != 0)) {
                            for (auto& note : track->notes) {
                                if (note.selected) {
                                    note.pitch = std::clamp(note.pitch + pitchDelta, 0, 127);
                                    int32_t newTick = static_cast<int32_t>(note.start_tick) + tickDelta;
                                    note.start_tick = static_cast<uint32_t>(std::max(0, newTick));
                                }
                            }
                            track->sortNotes();
                            app_.getProject().modified = true;
                        }
                        mode_ = InteractionMode::None;
                    }

                } else if (mode_ == InteractionMode::ResizingNotes) {
                    // Resize selected notes by dragging
                    auto* track = app_.getSelectedTrack();
                    if (track && gesture.ended) {
                        uint32_t currentTick = xToTick(gesture.x, canvasPos_);
                        currentTick = midi::snapToGrid(currentTick, app_.getProject().ticks_per_quarter, app_.getGridSnap());
                        uint32_t startTick = xToTick(dragStartX_, canvasPos_);

                        int32_t tickDelta = static_cast<int32_t>(currentTick) - static_cast<int32_t>(startTick);

                        if (resizingFromRight_) {
                            // Right edge: change duration only
                            for (auto& note : track->notes) {
                                if (note.selected) {
                                    int32_t newDuration = static_cast<int32_t>(note.duration) + tickDelta;
                                    note.duration = static_cast<uint32_t>(std::max(1, newDuration));
                                }
                            }
                        } else {
                            // Left edge: move start_tick, adjust duration to keep end fixed
                            for (auto& note : track->notes) {
                                if (note.selected) {
                                    uint32_t endTick = note.start_tick + note.duration;
                                    int32_t newStart = static_cast<int32_t>(note.start_tick) + tickDelta;
                                    newStart = std::max(0, newStart);
                                    if (static_cast<uint32_t>(newStart) >= endTick) {
                                        newStart = static_cast<int32_t>(endTick) - 1;
                                    }
                                    note.start_tick = static_cast<uint32_t>(newStart);
                                    note.duration = endTick - note.start_tick;
                                }
                            }
                        }
                        track->sortNotes();
                        app_.getProject().modified = true;
                        mode_ = InteractionMode::None;
                    }

                } else if (mode_ == InteractionMode::Scrolling) {
                    // One-finger pan: apply drag delta to scroll position
                    scrollX_ -= gesture.deltaX / pixelsPerTick_;
                    scrollY_ -= gesture.deltaY;
                    scrollX_ = std::max(0.0f, scrollX_);
                    scrollY_ = std::clamp(scrollY_, 0.0f,
                        std::max(0.0f, 127.0f * noteHeight_ - canvasSize_.y));

                    if (gesture.ended) {
                        mode_ = InteractionMode::None;
                    }

                } else if (mode_ == InteractionMode::None) {
                    // Ignore drags that start outside the piano roll area
                    float startX = gesture.x - gesture.deltaX;
                    float startY = gesture.y - gesture.deltaY;
                    bool startInArea = (startX >= areaLeft && startX <= areaRight &&
                                        startY >= areaTop && startY <= areaBottom);
                    if (!startInArea) break;

                    // In scroll mode, all one-finger drags become scrolling
                    if (scrollMode_) {
                        mode_ = InteractionMode::Scrolling;
                        break;
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
            break;
        }

        case GestureType::Pinch: {
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
            scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, 127.0f * noteHeight_ - canvasSize_.y));
            break;
        }

        default:
            break;
    }
}

// ========== Drawing ==========

void PianoRollMobile::drawGrid(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();

    uint32_t startTick = static_cast<uint32_t>(std::max(0.0f, scrollX_));
    uint32_t endTick = static_cast<uint32_t>(scrollX_ + canvasSize.x / pixelsPerTick_);

    int startPitch = std::max(0, yToPitch(canvasPos.y + canvasSize.y, canvasPos));
    int endPitch = std::min(127, yToPitch(canvasPos.y, canvasPos));

    // Horizontal lines (pitch rows)
    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        float y = pitchToY(pitch, canvasPos);

        int noteInOctave = pitch % 12;
        bool isBlackKey = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                          noteInOctave == 8 || noteInOctave == 10);

        if (isBlackKey) {
            drawList->AddRectFilled(
                ImVec2(canvasPos.x, y),
                ImVec2(canvasPos.x + canvasSize.x, y + noteHeight_),
                IM_COL32(20, 20, 25, 255)
            );
        }

        ImU32 lineColor = (noteInOctave == 0) ? IM_COL32(60, 60, 70, 255) : IM_COL32(40, 40, 50, 255);
        drawList->AddLine(
            ImVec2(canvasPos.x, y + noteHeight_),
            ImVec2(canvasPos.x + canvasSize.x, y + noteHeight_),
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

    int gridTicks = ticksPerBeat;
    if (pixelsPerTick_ > 0.4f) gridTicks = std::max(1, ticksPerBeat / 4);
    else if (pixelsPerTick_ > 0.2f) gridTicks = std::max(1, ticksPerBeat / 2);
    else if (pixelsPerTick_ < 0.08f) gridTicks = ticksPerBar;

    uint32_t tick = (startTick / gridTicks) * gridTicks;
    while (tick <= endTick) {
        float x = tickToX(tick, canvasPos);

        bool isBar = (tick % ticksPerBar == 0);
        bool isBeat = (tick % ticksPerBeat == 0);

        ImU32 color = isBar ? IM_COL32(80, 80, 90, 255)
                    : isBeat ? IM_COL32(50, 50, 60, 255)
                    : IM_COL32(40, 40, 50, 255);

        drawList->AddLine(ImVec2(x, canvasPos.y), ImVec2(x, canvasPos.y + canvasSize.y), color);

        if (isBar && tick >= startTick) {
            int barNumber = tick / ticksPerBar + 1;
            char label[16];
            snprintf(label, sizeof(label), "%d", barNumber);
            drawList->AddText(ImVec2(x + 4, canvasPos.y + 2), IM_COL32(100, 100, 110, 255), label);
        }

        tick += gridTicks;
    }
}

void PianoRollMobile::drawKeyboard(ImDrawList* drawList, ImVec2 pos, ImVec2 size) {
    ImVec2 gridPos(pos.x + KEYBOARD_WIDTH, pos.y);
    int startPitch = std::max(0, yToPitch(pos.y + size.y, gridPos) - 1);
    int endPitch = std::min(127, yToPitch(pos.y, gridPos) + 1);

    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        float y = pitchToY(pitch, gridPos);

        int noteInOctave = pitch % 12;
        bool isBlackKey = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                          noteInOctave == 8 || noteInOctave == 10);
        bool isC = (noteInOctave == 0);

        bool isPreviewing = (pitch == previewingPitch_ && previewNoteOffTimer_ > 0);
        ImU32 keyColor;
        if (isPreviewing) {
            keyColor = IM_COL32(100, 160, 255, 255);  // Highlighted blue when pressed
        } else {
            keyColor = isBlackKey ? IM_COL32(30, 30, 35, 255) : IM_COL32(200, 200, 210, 255);
        }
        float keyWidth = isBlackKey ? KEYBOARD_WIDTH * 0.65f : KEYBOARD_WIDTH;

        drawList->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + keyWidth, y + noteHeight_), keyColor);
        drawList->AddRect(ImVec2(pos.x, y), ImVec2(pos.x + keyWidth, y + noteHeight_), IM_COL32(50, 50, 60, 255));

        // Note labels for C notes (always visible on mobile since notes are bigger)
        if (isC && noteHeight_ >= 14) {
            std::string label = midi::getNoteName(pitch);
            drawList->AddText(ImVec2(pos.x + 3, y + 2), IM_COL32(50, 50, 60, 255), label.c_str());
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
    static const float hues[] = {0.6f, 0.0f, 0.3f, 0.15f, 0.45f, 0.75f, 0.9f, 0.55f};
    float hue = hues[trackIndex % 8];

    float saturation = isActiveTrack ? 0.7f : 0.4f;
    float value = 0.5f + (velocity / 127.0f) * 0.4f;

    if (isSelected) return IM_COL32(255, 200, 100, 255);

    if (!isActiveTrack) {
        value *= 0.6f;
        saturation *= 0.7f;
    }

    float h = hue * 6.0f;
    int i = static_cast<int>(h);
    float f = h - i;
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - saturation * f);
    float t = value * (1.0f - saturation * (1.0f - f));

    float r, g, b;
    switch (i % 6) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }

    return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), 255);
}

void PianoRollMobile::drawNotes(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();
    int selectedTrackIndex = app_.getSelectedTrackIndex();

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
            if (y + noteHeight_ < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;

            ImU32 noteColor = getTrackColor(trackIdx, note.velocity, false, false);
            drawList->AddRectFilled(ImVec2(x1, y + 1), ImVec2(x2, y + noteHeight_ - 1), noteColor, 3.0f);
            drawList->AddRect(ImVec2(x1, y + 1), ImVec2(x2, y + noteHeight_ - 1), IM_COL32(0, 0, 0, 50), 3.0f);
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
            if (y + noteHeight_ < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;

            ImU32 noteColor = getTrackColor(selectedTrackIndex, note.velocity, note.selected, true);
            drawList->AddRectFilled(ImVec2(x1, y + 1), ImVec2(x2, y + noteHeight_ - 1), noteColor, 3.0f);

            ImU32 borderColor = note.selected ? IM_COL32(255, 255, 200, 255) : IM_COL32(0, 0, 0, 100);
            drawList->AddRect(ImVec2(x1, y + 1), ImVec2(x2, y + noteHeight_ - 1), borderColor, 3.0f);

            // On mobile, show note name inside larger notes
            if (note.selected && noteHeight_ >= 18 && (x2 - x1) > 30) {
                std::string name = midi::getNoteName(note.pitch);
                drawList->AddText(ImVec2(x1 + 4, y + 3), IM_COL32(0, 0, 0, 200), name.c_str());
            }

            // Draw resize handles on selected notes (visual grab indicators)
            if (note.selected && (x2 - x1) > 40.0f) {
                float handleW = 4.0f;
                float handleInset = 2.0f;
                float handleTop = y + 4;
                float handleBot = y + noteHeight_ - 4;
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
    return canvasPos.x + (tick - scrollX_) * pixelsPerTick_;
}

uint32_t PianoRollMobile::xToTick(float x, ImVec2 canvasPos) const {
    float tick = scrollX_ + (x - canvasPos.x) / pixelsPerTick_;
    return static_cast<uint32_t>(std::max(0.0f, tick));
}

float PianoRollMobile::pitchToY(int pitch, ImVec2 canvasPos) const {
    return canvasPos.y + (127 - pitch) * noteHeight_ - scrollY_;
}

int PianoRollMobile::yToPitch(float y, ImVec2 canvasPos) const {
    int pitch = 127 - static_cast<int>((y - canvasPos.y + scrollY_) / noteHeight_);
    return std::clamp(pitch, 0, 127);
}

PianoRollMobile::NoteHit PianoRollMobile::hitTestNote(float touchX, float touchY,
                                                        ImVec2 canvasPos, ImVec2 canvasSize) {
    NoteHit result;
    auto* track = app_.getSelectedTrack();
    if (!track) return result;

    // Larger touch target for mobile (12px padding)
    const float touchPadding = 12.0f;

    for (int i = static_cast<int>(track->notes.size()) - 1; i >= 0; --i) {
        const auto& note = track->notes[i];

        float x1 = tickToX(note.start_tick, canvasPos);
        float x2 = tickToX(note.endTick(), canvasPos);
        float y = pitchToY(note.pitch, canvasPos);

        if (touchX >= x1 - touchPadding && touchX <= x2 + touchPadding &&
            touchY >= y - touchPadding && touchY <= y + noteHeight_ + touchPadding) {

            result.noteIndex = i;
            // Edge detection for resizing (20px touch zone on each side)
            float noteWidth = x2 - x1;
            if (noteWidth > 40.0f) {
                result.onRightEdge = (x2 - touchX < 20.0f);
                result.onLeftEdge  = (touchX - x1 < 20.0f);
            }

            if (note.selected) return result;
        }
    }

    return result;
}
