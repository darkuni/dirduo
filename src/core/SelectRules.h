#pragma once

#include "core/FileItem.h"
#include "core/FileKind.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QString>

// Select by what files are, and by how they compare with the other display:
// what Opus does with Select NEWER and friends, and its Select dialog.
struct SelectRules
{
    enum class Compare { None, Newer, Older, Missing, Same, Different, NewerOrMissing };
    enum class Type { Both, Files, Folders };

    QString pattern;  // wildcards, ; between several; empty: any name
    bool hasKind = false;
    FileKind::Category kind = FileKind::Category::Other;
    Type type = Type::Both;
    qint64 minSize = -1; // files only
    qint64 maxSize = -1;
    QDateTime newerThan; // modified after
    QDateTime olderThan; // modified before
    Compare compare = Compare::None;
    // Labels, ratings and tags.
    QString label;       // a label name; "*" any label; "none" no label
    int minRating = -1;  // at least this many stars (0: not rated only when maxRating is 0)
    int maxRating = -1;
    QString tag;         // has this tag (case-insensitive); "none" no tags
    QString otherRoot; // the other display's folder, for compare
    QString thisRoot;  // this display's folder

    static bool compareFromName(const QString &name, Compare &out);
    static QString compareName(Compare c);
    // Dates within this many seconds are the same (FAT on SD cards keeps 2 s).
    static constexpr int kDateSlackSecs = 2;

    bool isEmpty() const;
    bool matches(const FileItem &item) const;
    // Where the same item would be in the other display ("" if outside root).
    QString counterpart(const QString &path) const;
    // How an item stands against its counterpart (compare must be set).
    Compare classify(const FileItem &item) const;
};
