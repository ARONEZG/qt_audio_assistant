#include "AppController.h"

#include <string_view>
#include <utility>

namespace {
// Честный статус текущей стадии проекта: GUI-каркас собран, функции транскрибации
// и LLM ещё не подключены. Значение является частью видимого контракта приложения.
constexpr std::string_view kInitialStatus = "Каркас приложения собран. Функции пока не реализованы.";
}  // namespace

AppController::AppController(QObject* parent) : QObject(parent) {
    // fromUtf8(): исходная строка хранится как UTF-8 string_view (не содержит
    // NUL), конвертация выполняется явно в месте владения, а не в привязке.
    status_ = QString::fromUtf8(kInitialStatus);
}

QString AppController::status() const {
    return status_;
}

void AppController::setStatus(QString status) {
    if (status == status_) {
        return;
    }
    status_ = std::move(status);
    emit statusChanged(status_);
}
