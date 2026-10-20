#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <vector>

#include "audio/AudioFormat.h"
#include "domain/ProviderError.h"

namespace audio {
class AudioBuffer;
class AudioSource;
class VadSpeechGate;
}  // namespace audio

class TranscriptionProvider;

// The one seam that turns the three capture-pipeline parts into a pipeline:
//
//     AudioSource (microphone)  ->  AudioBuffer (last N seconds)  ->  TranscriptionProvider
//
// It is a facade, not a controller: it owns nothing. The composition root
// (main()) owns the concrete buffer, source and transcriber and keeps them alive
// for the whole event loop; this object borrows references to them, exactly the
// way Providers/NewChat borrow the objects they operate on. Deciding *which*
// source, buffer and transcriber are wired belongs to the composition root; that
// is why they are constructor parameters rather than looked up or created here.
//
// It is also the QML view model of the capture controls: the chat drives
// start()/stop() (through the composition root, which maps the chat's switch to
// these commands) and the "transcribe" button calls transcribeLast() directly;
// the terminal result reaches the view through transcribed()/failed() and the
// hasError()/errorText() properties, mirroring how Chat surfaces a failed reply.
//
// Its whole contract is delegation plus relay:
//
//   start()/stop()       -> source.start()/stop()
//   transcribeLast()     -> transcriber.transcribe(buffer.snapshot())
//   format()             -> source.format()
//   transcribed/failed   -> the transcriber's own terminal signals, re-emitted
//
// The commands return void and report nothing: the AudioSource contract has no
// status, so the composition root reads capture health from the concrete source
// it owns (MiniaudioCaptureSource::lastError()) rather than through this facade.
// Keeping the status out here is what avoids widening the AudioSource contract.
//
// Configuration of the transcriber (endpoint, key, model) is fixed when the
// composition root builds it and is *not* re-read per request: the facade holds
// the injected provider by reference and never rebuilds it.
//
// A transcribe request hands the transcriber an independent copy of the buffer's
// current contents; the facade holds no audio of its own. An empty buffer is
// still sent as an empty block: whether an empty request is meaningful (a real
// backend may reject it) is the transcriber's decision, not the facade's.
class AudioCapture : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AudioCapture)
    QML_ELEMENT
    QML_UNCREATABLE("Injected by the composition root")

    // Last failed transcribe request, presented to the user. Transient: a new
    // transcribeLast() clears it, like Chat clears its error on a new send().
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorTextChanged)

public:
    // The buffer, source and transcriber are borrowed and must outlive this
    // object (the AudioBuffer/Source lifetime rules). The transcriber's terminal
    // signals are connected to this object's in the constructor, so the relay
    // exists from the moment the facade does.
    AudioCapture(audio::AudioBuffer& buffer,
                 audio::AudioSource& source,
                 TranscriptionProvider& transcriber,
                 QObject* parent = nullptr);
    ~AudioCapture() override;

    // Attaches the voice-activity gate that selects the last utterance before
    // transcribing. The gate is optional: without it the whole buffer is sent
    // (the historical behaviour). When present, transcribeLast() sends only the
    // last detected speech interval, extended by a short pre-roll/post-roll so
    // word boundaries are not clipped, instead of the whole capture window. The
    // gate is borrowed and must outlive this object.
    void setVadGate(audio::VadSpeechGate& gate);

    // Starts/stops capture; a thin command over the injected source. Idempotent
    // at the source (see the AudioSource contract).
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    // Sends a snapshot of the buffer to the transcriber. The terminal result
    // arrives later through transcribed()/failed() (the TranscriptionProvider
    // contract: transcribe() never emits synchronously). Without a VAD gate an
    // empty buffer is sent as an empty block; with a gate, no detected speech
    // (or a fragment below the minimum duration) is reported locally through
    // failed() and no network request is made. Starts a new request, so a
    // previous failure is cleared.
    Q_INVOKABLE void transcribeLast();

    // The stream format of the injected source; a pure query.
    [[nodiscard]] audio::AudioFormat format() const noexcept;

    [[nodiscard]] QString errorText() const;
    [[nodiscard]] bool hasError() const noexcept;

signals:
    // The transcriber's terminal signals, re-emitted unchanged.
    void transcribed(const QString& text);
    void failed(ProviderError error, const QString& message);
    void errorTextChanged();

private:
    void onTranscribed(const QString& text);
    void onFailed(ProviderError error, const QString& message);
    void clearError();

    // Replaces `snapshot` with the last detected utterance plus its pre/post
    // roll and enforces the minimum transcribable duration. Returns false after
    // reporting a local failure (no speech / too short) when the caller must not
    // call the transcriber. The gate must have been attached and the snapshot
    // must be non-empty.
    [[nodiscard]] bool selectLastUtterance(audio::VadSpeechGate& gate, std::vector<float>& snapshot);

    audio::AudioBuffer& buffer_;
    audio::AudioSource& source_;
    TranscriptionProvider& transcriber_;
    audio::VadSpeechGate* vadGate_ = nullptr;
    QString errorText_;
    bool hasError_ = false;
};
