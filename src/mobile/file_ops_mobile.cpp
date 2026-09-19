#include "file_ops_mobile.h"
#include "../file_browser.h"
#include <imgui.h>
#include <cstring>
#include <filesystem>

void FileOpsMobile::openFile(std::function<void(const std::string&)> onFileSelected) {
    openDialogPending_ = true;
    openCallback_ = onFileSelected;
    std::memset(pathBuffer_, 0, sizeof(pathBuffer_));
    errorMessage_.clear();
    browseDirectory_ = defaultBrowseDirectory();
}

void FileOpsMobile::saveFile(App& app, const std::string& suggestedName) {
    saveDialogPending_ = true;
    saveApp_ = &app;
    saveSuggestedName_ = suggestedName;
    std::strncpy(pathBuffer_, suggestedName.c_str(), sizeof(pathBuffer_) - 1);
    errorMessage_.clear();
    browseDirectory_ = defaultBrowseDirectory();
}

void FileOpsMobile::renderBrowser(const char* popup_id, char* path_buffer, size_t path_size) {
    if (browseDirectory_.empty()) browseDirectory_ = defaultBrowseDirectory();

    ImGui::Text("Browse: %s", browseDirectory_.c_str());
    if (ImGui::Button("Up")) {
        browseDirectory_ = parentDirectory(browseDirectory_);
    }

    if (ImGui::BeginChild("##file_browser", ImVec2(320, 160), true)) {
        for (const auto& entry : listDirectory(browseDirectory_)) {
            std::string full_path = (std::filesystem::path(browseDirectory_) / entry.name).string();
            if (entry.is_directory) {
                if (ImGui::Selectable(("[dir] " + entry.name).c_str())) {
                    browseDirectory_ = full_path;
                }
            } else if (ImGui::Selectable(entry.name.c_str())) {
                std::strncpy(path_buffer, full_path.c_str(), path_size - 1);
                path_buffer[path_size - 1] = 0;
            }
        }
    }
    ImGui::EndChild();
}

void FileOpsMobile::renderOpenDialog() {
    if (openDialogPending_) {
        ImGui::OpenPopup("Open MIDI File##mobile");
        openDialogPending_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0));

    if (!ImGui::BeginPopupModal("Open MIDI File##mobile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    renderBrowser("open_browser", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SetNextItemWidth(-1);
    bool submit = ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_),
                                   ImGuiInputTextFlags_EnterReturnsTrue);

    if (!errorMessage_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", errorMessage_.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::Button("Open", ImVec2(150, 44)) || submit) {
        std::string path = pathBuffer_;
        if (path.empty()) {
            errorMessage_ = "Please enter a file path.";
        } else if (openCallback_) {
            openCallback_(path);
            errorMessage_.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(150, 44))) {
        errorMessage_.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void FileOpsMobile::renderSaveDialog() {
    if (saveDialogPending_) {
        ImGui::OpenPopup("Save MIDI File##mobile");
        saveDialogPending_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0));

    if (!ImGui::BeginPopupModal("Save MIDI File##mobile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    renderBrowser("save_browser", pathBuffer_, sizeof(pathBuffer_));
    ImGui::SetNextItemWidth(-1);
    bool submit = ImGui::InputText("Path", pathBuffer_, sizeof(pathBuffer_),
                                   ImGuiInputTextFlags_EnterReturnsTrue);

    if (!errorMessage_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", errorMessage_.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::Button("Save", ImVec2(150, 44)) || submit) {
        std::string path = pathBuffer_;
        if (path.empty()) {
            errorMessage_ = "Please enter a file path.";
        } else {
            if (path.find(".mid") == std::string::npos && path.find(".MID") == std::string::npos) {
                path += ".mid";
            }
            if (saveApp_ && saveApp_->saveFileAs(path)) {
                errorMessage_.clear();
                ImGui::CloseCurrentPopup();
            } else {
                errorMessage_ = "Failed to save file.";
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(150, 44))) {
        errorMessage_.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void FileOpsMobile::renderDialogs() {
    renderOpenDialog();
    renderSaveDialog();
}

bool FileOpsMobile::isDialogOpen() const {
    return ImGui::IsPopupOpen("Open MIDI File##mobile") || ImGui::IsPopupOpen("Save MIDI File##mobile");
}
