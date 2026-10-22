// Music tags, photo details and SID headers, read the way the columns read them.
#include "core/Metadata.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifdef LINOPUS_HAVE_TAGLIB
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#endif
#ifdef LINOPUS_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

namespace {

// One second of 44.1 kHz 16-bit stereo silence.
bool writeWav(const QString &path, int seconds = 1)
{
    const quint32 rate = 44100, channels = 2, bits = 16;
    const quint32 dataSize = rate * channels * (bits / 8) * quint32(seconds);
    QByteArray b;
    auto u32 = [&b](quint32 v) { for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xff)); };
    auto u16 = [&b](quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); };
    b.append("RIFF");
    u32(36 + dataSize);
    b.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(quint16(channels));
    u32(rate);
    u32(rate * channels * bits / 8);
    u16(quint16(channels * bits / 8));
    u16(quint16(bits));
    b.append("data");
    u32(dataSize);
    b.append(QByteArray(int(dataSize), '\0'));
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}

QByteArray sidHeader(const QByteArray &name, const QByteArray &author, const QByteArray &released, int songs, int start)
{
    QByteArray h(0x7c, '\0');
    h.replace(0, 4, "PSID");
    h[5] = 2; // version
    h[7] = 0x7c;
    h[0x0f] = char(songs);
    h[0x11] = char(start);
    h.replace(0x16, name.size(), name);
    h.replace(0x36, author.size(), author);
    h.replace(0x56, released.size(), released);
    return h;
}

} // namespace

class TestMetadata : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void kinds()
    {
        QCOMPARE(Metadata::kindFor(QStringLiteral("Song.MP3")), MediaInfo::Kind::Audio);
        QCOMPARE(Metadata::kindFor(QStringLiteral("Commando.sid")), MediaInfo::Kind::Audio);
        QCOMPARE(Metadata::kindFor(QStringLiteral("IMG_0001.CR3")), MediaInfo::Kind::Image);
        QCOMPARE(Metadata::kindFor(QStringLiteral("clip.mkv")), MediaInfo::Kind::Video);
        QCOMPARE(Metadata::kindFor(QStringLiteral("game.d64")), MediaInfo::Kind::None);
        QCOMPARE(Metadata::kindFor(QStringLiteral("README")), MediaInfo::Kind::None);
    }

    void text()
    {
        MediaInfo m;
        QVERIFY(m.isEmpty());
        QVERIFY(m.durationText().isEmpty());
        m.seconds = 222;
        QCOMPARE(m.durationText(), QStringLiteral("3:42"));
        m.seconds = 3725;
        QCOMPARE(m.durationText(), QStringLiteral("1:02:05"));
        m.width = 4000;
        m.height = 3000;
        QCOMPARE(m.dimensionsText(), QStringLiteral("4000 × 3000"));
        m.artist = QStringLiteral("Rob Hubbard");
        m.fNumber = 2.8;
        m.iso = 400;
        const QString s = m.summary();
        QVERIFY(s.contains(QStringLiteral("Artist: Rob Hubbard")));
        QVERIFY(s.contains(QStringLiteral("f/2.8")));
        QVERIFY(s.contains(QStringLiteral("ISO 400")));
    }

    void sid()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Commando.sid"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(sidHeader("Commando", "Rob Hubbard", "1985 Elite", 3, 1));
        f.write(QByteArray(512, '\x60'));
        f.close();
        const MediaInfo m = Metadata::read(path);
        QCOMPARE(m.kind, MediaInfo::Kind::Audio);
        QCOMPARE(m.title, QStringLiteral("Commando"));
        QCOMPARE(m.artist, QStringLiteral("Rob Hubbard"));
        QCOMPARE(m.album, QStringLiteral("1985 Elite"));
        QCOMPARE(m.year, 1985);
        QCOMPARE(m.track, 1);
        QCOMPARE(m.genre, QStringLiteral("C64 SID"));

        // Unknown fields are "<?>" in the HVSC.
        QFile g(dir.filePath(QStringLiteral("unknown.sid")));
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.write(sidHeader("Tune", "<?>", "<?>", 1, 1));
        g.close();
        const MediaInfo u = Metadata::read(g.fileName());
        QVERIFY(u.artist.isEmpty());
        QCOMPARE(u.year, 0);

        // Not a SID at all.
        QFile bad(dir.filePath(QStringLiteral("bad.sid")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("hello");
        bad.close();
        QVERIFY(Metadata::read(bad.fileName()).title.isEmpty());
    }

    void imageSizeWithoutExif()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("plain.png"));
        QImage img(320, 200, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(path));
        const MediaInfo m = Metadata::read(path);
        QCOMPARE(m.kind, MediaInfo::Kind::Image);
        QCOMPARE(m.width, 320);
        QCOMPARE(m.height, 200);
        QVERIFY(m.camera.isEmpty());
        QVERIFY(!m.taken.isValid());
    }

    void musicTags()
    {
#ifndef LINOPUS_HAVE_TAGLIB
        QSKIP("Built without TagLib");
#else
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("song.wav"));
        QVERIFY(writeWav(path, 2));
        {
            TagLib::FileRef f(QFile::encodeName(path).constData());
            QVERIFY(!f.isNull() && f.tag());
            f.tag()->setTitle("Monty on the Run");
            f.tag()->setArtist("Rob Hubbard");
            f.tag()->setAlbum("C64 Classics");
            f.tag()->setGenre("Chiptune");
            f.tag()->setYear(1985);
            f.tag()->setTrack(7);
            TagLib::PropertyMap props = f.file()->properties();
            props.insert("ALBUMARTIST", TagLib::StringList("Various"));
            f.file()->setProperties(props);
            QVERIFY(f.save());
        }
        const MediaInfo m = Metadata::read(path);
        QCOMPARE(m.kind, MediaInfo::Kind::Audio);
        QCOMPARE(m.title, QStringLiteral("Monty on the Run"));
        QCOMPARE(m.artist, QStringLiteral("Rob Hubbard"));
        QCOMPARE(m.album, QStringLiteral("C64 Classics"));
        QCOMPARE(m.genre, QStringLiteral("Chiptune"));
        QCOMPARE(m.year, 1985);
        QCOMPARE(m.track, 7);
        QCOMPARE(m.albumArtist, QStringLiteral("Various"));
        QCOMPARE(m.seconds, 2);
        QCOMPARE(m.sampleRate, 44100);
        QCOMPARE(m.channels, 2);
        QCOMPARE(m.bitrate, 1411);
        QCOMPARE(m.durationText(), QStringLiteral("0:02"));
