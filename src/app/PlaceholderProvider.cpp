#include "app/PlaceholderProvider.h"

#include <QTimer>
#include <iterator>

PlaceholderProvider::PlaceholderProvider(QObject* parent) : Provider(parent) {
}

PlaceholderProvider::~PlaceholderProvider() = default;

QString PlaceholderProvider::id() const {
    return QStringLiteral("placeholder");
}

QString PlaceholderProvider::displayName() const {
    return QStringLiteral("Placeholder (offline)");
}

QString PlaceholderProvider::replyTo(const std::vector<Message>& history) {
    if (history.empty()) {
        return QStringLiteral("(no message to echo)");
    }
    return QStringLiteral("Echo: %1").arg(history.back().text());
}

Answer* PlaceholderProvider::respond(const std::vector<Message>& history) {
    auto* answer = new Answer(this);
    const QString kReply = replyTo(history);
    // Feed the answer from the event loop. The Answer is the context object, so
    // the callback is dropped (and never touches a destroyed Answer) if the chat
    // is closed or the answer is cancelled before it fires.
    QTimer::singleShot(0, answer, [answer, kReply] {
        answer->appendChunk(kReply);
        answer->complete(Message(MessageRole::kAssistant, kReply));
    });
    return answer;
}
