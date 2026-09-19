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

static void drawCenteredLabel(ImDrawList* dl, const char* label, ImVec2 pos, ImVec2 size) {
    const char* hashPos = strstr(label, "##");
    const char* displayEnd = hashPos ? hashPos : label + strlen(label);
    ImVec2 textSize = ImGui::CalcTextSize(label, displayEnd);
    dl->AddText(
        ImVec2(pos.x + (size.x - textSize.x) * 0.5f,
               pos.y + (size.y - textSize.y) * 0.5f),
        IM_COL32(240, 240, 245, 255), label, displayEnd);
}

bool ToolbarMobile::themedButton(const char* label, ImVec2 size, bool highlighted) {
    return themedButton(label, size, ButtonGroupPos::Solo, highlighted);
}

bool ToolbarMobile::themedButton(const char* label, ImVec2 size, ButtonGroupPos pos, bool highlighted) {
    if (theme_ && theme_->hasButton()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(label, size);
        bool pressed = ImGui::IsItemClicked();
        bool active = ImGui::IsItemActive() || highlighted;
        bool hovered = ImGui::IsItemHovered();

        const NineSlice& ns = theme_->getButton(pos, active);
        ImU32 tint = hovered ? IM_COL32(255, 255, 255, 230) : IM_COL32_WHITE;
        DrawNineSlice(dl, ns, screenPos, size, tint);
        drawCenteredLabel(dl, label, screenPos, size);
        return pressed;
    }

    // Fallback: plain ImGui button
    if (highlighted) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
    }
    bool pressed = ImGui::Button(label, size);
    if (highlighted) {
        ImGui::PopStyleColor();
    }
    return pressed;
}

void ToolbarMobile::themedGroupLabel(const char* text, ImVec2 size) {
    if (theme_ && theme_->hasButton()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();

        const NineSlice& ns = theme_->getButton(ButtonGroupPos::Center, false);
        DrawNineSlice(dl, ns, screenPos, size);
        drawCenteredLabel(dl, text, screenPos, size);

        // Advance cursor as if we placed a widget
        ImGui::Dummy(size);
    } else {
        // Fallback: just centered text at button height
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::Text("%s", text);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (size.y - ImGui::GetTextLineHeight()) * 0.5f);
    }
}

void ToolbarMobile::render(float displayWidth) {
    auto& project = app_.getProject();

    float buttonSize = 44.0f;
    float padding = 8.0f;
    float rowHeight = buttonSize + padding * 2;
    height_ = rowHeight * 2 + 4.0f;

    // Tighter spacing within groups; the nine-slice edges handle visual separation
    float groupGap = (theme_ && theme_->hasButtonGroup()) ? 0.0f : 4.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 10));

    // === Row 1: File operations + Transport group + Time ===
    ImGui::BeginGroup();

    // Open button (standalone)
    if (themedButton("Open", ImVec2(buttonSize * 1.2f, buttonSize)) && fileOps_) {
        auto open = [this] { fileOps_->openFile([this](const std::string& path) {
            if (app_.loadFile(path)) {
                player_.syncTrackPrograms(app_.getProject());
            } else if (fileSafety_) fileSafety_->showError("Could not open MIDI file. Your current project is still open.");
        }); };
        if (fileSafety_) fileSafety_->request(open); else open();
    }
    ImGui::SameLine();

    // Save button (standalone)
    if (themedButton("Save", ImVec2(buttonSize * 1.2f, buttonSize))) {
        if (!project.filepath.empty()) {
            if (!app_.saveFile() && fileSafety_) fileSafety_->showError("Save failed. Your changes are still open.");
        } else {
            if (fileOps_) fileOps_->saveFile(app_, "project.mid");
        }
    }
    ImGui::SameLine();

    ImGui::Dummy(ImVec2(2, 0));
    ImGui::SameLine();

    // Transport group: [Play | Pause | Stop]
    bool isPlaying = app_.isPlaying();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(groupGap, 4));
    if (themedButton("Play", ImVec2(buttonSize * 1.2f, buttonSize), ButtonGroupPos::Left, isPlaying)) {
        if (!isPlaying) {
            app_.setPlaying(true);
        }
    }
    ImGui::SameLine();

    if (themedButton("||##pause", ImVec2(buttonSize, buttonSize), ButtonGroupPos::Center)) {
        if (isPlaying) {
            app_.setPlaying(false);
        }
    }
    ImGui::SameLine();

    if (themedButton("Stop", ImVec2(buttonSize * 1.2f, buttonSize), ButtonGroupPos::Right)) {
        app_.stop();
        player_.panic();
    }
    ImGui::PopStyleVar(); // restore ItemSpacing
    ImGui::SameLine();

    // Time display
    double seconds = project.ticksToSeconds(app_.getPlayheadTick());
    int minutes = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);

    int bar = project.tickToBar(app_.getPlayheadTick());
    int beat = project.tickToBeatInBar(app_.getPlayheadTick());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%02d:%02d.%03d  B%d.%d", minutes, secs, ms, bar, beat);

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

    // === Row 2: BPM group + Grid + Mode ===
    ImGui::BeginGroup();

    // BPM group: [- | BPM: 120 | +]
    char bpmText[32];
    snprintf(bpmText, sizeof(bpmText), "BPM: %.0f", project.tempo_bpm);
    float bpmLabelWidth = std::max(80.0f, ImGui::CalcTextSize(bpmText).x + 16.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(groupGap, 4));
    if (themedButton("-##bpm", ImVec2(buttonSize, buttonSize), ButtonGroupPos::Left)) {
        auto transaction = app_.edit();
        project.tempo_bpm = std::max(20.0f, project.tempo_bpm - 1.0f);
        project.modified = true;
    }
    ImGui::SameLine();

    themedGroupLabel(bpmText, ImVec2(bpmLabelWidth, buttonSize));
    ImGui::SameLine();

    if (themedButton("+##bpm", ImVec2(buttonSize, buttonSize), ButtonGroupPos::Right)) {
        auto transaction = app_.edit();
        project.tempo_bpm = std::min(300.0f, project.tempo_bpm + 1.0f);
        project.modified = true;
    }
    ImGui::PopStyleVar(); // restore ItemSpacing
    ImGui::SameLine();

    ImGui::Dummy(ImVec2(2, 0));
    ImGui::SameLine();

    // Grid snap selector
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

    if (ThemedCombo("##grid_mobile", &currentGridIndex, gridNames, 7,
                    theme_, 80, buttonSize)) {
        app_.setGridSnap(gridValues[currentGridIndex]);
    }
    ImGui::SameLine();

    // Scroll / Edit mode toggle (standalone)
    bool wasScrollMode = scrollMode_;
    if (!theme_ && wasScrollMode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.35f, 1.0f));
    }
    const char* modeLabel = wasScrollMode ? "Scroll" : "Edit";
    if (themedButton(modeLabel, ImVec2(buttonSize * 1.5f, buttonSize), wasScrollMode)) {
        scrollMode_ = !scrollMode_;
    }
    if (!theme_ && wasScrollMode) {
        ImGui::PopStyleColor(2);
    }

    ImGui::EndGroup();

    ImGui::PopStyleVar(2);
}
