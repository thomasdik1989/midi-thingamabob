#include "track_panel_mobile.h"
#include "../midi/general_midi.h"
#include <algorithm>
#include <cmath>

TrackPanelMobile::TrackPanelMobile(App& app, midi::MidiPlayer& player)
    : app_(app)
    , player_(player)
{
}

void TrackPanelMobile::processGesture(const TouchGesture& gesture) {
    if (gesture.type == GestureType::Drag && gesture.fingerCount == 1) {
        // Check if this is a horizontal swipe on a track card (for swipe-to-delete)
        if (cardBounds_.empty()) return;

        float touchY = gesture.y;

        // Find which card the touch is on
        int cardIndex = -1;
        for (int i = 0; i < static_cast<int>(cardBounds_.size()); ++i) {
            if (touchY >= cardBounds_[i].y && touchY < cardBounds_[i].y + cardBounds_[i].height) {
                cardIndex = i;
                break;
            }
        }

        if (cardIndex >= 0 && std::abs(gesture.deltaX) > std::abs(gesture.deltaY) * 1.5f) {
            // Horizontal drag on a card - swipe to delete
            auto& project = app_.getProject();
            if (project.tracks.size() <= 1) return; // Don't allow if only one track

            swipingTrackIndex_ = cardIndex;
            swipeOffset_ += gesture.deltaX;
            swipeOffset_ = std::clamp(swipeOffset_, -DELETE_BUTTON_WIDTH - 10.0f, 0.0f);

            if (gesture.ended) {
                if (swipeOffset_ < SWIPE_THRESHOLD) {
                    // Snap to reveal delete button
                    swipeOffset_ = -DELETE_BUTTON_WIDTH;
                    swipeDeleteRevealed_ = true;
                } else {
                    // Snap back
                    swipeOffset_ = 0.0f;
                    swipeDeleteRevealed_ = false;
                    swipingTrackIndex_ = -1;
                }
            }
        }
    } else if (gesture.type == GestureType::Tap) {
        // If delete is revealed and tap is on delete button, delete the track
        // This is not working ATM; it worked and then I borked it.
        if (swipeDeleteRevealed_ && swipingTrackIndex_ >= 0) {
            // Check if tap is in the delete button area (right side of card)
            if (swipingTrackIndex_ < static_cast<int>(cardBounds_.size())) {
                auto& bounds = cardBounds_[swipingTrackIndex_];
                if (gesture.y >= bounds.y && gesture.y < bounds.y + bounds.height) {
                    // Check if tap is on the right (delete button area)
                    float cardRight = ImGui::GetIO().DisplaySize.x - CARD_MARGIN;
                    if (gesture.x >= cardRight - DELETE_BUTTON_WIDTH) {
                        app_.removeTrack(swipingTrackIndex_);
                        swipingTrackIndex_ = -1;
                        swipeOffset_ = 0.0f;
                        swipeDeleteRevealed_ = false;
                        return;
                    }
                }
            }
            // Tap elsewhere: cancel swipe
            swipingTrackIndex_ = -1;
            swipeOffset_ = 0.0f;
            swipeDeleteRevealed_ = false;
        }
    }
}

void TrackPanelMobile::render(float width, float height) {
    auto& project = app_.getProject();

    // If the editing track was deleted, close the editor
    if (editingTrackIndex_ >= static_cast<int>(project.tracks.size())) {
        editingTrackIndex_ = -1;
    }

    if (editingTrackIndex_ >= 0) {
        renderTrackEditor(width, height);
    } else {
        renderTrackList(width, height);
    }
}

