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

void MidiPlayer::update(const Project& project, uint32_t currentTick, bool isPlaying,
                        const std::vector<PlaybackSpan>& spans) {
    if (!isPlaying) {
        if (wasPlaying_) panic();
        wasPlaying_ = false;
        lastTick_ = currentTick;
        return;
    }
    for (const auto& track : project.tracks) {
        audioSynth_.setChannelVolume(track.channel, track.volume);
        audioSynth_.setChannelPan(track.channel, track.pan);
    }
    const auto fallback = std::vector<PlaybackSpan>{{double(lastTick_), double(currentTick), !wasPlaying_ || currentTick < lastTick_}};
    for (const auto& span : spans.empty() ? fallback : spans) {
        for (const auto& message : schedulePlayback(project, span)) {
            const auto& data = message.data;
            int channel = data[0] & 15;
            int type = data[0] & 0xf0;
            if (type == 0x90 && data.size() >= 3) sendNoteOn(channel, data[1], data[2]);
            else if (type == 0x80 && data.size() >= 3) sendNoteOff(channel, data[1]);
            else if (type == 0xc0 && data.size() >= 2) sendProgramChange(channel, data[1]);
            else {
                if (useBuiltInSynth_) {
                    if (type == 0xb0 && data.size() >= 3) audioSynth_.controlChange(channel, data[1], data[2]);
                    if (type == 0xe0 && data.size() >= 3) audioSynth_.pitchBend(channel, data[1] | (data[2] << 7));
                }
                if (isDeviceOpen()) {
                    try { midiOut_->sendMessage(&data); }
                    catch (RtMidiError& error) { error.printMessage(); }
                }
            }
        }
    }
    wasPlaying_ = true;
    lastTick_ = currentTick;
}

void MidiPlayer::panic() {
    allNotesOff();
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
        for (int ch = 0; ch < 16; ++ch) {
            for (int controller : {64, 120, 123}) {
                std::vector<unsigned char> message{static_cast<unsigned char>(0xb0 | ch),
                    static_cast<unsigned char>(controller), 0};
                try { midiOut_->sendMessage(&message); }
                catch (RtMidiError& error) { error.printMessage(); }
            }
        }
    }
}

} // namespace midi
