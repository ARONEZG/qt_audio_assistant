#include "audio/SileroVoiceActivityDetector.h"

#include <onnxruntime_cxx_api.h>

#include <QByteArray>
#include <QFile>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace audio {

namespace {

// The canonical capture stream (see AudioSink.h / AudioFormat.h): 16 kHz,
// mono, f32. The model processes 32 ms frames with a 64-sample context tail
// from the previous frame (see docs/ADR-vad.md for the model card).
constexpr std::size_t kFrameSize = 512U;
constexpr std::size_t kContextSize = 64U;
constexpr std::size_t kInputSize = kFrameSize + kContextSize;   // 576
constexpr std::size_t kStateSize = std::size_t{2} * 1U * 128U;  // 256
constexpr std::int64_t kSampleRate = 16000;

// The embedded model asset (the audio-assistant-models resource library,
// resource path ":/models/silero_vad/silero_vad.onnx"). The model is a
// data-asset of the repository with a pinned SHA-256 (see docs/ADR-vad.md);
// it is never downloaded at runtime.
constexpr const char* kModelResource = ":/models/silero_vad/silero_vad.onnx";

// The ONNX input/output node names of the Silero VAD model (v6.2.1).
constexpr const char* kInputName = "input";
constexpr const char* kStateName = "state";
constexpr const char* kSrName = "sr";
constexpr const char* kOutputName = "output";
constexpr const char* kStateOutputName = "stateN";

constexpr std::array<std::int64_t, 3> kStateNodeDims{2, 1, 128};
constexpr std::array<std::int64_t, 1> kSrNodeDims{1};

// Reads the whole model file into memory (Qt resource path or plain path).
[[nodiscard]] std::vector<std::uint8_t> readModelBytes(const char* modelPath) {
    QFile file(QString::fromUtf8(modelPath));
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(std::string("SileroVoiceActivityDetector: cannot open model: ") + modelPath);
    }
    const QByteArray kBytes = file.readAll();
    if (kBytes.isEmpty()) {
        throw std::runtime_error(std::string("SileroVoiceActivityDetector: model file is empty: ") + modelPath);
    }
    return {kBytes.cbegin(), kBytes.cend()};
}

// Configures the session options and creates the session. The options must be
// fully configured before the session is constructed, so this runs as a factory
// in the pimpl's initializer list, not in the constructor body.
[[nodiscard]] Ort::Session makeSession(Ort::Env& env,
                                       const std::vector<std::uint8_t>& modelBytes,
                                       Ort::SessionOptions& options) {
    options.SetIntraOpNumThreads(1);
    options.SetInterOpNumThreads(1);
    options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    // The CPU provider is the default; no other execution provider is
    // registered, so the session runs on the CPU everywhere.
    // NOLINTBEGIN(modernize-return-braced-init-list): Ort::Session is an opaque
    // RAII wrapper without a public value-initializable form; the eager ctor
    // call is the only way to construct it.
    return Ort::Session(env, modelBytes.data(), modelBytes.size(), options);
    // NOLINTEND(modernize-return-braced-init-list)
}

}  // namespace

struct SileroVoiceActivityDetector::Impl {
    Impl(const char* modelPath)
        : modelBytes_(readModelBytes(modelPath)),
          session_(makeSession(env_, modelBytes_, sessionOptions_)),
          memoryInfo_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU)) {
        inputNodeDims_.at(0) = 1;
        inputNodeDims_.at(1) = static_cast<std::int64_t>(kInputSize);
        sr_.at(0) = kSampleRate;
    }

    // Runs the model on one 512-sample frame (with the context tail prepended)
    // and returns the speech probability.
    float run(std::span<const float> frame) {
        // Copy the frame after the 64-sample context tail already in place.
        for (std::size_t i = 0; i < kFrameSize; ++i) {
            input_.at(i + kContextSize) = frame[i];
        }

        Ort::Value inputTensor =
                Ort::Value::CreateTensor<float>(memoryInfo_, input_.data(), input_.size(), inputNodeDims_.data(), 2);
        Ort::Value stateTensor =
                Ort::Value::CreateTensor<float>(memoryInfo_, state_.data(), state_.size(), kStateNodeDims.data(), 3);
        Ort::Value srTensor =
                Ort::Value::CreateTensor<std::int64_t>(memoryInfo_, sr_.data(), sr_.size(), kSrNodeDims.data(), 1);

        std::array<Ort::Value, 3> inputs{std::move(inputTensor), std::move(stateTensor), std::move(srTensor)};
        const std::array<const char*, 3> kInputNames{kInputName, kStateName, kSrName};
        const std::array<const char*, 2> kOutputNames{kOutputName, kStateOutputName};

        std::vector<Ort::Value> outputs = session_.Run(Ort::RunOptions{nullptr},
                                                       kInputNames.data(),
                                                       inputs.data(),
                                                       inputs.size(),
                                                       kOutputNames.data(),
                                                       kOutputNames.size());

        const float kProbability = outputs.at(0).GetTensorMutableData<float>()[0];
        const std::size_t kStateBytes = kStateSize * sizeof(float);
        std::memcpy(state_.data(), outputs.at(1).GetTensorMutableData<float>(), kStateBytes);

        // Keep the 64-sample tail of this frame for the next call: the tail is
        // the last kContextSize samples of the 576-wide input, which is exactly
        // the last kContextSize samples of the current frame.
        for (std::size_t i = 0; i < kContextSize; ++i) {
            input_.at(i) = input_.at(i + kFrameSize);
        }

        return kProbability;
    }

    void resetState() {
        std::fill(state_.begin(), state_.end(), 0.0F);
        std::fill(input_.begin(), input_.end(), 0.0F);
    }

private:
    friend class SileroVoiceActivityDetector;

    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "silero-vad"};
    std::vector<std::uint8_t> modelBytes_;
    Ort::SessionOptions sessionOptions_;
    Ort::Session session_;
    Ort::MemoryInfo memoryInfo_;
    std::array<float, kInputSize> input_{};
    std::array<float, kStateSize> state_{};
    std::array<std::int64_t, 2> inputNodeDims_{};
    std::array<std::int64_t, 1> sr_{};
};

SileroVoiceActivityDetector::SileroVoiceActivityDetector() : impl_(std::make_unique<Impl>(kModelResource)) {
}

SileroVoiceActivityDetector::SileroVoiceActivityDetector(const char* modelPath)
    : impl_(std::make_unique<Impl>(modelPath)) {
}

SileroVoiceActivityDetector::~SileroVoiceActivityDetector() = default;

float SileroVoiceActivityDetector::process(std::span<const float> frame) {
    if (frame.size() != kFrameSize) {
        throw std::invalid_argument("SileroVoiceActivityDetector: expected a 512-sample frame, got " +
                                    std::to_string(frame.size()));
    }
    return impl_->run(frame);
}

void SileroVoiceActivityDetector::reset() {
    impl_->resetState();
}

}  // namespace audio
