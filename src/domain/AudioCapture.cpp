#include "domain/AudioCapture.h"

#include <QDebug>
#include <optional>
#include <span>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioSource.h"
#include "audio/VadSpeechGate.h"
#include "domain/ProviderErrorText.h"
#include "domain/TranscriptionProvider.h"

AudioCapture::AudioCapture(audio::AudioBuffer& buffer,
                           audio::AudioSource& source,
                           TranscriptionProvider& transcriber,
                           QObject* parent)
    : QObject(parent), buffer_(buffer), source_(source), transcriber_(transcriber) {
    // The transcriber's terminal signals are relayed through this object's own
    // slots, which also fold a failure into the view state; binding both to
    // `this` means a transcriber that outlives the facade can never reach a
    // destroyed receiver.
    connect(&transcriber_, &TranscriptionProvider::transcribed, this, &AudioCapture::onTranscribed);
    connect(&transcriber_, &TranscriptionProvider::failed, this, &AudioCapture::onFailed);
}

AudioCapture::~AudioCapture() = default;

void AudioCapture::setVadGate(audio::VadSpeechGate& gate) {
    vadGate_ = &gate;
}

void AudioCapture::start() {
    source_.start();
}

void AudioCapture::stop() {
    source_.stop();
}

void AudioCapture::transcribeLast() {
    // A new request supersedes the failure of the previous one (mirrors
    // Chat::send). The snapshot is an independent copy, so the transcriber owns
    // the block it is handed and the capture callback keeps writing into the
    // buffer. An empty buffer produces an empty block: it is still a request, and
    // whether the backend accepts it is the backend's decision.
    clearError();
    std::vector<float> snapshot = buffer_.snapshot();
    if (snapshot.empty()) {
        qWarning() << "Transcribing an empty buffer: no audio captured yet (is background capture on?)";
    } else {
        const double kSeconds =
                static_cast<double>(snapshot.size()) / static_cast<double>(source_.format().sampleRate());
        qInfo() << "Transcribing" << snapshot.size() << "samples (" << kSeconds << "s)";
    }

    // When a VAD gate is attached, trim the silence: the transcriber only
    // receives the samples inside the detected speech intervals. The gate
    // classifies the same canonical stream the buffer holds (freshly, from the
    // snapshot), so its intervals map 1:1 onto the snapshot.
    if (vadGate_ != nullptr && !snapshot.empty()) {
        const std::size_t kCapturedSamples = snapshot.size();
        snapshot = vadGate_->trimSilence(std::span<const float>(snapshot));
        qInfo() << "VAD kept" << snapshot.size() << "of" << kCapturedSamples << "samples";
    }

    transcriber_.transcribe(std::move(snapshot));
}

audio::AudioFormat AudioCapture::format() const noexcept {
    return source_.format();
}

QString AudioCapture::errorText() const {
    return errorText_;
}

bool AudioCapture::hasError() const noexcept {
    return hasError_;
}

void AudioCapture::onTranscribed(const QString& text) {
    qInfo() << "Transcription relayed:" << text.size() << "characters";
    emit transcribed(text);
}

void AudioCapture::onFailed(ProviderError error, const QString& message) {
    qWarning() << "Transcription failed (error" << static_cast<int>(error) << "):" << message;
    // Cancellation is not a failure (mirrors Chat): it is relayed but leaves no
    // error on the view.
    if (const std::optional<QString> kText = providerFailureText(error, message); kText.has_value()) {
        errorText_ = *kText;
        hasError_ = true;
        emit errorTextChanged();
    }
    emit failed(error, message);
}

void AudioCapture::clearError() {
    if (!hasError_) {
        return;
    }
    errorText_.clear();
    hasError_ = false;
    emit errorTextChanged();
}
