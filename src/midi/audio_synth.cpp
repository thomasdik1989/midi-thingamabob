#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio.h"

#define TSF_IMPLEMENTATION
#include "../../third_party/tsf.h"

#include "audio_synth.h"
#include <cmath>
#include <array>
#include <atomic>

namespace midi {

    // PolyBLEP residual for anti-aliased waveforms
    // Please look at https://www.martin-finke.de/articles/audio-plugins-018-polyblep-oscillator/
    // for an explanation.
    static double polyBlep(double t, double dt)
    {
        // t is phase (0..1), dt is phase increment per sample
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        else if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
}

// Simple oscillator for when no SoundFont is loaded
struct SimpleVoice {
    bool active = false;
    int pitch = 60;
    int velocity = 0;
    int channel = 0;
    double phase = 0.0;
    double releasePhase = 0.0;
    bool releasing = false;

    // ADSR envelope
    double envelope = 0.0;
    double attackTime = 0.01;
    double decayTime = 0.1;
    double sustainLevel = 0.7;
    double releaseTime = 0.3;
    double time = 0.0;
};

struct AudioSynth::Impl {
    ma_device device;
    ma_device_config deviceConfig;

    // SoundFont pointer -- guarded by sfMutex
    tsf* soundFont = nullptr;
    std::mutex sfMutex;

    AudioSynth* parent = nullptr;

    // Simple synth voices (polyphony)
    static constexpr int MAX_VOICES = 64;
    std::array<SimpleVoice, MAX_VOICES> voices;
    std::mutex voicesMutex;

    // Per-channel program and volume/pan
    std::array<int, 16> channelPrograms{};
    std::array<float, 16> channelVolume{};
    std::array<float, 16> channelPan{};  // 0.0=left, 0.5=center, 1.0=right

    int sampleRate = 44100;

    Impl() {
        channelPrograms.fill(0);
        channelVolume.fill(1.0f);
        channelPan.fill(0.5f);
    }

    SimpleVoice* findFreeVoice() {
        // First try to find an inactive voice
        for (auto& v : voices) {
            if (!v.active) return &v;
        }
        // Steal the oldest releasing voice
        for (auto& v : voices) {
            if (v.releasing) return &v;
        }
        // Steal the oldest voice
        return &voices[0];
    }

    SimpleVoice* findVoice(int channel, int pitch) {
        for (auto& v : voices) {
            if (v.active && v.channel == channel && v.pitch == pitch && !v.releasing) {
                return &v;
            }
        }
        return nullptr;
    }

    static double pitchToFreq(int pitch) {
        return 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
    }

