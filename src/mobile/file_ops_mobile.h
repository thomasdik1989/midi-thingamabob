#pragma once

#include "../app.h"
#include <string>
#include <functional>

class FileOpsMobile {
public:
    void openFile(std::function<void(const std::string&)> onFileSelected);
    void saveFile(App& app, const std::string& suggestedName);
    void renderDialogs();
    bool isDialogOpen() const;

private:
    void renderOpenDialog();
    void renderSaveDialog();
    void renderBrowser(const char* popup_id, char* path_buffer, size_t path_size);

    bool openDialogPending_ = false;
    bool saveDialogPending_ = false;
    std::function<void(const std::string&)> openCallback_;
    App* saveApp_ = nullptr;
    std::string saveSuggestedName_;
    char pathBuffer_[512] = {};
    std::string errorMessage_;
    std::string browseDirectory_;
};
