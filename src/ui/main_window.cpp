#include "main_window.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>

MainWindow::MainWindow(App& app)
    : app_(app)
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
    midiPlayer_.update(app_.getProject(), app_.getPlayheadTick(), app_.isPlaying());
    
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
    
    firstFrame_ = false;
}

void MainWindow::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", "Ctrl+N")) {
                app_.newProject();
            }
            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                showOpenDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, !app_.getProject().filepath.empty())) {
                app_.saveFile();
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
                showSaveDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // Will be handled by GLFW
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
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Track")) {
            if (ImGui::MenuItem("Add Track")) {
                app_.addTrack();
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
    if (io.WantTextInput) return;
    
    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;
    
    // File operations
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_N)) {
        app_.newProject();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        showOpenDialog();
    }
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (app_.getProject().filepath.empty()) {
            showSaveDialog();
        } else {
            app_.saveFile();
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
    
    // Transport
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        app_.togglePlayback();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        app_.stop();
    }
}

void MainWindow::showOpenDialog() {
    showOpenFileDialog_ = true;
    std::memset(filePathBuffer_, 0, sizeof(filePathBuffer_));
}

void MainWindow::showSaveDialog() {
    showSaveFileDialog_ = true;
    std::strncpy(filePathBuffer_, app_.getProject().filepath.c_str(), sizeof(filePathBuffer_) - 1);
}

void MainWindow::handleFileDialogs() {
    // Open file dialog
    if (showOpenFileDialog_) {
        ImGui::OpenPopup("Open MIDI File");
        showOpenFileDialog_ = false;
    }
    
    if (ImGui::BeginPopupModal("Open MIDI File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter file path:");
        ImGui::SetNextItemWidth(400);
        if (ImGui::InputText("##filepath", filePathBuffer_, sizeof(filePathBuffer_), 
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (app_.loadFile(filePathBuffer_)) {
                // Send program changes for all tracks
                for (const auto& track : app_.getProject().tracks) {
                    midiPlayer_.sendProgramChange(track.channel, track.program);
                }
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::Separator();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (app_.loadFile(filePathBuffer_)) {
                for (const auto& track : app_.getProject().tracks) {
                    midiPlayer_.sendProgramChange(track.channel, track.program);
                }
                ImGui::CloseCurrentPopup();
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
    }
    
    if (ImGui::BeginPopupModal("Save MIDI File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter file path:");
        ImGui::SetNextItemWidth(400);
        if (ImGui::InputText("##filepath", filePathBuffer_, sizeof(filePathBuffer_),
                            ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string path = filePathBuffer_;
            // Add .mid extension if not present
            if (path.find(".mid") == std::string::npos && path.find(".MID") == std::string::npos) {
                path += ".mid";
            }
            if (app_.saveFileAs(path)) {
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            std::string path = filePathBuffer_;
            if (path.find(".mid") == std::string::npos && path.find(".MID") == std::string::npos) {
                path += ".mid";
            }
            if (app_.saveFileAs(path)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