    // Generate a sample for a voice
    float generateSample(SimpleVoice& voice, double dt) {
        if (!voice.active) return 0.0f;

        double freq = pitchToFreq(voice.pitch);
        int ch = voice.channel;
        int program = (ch >= 0 && ch < 16) ? channelPrograms[ch] : 0;

        voice.time += dt;

        if (!voice.releasing) {
            if (voice.time < voice.attackTime) {
                voice.envelope = voice.time / voice.attackTime;
            } else if (voice.time < voice.attackTime + voice.decayTime) {
                double decayProgress = (voice.time - voice.attackTime) / voice.decayTime;
                voice.envelope = 1.0 - (1.0 - voice.sustainLevel) * decayProgress;
            } else {
                voice.envelope = voice.sustainLevel;
            }
        } else {
            voice.releasePhase += dt;
            double releaseProgress = voice.releasePhase / voice.releaseTime;
            if (releaseProgress >= 1.0) {
                voice.active = false;
                return 0.0f;
            }
            voice.envelope = voice.sustainLevel * (1.0 - releaseProgress);
        }

        // Generate waveform based on program category
        double sample = 0.0;
        int category = program / 8;

        // Phase increment for PolyBLEP
        double phaseInc = freq * dt;

        voice.phase += phaseInc;
        voice.phase -= std::floor(voice.phase);  // Modulo 1.0 without drift

        double phase = voice.phase;

        //see https://en.wikipedia.org/wiki/Additive_synthesis for a detailed explanation.
        //  We mix sine waves to create the "timbre" or "color" of the sound.
        switch (category)
        {
        case 0: // Piano - combination of harmonics
        case 1: // Chromatic Percussion
            sample = 0.5 * std::sin(2.0 * M_PI * phase) +
                     0.25 * std::sin(4.0 * M_PI * phase) +
                     0.125 * std::sin(6.0 * M_PI * phase);
            // Quick decay for piano-like sounds
            if (!voice.releasing && voice.time > 0.5)
            {
                voice.envelope *= std::exp(-2.0 * (voice.time - 0.5));
            }
            break;

        case 2: // Organ - additive harmonics.
            sample = 0.4 * std::sin(2.0 * M_PI * phase) +
                     0.3 * std::sin(4.0 * M_PI * phase) +
                     0.2 * std::sin(6.0 * M_PI * phase) +
                     0.1 * std::sin(8.0 * M_PI * phase);
            break;

        case 3: // Guitar
        case 4: // Bass
            sample = std::sin(2.0 * M_PI * phase) *
                     (1.0 + 0.3 * std::sin(4.0 * M_PI * phase));
            if (!voice.releasing && voice.time > 0.1)
            {
                voice.envelope *= std::exp(-3.0 * (voice.time - 0.1));
            }
            break;

        case 5: // Strings
        case 6: // Ensemble
            // Slight detuning for string ensemble effect
            sample = 0.5 * std::sin(2.0 * M_PI * phase) +
                     0.3 * std::sin(2.0 * M_PI * phase * 1.002) +
                     0.2 * std::sin(2.0 * M_PI * phase * 0.998);
            break;

        case 7: // Brass
        case 8:
        { // Reed -- PolyBLEP sawtooth
            sample = 2.0 * phase - 1.0;
            sample -= polyBlep(phase, phaseInc);
            sample = sample * 0.7 + 0.3 * std::sin(2.0 * M_PI * phase);
            break;
            }

            case 9: // Pipe
                // Pure sine with slight vibrato
                sample = std::sin(2.0 * M_PI * phase + 0.02 * std::sin(5.0 * voice.time));
                break;

            case 10: // Synth Lead
            case 11: { // Synth Pad -- PolyBLEP square
                // Band-limited square: sawtooth - phase-shifted sawtooth
                double saw1 = 2.0 * phase - 1.0;
                saw1 -= polyBlep(phase, phaseInc);
                double phase2 = phase + 0.5;
                phase2 -= std::floor(phase2);
                double saw2 = 2.0 * phase2 - 1.0;
                saw2 -= polyBlep(phase2, phaseInc);
                sample = 0.8 * (saw1 - saw2);
                break;
            }

            case 12: // Synth Effects
            case 13: // Ethnic
            case 14: // Percussive
            case 15: // Sound Effects
            default: {
                // PolyBLEP triangle (integrated square)
                double saw1 = 2.0 * phase - 1.0;
                saw1 -= polyBlep(phase, phaseInc);
                double phase2 = phase + 0.5;
                phase2 -= std::floor(phase2);
                double saw2 = 2.0 * phase2 - 1.0;
                saw2 -= polyBlep(phase2, phaseInc);
                double sq = saw1 - saw2;
                // Integrate square to get triangle (approximation)
                sample = 4.0 * std::abs(phase - 0.5) - 1.0;
                // Blend with integrated PolyBLEP square for better quality
                sample = 0.5 * sample + 0.5 * sq;
                break;
            }
            }

        // Apply envelope and exponential velocity curve
        float velocityScale = static_cast<float>(std::pow(voice.velocity / 127.0, 2.0));
        return static_cast<float>(sample * voice.envelope * velocityScale * 0.5);
    }

    // Audio callback - static method to be passed to miniaudio
    static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
        Impl* impl = static_cast<Impl*>(device->pUserData);
        float* out = static_cast<float*>(output);
        float volume = impl->parent->getMasterVolume();

        // Try to lock soundFont, if we can't use simple synth.
        {
            std::unique_lock<std::mutex> sfLock(impl->sfMutex, std::try_to_lock);
            if (sfLock.owns_lock() && impl->soundFont) {
                // Use TinySoundFont
                tsf_render_float(impl->soundFont, out, static_cast<int>(frameCount), 0);

                // Apply master volume.
                // There is still a wee thing not quite right here.
                for (ma_uint32 i = 0; i < frameCount * 2; ++i) {
                    out[i] *= volume;
                }
                return;
            }
        }

        // Use simple synth
        double dt = 1.0 / impl->sampleRate;

        std::lock_guard<std::mutex> lock(impl->voicesMutex);

        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float sampleL = 0.0f;
            float sampleR = 0.0f;

            for (auto& voice : impl->voices) {
                if (voice.active) {
                    float s = impl->generateSample(voice, dt);

                    // Apply per-channel volume and pan
                    int ch = voice.channel;
                    if (ch >= 0 && ch < 16) {
                        s *= impl->channelVolume[ch];
                        float pan = impl->channelPan[ch];
                        sampleL += s * (1.0f - pan);
                        sampleR += s * pan;
                    } else {
                        sampleL += s * 0.5f;
                        sampleR += s * 0.5f;
                    }
                }
            }

            // Soft clipping to prevent harsh distortion
            auto softClip = [](float x) -> float {
                if (x > 1.0f) return 1.0f - 1.0f / (1.0f + x);
                if (x < -1.0f) return -1.0f + 1.0f / (1.0f - x);
                return x;
            };

            out[i * 2] = softClip(sampleL * volume);
            out[i * 2 + 1] = softClip(sampleR * volume);
        }
    }
};

AudioSynth::AudioSynth() : impl_(std::make_unique<Impl>()) {
    impl_->parent = this;
}

AudioSynth::~AudioSynth() {
    shutdown();
}

