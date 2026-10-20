#include "domain/CaptureConfigSource.h"

#include <utility>

namespace {

constexpr auto kModelVariable = QLatin1StringView("POLZA_STT_MODEL");
constexpr auto kLanguageVariable = QLatin1StringView("POLZA_STT_LANGUAGE");
constexpr auto kPromptVariable = QLatin1StringView("POLZA_STT_PROMPT");

}  // namespace

CaptureConfigSource::CaptureConfigSource(CaptureConfigFile file, const QProcessEnvironment& environment)
    : file_(std::move(file)), environment_(environment) {
}

CaptureConfigSource::Result CaptureConfigSource::resolve() const {
    const CaptureConfigFile::LoadResult kLoaded = file_.load();

    // The file is the base configuration; an environment variable overrides a
    // value only when it is explicitly present. This keeps the user's saved
    // language/prompt across restarts while still letting an operator override
    // them for a single run.
    Result result{kLoaded.config, kLoaded.error};

    // A blank model variable is treated as unset so it cannot silently erase a
    // model the user saved in the configuration file.
    const QString kEnvironmentModel = environment_.value(kModelVariable).trimmed();
    if (!kEnvironmentModel.isEmpty()) {
        result.config.sttModel = kEnvironmentModel;
    }

    // An explicitly present language wins: empty means endpoint autodetection,
    // a value means that language. An absent variable keeps the file value
    // (which defaults to "ru").
    if (environment_.contains(kLanguageVariable)) {
        result.config.sttLanguage = environment_.value(kLanguageVariable);
    }

    // An absent or blank prompt keeps the file value; a non-empty one overrides
    // it (trimmed, because surrounding whitespace is only request noise).
    const QString kEnvironmentPrompt = environment_.value(kPromptVariable).trimmed();
    if (!kEnvironmentPrompt.isEmpty()) {
        result.config.sttPrompt = kEnvironmentPrompt;
    }
    return result;
}
