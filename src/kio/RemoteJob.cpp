#include "kio/RemoteJob.h"

#include "core/Location.h"
#include "kio/KioBackend.h"

#include <KFileUtils>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/JobUiDelegate>

#include <QFileInfo>

namespace {

// KIO::JobUiDelegate's constructor that takes extra interfaces is protected;
// this is the supported way in.
class JobDelegate : public KIO::JobUiDelegate
{
public:
    JobDelegate(QWidget *window, QObject *askUser)
        : KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingDisabled, window, {askUser})
    {
    }
};

} // namespace

// ---------------------------------------------------------------- bridge

CollisionBridge::CollisionBridge(RemoteJob *job)
    : KIO::WidgetsAskUserActionHandler(nullptr)
    , m_job(job)
{
}

void CollisionBridge::askUserRename(KJob *job, const QString &, const QUrl &src, const QUrl &dest,
                                    KIO::RenameDialog_Options options, KIO::filesize_t, KIO::filesize_t,
                                    const QDateTime &, const QDateTime &, const QDateTime &mtimeSrc,
                                    const QDateTime &mtimeDest)
{
    m_job->handleRename(job, src, dest, options & KIO::RenameDialog_MultipleItems,
                        options & KIO::RenameDialog_SourceIsDirectory, options & KIO::RenameDialog_DestIsDirectory,
                        mtimeSrc, mtimeDest);
}

void CollisionBridge::askUserSkip(KJob *job, KIO::SkipDialog_Options options, const QString &errorText)
{
    // Same rule as local jobs: skip what fails, list it when the job ends.
    m_job->m_errors << errorText;
    Q_EMIT askUserSkipResult(options & KIO::SkipDialog_MultipleItems ? KIO::Result_AutoSkip : KIO::Result_Skip, job);
}

// ---------------------------------------------------------------- job

RemoteJob::RemoteJob(int id, JobSpec spec, QString queueKey)
    : JobBase(id, std::move(spec), std::move(queueKey))
{
}

RemoteJob::~RemoteJob()
{
    if (m_current)
        m_current->kill(KJob::Quietly);
}

JobSnapshot RemoteJob::snapshot() const
{
    JobSnapshot s;
    fillCommon(s);
    if (m_finished)
        s.state = m_cancelled ? JobState::Cancelled : (m_errors.isEmpty() ? JobState::Finished : JobState::Failed);
    else if (m_cancelled && !m_current)
        s.state = JobState::Cancelled;
    else if (m_waiting)
        s.state = JobState::WaitingForUser;
    else if (m_paused)
        s.state = JobState::Paused;
    else
        s.state = m_state;
    s.currentItem = m_currentItem;
    s.totalBytes = m_totalBytes;
    s.doneBytes = m_doneBytes;
    s.totalItems = m_totalItems;
    s.doneItems = m_doneItems;
    s.bytesPerSecond = m_speed;
    s.errors = m_errors;
    s.bypassQueue = m_bypassQueue;
    return s;
}

void RemoteJob::start(const CollisionNotifier &notifier, const DoneCallback &done)
{
    m_notifier = notifier;
    m_done = done;
    if (m_cancelled) {
        finish();
        return;
    }
    m_state = JobState::Scanning;

    const QList<QUrl> sources = Location::toUrls(m_spec.sources);
    const QUrl dest = Location::toUrl(m_spec.destDir);
    switch (m_spec.type) {
    case JobType::Copy:
    case JobType::Move:
        if (!m_spec.targetNames.isEmpty()) {
            // Copy AS: each item goes to its own new name.
            m_oneAtATime = true;
            m_queue = sources;
            startNext();
        } else if (m_spec.type == JobType::Move) {
            attach(KIO::move(sources, dest, KIO::HideProgressInfo));
        } else {
            attach(KIO::copy(sources, dest, KIO::HideProgressInfo));
        }
        break;
    case JobType::Delete:
        attach(KIO::del(sources, KIO::HideProgressInfo));
        break;
    case JobType::Trash:
        attach(KIO::trash(sources, KIO::HideProgressInfo));
        break;
    }
}

