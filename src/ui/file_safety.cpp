#include "file_safety.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

void renderFileSafety(FileSafety& safety) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1)) {
        safety.app().endUndoGroup();
    }

    safety.tickAutosave();

    const float dialogWidth = std::clamp(ImGui::GetIO().DisplaySize.x - 24.0f, 240.0f, 460.0f);
    auto sizeDialog = [&] {
        ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    };

    if (safety.shouldOpenRecoveryPopup()) {
        ImGui::OpenPopup("Recover unsaved work");
    }

    sizeDialog();
    if (ImGui::BeginPopupModal("Recover unsaved work", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Recovery snapshots were found. Choose one to restore, or keep them for later.");
        for (const auto& path : safety.app().recoveryFiles()) {
            ImGui::PushID(path.c_str());
            if (ImGui::Button(("Recover " + std::filesystem::path(path).filename().string()).c_str(), ImVec2(-1, 44))) {
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
        if (ImGui::Button("Later", ImVec2(120, 44))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (safety.shouldShowPrompt()) {
        ImGui::OpenPopup("Unsaved changes");
        safety.clearShowPrompt();
    }

    sizeDialog();
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
        if (ImGui::Button("Save", ImVec2(buttonWidth, 44)) &&
            safety.resolve(FileSafety::Decision::Save, safety.savePath())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(buttonWidth, 44)) &&
            safety.resolve(FileSafety::Decision::Discard)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 44)) &&
            safety.resolve(FileSafety::Decision::Cancel)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (safety.shouldShowError()) {
        ImGui::OpenPopup("File error");
        safety.clearShowError();
    }

    sizeDialog();
    if (ImGui::BeginPopupModal("File error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", safety.error().c_str());
        if (ImGui::Button("OK", ImVec2(120, 44))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
