#include "nine_slice.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// GL includes (match imgui_impl_opengl3.cpp's approach)
#if defined(IMGUI_IMPL_OPENGL_ES2)
    #if defined(__APPLE__)
        #include <OpenGLES/ES2/gl.h>
    #else
        #include <GLES2/gl2.h>
    #endif
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    #if defined(__APPLE__)
        #include <OpenGLES/ES3/gl.h>
    #else
        #include <GLES3/gl3.h>
    #endif
#elif defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <imgui_impl_opengl3_loader.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

void DrawNineSlice(ImDrawList* dl, const NineSlice& ns,
                   ImVec2 pos, ImVec2 size, ImU32 tint)
{
    if (!ns.valid()) return;

    ImTextureRef tex(ns.texture);

    float uvL = ns.borderL / ns.texW;
    float uvR = 1.0f - ns.borderR / ns.texW;
    float uvT = ns.borderT / ns.texH;
    float uvB = 1.0f - ns.borderB / ns.texH;

    float x0 = pos.x;
    float x1 = pos.x + ns.borderL;
    float x2 = pos.x + size.x - ns.borderR;
    float x3 = pos.x + size.x;
    float y0 = pos.y;
    float y1 = pos.y + ns.borderT;
    float y2 = pos.y + size.y - ns.borderB;
    float y3 = pos.y + size.y;

    // Clamp so borders don't overlap when the element is smaller than borders
    if (x2 < x1) { float mid = (x0 + x3) * 0.5f; x1 = mid; x2 = mid; }
    if (y2 < y1) { float mid = (y0 + y3) * 0.5f; y1 = mid; y2 = mid; }

    dl->AddImage(tex, {x0,y0}, {x1,y1}, {0,0},     {uvL,uvT},   tint); // TL
    dl->AddImage(tex, {x1,y0}, {x2,y1}, {uvL,0},    {uvR,uvT},   tint); // Top
    dl->AddImage(tex, {x2,y0}, {x3,y1}, {uvR,0},    {1,uvT},     tint); // TR
    dl->AddImage(tex, {x0,y1}, {x1,y2}, {0,uvT},    {uvL,uvB},   tint); // Left
    dl->AddImage(tex, {x1,y1}, {x2,y2}, {uvL,uvT},  {uvR,uvB},   tint); // Center
    dl->AddImage(tex, {x2,y1}, {x3,y2}, {uvR,uvT},  {1,uvB},     tint); // Right
    dl->AddImage(tex, {x0,y2}, {x1,y3}, {0,uvB},    {uvL,1},     tint); // BL
    dl->AddImage(tex, {x1,y2}, {x2,y3}, {uvL,uvB},  {uvR,1},     tint); // Bottom
    dl->AddImage(tex, {x2,y2}, {x3,y3}, {uvR,uvB},  {1,1},       tint); // BR
}

// Shared helper: draw the combo header (nine-slice button + text + arrow)
// and open the popup if clicked. Returns true if popup was opened.
static bool drawComboHeader(const char* label, const char* previewValue,
                            const NineSliceTheme* theme,
                            float width, float height, ImVec2* outPos)
{
    if (width <= 0) width = ImGui::CalcItemWidth();
    if (height <= 0) height = ImGui::GetFrameHeight();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size(width, height);
    if (outPos) *outPos = pos;

    ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    const NineSlice& ns = theme->button;
    ImU32 tint = hovered ? IM_COL32(255, 255, 255, 230) : IM_COL32_WHITE;
    DrawNineSlice(dl, ns, pos, size, tint);

    float textPad = 8.0f;
    float arrowPad = 6.0f;
    const char* arrow = "v";
    ImVec2 arrowSz = ImGui::CalcTextSize(arrow);
    float arrowX = pos.x + size.x - arrowSz.x - arrowPad;
    float maxTextW = size.x - textPad - arrowSz.x - arrowPad * 2;

    // Clip the preview text so it doesn't overlap the arrow
    ImVec2 textSz = ImGui::CalcTextSize(previewValue);
    float textY = pos.y + (size.y - textSz.y) * 0.5f;
    ImVec4 clipRect(pos.x + textPad, pos.y, pos.x + textPad + maxTextW, pos.y + size.y);
    dl->AddText(nullptr, 0.0f, ImVec2(pos.x + textPad, textY),
                IM_COL32(240, 240, 245, 255), previewValue, nullptr,
                0.0f, &clipRect);

    dl->AddText(ImVec2(arrowX, textY), IM_COL32(180, 180, 190, 255), arrow);

    if (clicked) {
        ImGui::OpenPopup("##combo_popup");
    }
    return clicked;
}

