#include "toolbar.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>

Toolbar::Toolbar(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void Toolbar::render() {
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    
    auto& project = app_.getProject();
    
    // Transport controls
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    
    // Stop button
    if (ImGui::Button("Stop")) {
        app_.stop();
        player_.panic();
    }
    ImGui::SameLine();
    
    // Play/Pause button
    const char* playLabel = app_.isPlaying() ? "Pause" : "Play";
    if (ImGui::Button(playLabel)) {
        app_.togglePlayback();
    }
    ImGui::SameLine();
    
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Time display
    double seconds = project.ticksToSeconds(app_.getPlayheadTick());
    int minutes = static_cast<int>(seconds) / 60;
    int secs = static_cast<int>(seconds) % 60;
    int ms = static_cast<int>((seconds - std::floor(seconds)) * 1000);
    ImGui::Text("%02d:%02d.%03d", minutes, secs, ms);
    ImGui::SameLine();
    
    // Beat display
    double beats = project.ticksToBeats(app_.getPlayheadTick());
    int bar = static_cast<int>(beats) / 4 + 1;
    int beat = static_cast<int>(beats) % 4 + 1;
    ImGui::Text("| Bar %d Beat %d", bar, beat);
    ImGui::SameLine();
    
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Tempo
    ImGui::Text("BPM:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    float tempo = project.tempo_bpm;
    if (ImGui::DragFloat("##tempo", &tempo, 1.0f, 20.0f, 300.0f, "%.0f")) {
        project.tempo_bpm = tempo;
        project.modified = true;
    }
    ImGui::SameLine();
    
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Grid snap
    ImGui::Text("Grid:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    
    static const char* gridNames[] = { "Off", "1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    static const midi::GridSnap gridValues[] = { 
        midi::GridSnap::None, 
        midi::GridSnap::Whole, 
        midi::GridSnap::Half, 
        midi::GridSnap::Quarter, 
        midi::GridSnap::Eighth, 
        midi::GridSnap::Sixteenth, 
        midi::GridSnap::ThirtySecond 
    };
    
    int currentGridIndex = 0;
    midi::GridSnap currentSnap = app_.getGridSnap();
    for (int i = 0; i < 7; ++i) {
        if (gridValues[i] == currentSnap) {
            currentGridIndex = i;
            break;
        }
    }
    
    if (ImGui::Combo("##grid", &currentGridIndex, gridNames, 7)) {
        app_.setGridSnap(gridValues[currentGridIndex]);
    }
    ImGui::SameLine();
    
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Volume control
    ImGui::Text("Volume:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    float volume = player_.getAudioSynth().getMasterVolume();
    if (ImGui::SliderFloat("##volume", &volume, 0.0f, 1.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp)) {
        player_.getAudioSynth().setMasterVolume(volume);
    }
    ImGui::SameLine();
    
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // MIDI device selector (for external devices)
    ImGui::Text("Ext MIDI:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    
    auto devices = player_.getOutputDevices();
    std::vector<const char*> deviceNames;
    deviceNames.push_back("(None)");
    for (const auto& d : devices) {
        deviceNames.push_back(d.c_str());
    }
    
    int deviceIndex = player_.getCurrentDevice() + 1; // +1 because of "(None)" option
    if (ImGui::Combo("##mididevice", &deviceIndex, deviceNames.data(), static_cast<int>(deviceNames.size()))) {
        if (deviceIndex == 0) {
            player_.closeDevice();
        } else {
            if (player_.openDevice(deviceIndex - 1)) {
                // Send program changes for all tracks
                for (const auto& track : project.tracks) {
                    player_.sendProgramChange(track.channel, track.program);
                }
            }
        }
    }
    
    ImGui::PopStyleVar();
    
    ImGui::End();
}
