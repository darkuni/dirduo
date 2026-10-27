#pragma once
#include "core/Job.h"
#include <QHash>
#include <QElapsedTimer>

#include "ui/Launch.h"

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
class ToolbarStore;

// Application-wide services shared by every lister: aliases, the job queue,
// the command engine, and the session.
class ScriptHost;

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
    ToolbarStore &toolbars() { return *m_toolbars; }
    // Tests: notices even with a window active.
    void setNotifyWhenActive(bool on) { m_notifyWhenActive = on; }
    ScriptHost &scripts() { return *m_scripts; }

    // Every file under ~/.config/dirduo in one zip, and back. Restoring
    // reloads aliases, toolbars, themes, labels and folder formats at once.
    bool backupSettings(const QString &zipFile, QString *error);

    // Layouts: a lister's whole arrangement under a name, in layouts/<name>.ini.
    // Without folders, loading keeps the folders the lister shows now.
    QStringList layoutNames() const;
    bool saveLayout(const QString &name, ListerWindow *from, bool withFolders, QString *error);
    // Replaces the lister with one arranged as saved; returns the new one.
    ListerWindow *loadLayout(const QString &name, ListerWindow *replace, QString *error);
    bool deleteLayout(const QString &name);
    bool restoreSettings(const QString &zipFile, QString *error);
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

    // A launch from outside (or handed over by a later launch): folders and
    // files to open, commands to run; brings a lister to the front.
    void handleLaunch(const Launch::Request &request);
    // Reopens the listers from last time (or one default lister).
    void restoreSession();
    void saveSession(const QList<ListerWindow *> &listers);

private:
    void onCollision(const CollisionRequest &request);

    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<ToolbarStore> m_toolbars;
    std::unique_ptr<ScriptHost> m_scripts;
    void notifyJobDone(const struct JobSnapshot &snap, qint64 ms);
    QHash<int, QElapsedTimer> m_jobClocks;
    QHash<int, struct JobSnapshot> m_doneSnaps;
    bool m_notifyWhenActive = false;
    std::unique_ptr<AliasManager> m_aliases;
    std::unique_ptr<JobManager> m_jobs;
    std::unique_ptr<CommandEngine> m_engine;
    std::unique_ptr<ProgressWindows> m_progress;
    QList<QPointer<ListerWindow>> m_listers; // most recently active first
    bool m_quitting = false;
};
