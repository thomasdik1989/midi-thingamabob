#include "piano_roll.h"
#include "../midi/harmony.h"
#include "../midi/patterns.h"
#include "../midi/piano_roll_view.h"
#include "../midi/types.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
std::vector<size_t> selectedNoteIndices(const midi::Track& track) {
    std::vector<size_t> indices;
    for (size_t i = 0; i < track.notes.size(); ++i) {
        if (track.notes[i].selected) indices.push_back(i);
    }
    return indices;
}

std::string truncateLabel(const char* text, float max_width) {
    if (!text || max_width <= 0.0f) return {};
    ImVec2 size = ImGui::CalcTextSize(text);
    if (size.x <= max_width) return text;
    std::string out(text);
    while (!out.empty() && ImGui::CalcTextSize((out + "...").c_str()).x > max_width) {
        out.pop_back();
    }
    out += "...";
    return out;
}

void applyVelocityChange(App& app, int trackIndex, const std::vector<midi::Note>& notes,
                         int newVelocity, uint32_t clickTick) {
    std::vector<size_t> indices;
    std::vector<int> newVelocities;
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& note = notes[i];
        if (note.selected || (clickTick >= note.start_tick && clickTick < note.endTick())) {
            indices.push_back(i);
            newVelocities.push_back(newVelocity);
        }
    }
    if (indices.empty()) return;
    app.executeCommand(std::make_unique<ChangeVelocityCommand>(
        app, trackIndex, std::move(indices), std::move(newVelocities)));
}
}

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

    // Reserve space for velocity lane at the bottom
    ImVec2 velocityPos = ImVec2(canvasPos.x + KEYBOARD_WIDTH, canvasPos.y + canvasSize.y - VELOCITY_LANE_HEIGHT);
    ImVec2 velocitySize = ImVec2(canvasSize.x - KEYBOARD_WIDTH, VELOCITY_LANE_HEIGHT);
    float pianoAreaHeight = canvasSize.y - VELOCITY_LANE_HEIGHT - 2; // 2px separator

    // Adjust for keyboard width
    ImVec2 keyboardPos = canvasPos;
    ImVec2 keyboardSize = ImVec2(KEYBOARD_WIDTH, pianoAreaHeight);

    ImVec2 gridPos = ImVec2(canvasPos.x + KEYBOARD_WIDTH, canvasPos.y);
    ImVec2 gridSize = ImVec2(canvasSize.x - KEYBOARD_WIDTH, pianoAreaHeight);

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
    drawLoopRegion(drawList, gridPos, gridSize);
    drawNotes(drawList, gridPos, gridSize);
    drawPlayhead(drawList, gridPos, gridSize);

    // Separator between piano roll and velocity lane
    drawList->AddLine(
        ImVec2(canvasPos.x, canvasPos.y + pianoAreaHeight + 1),
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + pianoAreaHeight + 1),
        IM_COL32(80, 80, 90, 255)
    );

    drawVelocityLane(drawList, velocityPos, velocitySize);
    handleKeyboardPreview(keyboardPos, keyboardSize);
    handleVelocityInput(velocityPos, velocitySize);

    if (mode_ == InteractionMode::SelectingBox) {
        drawSelectionBox(drawList, canvasPos);
    }

    int selectedTrack = app_.getSelectedTrackIndex();
    if (selectedTrack != lastSelectedTrack_) {
        if (auto* track = app_.getSelectedTrack(); track && midi::isDrumTrack(*track)) {
            focusDrumMap(gridPos, gridSize);
        }
        lastSelectedTrack_ = selectedTrack;
    }

    // Handle input
    if (isHovered || isActive) {
        handleInput(gridPos, gridSize);
    }

    // Fix: Always stop preview note on mouse release regardless of position
    if (previewingPitch_ >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        auto* track = app_.getSelectedTrack();
        if (track) {
            player_.previewNoteOff(track->channel, previewingPitch_);
        }
        previewingPitch_ = -1;
    }

    // Auto-follow playhead during playback
    if (app_.isPlaying()) {
        autoFollowPlayhead(gridPos, gridSize);
    }

    // Show info tooltip
    if (isHovered && mode_ == InteractionMode::None) {
        ImVec2 mousePos = ImGui::GetMousePos();
        if (mousePos.x >= gridPos.x && mousePos.y < gridPos.y + gridSize.y) {
            int pitch = yToPitch(mousePos.y, gridPos, gridSize);
            uint32_t tick = xToTick(mousePos.x, gridPos, gridSize);
            const auto& project = app_.getProject();
            int bar = project.tickToBar(tick);
            int beat = project.tickToBeatInBar(tick);

            bool drum_track = false;
            if (auto* track = app_.getSelectedTrack()) {
                drum_track = midi::isDrumTrack(*track);
            }

            ImGui::BeginTooltip();
            ImGui::Text("%s | Bar %d Beat %d",
                        midi::getTrackPitchLabel(pitch, drum_track).c_str(), bar, beat);
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

    const float row_height = rowHeight();
    int startPitch = 0;
    int endPitch = 127;
    if (useDrumMap()) {
        startPitch = midi::drumYToPitch(canvasPos.y + canvasSize.y, canvasPos.y, drumRowHeight_, scrollY_);
        endPitch = midi::drumYToPitch(canvasPos.y, canvasPos.y, drumRowHeight_, scrollY_);
        if (startPitch > endPitch) std::swap(startPitch, endPitch);
    } else {
        startPitch = std::max(0, yToPitch(canvasPos.y + canvasSize.y, canvasPos, canvasSize));
        endPitch = std::min(127, yToPitch(canvasPos.y, canvasPos, canvasSize));
    }

    // Draw horizontal lines (pitch rows)
    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        if (useDrumMap() && !midi::isDrumPitch(pitch)) continue;
        float y = pitchToY(pitch, canvasPos, canvasSize);

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

        if (noteInOctave == 0) {
            drawList->AddLine(
                ImVec2(canvasPos.x, y + row_height),
                ImVec2(canvasPos.x + canvasSize.x, y + row_height),
                IM_COL32(60, 60, 70, 255)
            );
        } else {
            drawList->AddLine(
                ImVec2(canvasPos.x, y + row_height),
                ImVec2(canvasPos.x + canvasSize.x, y + row_height),
                IM_COL32(40, 40, 50, 255)
            );
        }
    }

    // Draw vertical lines using time signature
    int ppq = project.ticks_per_quarter > 0 ? project.ticks_per_quarter : 480;
    int bu = project.beat_unit > 0 ? project.beat_unit : 4;
    int ticksPerBeat = ppq * 4 / bu;  // ticks per beat for this time signature
    int ticksPerBar = project.ticksPerBar();
    if (ticksPerBar <= 0) ticksPerBar = ppq * 4;
    if (ticksPerBeat <= 0) ticksPerBeat = ppq;

    int gridTicks = midi::gridSubdivisionTicks(
        ppq, bu, app_.getGridSnap(), pixelsPerTick_, ticksPerBar, ticksPerBeat);

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
    ImVec2 canvasPos = ImVec2(pos.x + KEYBOARD_WIDTH, pos.y);
    const float row_height = rowHeight();
    const float text_height = ImGui::GetTextLineHeight();
    const bool drum_map = useDrumMap();

    int startPitch = 0;
    int endPitch = 127;
    if (drum_map) {
        startPitch = midi::drumYToPitch(pos.y + size.y, canvasPos.y, drumRowHeight_, scrollY_);
        endPitch = midi::drumYToPitch(pos.y, canvasPos.y, drumRowHeight_, scrollY_);
        if (startPitch > endPitch) std::swap(startPitch, endPitch);
    } else {
        startPitch = std::max(0, yToPitch(pos.y + size.y, canvasPos, ImVec2(size.x - KEYBOARD_WIDTH, size.y)) - 1);
        endPitch = std::min(127, yToPitch(pos.y, canvasPos, ImVec2(size.x - KEYBOARD_WIDTH, size.y)) + 1);
    }

    for (int pitch = startPitch; pitch <= endPitch; ++pitch) {
        if (drum_map && !midi::isDrumPitch(pitch)) continue;

        float y = pitchToY(pitch, canvasPos, ImVec2(size.x - KEYBOARD_WIDTH, size.y));
        ImU32 keyColor = (pitch == previewingPitch_)
            ? IM_COL32(100, 150, 200, 255)
            : (drum_map ? IM_COL32(42, 42, 48, 255) : IM_COL32(200, 200, 210, 255));

        if (!drum_map) {
            int noteInOctave = pitch % 12;
            bool isBlackKey = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6 ||
                              noteInOctave == 8 || noteInOctave == 10);
            if (isBlackKey) keyColor = IM_COL32(30, 30, 35, 255);
        }

        float keyWidth = drum_map ? KEYBOARD_WIDTH : ((pitch % 12 == 1 || pitch % 12 == 3 || pitch % 12 == 6 ||
            pitch % 12 == 8 || pitch % 12 == 10) ? KEYBOARD_WIDTH * 0.6f : KEYBOARD_WIDTH);

        drawList->AddRectFilled(
            ImVec2(pos.x, y),
            ImVec2(pos.x + keyWidth, y + row_height),
            keyColor
        );
        drawList->AddRect(
            ImVec2(pos.x, y),
            ImVec2(pos.x + keyWidth, y + row_height),
            IM_COL32(50, 50, 60, 255)
        );

        bool show_label = drum_map
            ? (row_height >= text_height + 2.0f)
            : ((pitch % 12 == 0) && row_height >= 10.0f);
        if (show_label) {
            std::string label = truncateLabel(
                midi::getTrackPitchLabel(pitch, drum_map).c_str(), KEYBOARD_WIDTH - 8.0f);
            ImU32 labelColor = drum_map
                ? IM_COL32(235, 235, 245, 255)
                : IM_COL32(50, 50, 60, 255);
            drawList->AddText(
                ImVec2(pos.x + 4, y + 1),
                labelColor,
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

}

static ImU32 getTrackColor(int trackIndex, int velocity, bool isSelected, bool isActiveTrack) {
    auto rgb = midi::trackColorRgb(trackIndex, velocity, isSelected, isActiveTrack);
    return IM_COL32(rgb.r, rgb.g, rgb.b, 255);
}

midi::PianoRollView PianoRoll::viewState(ImVec2 canvasPos) const {
    midi::PianoRollView view;
    view.pixels_per_tick = pixelsPerTick_;
    view.note_height = noteHeight_;
    view.drum_row_height = drumRowHeight_;
    view.scroll_x = scrollX_;
    view.scroll_y = scrollY_;
    view.use_drum_map = useDrumMap();
    return view;
}

void PianoRoll::drawNotes(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();
    int selectedTrackIndex = app_.getSelectedTrackIndex();
    const float row_height = rowHeight();

    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    // First pass: non-selected tracks (behind)
    for (int trackIdx = 0; trackIdx < static_cast<int>(project.tracks.size()); ++trackIdx) {
        if (trackIdx == selectedTrackIndex) continue;
        const auto& track = project.tracks[trackIdx];
        if (track.muted) continue;

        for (const auto& note : track.notes) {
            float x1 = tickToX(note.start_tick, canvasPos, canvasSize);
            float x2 = tickToX(note.endTick(), canvasPos, canvasSize);
            float y = pitchToY(note.pitch, canvasPos, canvasSize);

            if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) continue;
            if (y + row_height < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;

            ImU32 noteColor = getTrackColor(trackIdx, note.velocity, false, false);
            drawList->AddRectFilled(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), noteColor);
            drawList->AddRect(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), IM_COL32(0, 0, 0, 50));
        }
    }

    // Second pass: selected track (on top)
    if (selectedTrackIndex >= 0 && selectedTrackIndex < static_cast<int>(project.tracks.size())) {
        const auto& track = project.tracks[selectedTrackIndex];

        for (size_t i = 0; i < track.notes.size(); ++i) {
            const auto& note = track.notes[i];

            float x1 = tickToX(note.start_tick, canvasPos, canvasSize);
            float x2 = tickToX(note.endTick(), canvasPos, canvasSize);
            float y = pitchToY(note.pitch, canvasPos, canvasSize);

            if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) continue;
            if (y + row_height < canvasPos.y || y > canvasPos.y + canvasSize.y) continue;

            ImU32 noteColor = getTrackColor(selectedTrackIndex, note.velocity, note.selected, true);
            drawList->AddRectFilled(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), noteColor);

            ImU32 borderColor = note.selected ? IM_COL32(255, 255, 200, 255) : IM_COL32(0, 0, 0, 100);
            drawList->AddRect(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), borderColor);
        }
    }

    // Draw note being created
    if (mode_ == InteractionMode::CreatingNote && creatingNotePitch_ >= 0) {
        uint32_t startTick = std::min(creatingNoteStart_, creatingNoteEnd_);
        uint32_t endTick = std::max(creatingNoteStart_, creatingNoteEnd_);

        float x1 = tickToX(startTick, canvasPos, canvasSize);
        float x2 = tickToX(endTick, canvasPos, canvasSize);
        float y = pitchToY(creatingNotePitch_, canvasPos, canvasSize);

        drawList->AddRectFilled(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), IM_COL32(100, 200, 255, 150));
        drawList->AddRect(ImVec2(x1, y + 1), ImVec2(x2, y + row_height - 1), IM_COL32(100, 200, 255, 255));
    }

    drawList->PopClipRect();
}