void TrackPanelMobile::renderTrackList(float width, float height) {
    auto& project = app_.getProject();

    float contentPadding = CARD_MARGIN;

    // Title
    ImGui::SetCursorPos(ImVec2(contentPadding, contentPadding));
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : nullptr);
    ImGui::Text("TRACKS");
    ImGui::PopFont();

    ImGui::Spacing();
    ImGui::Spacing();

    // Track cards
    float cardWidth = width - contentPadding * 2;
    cardBounds_.clear();

    ImGui::BeginChild("##track_scroll", ImVec2(cardWidth + contentPadding * 2,
                       height - ImGui::GetCursorPosY() - 70), false);

    for (int i = 0; i < static_cast<int>(project.tracks.size()); ++i) {
        ImGui::PushID(i);

        // Record card bounds for gesture hit testing
        ImVec2 cardScreenPos = ImGui::GetCursorScreenPos();
        cardBounds_.push_back({cardScreenPos.y, CARD_HEIGHT});

        // Apply swipe offset if this card is being swiped
        float offsetX = (i == swipingTrackIndex_) ? swipeOffset_ : 0.0f;

        // Draw delete button behind the card (revealed when swiped)
        if (i == swipingTrackIndex_ && offsetX < -5.0f) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float deleteX = cardScreenPos.x + cardWidth + offsetX;
            drawList->AddRectFilled(
                ImVec2(deleteX, cardScreenPos.y),
                ImVec2(cardScreenPos.x + cardWidth, cardScreenPos.y + CARD_HEIGHT),
                IM_COL32(200, 50, 50, 255),
                8.0f, ImDrawFlags_RoundCornersRight
            );

            // "Delete" text centered in the red area
            const char* delText = "Delete";
            ImVec2 textSize = ImGui::CalcTextSize(delText);
            float textX = deleteX + (-offsetX - textSize.x) * 0.5f;
            float textY = cardScreenPos.y + (CARD_HEIGHT - textSize.y) * 0.5f;
            drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), delText);
        }

        // Render the card with offset
        if (offsetX != 0.0f) {
            ImGui::SetCursorScreenPos(ImVec2(cardScreenPos.x + offsetX, cardScreenPos.y));
        }

        renderTrackCard(i, project.tracks[i], cardWidth + offsetX);

        // Move cursor past the card
        ImGui::SetCursorScreenPos(ImVec2(cardScreenPos.x, cardScreenPos.y + CARD_HEIGHT + CARD_MARGIN));
        ImGui::Dummy(ImVec2(0, 0));  // Required to grow parent bounds after SetCursorScreenPos

        ImGui::PopID();
    }

    ImGui::EndChild();

    // Add Track button at the bottom
    ImGui::SetCursorPosX(contentPadding);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
    if (ImGui::Button("+ Add Track", ImVec2(cardWidth, 50))) {
        app_.addTrack();
    }
    ImGui::PopStyleColor(2);
}

