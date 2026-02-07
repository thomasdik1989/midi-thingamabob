#include "piano_roll.h"
#include "../midi/types.h"
#include <algorithm>
#include <cmath>

PianoRoll::PianoRoll(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void PianoRoll::render() {
    ImGui::Begin("Piano Roll");
    
    // Get canvas area
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    // Minimum size
    if (canvasSize.x < 100 || canvasSize.y < 100) {
        ImGui::End();
        return;
    }
    
    // Adjust for keyboard width
    ImVec2 keyboardPos = canvasPos;
    ImVec2 keyboardSize = ImVec2(KEYBOARD_WIDTH, canvasSize.y);
    
    ImVec2 gridPos = ImVec2(canvasPos.x + KEYBOARD_WIDTH, canvasPos.y);
    ImVec2 gridSize = ImVec2(canvasSize.x - KEYBOARD_WIDTH, canvasSize.y);
    
    // Create invisible button for input handling
    ImGui::InvisibleButton("piano_roll_canvas", canvasSize, 
                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Draw background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                           IM_COL32(30, 30, 35, 255));
    
    // Draw components
    drawGrid(drawList, gridPos, gridSize);
    drawKeyboard(drawList, keyboardPos, keyboardSize);
    drawNotes(drawList, gridPos, gridSize);
    drawPlayhead(drawList, gridPos, gridSize);
    
    if (mode_ == InteractionMode::SelectingBox) {
        drawSelectionBox(drawList, canvasPos);
    }
    
    // Handle input
    if (isHovered || isActive) {
        handleInput(gridPos, gridSize);
    }
    
    // Show info tooltip
    if (isHovered && mode_ == InteractionMode::None) {
        ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x >= gridPos.x) {
            int pitch = yToPitch(mousePos.y, gridPos, gridSize);
            uint32_t tick = xToTick(mousePos.x, gridPos, gridSize);
            double beat = app_.getProject().ticksToBeats(tick);
            int bar = static_cast<int>(beat) / 4 + 1;
            int beatInBar = static_cast<int>(beat) % 4 + 1;
            
            ImGui::BeginTooltip();
            ImGui::Text("%s | Bar %d Beat %d", midi::getNoteName(pitch).c_str(), bar, beatInBar);
            ImGui::EndTooltip();
        }
    }
    
    ImGui::End();
}

void PianoRoll::drawGrid(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();
    
    // Calculate visible range
    uint32_t startTick = static_cast<uint32_t>(std::max(0.0f, scrollX_));
    uint32_t endTick = static_cast<uint32_t>(scrollX_ + canvasSize.x / pixelsPerTick_);
    
    int startPitch = std::max(0, yToPitch(canvasPos.y + canvasSize.y, canvasPos, canvasSize));
    int endPitch = std::min(127, yToPitch(canvasPos.y, canvasPos, canvasSize));
    
    // Draw horizontal lines (pitch rows)
    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        float y = pitchToY(pitch, canvasPos, canvasSize);
        
        // Black key rows get darker background
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
        
        // C notes get a highlight line
        if (noteInOctave == 0) {
            drawList->AddLine(
                ImVec2(canvasPos.x, y + noteHeight_),
                ImVec2(canvasPos.x + canvasSize.x, y + noteHeight_),
                IM_COL32(60, 60, 70, 255)
            );
        } else {
            drawList->AddLine(
                ImVec2(canvasPos.x, y + noteHeight_),
                ImVec2(canvasPos.x + canvasSize.x, y + noteHeight_),
                IM_COL32(40, 40, 50, 255)
            );
        }
    }
    
    // Draw vertical lines (time divisions)
    int ticksPerBeat = project.ticks_per_quarter;
    int ticksPerBar = ticksPerBeat * 4;
    
    // Determine grid resolution based on zoom
    int gridTicks = ticksPerBeat; // Quarter note by default
    if (pixelsPerTick_ > 0.3f) gridTicks = ticksPerBeat / 4;  // 16th notes
    else if (pixelsPerTick_ > 0.15f) gridTicks = ticksPerBeat / 2;  // 8th notes
    else if (pixelsPerTick_ < 0.05f) gridTicks = ticksPerBar;  // Bars only
    
    uint32_t tick = (startTick / gridTicks) * gridTicks;
    while (tick <= endTick) {
        float x = tickToX(tick, canvasPos, canvasSize);
        
        bool isBar = (tick % ticksPerBar == 0);
        bool isBeat = (tick % ticksPerBeat == 0);
        
        ImU32 color;
        if (isBar) {
            color = IM_COL32(80, 80, 90, 255);
        } else if (isBeat) {
            color = IM_COL32(50, 50, 60, 255);
        } else {
            color = IM_COL32(40, 40, 50, 255);
        }
        
        drawList->AddLine(
            ImVec2(x, canvasPos.y),
            ImVec2(x, canvasPos.y + canvasSize.y),
            color
        );
        
        // Bar numbers
        if (isBar && tick >= startTick) {
            int barNumber = tick / ticksPerBar + 1;
            char label[16];
            snprintf(label, sizeof(label), "%d", barNumber);
            drawList->AddText(ImVec2(x + 4, canvasPos.y + 2), IM_COL32(100, 100, 110, 255), label);
        }
        
        tick += gridTicks;
    }
}

