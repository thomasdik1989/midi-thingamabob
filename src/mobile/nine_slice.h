#pragma once

#include <imgui.h>

struct NineSlice {
    ImTextureID texture = 0;
    float texW = 0;
    float texH = 0;
    float borderL = 0;
    float borderR = 0;
    float borderT = 0;
    float borderB = 0;

    bool valid() const { return texture != 0 && texW > 0 && texH > 0; }
};

// Position of a button within a group
enum class ButtonGroupPos { Solo, Left, Center, Right };

struct NineSliceTheme {
    // Standalone buttons (all corners rounded)
    NineSlice button;
    NineSlice buttonActive;

    // Grouped buttons: left end (rounded left, square right)
    NineSlice buttonLeft;
    NineSlice buttonLeftActive;

    // Grouped buttons: center segment (square both sides)
    NineSlice buttonCenter;
    NineSlice buttonCenterActive;

    // Grouped buttons: right end (square left, rounded right)
    NineSlice buttonRight;
    NineSlice buttonRightActive;

    NineSlice panel;
    NineSlice card;

    bool hasButton() const { return button.valid(); }
    bool hasButtonGroup() const { return buttonLeft.valid() && buttonCenter.valid() && buttonRight.valid(); }
    bool hasPanel() const { return panel.valid(); }
    bool hasCard() const { return card.valid(); }
    bool loaded() const { return hasButton() || hasPanel() || hasCard(); }

    // Pick the right nine-slice for a button given its group position and state
    const NineSlice& getButton(ButtonGroupPos pos, bool active) const;
};

void DrawNineSlice(ImDrawList* dl, const NineSlice& ns,
                   ImVec2 pos, ImVec2 size,
                   ImU32 tint = IM_COL32_WHITE);

// Themed combo (selectbox). Returns true when a new item is selected.
// Updates *currentItem to the new index. Falls back to ImGui::Combo when
// theme is null or has no button asset.
bool ThemedCombo(const char* label, int* currentItem,
                 const char* const items[], int itemCount,
                 const NineSliceTheme* theme, float width = 0,
                 float height = 0);

// Themed BeginCombo/EndCombo pair for custom selectable content.
// Use like ImGui::BeginCombo/EndCombo but with nine-slice styling.
bool ThemedBeginCombo(const char* label, const char* previewValue,
                      const NineSliceTheme* theme, float width = 0,
                      float height = 0);
void ThemedEndCombo(const NineSliceTheme* theme);

ImTextureID LoadTextureFromFile(const char* path, int* outW, int* outH);

NineSliceTheme LoadNineSliceTheme(const char* assetsDir);
void FreeNineSliceTheme(NineSliceTheme& theme);
