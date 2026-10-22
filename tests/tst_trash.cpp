#include "core/Paths.h"
#include "core/Trash.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestTrash : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        // Keep the test trash next to the files so they share a device.
        QVERIFY(m_root.isValid());
        Paths::setDataHomeOverride(m_root.filePath(QStringLiteral("data")));
        QDir().mkpath(m_root.filePath(QStringLiteral("data")));
    }

    void trashesFileWithInfo()
    {
        const QString file = m_root.filePath(QStringLiteral("song 1.mp3"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("abc");
        }
        QString trashed, error;
        QVERIFY2(Trash::moveToTrash(file, &trashed, &error), qPrintable(error));
        QVERIFY(!QFile::exists(file));
        QCOMPARE(trashed, Trash::homeTrashDir() + QStringLiteral("/files/song 1.mp3"));
        QVERIFY(QFile::exists(trashed));

        QFile info(Trash::homeTrashDir() + QStringLiteral("/info/song 1.mp3.trashinfo"));
        QVERIFY(info.open(QIODevice::ReadOnly));
        const QByteArray body = info.readAll();
        QVERIFY(body.startsWith("[Trash Info]\n"));
        QVERIFY(body.contains("Path=" + QUrl::toPercentEncoding(file, "/")));
        QVERIFY(body.contains("DeletionDate="));
    }

    void sameNameGetsSuffix()
    {
        for (int i = 0; i < 2; ++i) {
            const QString dir = m_root.filePath(QStringLiteral("folder"));
            QVERIFY(QDir().mkpath(dir + QStringLiteral("/inner")));
            QString trashed, error;
            QVERIFY2(Trash::moveToTrash(dir, &trashed, &error), qPrintable(error));
            if (i == 1)
                QCOMPARE(trashed, Trash::homeTrashDir() + QStringLiteral("/files/folder.2"));
        }
        QVERIFY(QFile::exists(Trash::homeTrashDir() + QStringLiteral("/info/folder.2.trashinfo")));
    }

    void missingFileFails()
    {
        QString error;
        QVERIFY(!Trash::moveToTrash(m_root.filePath(QStringLiteral("nope")), nullptr, &error));
        QVERIFY(!error.isEmpty());
    }

private:
    QTemporaryDir m_root;
};

QTEST_GUILESS_MAIN(TestTrash)
#include "tst_trash.moc"
