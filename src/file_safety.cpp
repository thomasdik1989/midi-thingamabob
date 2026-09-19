#include "file_safety.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

void FileSafety::request(std::function<void()> action) {
    if (pending_) return;
    app_.endUndoGroup();
    if (!app_.getProject().modified) {
        action();
        return;
    }
    pending_ = std::move(action);
    showPrompt_ = true;
    error_.clear();
    savePath_ = app_.getProject().filepath;
}

bool FileSafety::resolve(Decision decision, const std::string& savePath) {
    if (!pending_) return false;
    if (decision == Decision::Cancel) {
        pending_ = {};
        error_.clear();
        showPrompt_ = false;
        return true;
    }
    if (decision == Decision::Save) {
        std::string path = savePath.empty() ? app_.getProject().filepath : savePath;
        if (path.empty()) {
            error_ = "Enter a file path first.";
            return false;
        }
        auto extension = std::filesystem::path(path).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (extension != ".mid" && extension != ".midi") path += ".mid";
        if (!app_.saveFileAs(path)) {
            error_ = "Save failed. Your changes are still open. Choose a writable path or cancel.";
            return false;
        }
    }
    auto action = std::move(pending_);
    pending_ = {};
    showPrompt_ = false;
    error_.clear();
    action();
    return true;
}

void FileSafety::tickAutosave() {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastAutosave_ < std::chrono::seconds(10)) return;
    if (!app_.autosave()) {
        showError("Recovery could not be saved. Please save your work to a writable location.");
    }
    lastAutosave_ = now;
}

bool FileSafety::shouldOpenRecoveryPopup() {
    if (checkedRecovery_) return false;
    checkedRecovery_ = true;
    return !app_.recoveryFiles().empty();
}
