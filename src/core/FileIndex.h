#pragma once

#include "core/FileItem.h"

#include <QDateTime>
#include <QHash>
#include <QFutureWatcher>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <memory>

// One file or folder put into the index outside a build.
struct OverlayItem
{
    QByteArray name;
    bool isDir = false;
    qint64 size = 0;
    qint64 mtime = 0;
};

// An index of every file name under the folders you pick (your home and the
// SD card by default), for instant search by name as you type, as the
// Everything tool does on Windows. Built on a background thread at low
// priority, kept in ~/.cache/dirduo/index.bin between runs, built again on a
// timer, and patched at once for folders DirDuo lists or copies into.
//
// Search words (all must match, any order, case does not matter):
//   mario kart       names with both words in them
//   *.d64  sonic?.*  wildcards
//   ext:mp3;flac     by extension
//   file: folder:    only files / only folders
//   roms/snes        a word with / matches the folder path too
class FileIndex : public QObject
{
    Q_OBJECT
public:
    struct Settings
    {
        bool enabled = true;
        QStringList roots;            // empty: home and removable media
        bool hidden = false;          // index hidden files and folders too
        int rebuildMinutes = 60;      // 0: only when asked
        QStringList exclude;          // folder names to leave out
        bool operator==(const Settings &o) const
        {
            return enabled == o.enabled && roots == o.roots && hidden == o.hidden && rebuildMinutes == o.rebuildMinutes
                && exclude == o.exclude;
        }
    };
    struct Hit
    {
        QString name;
        QString folder;
        qint64 size = 0;
        QDateTime modified;
        bool isDir = false;
        QString path() const;
    };
    struct Result
    {
        QList<Hit> hits;  // sorted by name
        int total = 0;    // all that matched
        bool truncated = false;
    };

    static FileIndex *instance();
    static QStringList defaultRoots();
    static QStringList defaultExclude();

    Settings settings() const { return m_settings; }
    QStringList roots() const; // the ones in use
    void setSettings(const Settings &s); // saves; builds again when the folders changed
    QString cacheFile() const;

    // Loads the saved index and starts the timer (when enabled).
    void start();
    void rebuild();
    bool isBuilding() const { return m_building; }
    bool isReady() const;
    qint64 count() const; // files and folders in it
    QDateTime builtAt() const;
    // Tests: waits until a build or load is done.
    bool waitIdle(int ms);

    // A folder DirDuo just listed: its contents replace what the index had.
    void folderListed(const QString &path, const FileItemList &items);
    // Reads a folder again (after a copy into it); recursive also reads the
    // folders in it that the index did not know.
    void rescan(const QString &folder, bool recursive);

    // Safe from any thread.
    Result search(const QString &query, int limit = 5000, const std::atomic<bool> *cancel = nullptr) const;

    struct Data;
    struct Overlay;

Q_SIGNALS:
    void changed();      // built, loaded or patched
    void buildingChanged(bool building);

private:
    FileIndex();
    void loadSettings();
    void scheduleCheck();
    void finishBuild(std::shared_ptr<const Data> d, bool fromCache);
    void applyPatches(const QHash<QByteArray, QList<OverlayItem>> &patches);
    QStringList unknownSubfolders(const QString &folder, const QList<OverlayItem> &items) const;
    bool covers(const QString &path) const;

    Settings m_settings;
    std::shared_ptr<const Data> m_data;
    std::shared_ptr<const Overlay> m_overlay;
    mutable QMutex m_lock; // guards m_data and m_overlay (the pointers, not what they point to)
    bool m_building = false;
    bool m_started = false;
    QTimer m_timer;
    QFutureWatcher<std::shared_ptr<const Data>> m_watcher;
    bool m_watcherFromCache = false;
    bool m_rebuildPending = false;
};