void PianoRoll::drawKeyboard(ImDrawList* drawList, ImVec2 pos, ImVec2 size) {
    int startPitch = std::max(0, yToPitch(pos.y + size.y, pos, size) - 1);
    int endPitch = std::min(127, yToPitch(pos.y, pos, size) + 1);
    
    ImVec2 canvasPos = ImVec2(pos.x + KEYBOARD_WIDTH, pos.y);
    
    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        float y = pitchToY(pitch, canvasPos, ImVec2(size.x - KEYBOARD_WIDTH, size.y));
        
        int noteInOctave = pitch % 12;
        bool isBlackKey = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 || 
                          noteInOctave == 8 || noteInOctave == 10);
        bool isC = (noteInOctave == 0);
        
        ImU32 keyColor;
        if (pitch == previewingPitch_) {
            keyColor = IM_COL32(100, 150, 200, 255);
        } else if (isBlackKey) {
            keyColor = IM_COL32(30, 30, 35, 255);
        } else {
            keyColor = IM_COL32(200, 200, 210, 255);
        }
        
        float keyWidth = isBlackKey ? KEYBOARD_WIDTH * 0.6f : KEYBOARD_WIDTH;
        
        drawList->AddRectFilled(
            ImVec2(pos.x, y),
            ImVec2(pos.x + keyWidth, y + noteHeight_),
            keyColor
        );
        
        // Key border
        drawList->AddRect(
            ImVec2(pos.x, y),
            ImVec2(pos.x + keyWidth, y + noteHeight_),
            IM_COL32(50, 50, 60, 255)
        );
        
        // Note label for C notes
        if (isC && noteHeight_ >= 10) {
            std::string label = midi::getNoteName(pitch);
            drawList->AddText(
                ImVec2(pos.x + 4, y + 1),
                IM_COL32(50, 50, 60, 255),
                label.c_str()
            );
        }
    }
    
    // Keyboard border
    drawList->AddLine(
        ImVec2(pos.x + KEYBOARD_WIDTH, pos.y),
        ImVec2(pos.x + KEYBOARD_WIDTH, pos.y + size.y),
        IM_COL32(80, 80, 90, 255)
    );
    
    // Handle keyboard clicks for preview
    ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.x >= pos.x && mousePos.x < pos.x + KEYBOARD_WIDTH &&
        mousePos.y >= pos.y && mousePos.y < pos.y + size.y) {
        
        ImVec2 canvasPosForY = ImVec2(pos.x + KEYBOARD_WIDTH, pos.y);
        int pitch = yToPitch(mousePos.y, canvasPosForY, ImVec2(size.x - KEYBOARD_WIDTH, size.y));
        
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            previewingPitch_ = pitch;
            auto* track = app_.getSelectedTrack();
            if (track) {
                player_.previewNoteOn(track->channel, pitch, 100);
            }
        }
    }
    
    if (previewingPitch_ >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        auto* track = app_.getSelectedTrack();
        if (track) {
            player_.previewNoteOff(track->channel, previewingPitch_);
        }
        previewingPitch_ = -1;
    }
}

