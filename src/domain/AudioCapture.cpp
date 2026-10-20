#include "domain/AudioCapture.h"

#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <cstddef>
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

void AudioCapture::setCaptureOptions(int captureWindowSeconds, bool vadEnabled, double minSpeechSeconds) {
    captureWindowSeconds_ = captureWindowSeconds;
    vadEnabled_ = vadEnabled;
    minSpeechSeconds_ = minSpeechSeconds;
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
    // Never send more than the user's capture window. The ring buffer holds the
    // maximum supported window (see main()), so this only drops the part older
    // than the selected duration and keeps the newest samples. The settings file
    // is hand-editable and load() does not validate, so clamp before the
    // float -> size_t conversion (std::max also maps NaN to zero): a broken value
    // must not become a huge or undefined sample count.
    const auto kSampleRate = static_cast<double>(source_.format().sampleRate());
    const auto kWindowSamples =
            static_cast<std::size_t>(std::max(0.0, static_cast<double>(captureWindowSeconds_)) * kSampleRate);
    if (kWindowSamples < snapshot.size()) {
        snapshot.erase(snapshot.begin(), snapshot.end() - static_cast<std::ptrdiff_t>(kWindowSamples));
    }
    const qint64 kSnapshotMs = stageTimer.elapsed();
    if (snapshot.empty()) {
        qWarning() << "Transcribing an empty buffer: no audio captured yet (is background capture on?)";
    } else {
        const double kSeconds =
                static_cast<double>(snapshot.size()) / static_cast<double>(source_.format().sampleRate());
        qInfo() << "Transcribing" << snapshot.size() << "samples (" << kSeconds << "s)";
    }

    // When VAD is enabled and a gate is attached, send only the last detected
    // utterance (plus pre/post-roll). The gate classifies the same canonical
    // stream the buffer holds (freshly, from the snapshot), so its intervals map
    // 1:1 onto the snapshot. With VAD disabled, the (window-trimmed) snapshot is
    // sent as a whole.
    if (vadEnabled_ && vadGate_ != nullptr && !snapshot.empty()) {
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
    // lower the effective speech threshold and let short noise bursts through -
    // exactly the hallucination case this guard prevents. The threshold is the
    // user's minimum speech duration (default 0.75 s at 16 kHz = 12000 samples).
    // Clamped like the window above: a negative/NaN hand-edited value cannot
    // become a huge or undefined sample count.
    const auto kSampleRate = static_cast<double>(source_.format().sampleRate());
    const auto kMinSpeechSamples = static_cast<std::size_t>(std::max(0.0, minSpeechSeconds_) * kSampleRate);
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
