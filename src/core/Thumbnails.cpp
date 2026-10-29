#include "core/Thumbnails.h"

#include "core/FileKind.h"
#include "core/Metadata.h"
#include "core/Paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QUrl>

#include <mutex>

namespace Thumbnails {

namespace {

std::mutex g_makersMutex;
Maker g_documentMaker;
AsyncMaker g_videoMaker;

QString bucket(int size)
{
    return size > kLarge ? QStringLiteral("x-large") : size > kNormal ? QStringLiteral("large") : QStringLiteral("normal");
}

QString uriFor(const QString &path)
{
    return QString::fromLatin1(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toEncoded());
}

QImage normalized(QImage img, int size)
{
    if (img.isNull())
        return {};
    if (img.width() > size || img.height() > size)
        img = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB32)
        img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    return img;
}

Maker documentMaker()
{
    std::lock_guard<std::mutex> lock(g_makersMutex);
    return g_documentMaker;
}

} // namespace

int bucketFor(int pixels)
{
    return pixels > kLarge ? kXLarge : kLarge;
}

Kind kindOf(const QString &name)
{
    if (canMake(name))
        return Kind::Picture;
    const QString ext = FileKind::extensionOf(name);
    if (ext == QLatin1String("pdf"))
        return Kind::Document;
    switch (FileKind::categoryFor(name, false)) {
    case FileKind::Category::Video:
        return Kind::Video;
    case FileKind::Category::Audio:
        return Kind::Audio;
    default:
        return Kind::None;
    }
}

QString cacheFileFor(const QString &path, int size)
{
    const QByteArray md5 = QCryptographicHash::hash(uriFor(path).toUtf8(), QCryptographicHash::Md5).toHex();
    return Paths::userCacheHome() + QStringLiteral("/thumbnails/") + bucket(size) + QLatin1Char('/') + QString::fromLatin1(md5)
        + QStringLiteral(".png");
}

QImage readCached(const QString &path, const QDateTime &modified, int size, QSize *full)
{
    // A bigger bucket will do too: it is scaled down for showing anyway.
    QList<int> sizes{size};
    for (int s : {kLarge, kXLarge}) {
        if (s > size)
            sizes << s;
    }
    for (int s : std::as_const(sizes)) {
        const QString file = cacheFileFor(path, s);
        if (!QFileInfo::exists(file))
            continue;
        QImage img(file);
        if (img.isNull())
            continue;
        const QString mtime = img.text(QStringLiteral("Thumb::MTime"));
        if (!mtime.isEmpty() && mtime.toLongLong() != modified.toSecsSinceEpoch())
            continue; // made for an older version of the file
        if (img.text(QStringLiteral("Thumb::URI")) != uriFor(path))
            continue;
        if (full) {
            const int w = img.text(QStringLiteral("Thumb::Image::Width")).toInt();
            const int h = img.text(QStringLiteral("Thumb::Image::Height")).toInt();
            *full = w > 0 && h > 0 ? QSize(w, h) : QSize();
        }
        return img;
    }
    return {};
}

bool canMake(const QString &name)
{
    static const QSet<QString> formats = []() {
        QSet<QString> s;
        for (const QByteArray &f : QImageReader::supportedImageFormats())
            s.insert(QString::fromLatin1(f).toLower());
        s.remove(QStringLiteral("svg")); // drawn, not decoded; the icon does better
        s.remove(QStringLiteral("svgz"));
        s.remove(QStringLiteral("pdf"));
        return s;
    }();
    return formats.contains(FileKind::extensionOf(name));
}

QImage make(const QString &path, int size, QSize *full)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // upright, the way the camera's EXIF says
    QSize whole = reader.size();
    if (whole.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90))
        whole.transpose();
    if (full)
        *full = whole;
    if (whole.isValid() && (whole.width() > size || whole.height() > size)) {
        QSize scaled = whole.scaled(size, size, Qt::KeepAspectRatio);
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            scaled.transpose(); // the reader scales before it turns
        reader.setScaledSize(scaled); // JPEGs decode at the small size: fast
    }
    QImage img = reader.read();
    if (full && !full->isValid() && !img.isNull())
        *full = img.size();
    return normalized(img, size);
}

