#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QUrl>
#include <QtTest>

#include "domain/SafeMarkdown.h"

namespace {

// Every '<' must be backslash-escaped (or preceded by an even run of literal
// backslashes). A surviving raw '<' is what a Markdown renderer with HTML enabled
// (QML Text.MarkdownText, GitHub dialect) would try to interpret as a tag.
[[nodiscard]] bool hasUnescapedAngleBracket(const QString& markdown) {
    int precedingBackslashes = 0;
    for (const QChar kCharacter : markdown) {
        if (kCharacter == QLatin1Char('<') && precedingBackslashes % 2 == 0) {
            return true;
        }
        precedingBackslashes = kCharacter == QLatin1Char('\\') ? precedingBackslashes + 1 : 0;
    }
    return false;
}

// Re-parses the sanitized output exactly as the QML renderer will (GitHub
// dialect), so the assertions observe what would actually be displayed rather
// than the intermediate Markdown string.
struct ReparseFacts {
    bool hasImage = false;
    bool hasNonWebAnchor = false;
    int webAnchors = 0;
};

// Records what a single fragment would render as. QTextCharFormat is an
// implicitly shared value, so it is passed by reference.
void accumulateFragmentFacts(const QTextCharFormat& format, ReparseFacts& facts) {
    if (format.isImageFormat()) {
        facts.hasImage = true;
    }
    if (format.isAnchor()) {
        const QString kScheme = QUrl(format.anchorHref()).scheme().toLower();
        if (kScheme == QStringLiteral("http") || kScheme == QStringLiteral("https")) {
            ++facts.webAnchors;
        } else {
            facts.hasNonWebAnchor = true;
        }
    }
}

[[nodiscard]] ReparseFacts reparseFacts(const QString& markdown) {
    QTextDocument document;
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    ReparseFacts facts;
    for (QTextBlock block = document.begin(); block != document.end(); block = block.next()) {
        for (auto iterator = block.begin(); iterator != block.end(); ++iterator) {
            accumulateFragmentFacts(iterator.fragment().charFormat(), facts);
        }
    }
    return facts;
}

}  // namespace

// SafeMarkdown is the security boundary for untrusted assistant text: HTML is
// not interpreted, embedded images are dropped and only web links survive. The
// tests observe the final Markdown string and re-parse it as the QML renderer
// would, so a regression cannot hide behind the intermediate representation.
class TestSafeMarkdown : public QObject {
    Q_OBJECT

public:
    explicit TestSafeMarkdown(QObject* parent = nullptr);

private slots:
    void test_emptyTextStaysEmpty();
    void test_plainTextIsPreserved();
    void test_markdownFormattingIsPreserved();
    void test_scriptTagBecomesLiteralText();
    void test_imageTagBecomesLiteralText();
    void test_markdownImageIsRemoved();
    void test_webLinkIsPreserved();
    void test_nonWebLinkIsUnwrapped_data();
    void test_nonWebLinkIsUnwrapped();
    void test_malformedMarkdownIsNeutralised();
    void test_everyAngleBracketIsEscaped_data();
    void test_everyAngleBracketIsEscaped();
    void test_sanitisedOutputReparsesSafely_data();
    void test_sanitisedOutputReparsesSafely();
};

TestSafeMarkdown::TestSafeMarkdown(QObject* parent) : QObject(parent) {
}

void TestSafeMarkdown::test_emptyTextStaysEmpty() {
    QCOMPARE(SafeMarkdown(QString()).text(), QString());
}

void TestSafeMarkdown::test_plainTextIsPreserved() {
    QCOMPARE(SafeMarkdown(QStringLiteral("hello world")).text().trimmed(), QStringLiteral("hello world"));
}

void TestSafeMarkdown::test_markdownFormattingIsPreserved() {
    const QString kOut = SafeMarkdown(QStringLiteral("## Heading\n\n**bold** and `code`")).text();

    QVERIFY(kOut.contains(QStringLiteral("## Heading")));
    QVERIFY(kOut.contains(QStringLiteral("**bold**")));
    QVERIFY(kOut.contains(QStringLiteral("`code`")));
}

void TestSafeMarkdown::test_scriptTagBecomesLiteralText() {
    const QString kOut = SafeMarkdown(QStringLiteral("<script>alert('x')</script>")).text();

    // The tag is shown, but as text: the renderer must not see an active tag.
    QVERIFY(kOut.contains(QStringLiteral("<script>")));
    QVERIFY(!hasUnescapedAngleBracket(kOut));
    QCOMPARE(reparseFacts(kOut).webAnchors, 0);
}

void TestSafeMarkdown::test_imageTagBecomesLiteralText() {
    const QString kOut = SafeMarkdown(QStringLiteral("<img src=\"https://evil.example/x.png\"> text")).text();

    QVERIFY(kOut.contains(QStringLiteral("<img")));
    QVERIFY(!hasUnescapedAngleBracket(kOut));
    QVERIFY(!reparseFacts(kOut).hasImage);
}

