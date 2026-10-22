#pragma once

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Advanced Rename: works out new names for a list of items and renames them
// together, safely (swaps and case-only changes go through temporary names,
// and a failure part way puts everything back).
//
// New-name tokens, in every mode:
//   [#] [##] [###]  counter, padded to the number of # (or the width option)
//   [date]          today, 2026-10-22      [mdate] the item's modified date
//   [mtime]         modified time, 104512  [name]  the original name, no extension
//   [ext]           the original extension [parent] the folder it is in
// Standard mode also takes Opus's bare # runs as counters and * as "what the
// matching * in the old name matched".
struct RenameOptions
{
    enum class Mode { Standard, FindReplace, RegExp };
    enum class Case { Keep, Lower, Upper, Title, Sentence };
    enum class ExtCase { Keep, Lower, Upper };

    Mode mode = Mode::Standard;
    QString oldPattern;          // wildcard, text to find, or regular expression
    QString newPattern;          // new name, replacement, or replacement with \1
    bool caseSensitive = false;  // find and replace, regular expression
    bool ignoreExtension = false;
    Case nameCase = Case::Keep;
    ExtCase extCase = ExtCase::Keep;
    int counterStart = 1;
    int counterStep = 1;
    int counterWidth = 0;        // at least this many digits
    bool trimSpaces = false;     // no leading, trailing or doubled spaces

    static QString modeName(Mode m);
    static bool modeFromName(const QString &name, Mode &out);
    static QString caseName(Case c);
    static bool caseFromName(const QString &name, Case &out);
};

struct RenameItem
{
    enum class Status { Unchanged, Ok, NoMatch, Invalid, Conflict };
    QString path;
    QString oldName;
    QString newName;
    Status status = Status::Unchanged;
    QString note; // why it is Invalid or a Conflict
    bool willChange() const { return status == Status::Ok; }
};

namespace RenameEngine {

// One entry per path, in the order given (counters follow that order).
QList<RenameItem> preview(const QStringList &paths, const RenameOptions &options);

// Just the new name for one file (no conflict checks), for tests and the
// editor's examples.
QString newNameFor(const QString &name, const RenameOptions &options, int counter, const QDateTime &modified = {},
                   const QString &parent = {}, bool *matched = nullptr);

bool hasConflicts(const QList<RenameItem> &items);
int changeCount(const QList<RenameItem> &items);

// Renames every Ok item (local files). done receives (new path, old path)
// pairs for undo. On failure nothing stays renamed.
bool apply(const QList<RenameItem> &items, QList<QPair<QString, QString>> *done, QString *error);

// Renames each new path back to its old one.
bool undo(const QList<QPair<QString, QString>> &done, QString *error);

} // namespace RenameEngine

// Saved settings of the rename dialog, by name, in rename.ini.
namespace RenamePresets {
QStringList names(const QString &file = {});
bool load(const QString &name, RenameOptions &out, const QString &file = {});
bool save(const QString &name, const RenameOptions &options, const QString &file = {});
bool remove(const QString &name, const QString &file = {});
} // namespace RenamePresets
