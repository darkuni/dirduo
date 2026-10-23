#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>

// Creating and extracting archives with libarchive: zip, 7z and tar (plain,
// gz, xz, bz2, zstd) both ways; RAR, ISO, LHA/LZH, CAB, cpio and more to
// extract. Used by the Pack and Unpack jobs; safe from any thread.
namespace Archive {

enum class Format { Zip, SevenZip, Tar, TarGz, TarXz, TarBz2, TarZst };

bool available(); // built with libarchive

// From a file name: "x.tar.gz" -> TarGz. False when it is not one we write.
bool formatFromName(const QString &fileName, Format &out);
bool formatFromKey(const QString &key, Format &out); // "zip", "7z", "tar.gz"...
QString extensionFor(Format f);                      // "zip", "tar.gz"...
QString displayName(Format f);                       // "Zip", "7-Zip", "tar.gz"...
QList<Format> formats();

// Something we can extract (by name).
bool isArchive(const QString &path);
// "Games.tar.gz" -> "Games": the name of a folder to extract into.
QString baseName(const QString &path);

// The first path component of every entry, in order, without repeats:
// one name means the archive holds a single folder (or file).
QStringList topLevelNames(const QString &archive, QString *error);

struct Hooks
{
    std::function<bool()> keepGoing;                              // false: stop (canceled)
    std::function<void(const QString &item, qint64 size)> current; // starting this entry
    std::function<void(qint64 bytes)> advanced;                    // progress since the last call
    std::function<void(const QString &message)> problem;           // one entry failed; carries on
};

// Writes sources (files and folders, recursively; symlinks as symlinks) into
// a new archive at path. Entry names start at each source's own name.
bool pack(const QStringList &sources, const QString &path, Format format, const Hooks &hooks, QString *error);

// Extracts into destDir. A top-level name can be renamed on the way
// (rename["Game"] = "Game (2)"); with replace, files already there are
// overwritten, otherwise they are kept and reported. Never writes outside
// destDir: "../" and absolute names are refused. created receives the
// top-level items made (for undo).
bool unpack(const QString &archive, const QString &destDir, const QMap<QString, QString> &rename, bool replace,
            const Hooks &hooks, QStringList *created, QString *error);

} // namespace Archive