// Get a unique color for each track
static ImU32 getTrackColor(int trackIndex, int velocity, bool isSelected, bool isActiveTrack) {
    // Base hue for each track (spread across the color wheel)
    static const float hues[] = {0.6f, 0.0f, 0.3f, 0.15f, 0.45f, 0.75f, 0.9f, 0.55f};
    float hue = hues[trackIndex % 8];
    
    float saturation = isActiveTrack ? 0.7f : 0.4f;
    float value = 0.5f + (velocity / 127.0f) * 0.4f;
    
    if (isSelected) {
        return IM_COL32(255, 200, 100, 255);  // Selected notes are always yellow
    }
    
    if (!isActiveTrack) {
        value *= 0.6f;  // Dim non-active tracks
        saturation *= 0.7f;
    }
    
    // HSV to RGB conversion
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

void PianoRoll::drawNotes(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();
    int selectedTrackIndex = app_.getSelectedTrackIndex();
    
    // Clip to canvas
    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
    
    // First pass: draw notes from non-selected tracks (behind)
    for (int trackIdx = 0; trackIdx < static_cast<int>(project.tracks.size()); ++trackIdx) {
        if (trackIdx == selectedTrackIndex) continue;  // Draw selected track last
        
        const auto& track = project.tracks[trackIdx];
        if (track.muted) continue;  // Don't show muted tracks
        
        for (const auto& note : track.notes) {
            float x1 = tickToX(note.start_tick, canvasPos, canvasSize);
            float x2 = tickToX(note.endTick(), canvasPos, canvasSize);
            float y = pitchToY(note.pitch, canvasPos, canvasSize);
            
            // Skip notes outside visible area
            if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) continue;
            if (y + noteHeight_ < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;
            
            ImU32 noteColor = getTrackColor(trackIdx, note.velocity, false, false);
            
            drawList->AddRectFilled(
                ImVec2(x1, y + 1),
                ImVec2(x2, y + noteHeight_ - 1),
                noteColor
            );
            
            // Subtle border
            drawList->AddRect(
                ImVec2(x1, y + 1),
                ImVec2(x2, y + noteHeight_ - 1),
                IM_COL32(0, 0, 0, 50)
            );
        }
    }
    
    // Second pass: draw notes from selected track (on top)
    if (selectedTrackIndex >= 0 && selectedTrackIndex < static_cast<int>(project.tracks.size())) {
        const auto& track = project.tracks[selectedTrackIndex];
        
        for (size_t i = 0; i < track.notes.size(); ++i) {
            const auto& note = track.notes[i];
            
            float x1 = tickToX(note.start_tick, canvasPos, canvasSize);
            float x2 = tickToX(note.endTick(), canvasPos, canvasSize);
            float y = pitchToY(note.pitch, canvasPos, canvasSize);
            
            // Skip notes outside visible area
            if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) continue;
            if (y + noteHeight_ < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;
            
            // Note body
            ImU32 noteColor = getTrackColor(selectedTrackIndex, note.velocity, note.selected, true);
            
            drawList->AddRectFilled(
                ImVec2(x1, y + 1),
                ImVec2(x2, y + noteHeight_ - 1),
                noteColor
            );
            
            // Note border
            ImU32 borderColor = note.selected ? IM_COL32(255, 255, 200, 255) : IM_COL32(0, 0, 0, 100);
            drawList->AddRect(
                ImVec2(x1, y + 1),
                ImVec2(x2, y + noteHeight_ - 1),
                borderColor
            );
            
            // Velocity indicator (darker band at bottom)
            float velocityHeight = (note.velocity / 127.0f) * (noteHeight_ - 4);
            drawList->AddRectFilled(
                ImVec2(x1 + 1, y + noteHeight_ - velocityHeight - 2),
                ImVec2(x1 + 3, y + noteHeight_ - 2),
                IM_COL32(255, 255, 255, 100)
            );
        }
    }
    
    // Draw note being created
    if (mode_ == InteractionMode::CreatingNote && creatingNotePitch_ >= 0) {
        uint32_t startTick = std::min(creatingNoteStart_, creatingNoteEnd_);
        uint32_t endTick = std::max(creatingNoteStart_, creatingNoteEnd_);
        
        float x1 = tickToX(startTick, canvasPos, canvasSize);
        float x2 = tickToX(endTick, canvasPos, canvasSize);
        float y = pitchToY(creatingNotePitch_, canvasPos, canvasSize);
        
        drawList->AddRectFilled(
            ImVec2(x1, y + 1),
            ImVec2(x2, y + noteHeight_ - 1),
            IM_COL32(100, 200, 255, 150)
        );
        drawList->AddRect(
            ImVec2(x1, y + 1),
            ImVec2(x2, y + noteHeight_ - 1),
            IM_COL32(100, 200, 255, 255)
        );
    }
    
    drawList->PopClipRect();
}

void PianoRoll::drawPlayhead(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    uint32_t tick = app_.getPlayheadTick();
    float x = tickToX(tick, canvasPos, canvasSize);
    
    if (x >= canvasPos.x && x <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(
            ImVec2(x, canvasPos.y),
            ImVec2(x, canvasPos.y + canvasSize.y),
            IM_COL32(255, 100, 100, 255),
            2.0f
        );
        
        // Triangle at top
        drawList->AddTriangleFilled(
            ImVec2(x - 6, canvasPos.y),
            ImVec2(x + 6, canvasPos.y),
            ImVec2(x, canvasPos.y + 10),
            IM_COL32(255, 100, 100, 255)
        );
    }
}

void PianoRoll::drawSelectionBox(ImDrawList* drawList, ImVec2 canvasPos) {
    float x1 = std::min(selectionStart_.x, selectionEnd_.x);
    float y1 = std::min(selectionStart_.y, selectionEnd_.y);
    float x2 = std::max(selectionStart_.x, selectionEnd_.x);
    float y2 = std::max(selectionStart_.y, selectionEnd_.y);
    
    drawList->AddRectFilled(
        ImVec2(x1, y1),
        ImVec2(x2, y2),
        IM_COL32(100, 150, 255, 50)
    );
    drawList->AddRect(
        ImVec2(x1, y1),
        ImVec2(x2, y2),
        IM_COL32(100, 150, 255, 200)
    );
}

void PianoRoll::handleInput(ImVec2 canvasPos, ImVec2 canvasSize) {
    handleScrollAndZoom(canvasPos, canvasSize);
    
    ImVec2 mousePos = ImGui::GetMousePos();
    
    // Only handle note editing in the grid area (not keyboard)
    if (mousePos.x < canvasPos.x) return;
    
    ImGuiIO& io = ImGui::GetIO();
    
    switch (mode_) {
        case InteractionMode::None:
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                auto hit = hitTestNote(mousePos, canvasPos, canvasSize);
                
                if (hit.noteIndex >= 0) {
                    auto* track = app_.getSelectedTrack();
                    if (track) {
                        if (hit.onRightEdge) {
                            // Start resizing
                            mode_ = InteractionMode::ResizingNotes;
                            resizingFromRight_ = true;
                            originalDurations_.clear();
                            for (auto& note : track->notes) {
                                if (note.selected) {
                                    originalDurations_.push_back(note.duration);
                                }
                            }
                            dragStartMouse_ = mousePos;
                        } else if (hit.onLeftEdge) {
                            // Start resizing from left
                            mode_ = InteractionMode::ResizingNotes;
                            resizingFromRight_ = false;
                            originalDurations_.clear();
                            for (auto& note : track->notes) {
                                if (note.selected) {
                                    originalDurations_.push_back(note.duration);
                                }
                            }
                            dragStartMouse_ = mousePos;
                        } else {
                            // Select and prepare for dragging
                            if (!io.KeyCtrl && !track->notes[hit.noteIndex].selected) {
                                track->clearSelection();
                            }
                            track->notes[hit.noteIndex].selected = true;
                            
                            mode_ = InteractionMode::MovingNotes;
                            dragStartPitch_ = yToPitch(mousePos.y, canvasPos, canvasSize);
                            dragStartTick_ = xToTick(mousePos.x, canvasPos, canvasSize);
                            dragStartMouse_ = mousePos;
                            hasDragged_ = false;
                        }
                    }
                } else {
                    // Clicked on empty space
                    if (!io.KeyCtrl) {
                        app_.getProject().clearAllSelections();
                    }
                    
                    if (io.KeyShift) {
                        // Box selection
                        mode_ = InteractionMode::SelectingBox;
                        selectionStart_ = mousePos;
                        selectionEnd_ = mousePos;
                    } else {
                        // Create note
                        mode_ = InteractionMode::CreatingNote;
                        creatingNotePitch_ = yToPitch(mousePos.y, canvasPos, canvasSize);
                        creatingNoteStart_ = xToTick(mousePos.x, canvasPos, canvasSize);
                        
                        // Snap to grid
                        creatingNoteStart_ = midi::snapToGrid(creatingNoteStart_, 
                                                              app_.getProject().ticks_per_quarter, 
                                                              app_.getGridSnap());
                        creatingNoteEnd_ = creatingNoteStart_;
                        
                        // Preview the note
                        auto* track = app_.getSelectedTrack();
                        if (track) {
                            player_.previewNoteOn(track->channel, creatingNotePitch_, 100);
                        }
                    }
                }
            }
            
            // Right click to set playhead
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                uint32_t tick = xToTick(mousePos.x, canvasPos, canvasSize);
                app_.setPlayheadTick(tick);
            }
            break;
            
        case InteractionMode::CreatingNote:
            handleNoteCreation(canvasPos, canvasSize);
            break;
            
        case InteractionMode::SelectingBox:
            handleNoteSelection(canvasPos, canvasSize);
            break;
            
        case InteractionMode::MovingNotes:
            handleNoteDragging(canvasPos, canvasSize);
            break;
            
        case InteractionMode::ResizingNotes:
            handleNoteResizing(canvasPos, canvasSize);
            break;
    }
}

