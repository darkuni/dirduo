#pragma once

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>

// What a music file or a photo says about itself: tags, length, size in
// pixels, camera. Music through TagLib, photos through Exiv2 (both optional at
// build time; without Exiv2 photos still get their size). C64 SID tunes are
// read directly: name, author and release line from the header.
struct MediaInfo
{
    enum class Kind { None, Audio, Video, Image };
    Kind kind = Kind::None;

    // Music
    QString title;
    QString artist;
    QString albumArtist;
    QString album;
    QString genre;
    int year = 0;
    int track = 0;
    int disc = 0;
    int seconds = -1; // length; also for video
    int bitrate = 0;  // kbit/s
    int sampleRate = 0;
    int channels = 0;

    // Pictures
    int width = 0;
    int height = 0;
    QString camera; // "Canon EOS R6", without the maker twice
    QString lens;
    QDateTime taken;
    double fNumber = 0;
    QString exposure; // "1/250 s"
    int iso = 0;
    double focalLength = 0; // mm

    bool isEmpty() const;
    QString durationText() const;   // "3:42", "1:02:05"
    QString dimensionsText() const; // "4000 × 3000"
    QString summary() const;        // a few lines for the viewer pane
};

namespace Metadata {

// Decided by extension only.
MediaInfo::Kind kindFor(const QString &name);
bool haveTagLib();
bool haveExiv2();

// Reads a file now (blocking). Safe from any thread.
MediaInfo read(const QString &path);
MediaInfo readSid(const QString &path);

} // namespace Metadata

// Reads metadata in the background and remembers it while browsing. A file
// that changes (size or modified time) is read again.
class MetadataCache : public QObject
{
    Q_OBJECT
public:
    explicit MetadataCache(QObject *parent = nullptr);
    ~MetadataCache() override;
    static MetadataCache *instance();

    // Cached and current: true, with the info. Otherwise false, and a read is
    // started unless one is under way.
    bool lookup(const QString &path, qint64 size, const QDateTime &modified, MediaInfo *out);
    bool isPending(const QString &path) const;
    void clear();

Q_SIGNALS:
    void ready(const QString &path);

private:
    struct Entry
    {
        MediaInfo info;
        qint64 size = -1;
        QDateTime modified;
    };
    mutable QMutex m_mutex;
    QHash<QString, Entry> m_cache;
    QSet<QString> m_pending;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
