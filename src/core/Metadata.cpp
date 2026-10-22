#include "core/Metadata.h"

#include "core/FileKind.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>

#include <cmath>
#include <mutex>

#ifdef LINOPUS_HAVE_TAGLIB
#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#endif

#ifdef LINOPUS_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

bool MediaInfo::isEmpty() const
{
    return title.isEmpty() && artist.isEmpty() && album.isEmpty() && seconds < 0 && width == 0 && camera.isEmpty()
        && !taken.isValid();
}

QString MediaInfo::durationText() const
{
    if (seconds < 0)
        return {};
    const int h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString MediaInfo::dimensionsText() const
{
    if (width <= 0 || height <= 0)
        return {};
    return QStringLiteral("%1 × %2").arg(width).arg(height);
}

QString MediaInfo::summary() const
{
    QStringList lines;
    auto add = [&lines](const QString &label, const QString &value) {
        if (!value.isEmpty())
            lines << QStringLiteral("%1: %2").arg(label, value);
    };
    add(QStringLiteral("Title"), title);
    add(QStringLiteral("Artist"), artist);
    if (albumArtist != artist)
        add(QStringLiteral("Album artist"), albumArtist);
    add(QStringLiteral("Album"), album);
    if (track > 0)
        add(QStringLiteral("Track"), disc > 0 ? QStringLiteral("%1 (disc %2)").arg(track).arg(disc) : QString::number(track));
    if (year > 0)
        add(QStringLiteral("Year"), QString::number(year));
    add(QStringLiteral("Genre"), genre);
    add(QStringLiteral("Length"), durationText());
    if (bitrate > 0)
        add(QStringLiteral("Bitrate"), QStringLiteral("%1 kbit/s").arg(bitrate));
    if (sampleRate > 0)
        add(QStringLiteral("Sample rate"),
            QStringLiteral("%1 kHz%2").arg(sampleRate / 1000.0, 0, 'g', 4).arg(channels == 1 ? QStringLiteral(", mono")
                                                                             : channels == 2 ? QStringLiteral(", stereo")
                                                                             : channels > 2 ? QStringLiteral(", %1 channels").arg(channels)
                                                                                            : QString()));
    add(QStringLiteral("Size"), dimensionsText());
    if (taken.isValid())
        add(QStringLiteral("Taken"), QLocale().toString(taken, QLocale::ShortFormat));
    add(QStringLiteral("Camera"), camera);
    add(QStringLiteral("Lens"), lens);
    QStringList exposureBits;
    if (fNumber > 0)
        exposureBits << QStringLiteral("f/%1").arg(fNumber, 0, 'g', 3);
    if (!exposure.isEmpty())
        exposureBits << exposure;
    if (iso > 0)
        exposureBits << QStringLiteral("ISO %1").arg(iso);
    if (focalLength > 0)
        exposureBits << QStringLiteral("%1 mm").arg(focalLength, 0, 'g', 4);
    add(QStringLiteral("Exposure"), exposureBits.join(QStringLiteral(", ")));
    return lines.join(QLatin1Char('\n'));
}

namespace Metadata {

namespace {

const QSet<QString> &audioExtensions()
{
    static const QSet<QString> s = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"),
        QStringLiteral("m4a"), QStringLiteral("m4b"), QStringLiteral("aac"), QStringLiteral("wav"), QStringLiteral("aif"),
        QStringLiteral("aiff"), QStringLiteral("wma"), QStringLiteral("ape"), QStringLiteral("wv"), QStringLiteral("mpc"),
        QStringLiteral("spx"), QStringLiteral("tta"), QStringLiteral("dsf"), QStringLiteral("dff"), QStringLiteral("mod"),
        QStringLiteral("s3m"), QStringLiteral("xm"), QStringLiteral("it"), QStringLiteral("sid"), QStringLiteral("psid")};
    return s;
}

const QSet<QString> &videoExtensions()
{
    static const QSet<QString> s = {QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("mkv"),
                                    QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("wmv"), QStringLiteral("asf")};
    return s;
}

const QSet<QString> &imageExtensions()
{
    static const QSet<QString> s = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"), QStringLiteral("png"), QStringLiteral("webp"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"),
        QStringLiteral("jxl"), QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("dng"), QStringLiteral("cr2"),
        QStringLiteral("cr3"), QStringLiteral("crw"), QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("arw"),
        QStringLiteral("srw"), QStringLiteral("orf"), QStringLiteral("rw2"), QStringLiteral("raf"), QStringLiteral("pef"),
        QStringLiteral("psd"), QStringLiteral("tga"), QStringLiteral("ico"), QStringLiteral("iff"), QStringLiteral("ilbm"),
        QStringLiteral("lbm")};
    return s;
}

