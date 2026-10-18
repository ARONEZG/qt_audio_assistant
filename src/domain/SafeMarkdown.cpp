#include "domain/SafeMarkdown.h"

#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <algorithm>
#include <vector>

namespace {

// Only the two web schemes a chat answer may legitimately link to. Everything
// else (javascript:, data:, mailto:, file:, a relative path, an invalid URL) is
// not a link and is unwrapped to plain text.
[[nodiscard]] bool isWebLink(const QString& href) {
    const QUrl kUrl(href);
    if (!kUrl.isValid() || kUrl.isRelative()) {
        return false;
    }
    const QString kScheme = kUrl.scheme().toLower();
    return kScheme == QStringLiteral("http") || kScheme == QStringLiteral("https");
}

// A fragment of the parsed document that must be neutralised: an image (dropped
// entirely) or a link whose target is not a web link (unwrapped to plain text).
struct Neutralization {
    int position;
    int length;
    bool isImage;
};

// Records the neutralisation each fragment of one block needs, if any.
void collectBlockNeutralizations(const QTextBlock& block, std::vector<Neutralization>& edits) {
    for (auto iterator = block.begin(); iterator != block.end(); ++iterator) {
        const QTextFragment kFragment = iterator.fragment();
        const QTextCharFormat kFormat = kFragment.charFormat();
        if (kFormat.isImageFormat()) {
            edits.push_back(Neutralization{kFragment.position(), kFragment.length(), true});
        } else if (kFormat.isAnchor() && !isWebLink(kFormat.anchorHref())) {
            edits.push_back(Neutralization{kFragment.position(), kFragment.length(), false});
        }
    }
}

[[nodiscard]] std::vector<Neutralization> collectNeutralizations(const QTextDocument& document) {
    std::vector<Neutralization> edits;
    for (QTextBlock block = document.begin(); block != document.end(); block = block.next()) {
        collectBlockNeutralizations(block, edits);
    }
    return edits;
}

// Applies the edits back-to-front so every earlier position stays valid: they are
// collected in document order and reversed before they are applied in place.
void applyNeutralizations(QTextDocument& document, std::vector<Neutralization>& edits) {
    std::reverse(edits.begin(), edits.end());
    for (const Neutralization& kEdit : edits) {
        QTextCursor cursor(&document);
        cursor.setPosition(kEdit.position);
        cursor.setPosition(kEdit.position + kEdit.length, QTextCursor::KeepAnchor);
        if (kEdit.isImage) {
            // Removing the image character drops the remote URL with it.
            cursor.removeSelectedText();
        } else {
            QTextCharFormat plain;
            plain.setAnchor(false);
            cursor.setCharFormat(plain);
        }
    }
}

// Escapes every '<' that is not already escaped. A run of backslashes is counted
// so that an odd run (an existing escape) is left untouched while an even run
// (literal backslashes) still gets its '<' escaped.
[[nodiscard]] QString escapeAngleBrackets(const QString& markdown) {
    QString escaped;
    escaped.reserve(markdown.size());
    bool precededByBackslash = false;
    for (const QChar kCharacter : markdown) {
        if (kCharacter == QLatin1Char('<') && !precededByBackslash) {
            escaped += QLatin1Char('\\');
        }
        escaped += kCharacter;
        precededByBackslash = kCharacter == QLatin1Char('\\') ? !precededByBackslash : false;
    }
    return escaped;
}

}  // namespace

SafeMarkdown::SafeMarkdown(const QString& untrusted) {
    QTextDocument document;
    document.setMarkdown(untrusted, QTextDocument::MarkdownNoHTML);
    std::vector<Neutralization> edits = collectNeutralizations(document);
    applyNeutralizations(document, edits);
    text_ = escapeAngleBrackets(document.toMarkdown());
}

const QString& SafeMarkdown::text() const noexcept {
    return text_;
}
