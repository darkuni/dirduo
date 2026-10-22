#include "core/JobManager.h"

#include "core/FileOps.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QMetaMethod>
#include <QPointer>
#include <QSet>
#include <QThread>

JobManager::JobManager(QObject *parent)
    : QObject(parent)
{
    static const int reg = qRegisterMetaType<CollisionRequest>();
    Q_UNUSED(reg)
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &JobManager::tick);
}

JobManager::~JobManager()
{
    for (auto &e : m_jobs) {
        e.job->cancel();
    }
    for (auto &e : m_jobs) {
        if (e.thread) {
            e.thread->wait();
            delete e.thread;
            e.thread = nullptr;
        }
    }
}

QString JobManager::queueKeyFor(const JobSpec &spec)
{
    QString where;
    if (spec.type == JobType::Copy || spec.type == JobType::Move)
        where = spec.destDir;
    else if (!spec.sources.isEmpty())
        where = spec.sources.first();
    return QStringLiteral("dev:%1").arg(FileOps::deviceId(where));
}

int JobManager::enqueue(const JobSpec &spec)
{
    const int id = m_nextId++;
    Entry e;
    e.job = std::make_shared<Job>(id, spec, queueKeyFor(spec));
    m_jobs.insert(id, e);
    m_order.append(id);
    Q_EMIT jobAdded(id);
    schedule();
    if (!m_tick.isActive())
        m_tick.start();
    return id;
}

JobSnapshot JobManager::snapshot(int id) const
{
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.cend())
        return {};
    return it->job->snapshot();
}

bool JobManager::hasActiveJobs() const
{
    for (const auto &e : m_jobs) {
        const JobSnapshot s = e.job->snapshot();
        if (!s.isDone())
            return true;
    }
    return false;
}

void JobManager::pause(int id)
{
    if (auto it = m_jobs.find(id); it != m_jobs.end()) {
        it->job->pause();
        Q_EMIT jobChanged(id);
    }
}

void JobManager::resume(int id)
{
    if (auto it = m_jobs.find(id); it != m_jobs.end()) {
        it->job->resume();
        Q_EMIT jobChanged(id);
        schedule();
    }
}

void JobManager::cancel(int id)
{
    if (auto it = m_jobs.find(id); it != m_jobs.end()) {
        it->job->cancel();
        Q_EMIT jobChanged(id);
        // A job cancelled before it started never gets a thread; finish it here.
        if (!it->started && !it->reportedDone) {
            it->reportedDone = true;
            Q_EMIT jobFinished(id);
            schedule();
        }
    }
}

void JobManager::startNow(int id)
{
    if (auto it = m_jobs.find(id); it != m_jobs.end()) {
        it->job->setBypassQueue(true);
        schedule();
    }
}

void JobManager::remove(int id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    if (!it->job->snapshot().isDone())
        return;
    if (it->thread) {
        it->thread->wait();
        delete it->thread;
    }
    m_jobs.erase(it);
    m_order.removeAll(id);
    Q_EMIT jobRemoved(id);
}

void JobManager::resolveCollision(int id, const CollisionDecision &decision)
{
    if (auto it = m_jobs.find(id); it != m_jobs.end())
        it->job->provideDecision(decision);
}

void JobManager::schedule()
{
    // Devices that already have a job underway.
    QSet<QString> busy;
    for (int id : std::as_const(m_order)) {
        const Entry &e = m_jobs[id];
        if (e.started && !e.reportedDone)
            busy.insert(e.job->queueKey());
    }

    for (int id : std::as_const(m_order)) {
        Entry &e = m_jobs[id];
        if (e.started)
            continue;
        const JobSnapshot s = e.job->snapshot();
        if (s.state != JobState::Queued)
            continue; // paused while waiting, or cancelled
        if (busy.contains(e.job->queueKey()) && !s.bypassQueue)
            continue;

        busy.insert(e.job->queueKey());
        e.started = true;
        std::shared_ptr<Job> job = e.job;
        QPointer<JobManager> self(this);
        auto notifier = [self, id](const CollisionRequest &req) {
            QMetaObject::invokeMethod(
                self.data(),
                [self, id, req]() {
                    if (!self)
                        return;
                    Q_EMIT self->jobChanged(id);
                    if (self->isSignalConnected(QMetaMethod::fromSignal(&JobManager::collisionRequested))) {
                        Q_EMIT self->collisionRequested(req);
                    } else {
                        // Nobody to ask: leave existing files alone.
                        CollisionDecision skip;
                        skip.action = CollisionAction::Skip;
                        self->resolveCollision(id, skip);
                    }
                },
                Qt::QueuedConnection);
        };
        e.thread = QThread::create([job, notifier]() { job->run(notifier); });
        connect(e.thread, &QThread::finished, this, [this, id]() { onThreadFinished(id); }, Qt::QueuedConnection);
        e.thread->start();
        Q_EMIT jobChanged(id);
    }
}

void JobManager::onThreadFinished(int id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    if (it->thread) {
        it->thread->wait();
        it->thread->deleteLater();
        it->thread = nullptr;
    }
    if (!it->reportedDone) {
        it->reportedDone = true;
        Q_EMIT jobChanged(id);
        Q_EMIT jobFinished(id);
    }
    schedule();
}

void JobManager::tick()
{
    bool anyActive = false;
    for (int id : std::as_const(m_order)) {
        const Entry &e = m_jobs[id];
        if (e.reportedDone)
            continue;
        anyActive = true;
        Q_EMIT jobChanged(id);
    }
    if (!anyActive)
        m_tick.stop();
}

bool JobManager::waitForIdle(int msecs)
{
    QDeadlineTimer deadline(msecs);
    while (!deadline.hasExpired()) {
        bool idle = true;
        for (const auto &e : std::as_const(m_jobs)) {
            if (!e.reportedDone) {
                idle = false;
                break;
            }
        }
        if (idle)
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return false;
}
