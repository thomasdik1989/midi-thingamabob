#pragma once

#include <array>
#include <string_view>

namespace midi {

// General MIDI instrument names (Program 0-127)
constexpr std::array<std::string_view, 128> GM_INSTRUMENT_NAMES = {{
    // Piano (0-7)
    "Acoustic Grand Piano",
    "Bright Acoustic Piano",
    "Electric Grand Piano",
    "Honky-tonk Piano",
    "Electric Piano 1",
    "Electric Piano 2",
    "Harpsichord",
    "Clavinet",

    // Chromatic Percussion (8-15)
    "Celesta",
    "Glockenspiel",
    "Music Box",
    "Vibraphone",
    "Marimba",
    "Xylophone",
    "Tubular Bells",
    "Dulcimer",

    // Organ (16-23)
    "Drawbar Organ",
    "Percussive Organ",
    "Rock Organ",
    "Church Organ",
    "Reed Organ",
    "Accordion",
    "Harmonica",
    "Tango Accordion",

    // Guitar (24-31)
    "Acoustic Guitar (nylon)",
    "Acoustic Guitar (steel)",
    "Electric Guitar (jazz)",
    "Electric Guitar (clean)",
    "Electric Guitar (muted)",
    "Overdriven Guitar",
    "Distortion Guitar",
    "Guitar Harmonics",

    // Bass (32-39)
    "Acoustic Bass",
    "Electric Bass (finger)",
    "Electric Bass (pick)",
    "Fretless Bass",
    "Slap Bass 1",
    "Slap Bass 2",
    "Synth Bass 1",
    "Synth Bass 2",

    // Strings (40-47)
    "Violin",
    "Viola",
    "Cello",
    "Contrabass",
    "Tremolo Strings",
    "Pizzicato Strings",
    "Orchestral Harp",
    "Timpani",

    // Ensemble (48-55)
    "String Ensemble 1",
    "String Ensemble 2",
    "Synth Strings 1",
    "Synth Strings 2",
    "Choir Aahs",
    "Voice Oohs",
    "Synth Voice",
    "Orchestra Hit",

    // Brass (56-63)
    "Trumpet",
    "Trombone",
    "Tuba",
    "Muted Trumpet",
    "French Horn",
    "Brass Section",
    "Synth Brass 1",
    "Synth Brass 2",

    // Reed (64-71)
    "Soprano Sax",
    "Alto Sax",
    "Tenor Sax",
    "Baritone Sax",
    "Oboe",
    "English Horn",
    "Bassoon",
    "Clarinet",

    // Pipe (72-79)
    "Piccolo",
    "Flute",
    "Recorder",
    "Pan Flute",
    "Blown Bottle",
    "Shakuhachi",
    "Whistle",
    "Ocarina",

    // Synth Lead (80-87)
    "Lead 1 (square)",
    "Lead 2 (sawtooth)",
    "Lead 3 (calliope)",
    "Lead 4 (chiff)",
    "Lead 5 (charang)",
    "Lead 6 (voice)",
    "Lead 7 (fifths)",
    "Lead 8 (bass + lead)",

    // Synth Pad (88-95)
    "Pad 1 (new age)",
    "Pad 2 (warm)",
    "Pad 3 (polysynth)",
    "Pad 4 (choir)",
    "Pad 5 (bowed)",
    "Pad 6 (metallic)",
    "Pad 7 (halo)",
    "Pad 8 (sweep)",

    // Synth Effects (96-103)
    "FX 1 (rain)",
    "FX 2 (soundtrack)",
    "FX 3 (crystal)",
    "FX 4 (atmosphere)",
    "FX 5 (brightness)",
    "FX 6 (goblins)",
    "FX 7 (echoes)",
    "FX 8 (sci-fi)",

    // Ethnic (104-111)
    "Sitar",
    "Banjo",
    "Shamisen",
    "Koto",
    "Kalimba",
    "Bagpipe",
    "Fiddle",
    "Shanai",

    // Percussive (112-119)
    "Tinkle Bell",
    "Agogo",
    "Steel Drums",
    "Woodblock",
    "Taiko Drum",
    "Melodic Tom",
    "Synth Drum",
    "Reverse Cymbal",

    // Sound Effects (120-127)
    "Guitar Fret Noise",
    "Breath Noise",
    "Seashore",
    "Bird Tweet",
    "Telephone Ring",
    "Helicopter",
    "Applause",
    "Gunshot"
}};

// Instrument categories
constexpr std::array<std::string_view, 16> GM_CATEGORIES = {{
    "Piano",
    "Chromatic Percussion",
    "Organ",
    "Guitar",
    "Bass",
    "Strings",
    "Ensemble",
    "Brass",
    "Reed",
    "Pipe",
    "Synth Lead",
    "Synth Pad",
    "Synth Effects",
    "Ethnic",
    "Percussive",
    "Sound Effects"
}};

inline int getCategoryForProgram(int program) {
    return program / 8;
}

inline std::string_view getInstrumentName(int program) {
    if (program >= 0 && program < 128) {
        return GM_INSTRUMENT_NAMES[program];
    }
    return "Unknown";
}

inline std::string_view getCategoryName(int category) {
    if (category >= 0 && category < 16) {
        return GM_CATEGORIES[category];
    }
    return "Unknown";
}

// General MIDI Percussion (Channel 10, note numbers)
constexpr std::array<std::string_view, 47> GM_PERCUSSION_NAMES = {{
    "Acoustic Bass Drum",  // 35
    "Bass Drum 1",         // 36
    "Side Stick",          // 37
    "Acoustic Snare",      // 38
    "Hand Clap",           // 39
    "Electric Snare",      // 40
    "Low Floor Tom",       // 41
    "Closed Hi-Hat",       // 42
    "High Floor Tom",      // 43
    "Pedal Hi-Hat",        // 44
    "Low Tom",             // 45
    "Open Hi-Hat",         // 46
    "Low-Mid Tom",         // 47
    "Hi-Mid Tom",          // 48
    "Crash Cymbal 1",      // 49
    "High Tom",            // 50
    "Ride Cymbal 1",       // 51
    "Chinese Cymbal",      // 52
    "Ride Bell",           // 53
    "Tambourine",          // 54
    "Splash Cymbal",       // 55
    "Cowbell",             // 56
    "Crash Cymbal 2",      // 57
    "Vibraslap",           // 58
    "Ride Cymbal 2",       // 59
    "Hi Bongo",            // 60
    "Low Bongo",           // 61
    "Mute Hi Conga",       // 62
    "Open Hi Conga",       // 63
    "Low Conga",           // 64
    "High Timbale",        // 65
    "Low Timbale",         // 66
    "High Agogo",          // 67
    "Low Agogo",           // 68
    "Cabasa",              // 69
    "Maracas",             // 70
    "Short Whistle",       // 71
    "Long Whistle",        // 72
    "Short Guiro",         // 73
    "Long Guiro",          // 74
    "Claves",              // 75
    "Hi Wood Block",       // 76
    "Low Wood Block",      // 77
    "Mute Cuica",          // 78
    "Open Cuica",          // 79
    "Mute Triangle",       // 80
    "Open Triangle"        // 81
}};

inline std::string_view getPercussionName(int note) {
    if (note >= 35 && note <= 81) {
        return GM_PERCUSSION_NAMES[note - 35];
    }
    return "Unknown";
}

} // namespace midi