void TestSafeMarkdown::test_markdownImageIsRemoved() {
    const QString kOut = SafeMarkdown(QStringLiteral("before ![alt](https://evil.example/x.png) after")).text();

    QVERIFY(!kOut.contains(QStringLiteral("![")));
    QVERIFY(!reparseFacts(kOut).hasImage);
    QVERIFY(kOut.contains(QStringLiteral("before")));
    QVERIFY(kOut.contains(QStringLiteral("after")));
}

void TestSafeMarkdown::test_webLinkIsPreserved() {
    const QString kOut =
            SafeMarkdown(QStringLiteral("see [insecure](http://example.com/a) and [secure](https://example.com/b)"))
                    .text();

    QVERIFY(kOut.contains(QStringLiteral("[insecure](http://example.com/a)")));
    QVERIFY(kOut.contains(QStringLiteral("[secure](https://example.com/b)")));
    const ReparseFacts kFacts = reparseFacts(kOut);
    QCOMPARE(kFacts.webAnchors, 2);
    QVERIFY(!kFacts.hasNonWebAnchor);
}

void TestSafeMarkdown::test_nonWebLinkIsUnwrapped_data() {
    QTest::addColumn<QString>("markdown");

    QTest::newRow("javascript") << QStringLiteral("click [here](javascript:alert(1)) now");
    QTest::newRow("data") << QStringLiteral("click [here](data:text/html,<script>1</script>) now");
    QTest::newRow("mailto") << QStringLiteral("mail [me](mailto:someone@example.com) now");
    QTest::newRow("file") << QStringLiteral("open [file](file:///etc/passwd) now");
    QTest::newRow("relative") << QStringLiteral("go [there](/local/path) now");
    QTest::newRow("invalid-url") << QStringLiteral("go [there](http://[) now");
}

void TestSafeMarkdown::test_nonWebLinkIsUnwrapped() {
    QFETCH(QString, markdown);  // NOLINT(misc-const-correctness)

    const QString kOut = SafeMarkdown(markdown).text();

    // The link text survives, but it is no longer a link and no target remains.
    QVERIFY(!kOut.contains(QStringLiteral("](")));
    QVERIFY(kOut.contains(QStringLiteral("now")));
    const ReparseFacts kFacts = reparseFacts(kOut);
    QCOMPARE(kFacts.webAnchors, 0);
    QVERIFY(!kFacts.hasNonWebAnchor);
}

void TestSafeMarkdown::test_malformedMarkdownIsNeutralised() {
    const QString kOut = SafeMarkdown(QStringLiteral("**unclosed bold [broken(")).text();

    QVERIFY(!kOut.isEmpty());
    QVERIFY(!hasUnescapedAngleBracket(kOut));
}

void TestSafeMarkdown::test_everyAngleBracketIsEscaped_data() {
    QTest::addColumn<QString>("markdown");

    QTest::newRow("script") << QStringLiteral("<script>alert('x')</script>");
    QTest::newRow("img") << QStringLiteral("<img src=x onerror=alert(1)>");
    QTest::newRow("entity-open") << QStringLiteral("&lt;script&gt;alert(1)&lt;/script&gt;");
    QTest::newRow("entity-hex") << QStringLiteral("&#60;img src=x onerror=alert(1)&#62;");
    QTest::newRow("stray-close") << QStringLiteral("</script> tail");
    QTest::newRow("close-only") << QStringLiteral("a </b> b");
    QTest::newRow("svg") << QStringLiteral("<svg onload=alert(1)>");
}

void TestSafeMarkdown::test_everyAngleBracketIsEscaped() {
    QFETCH(QString, markdown);  // NOLINT(misc-const-correctness)

    QVERIFY(!hasUnescapedAngleBracket(SafeMarkdown(markdown).text()));
}

void TestSafeMarkdown::test_sanitisedOutputReparsesSafely_data() {
    QTest::addColumn<QString>("markdown");

    QTest::newRow("html") << QStringLiteral("<b>bold</b> and <script>x</script>");
    QTest::newRow("md-image") << QStringLiteral("![cat](https://example.com/cat.png)");
    QTest::newRow("data-image") << QStringLiteral("![x](data:image/png;base64,AAAA)");
    QTest::newRow("nested") << QStringLiteral("[![alt](https://img/e.png)](javascript:alert(1))");
    QTest::newRow("reference") << QStringLiteral("[ref][1]\n\n[1]: javascript:alert(1)");
    QTest::newRow("bad-autolink") << QStringLiteral("bad <javascript:alert(1)> autolink");
}

void TestSafeMarkdown::test_sanitisedOutputReparsesSafely() {
    QFETCH(QString, markdown);  // NOLINT(misc-const-correctness)

    const ReparseFacts kFacts = reparseFacts(SafeMarkdown(markdown).text());
    QVERIFY(!kFacts.hasImage);
    QVERIFY(!kFacts.hasNonWebAnchor);
}

QTEST_MAIN(TestSafeMarkdown)

#include "tst_safe_markdown.moc"