void PianoRoll::drawLoopRegion(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const auto& project = app_.getProject();
    if (!project.loop_enabled || project.loop_end <= project.loop_start) return;

    float x1 = tickToX(project.loop_start, canvasPos, canvasSize);
    float x2 = tickToX(project.loop_end, canvasPos, canvasSize);

    // Skip if outside view
    if (x2 < canvasPos.x || x1 > canvasPos.x + canvasSize.x) return;

    x1 = std::max(x1, canvasPos.x);
    x2 = std::min(x2, canvasPos.x + canvasSize.x);

    // Tinted background for loop region
    drawList->AddRectFilled(
        ImVec2(x1, canvasPos.y),
        ImVec2(x2, canvasPos.y + canvasSize.y),
        IM_COL32(50, 120, 50, 30)
    );

    // Loop markers (vertical lines)
    drawList->AddLine(ImVec2(x1, canvasPos.y), ImVec2(x1, canvasPos.y + canvasSize.y), IM_COL32(80, 200, 80, 200), 2.0f);
    drawList->AddLine(ImVec2(x2, canvasPos.y), ImVec2(x2, canvasPos.y + canvasSize.y), IM_COL32(80, 200, 80, 200), 2.0f);

    // "L" labels at top
    drawList->AddText(ImVec2(x1 + 3, canvasPos.y + 2), IM_COL32(80, 200, 80, 255), "L");
    drawList->AddText(ImVec2(x2 - 10, canvasPos.y + 2), IM_COL32(80, 200, 80, 255), "R");
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

        drawList->AddTriangleFilled(
            ImVec2(x - 6, canvasPos.y),
            ImVec2(x + 6, canvasPos.y),
            ImVec2(x, canvasPos.y + 10),
            IM_COL32(255, 100, 100, 255)
        );
    }
}

