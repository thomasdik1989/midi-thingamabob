// Must be defined before <cmath> for M_PI on MSVC
#define _USE_MATH_DEFINES

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifdef _WIN32
#define NOMINMAX
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "../../third_party/miniaudio.h"

#define TSF_IMPLEMENTATION
#include "../../third_party/tsf.h"

#include "audio_synth.h"
#include "synth_presets.h"
#include <cmath>
#include <algorithm>
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
    int program = 0;
    double phase = 0.0;
    double release_phase = 0.0;
    bool releasing = false;

    // ADSR envelope
    double envelope = 0.0;
    double attack_time = 0.01;
    double decay_time = 0.1;
    double sustain_level = 0.7;
    double release_time = 0.3;
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
    std::array<std::atomic<float>, 16> channel_volume{};
    std::array<std::atomic<float>, 16> channel_pan{};  // 0.0=left, 0.5=center, 1.0=right

    int sampleRate = 44100;

    Impl() {
        channelPrograms.fill(0);
        for (auto& volume : channel_volume) volume.store(1.0f, std::memory_order_relaxed);
        for (auto& pan : channel_pan) pan.store(0.5f, std::memory_order_relaxed);
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

    static void applyPresetToVoice(SimpleVoice& voice, const SynthPreset& preset) {
        voice.attack_time = 0.01 * preset.attack_mul;
        voice.decay_time = 0.1 * preset.decay_mul;
        voice.sustain_level = 0.7 * preset.sustain_mul;
        voice.release_time = 0.3 * preset.release_mul;
    }

    static double generateWaveform(const SynthPreset& preset, double phase, double phaseInc, double time) {
        const double vibrato = preset.vibrato * std::sin(5.0 * time);
        const double phase2 = phase * (1.0 + preset.detune);
        const double phase3 = phase * (1.0 - preset.detune);

        switch (preset.waveform) {
        case WaveformKind::SineStack:
            return preset.h1 * std::sin(2.0 * M_PI * phase + vibrato) +
                   preset.h2 * std::sin(4.0 * M_PI * phase + vibrato) +
                   preset.h3 * std::sin(6.0 * M_PI * phase + vibrato) +
                   preset.h4 * std::sin(8.0 * M_PI * phase + vibrato);

        case WaveformKind::Organ:
            return preset.h1 * std::sin(2.0 * M_PI * phase) +
                   preset.h2 * std::sin(4.0 * M_PI * phase) +
                   preset.h3 * std::sin(6.0 * M_PI * phase) +
                   preset.h4 * std::sin(8.0 * M_PI * phase);

        case WaveformKind::Plucked:
            return std::sin(2.0 * M_PI * phase + vibrato) *
                   (1.0 + preset.h2 * std::sin(4.0 * M_PI * phase)) +
                   preset.h4 * std::sin(8.0 * M_PI * phase);

        case WaveformKind::Strings:
            return preset.h1 * std::sin(2.0 * M_PI * phase + vibrato) +
                   preset.h2 * std::sin(2.0 * M_PI * phase2) +
                   preset.h3 * std::sin(2.0 * M_PI * phase3);

        case WaveformKind::Reed: {
            double sample = 2.0 * phase - 1.0;
            sample -= polyBlep(phase, phaseInc);
            return sample * preset.h1 + preset.h2 * std::sin(2.0 * M_PI * phase + vibrato) +
                   preset.h3 * std::sin(4.0 * M_PI * phase);
        }

        case WaveformKind::Pipe:
            return std::sin(2.0 * M_PI * phase + vibrato);

        case WaveformKind::Square: {
            double saw1 = 2.0 * phase - 1.0;
            saw1 -= polyBlep(phase, phaseInc);
            double shifted = phase + 0.5;
            shifted -= std::floor(shifted);
            double saw2 = 2.0 * shifted - 1.0;
            saw2 -= polyBlep(shifted, phaseInc);
            return 0.8 * (saw1 - saw2);
        }

        case WaveformKind::Triangle:
        default: {
            double saw1 = 2.0 * phase - 1.0;
            saw1 -= polyBlep(phase, phaseInc);
            double shifted = phase + 0.5;
            shifted -= std::floor(shifted);
            double saw2 = 2.0 * shifted - 1.0;
            saw2 -= polyBlep(shifted, phaseInc);
            double triangle = 4.0 * std::abs(phase - 0.5) - 1.0;
            double square = saw1 - saw2;
            return 0.5 * triangle + 0.5 * square;
        }
        }
    }

    float generateDrumSample(SimpleVoice& voice, double dt) {
        voice.time += dt;

        double attack = 0.001;
        double release = 0.08;
        int pitch = voice.pitch;

        if (pitch == 35 || pitch == 36) {
            attack = 0.001;
            release = 0.18;
        } else if (pitch == 38 || pitch == 40) {
            attack = 0.001;
            release = 0.12;
        } else if (pitch == 42) {
            attack = 0.001;
            release = 0.04;
        } else if (pitch == 46) {
            attack = 0.001;
            release = 0.10;
        } else if (pitch == 49 || pitch == 57) {
            attack = 0.002;
            release = 0.45;
        } else if (pitch >= 41 && pitch <= 50) {
            attack = 0.001;
            release = 0.14;
        }

        if (!voice.releasing) {
            if (voice.time < attack) {
                voice.envelope = voice.time / attack;
            } else {
                voice.envelope = 1.0;
            }
            if (voice.time > release) {
                voice.releasing = true;
                voice.release_phase = 0.0;
            }
        } else {
            voice.release_phase += dt;
            double release_progress = voice.release_phase / release;
            if (release_progress >= 1.0) {
                voice.active = false;
                return 0.0f;
            }
            voice.envelope = 1.0 - release_progress;
        }

        double sample = 0.0;
        if (pitch == 35 || pitch == 36) {
            double freq = 80.0 * std::exp(-voice.time * 18.0);
            sample = std::sin(2.0 * M_PI * freq * voice.time) * std::exp(-voice.time * 10.0);
        } else if (pitch == 38 || pitch == 40) {
            double noise = std::sin(voice.time * 12000.0 + voice.phase * 97.0) *
                           std::sin(voice.time * 9000.0 + voice.phase * 53.0);
            sample = noise * std::exp(-voice.time * 22.0);
        } else if (pitch == 42 || pitch == 44) {
            double noise = std::sin(voice.time * 18000.0 + voice.phase * 31.0);
            sample = noise * std::exp(-voice.time * 55.0);
        } else if (pitch == 46) {
            double noise = std::sin(voice.time * 14000.0 + voice.phase * 41.0);
            sample = noise * std::exp(-voice.time * 18.0);
        } else if (pitch == 49 || pitch == 57) {
            double noise = std::sin(voice.time * 16000.0 + voice.phase * 17.0) *
                           std::sin(voice.time * 11000.0 + voice.phase * 71.0);
            sample = noise * std::exp(-voice.time * 4.0);
        } else {
            double freq = pitchToFreq(std::clamp(pitch, 36, 81));
            voice.phase += freq * dt;
            voice.phase -= std::floor(voice.phase);
            sample = std::sin(2.0 * M_PI * voice.phase) * std::exp(-voice.time * 12.0);
        }

        float velocity_scale = static_cast<float>(std::pow(voice.velocity / 127.0, 2.0));
        return static_cast<float>(sample * voice.envelope * velocity_scale * 0.7);
    }

    // Generate a sample for a voice
    float generateSample(SimpleVoice& voice, double dt) {
        if (!voice.active) return 0.0f;

        if (voice.channel == 9) {
            return generateDrumSample(voice, dt);
        }

        double freq = pitchToFreq(voice.pitch);
        const SynthPreset& preset = getSynthPreset(voice.program);

        voice.time += dt;

        if (!voice.releasing) {
            if (voice.time < voice.attack_time) {
                voice.envelope = voice.time / voice.attack_time;
            } else if (voice.time < voice.attack_time + voice.decay_time) {
                double decayProgress = (voice.time - voice.attack_time) / voice.decay_time;
                voice.envelope = 1.0 - (1.0 - voice.sustain_level) * decayProgress;
            } else {
                voice.envelope = voice.sustain_level;
            }
        } else {
            voice.release_phase += dt;
            double releaseProgress = voice.release_phase / voice.release_time;
            if (releaseProgress >= 1.0) {
                voice.active = false;
                return 0.0f;
            }
            voice.envelope = voice.sustain_level * (1.0 - releaseProgress);
        }

        double phaseInc = freq * dt;
        voice.phase += phaseInc;
        voice.phase -= std::floor(voice.phase);

        double sample = generateWaveform(preset, voice.phase, phaseInc, voice.time);

        if (preset.piano_decay && !voice.releasing && voice.time > 0.5) {
            voice.envelope *= std::exp(-2.0 * (voice.time - 0.5));
        }
        if (preset.pluck_decay && !voice.releasing && voice.time > 0.1) {
            voice.envelope *= std::exp(-3.0 * (voice.time - 0.1));
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
                        s *= impl->channel_volume[ch].load(std::memory_order_relaxed);
                        float pan = impl->channel_pan[ch].load(std::memory_order_relaxed);
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
    int program = (channel >= 0 && channel < 16) ? impl_->channelPrograms[channel] : 0;
    const SynthPreset& preset = getSynthPreset(program);

    auto* existing = impl_->findVoice(channel, pitch);
    if (existing) {
        existing->velocity = velocity;
        existing->program = program;
        existing->time = 0;
        existing->releasing = false;
        existing->release_phase = 0;
        Impl::applyPresetToVoice(*existing, preset);
        return;
    }

    auto* voice = impl_->findFreeVoice();
    if (voice) {
        voice->active = true;
        voice->pitch = pitch;
        voice->velocity = velocity;
        voice->channel = channel;
        voice->program = program;
        voice->phase = 0.0;
        voice->envelope = 0.0;
        voice->time = 0.0;
        voice->releasing = false;
        voice->release_phase = 0.0;
        Impl::applyPresetToVoice(*voice, preset);
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
            voice.release_phase = 0.0;
        }
    }
}

void AudioSynth::allNotesOff() {
    if (!initialized_) return;

    {
        std::lock_guard<std::mutex> sfLock(impl_->sfMutex);
        if (impl_->soundFont) {
            for (int channel = 0; channel < 16; ++channel) {
                tsf_channel_midi_control(impl_->soundFont, channel, 64, 0);
                tsf_channel_midi_control(impl_->soundFont, channel, 120, 0);
            }
            tsf_note_off_all(impl_->soundFont);
        }
    }

    std::lock_guard<std::mutex> lock(impl_->voicesMutex);

    for (auto& voice : impl_->voices) {
        if (voice.active) {
            voice.releasing = true;
            voice.release_phase = 0.0;
        }
    }
}

void AudioSynth::allNotesOffChannel(int channel) {
    if (!initialized_ || channel < 0 || channel >= 16) return;

    {
        std::lock_guard<std::mutex> sfLock(impl_->sfMutex);
        if (impl_->soundFont) {
            tsf_channel_midi_control(impl_->soundFont, channel, 64, 0);
            tsf_channel_midi_control(impl_->soundFont, channel, 120, 0);
            tsf_channel_midi_control(impl_->soundFont, channel, 123, 0);
        }
    }

    std::lock_guard<std::mutex> lock(impl_->voicesMutex);
    for (auto& voice : impl_->voices) {
        if (voice.active && voice.channel == channel) {
            voice.releasing = true;
            voice.release_phase = 0.0;
        }
    }
}

int AudioSynth::getChannelProgram(int channel) const {
    if (channel < 0 || channel >= 16) return 0;
    return impl_->channelPrograms[channel];
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

void AudioSynth::controlChange(int channel, int controller, int value) {
    if (!initialized_) return;
    {
        std::lock_guard<std::mutex> lock(impl_->sfMutex);
        if (impl_->soundFont) {
            tsf_channel_midi_control(impl_->soundFont, channel, controller, value);
            return;
        }
    }
    if (controller == 123 || controller == 120) {
        std::lock_guard<std::mutex> lock(impl_->voicesMutex);
        for (auto& voice : impl_->voices) {
            if (voice.channel == channel) voice.active = false;
        }
    }
}

void AudioSynth::pitchBend(int channel, int value) {
    if (!initialized_) return;
    std::lock_guard<std::mutex> lock(impl_->sfMutex);
    if (impl_->soundFont) tsf_channel_set_pitchwheel(impl_->soundFont, channel, value);
}

void AudioSynth::setChannelVolume(int channel, float volume) {
    if (channel >= 0 && channel < 16) {
        impl_->channel_volume[channel].store(
            std::max(0.0f, std::min(1.0f, volume)), std::memory_order_relaxed);
    }
}

void AudioSynth::setChannelPan(int channel, float pan) {
    if (channel >= 0 && channel < 16) {
        impl_->channel_pan[channel].store(
            std::max(0.0f, std::min(1.0f, pan)), std::memory_order_relaxed);
    }
}

void AudioSynth::setMasterVolume(float volume) {
    masterVolume_ = std::max(0.0f, std::min(1.0f, volume));
}

}
