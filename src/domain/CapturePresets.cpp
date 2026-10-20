#include "domain/CapturePresets.h"

#include <set>
#include <utility>

CapturePresets::CapturePresets(QObject* parent) : QAbstractListModel(parent) {
    // Three schemes that cover the common workflows: the balanced default, long
    // recordings up to the supported maximum, and short commands with a lower
    // speech-duration guard.
    presets_ = {
            {QStringLiteral("balanced"),
             QStringLiteral("Balanced"),
             CaptureConfig{20, QStringLiteral("openai/whisper-1"), QStringLiteral("ru"), {}, true, 0.75}},
            {QStringLiteral("long"),
             QStringLiteral("Long recordings"),
             CaptureConfig{120, QStringLiteral("openai/whisper-1"), QStringLiteral("ru"), {}, true, 0.75}},
            {QStringLiteral("quick"),
             QStringLiteral("Quick commands"),
             CaptureConfig{10, QStringLiteral("openai/whisper-1"), QStringLiteral("ru"), {}, true, 0.25}},
    };

    // A duplicate id would make a by-id lookup ambiguous, so the offered
    // identities are a set by construction. Duplicates are a composition-root
    // mistake, checked in debug builds only. Built unconditionally: a handful of
    // presets makes the check cheaper than the branch around it.
    std::set<QString> offeredIds;
    for (const Preset& preset : presets_) {
        offeredIds.insert(preset.id);
    }
    Q_ASSERT(offeredIds.size() == presets_.size());
}

CapturePresets::~CapturePresets() = default;

int CapturePresets::rowCount(const QModelIndex& parent) const {
    // A list model has no children, so rows exist only under the root index.
    if (parent.isValid()) {
        return 0;
    }
    return count();
}

QVariant CapturePresets::data(const QModelIndex& index, int role) const {
    const Preset* preset = presetAt(index.row());
    if (preset == nullptr) {
        return {};
    }
    if (role == kIdRole) {
        return preset->id;
    }
    if (role == kDisplayNameRole) {
        return preset->displayName;
    }
    return {};
}

QHash<int, QByteArray> CapturePresets::roleNames() const {
    // roleNames() is a Qt model API whose return type is mandated as QHash. This
    // is the unavoidable-container boundary from src/AGENTS.md: the QHash is
    // built here, never stored in a field and never returned by another API.
    QHash<int, QByteArray> roles;
    roles.insert(kIdRole, QByteArrayLiteral("id"));
    roles.insert(kDisplayNameRole, QByteArrayLiteral("displayName"));
    return roles;
}

int CapturePresets::count() const noexcept {
    return static_cast<int>(presets_.size());
}

QString CapturePresets::idAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? QString() : preset->id;
}

QString CapturePresets::displayNameAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? QString() : preset->displayName;
}

int CapturePresets::captureDurationSecondsAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? 0 : preset->config.captureDurationSeconds;
}

QString CapturePresets::sttModelAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? QString() : preset->config.sttModel;
}

QString CapturePresets::sttLanguageAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? QString() : preset->config.sttLanguage;
}

QString CapturePresets::sttPromptAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? QString() : preset->config.sttPrompt;
}

bool CapturePresets::vadEnabledAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset != nullptr && preset->config.vadEnabled;
}

double CapturePresets::minSpeechSecondsAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? 0.0 : preset->config.minSpeechSeconds;
}

CaptureConfig CapturePresets::configAt(int index) const {
    const Preset* preset = presetAt(index);
    return preset == nullptr ? CaptureConfig{} : preset->config;
}

bool CapturePresets::isValidRow(int index) const noexcept {
    return index >= 0 && index < count();
}

const CapturePresets::Preset* CapturePresets::presetAt(int index) const {
    if (!isValidRow(index)) {
        return nullptr;
    }
    return &presets_[static_cast<std::size_t>(index)];
}