void PianoRoll::handleNoteCreation(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImVec2 mousePos = ImGui::GetMousePos();
    
    creatingNoteEnd_ = xToTick(mousePos.x, canvasPos, canvasSize);
    creatingNoteEnd_ = midi::snapToGrid(creatingNoteEnd_, 
                                        app_.getProject().ticks_per_quarter, 
                                        app_.getGridSnap());
    
    // Ensure minimum note length
    if (creatingNoteEnd_ <= creatingNoteStart_) {
        creatingNoteEnd_ = creatingNoteStart_ + app_.getProject().ticks_per_quarter / 4;
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // Finish creating note
        auto* track = app_.getSelectedTrack();
        if (track) {
            player_.previewNoteOff(track->channel, creatingNotePitch_);
            
            midi::Note newNote;
            newNote.pitch = creatingNotePitch_;
            newNote.velocity = 100;
            newNote.start_tick = std::min(creatingNoteStart_, creatingNoteEnd_);
            newNote.duration = std::abs(static_cast<int32_t>(creatingNoteEnd_ - creatingNoteStart_));
            if (newNote.duration < 1) newNote.duration = app_.getProject().ticks_per_quarter / 4;
            newNote.selected = true;
            
            // Clear other selections
            track->clearSelection();
            
            auto cmd = std::make_unique<AddNotesCommand>(app_, app_.getSelectedTrackIndex(), 
                                                         std::vector<midi::Note>{newNote});
            app_.executeCommand(std::move(cmd));
        }
        
        mode_ = InteractionMode::None;
        creatingNotePitch_ = -1;
    }
}

