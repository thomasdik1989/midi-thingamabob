#include "main_window.h"
#include "../file_browser.h"
#include <filesystem>
#include "../midi/patterns.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>

MainWindow::MainWindow(App& app)
    : app_(app)
    , fileSafety_(app)
    , toolbar_(app, midiPlayer_)
    , trackPanel_(app, midiPlayer_)
    , pianoRoll_(app, midiPlayer_)
    , lastFrame_(std::chrono::steady_clock::now())
{
}

MainWindow::~MainWindow() = default;

void MainWindow::render() {
    // Calculate delta time
    auto now = std::chrono::steady_clock::now();
    double deltaTime = std::chrono::duration<double>(now - lastFrame_).count();
    lastFrame_ = now;

    // Update playback
    if (app_.isPlaying()) {
        app_.advancePlayhead(deltaTime);
    }
    midiPlayer_.update(app_.getProject(), app_.getPlayheadTick(), app_.isPlaying(), app_.playbackSpans());

    // Handle keyboard shortcuts
    handleKeyboardShortcuts();

    // Render menu bar
    renderMenuBar();

    // Render dockspace
    renderDockspace();

    // Render panels
    toolbar_.render();
    trackPanel_.render();
    pianoRoll_.render();

    // Handle file dialogs
    handleFileDialogs();

    renderFileSafety(fileSafety_);
    firstFrame_ = false;
}

void MainWindow::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                fileSafety_.request([this] {
                    midiPlayer_.panic();
                    app_.newProject();
                    midiPlayer_.syncTrackPrograms(app_.getProject());
                });
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                showOpenDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (app_.getProject().filepath.empty()) showSaveDialog();
                else if (!app_.saveFile()) fileSafety_.showError("Save failed. Your changes are still open.");
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                showSaveDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                requestClose();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, app_.canUndo())) {
                app_.undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, app_.canRedo())) {
                app_.redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                app_.selectAllNotes();
            }
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                app_.copySelectedNotes();
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, app_.hasClipboard())) {
                app_.pasteNotes();
            }
            if (ImGui::MenuItem("Delete", "Delete")) {
                app_.deleteSelectedNotes();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quantize", "Q")) {
                app_.quantizeSelectedNotes();
            }
            if (ImGui::MenuItem("Harmonize", "H")) {
                app_.harmonizeSelectedNotes();
            }
            if (ImGui::BeginMenu("Insert Beat")) {
                static const int bar_counts[] = {1, 2, 4, 8, 16, 32, 64};
                for (int g = 0; g < midi::drumGrooveCount(); ++g) {
                    const auto& groove = midi::getDrumGroove(g);
                    if (ImGui::BeginMenu(groove.name)) {
                        for (int bars : bar_counts) {
                            char label[64];
                            snprintf(label, sizeof(label), "%d bar%s", bars, bars == 1 ? "" : "s");
                            if (ImGui::MenuItem(label)) {
                                app_.insertDrumGroove(g, bars);
                            }
                        }
                        if (ImGui::MenuItem("To song end")) {
                            app_.insertDrumGroove(g, 0);
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Track")) {
            if (ImGui::MenuItem("Add Track")) {
                app_.addTrack();
            }
            if (ImGui::MenuItem("Add Drum Track")) {
                app_.addDrumTrack();
            }
            if (ImGui::MenuItem("Remove Track", nullptr, false,
                                app_.getProject().tracks.size() > 1)) {
                app_.removeTrack(app_.getSelectedTrackIndex());
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Transport")) {
            if (ImGui::MenuItem("Play/Pause", "Space")) {
                app_.togglePlayback();
            }
            if (ImGui::MenuItem("Stop", "Enter")) {
                app_.stop();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Panic (All Notes Off)")) {
                midiPlayer_.panic();
            }
            ImGui::EndMenu();
        }

        // Display project info on the right
        float windowWidth = ImGui::GetWindowWidth();
        std::string info = app_.getProject().filepath.empty() ? "New Project" : app_.getProject().filepath;
        if (app_.getProject().modified) info += " *";
        float textWidth = ImGui::CalcTextSize(info.c_str()).x;
        ImGui::SetCursorPosX(windowWidth - textWidth - 20);
        ImGui::TextDisabled("%s", info.c_str());

        ImGui::EndMainMenuBar();
    }
}

void MainWindow::renderDockspace() {
    // Create a fullscreen dockspace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // Setup default layout on first frame
    if (firstFrame_) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.2f, nullptr, &dock_main);
        ImGuiID dock_top = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.08f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Toolbar", dock_top);
        ImGui::DockBuilderDockWindow("Tracks", dock_left);
        ImGui::DockBuilderDockWindow("Piano Roll", dock_main);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

void MainWindow::handleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();

    // Don't handle shortcuts if typing in an input field
    if (io.WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;

    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;

    // File operations
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_N)) {
        fileSafety_.request([this] {
            midiPlayer_.panic();
            app_.newProject();
            midiPlayer_.syncTrackPrograms(app_.getProject());
        });
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        showOpenDialog();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (app_.getProject().filepath.empty()) {
            showSaveDialog();
        } else {
            if (!app_.saveFile()) fileSafety_.showError("Save failed. Your changes are still open.");
        }
    }
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        showSaveDialog();
    }

    // Edit operations
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app_.undo();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        app_.redo();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_A)) {
        app_.selectAllNotes();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_C)) {
        app_.copySelectedNotes();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_V)) {
        app_.pasteNotes();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        app_.deleteSelectedNotes();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
        app_.quantizeSelectedNotes();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_H)) {
        app_.harmonizeSelectedNotes();
    }

    // Transport
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        app_.togglePlayback();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        app_.stop();
    }
}

