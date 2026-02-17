#include "settings_screen.h"
#include "file_ops_mobile.h"
#include "../midi/types.h"
#include <algorithm>
#include <cmath>

SettingsScreen::SettingsScreen(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void SettingsScreen::beginCard(const char* title, float cardWidth) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CARD_PADDING, CARD_PADDING));

    ImGui::BeginChild(title, ImVec2(cardWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    // Section title
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "%s", title);
    ImGui::Spacing();
}

void SettingsScreen::endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void SettingsScreen::render(float width, float height) {
    float cardWidth = width - CARD_MARGIN * 2;

    // Title
    ImGui::SetCursorPos(ImVec2(CARD_MARGIN, CARD_MARGIN));
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
    ImGui::Text("SETTINGS");
    ImGui::PopFont();

    ImGui::Spacing();
    ImGui::Spacing();

    // Scrollable content
    ImGui::BeginChild("##settings_scroll", ImVec2(width, height - ImGui::GetCursorPosY()), false);
    ImGui::SetCursorPosX(CARD_MARGIN);

    renderTimeSignature(cardWidth);
    renderLoopRegion(cardWidth);
    renderMasterVolume(cardWidth);
    renderQuantize(cardWidth);
    renderMidiOutput(cardWidth);
    renderExport(cardWidth);

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::EndChild();
}