void PianoRoll::handleNoteSelection(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImVec2 mousePos = ImGui::GetMousePos();
    selectionEnd_ = mousePos;
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // Select all notes in box
        auto* track = app_.getSelectedTrack();
        if (track) {
            float x1 = std::min(selectionStart_.x, selectionEnd_.x);
            float y1 = std::min(selectionStart_.y, selectionEnd_.y);
            float x2 = std::max(selectionStart_.x, selectionEnd_.x);
            float y2 = std::max(selectionStart_.y, selectionEnd_.y);
            
            uint32_t startTick = xToTick(x1, canvasPos, canvasSize);
            uint32_t endTick = xToTick(x2, canvasPos, canvasSize);
            int highPitch = yToPitch(y1, canvasPos, canvasSize);
            int lowPitch = yToPitch(y2, canvasPos, canvasSize);
            
            for (auto& note : track->notes) {
                if (note.start_tick < endTick && note.endTick() > startTick &&
                    note.pitch <= highPitch && note.pitch >= lowPitch) {
                    note.selected = true;
                }
            }
        }
        
        mode_ = InteractionMode::None;
    }
}

void PianoRoll::handleNoteDragging(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImVec2 mousePos = ImGui::GetMousePos();
    
    int currentPitch = yToPitch(mousePos.y, canvasPos, canvasSize);
    uint32_t currentTick = xToTick(mousePos.x, canvasPos, canvasSize);
    
    int pitchDelta = currentPitch - dragStartPitch_;
    int32_t tickDelta = static_cast<int32_t>(currentTick) - static_cast<int32_t>(dragStartTick_);
    
    // Snap tick delta to grid
    if (app_.getGridSnap() != midi::GridSnap::None) {
        int gridSize = app_.getProject().ticks_per_quarter * 4 / static_cast<int>(app_.getGridSnap());
        tickDelta = (tickDelta / gridSize) * gridSize;
    }
    
    // Check if we've actually moved
    if (std::abs(mousePos.x - dragStartMouse_.x) > 3 || 
        std::abs(mousePos.y - dragStartMouse_.y) > 3) {
        hasDragged_ = true;
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (hasDragged_ && (pitchDelta != 0 || tickDelta != 0)) {
            // Commit the move
            auto* track = app_.getSelectedTrack();
            if (track) {
                std::vector<size_t> indices;
                for (size_t i = 0; i < track->notes.size(); ++i) {
                    if (track->notes[i].selected) {
                        indices.push_back(i);
                    }
                }
                
                // Apply move directly (command will track undo)
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
        }
        
        mode_ = InteractionMode::None;
    } else if (hasDragged_) {
        // Preview the move
        auto* track = app_.getSelectedTrack();
        if (track) {
            // Note: We're not actually modifying here, just visually
            // The actual modification happens on mouse release
        }
    }
}

void PianoRoll::handleNoteResizing(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImVec2 mousePos = ImGui::GetMousePos();
    
    uint32_t currentTick = xToTick(mousePos.x, canvasPos, canvasSize);
    currentTick = midi::snapToGrid(currentTick, app_.getProject().ticks_per_quarter, app_.getGridSnap());
    
    int32_t tickDelta = static_cast<int32_t>(currentTick) - static_cast<int32_t>(xToTick(dragStartMouse_.x, canvasPos, canvasSize));
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // Commit resize
        auto* track = app_.getSelectedTrack();
        if (track && tickDelta != 0) {
            for (auto& note : track->notes) {
                if (note.selected) {
                    if (resizingFromRight_) {
                        int32_t newDuration = static_cast<int32_t>(note.duration) + tickDelta;
                        note.duration = static_cast<uint32_t>(std::max(1, newDuration));
                    } else {
                        // Resize from left
                        int32_t newStart = static_cast<int32_t>(note.start_tick) + tickDelta;
                        int32_t newDuration = static_cast<int32_t>(note.duration) - tickDelta;
                        if (newDuration > 0 && newStart >= 0) {
                            note.start_tick = static_cast<uint32_t>(newStart);
                            note.duration = static_cast<uint32_t>(newDuration);
                        }
                    }
                }
            }
            track->sortNotes();
            app_.getProject().modified = true;
        }
        
        mode_ = InteractionMode::None;
        originalDurations_.clear();
    }
}

