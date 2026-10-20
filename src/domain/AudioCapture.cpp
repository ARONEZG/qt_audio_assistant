#include "domain/AudioCapture.h"

#include <QDebug>
#include <QElapsedTimer>
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
    // buffer.
    clearError();

    // Per-stage timing makes the latency budget visible in the log: the
    // snapshot copy and the VAD pass are the local cost, the transcriber logs
    // its own encode/round-trip cost.
    QElapsedTimer stageTimer;
    stageTimer.start();
    std::vector<float> snapshot = buffer_.snapshot();
    const qint64 kSnapshotMs = stageTimer.elapsed();
    if (snapshot.empty()) {
        qWarning() << "Transcribing an empty buffer: no audio captured yet (is background capture on?)";
    } else {
        const double kSeconds =
                static_cast<double>(snapshot.size()) / static_cast<double>(source_.format().sampleRate());
        qInfo() << "Transcribing" << snapshot.size() << "samples (" << kSeconds << "s)";
    }

    // When a VAD gate is attached, send only the last detected utterance
    // (plus pre/post-roll). The gate classifies the same canonical stream the
    // buffer holds (freshly, from the snapshot), so its intervals map 1:1 onto
    // the snapshot.
    if (vadGate_ != nullptr && !snapshot.empty()) {
        if (!selectLastUtterance(*vadGate_, snapshot, kSnapshotMs)) {
            return;
        }
    } else {
        qInfo() << "Transcription timing: snapshotMs=" << kSnapshotMs << "vadMs=0";
    }

    transcriber_.transcribe(std::move(snapshot));
}

bool AudioCapture::selectLastUtterance(audio::VadSpeechGate& gate, std::vector<float>& snapshot, qint64 snapshotMs) {
    const std::size_t kCapturedSamples = snapshot.size();
    QElapsedTimer stageTimer;
    stageTimer.start();
    snapshot = gate.lastSpeechBlock(std::span<const float>(snapshot));
    const qint64 kVadMs = stageTimer.elapsed();
    const std::size_t kSpeechSamples = gate.lastSpeechSamples();
    qInfo() << "Transcription timing: snapshotMs=" << snapshotMs << "vadMs=" << kVadMs << "kept" << snapshot.size()
            << "of" << kCapturedSamples << "samples (speech" << kSpeechSamples << ")";

    // No speech means there is nothing to recognise. Report it locally instead
    // of sending an empty block to the network: the user gets an honest reason
    // and no pointless round-trip.
    if (snapshot.empty()) {
        qWarning() << "No speech detected in the captured audio: skipping transcription";
        onFailed(ProviderError::kInvalidResponse, QStringLiteral("No speech detected in the captured audio"));
        return false;
    }

    // Guard: apply the minimum to the detected speech, not to the padded block.
    // The pre/post-roll is 500 ms of silence, so measuring the padded size would
    // lower the effective speech threshold to ~250 ms and let short noise
    // bursts through - exactly the hallucination case this guard prevents.
    // 0.75 s at 16 kHz = 12000 samples of actual speech.
    constexpr std::size_t kMinSpeechSamples = 12000;
    if (kSpeechSamples < kMinSpeechSamples) {
        qWarning() << "Speech fragment too short (" << kSpeechSamples << " speech samples, < " << kMinSpeechSamples
                   << "): skipping transcription";
        onFailed(ProviderError::kInvalidResponse,
                 QStringLiteral("Speech fragment too short — too brief to transcribe reliably"));
        return false;
    }
    return true;
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
