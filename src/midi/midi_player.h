#pragma once

#include "types.h"
#include "audio_synth.h"
#include <RtMidi.h>
#include <memory>
#include <vector>
#include <string>

namespace midi {

class MidiPlayer {
public:
    MidiPlayer();
    ~MidiPlayer();

    // Built-in audio synth (always available)
    AudioSynth& getAudioSynth() { return audioSynth_; }
    bool isAudioEnabled() const { return useBuiltInSynth_; }
    void setAudioEnabled(bool enabled) { useBuiltInSynth_ = enabled; }

    // External MIDI device management
    std::vector<std::string> getOutputDevices() const;
    bool openDevice(int deviceIndex);
    void closeDevice();
    bool isDeviceOpen() const;
    int getCurrentDevice() const { return currentDevice_; }

    // Load SoundFont for better audio quality
    bool loadSoundFont(const std::string& filepath);

    // Playback
    void update(const Project& project, uint32_t currentTick, bool isPlaying);
    void panic(); // All notes off

    // Preview note (for clicking on piano roll)
    void previewNoteOn(int channel, int pitch, int velocity);
    void previewNoteOff(int channel, int pitch);

    // Send program change
    void sendProgramChange(int channel, int program);

private:
    void sendNoteOn(int channel, int pitch, int velocity);
    void sendNoteOff(int channel, int pitch);
    void allNotesOff();

    // Built-in audio synthesizer
    AudioSynth audioSynth_;
    bool useBuiltInSynth_ = true;

    // External MIDI output
    std::unique_ptr<RtMidiOut> midiOut_;
    int currentDevice_ = -1;

    // Track which notes are currently playing
    struct PlayingNote {
        int channel;
        int pitch;
        uint32_t endTick;
    };
    std::vector<PlayingNote> playingNotes_;

    uint32_t lastTick_ = 0;
    bool wasPlaying_ = false;
};

} // namespace midi
