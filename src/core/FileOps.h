#pragma once

#include <QString>
#include <QStringList>

// Small synchronous filesystem helpers. Anything that can take a while goes
// through the JobManager instead.
namespace FileOps {

// Device number of the filesystem holding path (or its nearest existing
// parent). 0 if nothing along the path exists.
quint64 deviceId(const QString &path);

bool sameDevice(const QString &a, const QString &b);
// Room left for the user on the drive holding path, and its size; -1 when
// unknown (a server, a path that is gone).
qint64 bytesFree(const QString &path);
qint64 bytesTotal(const QString &path);
// Tests: pretend every drive has this much free (-1: the real figure).
void setBytesFreeOverride(qint64 bytes);

// True if child is parent itself or somewhere inside it.
bool isSameOrInside(const QString &child, const QString &parent);

// A name that does not exist in dir yet: "photo.jpg" -> "photo (2).jpg".
QString uniqueName(const QString &dir, const QString &name);

// Rejects empty names, "." / "..", and names containing '/' or NUL.
bool isValidFileName(const QString &name, QString *error = nullptr);

// Renames in place. Never overwrites an existing entry.
bool renameEntry(const QString &path, const QString &newName, QString *newPath, QString *error);

// "A, B,C" -> creates A, B and C in parent. Existing folders are left alone and
// still returned so they can be selected. Returns full paths.
QStringList createFolders(const QString &parent, const QString &commaSeparatedNames, QString *error);

// Natural order: "audio2" before "audio10", case-insensitive, locale-aware
// for the text between numbers. Negative, zero or positive like strcmp.
int naturalCompare(const QString &a, const QString &b);

// "1.5 MB" style, base 1024 with one decimal like Opus.
QString formatSize(qint64 bytes);

} // namespace FileOps
