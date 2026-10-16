#pragma once

#include <QObject>
#include <QString>

// Экранный статус приложения, доступный QML. Пока строка; расширение до
// перечисления статусов и привязки к внутренним состояниям — отдельный инкремент.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status WRITE setStatus NOTIFY statusChanged)

public:
    explicit AppController(QObject* parent = nullptr);

    // Возвращает текущее значение статуса.
    [[nodiscard]] QString status() const;

    // Заменяет значение статуса и эмитит statusChanged, только если строка
    // действительно изменилась. Пустая строка допустима: приложение обязано
    // отображать статус дословно, поэтому пустое значение не заменяется
    // значением по умолчанию.
    void setStatus(QString status);

signals:
    void statusChanged(const QString& status);

private:
    QString status_;
};
