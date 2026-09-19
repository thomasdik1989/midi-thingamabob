#pragma once

#include <cstdint>

namespace midi {

enum class WaveformKind {
    SineStack,
    Organ,
    Plucked,
    Strings,
    Reed,
    Pipe,
    Square,
    Triangle,
};

struct SynthPreset {
    WaveformKind waveform = WaveformKind::SineStack;
    float h1 = 0.5f;
    float h2 = 0.25f;
    float h3 = 0.125f;
    float h4 = 0.0f;
    float attack_mul = 1.0f;
    float decay_mul = 1.0f;
    float sustain_mul = 1.0f;
    float release_mul = 1.0f;
    float detune = 0.0f;
    float vibrato = 0.0f;
    bool piano_decay = false;
    bool pluck_decay = false;
};

const SynthPreset& getSynthPreset(int program);
bool synthPresetsEqual(const SynthPreset& a, const SynthPreset& b);

} // namespace midi
