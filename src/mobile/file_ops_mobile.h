#pragma once

#include "../app.h"
#include <string>
#include <functional>

// Platform-specific file operations for mobile.
// iOS: UIDocumentPickerViewController
// Android: Intent.ACTION_OPEN_DOCUMENT / ACTION_CREATE_DOCUMENT
// Desktop preview: simple file path input via ImGui popup
class FileOpsMobile {
public:
    static void openFile(std::function<void(const std::string&)> onFileSelected);
    static void saveFile(App& app, const std::string& suggestedName);
    static void renderDialogs();
    static bool isDialogOpen();

private:
    static bool openDialogPending_;
    static bool saveDialogPending_;
    static std::function<void(const std::string&)> openCallback_;
    static App* saveApp_;
    static std::string saveSuggestedName_;
    static char pathBuffer_[512];
    static std::string errorMessage_;
};
