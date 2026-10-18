#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "domain/ProviderError.h"
#include "domain/TranscriptionProvider.h"

// Double for the TranscriptionProvider role: records the samples it was handed
// and emits exactly one terminal signal, always from the event loop (never
// synchronously inside transcribe()), mirroring the real network adapter.
class FakeTranscriber final : public TranscriptionProvider {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(FakeTranscriber)

    // Ordered widest-first so the class carries no internal padding.
    std::vector<float> lastSamples_;
    QString text_;
    QString message_;
    int callCount_ = 0;
    ProviderError error_ = ProviderError::kUnknown;
    bool succeeds_ = true;

public:
    explicit FakeTranscriber(QObject* parent = nullptr);
    ~FakeTranscriber() override;

    void transcribe(std::vector<float> samples) override;

    // Configures the outcome the queued emission reports.
    void succeedWith(QString text);
    void failWith(ProviderError error, QString message);

    [[nodiscard]] int callCount() const noexcept;
    [[nodiscard]] const std::vector<float>& lastSamples() const noexcept;

private slots:
    // Emits the configured terminal signal from the event loop, so transcribe()
    // never emits synchronously (the TranscriptionProvider contract).
    void emitPendingResult();
};
