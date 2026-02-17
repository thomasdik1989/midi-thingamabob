// AppDelegate.m - Minimal iOS app delegate for SDL2-based MIDI Editor.
// SDL2 handles the main application lifecycle on iOS. This file provides
// the required UIApplicationDelegate entry point that SDL expects.

#import <UIKit/UIKit.h>
#import <SDL.h>

// SDL provides its own UIApplicationDelegate implementation.
// We just need to ensure the SDL main function is called.
// The actual main() is in main_mobile.cpp via SDL_main.

// If you need custom iOS lifecycle handling (e.g., for UIDocumentPicker),
// subclass SDLUIKitDelegate and override the relevant methods here.
