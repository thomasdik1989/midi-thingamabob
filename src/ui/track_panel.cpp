#include "track_panel.h"
#include "../midi/general_midi.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>

TrackPanel::TrackPanel(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void TrackPanel::render() {
    ImGui::Begin("Tracks");
    
    auto& project = app_.getProject();
    
    // Add track button
    if (ImGui::Button("+ Add Track")) {
        app_.addTrack();
    }
    
    ImGui::Separator();
    
    // Track list
    for (int i = 0; i < static_cast<int>(project.tracks.size()); ++i) {
        ImGui::PushID(i);
        renderTrackItem(i, project.tracks[i]);
        ImGui::PopID();
    }
    
    ImGui::End();
}

void TrackPanel::renderTrackItem(int index, midi::Track& track) {
    auto& project = app_.getProject();
    bool isSelected = (index == app_.getSelectedTrackIndex());
    
    // Highlight selected track
    if (isSelected) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.35f, 0.55f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.6f, 0.8f, 1.0f));
    }
    
    bool open = ImGui::CollapsingHeader(track.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    
    // Select track on click
    if (ImGui::IsItemClicked()) {
        app_.setSelectedTrack(index);
    }
    
    if (isSelected) {
        ImGui::PopStyleColor(3);
    }
    
    if (open) {
        ImGui::Indent();
        
        // Track name
        char nameBuffer[64];
        std::strncpy(nameBuffer, track.name.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer))) {
            track.name = nameBuffer;
            project.modified = true;
        }
        
        // Channel selector
        ImGui::Text("Channel:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        int channel = track.channel + 1; // Display as 1-16
        if (ImGui::InputInt("##channel", &channel)) {
            channel = std::clamp(channel, 1, 16);
            track.channel = channel - 1;
            project.modified = true;
        }
        
        // Instrument selector
        ImGui::Text("Instrument:");
        ImGui::SetNextItemWidth(-1);
        
        // Category combo
        int category = midi::getCategoryForProgram(track.program);
        if (ImGui::BeginCombo("##category", std::string(midi::getCategoryName(category)).c_str())) {
            for (int c = 0; c < 16; ++c) {
                bool selected = (c == category);
                if (ImGui::Selectable(std::string(midi::getCategoryName(c)).c_str(), selected)) {
                    // Select first instrument in category
                    int oldProgram = track.program;
                    track.program = c * 8;
                    project.modified = true;
                    
                    // Send program change via command
                    auto cmd = std::make_unique<ChangeInstrumentCommand>(app_, index, oldProgram, track.program);
                    // Note: we already changed it above, so just record for undo
                    player_.sendProgramChange(track.channel, track.program);
                }
            }
            ImGui::EndCombo();
        }
        
        // Instrument in category
        int instrumentInCategory = track.program % 8;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##instrument", std::string(midi::getInstrumentName(track.program)).c_str())) {
            int baseProgram = (track.program / 8) * 8;
            for (int i = 0; i < 8; ++i) {
                int prog = baseProgram + i;
                bool selected = (prog == track.program);
                if (ImGui::Selectable(std::string(midi::getInstrumentName(prog)).c_str(), selected)) {
                    int oldProgram = track.program;
                    track.program = prog;
                    project.modified = true;
                    player_.sendProgramChange(track.channel, track.program);
                }
            }
            ImGui::EndCombo();
        }
        
        // Volume slider
        ImGui::Text("Vol:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        float vol = track.volume;
        if (ImGui::SliderFloat("##volume", &vol, 0.0f, 1.0f, "%.2f")) {
            track.volume = vol;
            player_.getAudioSynth().setChannelVolume(track.channel, vol);
            project.modified = true;
        }
        
        // Pan slider
        ImGui::Text("Pan:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        float pan = track.pan;
        if (ImGui::SliderFloat("##pan", &pan, 0.0f, 1.0f, pan < 0.48f ? "L %.0f" : (pan > 0.52f ? "R %.0f" : "C"), ImGuiSliderFlags_AlwaysClamp)) {
            track.pan = pan;
            player_.getAudioSynth().setChannelPan(track.channel, pan);
            project.modified = true;
        }
        
        // Mute/Solo buttons
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
        
        bool muted = track.muted;
        if (muted) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        }
        if (ImGui::Button("M##mute", ImVec2(24, 0))) {
            track.muted = !track.muted;
        }
        if (muted) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Mute");
        }
        
        ImGui::SameLine();
        
        bool solo = track.solo;
        if (solo) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        }
        if (ImGui::Button("S##solo", ImVec2(24, 0))) {
            track.solo = !track.solo;
        }
        if (solo) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Solo");
        }
        
        ImGui::SameLine();
        
        // Delete button (only if more than one track)
        if (project.tracks.size() > 1) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("X##delete", ImVec2(24, 0))) {
                app_.removeTrack(index);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Delete Track");
            }
        }
        
        ImGui::PopStyleVar();
        
        // Note count info
        ImGui::TextDisabled("%zu notes", track.notes.size());
        
        ImGui::Unindent();
        ImGui::Separator();
    }
}