void PianoRoll::handleScrollAndZoom(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImGuiIO& io = ImGui::GetIO();
    const auto& project = app_.getProject();
    
    // Calculate max scroll based on song length (add some extra space)
    uint32_t totalTicks = project.getTotalTicks();
    float maxScrollX = static_cast<float>(totalTicks) + (canvasSize.x / pixelsPerTick_) * 0.5f;
    
    // Middle mouse button drag for panning
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = io.MouseDelta;
        scrollX_ -= delta.x / pixelsPerTick_;
        scrollY_ -= delta.y;
    }
    
    // Mouse wheel scrolling/zooming
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0) {
        if (io.KeyCtrl) {
            // Zoom
            float zoomFactor = io.MouseWheel > 0 ? 1.2f : 0.8f;
            
            if (io.KeyShift) {
                // Vertical zoom
                noteHeight_ = std::clamp(noteHeight_ * zoomFactor, 6.0f, 30.0f);
            } else {
                // Horizontal zoom
                ImVec2 mousePos = ImGui::GetMousePos();
                float mouseTickBefore = scrollX_ + (mousePos.x - canvasPos.x) / pixelsPerTick_;
                
                pixelsPerTick_ = std::clamp(pixelsPerTick_ * zoomFactor, 0.01f, 1.0f);
                
                // Adjust scroll to keep mouse position stable
                scrollX_ = mouseTickBefore - (mousePos.x - canvasPos.x) / pixelsPerTick_;
                
                // Recalculate max scroll with new zoom
                maxScrollX = static_cast<float>(totalTicks) + (canvasSize.x / pixelsPerTick_) * 0.5f;
            }
        } else if (io.KeyShift) {
            // Horizontal scroll with shift+wheel
            scrollX_ -= io.MouseWheel * 500 / pixelsPerTick_;
        } else {
            // Vertical scroll (default) AND horizontal scroll without modifier
            scrollY_ -= io.MouseWheel * 50;
        }
    }
    
    // Horizontal scroll with shift+wheel OR just wheel if Alt is held
    if (ImGui::IsWindowHovered() && io.MouseWheelH != 0) {
        scrollX_ -= io.MouseWheelH * 500 / pixelsPerTick_;
    }
    
    // Arrow keys for scrolling when window is focused
    if (ImGui::IsWindowFocused()) {
        float scrollSpeed = 100.0f / pixelsPerTick_;
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            scrollX_ -= scrollSpeed * io.DeltaTime * 5;
        }
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            scrollX_ += scrollSpeed * io.DeltaTime * 5;
        }
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            scrollY_ -= 200 * io.DeltaTime;
        }
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            scrollY_ += 200 * io.DeltaTime;
        }
        
        // Home/End keys for quick navigation
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            scrollX_ = 0;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            scrollX_ = maxScrollX - canvasSize.x / pixelsPerTick_;
        }
    }
    
    // Clamp scroll
    scrollX_ = std::clamp(scrollX_, 0.0f, std::max(0.0f, maxScrollX));
    scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, 127.0f * noteHeight_ - canvasSize.y));
}

