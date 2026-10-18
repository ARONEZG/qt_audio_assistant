#include "FakeTranscriber.h"

#include <QMetaObject>
#include <utility>

FakeTranscriber::FakeTranscriber(QObject* parent) : TranscriptionProvider(parent) {
}

FakeTranscriber::~FakeTranscriber() = default;

void FakeTranscriber::transcribe(std::vector<float> samples) {
    lastSamples_ = std::move(samples);
    ++callCount_;
    // Queued so the terminal signal is never emitted synchronously inside
    // transcribe() (the TranscriptionProvider contract); a pointer to the member
    // keeps the slot name compile-time checked.
    //
    // The analyzer's NewDeleteLeaks here is a false positive: Qt owns the
    // callable inside the posted QMetaCallEvent and frees it after delivery,
    // which the analyzer cannot see through the event queue.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    QMetaObject::invokeMethod(this, &FakeTranscriber::emitPendingResult, Qt::QueuedConnection);
}

void FakeTranscriber::succeedWith(QString text) {
    succeeds_ = true;
    text_ = std::move(text);
}

void FakeTranscriber::failWith(ProviderError error, QString message) {
    succeeds_ = false;
    error_ = error;
    message_ = std::move(message);
}

int FakeTranscriber::callCount() const noexcept {
    return callCount_;
}

const std::vector<float>& FakeTranscriber::lastSamples() const noexcept {
    return lastSamples_;
}

void FakeTranscriber::emitPendingResult() {
    if (succeeds_) {
        emit transcribed(text_);
    } else {
        emit failed(error_, message_);
    }
}
