#include "file_safety_mobile.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace {
static void pushModalStyle(const NineSliceTheme* theme) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    if (theme && theme->hasCard()) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    }
}

static void popModalStyle(const NineSliceTheme* theme) {
    if (theme && theme->hasCard()) {
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();
}

static void drawModalBackground(const NineSliceTheme* theme) {
    if (theme && theme->hasCard()) {
        DrawNineSlice(ImGui::GetWindowDrawList(), theme->card,
                      ImGui::GetWindowPos(), ImGui::GetWindowSize());
    }
}

static bool themedModalButton(const char* label, ImVec2 size, const NineSliceTheme* theme) {
    if (theme && theme->hasButton()) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(label, size);
        bool pressed = ImGui::IsItemClicked();
        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        const NineSlice& slice = theme->getButton(ButtonGroupPos::Solo, active);
        ImU32 tint = hovered ? IM_COL32(255, 255, 255, 230) : IM_COL32_WHITE;
        DrawNineSlice(drawList, slice, screenPos, size, tint);
        const char* hashPos = std::strstr(label, "##");
        const char* displayEnd = hashPos ? hashPos : label + std::strlen(label);
        ImVec2 textSize = ImGui::CalcTextSize(label, displayEnd);
        drawList->AddText(
            ImVec2(screenPos.x + (size.x - textSize.x) * 0.5f,
                   screenPos.y + (size.y - textSize.y) * 0.5f),
            IM_COL32(240, 240, 245, 255), label, displayEnd);
        return pressed;
    }
    return ImGui::Button(label, size);
}
}

void renderFileSafetyMobile(FileSafety& safety, const NineSliceTheme* theme) {
    safety.tickAutosave();

    const float dialogWidth = std::clamp(ImGui::GetIO().DisplaySize.x - 24.0f, 240.0f, 460.0f);
    auto sizeDialog = [&] {
        ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    };

    if (safety.shouldOpenRecoveryPopup()) {
        ImGui::OpenPopup("Recover unsaved work##mobile");
    }

    sizeDialog();
    pushModalStyle(theme);
    if (ImGui::BeginPopupModal("Recover unsaved work##mobile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        drawModalBackground(theme);
        ImGui::TextWrapped("Recovery snapshots were found. Choose one to restore, or keep them for later.");
        for (const auto& path : safety.app().recoveryFiles()) {
            ImGui::PushID(path.c_str());
            if (themedModalButton(("Recover " + std::filesystem::path(path).filename().string() + "##recover").c_str(),
                                  ImVec2(-1, 44), theme)) {
                if (safety.app().recover(path)) {
                    safety.clearError();
                    ImGui::CloseCurrentPopup();
                } else {
                    safety.showError("This recovery snapshot could not be read.");
                }
            }
            ImGui::PopID();
        }
        if (!safety.error().empty()) ImGui::TextWrapped("%s", safety.error().c_str());
        if (themedModalButton("Later##recovery", ImVec2(120, 44), theme)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    popModalStyle(theme);

    if (safety.shouldShowPrompt()) {
        ImGui::OpenPopup("Unsaved changes##mobile");
        safety.clearShowPrompt();
    }

    sizeDialog();
    pushModalStyle(theme);
    if (ImGui::BeginPopupModal("Unsaved changes##mobile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        drawModalBackground(theme);
        ImGui::TextWrapped("Save your changes before continuing?");
        ImGui::TextUnformatted("Save path");
        ImGui::SetNextItemWidth(-1);
        char pathBuffer[1024] = {};
        std::strncpy(pathBuffer, safety.savePath().c_str(), sizeof(pathBuffer) - 1);
        if (ImGui::InputText("##save_path", pathBuffer, sizeof(pathBuffer))) {
            safety.savePath() = pathBuffer;
        }
        const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3;
        if (!safety.error().empty()) ImGui::TextWrapped("%s", safety.error().c_str());
        if (themedModalButton("Save##unsaved", ImVec2(buttonWidth, 44), theme) &&
            safety.resolve(FileSafety::Decision::Save, safety.savePath())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (themedModalButton("Discard##unsaved", ImVec2(buttonWidth, 44), theme) &&
            safety.resolve(FileSafety::Decision::Discard)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (themedModalButton("Cancel##unsaved", ImVec2(buttonWidth, 44), theme) &&
            safety.resolve(FileSafety::Decision::Cancel)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    popModalStyle(theme);

    if (safety.shouldShowError()) {
        ImGui::OpenPopup("File error##mobile");
        safety.clearShowError();
    }

    sizeDialog();
    pushModalStyle(theme);
    if (ImGui::BeginPopupModal("File error##mobile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        drawModalBackground(theme);
        ImGui::TextWrapped("%s", safety.error().c_str());
        if (themedModalButton("OK##file_error", ImVec2(120, 44), theme)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    popModalStyle(theme);
}
