#pragma once

#include "core/Job.h"

#include <QList>
#include <QObject>
#include <QPointer>

#include <memory>

class AliasManager;
class CommandEngine;
class JobManager;
class ListerWindow;
class ProgressWindows;
class QSettings;

// Application-wide services shared by every lister: aliases, the job queue,
// the command engine, and the session.
class AppContext : public QObject
{
    Q_OBJECT
public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    static AppContext *instance();

    // Makes sure theme icons resolve even without a desktop platform theme
    // (offscreen, a bare X session, some Flatpak setups).
    static void setupIconTheme();

    AliasManager &aliases() { return *m_aliases; }
    JobManager &jobs() { return *m_jobs; }
    CommandEngine &engine() { return *m_engine; }
    QSettings &settings() { return *m_settings; }
    ProgressWindows &progress() { return *m_progress; }

    ListerWindow *newLister(const QString &startPath = {});
    // The lister used as destination when the active one shows a single display.
    ListerWindow *otherLister(const ListerWindow *self) const;
    ListerWindow *activeLister() const;
    int listerCount() const;

    void listerActivated(ListerWindow *lister);
    void listerClosing(ListerWindow *lister);

    // Saves every open lister as the session, then closes them all.
    void quitAll();
    bool isQuitting() const { return m_quitting; }

    // Reopens the listers from last time (or one default lister).
    void restoreSession();
    void saveSession(const QList<ListerWindow *> &listers);

private:
    void onCollision(const CollisionRequest &request);

    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<AliasManager> m_aliases;
    std::unique_ptr<JobManager> m_jobs;
    std::unique_ptr<CommandEngine> m_engine;
    std::unique_ptr<ProgressWindows> m_progress;
    QList<QPointer<ListerWindow>> m_listers; // most recently active first
    bool m_quitting = false;
};