bool AudioSynth::init() {
    if (initialized_) return true;

    impl_->deviceConfig = ma_device_config_init(ma_device_type_playback);
    impl_->deviceConfig.playback.format = ma_format_f32;
    impl_->deviceConfig.playback.channels = 2;
    impl_->deviceConfig.sampleRate = impl_->sampleRate;
    impl_->deviceConfig.dataCallback = Impl::audioCallback;
    impl_->deviceConfig.pUserData = impl_.get();

    if (ma_device_init(nullptr, &impl_->deviceConfig, &impl_->device) != MA_SUCCESS) {
        fprintf(stderr, "Audio error: Failed to initialize audio device\n");
        return false;
    }

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        fprintf(stderr, "Audio error: Failed to start audio device\n");
        ma_device_uninit(&impl_->device);
        return false;
    }

    fprintf(stderr, "Audio: Initialized at %d Hz\n", impl_->sampleRate);
    initialized_ = true;
    return true;
}

void AudioSynth::shutdown() {
    if (!initialized_) return;

    // Stop and uninit audio device first (ensures callback won't run after)
    ma_device_uninit(&impl_->device);

    {
        std::lock_guard<std::mutex> lock(impl_->sfMutex);
        if (impl_->soundFont) {
            tsf_close(impl_->soundFont);
            impl_->soundFont = nullptr;
        }
    }

    initialized_ = false;
    soundFontLoaded_ = false;
}

bool AudioSynth::loadSoundFont(const std::string& filepath) {
    // Load the new soundfont first, then swap
    tsf* newSf = tsf_load_filename(filepath.c_str());
    if (!newSf) {
        fprintf(stderr, "Audio error: Failed to load SoundFont: %s\n", filepath.c_str());
        soundFontLoaded_ = false;
        return false;
    }

    tsf_set_output(newSf, TSF_STEREO_INTERLEAVED, impl_->sampleRate, 0);

    // Swap under lock
    {
        std::lock_guard<std::mutex> lock(impl_->sfMutex);
        tsf* oldSf = impl_->soundFont;
        impl_->soundFont = newSf;
        if (oldSf) {
            tsf_close(oldSf);
        }
    }

    fprintf(stderr, "Audio: Loaded SoundFont: %s\n", filepath.c_str());
    soundFontLoaded_ = true;
    return true;
}

void AudioSynth::noteOn(int channel, int pitch, int velocity) {
    if (!initialized_) return;

    {
        std::lock_guard<std::mutex> sfLock(impl_->sfMutex);
        if (impl_->soundFont) {
            tsf_channel_note_on(impl_->soundFont, channel, pitch, velocity / 127.0f);
            return;
        }
    }

    std::lock_guard<std::mutex> lock(impl_->voicesMutex);

    // Check if note is already playing
    auto* existing = impl_->findVoice(channel, pitch);
    if (existing) {
        existing->velocity = velocity;
        existing->time = 0;
        existing->releasing = false;
        existing->releasePhase = 0;
        return;
    }

    auto* voice = impl_->findFreeVoice();
    if (voice) {
        voice->active = true;
        voice->pitch = pitch;
        voice->velocity = velocity;
        voice->channel = channel;
        voice->phase = 0.0;
        voice->envelope = 0.0;
        voice->time = 0.0;
        voice->releasing = false;
        voice->releasePhase = 0.0;
    }
}

void AudioSynth::noteOff(int channel, int pitch) {
    if (!initialized_) return;

    {
        std::lock_guard<std::mutex> sfLock(impl_->sfMutex);
        if (impl_->soundFont) {
            tsf_channel_note_off(impl_->soundFont, channel, pitch);
            return;
        }
    }

    std::lock_guard<std::mutex> lock(impl_->voicesMutex);

    for (auto& voice : impl_->voices) {
        if (voice.active && voice.channel == channel && voice.pitch == pitch && !voice.releasing) {
            voice.releasing = true;
            voice.releasePhase = 0.0;
        }
    }
}

void AudioSynth::allNotesOff() {
    if (!initialized_) return;

    {
        std::lock_guard<std::mutex> sfLock(impl_->sfMutex);
        if (impl_->soundFont) {
            tsf_note_off_all(impl_->soundFont);
        }
    }

    std::lock_guard<std::mutex> lock(impl_->voicesMutex);

    for (auto& voice : impl_->voices) {
        if (voice.active) {
            voice.releasing = true;
            voice.releasePhase = 0.0;
        }
    }
}

void AudioSynth::programChange(int channel, int program) {
    if (!initialized_) return;

    {
        std::lock_guard<std::mutex> sfLock(impl_->sfMutex);
        if (impl_->soundFont) {
            tsf_channel_set_presetnumber(impl_->soundFont, channel, program, channel == 9);
        }
    }

    // Also store for simple synth
    if (channel >= 0 && channel < 16) {
        impl_->channelPrograms[channel] = program;
    }
}

void AudioSynth::setChannelVolume(int channel, float volume) {
    if (channel >= 0 && channel < 16) {
        impl_->channelVolume[channel] = std::max(0.0f, std::min(1.0f, volume));
    }
}

void AudioSynth::setChannelPan(int channel, float pan) {
    if (channel >= 0 && channel < 16) {
        impl_->channelPan[channel] = std::max(0.0f, std::min(1.0f, pan));
    }
}

void AudioSynth::setMasterVolume(float volume) {
    masterVolume_ = std::max(0.0f, std::min(1.0f, volume));
}

}
