#include "touch_input.h"
#include <cmath>
#include <algorithm>

TouchInput::TouchInput() {}

TouchInput::Finger* TouchInput::findFinger(SDL_FingerID id) {
    for (auto& f : fingers_) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

void TouchInput::removeFinger(SDL_FingerID id) {
    fingers_.erase(
        std::remove_if(fingers_.begin(), fingers_.end(),
                       [id](const Finger& f) { return f.id == id; }),
        fingers_.end()
    );
}

float TouchInput::distance(float x1, float y1, float x2, float y2) const {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

void TouchInput::processEvent(const SDL_Event& event, float displayWidth, float displayHeight) {
    switch (event.type) {
        case SDL_FINGERDOWN: {
            Finger finger;
            finger.id = event.tfinger.fingerId;
            // SDL touch coordinates are normalized 0-1; convert to pixels
            finger.startX = event.tfinger.x * displayWidth;
            finger.startY = event.tfinger.y * displayHeight;
            finger.currentX = finger.startX;
            finger.currentY = finger.startY;
            finger.lastX = finger.startX;
            finger.lastY = finger.startY;
            finger.startTime = currentTime_;
            finger.moved = false;
            fingers_.push_back(finger);

            // If we now have 2 fingers, start pinch tracking
            if (fingers_.size() == 2) {
                lastPinchDist_ = distance(
                    fingers_[0].currentX, fingers_[0].currentY,
                    fingers_[1].currentX, fingers_[1].currentY
                );
                pinchActive_ = true;

                TouchGesture g;
                g.type = GestureType::Pinch;
                g.began = true;
                g.active = true;
                g.fingerCount = 2;
                g.pinchScale = 1.0f;
                g.pinchCenterX = (fingers_[0].currentX + fingers_[1].currentX) * 0.5f;
                g.pinchCenterY = (fingers_[0].currentY + fingers_[1].currentY) * 0.5f;
                gestures_.push_back(g);
            }
            break;
        }

        case SDL_FINGERMOTION: {
            Finger* finger = findFinger(event.tfinger.fingerId);
            if (finger) {
                finger->lastX = finger->currentX;
                finger->lastY = finger->currentY;
                finger->currentX = event.tfinger.x * displayWidth;
                finger->currentY = event.tfinger.y * displayHeight;

                float dist = distance(finger->startX, finger->startY,
                                      finger->currentX, finger->currentY);
                if (dist > DRAG_THRESHOLD) {
                    finger->moved = true;
                }

                if (fingers_.size() == 2 && pinchActive_) {
                    // Two-finger: emit pinch gesture
                    float newDist = distance(
                        fingers_[0].currentX, fingers_[0].currentY,
                        fingers_[1].currentX, fingers_[1].currentY
                    );

                    TouchGesture g;
                    g.type = GestureType::Pinch;
                    g.active = true;
                    g.fingerCount = 2;
                    g.pinchScale = (lastPinchDist_ > 0.01f) ? newDist / lastPinchDist_ : 1.0f;
                    g.pinchCenterX = (fingers_[0].currentX + fingers_[1].currentX) * 0.5f;
                    g.pinchCenterY = (fingers_[0].currentY + fingers_[1].currentY) * 0.5f;
                    // Also report two-finger drag
                    g.deltaX = (event.tfinger.dx * displayWidth);
                    g.deltaY = (event.tfinger.dy * displayHeight);
                    lastPinchDist_ = newDist;
                    gestures_.push_back(g);

                } else if (fingers_.size() == 1 && finger->moved) {
                    // Single-finger drag
                    TouchGesture g;
                    g.type = GestureType::Drag;
                    g.x = finger->currentX;
                    g.y = finger->currentY;
                    g.deltaX = finger->currentX - finger->lastX;
                    g.deltaY = finger->currentY - finger->lastY;
                    g.active = true;
                    g.fingerCount = 1;
                    gestures_.push_back(g);
                }
            }
            break;
        }

        case SDL_FINGERUP: {
            Finger* finger = findFinger(event.tfinger.fingerId);
            if (finger) {
                float duration = currentTime_ - finger->startTime;
                float dist = distance(finger->startX, finger->startY,
                                      finger->currentX, finger->currentY);

                if (pinchActive_ && fingers_.size() <= 2) {
                    // End pinch
                    TouchGesture g;
                    g.type = GestureType::Pinch;
                    g.ended = true;
                    g.fingerCount = static_cast<int>(fingers_.size());
                    gestures_.push_back(g);
                    pinchActive_ = false;
                    lastPinchDist_ = 0.0f;
                }

                if (fingers_.size() == 1) {
                    // Single finger released
                    if (!finger->moved && duration < TAP_MAX_DURATION && dist < TAP_MAX_DISTANCE) {
                        // Tap
                        TouchGesture g;
                        g.type = GestureType::Tap;
                        g.x = finger->currentX;
                        g.y = finger->currentY;
                        g.began = true;
                        g.ended = true;
                        g.fingerCount = 1;
                        gestures_.push_back(g);
                    } else if (finger->moved && dist >= SWIPE_MIN_DISTANCE) {
                        // Check for swipe
                        float velocity = dist / std::max(duration, 0.001f);
                        if (velocity >= SWIPE_MIN_VELOCITY) {
                            float dx = finger->currentX - finger->startX;
                            float dy = finger->currentY - finger->startY;

                            TouchGesture g;
                            g.type = GestureType::Swipe;
                            g.x = finger->currentX;
                            g.y = finger->currentY;
                            g.swipeVelocity = velocity;
                            g.began = true;
                            g.ended = true;
                            g.fingerCount = 1;

                            if (std::abs(dx) > std::abs(dy)) {
                                g.swipeDir = dx > 0 ? SwipeDirection::Right : SwipeDirection::Left;
                            } else {
                                g.swipeDir = dy > 0 ? SwipeDirection::Down : SwipeDirection::Up;
                            }
                            gestures_.push_back(g);
                        }
                    }

                    // Emit drag end if was dragging
                    if (finger->moved) {
                        TouchGesture g;
                        g.type = GestureType::Drag;
                        g.x = finger->currentX;
                        g.y = finger->currentY;
                        g.ended = true;
                        g.fingerCount = 1;
                        gestures_.push_back(g);
                    }
                }

                removeFinger(event.tfinger.fingerId);
            }
            break;
        }

        // Desktop preview: handle mouse events as single-finger touch.
        // This allows the desktop build to be tested with a mouse.
        case SDL_MOUSEBUTTONDOWN: {
            if (event.button.button == SDL_BUTTON_LEFT && !mouseDown_) {
                mouseDown_ = true;
                float mx = static_cast<float>(event.button.x);
                float my = static_cast<float>(event.button.y);

                Finger finger;
                finger.id = MOUSE_FINGER_ID;
                finger.startX = mx;
                finger.startY = my;
                finger.currentX = mx;
                finger.currentY = my;
                finger.lastX = mx;
                finger.lastY = my;
                finger.startTime = currentTime_;
                finger.moved = false;
                fingers_.push_back(finger);
            }
            break;
        }

        case SDL_MOUSEMOTION: {
            if (mouseDown_) {
                Finger* finger = findFinger(MOUSE_FINGER_ID);
                if (finger) {
                    finger->lastX = finger->currentX;
                    finger->lastY = finger->currentY;
                    finger->currentX = static_cast<float>(event.motion.x);
                    finger->currentY = static_cast<float>(event.motion.y);

                    float dist = distance(finger->startX, finger->startY,
                                          finger->currentX, finger->currentY);
                    if (dist > DRAG_THRESHOLD) {
                        finger->moved = true;
                    }

                    if (finger->moved && fingers_.size() == 1) {
                        TouchGesture g;
                        g.type = GestureType::Drag;
                        g.x = finger->currentX;
                        g.y = finger->currentY;
                        g.deltaX = finger->currentX - finger->lastX;
                        g.deltaY = finger->currentY - finger->lastY;
                        g.active = true;
                        g.fingerCount = 1;
                        gestures_.push_back(g);
                    }
                }
            }
            break;
        }

        case SDL_MOUSEBUTTONUP: {
            if (event.button.button == SDL_BUTTON_LEFT && mouseDown_) {
                mouseDown_ = false;
                Finger* finger = findFinger(MOUSE_FINGER_ID);
                if (finger) {
                    float duration = currentTime_ - finger->startTime;
                    float dist = distance(finger->startX, finger->startY,
                                          finger->currentX, finger->currentY);

                    if (!finger->moved && duration < TAP_MAX_DURATION && dist < TAP_MAX_DISTANCE) {
                        TouchGesture g;
                        g.type = GestureType::Tap;
                        g.x = finger->currentX;
                        g.y = finger->currentY;
                        g.began = true;
                        g.ended = true;
                        g.fingerCount = 1;
                        gestures_.push_back(g);
                    } else if (finger->moved && dist >= SWIPE_MIN_DISTANCE) {
                        float velocity = dist / std::max(duration, 0.001f);
                        if (velocity >= SWIPE_MIN_VELOCITY) {
                            float dx = finger->currentX - finger->startX;
                            float dy = finger->currentY - finger->startY;

                            TouchGesture g;
                            g.type = GestureType::Swipe;
                            g.x = finger->currentX;
                            g.y = finger->currentY;
                            g.swipeVelocity = velocity;
                            g.began = true;
                            g.ended = true;
                            g.fingerCount = 1;

                            if (std::abs(dx) > std::abs(dy)) {
                                g.swipeDir = dx > 0 ? SwipeDirection::Right : SwipeDirection::Left;
                            } else {
                                g.swipeDir = dy > 0 ? SwipeDirection::Down : SwipeDirection::Up;
                            }
                            gestures_.push_back(g);
                        }
                    }

                    if (finger->moved) {
                        TouchGesture g;
                        g.type = GestureType::Drag;
                        g.x = finger->currentX;
                        g.y = finger->currentY;
                        g.ended = true;
                        g.fingerCount = 1;
                        gestures_.push_back(g);
                    }

                    removeFinger(MOUSE_FINGER_ID);
                }
            }
            break;
        }

        default:
            break;
    }
}

void TouchInput::update(float deltaTime) {
    currentTime_ += deltaTime;

    // Check for long press on stationary fingers
    for (auto& finger : fingers_) {
        if (!finger.moved && fingers_.size() == 1) {
            float duration = currentTime_ - finger.startTime;
            float dist = distance(finger.startX, finger.startY,
                                  finger.currentX, finger.currentY);

            if (duration >= LONG_PRESS_DURATION && dist < TAP_MAX_DISTANCE) {
                TouchGesture g;
                g.type = GestureType::LongPress;
                g.x = finger.currentX;
                g.y = finger.currentY;
                g.began = true;
                g.ended = true;
                g.fingerCount = 1;
                gestures_.push_back(g);

                // Mark as moved to avoid re-triggering
                finger.moved = true;
            }
        }
    }
}