void RemoteJob::startNext()
{
    const QUrl src = m_queue.takeFirst();
    const QString srcLoc = Location::fromUrl(src);
    const QString name = m_spec.targetNames.value(srcLoc, Location::fileName(srcLoc));
    const QUrl dest = Location::toUrl(Location::join(m_spec.destDir, name));
    attach(m_spec.type == JobType::Move ? KIO::moveAs(src, dest, KIO::HideProgressInfo)
                                        : KIO::copyAs(src, dest, KIO::HideProgressInfo));
}

void RemoteJob::attach(KIO::Job *job)
{
    m_current = job;
    // A fresh bridge per KIO job: the delegate owns it.
    auto *bridge = new CollisionBridge(this);
    QWidget *window = KioBackend::kioInstance() ? KioBackend::kioInstance()->window() : nullptr;
    job->setUiDelegate(new JobDelegate(window, bridge));
    m_bridge = bridge;

    QObject::connect(job, &KJob::totalAmountChanged, job, [this](KJob *, KJob::Unit unit, qulonglong amount) {
        if (unit == KJob::Bytes)
            m_totalBytes = m_bytesBefore + static_cast<qint64>(amount);
        else if (unit == KJob::Files || unit == KJob::Directories || unit == KJob::Items)
            m_totalItems = qMax(m_totalItems, m_itemsBefore + static_cast<int>(amount));
        if (m_state == JobState::Scanning)
            m_state = JobState::Running;
    });
    QObject::connect(job, &KJob::processedAmountChanged, job, [this](KJob *, KJob::Unit unit, qulonglong amount) {
        if (unit == KJob::Bytes)
            m_doneBytes = m_bytesBefore + static_cast<qint64>(amount);
        else if (unit == KJob::Files || unit == KJob::Items)
            m_doneItems = m_itemsBefore + static_cast<int>(amount);
        if (m_state == JobState::Scanning)
            m_state = JobState::Running;
    });
    QObject::connect(job, &KJob::speed, job, [this](KJob *, unsigned long bytesPerSecond) {
        m_speed = static_cast<double>(bytesPerSecond);
    });
    if (auto *copy = qobject_cast<KIO::CopyJob *>(job)) {
        QObject::connect(copy, &KIO::CopyJob::copying, copy, [this](KIO::Job *, const QUrl &src, const QUrl &) {
            m_currentItem = Location::fromUrl(src);
            m_state = JobState::Running;
        });
        QObject::connect(copy, &KIO::CopyJob::moving, copy, [this](KIO::Job *, const QUrl &src, const QUrl &) {
            m_currentItem = Location::fromUrl(src);
            m_state = JobState::Running;
        });
    }
    if (auto *del = qobject_cast<KIO::DeleteJob *>(job)) {
        QObject::connect(del, &KIO::DeleteJob::deleting, del, [this](KIO::Job *, const QUrl &file) {
            m_currentItem = Location::fromUrl(file);
            m_state = JobState::Running;
        });
    }
    QObject::connect(job, &KJob::result, job, [this](KJob *j) { onResult(j); });
    if (m_paused)
        job->suspend();
}

void RemoteJob::onResult(KJob *job)
{
    if (job != m_current)
        return;
    m_current.clear();
    if (job->error()) {
        if (job->error() == KJob::KilledJobError)
            m_cancelled = true;
        else
            m_errors << job->errorString();
    }
    if (m_oneAtATime && !m_queue.isEmpty() && !m_cancelled) {
        m_bytesBefore = m_doneBytes;
        m_itemsBefore = m_doneItems + 1;
        m_doneItems = m_itemsBefore;
        startNext();
        return;
    }
    finish();
}

void RemoteJob::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    m_waiting = false;
    m_currentItem.clear();
    if (!m_cancelled && m_totalBytes > 0)
        m_doneBytes = m_totalBytes;
    if (m_done)
        m_done();
}