bool writeCached(const QString &path, const QDateTime &modified, int size, const QImage &thumb, const QSize &full)
{
    const QString file = cacheFileFor(path, size);
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
        return false;
    QFile::setPermissions(QFileInfo(file).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QImage img = thumb;
    img.setText(QStringLiteral("Thumb::URI"), uriFor(path));
    img.setText(QStringLiteral("Thumb::MTime"), QString::number(modified.toSecsSinceEpoch()));
    if (full.isValid()) {
        img.setText(QStringLiteral("Thumb::Image::Width"), QString::number(full.width()));
        img.setText(QStringLiteral("Thumb::Image::Height"), QString::number(full.height()));
    }
    img.setText(QStringLiteral("Software"), QStringLiteral("DirDuo"));
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    QImageWriter w(&out, "png");
    if (!w.write(img)) {
        out.cancelWriting();
        return false;
    }
    if (!out.commit())
        return false;
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QImage load(const QString &path, const QDateTime &modified, int size, const Options &options, QSize *full)
{
    QImage img = readCached(path, modified, size, full);
    if (!img.isNull())
        return img;
    const Kind kind = kindOf(path);
    QSize whole;
    switch (kind) {
    case Kind::Picture:
        img = make(path, size, &whole);
        // Tiny pictures are their own thumbnail; the spec says not to cache those.
        if (!img.isNull() && !(whole.isValid() && whole.width() <= size && whole.height() <= size))
            writeCached(path, modified, size, img, whole);
        break;
    case Kind::Audio:
        if (!options.covers)
            return {};
        img = Metadata::coverArt(path);
        if (!img.isNull()) {
            whole = img.size();
            img = normalized(img, size);
            writeCached(path, modified, size, img, whole);
        }
        break;
    case Kind::Document:
        if (!options.documents)
            return {};
        if (Maker m = documentMaker()) {
            img = normalized(m(path, size), size);
            if (!img.isNull())
                writeCached(path, modified, size, img);
        }
        break;
    case Kind::Video:
    case Kind::None:
        return {};
    }
    if (full)
        *full = whole;
    return img;
}

QImage folderCollage(const QString &dir, int size, bool newest, int count)
{
    QDir d(dir);
    const QFileInfoList entries =
        d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, newest ? QDir::Time : (QDir::Name | QDir::IgnoreCase));
    QList<QImage> pictures;
    const int cell = size / 2;
    int looked = 0;
    for (const QFileInfo &fi : entries) {
        if (pictures.size() >= count || ++looked > 400)
            break;
        if (!canMake(fi.fileName()) || fi.fileName().startsWith(QLatin1Char('.')))
            continue;
        QImage img = readCached(fi.absoluteFilePath(), fi.lastModified(), kLarge);
        if (img.isNull())
            img = make(fi.absoluteFilePath(), pictures.isEmpty() && count == 1 ? size : qMax(cell, kNormal));
        if (!img.isNull())
            pictures << img;
    }
    if (pictures.isEmpty())
        return {};
    QImage canvas(size, size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    const int gap = qMax(2, size / 48);
    QList<QRect> cells;
    if (pictures.size() == 1) {
        cells << QRect(0, 0, size, size);
    } else if (pictures.size() == 2) {
        const int w = (size - gap) / 2;
        cells << QRect(0, size / 4, w, size / 2) << QRect(w + gap, size / 4, w, size / 2);
    } else {
        const int w = (size - gap) / 2;
        cells << QRect(0, 0, w, w) << QRect(w + gap, 0, w, w) << QRect(0, w + gap, w, w) << QRect(w + gap, w + gap, w, w);
    }
    for (int i = 0; i < pictures.size() && i < cells.size(); ++i) {
        const QImage &img = pictures.at(i);
        const QRect c = cells.at(i);
        // Each fills its cell, cropped from the middle.
        const QSize fill = img.size().scaled(c.size(), Qt::KeepAspectRatioByExpanding);
        const QRect src((fill.width() - c.width()) / 2 * img.width() / qMax(1, fill.width()),
                        (fill.height() - c.height()) / 2 * img.height() / qMax(1, fill.height()),
                        c.width() * img.width() / qMax(1, fill.width()), c.height() * img.height() / qMax(1, fill.height()));
        QPainterPath clip;
        clip.addRoundedRect(QRectF(c), size / 40.0, size / 40.0);
        p.save();
        p.setClipPath(clip);
        p.drawImage(c, img, src);
        p.restore();
    }
    p.end();
    return canvas;
}

void setDocumentMaker(Maker maker)
{
    std::lock_guard<std::mutex> lock(g_makersMutex);
    g_documentMaker = std::move(maker);
}

bool hasDocumentMaker()
{
    std::lock_guard<std::mutex> lock(g_makersMutex);
    return bool(g_documentMaker);
}

void setVideoMaker(AsyncMaker maker)
{
    std::lock_guard<std::mutex> lock(g_makersMutex);
    g_videoMaker = std::move(maker);
}

bool hasVideoMaker()
{
    std::lock_guard<std::mutex> lock(g_makersMutex);
    return bool(g_videoMaker);
}

} // namespace Thumbnails

// ---- Cache -----------------------------------------------------------------

namespace {
ThumbnailCache *g_thumbs = nullptr;

QString folderKey(const QString &dir, bool newest)
{
    return QStringLiteral("\x01") + (newest ? QStringLiteral("n") : QStringLiteral("a")) + dir;
}
} // namespace

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    m_cache.setMaxCost(256 * 1024); // about 1600 thumbnails at 256 px
    m_pool.setMaxThreadCount(qBound(2, QThread::idealThreadCount() / 2, 4));
}