void PianoRoll::drawVelocityLane(ImDrawList* drawList, ImVec2 pos, ImVec2 size) {
    // Background
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(25, 25, 30, 255));

    // Label
    drawList->AddText(ImVec2(pos.x - KEYBOARD_WIDTH + 4, pos.y + 4), IM_COL32(100, 100, 110, 255), "Vel");

    auto* track = app_.getSelectedTrack();
    if (!track) return;

    // Clip to velocity lane
    drawList->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);

    for (size_t i = 0; i < track->notes.size(); ++i) {
        const auto& note = track->notes[i];

        float x = tickToX(note.start_tick, pos, size);
        float x2 = tickToX(note.endTick(), pos, size);
        float noteWidth = std::max(3.0f, x2 - x);

        if (x + noteWidth < pos.x || x > pos.x + size.x) continue;

        float barHeight = (note.velocity / 127.0f) * (size.y - 4);
        float barY = pos.y + size.y - barHeight - 2;

        ImU32 barColor = velocityToColor(note.velocity);
        if (note.selected) {
            barColor = IM_COL32(255, 200, 100, 255);
        }

        drawList->AddRectFilled(
            ImVec2(x, barY),
            ImVec2(x + std::min(noteWidth, 8.0f), pos.y + size.y - 2),
            barColor
        );
    }

    // Horizontal guide lines
    for (int v = 32; v <= 96; v += 32) {
        float y = pos.y + size.y - (v / 127.0f) * (size.y - 4) - 2;
        drawList->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), IM_COL32(50, 50, 60, 128));
    }

    drawList->PopClipRect();
}