void RemoteJob::pause()
{
    if (m_finished || m_paused)
        return;
    m_paused = true;
    if (m_current)
        m_current->suspend();
}

void RemoteJob::resume()
{
    if (!m_paused)
        return;
    m_paused = false;
    if (m_current)
        m_current->resume();
}

void RemoteJob::cancel()
{
    if (m_finished)
        return;
    m_cancelled = true;
    if (m_current)
        m_current->kill(KJob::EmitResult); // onResult finishes the job
}

void RemoteJob::handleRename(KJob *job, const QUrl &src, const QUrl &dest, bool multiple, bool srcIsDir,
                             bool destIsDir, const QDateTime &mtimeSrc, const QDateTime &mtimeDest)
{
    PendingRename p;
    p.job = job;
    p.src = src;
    p.dest = dest;
    p.multiple = multiple;
    p.mtimeSrc = mtimeSrc;
    p.mtimeDest = mtimeDest;

    // A folder landing on a folder merges into it, as local copies do.
    if (srcIsDir && destIsDir) {
        Q_EMIT m_bridge->askUserRenameResult(KIO::Result_Overwrite, QUrl(), job);
        return;
    }
    if (m_hasSticky) {
        answerRename(p, m_sticky);
        return;
    }
    if (m_spec.whenExists != CollisionAction::Ask) {
        CollisionDecision d;
        d.action = m_spec.whenExists;
        d.applyToAll = true;
        answerRename(p, d);
        return;
    }

    m_pending = p;
    m_waiting = true;
    CollisionRequest req;
    req.jobId = m_id;
    req.source = Location::fromUrl(src);
    req.target = Location::fromUrl(dest);
    req.suggestedName = KFileUtils::suggestName(dest.adjusted(QUrl::RemoveFilename), dest.fileName());
    req.sourceIsDir = srcIsDir;
    req.targetIsDir = destIsDir;
    if (m_notifier)
        m_notifier(req);
}

void RemoteJob::provideDecision(const CollisionDecision &decision)
{
    if (!m_waiting)
        return;
    m_waiting = false;
    if (decision.applyToAll && decision.action != CollisionAction::CancelJob) {
        m_hasSticky = true;
        m_sticky = decision;
        m_sticky.newName.clear();
    }
    answerRename(m_pending, decision);
}

void RemoteJob::answerRename(const PendingRename &p, const CollisionDecision &d)
{
    const bool all = p.multiple && d.applyToAll;
    KIO::RenameDialog_Result result = KIO::Result_Skip;
    QUrl newUrl;
    switch (d.action) {
    case CollisionAction::Replace:
        result = all ? KIO::Result_OverwriteAll : KIO::Result_Overwrite;
        break;
    case CollisionAction::ReplaceIfNewer:
        if (all)
            result = KIO::Result_OverwriteWhenOlder;
        else
            result = (p.mtimeSrc.isValid() && p.mtimeDest.isValid() && p.mtimeSrc > p.mtimeDest) ? KIO::Result_Overwrite
                                                                                               : KIO::Result_Skip;
        break;
    case CollisionAction::Skip:
    case CollisionAction::Ask:
        result = all ? KIO::Result_AutoSkip : KIO::Result_Skip;
        break;
    case CollisionAction::Rename: {
        if (all && d.newName.isEmpty()) {
            result = KIO::Result_AutoRename;
            break;
        }
        result = KIO::Result_Rename;
        const QUrl dir = p.dest.adjusted(QUrl::RemoveFilename);
        const QString name = d.newName.isEmpty() ? KFileUtils::suggestName(dir, p.dest.fileName()) : d.newName;
        newUrl = dir;
        newUrl.setPath(dir.path() + name);
        break;
    }
    case CollisionAction::CancelJob:
        m_cancelled = true;
        result = KIO::Result_Cancel;
        break;
    }
    if (m_bridge)
        Q_EMIT m_bridge->askUserRenameResult(result, newUrl, p.job);
}