#ifdef LINOPUS_HAVE_TAGLIB
QString qs(const TagLib::String &s)
{
    return QString::fromUtf8(s.toCString(true)).trimmed();
}

void readTagLib(const QString &path, MediaInfo &info)
{
    TagLib::FileRef f(QFile::encodeName(path).constData(), true, TagLib::AudioProperties::Average);
    if (f.isNull())
        return;
    if (TagLib::Tag *t = f.tag()) {
        info.title = qs(t->title());
        info.artist = qs(t->artist());
        info.album = qs(t->album());
        info.genre = qs(t->genre());
        info.year = int(t->year());
        info.track = int(t->track());
        const TagLib::PropertyMap props = f.file()->properties();
        auto first = [&props](const char *key) -> QString {
            const auto it = props.find(key);
            return it == props.end() || it->second.isEmpty() ? QString() : qs(it->second.front());
        };
        info.albumArtist = first("ALBUMARTIST");
        const QString disc = first("DISCNUMBER");
        info.disc = disc.section(QLatin1Char('/'), 0, 0).toInt();
    }
    if (TagLib::AudioProperties *a = f.audioProperties()) {
        info.seconds = a->lengthInSeconds();
        info.bitrate = a->bitrate();
        info.sampleRate = a->sampleRate();
        info.channels = a->channels();
        if (info.seconds == 0 && info.bitrate == 0)
            info.seconds = -1; // nothing known
    }
}
#endif

#ifdef LINOPUS_HAVE_EXIV2
void readExiv2(const QString &path, MediaInfo &info)
{
    static std::once_flag once;
    std::call_once(once, []() { Exiv2::XmpParser::initialize(); });
    try {
        auto image = Exiv2::ImageFactory::open(std::string(QFile::encodeName(path).constData()));
        if (!image.get())
            return;
        image->readMetadata();
        const Exiv2::ExifData &exif = image->exifData();
        if (exif.empty())
            return;
        auto str = [&exif](const char *key) -> QString {
            const auto it = exif.findKey(Exiv2::ExifKey(key));
            if (it == exif.end())
                return {};
            return QString::fromStdString(it->toString()).trimmed();
        };
        auto num = [&exif](const char *key) -> double {
            const auto it = exif.findKey(Exiv2::ExifKey(key));
            if (it == exif.end() || it->count() == 0)
                return 0;
            const float v = it->toFloat(0);
            return std::isfinite(v) ? double(v) : 0.0;
        };
        const QString make = str("Exif.Image.Make");
        QString model = str("Exif.Image.Model");
        // "Canon" + "Canon EOS R6" should not read "Canon Canon EOS R6"; and
        // "NIKON CORPORATION" + "NIKON D750" is just "NIKON D750".
        const QString makeWord = make.section(QLatin1Char(' '), 0, 0);
        if (!model.isEmpty() && !makeWord.isEmpty() && model.startsWith(makeWord, Qt::CaseInsensitive))
            info.camera = model;
        else
            info.camera = QStringList{make, model}.join(QLatin1Char(' ')).trimmed();
        info.lens = str("Exif.Photo.LensModel");
        QString when = str("Exif.Photo.DateTimeOriginal");
        if (when.isEmpty())
            when = str("Exif.Image.DateTime");
        info.taken = QDateTime::fromString(when, QStringLiteral("yyyy:MM:dd HH:mm:ss"));
        info.fNumber = num("Exif.Photo.FNumber");
        info.iso = int(num("Exif.Photo.ISOSpeedRatings"));
        info.focalLength = num("Exif.Photo.FocalLength");
        const auto exp = exif.findKey(Exiv2::ExifKey("Exif.Photo.ExposureTime"));
        if (exp != exif.end() && exp->count() > 0) {
            const Exiv2::Rational r = exp->toRational(0);
            if (r.first > 0 && r.second > 0) {
                if (r.first < r.second) {
                    const int denom = int(std::lround(double(r.second) / r.first));
                    info.exposure = QStringLiteral("1/%1 s").arg(denom);
                } else {
                    info.exposure = QStringLiteral("%1 s").arg(double(r.first) / r.second, 0, 'g', 3);
                }
            }
        }
    } catch (const std::exception &) {
        // Not a format Exiv2 knows, or a damaged file: leave what Qt found.
    }
}
#endif

} // namespace

MediaInfo::Kind kindFor(const QString &name)
{
    const QString ext = FileKind::extensionOf(name);
    if (ext.isEmpty())
        return MediaInfo::Kind::None;
    if (audioExtensions().contains(ext))
        return MediaInfo::Kind::Audio;
    if (imageExtensions().contains(ext))
        return MediaInfo::Kind::Image;
    if (videoExtensions().contains(ext))
        return MediaInfo::Kind::Video;
    return MediaInfo::Kind::None;
}

