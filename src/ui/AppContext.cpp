#include "ui/AppContext.h"

#include "core/AliasManager.h"
#include "core/CommandEngine.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "ui/CollisionDialog.h"
#include "ui/ListerWindow.h"
#include "ui/ProgressWindow.h"
#ifdef LINOPUS_HAVE_KIO
#include "kio/KioBackend.h"
#endif

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include "ui/ButtonBar.h"
#include "ui/FolderFormats.h"
#include "core/Zip.h"
#include "ui/Style.h"

#include <QSettings>
#include <QFile>
#include <QSaveFile>
#include <QDirIterator>

namespace {
AppContext *g_instance = nullptr;
}

AppContext::AppContext(QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat))
    , m_toolbars(std::make_unique<ToolbarStore>(Paths::configFile(QStringLiteral("toolbars"))))
    , m_aliases(std::make_unique<AliasManager>())
    , m_jobs(std::make_unique<JobManager>())
    , m_engine(std::make_unique<CommandEngine>(*m_aliases, *m_jobs))
    , m_progress(std::make_unique<ProgressWindows>(*m_jobs))
{
    g_instance = this;
    // The theme applies before the first lister opens.
    const QString theme = m_settings->value(QStringLiteral("Appearance/theme"), QStringLiteral("Follow Desktop")).toString();
    if (!Style::instance()->setTheme(theme))
        Style::instance()->setTheme(QStringLiteral("Follow Desktop"));
    const int size = Style::sizeFromName(m_settings->value(QStringLiteral("Appearance/size")).toString());
    if (size > 0)
        Style::instance()->setSizeLevel(size);
    connect(m_jobs.get(), &JobManager::collisionRequested, this, &AppContext::onCollision);
}

AppContext::~AppContext()
{
    if (g_instance == this)
        g_instance = nullptr;
}

AppContext *AppContext::instance()
{
    return g_instance;
}

