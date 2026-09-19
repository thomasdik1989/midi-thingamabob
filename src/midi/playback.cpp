#include "playback.h"
#include <algorithm>

namespace midi {
namespace {
bool trackAudible(const Track& track, bool soloActive) {
    if (track.muted) return false;
    if (soloActive && !track.solo) return false;
    return true;
}

int noteChannel(const Track& track, const Note& note) {
    if (note.channel < 0 || note.channel == track.source_channel) return track.channel;
    return note.channel;
}

bool eventInSpan(uint32_t tick, const PlaybackSpan& span) {
    if (tick <= span.from) return false;
    if (tick < span.to) return true;
    return span.include_end && tick <= span.to;
}

bool noteStartsInSpan(uint32_t startTick, const PlaybackSpan& span) {
    if (startTick <= span.from) return false;
    if (startTick < span.to) return true;
    return span.include_end && startTick <= span.to;
}

int messagePriority(const PlaybackMessage& message) {
    const int type = message.data[0] & 0xf0;
    if (type == 0x90) return 2;
    if (type == 0x80) return 1;
    return 0;
}

void appendResetMessages(std::vector<PlaybackMessage>& messages, const PlaybackSpan& span) {
    for (int channel = 0; channel < 16; ++channel) {
        messages.push_back({span.from, {static_cast<unsigned char>(0xb0 | channel), 64, 0}});
        messages.push_back({span.from, {static_cast<unsigned char>(0xb0 | channel), 123, 0}});
        messages.push_back({span.from, {static_cast<unsigned char>(0xb0 | channel), 121, 0}});
    }
}

void appendTrackEvents(const Project& project, const Track& track, const PlaybackSpan& span,
                       std::vector<PlaybackMessage>& messages, std::vector<PlaybackMessage>& chase,
                       bool soloActive) {
    if (!trackAudible(track, soloActive)) return;

    if (span.reset) {
        messages.push_back({span.from, {static_cast<unsigned char>(0xc0 | track.channel),
                                        static_cast<unsigned char>(track.program)}});
    }

    for (const auto& event : track.events) {
        auto data = event.data;
        if (data.empty() || data[0] < 0x80 || data[0] >= 0xf0) continue;

        const int type = data[0] & 0xf0;
        const size_t requiredSize = (type == 0xc0 || type == 0xd0) ? 2u : 3u;
        if (data.size() < requiredSize) continue;

        if ((data[0] & 15) == track.source_channel) {
            data[0] = (data[0] & 0xf0) | track.channel;
        }
        if (type == 0xc0 && event.tick == 0 && (data[0] & 15) == track.channel &&
            track.program != track.source_program) {
            data[1] = static_cast<unsigned char>(track.program);
        }

        if (span.reset && event.tick <= span.from && (data[0] & 0xf0) >= 0xb0) {
            chase.push_back({double(event.tick), data});
        } else if (eventInSpan(event.tick, span)) {
            messages.push_back({double(event.tick), data});
        }
    }
}

void appendTrackNotes(const Track& track, const PlaybackSpan& span, std::vector<PlaybackMessage>& messages,
                      bool soloActive) {
    if (!trackAudible(track, soloActive)) return;

    for (const auto& note : track.notes) {
        if (note.duration == 0) continue;

        const int channel = noteChannel(track, note);
        const bool chased = span.reset && note.start_tick <= span.from && note.endTick() > span.from;
        if (chased || noteStartsInSpan(note.start_tick, span)) {
            const double tick = chased ? span.from : double(note.start_tick);
            messages.push_back({tick, {static_cast<unsigned char>(0x90 | channel),
                                       static_cast<unsigned char>(note.pitch),
                                       static_cast<unsigned char>(note.velocity)}});
        }
        if (note.endTick() > span.from && note.endTick() <= span.to) {
            messages.push_back({double(note.endTick()),
                                {static_cast<unsigned char>(0x80 | channel),
                                 static_cast<unsigned char>(note.pitch),
                                 static_cast<unsigned char>(note.release_velocity)}});
        }
    }
}
}

std::vector<PlaybackMessage> schedulePlayback(const Project& project, const PlaybackSpan& span) {
    std::vector<PlaybackMessage> messages;
    bool soloActive = false;
    for (const auto& track : project.tracks) {
        soloActive = soloActive || track.solo;
    }

    if (span.reset) appendResetMessages(messages, span);

    std::vector<PlaybackMessage> chase;
    for (const auto& track : project.tracks) {
        appendTrackEvents(project, track, span, messages, chase, soloActive);
    }

    std::stable_sort(chase.begin(), chase.end(),
                     [](const auto& left, const auto& right) { return left.tick < right.tick; });
    for (auto& event : chase) {
        event.tick = span.from;
        messages.push_back(std::move(event));
    }

    for (const auto& track : project.tracks) {
        appendTrackNotes(track, span, messages, soloActive);
    }

    std::stable_sort(messages.begin(), messages.end(), [](const auto& left, const auto& right) {
        if (left.tick != right.tick) return left.tick < right.tick;
        return messagePriority(left) < messagePriority(right);
    });
    return messages;
}
}
