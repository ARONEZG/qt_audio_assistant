#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "audio/AudioFormat.h"

namespace audio {

// Encodes decoded PCM into a 16-bit PCM WAV (RIFF) container - the most widely
// accepted container the transcription endpoint supports (MP3, WAV, M4A, FLAC,
// OGG, WebM). It is a free function on purpose: it holds no state, it is a
// single pure transformation, and it must stay usable without an object
// ("Utils"/"Manager" classes are avoided by design).
//
// Input: the stream description and its decoded samples. `format` supplies the
// sample rate and channel count written into the header; the sample encoding is
// always 32-bit float (the only SampleFormat defined, and the canonical capture
// encoding), so no encoding branch exists here. Samples are expected in
// [-1, 1]; values outside that range are clamped, so a source glitch can never
// wrap around into a loud pop.
//
// Output: a byte-exact RIFF file - "RIFF" + "fmt " + "data" chunks, 16-bit
// little-endian PCM, 44-byte header. Empty input yields a valid header with a
// zero data size (a valid but silent WAV) rather than an empty vector, so the
// caller never has to special-case "no audio captured".
//
// STL-only: no Qt, no platform code. The byte layout is therefore identical on
// Linux, Windows and macOS.
[[nodiscard]] std::vector<std::uint8_t> encodeWavPcm16(AudioFormat format, std::span<const float> samples);

}  // namespace audio
