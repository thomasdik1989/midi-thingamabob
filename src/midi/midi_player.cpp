#include "midi_player.h"
#include <RtMidi.h>
#include <algorithm>

namespace midi {

MidiPlayer::MidiPlayer() {
    // Initialize built-in audio synth
    audioSynth_.init();
    
    // Initialize MIDI output
    try {
        midiOut_ = std::make_unique<RtMidiOut>();
    } catch (RtMidiError& error) {
        error.printMessage();
    }
}

MidiPlayer::~MidiPlayer() {
    allNotesOff();
    
    if (midiOut_ && midiOut_->isPortOpen()) {
        midiOut_->closePort();
    }
    
    audioSynth_.shutdown();
}

std::vector<std::string> MidiPlayer::getOutputDevices() const {
    std::vector<std::string> devices;
    if (!midiOut_) return devices;
    
    unsigned int portCount = midiOut_->getPortCount();
    for (unsigned int i = 0; i < portCount; ++i) {
        try {
            devices.push_back(midiOut_->getPortName(i));
        } catch (RtMidiError& error) {
            devices.push_back("Unknown Device");
        }
    }
    return devices;
}

bool MidiPlayer::openDevice(int deviceIndex) {
    if (!midiOut_) return false;
    
    try {
        if (midiOut_->isPortOpen()) {
            allNotesOff();
            midiOut_->closePort();
        }
        
        if (deviceIndex >= 0 && deviceIndex < static_cast<int>(midiOut_->getPortCount())) {
            midiOut_->openPort(deviceIndex);
            currentDevice_ = deviceIndex;
            return true;
        }
    } catch (RtMidiError& error) {
        error.printMessage();
    }
    
    currentDevice_ = -1;
    return false;
}

void MidiPlayer::closeDevice() {
    if (midiOut_ && midiOut_->isPortOpen()) {
        allNotesOff();
        midiOut_->closePort();
    }
    currentDevice_ = -1;
}

bool MidiPlayer::isDeviceOpen() const {
    return midiOut_ && midiOut_->isPortOpen();
}

bool MidiPlayer::loadSoundFont(const std::string& filepath) {
    return audioSynth_.loadSoundFont(filepath);
}

void MidiPlayer::update(const Project& project, uint32_t currentTick, bool isPlaying) {
    // Built-in synth is always available
    bool hasOutput = useBuiltInSynth_ || isDeviceOpen();
    if (!hasOutput) return;
    
    // If we just started playing or playback position jumped
    if (isPlaying && (!wasPlaying_ || currentTick < lastTick_)) {
        // Stop all currently playing notes
        for (const auto& pn : playingNotes_) {
            sendNoteOff(pn.channel, pn.pitch);
        }
        playingNotes_.clear();
    }
    
    if (!isPlaying) {
        // Stop all playing notes when playback stops
        if (wasPlaying_) {
            for (const auto& pn : playingNotes_) {
                sendNoteOff(pn.channel, pn.pitch);
            }
            playingNotes_.clear();
        }
        wasPlaying_ = false;
        lastTick_ = currentTick;
        return;
    }
    
    // Check for notes that should end
    auto it = playingNotes_.begin();
    while (it != playingNotes_.end()) {
        if (currentTick >= it->endTick) {
            sendNoteOff(it->channel, it->pitch);
            it = playingNotes_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Check if any track is solo'd (compute once)
    bool hasSolo = false;
    for (const auto& t : project.tracks) {
        if (t.solo) { hasSolo = true; break; }
    }
    
    // Check for new notes that should start
    for (const auto& track : project.tracks) {
        if (track.muted) continue;
        if (hasSolo && !track.solo) continue;
        
        // Apply track volume/pan to audio synth channel
        if (useBuiltInSynth_) {
            audioSynth_.setChannelVolume(track.channel, track.volume);
            audioSynth_.setChannelPan(track.channel, track.pan);
        }
        
        for (const auto& note : track.notes) {
            // Check if note starts in the time window since last update
            if (note.start_tick > lastTick_ && note.start_tick <= currentTick) {
                // Check if this note isn't already playing
                bool alreadyPlaying = false;
                for (const auto& pn : playingNotes_) {
                    if (pn.channel == track.channel && pn.pitch == note.pitch) {
                        alreadyPlaying = true;
                        break;
                    }
                }
                
                if (!alreadyPlaying) {
                    sendNoteOn(track.channel, note.pitch, note.velocity);
                    playingNotes_.push_back({track.channel, note.pitch, note.endTick()});
                }
            }
        }
    }
    
    wasPlaying_ = isPlaying;
    lastTick_ = currentTick;
}

void MidiPlayer::panic() {
    allNotesOff();
    playingNotes_.clear();
}

void MidiPlayer::previewNoteOn(int channel, int pitch, int velocity) {
    sendNoteOn(channel, pitch, velocity);
}

void MidiPlayer::previewNoteOff(int channel, int pitch) {
    sendNoteOff(channel, pitch);
}

void MidiPlayer::sendProgramChange(int channel, int program) {
    // Send to built-in synth
    if (useBuiltInSynth_) {
        audioSynth_.programChange(channel, program);
    }
    
    // Send to external MIDI device
    if (isDeviceOpen()) {
        std::vector<unsigned char> message;
        message.push_back(0xC0 | (channel & 0x0F)); // Program Change
        message.push_back(program & 0x7F);
        
        try {
            midiOut_->sendMessage(&message);
        } catch (RtMidiError& error) {
            error.printMessage();
        }
    }
}

void MidiPlayer::sendNoteOn(int channel, int pitch, int velocity) {
    // Send to built-in synth
    if (useBuiltInSynth_) {
        audioSynth_.noteOn(channel, pitch, velocity);
    }
    
    // Send to external MIDI device
    if (isDeviceOpen()) {
        std::vector<unsigned char> message;
        message.push_back(0x90 | (channel & 0x0F)); // Note On
        message.push_back(pitch & 0x7F);
        message.push_back(velocity & 0x7F);
        
        try {
            midiOut_->sendMessage(&message);
        } catch (RtMidiError& error) {
            error.printMessage();
        }
    }
}

void MidiPlayer::sendNoteOff(int channel, int pitch) {
    // Send to built-in synth
    if (useBuiltInSynth_) {
        audioSynth_.noteOff(channel, pitch);
    }
    
    // Send to external MIDI device
    if (isDeviceOpen()) {
        std::vector<unsigned char> message;
        message.push_back(0x80 | (channel & 0x0F)); // Note Off
        message.push_back(pitch & 0x7F);
        message.push_back(0); // Velocity 0
        
        try {
            midiOut_->sendMessage(&message);
        } catch (RtMidiError& error) {
            error.printMessage();
        }
    }
}

void MidiPlayer::allNotesOff() {
    // Send to built-in synth
    if (useBuiltInSynth_) {
        audioSynth_.allNotesOff();
    }
    
    // Send to external MIDI device
    if (isDeviceOpen()) {
        // Send All Notes Off (CC 123) on all channels
        for (int ch = 0; ch < 16; ++ch) {
            std::vector<unsigned char> message;
            message.push_back(0xB0 | ch); // Control Change
            message.push_back(123);       // All Notes Off
            message.push_back(0);
            
            try {
                midiOut_->sendMessage(&message);
            } catch (RtMidiError& error) {
                error.printMessage();
            }
        }
    }
}

} // namespace midi
