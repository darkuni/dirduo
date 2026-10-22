#include "core/FileOps.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static void touch(const QString &path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

class TestFileOps : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void uniqueNames()
    {
        QTemporaryDir d;
        QCOMPARE(FileOps::uniqueName(d.path(), QStringLiteral("a.jpg")), QStringLiteral("a.jpg"));
        touch(d.filePath(QStringLiteral("a.jpg")));
        QCOMPARE(FileOps::uniqueName(d.path(), QStringLiteral("a.jpg")), QStringLiteral("a (2).jpg"));
        touch(d.filePath(QStringLiteral("a (2).jpg")));
        QCOMPARE(FileOps::uniqueName(d.path(), QStringLiteral("a.jpg")), QStringLiteral("a (3).jpg"));
        touch(d.filePath(QStringLiteral(".bashrc")));
        QCOMPARE(FileOps::uniqueName(d.path(), QStringLiteral(".bashrc")), QStringLiteral(".bashrc (2)"));
        QDir(d.path()).mkdir(QStringLiteral("v1.0"));
        QCOMPARE(FileOps::uniqueName(d.path(), QStringLiteral("v1.0")), QStringLiteral("v1.0 (2)"));
    }

    void insideChecks()
    {
        QVERIFY(FileOps::isSameOrInside(QStringLiteral("/a/b"), QStringLiteral("/a")));
        QVERIFY(FileOps::isSameOrInside(QStringLiteral("/a"), QStringLiteral("/a/")));
        QVERIFY(!FileOps::isSameOrInside(QStringLiteral("/ab"), QStringLiteral("/a")));
        QVERIFY(FileOps::isSameOrInside(QStringLiteral("/x"), QStringLiteral("/")));
    }

    void renameNeverOverwrites()
    {
        QTemporaryDir d;
        touch(d.filePath(QStringLiteral("one")));
        touch(d.filePath(QStringLiteral("two")));
        QString newPath, error;
        QVERIFY(!FileOps::renameEntry(d.filePath(QStringLiteral("one")), QStringLiteral("two"), &newPath, &error));
        QVERIFY(error.contains(QStringLiteral("already exists")));
        QVERIFY(FileOps::renameEntry(d.filePath(QStringLiteral("one")), QStringLiteral("three"), &newPath, &error));
        QCOMPARE(newPath, d.filePath(QStringLiteral("three")));
        QVERIFY(QFile::exists(newPath));
        QVERIFY(!FileOps::renameEntry(newPath, QStringLiteral("a/b"), &newPath, &error));
        QVERIFY(!FileOps::renameEntry(d.filePath(QStringLiteral("two")), QStringLiteral(""), &newPath, &error));
    }

    void createFoldersFromList()
    {
        QTemporaryDir d;
        touch(d.filePath(QStringLiteral("C")));
        QString error;
        const QStringList made = FileOps::createFolders(d.path(), QStringLiteral("A, B ,, Joan Jett,C,../x"), &error);
        QCOMPARE(made.size(), 3);
        QVERIFY(QFileInfo(d.filePath(QStringLiteral("Joan Jett"))).isDir());
        QVERIFY(error.contains(QStringLiteral("C:")));
        QVERIFY(error.contains(QStringLiteral("../x")));
        // Running again keeps existing folders and still reports them.
        QCOMPARE(FileOps::createFolders(d.path(), QStringLiteral("A"), &error).size(), 1);
    }

    void naturalOrder()
    {
        QStringList names = {QStringLiteral("audio10.mp3"), QStringLiteral("Audio2.mp3"), QStringLiteral("audio1.mp3"),
                             QStringLiteral("track 007"), QStringLiteral("track 8"), QStringLiteral("10 intro"),
                             QStringLiteral("b"), QStringLiteral("A")};
        std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) { return FileOps::naturalCompare(a, b) < 0; });
        QCOMPARE(names, (QStringList{QStringLiteral("10 intro"), QStringLiteral("A"), QStringLiteral("audio1.mp3"),
                                     QStringLiteral("Audio2.mp3"), QStringLiteral("audio10.mp3"), QStringLiteral("b"),
                                     QStringLiteral("track 007"), QStringLiteral("track 8")}));
        QVERIFY(FileOps::naturalCompare(QStringLiteral("x"), QStringLiteral("x")) == 0);
    }

    void sizes()
    {
        QCOMPARE(FileOps::formatSize(512), QStringLiteral("512 bytes"));
        QVERIFY(FileOps::formatSize(1536).endsWith(QStringLiteral("KB")));
        QVERIFY(FileOps::formatSize(5LL * 1024 * 1024 * 1024).endsWith(QStringLiteral("GB")));
    }
};

QTEST_GUILESS_MAIN(TestFileOps)
#include "tst_fileops.moc"
