#pragma once

#include <QDateTime>
#include <QImage>
#include <QVariantMap>
#include <QStringList>

#include <optional>
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
    QString comment;

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
    QString description; // EXIF image description
    QString copyright;   // (a photo's photographer goes in artist)

    bool isEmpty() const;
    QString durationText() const;   // "3:42", "1:02:05"
    QString dimensionsText() const; // "4000 × 3000"
    QString summary() const;        // a few lines for the viewer pane
};

// Changes to make to a file's tags: only the fields that are set are
// written; an empty string or 0 clears that field.
struct MediaEdit
{
    std::optional<QString> title, artist, albumArtist, album, genre, comment; // music (artist also photos)
    std::optional<int> year, track, disc;
    std::optional<QDateTime> taken; // photos; invalid clears it
    std::optional<QString> description, copyright;

    bool isEmpty() const;
    // The fields this edit sets, as they are in info now (for undo).
    MediaEdit currentValues(const MediaInfo &info) const;
    QVariantMap toMap() const;
    static MediaEdit fromMap(const QVariantMap &m);
    QStringList fieldNames() const; // "Artist", "Album"... for messages
};

namespace Metadata {

// Decided by extension only.
MediaInfo::Kind kindFor(const QString &name);
bool haveTagLib();
bool haveExiv2();

// Reads a file now (blocking). Safe from any thread.
MediaInfo read(const QString &path);
MediaInfo readSid(const QString &path);

// Whether tags can be written to this file here (by extension, and what this
// build has): MP3, FLAC, Ogg, Opus, M4A, WAV, AIFF... with TagLib; JPEG,
// PNG, WebP and TIFF with Exiv2.
bool canWrite(const QString &path);
// Writes the fields that are set. Music fields to music files, photo fields
// to pictures (artist goes to both); the rest is left alone.
bool write(const QString &path, const MediaEdit &edit, QString *error);
// The picture embedded in a music file (ID3v2 APIC, FLAC and Ogg pictures, MP4
// covr), front cover first; else cover.jpg, folder.jpg... next to it. Null
// when there is none. Safe from any thread.
QImage coverArt(const QString &path);
// "+1h", "-30m", "+2d", "+90s", "-1:30" (hours:minutes): seconds, or false.
bool parseShift(const QString &text, qint64 *seconds);

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
