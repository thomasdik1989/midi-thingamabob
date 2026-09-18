#include "settings_screen.h"
#include "nine_slice.h"
#include "file_ops_mobile.h"
#include "../midi/harmony.h"
#include "../midi/patterns.h"
#include "../midi/types.h"
#include <algorithm>
#include <cmath>

SettingsScreen::SettingsScreen(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void SettingsScreen::beginCard(const char* title, float cardWidth) {
    bool useNineSlice = theme_ && theme_->hasCard();

    if (useNineSlice) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CARD_PADDING, CARD_PADDING));

    ImGui::BeginChild(title, ImVec2(cardWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    // Draw nine-slice as the first thing in the child so it's behind all content.
    // Uses the previous frame's auto-sized dimensions (settles after one frame).
    if (useNineSlice) {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 sz = ImGui::GetWindowSize();
        DrawNineSlice(ImGui::GetWindowDrawList(), theme_->card, pos, sz);
    }

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

    beginCard("Edit History", cardWidth);
    ImGui::BeginDisabled(!app_.canUndo());
    if (ImGui::Button("Undo", ImVec2((cardWidth - CARD_PADDING * 2 - 8) / 2, BUTTON_HEIGHT))) app_.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!app_.canRedo());
    if (ImGui::Button("Redo", ImVec2((cardWidth - CARD_PADDING * 2 - 8) / 2, BUTTON_HEIGHT))) app_.redo();
    ImGui::EndDisabled();
    endCard();
    renderTimeSignature(cardWidth);
    renderLoopRegion(cardWidth);
    renderMasterVolume(cardWidth);
    renderQuantize(cardWidth);
    renderHarmony(cardWidth);
    renderSongLength(cardWidth);
    renderPatterns(cardWidth);
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

    float innerWidth = cardWidth - CARD_PADDING * 2;
    float groupWidth = (innerWidth - 20) * 0.5f;
    float btnW = BUTTON_HEIGHT;
    float numW = groupWidth - btnW * 2 - 12;

    // Two groups side by side: Beats | Unit
    ImGui::BeginGroup();

    // "Beats:" label centered above controls
    float beatsLabelW = ImGui::CalcTextSize("Beats:").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (groupWidth - beatsLabelW) * 0.5f);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "Beats:");

    if (ImGui::Button("-##beats", ImVec2(btnW, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        project.beats_per_bar = std::max(1, project.beats_per_bar - 1);
        project.modified = true;
    }
    ImGui::SameLine();
    // Centered number between - and +
    float numTextW = ImGui::CalcTextSize("00").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (numW - numTextW) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (BUTTON_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%d", project.beats_per_bar);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (BUTTON_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (numW - numTextW) * 0.5f);
    if (ImGui::Button("+##beats", ImVec2(btnW, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        project.beats_per_bar = std::min(16, project.beats_per_bar + 1);
        project.modified = true;
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 20);

    ImGui::BeginGroup();

    // "Unit:" label centered above controls
    float unitLabelW = ImGui::CalcTextSize("Unit:").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (groupWidth - unitLabelW) * 0.5f);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "Unit:");

    static const int beatUnits[] = {2, 4, 8, 16};
    int currentIdx = 1;
    for (int i = 0; i < 4; ++i) {
        if (beatUnits[i] == project.beat_unit) { currentIdx = i; break; }
    }

    if (ImGui::Button("-##unit", ImVec2(btnW, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        currentIdx = std::max(0, currentIdx - 1);
        project.beat_unit = beatUnits[currentIdx];
        project.modified = true;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (numW - numTextW) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (BUTTON_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::Text("%d", project.beat_unit);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (BUTTON_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (numW - numTextW) * 0.5f);
    if (ImGui::Button("+##unit", ImVec2(btnW, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        currentIdx = std::min(3, currentIdx + 1);
        project.beat_unit = beatUnits[currentIdx];
        project.modified = true;
    }
    ImGui::EndGroup();

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
        auto transaction = app_.edit();
        project.loop_enabled = loopEnabled;
    }

    ImGui::Spacing();

    // Always show Start/End controls, dimmed when disabled
    if (!project.loop_enabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
        ImGui::BeginDisabled();
    }

    int ticksPerBar = project.ticksPerBar();
    if (ticksPerBar <= 0) ticksPerBar = project.ticks_per_quarter * 4;

    // Start bar
    int startBar = static_cast<int>(project.loop_start / ticksPerBar) + 1;
    ImGui::Text("Start:");
    ImGui::SameLine();
    if (ImGui::Button("-##loopstart", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        startBar = std::max(1, startBar - 1);
        project.loop_start = static_cast<uint32_t>((startBar - 1) * ticksPerBar);
    }
    ImGui::SameLine();
    ImGui::Text("Bar %d", startBar);
    ImGui::SameLine();
    if (ImGui::Button("+##loopstart", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        startBar++;
        project.loop_start = static_cast<uint32_t>((startBar - 1) * ticksPerBar);
    }

    // End bar
    int endBar = static_cast<int>(project.loop_end / ticksPerBar) + 1;
    ImGui::Text("End:  ");
    ImGui::SameLine();
    if (ImGui::Button("-##loopend", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        endBar = std::max(startBar + 1, endBar - 1);
        project.loop_end = static_cast<uint32_t>((endBar - 1) * ticksPerBar);
    }
    ImGui::SameLine();
    ImGui::Text("Bar %d", endBar);
    ImGui::SameLine();
    if (ImGui::Button("+##loopend", ImVec2(BUTTON_HEIGHT, BUTTON_HEIGHT))) {
        auto transaction = app_.edit();
        endBar++;
        project.loop_end = static_cast<uint32_t>((endBar - 1) * ticksPerBar);
    }

    if (!project.loop_enabled) {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
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

void SettingsScreen::renderHarmony(float cardWidth) {
    beginCard("Harmony Stamp", cardWidth);

    float item_width = cardWidth - CARD_PADDING * 2;
    int stamp_index = static_cast<int>(app_.getHarmonyKind());
    if (stamp_index < 0 || stamp_index >= midi::harmonyKindCount()) stamp_index = 0;

    static const char* stamp_names[] = {
        "Single", "Octave", "5th Below", "3rd Below", "6th Below", "Triad Below"
    };
    if (ThemedCombo("##stamp_mobile", &stamp_index, stamp_names,
                    midi::harmonyKindCount(), theme_, item_width)) {
        app_.setHarmonyKind(static_cast<midi::HarmonyKind>(stamp_index));
    }

    ImGui::Spacing();

    static const char* key_names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int tonic = app_.getHarmonyTonic();
    if (ThemedCombo("##key_mobile", &tonic, key_names, 12, theme_, item_width)) {
        app_.setHarmonyTonic(tonic);
    }

    ImGui::Spacing();

    bool minor = app_.getHarmonyMinor();
    if (ImGui::Checkbox("Minor key", &minor)) {
        app_.setHarmonyMinor(minor);
    }

    ImGui::Spacing();
    if (ImGui::Button("Harmonize Selection", ImVec2(item_width, BUTTON_HEIGHT))) {
        app_.harmonizeSelectedNotes();
    }

    endCard();
}

void SettingsScreen::renderSongLength(float cardWidth) {
    beginCard("Song Length", cardWidth);

    float item_width = cardWidth - CARD_PADDING * 2;
    int bars = app_.getLengthBars();
    ImGui::Text("Bars:");
    ImGui::SetNextItemWidth(item_width);
    if (ImGui::InputInt("##song_length", &bars)) {
        app_.setLengthBars(bars);
    }

    ImGui::Spacing();
    float preset_width = (item_width - 12) / 4.0f;
    if (ImGui::Button("16", ImVec2(preset_width, BUTTON_HEIGHT))) app_.setLengthBars(16);
    ImGui::SameLine();
    if (ImGui::Button("32", ImVec2(preset_width, BUTTON_HEIGHT))) app_.setLengthBars(32);
    ImGui::SameLine();
    if (ImGui::Button("64", ImVec2(preset_width, BUTTON_HEIGHT))) app_.setLengthBars(64);
    ImGui::SameLine();
    if (ImGui::Button("128", ImVec2(preset_width, BUTTON_HEIGHT))) app_.setLengthBars(128);

    endCard();
}

void SettingsScreen::renderPatterns(float cardWidth) {
    beginCard("Insert Beat", cardWidth);

    float item_width = cardWidth - CARD_PADDING * 2;
    static int selected_groove = 0;
    static int selected_bars = 4;

    std::vector<const char*> groove_names;
    for (int i = 0; i < midi::drumGrooveCount(); ++i) {
        groove_names.push_back(midi::getDrumGroove(i).name);
    }
    if (ThemedCombo("##groove_mobile", &selected_groove, groove_names.data(),
                     static_cast<int>(groove_names.size()), theme_, item_width)) {
    }

    ImGui::Spacing();

    static const char* bar_labels[] = {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars", "64 bars", "To song end"};
    static const int bar_values[] = {1, 2, 4, 8, 16, 32, 64, 0};
    if (ThemedCombo("##bars_mobile", &selected_bars, bar_labels, 8, theme_, item_width)) {
    }
    selected_bars = std::clamp(selected_bars, 0, 7);

    ImGui::Spacing();
    if (ImGui::Button("Insert Beat", ImVec2(item_width, BUTTON_HEIGHT))) {
        app_.insertDrumGroove(selected_groove, bar_values[selected_bars]);
    }

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
    if (ThemedCombo("##midi_device", &deviceIndex, deviceNames.data(),
                     static_cast<int>(deviceNames.size()), theme_,
                     cardWidth - CARD_PADDING * 2)) {
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
