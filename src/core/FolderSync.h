#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <atomic>

class CommandEngine;

// Shared by Synchronize and the duplicate finder: a cancel flag and a count
// the dialogs show while the work runs on another thread.
struct ScanProgress
{
    std::atomic<int> files{0};
    std::atomic<int> phase{0}; // 0 listing, 1 checking contents
    std::atomic<bool> cancel{false};
};

namespace FileHash {
// MD5 of the file (or of its first limit bytes). Empty when unreadable or
// cancelled.
QByteArray of(const QString &path, qint64 limit = -1, const std::atomic<bool> *cancel = nullptr);
}

struct SyncOptions
{
    enum class Mode { Update, Mirror, TwoWay };
    enum class CompareBy { Date, Size, DateAndSize, Contents };
    Mode mode = Mode::Update;
    CompareBy compareBy = CompareBy::DateAndSize;
    bool includeHidden = true;
    QStringList exclude;   // wildcards on names, ';'-separated entries allowed
    int toleranceSecs = 2; // FAT (SD cards) keeps times to 2 seconds

    static QString modeName(Mode m);
    static bool modeFromName(const QString &name, Mode &out);
    static QString compareName(CompareBy c);
    static bool compareFromName(const QString &name, CompareBy &out);
};

struct SyncEntry
{
    enum class State { OnlyLeft, OnlyRight, LeftNewer, RightNewer, Different, Same };
    enum class Action { None, CopyToRight, CopyToLeft, DeleteRight, DeleteLeft };
    QString rel; // path under both roots
    bool isDir = false;
    qint64 leftSize = -1;
    qint64 rightSize = -1;
    QDateTime leftTime;
    QDateTime rightTime;
    State state = State::Same;
    Action action = Action::None;
};

namespace FolderSync {

// Everything that differs, plus the identical files (state Same). A folder on
// one side only is one entry; its contents go with it.
QList<SyncEntry> compare(const QString &left, const QString &right, const SyncOptions &options,
                         ScanProgress *progress = nullptr);

SyncEntry::Action defaultAction(SyncEntry::State state, SyncOptions::Mode mode);
QString stateText(SyncEntry::State s);
QString actionText(SyncEntry::Action a);

struct Plan
{
    QMap<QString, QStringList> copies; // destination folder -> sources
    QStringList deletes;
    int items = 0;
    qint64 bytes = 0;
};
Plan plan(const QList<SyncEntry> &entries, const QString &left, const QString &right);

// Queues the copies and deletions (to the trash) on the job manager. Returns
// how many jobs were queued.
int run(const Plan &plan, CommandEngine &engine);

} // namespace FolderSync

struct DuplicateOptions
{
    enum class By { Contents, NameAndSize, Name };
    By by = By::Contents;
    qint64 minSize = 1; // empty files are all "the same"; skip them
    bool includeHidden = false;
    QStringList exclude;
};

struct DuplicateGroup
{
    qint64 size = 0;
    QStringList paths; // two or more, sorted
};

namespace Duplicates {
QList<DuplicateGroup> find(const QStringList &roots, const DuplicateOptions &options, ScanProgress *progress = nullptr);
qint64 reclaimable(const QList<DuplicateGroup> &groups); // all but one copy of each
}
