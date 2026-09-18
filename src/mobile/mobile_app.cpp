#include "mobile_app.h"
#include "file_ops_mobile.h"
#include <imgui.h>
#include <SDL.h>

MobileApp::MobileApp()
    : toolbar_(app_, midiPlayer_)
    , pianoRoll_(app_, midiPlayer_)
    , trackPanel_(app_, midiPlayer_)
    , settings_(app_, midiPlayer_)
    , theme_{}
    , lastFrame_(std::chrono::steady_clock::now())
{
    // Setup swipe navigation screens
    swipeNav_.setScreen(0, "Tracks", [this](float w, float h) {
        trackPanel_.render(w, h);
    });
    swipeNav_.setScreen(1, "Piano Roll", [this](float w, float h) {
        toolbar_.render(w);
        pianoRoll_.render(w, h - toolbar_.getHeight());
    });
    swipeNav_.setScreen(2, "Settings", [this](float w, float h) {
        settings_.render(w, h);
    });
}

MobileApp::~MobileApp() {
    FreeNineSliceTheme(theme_);
}

void MobileApp::loadTheme() {
    if (themeLoaded_) return;
    themeLoaded_ = true;

    auto tryLoad = [&](const std::string& dir) -> bool {
        fprintf(stderr, "nine_slice: trying path: %s\n", dir.c_str());
        NineSliceTheme t = LoadNineSliceTheme(dir.c_str());
        if (t.loaded()) {
            theme_ = t;
            return true;
        }
        FreeNineSliceTheme(t);
        return false;
    };

    // Build a list of candidate paths (platform-dependent)
    std::vector<std::string> candidates;

#if defined(__ANDROID__)
    // On Android, SDL_RWFromFile uses AAssetManager. Files from Gradle's
    // assets.srcDirs end up at the APK's asset root, so use an empty prefix.
    candidates.push_back("");
#endif

    const char* base = SDL_GetBasePath();
    if (base) {
        candidates.push_back(std::string(base) + "assets");
        candidates.push_back(std::string(base) + "Resources/assets");
        candidates.push_back(std::string(base) + "assets");
    }
    candidates.push_back("assets");
    candidates.push_back("../assets");

    for (const auto& path : candidates) {
        if (tryLoad(path)) break;
    }

    if (!theme_.loaded()) {
        fprintf(stderr, "nine_slice: no theme found, using default rendering\n");
        return;
    }

    toolbar_.setTheme(&theme_);
    pianoRoll_.setTheme(&theme_);
    trackPanel_.setTheme(&theme_);
    settings_.setTheme(&theme_);
    swipeNav_.setTheme(&theme_);
}

void MobileApp::processEvent(const SDL_Event& event) {
    touchInput_.processEvent(event, displayWidth_, displayHeight_);
}

void MobileApp::update(float deltaTime) {
    auto now = std::chrono::steady_clock::now();
    double frameDelta = std::chrono::duration<double>(now - lastFrame_).count();
    lastFrame_ = now;

    if (app_.isPlaying()) {
        app_.advancePlayhead(frameDelta);
    }
    midiPlayer_.update(app_.getProject(), app_.getPlayheadTick(), app_.isPlaying(), app_.playbackSpans());

    // Update touch input (detects long-press, etc.)
    touchInput_.update(deltaTime);

    // Sync scroll mode from toolbar to piano roll
    pianoRoll_.setScrollMode(toolbar_.isScrollMode());

    // Process gestures: swipe nav gets first priority.
    // When an ImGui popup is open (combo dropdown, modal dialog, etc.)
    // skip forwarding gestures to screen components so that taps on
    // popup items don't propagate through to create notes / interact
    // with the piano roll behind the popup.
    // Yes this was silly I click a checkbox and it creates a note.
    // Note: we no longer check IsAnyItemActive() here because the
    // Y-bounds check in processGesture() already prevents toolbar
    // interactions from reaching the piano roll.
    bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    const auto& gestures = touchInput_.getGestures();
    for (const auto& gesture : gestures) {
        bool consumed = swipeNav_.processGesture(gesture);

        if (!consumed && !swipeNav_.isAnimating() && !popupOpen) {
            // Forward to active screen's component
            int activeScreen = swipeNav_.getCurrentScreen();
            switch (activeScreen) {
                case 0:
                    trackPanel_.processGesture(gesture);
                    break;
                case 1:
                    pianoRoll_.processGesture(gesture);
                    break;
                case 2:
                    // Settings uses ImGui widgets directly, no custom gesture handling needed
                    break;
            }
        }
    }

    swipeNav_.update(deltaTime);

    // Clear gestures for next frame
    touchInput_.clearGestures();
}

void MobileApp::render(float displayWidth, float displayHeight) {
    displayWidth_ = displayWidth;
    displayHeight_ = displayHeight;

    // Deferred theme load: GL context must be current
    if (!themeLoaded_) {
        loadTheme();
    }

    toolbar_.setFileSafety(&fileSafety_);
    swipeNav_.render(displayWidth, displayHeight);

    // Render file dialogs (modal popups on top)
    FileOpsMobile::renderDialogs();
    renderFileSafetyMobile(fileSafety_, theme_.loaded() ? &theme_ : nullptr);
}

void MobileApp::requestClose() {
    fileSafety_.request([this] {
        midiPlayer_.panic();
        app_.discardRecovery();
        closeApproved_ = true;
    });
}
