#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

// File attributes Opus calls Set Attributes: permissions and dates, plus
// making links. Local files only.
namespace Attributes {

struct Info
{
    bool exists = false;
    bool isDir = false;
    bool isLink = false;
    QString linkTarget; // as written in the link
    bool brokenLink = false;
    int mode = 0;       // permission bits (07777)
    QString owner;
    QString group;
    qint64 size = 0;
    QDateTime modified;
    QDateTime accessed;
    QDateTime created; // invalid when the filesystem does not record it
};

Info read(const QString &path);

// One change to permission bits: set exactly, or add and remove bits.
struct ModeChange
{
    std::optional<int> exact; // 0644...
    int add = 0;
    int remove = 0;
    // For folders, x where r is added ("X" in chmod terms: folders stay openable).
    int apply(int mode, bool isDir) const;
    bool isEmpty() const { return !exact && add == 0 && remove == 0; }
};

// "644", "0755", "rwxr-xr-x", "u+x", "go-w", "a+r,u+w", "+x". False when it
// cannot be read.
bool parseMode(const QString &text, ModeChange &out);
QString modeString(int mode, bool isDir); // "drwxr-xr-x"
QString octal(int mode);                  // "755"

struct Change
{
    std::optional<QDateTime> modified;
    std::optional<QDateTime> accessed;
    ModeChange mode;
    bool recurse = false; // into folders
    bool isEmpty() const { return !modified && !accessed && mode.isEmpty(); }
};

// Applies to each path (and, with recurse, everything inside folders).
// Links themselves are left alone (permissions on a link mean nothing).
// Returns the number of items changed; problems go into errors.
int apply(const QStringList &paths, const Change &change, QStringList *errors);

// "2024-05-01 13:30", "2024-05-01", "now", "2024-05-01T13:30:00": invalid
// when it cannot be read.
QDateTime parseDate(const QString &text);

// A symbolic link at linkPath pointing to target; relative makes the link
// work when both move together (an SD card used in another machine).
bool makeLink(const QString &target, const QString &linkPath, bool relative, QString *error);

} // namespace Attributes
