// Thumbnails and the shared freedesktop.org thumbnail cache.
#include "core/Paths.h"
#include "core/Thumbnails.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class TestThumbnails : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    QString picture(const QString &name, const QSize &size)
    {
        QImage img(size, QImage::Format_RGB32);
        img.fill(Qt::magenta);
        const QString path = m_dir.filePath(QStringLiteral("files/") + name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!img.save(path))
            qFatal("could not write %s", qPrintable(path));
        return path;
    }

private Q_SLOTS:
    void initTestCase()
    {
        Paths::setCacheHomeOverride(m_dir.filePath(QStringLiteral("cache")));
    }

    void cacheFileFollowsTheSpec()
    {
        const QString path = m_dir.filePath(QStringLiteral("files/a b.png"));
        const QByteArray uri = QUrl::fromLocalFile(path).toEncoded();
        QVERIFY(uri.contains("a%20b.png"));
        const QString md5 = QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex());
        QCOMPARE(Thumbnails::cacheFileFor(path, 256), m_dir.filePath(QStringLiteral("cache/thumbnails/large/")) + md5 + QStringLiteral(".png"));
        QVERIFY(Thumbnails::cacheFileFor(path, 128).contains(QStringLiteral("/normal/")));
        QVERIFY(Thumbnails::cacheFileFor(path, 512).contains(QStringLiteral("/x-large/")));
    }

    void makesAndCaches()
    {
        const QString path = picture(QStringLiteral("big.png"), QSize(1280, 800));
        const QDateTime mtime = QFileInfo(path).lastModified();
        QVERIFY(Thumbnails::canMake(path));
        QVERIFY(!Thumbnails::canMake(QStringLiteral("song.mp3")));
        const QImage t = Thumbnails::load(path, mtime, Thumbnails::kLarge);
        QCOMPARE(t.size(), QSize(256, 160));
        // Saved for every other app, with what the spec asks for.
        const QString cached = Thumbnails::cacheFileFor(path, Thumbnails::kLarge);
        QVERIFY(QFileInfo::exists(cached));
        const QImage c(cached);
        QCOMPARE(c.text(QStringLiteral("Thumb::URI")), QString::fromLatin1(QUrl::fromLocalFile(path).toEncoded()));
        QCOMPARE(c.text(QStringLiteral("Thumb::MTime")), QString::number(mtime.toSecsSinceEpoch()));
        QCOMPARE(QFileInfo(cached).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions());
        QVERIFY(!Thumbnails::readCached(path, mtime, 256).isNull());
        // A changed file does not get the old thumbnail.
        QVERIFY(Thumbnails::readCached(path, mtime.addSecs(60), 256).isNull());
    }

    void smallPicturesAreNotCached()
    {
        const QString path = picture(QStringLiteral("tiny.png"), QSize(32, 32));
        const QImage t = Thumbnails::load(path, QFileInfo(path).lastModified(), 256);
        QCOMPARE(t.size(), QSize(32, 32));
        QVERIFY(!QFileInfo::exists(Thumbnails::cacheFileFor(path, 256)));
    }

    void usesOtherAppsThumbnails()
    {
        // A video: Linopus cannot make one, but Dolphin may have.
        const QString video = m_dir.filePath(QStringLiteral("files/clip.mkv"));
        QFile f(video);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not really a video");
        f.close();
        const QDateTime mtime = QFileInfo(video).lastModified();
        QVERIFY(Thumbnails::load(video, mtime, 256).isNull());
        QImage made(256, 144, QImage::Format_RGB32);
        made.fill(Qt::cyan);
        QVERIFY(Thumbnails::writeCached(video, mtime, 256, made));
        QCOMPARE(Thumbnails::load(video, mtime, 256).size(), QSize(256, 144));
    }

    void cacheIsAsync()
    {
        const QString path = picture(QStringLiteral("async.jpg"), QSize(600, 600));
        const QDateTime mtime = QFileInfo(path).lastModified();
        ThumbnailCache cache;
        QSignalSpy ready(&cache, &ThumbnailCache::ready);
        QImage img;
        QVERIFY(!cache.lookup(path, mtime, &img));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(cache.lookup(path, mtime, &img));
        QCOMPARE(img.size(), QSize(256, 256));
        // Not a picture: remembered as having none.
        const QString text = m_dir.filePath(QStringLiteral("files/readme.txt"));
        QFile t(text);
        QVERIFY(t.open(QIODevice::WriteOnly));
        t.write("hi");
        t.close();
        QVERIFY(!cache.lookup(text, QFileInfo(text).lastModified(), &img));
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(cache.lookup(text, QFileInfo(text).lastModified(), &img));
        QVERIFY(img.isNull());
    }
};

QTEST_GUILESS_MAIN(TestThumbnails)
#include "tst_thumbnails.moc"
