#include "file_ops_mobile.h"
#include <imgui.h>
#include <cstring>

// Static member definitions
bool FileOpsMobile::openDialogPending_ = false;
bool FileOpsMobile::saveDialogPending_ = false;
std::function<void(const std::string&)> FileOpsMobile::openCallback_;
App* FileOpsMobile::saveApp_ = nullptr;
std::string FileOpsMobile::saveSuggestedName_;
char FileOpsMobile::pathBuffer_[512] = {0};
std::string FileOpsMobile::errorMessage_;

void FileOpsMobile::openFile(std::function<void(const std::string&)> onFileSelected) {
#if defined(__IPHONEOS__)
    // iOS: Use UIDocumentPickerViewController via Objective-C bridge
    // For now, fall back to ImGui dialog (native picker requires ObjC integration)
    openDialogPending_ = true;
    openCallback_ = onFileSelected;
    std::memset(pathBuffer_, 0, sizeof(pathBuffer_));
    errorMessage_.clear();
#elif defined(__ANDROID__)
    // Android: Use JNI to launch Intent.ACTION_OPEN_DOCUMENT
    // For now, fall back to ImGui dialog
    openDialogPending_ = true;
    openCallback_ = onFileSelected;
    std::memset(pathBuffer_, 0, sizeof(pathBuffer_));
    errorMessage_.clear();
#else
    // Desktop preview: use ImGui dialog
    openDialogPending_ = true;
    openCallback_ = onFileSelected;
    std::memset(pathBuffer_, 0, sizeof(pathBuffer_));
    errorMessage_.clear();
#endif
}

// TODO make this work :D it just throws an error for now.
void FileOpsMobile::saveFile(App& app, const std::string& suggestedName) {
    // Not sure If it needs to be like this but we can split things like this.
#if defined(__IPHONEOS__)
    // iOS: Save to app sandbox, then offer share thing
    // For now, fall back to ImGui dialog
    saveDialogPending_ = true;
    saveApp_ = &app;
    saveSuggestedName_ = suggestedName;
    std::strncpy(pathBuffer_, suggestedName.c_str(), sizeof(pathBuffer_) - 1);
    errorMessage_.clear();
#elif defined(__ANDROID__)
    // Android: Use JNI to launch Intent.ACTION_CREATE_DOCUMENT
    // For now, fall back to ImGui dialog
    saveDialogPending_ = true;
    saveApp_ = &app;
    saveSuggestedName_ = suggestedName;
    std::strncpy(pathBuffer_, suggestedName.c_str(), sizeof(pathBuffer_) - 1);
    errorMessage_.clear();
#else
    // Desktop preview
    saveDialogPending_ = true;
    saveApp_ = &app;
    saveSuggestedName_ = suggestedName;
    std::strncpy(pathBuffer_, suggestedName.c_str(), sizeof(pathBuffer_) - 1);
    errorMessage_.clear();
#endif
}

void FileOpsMobile::renderDialogs() {
    // Open file dialog
    if (openDialogPending_) {
        ImGui::OpenPopup("Open MIDI File##mobile");
        openDialogPending_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350, 0));

    if (ImGui::BeginPopupModal("Open MIDI File##mobile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter file path:");
        ImGui::SetNextItemWidth(-1);

        bool submit = ImGui::InputText("##open_path", pathBuffer_, sizeof(pathBuffer_),
                                       ImGuiInputTextFlags_EnterReturnsTrue);

        if (!errorMessage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", errorMessage_.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        float btnWidth = 150.0f;
        if (ImGui::Button("Open", ImVec2(btnWidth, 44)) || submit) {
            std::string path = pathBuffer_;
            if (path.empty()) {
                errorMessage_ = "Please enter a file path.";
            } else {
                if (openCallback_) {
                    openCallback_(path);
                }
                errorMessage_.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(btnWidth, 44))) {
            errorMessage_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // Save file dialog
    if (saveDialogPending_) {
        ImGui::OpenPopup("Save MIDI File##mobile");
        saveDialogPending_ = false;
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350, 0));

    if (ImGui::BeginPopupModal("Save MIDI File##mobile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter file path:");
        ImGui::SetNextItemWidth(-1);

        bool submit = ImGui::InputText("##save_path", pathBuffer_, sizeof(pathBuffer_),
                                       ImGuiInputTextFlags_EnterReturnsTrue);

        if (!errorMessage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", errorMessage_.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        float btnWidth = 150.0f;
        if (ImGui::Button("Save", ImVec2(btnWidth, 44)) || submit) {
            std::string path = pathBuffer_;
            if (path.empty()) {
                errorMessage_ = "Please enter a file path.";
            } else {
                // Add .mid extension if not present
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
        if (ImGui::Button("Cancel", ImVec2(btnWidth, 44))) {
            errorMessage_.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool FileOpsMobile::isDialogOpen() {
    return openDialogPending_ || saveDialogPending_;
}