void TrackPanelMobile::renderTrackEditor(float width, float height) {
    auto& project = app_.getProject();
    auto& track = project.tracks[editingTrackIndex_];

    float padding = CARD_MARGIN;
    float cardWidth = width - padding * 2;
    float itemWidth = cardWidth - padding * 2;

    // Title bar with back button
    ImGui::SetCursorPos(ImVec2(padding, padding));
    if (ImGui::Button("< Back", ImVec2(80, 36))) {
        // Copy name back from edit buffer
        track.name = editNameBuf_;
        project.modified = true;
        editingTrackIndex_ = -1;
        return;
    }
    ImGui::SameLine();
    ImGui::Text("Edit Track %d", editingTrackIndex_ + 1);

    ImGui::Spacing();
    ImGui::Spacing();

    // Scrollable content
    ImGui::BeginChild("##track_edit_scroll", ImVec2(width, height - ImGui::GetCursorPosY() - padding), false);
    ImGui::SetCursorPosX(padding);

    // --- Track Name ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CARD_PADDING, CARD_PADDING));
    ImGui::BeginChild("##name_card", ImVec2(cardWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "Track Name");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::InputText("##track_name", editNameBuf_, sizeof(editNameBuf_))) {
        track.name = editNameBuf_;
        project.modified = true;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::SetCursorPosX(padding);

    // --- MIDI Channel ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CARD_PADDING, CARD_PADDING));
    ImGui::BeginChild("##channel_card", ImVec2(cardWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "MIDI Channel");
    ImGui::Spacing();
    int channel = track.channel + 1;  // Display as 1-16
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::InputInt("##channel", &channel)) {
        channel = std::clamp(channel, 1, 16);
        track.channel = channel - 1;
        project.modified = true;
        // Re-send program change on the new channel
        player_.sendProgramChange(track.channel, track.program);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::SetCursorPosX(padding);

    // --- Instrument ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CARD_PADDING, CARD_PADDING));
    ImGui::BeginChild("##instrument_card", ImVec2(cardWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "Instrument");
    ImGui::Spacing();

    // Category combo
    int category = midi::getCategoryForProgram(track.program);
    ImGui::Text("Category:");
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::BeginCombo("##category", std::string(midi::getCategoryName(category)).c_str())) {
        for (int c = 0; c < 16; ++c) {
            bool selected = (c == category);
            if (ImGui::Selectable(std::string(midi::getCategoryName(c)).c_str(), selected)) {
                track.program = c * 8;  // First instrument in category
                project.modified = true;
                player_.sendProgramChange(track.channel, track.program);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    // Instrument within category
    ImGui::Text("Sound:");
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::BeginCombo("##instrument", std::string(midi::getInstrumentName(track.program)).c_str())) {
        int baseProgram = (track.program / 8) * 8;
        for (int i = 0; i < 8; ++i) {
            int prog = baseProgram + i;
            bool selected = (prog == track.program);
            if (ImGui::Selectable(std::string(midi::getInstrumentName(prog)).c_str(), selected)) {
                track.program = prog;
                project.modified = true;
                player_.sendProgramChange(track.channel, track.program);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::SetCursorPosX(padding);

    // --- Volume & Pan ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CARD_PADDING, CARD_PADDING));
    ImGui::BeginChild("##volume_pan_card", ImVec2(cardWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "Volume & Pan");
    ImGui::Spacing();

    // Volume
    ImGui::Text("Volume:");
    ImGui::SetNextItemWidth(itemWidth);
    float vol = track.volume;
    if (ImGui::SliderFloat("##edit_vol", &vol, 0.0f, 1.0f, "%.0f%%")) {
        track.volume = vol;
        player_.getAudioSynth().setChannelVolume(track.channel, vol);
        project.modified = true;
    }

    ImGui::Spacing();

    // Pan
    ImGui::Text("Pan:");
    ImGui::SetNextItemWidth(itemWidth);
    float pan = track.pan;
    const char* panLabel = (pan < 0.45f) ? "L" : (pan > 0.55f) ? "R" : "C";
    char panFmt[16];
    snprintf(panFmt, sizeof(panFmt), "%s %.0f%%", panLabel,
             std::abs(pan - 0.5f) * 200.0f);
    if (ImGui::SliderFloat("##edit_pan", &pan, 0.0f, 1.0f, panFmt)) {
        track.pan = pan;
        project.modified = true;
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::SetCursorPosX(padding);

    // --- Mute / Solo ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CARD_PADDING, CARD_PADDING));
    ImGui::BeginChild("##mute_solo_card", ImVec2(cardWidth, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "Mute / Solo");
    ImGui::Spacing();

    float halfWidth = (itemWidth - 8) * 0.5f;
    bool muted = track.muted;
    if (muted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button(muted ? "Unmute" : "Mute", ImVec2(halfWidth, 44))) {
        track.muted = !track.muted;
    }
    if (muted) ImGui::PopStyleColor();

    ImGui::SameLine();

    bool solo = track.solo;
    if (solo) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    if (ImGui::Button(solo ? "Unsolo" : "Solo", ImVec2(halfWidth, 44))) {
        track.solo = !track.solo;
    }
    if (solo) ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::EndChild();  // End scroll area
}

void TrackPanelMobile::renderTrackCard(int index, midi::Track& track, float cardWidth) {
    bool isSelected = (index == app_.getSelectedTrackIndex());
    auto& project = app_.getProject();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cardPos = ImGui::GetCursorScreenPos();
    ImVec2 cardEnd(cardPos.x + cardWidth, cardPos.y + CARD_HEIGHT);

    // Card background
    ImU32 bgColor = IM_COL32(42, 42, 42, 255);
    drawList->AddRectFilled(cardPos, cardEnd, bgColor, 8.0f);

    // Selection border
    if (isSelected) {
        drawList->AddRect(cardPos, cardEnd, IM_COL32(70, 130, 200, 255), 8.0f, 0, 2.5f);
    }

    // Track color indicator
    static const ImU32 trackColors[] = {
        IM_COL32(70, 130, 200, 255),   // Blue
        IM_COL32(70, 180, 70, 255),    // Green
        IM_COL32(220, 160, 50, 255),   // Orange
        IM_COL32(180, 70, 180, 255),   // Purple
        IM_COL32(70, 180, 180, 255),   // Cyan
        IM_COL32(220, 70, 70, 255),    // Red
        IM_COL32(180, 180, 70, 255),   // Yellow
        IM_COL32(140, 100, 70, 255),   // Brown
    };
    ImU32 indicatorColor = trackColors[index % 8];
    drawList->AddCircleFilled(
        ImVec2(cardPos.x + CARD_PADDING + 8, cardPos.y + CARD_PADDING + 10),
        8.0f, indicatorColor
    );

    // Track name
    float textStartX = cardPos.x + CARD_PADDING + 24;
    drawList->AddText(
        ImVec2(textStartX, cardPos.y + CARD_PADDING),
        IM_COL32(240, 240, 240, 255), track.name.c_str()
    );

    // Instrument name (smaller, dimmer)
    std::string instrName(midi::getInstrumentName(track.program));
    drawList->AddText(
        ImVec2(textStartX, cardPos.y + CARD_PADDING + 22),
        IM_COL32(150, 150, 160, 255), instrName.c_str()
    );

    // Mute/Solo buttons (right side)
    float btnSize = 36.0f;
    float btnY = cardPos.y + CARD_PADDING;
    float btnX = cardPos.x + cardWidth - CARD_PADDING - btnSize * 2 - 8;

    // Create invisible ImGui buttons for M and S
    ImGui::SetCursorScreenPos(ImVec2(btnX, btnY));

    bool muted = track.muted;
    if (muted) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
    }
    if (ImGui::Button("M", ImVec2(btnSize, btnSize))) {
        track.muted = !track.muted;
        // Select this track on interaction
        app_.setSelectedTrack(index);
    }
    if (muted) {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    bool solo = track.solo;
    if (solo) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    }
    if (ImGui::Button("S", ImVec2(btnSize, btnSize))) {
        track.solo = !track.solo;
        app_.setSelectedTrack(index);
    }
    if (solo) {
        ImGui::PopStyleColor();
    }

    // Volume slider (bottom of card)
    float sliderY = cardPos.y + CARD_HEIGHT - CARD_PADDING - 20;
    float sliderWidth = cardWidth - CARD_PADDING * 2 - 28;
    ImGui::SetCursorScreenPos(ImVec2(textStartX, sliderY));
    ImGui::SetNextItemWidth(sliderWidth);
    float vol = track.volume;
    char volLabel[16];
    snprintf(volLabel, sizeof(volLabel), "##vol_%d", index);
    if (ImGui::SliderFloat(volLabel, &vol, 0.0f, 1.0f, "%.0f%%")) {
        track.volume = vol;
        player_.getAudioSynth().setChannelVolume(track.channel, vol);
        project.modified = true;
    }

    // Note count
    char noteInfo[32];
    snprintf(noteInfo, sizeof(noteInfo), "%zu notes", track.notes.size());
    drawList->AddText(
        ImVec2(cardPos.x + CARD_PADDING, sliderY + 2),
        IM_COL32(100, 100, 110, 255), noteInfo
    );

    // Make the whole card clickable for selection and opening the editor
    ImGui::SetCursorScreenPos(cardPos);
    char btnId[32];
    snprintf(btnId, sizeof(btnId), "##card_%d", index);
    if (ImGui::InvisibleButton(btnId, ImVec2(cardWidth, CARD_HEIGHT))) {
        app_.setSelectedTrack(index);
        // Open the track editor
        editingTrackIndex_ = index;
        // Copy current name into edit buffer
        snprintf(editNameBuf_, sizeof(editNameBuf_), "%s", track.name.c_str());
    }
}
