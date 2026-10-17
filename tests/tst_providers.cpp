#include <QByteArray>
#include <QModelIndex>
#include <QString>
#include <QtTest>
#include <memory>
#include <utility>
#include <vector>

#include "FakeProvider.h"
#include "domain/Provider.h"
#include "domain/ProviderOffer.h"
#include "domain/Providers.h"

namespace {

// An offer whose minted provider reports the same identity as the offer, so a
// test can prove that "a fresh instance of this id" really is this provider.
ProviderOffer offerOf(const QString& id, const QString& displayName) {
    return {id, displayName, [id, displayName] { return std::make_unique<FakeProvider>(id, displayName); }};
}

}  // namespace

class TestProviders : public QObject {
    Q_OBJECT

public:
    explicit TestProviders(QObject* parent = nullptr);

private slots:
    void test_offerExposesItsIdentity();
    void test_offerMintsIndependentInstances();
    void test_emptyCollectionHasNoRows();
    void test_collectionExposesOfferedIdentities();
    void test_rowCountIgnoresChildren();
    void test_modelExposesOfferedRoles();
    void test_modelRejectsUnknownRole();
    void test_modelRejectsInvalidIndex();
    void test_modelRejectsIndexOfAnotherModel();
    void test_freshMintsNewInstancePerCall();
    void test_freshMintsTheProviderOfTheRequestedId();
    void test_freshOfUnknownIdReturnsNothing();
};

TestProviders::TestProviders(QObject* parent) : QObject(parent) {
}

void TestProviders::test_offerExposesItsIdentity() {
    const ProviderOffer kOffer = offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"));

    QCOMPARE(kOffer.id(), QStringLiteral("alpha"));
    QCOMPARE(kOffer.displayName(), QStringLiteral("Alpha"));
}

void TestProviders::test_offerMintsIndependentInstances() {
    const ProviderOffer kOffer = offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"));

    const std::unique_ptr<Provider> kFirst = kOffer.fresh();
    const std::unique_ptr<Provider> kSecond = kOffer.fresh();

    QVERIFY(kFirst != nullptr);
    QVERIFY(kSecond != nullptr);
    QVERIFY(kFirst.get() != kSecond.get());
    QCOMPARE(kFirst->id(), QStringLiteral("alpha"));
    QCOMPARE(kSecond->displayName(), QStringLiteral("Alpha"));
}

void TestProviders::test_emptyCollectionHasNoRows() {
    const Providers kProviders(std::vector<ProviderOffer>{});

    QCOMPARE(kProviders.count(), 0);
    QCOMPARE(kProviders.rowCount(), 0);
    QCOMPARE(kProviders.contains(QStringLiteral("alpha")), false);
    QVERIFY(kProviders.fresh(QStringLiteral("alpha")) == nullptr);
}

void TestProviders::test_collectionExposesOfferedIdentities() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha")),
                                offerOf(QStringLiteral("beta"), QStringLiteral("Beta"))});

    QCOMPARE(kProviders.count(), 2);
    QCOMPARE(kProviders.rowCount(), 2);
    QCOMPARE(kProviders.idAt(0), QStringLiteral("alpha"));
    QCOMPARE(kProviders.displayNameAt(0), QStringLiteral("Alpha"));
    QCOMPARE(kProviders.idAt(1), QStringLiteral("beta"));
    QCOMPARE(kProviders.displayNameAt(1), QStringLiteral("Beta"));
    QCOMPARE(kProviders.contains(QStringLiteral("beta")), true);
    QCOMPARE(kProviders.contains(QStringLiteral("gamma")), false);
}

void TestProviders::test_rowCountIgnoresChildren() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"))});

    QCOMPARE(kProviders.rowCount(kProviders.index(0)), 0);
}

void TestProviders::test_modelExposesOfferedRoles() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"))});

    QCOMPARE(kProviders.roleNames().value(Providers::kIdRole), QByteArrayLiteral("id"));
    QCOMPARE(kProviders.roleNames().value(Providers::kDisplayNameRole), QByteArrayLiteral("displayName"));
    QCOMPARE(kProviders.data(kProviders.index(0), Providers::kIdRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(kProviders.data(kProviders.index(0), Providers::kDisplayNameRole).toString(), QStringLiteral("Alpha"));
}

void TestProviders::test_modelRejectsUnknownRole() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"))});

    QVERIFY(!kProviders.data(kProviders.index(0), Qt::DisplayRole).isValid());
}

void TestProviders::test_modelRejectsInvalidIndex() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"))});

    QVERIFY(!kProviders.data(QModelIndex(), Providers::kIdRole).isValid());
    QVERIFY(!kProviders.data(kProviders.index(1), Providers::kIdRole).isValid());
}

void TestProviders::test_modelRejectsIndexOfAnotherModel() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"))});
    const Providers kOther({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha")),
                            offerOf(QStringLiteral("beta"), QStringLiteral("Beta"))});

    // A valid index of another model can name a row this model does not have.
    QVERIFY(!kProviders.data(kOther.index(1), Providers::kIdRole).isValid());
}

void TestProviders::test_freshMintsNewInstancePerCall() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"))});

    const std::unique_ptr<Provider> kFirst = kProviders.fresh(QStringLiteral("alpha"));
    const std::unique_ptr<Provider> kSecond = kProviders.fresh(QStringLiteral("alpha"));

    QVERIFY(kFirst != nullptr);
    QVERIFY(kSecond != nullptr);
    QVERIFY(kFirst.get() != kSecond.get());
}

void TestProviders::test_freshMintsTheProviderOfTheRequestedId() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha")),
                                offerOf(QStringLiteral("beta"), QStringLiteral("Beta"))});

    const std::unique_ptr<Provider> kAlpha = kProviders.fresh(QStringLiteral("alpha"));
    const std::unique_ptr<Provider> kBeta = kProviders.fresh(QStringLiteral("beta"));

    QVERIFY(kAlpha != nullptr);
    QVERIFY(kBeta != nullptr);
    QCOMPARE(kAlpha->id(), QStringLiteral("alpha"));
    QCOMPARE(kAlpha->displayName(), QStringLiteral("Alpha"));
    QCOMPARE(kBeta->id(), QStringLiteral("beta"));
    QCOMPARE(kBeta->displayName(), QStringLiteral("Beta"));
}

void TestProviders::test_freshOfUnknownIdReturnsNothing() {
    const Providers kProviders({offerOf(QStringLiteral("alpha"), QStringLiteral("Alpha"))});

    QVERIFY(kProviders.fresh(QStringLiteral("beta")) == nullptr);
    QCOMPARE(kProviders.contains(QStringLiteral("beta")), false);
}

QTEST_GUILESS_MAIN(TestProviders)

#include "tst_providers.moc"
