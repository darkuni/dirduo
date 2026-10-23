#include "core/JobManager.h"

#include "core/FileOps.h"
#include "core/Location.h"
#include "core/RemoteBackend.h"

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

namespace {

// Stands in for a remote job when the app was built without KIO.
class UnsupportedJob : public JobBase
{
public:
    using JobBase::JobBase;
    JobSnapshot snapshot() const override
    {
        JobSnapshot s;
        fillCommon(s);
        s.state = m_done ? JobState::Failed : JobState::Queued;
        if (m_done)
            s.errors << ((m_spec.type == JobType::Split || m_spec.type == JobType::Join)
                             ? QStringLiteral("Splitting and joining work in local folders (SD cards and drives too); copy it here first.")
                         : (m_spec.type == JobType::Pack || m_spec.type == JobType::Unpack)
                             ? QStringLiteral("Archives are made and extracted in local folders (SD cards and drives too); copy it here first.")
                             : QStringLiteral("Remote locations need the KIO build of Linopus."));
        return s;
    }
    void pause() override {}
    void resume() override {}
    void cancel() override {}
    void setBypassQueue(bool) override {}
    void provideDecision(const CollisionDecision &) override {}
    void start(const CollisionNotifier &, const DoneCallback &done) override
    {
        m_done = true;
        done();
    }

private:
    bool m_done = false;
};

bool involvesRemote(const JobSpec &spec)
{
    if (spec.type != JobType::Trash && spec.type != JobType::Delete && Location::isRemote(spec.destDir))
        return true;
    for (const QString &s : spec.sources) {
        if (Location::isRemote(s))
            return true;
    }
    return false;
}

} // namespace

JobManager::~JobManager()
{
    for (auto &e : m_jobs)
        e.job->cancel();
    for (auto &e : m_jobs)
        e.job->waitFinished();
}

QString JobManager::queueKeyFor(const JobSpec &spec)
{
    QString where;
    if (spec.type == JobType::Copy || spec.type == JobType::Move || spec.type == JobType::Pack || spec.type == JobType::Unpack
        || spec.type == JobType::Split || spec.type == JobType::Join)
        where = spec.destDir; // the drive written to
    else if (!spec.sources.isEmpty())
        where = spec.sources.first();
    if (Location::isRemote(where))
        return QStringLiteral("net:") + Location::serverKey(where);
    return QStringLiteral("dev:%1").arg(FileOps::deviceId(where));
}

int JobManager::enqueue(const JobSpec &spec)
{
    const int id = m_nextId++;
    Entry e;
    const QString key = queueKeyFor(spec);
    const bool archive = spec.type == JobType::Pack || spec.type == JobType::Unpack || spec.type == JobType::Split
        || spec.type == JobType::Join; // local folders only
    if (archive && involvesRemote(spec)) {
        e.job = std::make_shared<UnsupportedJob>(id, spec, key); // archives: local folders only
    } else if (involvesRemote(spec)) {
        if (RemoteBackend *backend = RemoteBackend::instance())
            e.job = backend->createJob(id, spec, key);
        else
            e.job = std::make_shared<UnsupportedJob>(id, spec, key);
    } else {
        e.job = std::make_shared<Job>(id, spec, key);
    }
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

QList<JobOutcome> JobManager::outcomes(int id) const
{
    const auto it = m_jobs.constFind(id);
    return it == m_jobs.cend() ? QList<JobOutcome>() : it->job->outcomes();
}

const JobSpec *JobManager::spec(int id) const
{
    const auto it = m_jobs.constFind(id);
    return it == m_jobs.cend() ? nullptr : &it->job->spec();
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
        // A job canceled before it started never gets a thread; finish it here.
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
    it->job->waitFinished();
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
            continue; // paused while waiting, or canceled
        if (busy.contains(e.job->queueKey()) && !s.bypassQueue)
            continue;

        busy.insert(e.job->queueKey());
        e.started = true;
        std::shared_ptr<JobBase> job = e.job;
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
        // done() may arrive from a worker thread; hop back before touching state.
        auto done = [self, id]() {
            QMetaObject::invokeMethod(
                self.data(),
                [self, id]() {
                    if (self)
                        self->onJobDone(id);
                },
                Qt::QueuedConnection);
        };
        Q_EMIT jobChanged(id);
        job->start(notifier, done);
    }
}

void JobManager::onJobDone(int id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    it->job->waitFinished();
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
