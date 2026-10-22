#include "core/Location.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestLocation : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void remoteOrLocal()
    {
        QVERIFY(Location::isLocal(QStringLiteral("/home/deck")));
        QVERIFY(Location::isLocal(QStringLiteral("relative/dir")));
        QVERIFY(Location::isLocal(QStringLiteral("file:///home/deck")));
        QVERIFY(Location::isRemote(QStringLiteral("sftp://deck@nas/media")));
        QVERIFY(Location::isRemote(QStringLiteral("ftp://host/pub")));
        QVERIFY(Location::isRemote(QStringLiteral("zip:/home/deck/a.zip")));
        QVERIFY(Location::isLocal(QStringLiteral("/home/deck/notes:2026.txt"))); // a colon later on is just a name
        QVERIFY(Location::isArchive(QStringLiteral("zip:/x.zip")));
        QVERIFY(!Location::isArchive(QStringLiteral("sftp://h/x.zip")));
    }

    void cleanAndJoin()
    {
        QCOMPARE(Location::clean(QStringLiteral("/a/./b/")), QStringLiteral("/a/b"));
        QCOMPARE(Location::clean(QStringLiteral("file:///a/b")), QStringLiteral("/a/b"));
        QCOMPARE(Location::clean(QStringLiteral("sftp://h/a//b/")), QStringLiteral("sftp://h/a/b"));
        QCOMPARE(Location::clean(QStringLiteral("sftp://h")), QStringLiteral("sftp://h/"));
        QCOMPARE(Location::join(QStringLiteral("sftp://deck@nas/media"), QStringLiteral("Movies")),
                 QStringLiteral("sftp://deck@nas/media/Movies"));
        QCOMPARE(Location::join(QStringLiteral("sftp://nas/"), QStringLiteral("x")), QStringLiteral("sftp://nas/x"));
        QCOMPARE(Location::join(QStringLiteral("/home"), QStringLiteral("deck")), QStringLiteral("/home/deck"));
        // Names with spaces survive the round trip through a URL.
        const QString spaced = Location::join(QStringLiteral("ftp://h/pub"), QStringLiteral("My Files"));
        QCOMPARE(Location::fileName(spaced), QStringLiteral("My Files"));
    }

    void parents()
    {
        QCOMPARE(Location::parent(QStringLiteral("/home/deck")), QStringLiteral("/home"));
        QCOMPARE(Location::parent(QStringLiteral("sftp://nas/media/Movies")), QStringLiteral("sftp://nas/media"));
        QCOMPARE(Location::parent(QStringLiteral("sftp://nas/media")), QStringLiteral("sftp://nas/"));
        QCOMPARE(Location::parent(QStringLiteral("sftp://nas/")), QStringLiteral("sftp://nas/"));
        QVERIFY(Location::isRoot(QStringLiteral("sftp://nas/")));
        QVERIFY(Location::isRoot(QStringLiteral("/")));
    }

    void archivesLeadBackToTheirFolder()
    {
        QTemporaryDir d;
        const QString zip = d.filePath(QStringLiteral("roms.zip"));
        QFile f(zip);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("PK");
        f.close();
        const QString inside = QStringLiteral("zip:") + zip + QStringLiteral("/c64/games");
        QCOMPARE(Location::archiveFile(inside), zip);
        QCOMPARE(Location::parent(inside), QStringLiteral("zip:") + zip + QStringLiteral("/c64"));
        QCOMPARE(Location::parent(QStringLiteral("zip:") + zip), d.path());
        QVERIFY(!Location::isRoot(QStringLiteral("zip:") + zip));
        QCOMPARE(Location::title(QStringLiteral("zip:") + zip), QStringLiteral("roms.zip"));
    }

    void titlesAndDisplay()
    {
        QCOMPARE(Location::title(QStringLiteral("sftp://deck@nas/")), QStringLiteral("nas"));
        QCOMPARE(Location::title(QStringLiteral("sftp://deck@nas/media")), QStringLiteral("media"));
        QCOMPARE(Location::title(QDir::homePath()), QStringLiteral("Home"));
        QCOMPARE(Location::display(QStringLiteral("ftp://me:secret@h/pub")), QStringLiteral("ftp://me@h/pub"));
    }

    void inside()
    {
        QVERIFY(Location::isSameOrInside(QStringLiteral("sftp://h/a/b"), QStringLiteral("sftp://h/a")));
        QVERIFY(Location::isSameOrInside(QStringLiteral("sftp://h/a"), QStringLiteral("sftp://h/")));
        QVERIFY(!Location::isSameOrInside(QStringLiteral("sftp://h/ab"), QStringLiteral("sftp://h/a")));
        QVERIFY(!Location::isSameOrInside(QStringLiteral("sftp://other/a"), QStringLiteral("sftp://h/")));
        QVERIFY(!Location::isSameOrInside(QStringLiteral("sftp://h/a"), QStringLiteral("/")));
    }

    void serverKeys()
    {
        QCOMPARE(Location::serverKey(QStringLiteral("sftp://deck:pw@nas:2222/x")), QStringLiteral("sftp://deck@nas:2222"));
        QCOMPARE(Location::serverKey(QStringLiteral("/home")), QString());
    }
};

QTEST_GUILESS_MAIN(TestLocation)
#include "tst_location.moc"
