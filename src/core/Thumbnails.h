#pragma once

#include <QCache>
#include <QDateTime>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <atomic>
#include <memory>

// Thumbnails for the icon and thumbnail views. Shares the freedesktop.org
// thumbnail cache (~/.cache/thumbnails) with Dolphin, Gwenview and the rest:
// anything they have already made, videos and documents included, shows up
// at once, and pictures Linopus makes itself are saved there for them too.
namespace Thumbnails {

constexpr int kLarge = 256;   // "large" in the spec
constexpr int kXLarge = 512;  // "x-large"

// The spec's cache file for a path: <cache>/thumbnails/<bucket>/<md5 of URI>.png
QString cacheFileFor(const QString &path, int size);
// Reads a cached thumbnail if it belongs to this version of the file.
QImage readCached(const QString &path, const QDateTime &modified, int size);
// Makes one from a picture (any format Qt reads); null when it is not one.
QImage make(const QString &path, int size);
// Saves it the way the spec wants (PNG with Thumb::URI and Thumb::MTime).
bool writeCached(const QString &path, const QDateTime &modified, int size, const QImage &thumb);
// Cached, or made (and cached) when the file is a picture.
QImage load(const QString &path, const QDateTime &modified, int size);

bool canMake(const QString &name); // a picture Qt can read, by extension

} // namespace Thumbnails

class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject *parent = nullptr);
    ~ThumbnailCache() override;
    static ThumbnailCache *instance();

    // In memory and current: true, with the image (null when the file has no
    // thumbnail). Otherwise false, and loading starts.
    bool lookup(const QString &path, const QDateTime &modified, QImage *out);
    void clear();

Q_SIGNALS:
    void ready(const QString &path);

private:
    struct Entry
    {
        QImage image;
        QDateTime modified;
    };
    QMutex m_mutex;
    QCache<QString, Entry> m_cache; // cost in KB
    QSet<QString> m_pending;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
