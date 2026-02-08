#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

namespace midi {

class AudioSynth {
public:
    AudioSynth();
    ~AudioSynth();
    
    // Initialize audio output
    bool init();
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // SoundFont loading (optional - falls back to simple synth)
    bool loadSoundFont(const std::string& filepath);
    bool hasSoundFont() const { return soundFontLoaded_; }
    
    // Note control
    void noteOn(int channel, int pitch, int velocity);
    void noteOff(int channel, int pitch);
    void allNotesOff();
    
    // Program change
    void programChange(int channel, int program);
    
    // Per-channel volume and pan
    void setChannelVolume(int channel, float volume);
    void setChannelPan(int channel, float pan);
    
    // Volume control (0.0 - 1.0)
    void setMasterVolume(float volume);
    float getMasterVolume() const { return masterVolume_; }
    
private:
    bool initialized_ = false;
    bool soundFontLoaded_ = false;
    std::atomic<float> masterVolume_{0.8f};
    
    // Forward declare implementation details (PIMPL pattern)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace midi
