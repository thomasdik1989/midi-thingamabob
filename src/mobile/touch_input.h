#pragma once

#include <SDL.h>
#include <vector>
#include <cstdint>

// High-level touch gesture types recognized from raw SDL touch events
enum class GestureType {
    None,
    Tap,         // Quick single-finger tap
    LongPress,   // Finger held down > threshold
    Drag,        // Single-finger move
    Pinch,       // Two-finger pinch/zoom
    Swipe,       // Fast horizontal or vertical flick
};

enum class SwipeDirection {
    None,
    Left,
    Right,
    Up,
    Down
};

struct TouchGesture {
    GestureType type = GestureType::None;

    // Position (in screen pixels)
    float x = 0.0f;
    float y = 0.0f;

    // Delta from last frame (for Drag)
    float deltaX = 0.0f;
    float deltaY = 0.0f;

    // Pinch data
    float pinchScale = 1.0f;   // > 1 = spreading apart, < 1 = pinching
    float pinchCenterX = 0.0f;
    float pinchCenterY = 0.0f;

    // Swipe data
    SwipeDirection swipeDir = SwipeDirection::None;
    float swipeVelocity = 0.0f;

    // Whether this gesture just started, is ongoing, or just ended
    bool began = false;
    bool ended = false;
    bool active = false;

    // Number of fingers involved
    int fingerCount = 0;
};

class TouchInput {
public:
    TouchInput();
    void processEvent(const SDL_Event& event, float displayWidth, float displayHeight);
    void update(float deltaTime);
    const std::vector<TouchGesture>& getGestures() const { return gestures_; }
    void clearGestures() { gestures_.clear(); }

private:
    struct Finger {
        SDL_FingerID id;
        float startX, startY;      // Where the finger first touched (pixels)
        float currentX, currentY;  // Current position (pixels)
        float lastX, lastY;        // Previous frame position
        float startTime;           // Time of touch down (seconds)
        bool moved;                // Has the finger moved beyond tap threshold?
    };

    std::vector<Finger> fingers_;
    std::vector<TouchGesture> gestures_;

    float lastPinchDist_ = 0.0f;
    bool pinchActive_ = false;

    float currentTime_ = 0.0f;

    static constexpr float TAP_MAX_DURATION = 0.3f;       // seconds
    static constexpr float TAP_MAX_DISTANCE = 15.0f;      // pixels
    static constexpr float LONG_PRESS_DURATION = 0.5f;    // seconds
    static constexpr float SWIPE_MIN_VELOCITY = 500.0f;   // pixels/second
    static constexpr float SWIPE_MIN_DISTANCE = 50.0f;    // pixels
    static constexpr float DRAG_THRESHOLD = 8.0f;         // pixels before drag starts

    Finger* findFinger(SDL_FingerID id);
    void removeFinger(SDL_FingerID id);
    float distance(float x1, float y1, float x2, float y2) const;

    // Special finger ID used to represent the mouse on desktop
    static constexpr SDL_FingerID MOUSE_FINGER_ID = -100;
    bool mouseDown_ = false;
};
