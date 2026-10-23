#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

// File checksums: CRC32 (SFV), MD5, SHA-1, SHA-256. Making them, reading
// and writing the files that list them (.sfv, .md5, .sha1, .sha256, in GNU
// "hash  name" or BSD "SHA256 (name) = hash" form), and checking files
// against those lists.
namespace Checksum {

enum class Algo { Crc32, Md5, Sha1, Sha256 };

QList<Algo> all();
bool fromName(const QString &name, Algo *out); // md5, sha1, sha-1, sha256, crc32, sfv
QString name(Algo a);                          // "md5"
QString label(Algo a);                         // "MD5"
QString extension(Algo a);                     // "md5" ("sfv" for CRC32)
int hexLength(Algo a);

// The checksum, lowercase hex (CRC32 uppercase, as SFV files have it). An
// empty string when the file could not be read or progress returned false.
// progress gets the bytes read so far.
QString compute(const QString &path, Algo a, const std::function<bool(qint64)> &progress = {}, QString *error = nullptr);
QString computeData(const QByteArray &data, Algo a);

// A checksum list by its name: .sfv, .md5, .sha1, .sha256 (and .sha256sum...).
bool isListFile(const QString &name);

struct Entry
{
    QString name; // as written in the list, relative to the list's folder
    QString hash; // lowercase (CRC32 uppercase)
    Algo algo = Algo::Md5;
};
// Reads a list. The algorithm comes from the file's extension, a BSD line's
// tag, or the length of the hash.
QList<Entry> parse(const QString &text, const QString &fileName);
QList<Entry> readList(const QString &path, QString *error = nullptr);
// A list's text: "hash  name" lines (SFV: "name HASH" under a comment).
QString format(const QList<Entry> &entries, Algo a);

enum class Result { Ok, Bad, Missing, Unreadable };
QString resultText(Result r);

} // namespace Checksum

// Checksums for the columns: made in the background, remembered while the
// file stays the same size and date.
class ChecksumCache : public QObject
{
    Q_OBJECT
public:
    explicit ChecksumCache(QObject *parent = nullptr);
    ~ChecksumCache() override;
    static ChecksumCache *instance();

    bool lookup(const QString &path, Checksum::Algo a, qint64 size, const QDateTime &modified, QString *out);
    void clear();

Q_SIGNALS:
    void ready(const QString &path);

private:
    struct Entry
    {
        QString hash;
        qint64 size = -1;
        QDateTime modified;
    };
    static QString key(const QString &path, Checksum::Algo a);
    mutable QMutex m_mutex;
    QHash<QString, Entry> m_cache;
    QSet<QString> m_pending;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
