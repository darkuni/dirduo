#include "core/Thumbnails.h"

#include "core/FileKind.h"
#include "core/Paths.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

namespace Thumbnails {

namespace {

QString bucket(int size)
{
    return size > kLarge ? QStringLiteral("x-large") : size > 128 ? QStringLiteral("large") : QStringLiteral("normal");
}

QString uriFor(const QString &path)
{
    return QString::fromLatin1(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toEncoded());
}

} // namespace

QString cacheFileFor(const QString &path, int size)
{
    const QByteArray md5 = QCryptographicHash::hash(uriFor(path).toUtf8(), QCryptographicHash::Md5).toHex();
    return Paths::userCacheHome() + QStringLiteral("/thumbnails/") + bucket(size) + QLatin1Char('/') + QString::fromLatin1(md5)
        + QStringLiteral(".png");
}

QImage readCached(const QString &path, const QDateTime &modified, int size)
{
    // A bigger bucket will do too: it is scaled down for showing anyway.
    for (int s : {size, kXLarge}) {
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

QImage make(const QString &path, int size)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid() && (full.width() > size || full.height() > size))
        reader.setScaledSize(full.scaled(size, size, Qt::KeepAspectRatio)); // JPEGs decode at the small size: fast
    QImage img = reader.read();
    if (img.isNull())
        return {};
    if (img.width() > size || img.height() > size)
        img = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB32)
        img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    return img;
}

bool writeCached(const QString &path, const QDateTime &modified, int size, const QImage &thumb)
{
    const QString file = cacheFileFor(path, size);
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
        return false;
    QFile::setPermissions(QFileInfo(file).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QImage img = thumb;
    img.setText(QStringLiteral("Thumb::URI"), uriFor(path));
    img.setText(QStringLiteral("Thumb::MTime"), QString::number(modified.toSecsSinceEpoch()));
    img.setText(QStringLiteral("Software"), QStringLiteral("Linopus"));
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

QImage load(const QString &path, const QDateTime &modified, int size)
{
    QImage img = readCached(path, modified, size);
    if (!img.isNull())
        return img;
    if (!canMake(path))
        return {};
    img = make(path, size);
    // Tiny pictures are their own thumbnail; the spec says not to cache those.
    const QSize full = QImageReader(path).size();
    if (!img.isNull() && !(full.isValid() && full.width() <= size && full.height() <= size))
        writeCached(path, modified, size, img);
    return img;
}

} // namespace Thumbnails

// ---- Cache -----------------------------------------------------------------

namespace {
ThumbnailCache *g_thumbs = nullptr;
}

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    m_cache.setMaxCost(160 * 1024); // about 1000 thumbnails at 256 px
    m_pool.setMaxThreadCount(2);
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

bool ThumbnailCache::lookup(const QString &path, const QDateTime &modified, QImage *out)
{
    {
        QMutexLocker lock(&m_mutex);
        if (Entry *e = m_cache.object(path); e && e->modified == modified) {
            if (out)
                *out = e->image;
            return true;
        }
        if (m_pending.contains(path))
            return false;
        m_pending.insert(path);
    }
    QPointer<ThumbnailCache> self(this);
    auto cancel = m_cancel;
    m_pool.start([self, path, modified, cancel]() {
        if (*cancel)
            return;
        const QImage img = Thumbnails::load(path, modified, Thumbnails::kLarge);
        if (*cancel)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [self, path, modified, img]() {
                if (!self)
                    return;
                {
                    QMutexLocker lock(&self->m_mutex);
                    if (!self->m_pending.remove(path))
                        return;
                    const int cost = qMax(1, int(img.sizeInBytes() / 1024));
                    self->m_cache.insert(path, new Entry{img, modified}, cost);
                }
                Q_EMIT self->ready(path);
            },
            Qt::QueuedConnection);
    });
    return false;
}

void ThumbnailCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_pending.clear();
}
