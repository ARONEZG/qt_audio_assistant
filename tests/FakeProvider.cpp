#include "FakeProvider.h"

FakeProvider::FakeProvider(QObject* parent) : Provider(parent) {
}

FakeProvider::~FakeProvider() = default;

QString FakeProvider::id() const {
    return QStringLiteral("fake");
}

QString FakeProvider::displayName() const {
    return QStringLiteral("Fake Provider");
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
