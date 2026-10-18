// Smoke test for the sqlite3 dependency wiring.
//
// What it proves and why it is not persistence logic:
//  * <sqlite3.h> is included through the SYSTEM include directory configured in
//    CMakeLists.txt, so the strict -Weverything -Werror profile still applies to
//    this project-owned test while diagnostics inside the third-party header stay
//    suppressed. A successful build therefore proves the compile wiring.
//  * sqlite3_open/sqlite3_exec/sqlite3_libversion resolve from the Conan package,
//    so a successful link proves the SQLite::SQLite3 wiring, including the system
//    libraries the recipe declares (pthread/dl/m on Linux).
//  * The test only uses an in-memory database: no file is created and no
//    persistence layer (schema, chats, messages) is exercised.
#include <sqlite3.h>

#include <QString>
#include <QtTest>

namespace {

// The pinned sqlite3 version (see `requires` in conanfile.py). Keeping the
// expectation here means a silently resolved different version fails the test
// instead of going unnoticed.
constexpr const char* kExpectedVersion = "3.54.0";

}  // namespace

class TestSqliteSmoke : public QObject {
    Q_OBJECT

public:
    explicit TestSqliteSmoke(QObject* parent = nullptr);

private slots:
    void test_compileTimeVersionMatchesPinnedDependency();
    void test_libraryIsLinkedAndReportsVersion();
    void test_inMemoryDatabaseExecutesAndReadsBack();
};

TestSqliteSmoke::TestSqliteSmoke(QObject* parent) : QObject(parent) {
}

void TestSqliteSmoke::test_compileTimeVersionMatchesPinnedDependency() {
    QCOMPARE(QString::fromLatin1(SQLITE_VERSION), QString::fromLatin1(kExpectedVersion));
}

void TestSqliteSmoke::test_libraryIsLinkedAndReportsVersion() {
    // sqlite3_libversion() is a symbol in the linked library, not a macro: a
    // mismatched header/library pair (or a missing link) fails here.
    QCOMPARE(QString::fromLatin1(sqlite3_libversion()), QString::fromLatin1(kExpectedVersion));
    QCOMPARE(sqlite3_libversion_number(), SQLITE_VERSION_NUMBER);
}

void TestSqliteSmoke::test_inMemoryDatabaseExecutesAndReadsBack() {
    sqlite3* database = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &database), SQLITE_OK);
    QVERIFY(database != nullptr);

    const auto kExecute = [database](const char* statement) {
        return sqlite3_exec(database, statement, nullptr, nullptr, nullptr);
    };
    QCOMPARE(kExecute("CREATE TABLE note(value TEXT);"), SQLITE_OK);
    QCOMPARE(kExecute("INSERT INTO note(value) VALUES ('audio-assistant');"), SQLITE_OK);

    sqlite3_stmt* statement = nullptr;
    QCOMPARE(sqlite3_prepare_v2(database, "SELECT value FROM note;", -1, &statement, nullptr), SQLITE_OK);
    QCOMPARE(sqlite3_step(statement), SQLITE_ROW);
    const unsigned char* value = sqlite3_column_text(statement, 0);
    QVERIFY(value != nullptr);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(value)), QStringLiteral("audio-assistant"));
    QCOMPARE(sqlite3_step(statement), SQLITE_DONE);
    QCOMPARE(sqlite3_finalize(statement), SQLITE_OK);
    QCOMPARE(sqlite3_close(database), SQLITE_OK);
}

QTEST_GUILESS_MAIN(TestSqliteSmoke)

#include "tst_sqlite_smoke.moc"
