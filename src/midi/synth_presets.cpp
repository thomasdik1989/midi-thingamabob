#include "synth_presets.h"

#include <array>

namespace midi {

namespace {

float variantMix(int variant, float base, float spread) {
    return base + spread * (static_cast<float>(variant) - 3.5f) / 3.5f;
}

SynthPreset makeSynthPreset(int program) {
    const int category = program / 8;
    const int variant = program % 8;

    SynthPreset preset{};
    preset.h1 = variantMix(variant, 0.5f, 0.12f);
    preset.h2 = variantMix(variant, 0.25f, 0.10f);
    preset.h3 = variantMix(variant, 0.125f, 0.08f);
    preset.h4 = variantMix(variant, 0.04f, 0.06f);
    preset.attack_mul = variantMix(variant, 1.0f, 0.25f);
    preset.decay_mul = variantMix(variant, 1.0f, 0.35f);
    preset.sustain_mul = variantMix(variant, 1.0f, 0.15f);
    preset.release_mul = variantMix(variant, 1.0f, 0.30f);
    preset.detune = 0.001f * static_cast<float>(variant);
    preset.vibrato = 0.005f * static_cast<float>(variant);

    switch (category) {
    case 0:
    case 1:
        preset.waveform = WaveformKind::SineStack;
        preset.piano_decay = true;
        break;
    case 2:
        preset.waveform = WaveformKind::Organ;
        preset.h1 = variantMix(variant, 0.4f, 0.10f);
        preset.h2 = variantMix(variant, 0.3f, 0.10f);
        preset.h3 = variantMix(variant, 0.2f, 0.08f);
        preset.h4 = variantMix(variant, 0.1f, 0.08f);
        preset.sustain_mul = variantMix(variant, 1.0f, 0.05f);
        preset.release_mul = variantMix(variant, 1.2f, 0.40f);
        break;
    case 3:
    case 4:
        preset.waveform = WaveformKind::Plucked;
        preset.pluck_decay = true;
        preset.decay_mul = variantMix(variant, 0.9f, 0.30f);
        break;
    case 5:
    case 6:
        preset.waveform = WaveformKind::Strings;
        preset.detune = variantMix(variant, 0.002f, 0.002f);
        break;
    case 7:
    case 8:
        preset.waveform = WaveformKind::Reed;
        preset.h1 = variantMix(variant, 0.7f, 0.12f);
        preset.h2 = variantMix(variant, 0.3f, 0.10f);
        break;
    case 9:
        preset.waveform = WaveformKind::Pipe;
        preset.vibrato = variantMix(variant, 0.02f, 0.02f);
        break;
    case 10:
    case 11:
        preset.waveform = WaveformKind::Square;
        break;
    default:
        preset.waveform = WaveformKind::Triangle;
        break;
    }

    switch (program) {
    case 21: // Accordion
        preset.waveform = WaveformKind::Reed;
        preset.detune = 0.003f;
        preset.decay_mul = 0.85f;
        break;
    case 22: // Harmonica
        preset.waveform = WaveformKind::Reed;
        preset.h1 = 0.55f;
        preset.h2 = 0.30f;
        preset.h3 = 0.15f;
        preset.h4 = 0.0f;
        preset.vibrato = 0.04f;
        preset.decay_mul = 0.55f;
        preset.release_mul = 0.7f;
        break;
    case 23: // Tango Accordion
        preset.waveform = WaveformKind::Square;
        preset.detune = 0.004f;
        preset.decay_mul = 0.75f;
        break;
    case 31: // Guitar harmonics
        preset.waveform = WaveformKind::Plucked;
        preset.h4 = 0.30f;
        preset.decay_mul = 0.35f;
        preset.pluck_decay = true;
        break;
    default:
        break;
    }

    return preset;
}

const std::array<SynthPreset, 128>& gmPresets() {
    static const std::array<SynthPreset, 128> presets = [] {
        std::array<SynthPreset, 128> built{};
        for (int i = 0; i < 128; ++i) {
            built[i] = makeSynthPreset(i);
        }
        return built;
    }();
    return presets;
}

} // namespace

const SynthPreset& getSynthPreset(int program) {
    if (program < 0 || program >= 128) {
        return gmPresets()[0];
    }
    return gmPresets()[program];
}

bool synthPresetsEqual(const SynthPreset& a, const SynthPreset& b) {
    return a.waveform == b.waveform &&
           a.h1 == b.h1 && a.h2 == b.h2 && a.h3 == b.h3 && a.h4 == b.h4 &&
           a.attack_mul == b.attack_mul && a.decay_mul == b.decay_mul &&
           a.sustain_mul == b.sustain_mul && a.release_mul == b.release_mul &&
           a.detune == b.detune && a.vibrato == b.vibrato &&
           a.piano_decay == b.piano_decay && a.pluck_decay == b.pluck_decay;
}

} // namespace midi
