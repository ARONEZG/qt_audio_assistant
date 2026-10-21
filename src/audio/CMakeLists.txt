# ── Audio library ───────────────────────────────────────────
# The audio subsystem. It holds three things:
#   * the framework-agnostic capture contracts (AudioFormat, AudioSource,
#     AudioSink) and the SPSC ring buffer (AudioBuffer) that implements the
#     AudioSink port; they depend only on the STL, so a consumer or a test can
#     use them without pulling Qt or any platform code in;
#   * the miniaudio implementation translation unit, so the third-party
#     amalgamation is compiled once and the platform-specific capture code has
#     a single home behind a narrow interface;
#   * the concrete MiniaudioCaptureSource: the microphone AudioSource that
#     resamples the device stream to the canonical format and forwards it into
#     an injected AudioSink. miniaudio.h stays confined to its .cpp (pimpl), so
#     the public audio headers remain free of third-party includes.
add_library(audio-assistant-audio STATIC
    AudioBuffer.cpp
    AudioFormat.cpp
    AudioSink.cpp
    AudioSource.cpp
    MiniaudioCaptureSource.cpp
    MiniaudioImplementation.cpp
    SileroVoiceActivityDetector.cpp
    SpeechSegmenter.cpp
    VadSpeechGate.cpp
    VoiceActivityDetector.cpp
    WavEncoder.cpp
)

set_target_properties(audio-assistant-audio PROPERTIES
    CXX_EXTENSIONS OFF
)

# Consumers include audio headers as "audio/<Header>.h".
target_include_directories(audio-assistant-audio PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

# audio-assistant-qt-warnings is deliberately not linked here: the audio
# contracts are STL-only and must not inherit Qt/moc-specific suppressions that
# could hide real diagnostics in future capture code. miniaudio.h stays SYSTEM.
# onnxruntime is linked PRIVATE: the Silero VAD adapter holds every ONNX type
# behind a pimpl, so no onnxruntime header ever reaches the public audio
# headers; the static library's private dependencies are still carried to the
# final link. Qt6::Core is PRIVATE for the same reason — only the Silero VAD
# adapter's .cpp reads the model through QFile/QResource (the qrc resource is
# built into the library); the public audio headers stay Qt-free. The strict
# profile is linked PRIVATE on purpose: every project-owned TU must see it, and
# consumers compile against the public headers with their own flags.
target_link_libraries(audio-assistant-audio
    PRIVATE audio-assistant-strict miniaudio::miniaudio onnxruntime::onnxruntime Qt6::Core
)