void PianoRoll::handleKeyboardPreview(ImVec2 pos, ImVec2 size) {
    ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.x < pos.x || mousePos.x >= pos.x + KEYBOARD_WIDTH ||
        mousePos.y < pos.y || mousePos.y >= pos.y + size.y) {
        return;
    }

    ImVec2 canvasPosForY = ImVec2(pos.x + KEYBOARD_WIDTH, pos.y);
    int pitch = yToPitch(mousePos.y, canvasPosForY, ImVec2(size.x - KEYBOARD_WIDTH, size.y));
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

    previewingPitch_ = pitch;
    auto* track = app_.getSelectedTrack();
    if (track) player_.previewNoteOn(track->channel, pitch, 100);
}

void PianoRoll::handleVelocityInput(ImVec2 pos, ImVec2 size) {
    ImVec2 mousePos = ImGui::GetMousePos();
    if (mousePos.x < pos.x || mousePos.x > pos.x + size.x ||
        mousePos.y < pos.y || mousePos.y > pos.y + size.y) {
        return;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        app_.beginUndoGroup();
        mode_ = InteractionMode::EditingVelocity;
    }

    if (mode_ != InteractionMode::EditingVelocity) return;

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float relY = 1.0f - (mousePos.y - pos.y) / size.y;
        int newVelocity = std::clamp(static_cast<int>(relY * 127), 1, 127);
        uint32_t clickTick = xToTick(mousePos.x, pos, size);
        if (auto* track = app_.getSelectedTrack()) {
            applyVelocityChange(app_, app_.getSelectedTrackIndex(), track->notes, newVelocity, clickTick);
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        app_.endUndoGroup();
        mode_ = InteractionMode::None;
    }
}

