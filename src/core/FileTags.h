#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Labels, star ratings, tags and descriptions on files, as Opus has them.
// Kept in the file's extended attributes, the same ones KDE's Dolphin and
// Baloo use, so both see the same: user.xdg.tags, user.baloo.rating (0-10,
// two per star), user.xdg.comment; the color label in user.linopus.label.
// Where a drive cannot keep them (FAT and exFAT cards, servers), Linopus
// keeps them in its own list (by path). Safe from any thread.
struct FileTagData
{
    QString label;    // "Red", "To Do"... empty: none
    int rating = 0;   // 0-5 stars
    QStringList tags; // keywords
    QString comment;  // description

    bool isEmpty() const { return label.isEmpty() && rating == 0 && tags.isEmpty() && comment.isEmpty(); }
    bool operator==(const FileTagData &o) const
    {
        return label == o.label && rating == o.rating && tags == o.tags && comment == o.comment;
    }
    QVariantMap toMap() const;
    static FileTagData fromMap(const QVariantMap &m);
};

// One color label: a name and its color.
struct LabelDef
{
    QString name;
    QColor color;
};

namespace FileTags {

FileTagData read(const QString &path);
// Writes everything in data (empty fields remove theirs). False with error
// when it could be kept neither on the file nor in the list.
bool write(const QString &path, const FileTagData &data, QString *error = nullptr);

// Kept in the file itself (true) or in Linopus's list (false)?
bool storedOnFile(const QString &path);

// A file was renamed or moved: its entry in Linopus's list follows.
void renamed(const QString &from, const QString &to);
// Copies the extended attributes Linopus and the desktop use (user.*).
void copyAttributes(const QString &from, const QString &to);

// The color labels (Preferences > Labels). Defaults: Red, Orange, Yellow,
// Green, Blue, Purple, Gray.
QList<LabelDef> labels();
void setLabels(const QList<LabelDef> &labels);
QColor colorOf(const QString &label); // invalid when unknown

// Every tag used so far (for completion), most used first.
QStringList knownTags();
void noteTags(const QStringList &tags);

QString stars(int rating); // "★★★☆☆"
// Tests: where the fallback list lives (empty: the real place).
void setFallbackFile(const QString &file);
// Tests: act as if no drive could keep extended attributes.
void setForceFallback(bool on);

} // namespace FileTags