void MainWindow::showOpenDialog() {
    fileSafety_.request([this] {
        showOpenFileDialog_ = true;
        openErrorMessage_.clear();
        std::memset(filePathBuffer_, 0, sizeof(filePathBuffer_));
        browseDirectory_ = defaultBrowseDirectory();
    });
}

void MainWindow::showSaveDialog() {
    showSaveFileDialog_ = true;
    std::strncpy(filePathBuffer_, app_.getProject().filepath.c_str(), sizeof(filePathBuffer_) - 1);
    browseDirectory_ = defaultBrowseDirectory();
}

void MainWindow::handleFileDialogs() {
    // Open file dialog
    if (showOpenFileDialog_) {
        ImGui::OpenPopup("Open MIDI File");
        showOpenFileDialog_ = false;
    }

    if (ImGui::BeginPopupModal("Open MIDI File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (browseDirectory_.empty()) browseDirectory_ = defaultBrowseDirectory();
        ImGui::Text("Browse: %s", browseDirectory_.c_str());
        if (ImGui::Button("Up")) browseDirectory_ = parentDirectory(browseDirectory_);
        if (ImGui::BeginChild("##open_browser", ImVec2(400, 160), true)) {
            for (const auto& entry : listDirectory(browseDirectory_)) {
                std::string full_path = (std::filesystem::path(browseDirectory_) / entry.name).string();
                if (entry.is_directory) {
                    if (ImGui::Selectable(("[dir] " + entry.name).c_str())) {
                        browseDirectory_ = full_path;
                    }
                } else if (ImGui::Selectable(entry.name.c_str())) {
                    std::strncpy(filePathBuffer_, full_path.c_str(), sizeof(filePathBuffer_) - 1);
                }
            }
        }
        ImGui::EndChild();
        ImGui::Text("Enter file path:");
        ImGui::SetNextItemWidth(400);
        if (ImGui::InputText("##filepath", filePathBuffer_, sizeof(filePathBuffer_),
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (app_.loadFile(filePathBuffer_)) {
                midiPlayer_.syncTrackPrograms(app_.getProject());
                ImGui::CloseCurrentPopup();
            } else {
                openErrorMessage_ = "Could not open this file. Check the path and use a type 0/1 MIDI file with PPQ timing.";
            }
        }

        if (!openErrorMessage_.empty()) ImGui::TextWrapped("%s", openErrorMessage_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (app_.loadFile(filePathBuffer_)) {
                midiPlayer_.syncTrackPrograms(app_.getProject());
                ImGui::CloseCurrentPopup();
            } else {
                openErrorMessage_ = "Could not open this file. Check the path and use a type 0/1 MIDI file with PPQ timing.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Save file dialog
    if (showSaveFileDialog_) {
        ImGui::OpenPopup("Save MIDI File");
        showSaveFileDialog_ = false;
        saveErrorMessage_.clear();
    }

    if (ImGui::BeginPopupModal("Save MIDI File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (browseDirectory_.empty()) browseDirectory_ = defaultBrowseDirectory();
        ImGui::Text("Browse: %s", browseDirectory_.c_str());
        if (ImGui::Button("Up")) browseDirectory_ = parentDirectory(browseDirectory_);
        if (ImGui::BeginChild("##save_browser", ImVec2(400, 160), true)) {
            for (const auto& entry : listDirectory(browseDirectory_)) {
                std::string full_path = (std::filesystem::path(browseDirectory_) / entry.name).string();
                if (entry.is_directory) {
                    if (ImGui::Selectable(("[dir] " + entry.name).c_str())) {
                        browseDirectory_ = full_path;
                    }
                } else if (ImGui::Selectable(entry.name.c_str())) {
                    std::strncpy(filePathBuffer_, full_path.c_str(), sizeof(filePathBuffer_) - 1);
                }
            }
        }
        ImGui::EndChild();
        ImGui::Text("Enter file path:");
        ImGui::SetNextItemWidth(400);

        bool tryToSave = false;
        if (ImGui::InputText("##filepath", filePathBuffer_, sizeof(filePathBuffer_),
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            tryToSave = true;
        }

        // Show error message if any
        if (!saveErrorMessage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s", saveErrorMessage_.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            tryToSave = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        if (tryToSave) {
            std::string path = filePathBuffer_;

            // Check for empty path
            if (path.empty()) {
                saveErrorMessage_ = "Please enter a file path.";
            } else {
                // Add .mid extension if not present
                if (path.find(".mid") == std::string::npos && path.find(".MID") == std::string::npos) {
                    path += ".mid";
                }

                if (app_.saveFileAs(path)) {
                    saveErrorMessage_.clear();
                    ImGui::CloseCurrentPopup();
                } else {
                    saveErrorMessage_ = "Failed to save file. Check that the path is valid and you have write permission.";
                }
            }
        }

        ImGui::EndPopup();
    }
}

void MainWindow::requestClose() {
    fileSafety_.request([this] {
        midiPlayer_.panic();
        app_.discardRecovery();
        closeApproved_ = true;
    });
}
