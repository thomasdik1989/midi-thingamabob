#include "toolbar_mobile.h"
#include "file_ops_mobile.h"
#include "../midi/types.h"
#include <cmath>
#include <cstring>
#include <algorithm>

ToolbarMobile::ToolbarMobile(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void ToolbarMobile::render(float displayWidth) {
    auto& project = app_.getProject();

    float buttonSize = 44.0f;
    float padding = 8.0f;
    float rowHeight = buttonSize + padding * 2;
    height_ = rowHeight * 2 + 4.0f; // Two rows + separator

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));

    // === Row 1: File operations + Transport + Time ===
    ImGui::BeginGroup();

    // Open button
    if (ImGui::Button("Open", ImVec2(buttonSize * 1.2f, buttonSize))) {
        FileOpsMobile::openFile([this](const std::string& path) {
            if (app_.loadFile(path)) {
                for (const auto& track : app_.getProject().tracks) {
                    player_.sendProgramChange(track.channel, track.program);
                }
            }
        });
    }
    ImGui::SameLine();

    // Save button
    if (ImGui::Button("Save", ImVec2(buttonSize * 1.2f, buttonSize))) {
        if (!project.filepath.empty()) {
            app_.saveFile();
        } else {
            FileOpsMobile::saveFile(app_, "project.mid");
        }
    }
    ImGui::SameLine();

    // Spacer
    ImGui::Dummy(ImVec2(4, 0));
    ImGui::SameLine();

    // Play button
    bool isPlaying = app_.isPlaying();
    if (isPlaying) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
    }
    if (ImGui::Button("Play", ImVec2(buttonSize * 1.2f, buttonSize))) {
        if (!isPlaying) {
            app_.setPlaying(true);
        }
    }
    if (isPlaying) {
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();

    // Pause button
    if (ImGui::Button("||", ImVec2(buttonSize, buttonSize))) {
        if (isPlaying) {
            app_.setPlaying(false);
        }
    }
    ImGui::SameLine();

    // Stop button
    if (ImGui::Button("Stop", ImVec2(buttonSize * 1.2f, buttonSize))) {
        app_.stop();
        player_.panic();
    }
    ImGui::SameLine();

    // Time display
    double seconds = project.ticksToSeconds(app_.getPlayheadTick());
    int minutes = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%02d:%02d.%03d", minutes, secs, ms);

    ImGui::EndGroup();

    // Thin separator
    ImGui::Spacing();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 sepStart = ImGui::GetCursorScreenPos();
    drawList->AddLine(
        sepStart,
        ImVec2(sepStart.x + displayWidth, sepStart.y),
        IM_COL32(60, 60, 70, 255)
    );
    ImGui::Spacing();

    // === Row 2: BPM + Grid ===
    ImGui::BeginGroup();

    // BPM minus
    if (ImGui::Button("-##bpm", ImVec2(buttonSize, buttonSize))) {
        project.tempo_bpm = std::max(20.0f, project.tempo_bpm - 1.0f);
        project.modified = true;
    }
    ImGui::SameLine();

    // BPM display
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("BPM: %.0f", project.tempo_bpm);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (buttonSize - ImGui::GetTextLineHeight()) * 0.5f);

    // BPM plus
    if (ImGui::Button("+##bpm", ImVec2(buttonSize, buttonSize))) {
        project.tempo_bpm = std::min(300.0f, project.tempo_bpm + 1.0f);
        project.modified = true;
    }
    ImGui::SameLine();

    // Spacer
    ImGui::Dummy(ImVec2(10, 0));
    ImGui::SameLine();

    // Grid snap selector
    // We need to make sure this is represented in the piano roll.
    static const char* gridNames[] = { "Off", "1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    static const midi::GridSnap gridValues[] = {
        midi::GridSnap::None,
        midi::GridSnap::Whole,
        midi::GridSnap::Half,
        midi::GridSnap::Quarter,
        midi::GridSnap::Eighth,
        midi::GridSnap::Sixteenth,
        midi::GridSnap::ThirtySecond
    };

    int currentGridIndex = 0;
    midi::GridSnap currentSnap = app_.getGridSnap();
    for (int i = 0; i < 7; ++i) {
        if (gridValues[i] == currentSnap) {
            currentGridIndex = i;
            break;
        }
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("Grid:");
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (buttonSize - ImGui::GetTextLineHeight()) * 0.5f);

    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##grid_mobile", &currentGridIndex, gridNames, 7)) {
        app_.setGridSnap(gridValues[currentGridIndex]);
    }
    ImGui::SameLine();

    // Spacer
    ImGui::Dummy(ImVec2(10, 0));
    ImGui::SameLine();

    // Scroll / Edit mode toggle button
    bool wasScrollMode = scrollMode_;
    if (wasScrollMode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.35f, 1.0f));
    }
    const char* modeLabel = wasScrollMode ? "Scroll" : "Edit";
    if (ImGui::Button(modeLabel, ImVec2(buttonSize * 1.5f, buttonSize))) {
        scrollMode_ = !scrollMode_;
    }
    if (wasScrollMode) {
        ImGui::PopStyleColor(2);
    }

    ImGui::EndGroup();

    ImGui::PopStyleVar(2);
}
