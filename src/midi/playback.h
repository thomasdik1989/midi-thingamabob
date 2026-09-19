#pragma once
#include "types.h"
#include <vector>

namespace midi {
struct PlaybackSpan {
    double from = 0;
    double to = 0;
    bool reset = false;
    bool include_end = true;
};
struct PlaybackMessage {
    double tick = 0;
    std::vector<unsigned char> data;
};
// Pure scheduling, independent of the UI, audio device and wall clock.
std::vector<PlaybackMessage> schedulePlayback(const Project& project, const PlaybackSpan& span);
}
