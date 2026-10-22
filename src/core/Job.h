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

class QThread;

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

// What a finished job did with one item, for undo and the file log. For
// Copy and Move: the original and the new item, at the highest level the job
// created (a file copied into a folder that was already there is listed on
// its own, so undo never touches what was there before). For Trash: the
// original and where it went in the trash.
struct JobOutcome
{
    QString source;
    QString target;
    bool replaced = false; // it overwrote a file that was there
};

struct JobSnapshot
{
    int id = 0;
    JobType type = JobType::Copy;
    JobState state = JobState::Queued;
    QString title;
    QString fromFolder; // folder of the first source
    QString toFolder;   // destination (Copy and Move)
    QString currentItem;
    qint64 currentSize = 0; // bytes of the file being copied right now
    qint64 currentDone = 0;
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

// One file operation as the JobManager sees it. Local jobs run on their own
// thread; remote jobs (KIO) run on the GUI thread's event loop.
class JobBase
{
public:
    using CollisionNotifier = std::function<void(const CollisionRequest &)>;
    using DoneCallback = std::function<void()>;

    JobBase(int id, JobSpec spec, QString queueKey);
    virtual ~JobBase();

    int id() const { return m_id; }
    const JobSpec &spec() const { return m_spec; }
    QString queueKey() const { return m_queueKey; }

    virtual JobSnapshot snapshot() const = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void cancel() = 0;
    virtual void setBypassQueue(bool on) = 0;
    virtual void provideDecision(const CollisionDecision &decision) = 0;
    // What was done, item by item (local jobs; empty for remote ones).
    virtual QList<JobOutcome> outcomes() const { return {}; }

    // Starts the job. notifier asks for collision decisions; done is called
    // once when the job has ended (possibly from another thread).
    virtual void start(const CollisionNotifier &notifier, const DoneCallback &done) = 0;
    // Blocks until a started job's worker has fully stopped.
    virtual void waitFinished() {}

    static QString describe(const JobSpec &spec);
    // Fills title, fromFolder and toFolder.
    void fillCommon(JobSnapshot &s) const;

protected:
    const int m_id;
    const JobSpec m_spec;
    const QString m_queueKey;
};

// A job on local files. Created and controlled from the GUI thread; run()
// executes on a worker thread. Every public method is thread-safe.
class Job : public JobBase
{
public:
    Job(int id, JobSpec spec, QString queueKey);
    ~Job() override;

    JobSnapshot snapshot() const override;

    void pause() override;
    void resume() override;
    void cancel() override;
    void setBypassQueue(bool on) override;
    void provideDecision(const CollisionDecision &decision) override;
    QList<JobOutcome> outcomes() const override;
    void start(const CollisionNotifier &notifier, const DoneCallback &done) override;
    void waitFinished() override;

    // Runs the whole job on the calling thread. notifier is called (on this
    // thread) when a collision needs a decision; the job then blocks until
    // provideDecision() or cancel().
    void run(const CollisionNotifier &notifier);

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
    void setCurrent(const QString &item, qint64 size = 0);
    void addDone(qint64 bytes, int items);

    bool scan(const QString &path, const QString &name, int parent, std::vector<Entry> &out);
    CollisionDecision askCollision(const CollisionRequest &req);

    void runCopyMove();
    void runTrash();
    void runDelete();
    bool copyFile(const QString &src, const QString &dst, QString *error);
    bool copySymlink(const QString &src, const QString &dst, QString *error);

    CollisionNotifier m_notifier;
    QThread *m_thread = nullptr;

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
    qint64 m_currentSize = 0;
    qint64 m_currentDone = 0;
    qint64 m_totalBytes = 0;
    qint64 m_doneBytes = 0;
    int m_totalItems = 0;
    int m_doneItems = 0;
    QStringList m_errors;
    QList<JobOutcome> m_outcomes;
    void addOutcome(const QString &source, const QString &target, bool replaced = false);

    QElapsedTimer m_runClock;
    qint64 m_pausedMs = 0;
    QElapsedTimer m_pauseClock;
};
