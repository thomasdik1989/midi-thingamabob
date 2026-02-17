#pragma once

#include "touch_input.h"
#include <imgui.h>
#include <functional>
#include <string>
#include <vector>

// Manages three-screen horizontal swipe navigation with smooth transitions.
// Screens: 0 = Left (Tracks), 1 = Center (Piano Roll), 2 = Right (Settings)
class SwipeNav {
public:
    SwipeNav();
    using ScreenRenderFn = std::function<void(float width, float height)>;
    void setScreen(int index, const std::string& name, ScreenRenderFn renderFn);
    bool processGesture(const TouchGesture& gesture);
    void update(float deltaTime);
    void render(float displayWidth, float displayHeight);
    int getCurrentScreen() const;
    bool isAnimating() const { return animating_; }
    void goToScreen(int index);

private:
    struct Screen {
        std::string name;
        ScreenRenderFn renderFn;
    };

    std::vector<Screen> screens_;

    // Current position as a float (0.0 = left screen, 1.0 = center, 2.0 = right)
    float currentPos_ = 1.0f;    // Start on center screen
    float targetPos_ = 1.0f;
    float velocity_ = 0.0f;

    // Drag tracking for swipe navigation
    bool dragging_ = false;
    float dragStartPos_ = 0.0f;
    float dragStartX_ = 0.0f;
    float dragAccumX_ = 0.0f;

    // Animation
    bool animating_ = false;
    static constexpr float ANIMATION_SPEED = 8.0f;  // Spring-like animation speed
    static constexpr float SNAP_THRESHOLD = 0.01f;

    void renderPageIndicator(float displayWidth, float displayHeight);
    static constexpr float PAGE_INDICATOR_HEIGHT = 30.0f;
    static constexpr float DOT_SIZE = 8.0f;
    static constexpr float DOT_SPACING = 20.0f;

    // Edge zone for drag-to-navigate (pixels from screen edge)
    static constexpr float EDGE_ZONE = 40.0f;
};
