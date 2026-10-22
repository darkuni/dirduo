#pragma once

#include "core/FolderSync.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

// What Find looks for. Everything left empty matches anything.
struct FindCriteria
{
    enum class Type { Files, Folders, Both };
    QString name;              // wildcards, ';' between several; or a regular expression
    bool nameRegex = false;
    bool caseSensitive = false;
    QString contains;          // text inside files
    qint64 minSize = -1;
    qint64 maxSize = -1;
    QDateTime newerThan;       // modified after
    QDateTime olderThan;       // modified before
    Type type = Type::Files;
    bool includeHidden = false;
    bool recurse = true;

    bool isEmpty() const;
    // "10M", "500K", "2G", "1234" -> bytes; -1 when not a size.
    static qint64 parseSize(const QString &text);
    // "7" (days ago), "2026-10-01" -> date; invalid when neither.
    static QDateTime parseWhen(const QString &text);
};

namespace FindEngine {

// Every match under the roots, in folder order. Stops at maxResults.
QStringList find(const QStringList &roots, const FindCriteria &criteria, ScanProgress *progress = nullptr,
                 int maxResults = 100000, bool *truncated = nullptr);

// True when the file holds the text (read in pieces, so any size works).
bool fileContains(const QString &path, const QString &text, bool caseSensitive, const std::atomic<bool> *cancel = nullptr);

} // namespace FindEngine

// Saved searches, by name, in find.ini.
namespace FindPresets {
QStringList names(const QString &file = {});
bool load(const QString &name, FindCriteria &out, QStringList *roots = nullptr, const QString &file = {});
bool save(const QString &name, const FindCriteria &criteria, const QStringList &roots, const QString &file = {});
bool remove(const QString &name, const QString &file = {});
} // namespace FindPresets