#endif
    }

    void photoDetails()
    {
#ifndef LINOPUS_HAVE_EXIV2
        QSKIP("Built without Exiv2");
#else
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("IMG_0001.jpg"));
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(Qt::darkGreen);
        QVERIFY(img.save(path, "JPEG"));
        {
            auto image = Exiv2::ImageFactory::open(std::string(QFile::encodeName(path).constData()));
            QVERIFY(image.get());
            image->readMetadata();
            Exiv2::ExifData &exif = image->exifData();
            exif["Exif.Image.Make"] = std::string("Canon");
            exif["Exif.Image.Model"] = std::string("Canon EOS R6");
            exif["Exif.Photo.DateTimeOriginal"] = std::string("2026:10:21 18:15:00");
            exif["Exif.Photo.FNumber"] = Exiv2::Rational(28, 10);
            exif["Exif.Photo.ExposureTime"] = Exiv2::Rational(1, 250);
            exif["Exif.Photo.ISOSpeedRatings"] = uint16_t(400);
            exif["Exif.Photo.FocalLength"] = Exiv2::Rational(50, 1);
            exif["Exif.Photo.LensModel"] = std::string("RF50mm F1.8 STM");
            image->writeMetadata();
        }
        const MediaInfo m = Metadata::read(path);
        QCOMPARE(m.kind, MediaInfo::Kind::Image);
        QCOMPARE(m.width, 64);
        QCOMPARE(m.height, 48);
        QCOMPARE(m.camera, QStringLiteral("Canon EOS R6")); // not "Canon Canon EOS R6"
        QCOMPARE(m.lens, QStringLiteral("RF50mm F1.8 STM"));
        QCOMPARE(m.taken, QDateTime(QDate(2026, 10, 21), QTime(18, 15)));
        QVERIFY(qAbs(m.fNumber - 2.8) < 0.01);
        QCOMPARE(m.exposure, QStringLiteral("1/250 s"));
        QCOMPARE(m.iso, 400);
        QVERIFY(qAbs(m.focalLength - 50) < 0.01);

        // A damaged file is not an error, just nothing.
        QFile bad(dir.filePath(QStringLiteral("broken.jpg")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("\xff\xd8\xff\xe1garbage");
        bad.close();
        const MediaInfo b = Metadata::read(bad.fileName());
        QVERIFY(b.camera.isEmpty());
#endif
    }

    void cache()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.sid"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(sidHeader("First", "Someone", "1986", 1, 1));
        f.close();
        MetadataCache cache;
        QSignalSpy ready(&cache, &MetadataCache::ready);
        QFileInfo fi(path);
        MediaInfo m;
        QVERIFY(!cache.lookup(path, fi.size(), fi.lastModified(), &m)); // starts a read
        QVERIFY(cache.isPending(path));
        QVERIFY(!cache.lookup(path, fi.size(), fi.lastModified(), &m)); // not twice
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(cache.lookup(path, fi.size(), fi.lastModified(), &m));
        QCOMPARE(m.title, QStringLiteral("First"));

        // Changed on disk: read again.
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(sidHeader("Second", "Someone", "1986", 1, 1));
        f.write("more");
        f.close();
        fi.refresh();
        QVERIFY(!cache.lookup(path, fi.size(), fi.lastModified(), &m));
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(cache.lookup(path, fi.size(), fi.lastModified(), &m));
        QCOMPARE(m.title, QStringLiteral("Second"));
    }
};

QTEST_GUILESS_MAIN(TestMetadata)
#include "tst_metadata.moc"
