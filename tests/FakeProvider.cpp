#include "FakeProvider.h"

#include <utility>

FakeProvider::FakeProvider(QObject* parent)
    : FakeProvider(QStringLiteral("fake"), QStringLiteral("Fake Provider"), parent) {
}

FakeProvider::FakeProvider(QString id, QString displayName, QObject* parent)
    : Provider(parent), id_(std::move(id)), displayName_(std::move(displayName)) {
}

FakeProvider::~FakeProvider() = default;

QString FakeProvider::id() const {
    return id_;
}

QString FakeProvider::displayName() const {
    return displayName_;
}

Answer* FakeProvider::respond(const std::vector<Message>& history) {
    lastHistory_ = history;
    ++respondCount_;
    lastAnswer_ = new Answer(this);
    return lastAnswer_;
}

Answer* FakeProvider::lastAnswer() const noexcept {
    return lastAnswer_;
}

const std::vector<Message>& FakeProvider::lastHistory() const noexcept {
    return lastHistory_;
}

int FakeProvider::respondCount() const noexcept {
    return respondCount_;
}

void FakeProvider::destroyLastAnswer() {
    delete lastAnswer_;
    lastAnswer_ = nullptr;
}
