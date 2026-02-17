#include "swipe_nav.h"
#include <cmath>
#include <algorithm>

SwipeNav::SwipeNav() {
    screens_.resize(3);
}

void SwipeNav::setScreen(int index, const std::string& name, ScreenRenderFn renderFn) {
    if (index >= 0 && index < 3) {
        screens_[index].name = name;
        screens_[index].renderFn = renderFn;
    }
}

bool SwipeNav::processGesture(const TouchGesture& gesture) {
    switch (gesture.type) {
        case GestureType::Drag: {
            if (gesture.fingerCount != 1) return false;

            if (!dragging_ && !animating_) {
                // Only start a navigation drag if it's clearly horizontal
                // AND the touch started near the screen edges (within EDGE_ZONE pixels).
                // This prevents stealing drags from the piano roll.
                float screenWidth = ImGui::GetIO().DisplaySize.x;
                float touchStartX = gesture.x - gesture.deltaX;
                bool nearEdge = (touchStartX < EDGE_ZONE) || (touchStartX > screenWidth - EDGE_ZONE);

                if (nearEdge && std::abs(gesture.deltaX) > std::abs(gesture.deltaY) * 1.5f) {
                    dragging_ = true;
                    dragStartPos_ = currentPos_;
                    dragStartX_ = touchStartX;
                    dragAccumX_ = 0.0f;
                }
            }

            if (dragging_) {
                dragAccumX_ += gesture.deltaX;
                // Convert pixel drag to screen position change
                float screenWidth = ImGui::GetIO().DisplaySize.x;
                if (screenWidth > 0) {
                    float posChange = -dragAccumX_ / screenWidth;
                    currentPos_ = std::clamp(dragStartPos_ + posChange, 0.0f, 2.0f);
                }

                // Snap to nearest screen when the drag ends.
                if (gesture.ended) {
                    dragging_ = false;
                    targetPos_ = std::round(currentPos_);
                    targetPos_ = std::clamp(targetPos_, 0.0f, 2.0f);
                    animating_ = true;
                }
                return true;
            }
            return false;
        }

        case GestureType::Swipe: {
            if (animating_ || dragging_) return false;
            if (gesture.fingerCount != 1) return false;

            // Swipe gestures (fast flicks) always work for navigation
            int current = getCurrentScreen();
            if (gesture.swipeDir == SwipeDirection::Left && current < 2) {
                goToScreen(current + 1);
                return true;
            }
            if (gesture.swipeDir == SwipeDirection::Right && current > 0) {
                goToScreen(current - 1);
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

void SwipeNav::update(float deltaTime) {
    if (animating_) {
        float diff = targetPos_ - currentPos_;
        if (std::abs(diff) < SNAP_THRESHOLD) {
            currentPos_ = targetPos_;
            animating_ = false;
        } else {
            currentPos_ += diff * ANIMATION_SPEED * deltaTime;
        }
    }
}

void SwipeNav::render(float displayWidth, float displayHeight) {
    float contentHeight = displayHeight - PAGE_INDICATOR_HEIGHT;

    // Determine which screens to render (active + adjacent)
    int activeScreen = getCurrentScreen();

    for (int i = std::max(0, activeScreen - 1); i <= std::min(2, activeScreen + 1); ++i) {
        if (!screens_[i].renderFn) continue;

        float screenOffset = (static_cast<float>(i) - currentPos_) * displayWidth;

        // Skip if completely off-screen
        if (screenOffset > displayWidth || screenOffset < -displayWidth) continue;

        // Position the screen
        ImGui::SetNextWindowPos(ImVec2(screenOffset, PAGE_INDICATOR_HEIGHT));
        ImGui::SetNextWindowSize(ImVec2(displayWidth, contentHeight));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                 ImGuiWindowFlags_NoSavedSettings;

        char windowId[64];
        snprintf(windowId, sizeof(windowId), "##screen_%d", i);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f));

        if (ImGui::Begin(windowId, nullptr, flags)) {
            screens_[i].renderFn(displayWidth, contentHeight);
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    // Render page indicator on top
    renderPageIndicator(displayWidth, displayHeight);
}

int SwipeNav::getCurrentScreen() const {
    return std::clamp(static_cast<int>(std::round(currentPos_)), 0, 2);
}

void SwipeNav::goToScreen(int index) {
    targetPos_ = static_cast<float>(std::clamp(index, 0, 2));
    animating_ = true;
}

void SwipeNav::renderPageIndicator(float displayWidth, float displayHeight) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(displayWidth, PAGE_INDICATOR_HEIGHT));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));

    if (ImGui::Begin("##page_indicator", nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        float centerX = displayWidth * 0.5f;
        float centerY = PAGE_INDICATOR_HEIGHT * 0.5f;
        float totalWidth = DOT_SPACING * 2; // 3 dots, 2 gaps

        for (int i = 0; i < 3; ++i) {
            float dotX = centerX - totalWidth * 0.5f + i * DOT_SPACING;
            float dotY = centerY;

            // Smoothly interpolate dot brightness based on proximity to current position
            float proximity = 1.0f - std::min(1.0f, std::abs(currentPos_ - static_cast<float>(i)));
            int alpha = static_cast<int>(80 + proximity * 175);
            ImU32 color = IM_COL32(255, 255, 255, alpha);

            float radius = DOT_SIZE * 0.5f * (0.8f + proximity * 0.2f);
            drawList->AddCircleFilled(ImVec2(dotX, dotY), radius, color);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
