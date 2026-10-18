#pragma once

#include <QObject>
#include <QString>

#include "audio/AudioFormat.h"
#include "domain/ProviderError.h"

namespace audio {
class AudioBuffer;
class AudioSource;
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
// A transcribe request hands the transcriber an independent copy of the buffer's
// current contents; the facade holds no audio of its own.
class AudioCapture : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AudioCapture)

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

    // Starts/stops capture; a thin command over the injected source.
    void start();
    void stop();

    // Sends a snapshot of the whole buffer to the transcriber. The terminal
    // result arrives later through transcribed()/failed() (the
    // TranscriptionProvider contract: transcribe() never emits synchronously).
    void transcribeLast();

    // The stream format of the injected source; a pure query.
    [[nodiscard]] audio::AudioFormat format() const noexcept;

signals:
    // The transcriber's terminal signals, re-emitted unchanged.
    void transcribed(const QString& text);
    void failed(ProviderError error, const QString& message);

private:
    audio::AudioBuffer& buffer_;
    audio::AudioSource& source_;
    TranscriptionProvider& transcriber_;
};