// Push popup styles for themed combo dropdowns
static void pushComboPopupStyle(const NineSliceTheme* theme) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
    if (theme->hasCard()) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    }
}

static void popComboPopupStyle(const NineSliceTheme* theme) {
    if (theme->hasCard()) {
        ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar(2);
}

// Draw nine-slice background inside the combo popup
static void drawComboPopupBg(const NineSliceTheme* theme) {
    if (theme->hasCard()) {
        ImVec2 popupPos = ImGui::GetWindowPos();
        ImVec2 popupSize = ImGui::GetWindowSize();
        DrawNineSlice(ImGui::GetWindowDrawList(), theme->card, popupPos, popupSize);
    }
}

bool ThemedCombo(const char* label, int* currentItem,
                 const char* const items[], int itemCount,
                 const NineSliceTheme* theme, float width, float height)
{
    if (!theme || !theme->hasButton()) {
        if (width > 0) ImGui::SetNextItemWidth(width);
        return ImGui::Combo(label, currentItem, items, itemCount);
    }

    ImGui::PushID(label);

    if (width <= 0) width = ImGui::CalcItemWidth();
    if (height <= 0) height = ImGui::GetFrameHeight();

    const char* preview = (*currentItem >= 0 && *currentItem < itemCount)
                          ? items[*currentItem] : "";
    ImVec2 headerPos;
    drawComboHeader("##combo_btn", preview, theme, width, height, &headerPos);

    bool changed = false;
    pushComboPopupStyle(theme);

    ImGui::SetNextWindowPos(ImVec2(headerPos.x, headerPos.y + height));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, 300));
    if (ImGui::BeginPopup("##combo_popup")) {
        drawComboPopupBg(theme);
        for (int i = 0; i < itemCount; ++i) {
            bool selected = (i == *currentItem);
            if (ImGui::Selectable(items[i], selected)) {
                *currentItem = i;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndPopup();
    }

    popComboPopupStyle(theme);
    ImGui::PopID();
    return changed;
}

bool ThemedBeginCombo(const char* label, const char* previewValue,
                      const NineSliceTheme* theme, float width, float height)
{
    if (!theme || !theme->hasButton()) {
        if (width > 0) ImGui::SetNextItemWidth(width);
        return ImGui::BeginCombo(label, previewValue);
    }

    ImGui::PushID(label);

    if (width <= 0) width = ImGui::CalcItemWidth();
    if (height <= 0) height = ImGui::GetFrameHeight();

    ImVec2 headerPos;
    drawComboHeader("##combo_btn", previewValue, theme, width, height, &headerPos);

    pushComboPopupStyle(theme);

    ImGui::SetNextWindowPos(ImVec2(headerPos.x, headerPos.y + height));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, 300));
    if (ImGui::BeginPopup("##combo_popup")) {
        drawComboPopupBg(theme);
        return true;
    }

    popComboPopupStyle(theme);
    ImGui::PopID();
    return false;
}

void ThemedEndCombo(const NineSliceTheme* theme)
{
    ImGui::EndPopup();

    if (theme && theme->hasButton()) {
        popComboPopupStyle(theme);
        ImGui::PopID();
    }
}

ImTextureID LoadTextureFromFile(const char* path, int* outW, int* outH)
{
    // Use SDL_RWFromFile so Android APK assets are accessible transparently.
    // On other platforms this behaves like fopen.
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) {
        fprintf(stderr, "nine_slice: SDL_RWFromFile failed for %s: %s\n", path, SDL_GetError());
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return 0;
    }

    Sint64 fileSize = SDL_RWsize(rw);
    if (fileSize <= 0) {
        fprintf(stderr, "nine_slice: empty or unreadable file %s\n", path);
        SDL_RWclose(rw);
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return 0;
    }

    std::vector<unsigned char> fileData(static_cast<size_t>(fileSize));
    if (SDL_RWread(rw, fileData.data(), 1, fileData.size()) != fileData.size()) {
        fprintf(stderr, "nine_slice: failed to read %s\n", path);
        SDL_RWclose(rw);
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return 0;
    }
    SDL_RWclose(rw);

    int w, h, channels;
    unsigned char* data = stbi_load_from_memory(
        fileData.data(), static_cast<int>(fileData.size()), &w, &h, &channels, 4);
    if (!data) {
        fprintf(stderr, "nine_slice: stbi decode failed for %s: %s\n", path, stbi_failure_reason());
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    if (outW) *outW = w;
    if (outH) *outH = h;
    return static_cast<ImTextureID>(tex);
}

