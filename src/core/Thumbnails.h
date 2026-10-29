#pragma once

#include <QCache>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

// Thumbnails for the icon and thumbnail views. Shares the freedesktop.org
// thumbnail cache (~/.cache/thumbnails) with Dolphin, Gwenview and the rest:
// anything they have already made shows up at once, and what DirDuo makes
// itself (pictures, album art, PDF pages, video frames) is saved there for
// them too.
namespace Thumbnails {

constexpr int kNormal = 128;  // "normal" in the spec
constexpr int kLarge = 256;   // "large"
constexpr int kXLarge = 512;  // "x-large"

// The cache bucket to ask for to show this many pixels sharply.
int bucketFor(int pixels);

enum class Kind { None, Picture, Video, Document, Audio };
// By extension.
Kind kindOf(const QString &name);

// What gets a thumbnail besides pictures (Preferences > Thumbnails).
struct Options
{
    bool video = true;
    bool documents = true;
    bool covers = true;
};

// The spec's cache file for a path: <cache>/thumbnails/<bucket>/<md5 of URI>.png
QString cacheFileFor(const QString &path, int size);
// Reads a cached thumbnail if it belongs to this version of the file; full
// gets the original's size when the cache file says it.
QImage readCached(const QString &path, const QDateTime &modified, int size, QSize *full = nullptr);
// Makes one from a picture (any format Qt reads), turned the way its EXIF
// says; null when it is not one.
QImage make(const QString &path, int size, QSize *full = nullptr);
// Saves it the way the spec wants (PNG with Thumb::URI and Thumb::MTime).
bool writeCached(const QString &path, const QDateTime &modified, int size, const QImage &thumb, const QSize &full = {});
// Cached, or made (and cached): pictures, album art, and documents when a
// document maker is set. Videos only come from the cache here (the video
// maker runs on the main thread; ThumbnailCache uses it).
QImage load(const QString &path, const QDateTime &modified, int size, const Options &options = {}, QSize *full = nullptr);
// Up to count pictures from inside a folder, side by side in one image of
// size x size (transparent around them); null when it has none.
QImage folderCollage(const QString &dir, int size, bool newest, int count = 4);

bool canMake(const QString &name); // a picture Qt can read, by extension

// Documents (PDF): called from worker threads, must be safe there.
using Maker = std::function<QImage(const QString &path, int size)>;
void setDocumentMaker(Maker maker);
bool hasDocumentMaker();
// Videos: called on the main thread; answers later through done (a null
// image when it could not).
using AsyncMaker = std::function<void(const QString &path, int size, std::function<void(const QImage &)> done)>;
void setVideoMaker(AsyncMaker maker);
bool hasVideoMaker();

} // namespace Thumbnails

class ThumbnailCache : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailCache(QObject *parent = nullptr);
    ~ThumbnailCache() override;
    static ThumbnailCache *instance();

    struct Result
    {
        QImage image;  // null when the file has no thumbnail
        QSize full;    // the original's size when known
        bool sharp = true; // false while a bigger one is on its way
    };

    // In memory and current: true, with the result. Otherwise false, and
    // loading starts. pixels is what will be shown (device pixels); a
    // smaller one already in memory is returned while a bigger one is made.
    bool lookup(const QString &path, const QDateTime &modified, int pixels, Result *out);
    bool lookup(const QString &path, const QDateTime &modified, QImage *out); // 256 px
    // A folder's collage of the pictures inside.
    bool lookupFolder(const QString &dir, const QDateTime &modified, int pixels, bool newest, Result *out);

    void setOptions(const Thumbnails::Options &o);
    Thumbnails::Options options() const { return m_options; }
    void clear();
    int pendingCount() const;

Q_SIGNALS:
    void ready(const QString &path);

private:
    struct Entry
    {
        QImage image;
        QSize full;
        QDateTime modified;
        int bucket = 0;
    };
    void start(const QString &key, const QString &path, const QDateTime &modified, int bucket, bool folder, bool newest);
    void store(const QString &key, const QString &path, const Entry &e);
    void queueVideo(const QString &key, const QString &path, const QDateTime &modified);
    void nextVideo();

    mutable QMutex m_mutex;
    QCache<QString, Entry> m_cache; // cost in KB
    QSet<QString> m_pending;        // key + bucket
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    Thumbnails::Options m_options;
    struct VideoJob
    {
        QString key, path;
        QDateTime modified;
    };
    QQueue<VideoJob> m_videos;
    bool m_videoBusy = false;
};
