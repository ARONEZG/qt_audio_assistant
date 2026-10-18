#include "domain/ProviderConfigSource.h"

#include <utility>

namespace {

constexpr auto kBaseUrlVariable = QLatin1StringView("POLZA_BASE_URL");
constexpr auto kModelVariable = QLatin1StringView("POLZA_MODEL");
constexpr auto kApiKeyVariable = QLatin1StringView("POLZA_API_KEY");

// A blank environment variable is treated as unset so that it cannot silently
// erase a value the user saved in the configuration file.
[[nodiscard]] QString valueOrEmpty(const QProcessEnvironment& environment, QLatin1StringView name) {
    return environment.value(name).trimmed();
}

}  // namespace

ProviderConfigSource::ProviderConfigSource(ProviderConfigFile file, const QProcessEnvironment& environment)
    : file_(std::move(file)), environment_(environment) {
}

ProviderConfigSource::Result ProviderConfigSource::resolve() const {
    const ProviderConfigFile::LoadResult kLoaded = file_.load();

    Result result{kLoaded.config, valueOrEmpty(environment_, kApiKeyVariable), kLoaded.error};

    const QString kEnvironmentUrl = valueOrEmpty(environment_, kBaseUrlVariable);
    if (!kEnvironmentUrl.isEmpty()) {
        result.config.baseUrl = kEnvironmentUrl;
    }
    const QString kEnvironmentModel = valueOrEmpty(environment_, kModelVariable);
    if (!kEnvironmentModel.isEmpty()) {
        result.config.model = kEnvironmentModel;
    }
    return result;
}