ThumbnailCache::~ThumbnailCache()
{
    *m_cancel = true;
    m_pool.clear();
    m_pool.waitForDone();
    if (g_thumbs == this)
        g_thumbs = nullptr;
}

ThumbnailCache *ThumbnailCache::instance()
{
    if (!g_thumbs)
        g_thumbs = new ThumbnailCache(qApp);
    return g_thumbs;
}

void ThumbnailCache::setOptions(const Thumbnails::Options &o)
{
    if (o.video == m_options.video && o.documents == m_options.documents && o.covers == m_options.covers)
        return;
    m_options = o;
    clear(); // what has (or has not) a thumbnail changes
}

int ThumbnailCache::pendingCount() const
{
    QMutexLocker lock(&m_mutex);
    return int(m_pending.size()) + int(m_videos.size()) + (m_videoBusy ? 1 : 0);
}

bool ThumbnailCache::lookup(const QString &path, const QDateTime &modified, QImage *out)
{
    Result r;
    const bool ok = lookup(path, modified, Thumbnails::kLarge, &r);
    if (ok && out)
        *out = r.image;
    return ok;
}

bool ThumbnailCache::lookup(const QString &path, const QDateTime &modified, int pixels, Result *out)
{
    const int want = Thumbnails::bucketFor(pixels);
    {
        QMutexLocker lock(&m_mutex);
        if (Entry *e = m_cache.object(path); e && e->modified == modified) {
            // Big enough, nothing to have, or a picture smaller than the bucket.
            const bool final = e->bucket >= want || e->image.isNull()
                || (e->full.isValid() && e->full.width() <= e->bucket && e->full.height() <= e->bucket);
            if (out) {
                out->image = e->image;
                out->full = e->full;
                out->sharp = final;
            }
            if (final)
                return true;
        } else if (out) {
            *out = Result{};
        }
        if (m_pending.contains(path + QString::number(want)))
            return m_cache.contains(path) && m_cache.object(path)->modified == modified;
        m_pending.insert(path + QString::number(want));
    }
    start(path, path, modified, want, false, false);
    QMutexLocker lock(&m_mutex);
    return m_cache.contains(path) && m_cache.object(path)->modified == modified;
}

bool ThumbnailCache::lookupFolder(const QString &dir, const QDateTime &modified, int pixels, bool newest, Result *out)
{
    const int want = Thumbnails::bucketFor(pixels);
    const QString key = folderKey(dir, newest);
    {
        QMutexLocker lock(&m_mutex);
        if (Entry *e = m_cache.object(key); e && e->modified == modified) {
            if (out) {
                out->image = e->image;
                out->full = {};
                out->sharp = e->bucket >= want || e->image.isNull();
            }
            if (e->bucket >= want || e->image.isNull())
                return true;
        } else if (out) {
            *out = Result{};
        }
        if (m_pending.contains(key + QString::number(want)))
            return m_cache.contains(key) && m_cache.object(key)->modified == modified;
        m_pending.insert(key + QString::number(want));
    }
    start(key, dir, modified, want, true, newest);
    QMutexLocker lock(&m_mutex);
    return m_cache.contains(key) && m_cache.object(key)->modified == modified;
}

