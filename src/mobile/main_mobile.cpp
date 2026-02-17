#include "mobile_app.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include <SDL.h>

#if defined(__IPHONEOS__) || defined(__ANDROID__)
#include <SDL_opengles2.h>
#else
// Desktop preview: use regular OpenGL
#include <SDL_opengl.h>
#endif

#include <cstdio>
#include <algorithm>

// Compute the DPI scale factor for the current platform.
// Returns 1.0 if no scaling is needed (iOS handles it via SDL, desktop uses
// the framebuffer scale automatically). On Android, SDL reports the window
// in physical pixels, so we need to convert to logical points ourselves.
// Still not in the best shape but it works for now.
static float computeDpiScale(SDL_Window* window) {
    (void)window;
#if defined(__ANDROID__)
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0 && hdpi > 0.0f) {
        // Android base density is 160 dpi.  Clamp to a reasonable range
        // to avoid extreme values on unusual hardware.
        float scale = hdpi / 160.0f;
        return std::clamp(scale, 1.0f, 4.0f);
    }
    // Fallback: guess from window vs a typical phone logical width (~400pt).
    // This isn not ideal.
    int w = 0;
    SDL_GetWindowSize(window, &w, nullptr);
    if (w > 800) {
        return static_cast<float>(w) / 400.0f;
    }
    // Not sure if this needs to be 2 or 1.5.
    return 2.0f;
#else
    return 1.0f;
#endif
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    // OpenGL ES 2.0 for mobile, OpenGL 3.3 for desktop preview.
    // As ES 3 gave us the boot on mobile. :D
#if defined(__IPHONEOS__) || defined(__ANDROID__)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    // macOS requires at least OpenGL 3.2 Core Profile (GLSL 150)
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Window size: mobile portrait or desktop preview window
#if defined(__IPHONEOS__) || defined(__ANDROID__)
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI;
    int windowWidth = 0;
    int windowHeight = 0;
#else
    // Desktop preview: simulate mobile portrait (iPhone-like aspect ratio)
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int windowWidth = 430;
    int windowHeight = 932;
#endif

    SDL_Window* window = SDL_CreateWindow(
        "MIDI Editor Mobile",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight,
        windowFlags
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        fprintf(stderr, "SDL_GL_CreateContext error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1); // VSync

    // -------------------------------------------------------------------------
    // DPI scale factor
    // -------------------------------------------------------------------------
    // On iOS, SDL reports the window in logical points and the drawable in
    // physical pixels — ImGui picks this up automatically via
    // DisplayFramebufferScale.
    //
    // On Android, SDL reports both window and drawable in physical pixels, so
    // we compute a scale factor from the display DPI and apply it ourselves:
    //   • Override io.DisplaySize  → logical points (physical / dpiScale)
    //   • Override io.DisplayFramebufferScale → dpiScale
    //   • Scale mouse/touch events so coordinates match logical points
    //   • Rasterize fonts at physical pixel size, then set FontGlobalScale to
    //     1/dpiScale so they display at the correct logical size (crisp text).
    //
    // Yes I copy pasted this from the ImGui docs: have a look at https://wiki.libsdl.org/SDL3/README-ios :D
    // -------------------------------------------------------------------------
    const float dpiScale = computeDpiScale(window);
    const bool  needsDpiOverride = (dpiScale > 1.01f)
#if defined(__IPHONEOS__)
        && false   // iOS does its own scaling — never override
#endif
        ;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Enable touch/mouse input mapping
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;

    // Setup ImGui style for mobile (dark theme, larger elements)
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(10, 8);
    style.TouchExtraPadding = ImVec2(8, 8);
    style.ScrollbarSize = 20.0f;
    style.GrabMinSize = 20.0f;

    // Font and DPI scaling
    float baseFontSize = 18.0f;
#if defined(__IPHONEOS__) || defined(__ANDROID__)
    baseFontSize = 20.0f;  // slightly larger for touch comfort.
#endif

    // Rasterize fonts at physical pixel size so they are crisp on high-DPI
    // screens. FontGlobalScale is then set to 1/dpiScale so that ImGui
    // positions them at the correct logical size.
    float rasterFontSize = baseFontSize * dpiScale;

    io.Fonts->AddFontDefault();  // Fonts[0]: small fallback
    ImFontConfig fontConfig;
    fontConfig.SizePixels = rasterFontSize;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    ImFont* scaledFont = io.Fonts->AddFontDefault(&fontConfig);  // Fonts[1]: DPI-scaled

    // Make the scaled font the default so ALL UI text uses it.
    io.FontDefault = scaledFont;

    if (needsDpiOverride) {
        io.FontGlobalScale = 1.0f / dpiScale;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Initialize mobile application
    MobileApp mobileApp;

    // Load file from command line if provided (desktop preview)
    // You can try this when using the build.sh.
    if (argc > 1) {
        mobileApp.getApp().loadFile(argv[1]);
    }


    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // On Android we need to scale mouse/button coordinates from physical
            // pixels to logical points BEFORE ImGui processes them, so that
            // ImGui's hit-testing matches our logical coordinate system.
            // This was a brain melt so I keep the comment.
            if (needsDpiOverride) {
                switch (event.type) {
                    case SDL_MOUSEMOTION:
                        event.motion.x = static_cast<Sint32>(event.motion.x / dpiScale);
                        event.motion.y = static_cast<Sint32>(event.motion.y / dpiScale);
                        break;
                    case SDL_MOUSEBUTTONDOWN:
                    case SDL_MOUSEBUTTONUP:
                        event.button.x = static_cast<Sint32>(event.button.x / dpiScale);
                        event.button.y = static_cast<Sint32>(event.button.y / dpiScale);
                        break;
                    default:
                        break;
                }
            }

            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running = false;
            }

            // Always forward touch events to the gesture system so it can
            // track finger state. Popup-aware filtering happens later in
            // MobileApp::update() before gestures reach the piano roll.
            mobileApp.processEvent(event);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        // Override display size to logical points on Android.
        // ImGui_ImplSDL2_NewFrame() just set DisplaySize to the physical
        // window size — we correct it here before ImGui::NewFrame() uses it.
        if (needsDpiOverride) {
            io.DisplaySize.x /= dpiScale;
            io.DisplaySize.y /= dpiScale;
            io.DisplayFramebufferScale = ImVec2(dpiScale, dpiScale);
        }

        ImGui::NewFrame();

        // Logical window size for layout (matches io.DisplaySize)
        float logicalW = io.DisplaySize.x;
        float logicalH = io.DisplaySize.y;

        mobileApp.update(io.DeltaTime);
        mobileApp.render(logicalW, logicalH);

        ImGui::Render();

        int drawableW, drawableH;
        SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);
        glViewport(0, 0, drawableW, drawableH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
