#include "app/AudioCapture.h"

#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioSource.h"
#include "domain/TranscriptionProvider.h"

AudioCapture::AudioCapture(audio::AudioBuffer& buffer,
                           audio::AudioSource& source,
                           TranscriptionProvider& transcriber,
                           QObject* parent)
    : QObject(parent), buffer_(buffer), source_(source), transcriber_(transcriber) {
    // Signal-to-signal connections: the transcriber's terminal signals become
    // this object's own, with no logic in between. Binding both to `this` means
    // a transcriber that outlives the facade can never reach a destroyed
    // receiver.
    connect(&transcriber_, &TranscriptionProvider::transcribed, this, &AudioCapture::transcribed);
    connect(&transcriber_, &TranscriptionProvider::failed, this, &AudioCapture::failed);
}

AudioCapture::~AudioCapture() = default;

void AudioCapture::start() {
    source_.start();
}

void AudioCapture::stop() {
    source_.stop();
}

void AudioCapture::transcribeLast() {
    // snapshot() returns an independent copy, so the transcriber owns the block
    // it is handed and the capture callback keeps writing into the buffer.
    transcriber_.transcribe(buffer_.snapshot());
}

audio::AudioFormat AudioCapture::format() const noexcept {
    return source_.format();
}
