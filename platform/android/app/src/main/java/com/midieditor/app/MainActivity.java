package com.midieditor.app;

import org.libsdl.app.SDLActivity;

/**
 * Main activity for the MIDI Editor mobile app.
 * Extends SDLActivity which handles the SDL2 lifecycle, OpenGL ES context,
 * and touch input forwarding to the native C++ code.
 */
public class MainActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        // SDL2 is statically linked into MidiEditorMobile, no separate .so needed
        return new String[]{
            "MidiEditorMobile"
        };
    }

    @Override
    protected String getMainFunction() {
        // SDL.h renames main() to SDL_main() via a macro,
        // so the actual exported symbol is SDL_main
        return "SDL_main";
    }
}