void PianoRoll::drawSelectionBox(ImDrawList* drawList, ImVec2 canvasPos) {
    float x1 = std::min(selectionStart_.x, selectionEnd_.x);
    float y1 = std::min(selectionStart_.y, selectionEnd_.y);
    float x2 = std::max(selectionStart_.x, selectionEnd_.x);
    float y2 = std::max(selectionStart_.y, selectionEnd_.y);

    drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(100, 150, 255, 50));
    drawList->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(100, 150, 255, 200));
}

void PianoRoll::handleInput(ImVec2 canvasPos, ImVec2 canvasSize) {
    handleScrollAndZoom(canvasPos, canvasSize);

    ImVec2 mousePos = ImGui::GetMousePos();

    // Only handle note editing in the grid area (not keyboard, not velocity lane)
    if (mousePos.x < canvasPos.x) return;
    if (mode_ == InteractionMode::EditingVelocity) return; // Handled in drawVelocityLane

    ImGuiIO& io = ImGui::GetIO();

    switch (mode_) {
        case InteractionMode::None:
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                auto hit = hitTestNote(mousePos, canvasPos, canvasSize);

                if (hit.noteIndex >= 0) {
                    auto* track = app_.getSelectedTrack();
                    if (track) {
                        if (hit.onRightEdge) {
                            mode_ = InteractionMode::ResizingNotes;
                            resizingFromRight_ = true;
                            originalDurations_.clear();
                            for (auto& note : track->notes) {
                                if (note.selected) originalDurations_.push_back(note.duration);
                            }
                            dragStartMouse_ = mousePos;
                        } else if (hit.onLeftEdge) {
                            mode_ = InteractionMode::ResizingNotes;
                            resizingFromRight_ = false;
                            originalDurations_.clear();
                            for (auto& note : track->notes) {
                                if (note.selected) originalDurations_.push_back(note.duration);
                            }
                            dragStartMouse_ = mousePos;
                        } else {
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
                    if (!io.KeyCtrl) {
                        app_.getProject().clearAllSelections();
                    }

                    if (io.KeyShift) {
                        mode_ = InteractionMode::SelectingBox;
                        selectionStart_ = mousePos;
                        selectionEnd_ = mousePos;
                    } else {
                        mode_ = InteractionMode::CreatingNote;
                        creatingNotePitch_ = yToPitch(mousePos.y, canvasPos, canvasSize);
                        creatingNoteStart_ = xToTick(mousePos.x, canvasPos, canvasSize);
                        creatingNoteStart_ = midi::snapToGrid(creatingNoteStart_,
                                                              app_.getProject().ticks_per_quarter,
                                                              app_.getGridSnap());
                        creatingNoteEnd_ = creatingNoteStart_;

                        auto* track = app_.getSelectedTrack();
                        if (track) {
                            player_.previewNoteOn(track->channel, creatingNotePitch_, 100);
                        }
                    }
                }
            }

            // Right click: set playhead or set loop region (with Shift)
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (io.KeyShift) {
                    app_.beginUndoGroup();
                    auto transaction = app_.edit();
                    // Start setting loop region
                    mode_ = InteractionMode::SettingLoopRegion;
                    uint32_t tick = xToTick(mousePos.x, canvasPos, canvasSize);
                    tick = midi::snapToGrid(tick, app_.getProject().ticks_per_quarter, midi::GridSnap::Quarter);
                    app_.getProject().loop_start = tick;
                    app_.getProject().loop_end = tick;
                    app_.getProject().loop_enabled = true;
                } else {
                    uint32_t tick = xToTick(mousePos.x, canvasPos, canvasSize);
                    app_.setPlayheadTick(tick);
                }
            }
            break;

        case InteractionMode::SettingLoopRegion:
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                auto transaction = app_.edit();
                uint32_t tick = xToTick(mousePos.x, canvasPos, canvasSize);
                tick = midi::snapToGrid(tick, app_.getProject().ticks_per_quarter, midi::GridSnap::Quarter);
                auto& project = app_.getProject();
                if (tick > project.loop_start) {
                    project.loop_end = tick;
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                auto& project = app_.getProject();
                if (project.loop_end <= project.loop_start) {
                    auto transaction = app_.edit();
                    project.loop_enabled = false;
                }
                app_.endUndoGroup();
                mode_ = InteractionMode::None;
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

        default:
            break;
    }
}

void PianoRoll::handleNoteCreation(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImVec2 mousePos = ImGui::GetMousePos();

    creatingNoteEnd_ = xToTick(mousePos.x, canvasPos, canvasSize);
    creatingNoteEnd_ = midi::snapToGrid(creatingNoteEnd_,
                                        app_.getProject().ticks_per_quarter,
                                        app_.getGridSnap());

    if (creatingNoteEnd_ <= creatingNoteStart_) {
        creatingNoteEnd_ = creatingNoteStart_ + app_.getProject().ticks_per_quarter / 4;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
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

            track->clearSelection();

            auto notes = midi::buildStampedNotes(
                newNote, app_.getHarmonyKind(), app_.getHarmonyTonic(), app_.getHarmonyMinor());
            auto cmd = std::make_unique<AddNotesCommand>(
                app_, app_.getSelectedTrackIndex(), std::move(notes));
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

    if (app_.getGridSnap() != midi::GridSnap::None) {
        int gridSize = app_.getProject().ticks_per_quarter * 4 / static_cast<int>(app_.getGridSnap());
        if (gridSize > 0) tickDelta = (tickDelta / gridSize) * gridSize;
    }

    if (std::abs(mousePos.x - dragStartMouse_.x) > 3 ||
        std::abs(mousePos.y - dragStartMouse_.y) > 3) {
        hasDragged_ = true;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (hasDragged_ && (pitchDelta != 0 || tickDelta != 0)) {
            app_.moveSelectedNotes(pitchDelta, tickDelta);
        }

        mode_ = InteractionMode::None;
    }
}

void PianoRoll::handleNoteResizing(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImVec2 mousePos = ImGui::GetMousePos();

    uint32_t currentTick = xToTick(mousePos.x, canvasPos, canvasSize);
    currentTick = midi::snapToGrid(currentTick, app_.getProject().ticks_per_quarter, app_.getGridSnap());

    int32_t tickDelta = static_cast<int32_t>(currentTick) - static_cast<int32_t>(xToTick(dragStartMouse_.x, canvasPos, canvasSize));

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (tickDelta != 0) {
            app_.resizeSelectedNotes(tickDelta, resizingFromRight_);
        }

        mode_ = InteractionMode::None;
        originalDurations_.clear();
    }
}

void PianoRoll::handleScrollAndZoom(ImVec2 canvasPos, ImVec2 canvasSize) {
    ImGuiIO& io = ImGui::GetIO();
    const auto& project = app_.getProject();

    uint32_t totalTicks = project.getTotalTicks();
    float maxScrollX = static_cast<float>(totalTicks) + (canvasSize.x / pixelsPerTick_) * 0.5f;

    // Middle mouse button drag for panning
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = io.MouseDelta;
        scrollX_ -= delta.x / pixelsPerTick_;
        scrollY_ -= delta.y;
    }

    if (ImGui::IsWindowHovered() && io.MouseWheel != 0) {
        if (io.KeyCtrl) {
            float zoomFactor = io.MouseWheel > 0 ? 1.2f : 0.8f;

            if (io.KeyShift) {
                noteHeight_ = std::clamp(noteHeight_ * zoomFactor, 6.0f, 30.0f);
            } else {
                ImVec2 mousePos = ImGui::GetMousePos();
                float mouseTickBefore = scrollX_ + (mousePos.x - canvasPos.x) / pixelsPerTick_;
                pixelsPerTick_ = std::clamp(pixelsPerTick_ * zoomFactor, 0.01f, 1.0f);
                scrollX_ = mouseTickBefore - (mousePos.x - canvasPos.x) / pixelsPerTick_;
                maxScrollX = static_cast<float>(totalTicks) + (canvasSize.x / pixelsPerTick_) * 0.5f;
            }
        } else if (io.KeyShift) {
            scrollX_ -= io.MouseWheel * 500 / pixelsPerTick_;
        } else {
            scrollY_ -= io.MouseWheel * 50;
        }
    }

    if (ImGui::IsWindowHovered() && io.MouseWheelH != 0) {
        scrollX_ -= io.MouseWheelH * 500 / pixelsPerTick_;
    }

    if (ImGui::IsWindowFocused()) {
        float scrollSpeed = 100.0f / pixelsPerTick_;
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  scrollX_ -= scrollSpeed * io.DeltaTime * 5;
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) scrollX_ += scrollSpeed * io.DeltaTime * 5;
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    scrollY_ -= 200 * io.DeltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  scrollY_ += 200 * io.DeltaTime;

        if (ImGui::IsKeyPressed(ImGuiKey_Home)) scrollX_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End))  scrollX_ = maxScrollX - canvasSize.x / pixelsPerTick_;
    }

    scrollX_ = std::clamp(scrollX_, 0.0f, std::max(0.0f, maxScrollX));
    float maxScrollY = useDrumMap()
        ? static_cast<float>(midi::DRUM_PITCH_COUNT) * drumRowHeight_ - canvasSize.y
        : 127.0f * noteHeight_ - canvasSize.y;
    scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, maxScrollY));
}

