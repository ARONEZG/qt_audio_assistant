#pragma once

#include <QString>

// Assistant text is untrusted: a model can emit raw HTML, images that would pull
// remote content, or links to schemes other than the web. SafeMarkdown turns such
// text into a string that is safe to render as Markdown. Like the other domain
// values it is valid at construction and immutable afterwards.
//
// It builds on Qt's own Markdown support (no third-party library and no
// hand-written parser): the text is parsed with QTextDocument::MarkdownNoHTML, so
// inline HTML stays literal text instead of being interpreted, embedded images
// are dropped, and anchors whose scheme is neither http nor https are unwrapped
// to plain text. The document is then serialised back to Markdown and any
// remaining '<' is escaped, so a downstream Markdown renderer with HTML enabled
// (QML Text.MarkdownText uses the GitHub dialect) cannot re-interpret a tag.
class SafeMarkdown {
public:
    // Constructs the safe representation of untrusted Markdown source.
    explicit SafeMarkdown(const QString& untrusted);

    // The sanitised Markdown, ready to hand to a renderer (never HTML).
    [[nodiscard]] const QString& text() const noexcept;

private:
    QString text_;
};