void SettingsScreen::renderTimeSignature(float cardWidth) {
    beginCard("Time Signature", cardWidth);

    auto& project = app_.getProject();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 12));

    // Beats per bar
    ImGui::Text("Beats:");
    ImGui::SameLine();

    if (ImGui::Button("-##beats", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        project.beats_per_bar = std::max(1, project.beats_per_bar - 1);
        project.modified = true;
    }
    ImGui::SameLine();
    ImGui::Text("%d", project.beats_per_bar);
    ImGui::SameLine();
    if (ImGui::Button("+##beats", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        project.beats_per_bar = std::min(16, project.beats_per_bar + 1);
        project.modified = true;
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();

    // Beat unit
    ImGui::Text("Unit:");
    ImGui::SameLine();

    static const int beatUnits[] = {2, 4, 8, 16};
    int currentIdx = 1;
    for (int i = 0; i < 4; ++i) {
        if (beatUnits[i] == project.beat_unit) { currentIdx = i; break; }
    }

    if (ImGui::Button("-##unit", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        currentIdx = std::max(0, currentIdx - 1);
        project.beat_unit = beatUnits[currentIdx];
        project.modified = true;
    }
    ImGui::SameLine();
    ImGui::Text("%d", project.beat_unit);
    ImGui::SameLine();
    if (ImGui::Button("+##unit", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        currentIdx = std::min(3, currentIdx + 1);
        project.beat_unit = beatUnits[currentIdx];
        project.modified = true;
    }

    ImGui::PopStyleVar();

    endCard();
}

void SettingsScreen::renderLoopRegion(float cardWidth) {
    beginCard("Loop Region", cardWidth);

    auto& project = app_.getProject();

    // Loop enabled toggle
    ImGui::Text("Loop Enabled");
    ImGui::SameLine(cardWidth - CARD_PADDING * 2 - 50);
    bool loopEnabled = project.loop_enabled;
    if (ImGui::Checkbox("##loop_enabled", &loopEnabled)) {
        project.loop_enabled = loopEnabled;
    }

    if (project.loop_enabled) {
        ImGui::Spacing();

        int ticksPerBar = project.ticksPerBar();
        if (ticksPerBar <= 0) ticksPerBar = project.ticks_per_quarter * 4;

        // Start bar
        int startBar = static_cast<int>(project.loop_start / ticksPerBar) + 1;
        ImGui::Text("Start:");
        ImGui::SameLine();
        if (ImGui::Button("-##loopstart", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
            startBar = std::max(1, startBar - 1);
            project.loop_start = static_cast<uint32_t>((startBar - 1) * ticksPerBar);
        }
        ImGui::SameLine();
        ImGui::Text("Bar %d", startBar);
        ImGui::SameLine();
        if (ImGui::Button("+##loopstart", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
            startBar++;
            project.loop_start = static_cast<uint32_t>((startBar - 1) * ticksPerBar);
        }

        // End bar
        int endBar = static_cast<int>(project.loop_end / ticksPerBar) + 1;
        ImGui::Text("End:  ");
        ImGui::SameLine();
        if (ImGui::Button("-##loopend", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
            endBar = std::max(startBar + 1, endBar - 1);
            project.loop_end = static_cast<uint32_t>((endBar - 1) * ticksPerBar);
        }
        ImGui::SameLine();
        ImGui::Text("Bar %d", endBar);
        ImGui::SameLine();
        if (ImGui::Button("+##loopend", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
            endBar++;
            project.loop_end = static_cast<uint32_t>((endBar - 1) * ticksPerBar);
        }
    }

    endCard();
}

void SettingsScreen::renderMasterVolume(float cardWidth) {
    beginCard("Master Volume", cardWidth);

    float volume = player_.getAudioSynth().getMasterVolume();
    int volumePct = static_cast<int>(volume * 100);

    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 30.0f);
    ImGui::SetNextItemWidth(cardWidth - CARD_PADDING * 2 - 60);
    if (ImGui::SliderFloat("##master_vol", &volume, 0.0f, 1.0f, "")) {
        player_.getAudioSynth().setMasterVolume(volume);
    }
    ImGui::SameLine();
    ImGui::Text("%d%%", static_cast<int>(volume * 100));
    ImGui::PopStyleVar();

    endCard();
}

void SettingsScreen::renderQuantize(float cardWidth) {
    beginCard("Quantize", cardWidth);

    // Grid snap selector as pill buttons
    static const char* gridNames[] = {"1/4", "1/8", "1/16", "1/32"};
    static const midi::GridSnap gridValues[] = {
        midi::GridSnap::Quarter,
        midi::GridSnap::Eighth,
        midi::GridSnap::Sixteenth,
        midi::GridSnap::ThirtySecond
    };

    midi::GridSnap currentSnap = app_.getGridSnap();

    float pillWidth = (cardWidth - CARD_PADDING * 2 - 12) / 4;
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();

        bool isActive = (gridValues[i] == currentSnap);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
        }

        char label[16];
        snprintf(label, sizeof(label), "%s##q", gridNames[i]);
        if (ImGui::Button(label, ImVec2(pillWidth, BUTTON_HEIGHT))) {
            app_.setGridSnap(gridValues[i]);
        }

        if (isActive) {
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();

    // Quantize action button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
    if (ImGui::Button("Quantize Selection", ImVec2(cardWidth - CARD_PADDING * 2, BUTTON_HEIGHT))) {
        app_.quantizeSelectedNotes();
    }
    ImGui::PopStyleColor();

    endCard();
}

void SettingsScreen::renderMidiOutput(float cardWidth) {
    beginCard("MIDI Output", cardWidth);

    auto devices = player_.getOutputDevices();
    std::vector<const char*> deviceNames;
    deviceNames.push_back("Built-in Synth");
    for (const auto& d : devices) {
        deviceNames.push_back(d.c_str());
    }

    int deviceIndex = player_.getCurrentDevice() + 1;
    ImGui::SetNextItemWidth(cardWidth - CARD_PADDING * 2);
    if (ImGui::Combo("##midi_device", &deviceIndex, deviceNames.data(),
                     static_cast<int>(deviceNames.size()))) {
        if (deviceIndex == 0) {
            player_.closeDevice();
        } else {
            if (player_.openDevice(deviceIndex - 1)) {
                for (const auto& track : app_.getProject().tracks) {
                    player_.sendProgramChange(track.channel, track.program);
                }
            }
        }
    }

    endCard();
}

void SettingsScreen::renderExport(float cardWidth) {
    beginCard("Export", cardWidth);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
    if (ImGui::Button("Export MIDI File", ImVec2(cardWidth - CARD_PADDING * 2, BUTTON_HEIGHT))) {
        FileOpsMobile::saveFile(app_, "export.mid");
    }
    ImGui::PopStyleColor();

    endCard();
}