void PianoRoll::autoFollowPlayhead(ImVec2 canvasPos, ImVec2 canvasSize) {
    float playheadX = tickToX(app_.getPlayheadTick(), canvasPos, canvasSize);
    float rightEdge = canvasPos.x + canvasSize.x;
    float leftEdge = canvasPos.x;

    // If playhead is past 80% of visible area, scroll to keep it in view
    float threshold = leftEdge + (rightEdge - leftEdge) * 0.8f;

    if (playheadX > threshold) {
        // Scroll so playhead is at 30% of the visible area
        float targetX = leftEdge + (rightEdge - leftEdge) * 0.3f;
        float tickAtTarget = scrollX_ + (targetX - canvasPos.x) / pixelsPerTick_;
        float playheadTick = static_cast<float>(app_.getPlayheadTick());
        scrollX_ += (playheadTick - tickAtTarget);
    }

    // Also follow if playhead jumped backwards (loop)
    if (playheadX < leftEdge) {
        scrollX_ = static_cast<float>(app_.getPlayheadTick()) - (canvasSize.x / pixelsPerTick_) * 0.1f;
    }

    scrollX_ = std::max(0.0f, scrollX_);
}

float PianoRoll::tickToX(uint32_t tick, ImVec2 canvasPos, ImVec2) const {
    return midi::tickToX(tick, canvasPos.x, viewState(canvasPos));
}