float PianoRoll::tickToX(uint32_t tick, ImVec2 canvasPos, ImVec2 canvasSize) const {
    return canvasPos.x + (tick - scrollX_) * pixelsPerTick_;
}

uint32_t PianoRoll::xToTick(float x, ImVec2 canvasPos, ImVec2 canvasSize) const {
    float tick = scrollX_ + (x - canvasPos.x) / pixelsPerTick_;
    return static_cast<uint32_t>(std::max(0.0f, tick));
}

float PianoRoll::pitchToY(int pitch, ImVec2 canvasPos, ImVec2 canvasSize) const {
    // Pitch 127 is at top, pitch 0 is at bottom
    return canvasPos.y + (127 - pitch) * noteHeight_ - scrollY_;
}

int PianoRoll::yToPitch(float y, ImVec2 canvasPos, ImVec2 canvasSize) const {
    int pitch = 127 - static_cast<int>((y - canvasPos.y + scrollY_) / noteHeight_);
    return std::clamp(pitch, 0, 127);
}

PianoRoll::NoteHit PianoRoll::hitTestNote(ImVec2 mousePos, ImVec2 canvasPos, ImVec2 canvasSize) {
    NoteHit result;
    
    auto* track = app_.getSelectedTrack();
    if (!track) return result;
    
    const float edgeThreshold = 6.0f;
    
    // Test in reverse order (top notes first)
    for (int i = static_cast<int>(track->notes.size()) - 1; i >= 0; --i) {
        const auto& note = track->notes[i];
        
        float x1 = tickToX(note.start_tick, canvasPos, canvasSize);
        float x2 = tickToX(note.endTick(), canvasPos, canvasSize);
        float y = pitchToY(note.pitch, canvasPos, canvasSize);
        
        if (mousePos.x >= x1 && mousePos.x <= x2 &&
            mousePos.y >= y && mousePos.y <= y + noteHeight_) {
            
            result.noteIndex = i;
            result.onLeftEdge = (mousePos.x - x1 < edgeThreshold);
            result.onRightEdge = (x2 - mousePos.x < edgeThreshold);
            
            // Prefer selected notes
            if (note.selected) {
                return result;
            }
        }
    }
    
    return result;
}

ImU32 PianoRoll::velocityToColor(int velocity) const {
    // Interpolate from blue (low velocity) to red (high velocity)
    float t = velocity / 127.0f;
    
    int r = static_cast<int>(80 + t * 175);
    int g = static_cast<int>(130 - t * 30);
    int b = static_cast<int>(200 - t * 150);
    
    return IM_COL32(r, g, b, 255);
}