void AppContext::setupIconTheme()
{
    QStringList paths = QIcon::themeSearchPaths();
    QStringList dataDirs = qEnvironmentVariable("XDG_DATA_DIRS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (dataDirs.isEmpty())
        dataDirs = {QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share")};
    dataDirs.prepend(QDir::homePath() + QStringLiteral("/.local/share"));
    for (const QString &d : std::as_const(dataDirs)) {
        const QString icons = d + QStringLiteral("/icons");
        if (!paths.contains(icons) && QFileInfo(icons).isDir())
            paths << icons;
    }
    QIcon::setThemeSearchPaths(paths);
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor"))
        QIcon::setThemeName(QStringLiteral("breeze"));
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
}

ListerWindow *AppContext::newLister(const QString &startPath)
{
    auto *w = new ListerWindow(*this);
    if (!startPath.isEmpty())
        w->openDefault(startPath);
    else
        w->openDefault(QDir::homePath());
    m_listers.prepend(w);
    w->show();
    return w;
}

ListerWindow *AppContext::otherLister(const ListerWindow *self) const
{
    for (const auto &p : m_listers) {
        if (p && p != self)
            return p;
    }
    return nullptr;
}

ListerWindow *AppContext::activeLister() const
{
    for (const auto &p : m_listers) {
        if (p)
            return p;
    }
    return nullptr;
}

int AppContext::listerCount() const
{
    int n = 0;
    for (const auto &p : m_listers)
        n += p ? 1 : 0;
    return n;
}

void AppContext::listerActivated(ListerWindow *lister)
{
    m_listers.removeAll(QPointer<ListerWindow>(lister));
    m_listers.prepend(lister);
    m_progress->setAnchor(lister);
#ifdef LINOPUS_HAVE_KIO
    if (KioBackend *kio = KioBackend::kioInstance())
        kio->setWindow(lister);
#endif
}

void AppContext::listerClosing(ListerWindow *lister)
{
    m_listers.removeAll(QPointer<ListerWindow>(lister));
}

void AppContext::quitAll()
{
    QList<ListerWindow *> open;
    for (const auto &p : std::as_const(m_listers)) {
        if (p)
            open << p;
    }
    if (m_jobs->hasActiveJobs()) {
        const auto answer = QMessageBox::question(
            activeLister(), QStringLiteral("Jobs still running"),
            QStringLiteral("File operations are still running. Quitting cancels them; partly copied files are cleaned up. Quit anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        for (int id : m_jobs->jobIds())
            m_jobs->cancel(id);
        m_jobs->waitForIdle(5000);
    }
    // Oldest first, so the session reopens in the same stacking order.
    QList<ListerWindow *> ordered(open.crbegin(), open.crend());
    saveSession(ordered);
    m_quitting = true;
    for (ListerWindow *w : std::as_const(open))
        w->close();
}

void AppContext::restoreSession()
{
    QSettings &s = *m_settings;
    const int count = s.value(QStringLiteral("Session/listers"), 0).toInt();
    int opened = 0;
    for (int i = 0; i < count; ++i) {
        auto *w = new ListerWindow(*this);
        s.beginGroup(QStringLiteral("Lister%1").arg(i));
        const bool ok = w->restoreListerState(s);
        s.endGroup();
        if (!ok) {
            delete w;
            continue;
        }
        m_listers.append(w);
        w->show();
        ++opened;
    }
    if (opened == 0)
        newLister();
}

void AppContext::saveSession(const QList<ListerWindow *> &listers)
{
    QSettings &s = *m_settings;
    const int old = s.value(QStringLiteral("Session/listers"), 0).toInt();
    for (int i = 0; i < old; ++i)
        s.remove(QStringLiteral("Lister%1").arg(i));
    int n = 0;
    for (ListerWindow *w : listers) {
        if (!w)
            continue;
        s.beginGroup(QStringLiteral("Lister%1").arg(n++));
        w->saveListerState(s);
        s.endGroup();
    }
    s.setValue(QStringLiteral("Session/listers"), n);
    s.sync();
}

void AppContext::onCollision(const CollisionRequest &request)
{
    // Ask over the job's own progress window when it has one.
    QWidget *parent = m_progress->windowFor(request.jobId);
    if (!parent || !parent->isVisible())
        parent = activeLister();
    CollisionDialog dlg(request, 0, parent);
    dlg.exec();
    CollisionDecision d = dlg.decision();
    if (dlg.result() != QDialog::Accepted)
        d.action = CollisionAction::Skip; // closed with Esc or the window button
    m_jobs->resolveCollision(request.jobId, d);
}

bool AppContext::backupSettings(const QString &zipFile, QString *error)
{
    // The windows as they are now go into the backup too.
    QList<ListerWindow *> open;
    for (auto it = m_listers.crbegin(); it != m_listers.crend(); ++it) {
        if (*it)
            open << *it; // oldest first, as when quitting
    }
    if (!open.isEmpty())
        saveSession(open);
    m_settings->sync();
    const QDir base(Paths::configDir());
    QList<Zip::Entry> entries;
    QDirIterator it(base.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = base.relativeFilePath(path);
        if (rel.endsWith(QLatin1String(".lock")) || rel.startsWith(QLatin1String(".")))
            continue;
        if (QFileInfo(path).absoluteFilePath() == QFileInfo(zipFile).absoluteFilePath())
            continue;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        entries << Zip::Entry{QStringLiteral("linopus/") + rel, f.readAll()};
    }
    std::sort(entries.begin(), entries.end(), [](const Zip::Entry &a, const Zip::Entry &b) { return a.name < b.name; });
    if (entries.isEmpty()) {
        if (error)
            *error = QStringLiteral("There are no settings to back up yet.");
        return false;
    }
    return Zip::write(zipFile, entries, error);
}

bool AppContext::restoreSettings(const QString &zipFile, QString *error)
{
    QList<Zip::Entry> entries;
    if (!Zip::read(zipFile, entries, error))
        return false;
    QList<Zip::Entry> mine;
    for (const Zip::Entry &e : std::as_const(entries)) {
        if (!e.name.startsWith(QLatin1String("linopus/")))
            continue;
        const QString rel = e.name.mid(8);
        // Nothing may land outside the settings folder.
        if (rel.isEmpty() || rel.startsWith(QLatin1Char('/')) || rel.split(QLatin1Char('/')).contains(QStringLiteral("..")))
            continue;
        mine << Zip::Entry{rel, e.data};
    }
    if (mine.isEmpty()) {
        if (error)
            *error = QStringLiteral("This zip is not a Linopus settings backup.");
        return false;
    }
    // Write out anything pending first, so it cannot land on top of the
    // restored file later.
    m_settings->sync();
    const QDir base(Paths::configDir());
    for (const Zip::Entry &e : std::as_const(mine)) {
        const QString target = base.filePath(e.name);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QSaveFile f(target);
        if (!f.open(QIODevice::WriteOnly) || f.write(e.data) != e.data.size() || !f.commit()) {
            if (error)
                *error = QStringLiteral("Could not write %1: %2").arg(target, f.errorString());
            return false;
        }
    }
    // Take it all in now, without a restart.
    m_settings = std::make_unique<QSettings>(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
    m_aliases->load();
    m_toolbars->reload();
    FolderFormats::instance().reload();
    Style::instance()->reloadLabels();
    const QString theme = m_settings->value(QStringLiteral("Appearance/theme"), QStringLiteral("Follow Desktop")).toString();
    if (!Style::instance()->setTheme(theme))
        Style::instance()->setTheme(QStringLiteral("Follow Desktop"));
    Style::instance()->setSizeLevel(qMax(0, Style::sizeFromName(m_settings->value(QStringLiteral("Appearance/size")).toString())));
    return true;
}
