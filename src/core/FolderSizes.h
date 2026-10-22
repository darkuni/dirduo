#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <atomic>
#include <memory>

// Folder sizes, worked out in the background and remembered while browsing
// (Opus: hover a folder and its size fills in and stays). Local folders only.
class FolderSizes : public QObject
{
    Q_OBJECT
public:
    struct Result
    {
        qint64 bytes = 0;
        int files = 0;
        int folders = 0;
        bool complete = true; // false if some subfolder could not be read
        bool isEmpty() const { return files == 0 && folders == 0; }
    };

    explicit FolderSizes(QObject *parent = nullptr);
    ~FolderSizes() override;

    static FolderSizes *instance(); // shared by every display

    // Starts a calculation unless one is cached or running.
    void request(const QString &path);
    bool has(const QString &path) const;
    Result result(const QString &path) const;
    bool isPending(const QString &path) const;

    // Drops cached sizes at or under path (its contents changed).
    void invalidate(const QString &path);
    void clear();

    static Result measure(const QString &path, const std::atomic<bool> *cancel = nullptr);

Q_SIGNALS:
    void ready(const QString &path);

private:
    mutable QMutex m_mutex;
    QHash<QString, Result> m_cache;
    QSet<QString> m_pending;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
