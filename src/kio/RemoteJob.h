#pragma once

#include "core/Job.h"

#include <KIO/WidgetsAskUserActionHandler>

#include <QElapsedTimer>
#include <QPointer>
#include <QUrl>

class KJob;
class RemoteJob;

namespace KIO {
class Job;
}

// KIO asks this when a copy runs into an existing item or a failed one. Rename
// questions go to DirDuo's own collision dialog (through the JobManager);
// everything else (host keys, certificates, message boxes) keeps KIO's dialogs.
class CollisionBridge : public KIO::WidgetsAskUserActionHandler
{
    Q_OBJECT
public:
    explicit CollisionBridge(RemoteJob *job);

    void askUserRename(KJob *job, const QString &title, const QUrl &src, const QUrl &dest,
                       KIO::RenameDialog_Options options, KIO::filesize_t sizeSrc, KIO::filesize_t sizeDest,
                       const QDateTime &ctimeSrc, const QDateTime &ctimeDest, const QDateTime &mtimeSrc,
                       const QDateTime &mtimeDest) override;
    void askUserSkip(KJob *job, KIO::SkipDialog_Options options, const QString &errorText) override;

private:
    RemoteJob *m_job;
};

// A copy, move or delete that touches a server or an archive. Runs as KIO jobs
// on the GUI thread; KIO's workers do the transfers in their own processes.
class RemoteJob : public JobBase
{
public:
    RemoteJob(int id, JobSpec spec, QString queueKey);
    ~RemoteJob() override;

    JobSnapshot snapshot() const override;
    void pause() override;
    void resume() override;
    void cancel() override;
    void setBypassQueue(bool on) override { m_bypassQueue = on; }
    void provideDecision(const CollisionDecision &decision) override;
    void start(const CollisionNotifier &notifier, const DoneCallback &done) override;

private:
    friend class CollisionBridge;

    struct PendingRename
    {
        QPointer<KJob> job;
        QUrl src;
        QUrl dest;
        bool multiple = false;
        QDateTime mtimeSrc;
        QDateTime mtimeDest;
    };

    void startNext();
    void attach(KIO::Job *job);
    void onResult(KJob *job);
    void finish();
    void handleRename(KJob *job, const QUrl &src, const QUrl &dest, bool multiple, bool srcIsDir, bool destIsDir,
                      const QDateTime &mtimeSrc, const QDateTime &mtimeDest);
    void answerRename(const PendingRename &pending, const CollisionDecision &d);

    CollisionNotifier m_notifier;
    DoneCallback m_done;
    QPointer<CollisionBridge> m_bridge;
    QPointer<KJob> m_current;
    QList<QUrl> m_queue;          // for Copy AS: one source at a time
    bool m_oneAtATime = false;

    JobState m_state = JobState::Queued;
    bool m_paused = false;
    bool m_canceled = false;
    bool m_finished = false;
    bool m_bypassQueue = false;

    bool m_waiting = false;
    bool m_waitingProblem = false; // asked Retry / Skip / Abort after a dropped connection
    bool m_retrying = false;       // copying again: files already there in full are skipped
    PendingRename m_pending;
    bool m_hasSticky = false;
    CollisionDecision m_sticky;

    QString m_currentItem;
    qint64 m_totalBytes = 0;
    qint64 m_doneBytes = 0;
    qint64 m_bytesBefore = 0; // finished sub-jobs (Copy AS)
    int m_totalItems = 0;
    int m_doneItems = 0;
    int m_itemsBefore = 0;
    double m_speed = 0;
    QStringList m_errors;
};