static NineSlice loadSlice(const std::string& dir, const char* filename,
                           float borderL, float borderR, float borderT, float borderB)
{
    NineSlice ns;
    std::string path = dir.empty() ? std::string(filename) : (dir + "/" + filename);
    int w, h;
    ns.texture = LoadTextureFromFile(path.c_str(), &w, &h);
    if (ns.texture) {
        ns.texW = static_cast<float>(w);
        ns.texH = static_cast<float>(h);
        ns.borderL = borderL;
        ns.borderR = borderR;
        ns.borderT = borderT;
        ns.borderB = borderB;
    }
    return ns;
}

const NineSlice& NineSliceTheme::getButton(ButtonGroupPos pos, bool active) const
{
    switch (pos) {
        case ButtonGroupPos::Left:
            if (hasButtonGroup())
                return active ? buttonLeftActive : buttonLeft;
            break;
        case ButtonGroupPos::Center:
            if (hasButtonGroup())
                return active ? buttonCenterActive : buttonCenter;
            break;
        case ButtonGroupPos::Right:
            if (hasButtonGroup())
                return active ? buttonRightActive : buttonRight;
            break;
        default:
            break;
    }
    return active ? buttonActive : button;
}

NineSliceTheme LoadNineSliceTheme(const char* assetsDir)
{
    NineSliceTheme theme;
    std::string dir(assetsDir);

    // Standalone buttons (all corners rounded)
    theme.button       = loadSlice(dir, "button.png",        12, 12, 12, 12);
    theme.buttonActive = loadSlice(dir, "button_active.png", 12, 12, 12, 12);

    // Grouped button variants
    // Left: rounded left corners, straight right edge (small border for separator)
    theme.buttonLeft       = loadSlice(dir, "button_left.png",        12, 2, 12, 12);
    theme.buttonLeftActive = loadSlice(dir, "button_left_active.png", 12, 2, 12, 12);
    // Center: straight both edges
    theme.buttonCenter       = loadSlice(dir, "button_center.png",        2, 2, 12, 12);
    theme.buttonCenterActive = loadSlice(dir, "button_center_active.png", 2, 2, 12, 12);
    // Right: straight left edge, rounded right corners
    theme.buttonRight       = loadSlice(dir, "button_right.png",        2, 12, 12, 12);
    theme.buttonRightActive = loadSlice(dir, "button_right_active.png", 2, 12, 12, 12);

    theme.panel = loadSlice(dir, "panel.png", 16, 16, 16, 16);
    theme.card  = loadSlice(dir, "card.png",  12, 12, 12, 12);

    // Fallbacks: active variants fall back to normal if missing
    if (!theme.buttonActive.valid() && theme.button.valid())
        theme.buttonActive = theme.button;
    if (!theme.buttonLeftActive.valid() && theme.buttonLeft.valid())
        theme.buttonLeftActive = theme.buttonLeft;
    if (!theme.buttonCenterActive.valid() && theme.buttonCenter.valid())
        theme.buttonCenterActive = theme.buttonCenter;
    if (!theme.buttonRightActive.valid() && theme.buttonRight.valid())
        theme.buttonRightActive = theme.buttonRight;

    // Fallback: if group variants are missing, fall back to standalone for all positions
    // (getButton already handles this, so no extra work needed)

    if (theme.loaded()) {
        fprintf(stderr, "nine_slice: theme loaded from %s\n", assetsDir);
    }

    return theme;
}

static void freeSlice(NineSlice& ns) {
    if (ns.texture) {
        GLuint tex = static_cast<GLuint>(ns.texture);
        glDeleteTextures(1, &tex);
        ns.texture = 0;
    }
}

void FreeNineSliceTheme(NineSliceTheme& theme) {
    freeSlice(theme.button);
    freeSlice(theme.buttonActive);
    freeSlice(theme.buttonLeft);
    freeSlice(theme.buttonLeftActive);
    freeSlice(theme.buttonCenter);
    freeSlice(theme.buttonCenterActive);
    freeSlice(theme.buttonRight);
    freeSlice(theme.buttonRightActive);
    freeSlice(theme.panel);
    freeSlice(theme.card);
}
