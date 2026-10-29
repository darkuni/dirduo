// Thumbnails and the shared freedesktop.org thumbnail cache.
#include "core/Paths.h"
#include "core/Metadata.h"
#include "core/Thumbnails.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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
        // A video: DirDuo cannot make one, but Dolphin may have.
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
    QString colored(const QString &name, const QSize &size, const QColor &c)
    {
        QImage img(size, QImage::Format_RGB32);
        img.fill(c);
        const QString path = m_dir.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!img.save(path))
            qFatal("could not write %s", qPrintable(path));
        return path;
    }

    void kinds()
    {
        QCOMPARE(Thumbnails::kindOf(QStringLiteral("a.jpg")), Thumbnails::Kind::Picture);
        QCOMPARE(Thumbnails::kindOf(QStringLiteral("a.mkv")), Thumbnails::Kind::Video);
        QCOMPARE(Thumbnails::kindOf(QStringLiteral("a.MP4")), Thumbnails::Kind::Video);
        QCOMPARE(Thumbnails::kindOf(QStringLiteral("a.pdf")), Thumbnails::Kind::Document);
        QCOMPARE(Thumbnails::kindOf(QStringLiteral("a.flac")), Thumbnails::Kind::Audio);
        QCOMPARE(Thumbnails::kindOf(QStringLiteral("a.txt")), Thumbnails::Kind::None);
        QCOMPARE(Thumbnails::bucketFor(100), Thumbnails::kLarge);
        QCOMPARE(Thumbnails::bucketFor(256), Thumbnails::kLarge);
        QCOMPARE(Thumbnails::bucketFor(300), Thumbnails::kXLarge);
    }

    void remembersTheOriginalSize()
    {
        const QString path = colored(QStringLiteral("size/wide.png"), QSize(1200, 600), Qt::red);
        const QDateTime mtime = QFileInfo(path).lastModified();
        QSize full;
        const QImage t = Thumbnails::load(path, mtime, Thumbnails::kXLarge, {}, &full);
        QCOMPARE(t.size(), QSize(512, 256));
        QCOMPARE(full, QSize(1200, 600));
        // Read back from the cache file, as the spec's Thumb::Image fields.
        QSize again;
        QVERIFY(!Thumbnails::readCached(path, mtime, Thumbnails::kXLarge, &again).isNull());
        QCOMPARE(again, QSize(1200, 600));
    }

    void albumArtFromTheFolder()
    {
        const QString dir = m_dir.filePath(QStringLiteral("album"));
        QDir().mkpath(dir);
        colored(QStringLiteral("album/Cover.JPG"), QSize(600, 600), Qt::blue);
        const QString song = dir + QStringLiteral("/01 Track.mp3");
        QFile f(song);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1024, '\0'));
        f.close();
        const QImage cover = Metadata::coverArt(song);
        QCOMPARE(cover.size(), QSize(600, 600));
        const QDateTime mtime = QFileInfo(song).lastModified();
        const QImage t = Thumbnails::load(song, mtime, Thumbnails::kLarge);
        QCOMPARE(t.size(), QSize(256, 256));
        QVERIFY(qBlue(t.pixel(128, 128)) > 240 && qRed(t.pixel(128, 128)) < 16); // JPEG: nearly blue
        QVERIFY(QFileInfo::exists(Thumbnails::cacheFileFor(song, Thumbnails::kLarge)));
        // Switched off: none.
        Thumbnails::Options off;
        off.covers = false;
        const QString other = dir + QStringLiteral("/02 Track.ogg");
        QFile g(other);
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.write("x");
        g.close();
        QVERIFY(Thumbnails::load(other, QFileInfo(other).lastModified(), Thumbnails::kLarge, off).isNull());
        // Not music: no cover even with one next to it.
        QVERIFY(Metadata::coverArt(dir + QStringLiteral("/notes.txt")).isNull());
    }

    void documentsThroughTheirMaker()
    {
        const QString pdf = m_dir.filePath(QStringLiteral("docs/manual.pdf"));
        QDir().mkpath(QFileInfo(pdf).absolutePath());
        QFile f(pdf);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("%PDF-1.4 pretend");
        f.close();
        const QDateTime mtime = QFileInfo(pdf).lastModified();
        QVERIFY(Thumbnails::load(pdf, mtime, 256).isNull()); // no maker yet
        Thumbnails::setDocumentMaker([](const QString &, int size) {
            QImage page(size * 3 / 4, size, QImage::Format_RGB32);
            page.fill(Qt::white);
            return page;
        });
        QVERIFY(Thumbnails::hasDocumentMaker());
        Thumbnails::Options off;
        off.documents = false;
        QVERIFY(Thumbnails::load(pdf, mtime, 256, off).isNull());
        QCOMPARE(Thumbnails::load(pdf, mtime, 256).size(), QSize(192, 256));
        QVERIFY(QFileInfo::exists(Thumbnails::cacheFileFor(pdf, 256)));
        Thumbnails::setDocumentMaker({});
    }

    void folderCollages()
    {
        const QString dir = m_dir.filePath(QStringLiteral("trip"));
        QDir().mkpath(dir);
        QVERIFY(Thumbnails::folderCollage(dir, 256, false).isNull()); // nothing inside
        const QString a = colored(QStringLiteral("trip/a.png"), QSize(400, 300), Qt::red);
        const QString b = colored(QStringLiteral("trip/b.png"), QSize(400, 300), Qt::green);
        const QString c = colored(QStringLiteral("trip/c.png"), QSize(400, 300), Qt::blue);
        // c is the newest, a the oldest.
        const QDateTime now = QDateTime::currentDateTime();
        for (const auto &[path, age] : {std::pair{a, 300}, std::pair{b, 200}, std::pair{c, 100}}) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(now.addSecs(-age), QFileDevice::FileModificationTime));
        }
        const QImage first = Thumbnails::folderCollage(dir, 256, false, 1);
        QCOMPARE(first.size(), QSize(256, 256));
        QCOMPARE(QColor(first.pixel(128, 128)), QColor(Qt::red));
        const QImage newest = Thumbnails::folderCollage(dir, 256, true, 1);
        QCOMPARE(QColor(newest.pixel(128, 128)), QColor(Qt::blue));
        // Several: a grid, each picture in its own cell.
        const QImage grid = Thumbnails::folderCollage(dir, 256, false);
        QCOMPARE(QColor(grid.pixel(60, 60)), QColor(Qt::red));
        QCOMPARE(QColor(grid.pixel(196, 60)), QColor(Qt::green));
        QCOMPARE(QColor(grid.pixel(60, 196)), QColor(Qt::blue));
        QCOMPARE(qAlpha(grid.pixel(196, 196)), 0); // only three
    }

    void biggerOnRequest()
    {
        const QString path = colored(QStringLiteral("grow/huge.png"), QSize(1600, 1600), Qt::darkCyan);
        const QDateTime mtime = QFileInfo(path).lastModified();
        ThumbnailCache cache;
        QSignalSpy ready(&cache, &ThumbnailCache::ready);
        ThumbnailCache::Result r;
        QVERIFY(!cache.lookup(path, mtime, 200, &r));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(cache.lookup(path, mtime, 200, &r));
        QCOMPARE(r.image.size(), QSize(256, 256));
        QVERIFY(r.sharp);
        QCOMPARE(r.full, QSize(1600, 1600));
        // The slider goes past 256: the small one shows while a bigger one is made.
        QVERIFY(cache.lookup(path, mtime, 400, &r));
        QVERIFY(!r.sharp);
        QCOMPARE(r.image.size(), QSize(256, 256));
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(cache.lookup(path, mtime, 400, &r));
        QVERIFY(r.sharp);
        QCOMPARE(r.image.size(), QSize(512, 512));
        // And it does for small sizes too, without another load.
        QVERIFY(cache.lookup(path, mtime, 100, &r));
        QCOMPARE(r.image.size(), QSize(512, 512));
        QTest::qWait(50);
        QCOMPARE(ready.size(), 2);
        // A small picture is never made bigger.
        const QString small = colored(QStringLiteral("grow/small.png"), QSize(200, 100), Qt::yellow);
        QVERIFY(!cache.lookup(small, QFileInfo(small).lastModified(), 100, &r));
        QTRY_COMPARE(ready.size(), 3);
        QVERIFY(cache.lookup(small, QFileInfo(small).lastModified(), 500, &r));
        QVERIFY(r.sharp);
        QCOMPARE(r.image.size(), QSize(200, 100));
    }

    void videosThroughTheMainThreadMaker()
    {
        const QString video = m_dir.filePath(QStringLiteral("films/trip.mp4"));
        QDir().mkpath(QFileInfo(video).absolutePath());
        QFile f(video);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("pretend");
        f.close();
        int calls = 0;
        Thumbnails::setVideoMaker([&calls](const QString &, int size, std::function<void(const QImage &)> done) {
            ++calls;
            QTimer::singleShot(10, [done, size]() {
                QImage frame(1920, 1080, QImage::Format_RGB32);
                frame.fill(Qt::darkGreen);
                Q_UNUSED(size);
                done(frame);
            });
        });
        ThumbnailCache cache;
        QSignalSpy ready(&cache, &ThumbnailCache::ready);
        ThumbnailCache::Result r;
        const QDateTime mtime = QFileInfo(video).lastModified();
        QVERIFY(!cache.lookup(video, mtime, 256, &r));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(calls, 1);
        QVERIFY(cache.lookup(video, mtime, 256, &r));
        QCOMPARE(r.image.size(), QSize(512, 288));
        QCOMPARE(r.full, QSize(1920, 1080));
        QTRY_VERIFY(QFileInfo::exists(Thumbnails::cacheFileFor(video, Thumbnails::kXLarge)));
        // Switched off: not asked.
        Thumbnails::Options off;
        off.video = false;
        cache.setOptions(off);
        const QString other = m_dir.filePath(QStringLiteral("films/other.mkv"));
        QFile g(other);
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.write("x");
        g.close();
        QVERIFY(!cache.lookup(other, QFileInfo(other).lastModified(), 256, &r));
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(cache.lookup(other, QFileInfo(other).lastModified(), 256, &r));
        QVERIFY(r.image.isNull());
        QCOMPARE(calls, 1);
        Thumbnails::setVideoMaker({});
    }

    void folderThumbnailsInTheCache()
    {
        const QString dir = m_dir.filePath(QStringLiteral("album2"));
        colored(QStringLiteral("album2/x.png"), QSize(300, 300), Qt::magenta);
        ThumbnailCache cache;
        QSignalSpy ready(&cache, &ThumbnailCache::ready);
        ThumbnailCache::Result r;
        const QDateTime mtime = QFileInfo(dir).lastModified();
        QVERIFY(!cache.lookupFolder(dir, mtime, 128, false, &r));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.at(0).at(0).toString(), dir);
        QVERIFY(cache.lookupFolder(dir, mtime, 128, false, &r));
        QCOMPARE(r.image.size(), QSize(256, 256));
    }
};

QTEST_GUILESS_MAIN(TestThumbnails)
#include "tst_thumbnails.moc"