bool haveTagLib()
{
#ifdef LINOPUS_HAVE_TAGLIB
    return true;
#else
    return false;
#endif
}

bool haveExiv2()
{
#ifdef LINOPUS_HAVE_EXIV2
    return true;
#else
    return false;
#endif
}

MediaInfo readSid(const QString &path)
{
    MediaInfo info;
    info.kind = MediaInfo::Kind::Audio;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return info;
    const QByteArray h = f.read(0x76);
    if (h.size() < 0x76 || !(h.startsWith("PSID") || h.startsWith("RSID")))
        return info;
    // Three 32-byte Latin-1 strings: name, author, released ("1986 Thalamus").
    auto field = [&h](int at) { return QString::fromLatin1(h.mid(at, 32).constData()).trimmed(); };
    auto known = [](const QString &s) { return s == QLatin1String("<?>") ? QString() : s; };
    info.title = known(field(0x16));
    info.artist = known(field(0x36));
    const QString released = known(field(0x56));
    info.album = released;
    static const QRegularExpression yearRe(QStringLiteral("^(19[0-9]{2}|20[0-9]{2})"));
    const auto m = yearRe.match(released);
    if (m.hasMatch())
        info.year = m.captured(1).toInt();
    const int songs = (uchar(h.at(0x0e)) << 8) | uchar(h.at(0x0f));
    const int start = (uchar(h.at(0x10)) << 8) | uchar(h.at(0x11));
    if (songs > 0)
        info.track = qBound(1, start, songs);
    info.genre = QStringLiteral("C64 SID");
    return info;
}

MediaInfo read(const QString &path)
{
    MediaInfo info;
    info.kind = kindFor(path);
    switch (info.kind) {
    case MediaInfo::Kind::None:
        break;
    case MediaInfo::Kind::Audio: {
        const QString ext = FileKind::extensionOf(path);
        if (ext == QLatin1String("sid") || ext == QLatin1String("psid"))
            return readSid(path);
#ifdef LINOPUS_HAVE_TAGLIB
        readTagLib(path, info);
#endif
        break;
    }
    case MediaInfo::Kind::Video:
#ifdef LINOPUS_HAVE_TAGLIB
        readTagLib(path, info); // MP4 and friends: length, title
#endif
        break;
    case MediaInfo::Kind::Image: {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize size = reader.size();
        if (size.isValid()) {
            if (reader.transformation() & QImageIOHandler::TransformationRotate90)
                size.transpose();
            info.width = size.width();
            info.height = size.height();
        }
#ifdef LINOPUS_HAVE_EXIV2
        readExiv2(path, info);
#endif
        break;
    }
    }
    return info;
}

} // namespace Metadata

// ---- Cache -----------------------------------------------------------------

namespace {
MetadataCache *g_cache = nullptr;
}

MetadataCache::MetadataCache(QObject *parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    m_pool.setMaxThreadCount(2); // disk-bound; an SD card does not like more
}

MetadataCache::~MetadataCache()
{
    *m_cancel = true;
    m_pool.clear();
    m_pool.waitForDone();
    if (g_cache == this)
        g_cache = nullptr;
}

MetadataCache *MetadataCache::instance()
{
    if (!g_cache)
        g_cache = new MetadataCache(qApp);
    return g_cache;
}

bool MetadataCache::lookup(const QString &path, qint64 size, const QDateTime &modified, MediaInfo *out)
{
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(path);
        if (it != m_cache.constEnd() && it->size == size && it->modified == modified) {
            if (out)
                *out = it->info;
            return true;
        }
        if (m_pending.contains(path))
            return false;
        m_pending.insert(path);
    }
    QPointer<MetadataCache> self(this);
    auto cancel = m_cancel;
    m_pool.start([self, path, size, modified, cancel]() {
        if (*cancel)
            return;
        const MediaInfo info = Metadata::read(path);
        if (*cancel)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [self, path, size, modified, info]() {
                if (!self)
                    return;
                {
                    QMutexLocker lock(&self->m_mutex);
                    if (!self->m_pending.remove(path))
                        return; // cleared meanwhile
                    self->m_cache.insert(path, Entry{info, size, modified});
                }
                Q_EMIT self->ready(path);
            },
            Qt::QueuedConnection);
    });
    return false;
}

bool MetadataCache::isPending(const QString &path) const
{
    QMutexLocker lock(&m_mutex);
    return m_pending.contains(path);
}

void MetadataCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_pending.clear();
}
