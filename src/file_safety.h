#pragma once

#include "app.h"
#include <chrono>
#include <functional>
#include <string>

class FileSafety {
public:
    explicit FileSafety(App& app) : app_(app) {}

    enum class Decision { Save, Discard, Cancel };

    void request(std::function<void()> action);
    bool resolve(Decision decision, const std::string& savePath = "");
    bool hasPendingAction() const { return bool(pending_); }

    void tickAutosave();
    void showError(const std::string& error) { error_ = error; showError_ = true; }
    void clearError() { error_.clear(); }

    App& app() { return app_; }
    const App& app() const { return app_; }
    const std::string& error() const { return error_; }
    std::string& savePath() { return savePath_; }
    const std::string& savePath() const { return savePath_; }
    bool shouldShowPrompt() const { return showPrompt_; }
    void clearShowPrompt() { showPrompt_ = false; }
    bool shouldShowError() const { return showError_; }
    void clearShowError() { showError_ = false; }
    bool shouldOpenRecoveryPopup();
    void markRecoveryChecked() { checkedRecovery_ = true; }

private:
    App& app_;
    std::function<void()> pending_;
    bool showPrompt_ = false;
    bool showError_ = false;
    bool checkedRecovery_ = false;
    std::string savePath_;
    std::string error_;
    std::chrono::steady_clock::time_point lastAutosave_ = std::chrono::steady_clock::now();
};