void ThumbnailCache::store(const QString &key, const QString &path, const Entry &e)
{
    {
        QMutexLocker lock(&m_mutex);
        if (Entry *old = m_cache.object(key); old && old->modified == e.modified && old->bucket > e.bucket && !old->image.isNull())
            return; // a bigger one came first
        const int cost = qMax(1, int(e.image.sizeInBytes() / 1024));
        m_cache.insert(key, new Entry(e), cost);
    }
    Q_EMIT ready(path);
}

void ThumbnailCache::start(const QString &key, const QString &path, const QDateTime &modified, int bucket, bool folder, bool newest)
{
    QPointer<ThumbnailCache> self(this);
    auto cancel = m_cancel;
    const Thumbnails::Options options = m_options;
    m_pool.start([self, key, path, modified, bucket, folder, newest, cancel, options]() {
        if (*cancel)
            return;
        Entry e;
        e.modified = modified;
        e.bucket = bucket;
        bool video = false;
        if (folder) {
            e.image = Thumbnails::folderCollage(path, bucket, newest);
        } else {
            e.image = Thumbnails::load(path, modified, bucket, options, &e.full);
            video = e.image.isNull() && options.video && Thumbnails::kindOf(path) == Thumbnails::Kind::Video;
        }
        if (*cancel)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [self, key, path, modified, bucket, e, video]() {
                if (!self)
                    return;
                {
                    QMutexLocker lock(&self->m_mutex);
                    if (!self->m_pending.remove(key + QString::number(bucket)))
                        return; // cleared meanwhile
                }
                if (video && Thumbnails::hasVideoMaker()) {
                    self->queueVideo(key, path, modified);
                    return;
                }
                self->store(key, path, e);
            },
            Qt::QueuedConnection);
    });
}

void ThumbnailCache::queueVideo(const QString &key, const QString &path, const QDateTime &modified)
{
    {
        QMutexLocker lock(&m_mutex);
        for (const VideoJob &j : std::as_const(m_videos)) {
            if (j.key == key)
                return;
        }
        m_videos.enqueue(VideoJob{key, path, modified});
    }
    nextVideo();
}

void ThumbnailCache::nextVideo()
{
    VideoJob job;
    {
        QMutexLocker lock(&m_mutex);
        if (m_videoBusy || m_videos.isEmpty())
            return;
        job = m_videos.dequeue();
        m_videoBusy = true;
    }
    Thumbnails::AsyncMaker maker;
    {
        std::lock_guard<std::mutex> lock(Thumbnails::g_makersMutex);
        maker = Thumbnails::g_videoMaker;
    }
    QPointer<ThumbnailCache> self(this);
    auto done = [self, job](const QImage &frame) {
        if (!self)
            return;
        Entry e;
        e.modified = job.modified;
        e.bucket = Thumbnails::kXLarge;
        e.full = frame.size();
        e.image = frame.isNull() ? QImage() : frame.scaled(Thumbnails::kXLarge, Thumbnails::kXLarge, Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation);
        if (!e.image.isNull()) {
            const QImage img = e.image;
            const QString path = job.path;
            const QDateTime mtime = job.modified;
            const QSize full = e.full;
            self->m_pool.start([path, mtime, img, full]() { Thumbnails::writeCached(path, mtime, Thumbnails::kXLarge, img, full); });
        }
        {
            QMutexLocker lock(&self->m_mutex);
            self->m_videoBusy = false;
        }
        self->store(job.key, job.path, e);
        QMetaObject::invokeMethod(self, &ThumbnailCache::nextVideo, Qt::QueuedConnection);
    };
    if (!maker) {
        done(QImage());
        return;
    }
    maker(job.path, Thumbnails::kXLarge, done);
}

void ThumbnailCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_pending.clear();
    m_videos.clear();
}
