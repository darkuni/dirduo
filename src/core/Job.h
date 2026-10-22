#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QMap>
#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QWaitCondition>

#include <atomic>
#include <functional>
#include <vector>

enum class JobType { Copy, Move, Trash, Delete };

enum class JobState {
    Queued,         // waiting for another job on the same device
    Scanning,       // counting files and bytes
    Running,
    Paused,
    WaitingForUser, // a collision needs a decision
    Finished,
    Failed,         // finished, but some items could not be processed
    Cancelled,
};

enum class CollisionAction { Ask, Replace, ReplaceIfNewer, Skip, Rename, CancelJob };

struct CollisionRequest
{
    int jobId = 0;
    QString source;
    QString target;
    QString suggestedName; // free name in the target folder
    bool sourceIsDir = false;
    bool targetIsDir = false;
};

struct CollisionDecision
{
    CollisionAction action = CollisionAction::Skip;
    QString newName;         // for Rename
    bool applyToAll = false; // reuse for the rest of this job (Rename picks free names)
};

struct JobSpec
{
    JobType type = JobType::Copy;
    QStringList sources;
    QString destDir;                     // Copy and Move only
    QMap<QString, QString> targetNames;  // source path -> name at the destination (Copy AS)
    CollisionAction whenExists = CollisionAction::Ask;
};

struct JobSnapshot
{
    int id = 0;
    JobType type = JobType::Copy;
    JobState state = JobState::Queued;
    QString title;
    QString currentItem;
    qint64 totalBytes = 0;
    qint64 doneBytes = 0;
    int totalItems = 0;
    int doneItems = 0;
    double bytesPerSecond = 0;
    QStringList errors;
    QString queueKey;
    bool bypassQueue = false;

    bool isActive() const
    {
        return state == JobState::Scanning || state == JobState::Running || state == JobState::Paused
            || state == JobState::WaitingForUser;
    }
    bool isDone() const
    {
        return state == JobState::Finished || state == JobState::Failed || state == JobState::Cancelled;
    }
};

Q_DECLARE_METATYPE(CollisionRequest)

// One file operation. Created and controlled from the GUI thread; run() executes
// on a worker thread. Every public method is thread-safe.
class Job
{
public:
    using CollisionNotifier = std::function<void(const CollisionRequest &)>;

    Job(int id, JobSpec spec, QString queueKey);

    int id() const { return m_id; }
    const JobSpec &spec() const { return m_spec; }
    QString queueKey() const { return m_queueKey; }

    JobSnapshot snapshot() const;

    void pause();
    void resume();
    void cancel();
    void setBypassQueue(bool on);
    void provideDecision(const CollisionDecision &decision);

    // Runs the whole job on the calling thread. notifier is called (on this
    // thread) when a collision needs a decision; the job then blocks until
    // provideDecision() or cancel().
    void run(const CollisionNotifier &notifier);

    static QString describe(const JobSpec &spec);

private:
    struct Entry
    {
        QString src;
        QString name;
        int parent = -1;
        bool isDir = false;
        bool isSymlink = false;
        qint64 size = 0;
    };

    void setState(JobState s);
    bool checkpoint(); // honours pause; false once cancelled
    void addError(const QString &message);
    void setCurrent(const QString &item);
    void addDone(qint64 bytes, int items);

    bool scan(const QString &path, const QString &name, int parent, std::vector<Entry> &out);
    CollisionDecision askCollision(const CollisionRequest &req);

    void runCopyMove();
    void runTrash();
    void runDelete();
    bool copyFile(const QString &src, const QString &dst, QString *error);
    bool copySymlink(const QString &src, const QString &dst, QString *error);

    const int m_id;
    const JobSpec m_spec;
    const QString m_queueKey;
    CollisionNotifier m_notifier;

    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    JobState m_state = JobState::Queued;
    JobState m_stateBeforePause = JobState::Running;
    bool m_paused = false;
    std::atomic<bool> m_cancelled{false};
    bool m_bypassQueue = false;

    bool m_hasDecision = false;
    CollisionDecision m_decision;
    bool m_hasStickyDecision = false;
    CollisionDecision m_stickyDecision;

    QString m_current;
    qint64 m_totalBytes = 0;
    qint64 m_doneBytes = 0;
    int m_totalItems = 0;
    int m_doneItems = 0;
    QStringList m_errors;

    QElapsedTimer m_runClock;
    qint64 m_pausedMs = 0;
    QElapsedTimer m_pauseClock;
};
