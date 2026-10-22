#pragma once

#include "core/Job.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

#include <memory>


// Owns every file operation. Jobs that write to the same device run one after
// another in the order they were queued; jobs on different devices run side by
// side. A paused job keeps its place and holds its queue.
class JobManager : public QObject
{
    Q_OBJECT
public:
    explicit JobManager(QObject *parent = nullptr);
    ~JobManager() override;

    int enqueue(const JobSpec &spec);

    QList<int> jobIds() const { return m_order; }
    bool hasJob(int id) const { return m_jobs.contains(id); }
    JobSnapshot snapshot(int id) const;
    bool hasActiveJobs() const;

    void pause(int id);
    void resume(int id);
    void cancel(int id);
    void startNow(int id); // skip the device queue
    void remove(int id);   // finished jobs only
    void resolveCollision(int id, const CollisionDecision &decision);

    // Processes events until every job is done or msecs pass. For tests and
    // for shutdown.
    bool waitForIdle(int msecs);

    // "dev:<n>" for the local device a job writes to, or "net:<server>" for a
    // remote one.
    static QString queueKeyFor(const JobSpec &spec);

Q_SIGNALS:
    void jobAdded(int id);
    void jobChanged(int id);
    void jobFinished(int id);
    void jobRemoved(int id);
    void collisionRequested(const CollisionRequest &request);

private:
    void schedule();
    void tick();
    void onJobDone(int id);

    struct Entry
    {
        std::shared_ptr<JobBase> job;
        bool started = false;
        bool reportedDone = false;
    };

    QHash<int, Entry> m_jobs;
    QList<int> m_order;
    QTimer m_tick;
    int m_nextId = 1;
};