uint32_t PianoRoll::xToTick(float x, ImVec2 canvasPos, ImVec2) const {
    return midi::xToTick(x, canvasPos.x, viewState(canvasPos));
}

float PianoRoll::pitchToY(int pitch, ImVec2 canvasPos, ImVec2) const {
    return midi::pitchToY(pitch, canvasPos.y, viewState(canvasPos));
}

int PianoRoll::yToPitch(float y, ImVec2 canvasPos, ImVec2) const {
    return midi::yToPitch(y, canvasPos.y, viewState(canvasPos));
}

PianoRoll::NoteHit PianoRoll::hitTestNote(ImVec2 mousePos, ImVec2 canvasPos, ImVec2 canvasSize) {
    NoteHit result;
    auto* track = app_.getSelectedTrack();
    if (!track) return result;

    auto hit = midi::hitTestNote(*track, mousePos.x, mousePos.y, canvasPos.x, canvasPos.y,
                                 viewState(canvasPos), 6.0f);
    result.noteIndex = hit.note_index;
    result.onLeftEdge = hit.on_left_edge;
    result.onRightEdge = hit.on_right_edge;
    return result;
}

ImU32 PianoRoll::velocityToColor(int velocity) const {
    float t = velocity / 127.0f;
    int r = static_cast<int>(80 + t * 175);
    int g = static_cast<int>(130 - t * 30);
    int b = static_cast<int>(200 - t * 150);
    return IM_COL32(r, g, b, 255);
}

bool PianoRoll::useDrumMap() const {
    auto* track = app_.getSelectedTrack();
    return track && midi::isDrumTrack(*track);
}

float PianoRoll::rowHeight() const {
    return useDrumMap() ? drumRowHeight_ : noteHeight_;
}

void PianoRoll::focusDrumMap(ImVec2 canvasPos, ImVec2 canvasSize) {
    midi::focusDrumMap(canvasPos.y, canvasSize.y, scrollY_, drumRowHeight_);
}
