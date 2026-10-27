#include "ui/AppContext.h"
#include "core/Location.h"
#include <QPushButton>
#include "ui/FolderTab.h"
#include "ui/FileDisplay.h"

#include "core/AliasManager.h"
#include "core/CommandEngine.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "ui/CollisionDialog.h"
#include "ui/ListerWindow.h"
#include "ui/ProgressWindow.h"
#ifdef DIRDUO_HAVE_KIO
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
#include <QTemporaryFile>
#include <QFile>
#include <QSaveFile>
#include <QDirIterator>

namespace {
AppContext *g_instance = nullptr;
}

AppContext::AppContext(QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat))
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
    // Application icons from the host (Open With): its system icons and the
    // icons of installed Flatpaks.
    dataDirs << QStringLiteral("/run/host/usr/share") << QStringLiteral("/var/lib/flatpak/exports/share")
             << QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share");
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
#ifdef DIRDUO_HAVE_KIO
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

void AppContext::handleLaunch(const Launch::Request &request)
{
    ListerWindow *target = activeLister();
    for (const QString &arg : request.args) {
        const Launch::Target t = Launch::resolve(arg, request.cwd, &aliases());
        if (t.folder.isEmpty())
            continue;
        FolderTab *tab = nullptr;
        if (request.tab && target) {
            tab = target->sourceDisplay()->addTab(t.folder);
        } else {
            target = newLister(t.folder);
            tab = target->sourceDisplay()->currentTab();
        }
        if (tab && !t.select.isEmpty())
            tab->selectPaths({t.select}); // once it has been read
    }
    if (!request.commands.isEmpty()) {
        if (!target)
            target = newLister();
        for (const QString &c : request.commands)
            target->runCommand(c);
    }
    if (!target)
        target = newLister(); // started again with nothing to say: a lister
    // Wayland only lets a window come forward with the launcher's token.
    if (!request.activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", request.activationToken.toUtf8());
    if (target->isMinimized())
        target->showNormal();
    target->show();
    target->raise();
    target->activateWindow();
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
    if (!request.problem.isEmpty()) {
        // An item failed: Retry, Skip, Skip All or Abort, as in Opus.
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Problem"),
                        QStringLiteral("<b>%1</b> could not be copied.<p>%2</p>")
                            .arg(Location::display(request.source).toHtmlEscaped(), request.problem.toHtmlEscaped()),
                        QMessageBox::NoButton, parent);
        box.setObjectName(QStringLiteral("problemBox"));
        QPushButton *retry = box.addButton(QStringLiteral("&Retry"), QMessageBox::AcceptRole);
        QPushButton *skip = box.addButton(QStringLiteral("&Skip"), QMessageBox::RejectRole);
        QPushButton *skipAll = box.addButton(QStringLiteral("Skip &All"), QMessageBox::RejectRole);
        QPushButton *abort = box.addButton(QStringLiteral("A&bort"), QMessageBox::DestructiveRole);
        box.setDefaultButton(retry);
        box.setEscapeButton(skip);
        box.exec();
        CollisionDecision d;
        if (box.clickedButton() == retry)
            d.action = CollisionAction::Retry;
        else if (box.clickedButton() == abort)
            d.action = CollisionAction::CancelJob;
        else {
            d.action = CollisionAction::Skip;
            d.applyToAll = box.clickedButton() == skipAll;
        }
        Q_UNUSED(skip)
        m_jobs->resolveCollision(request.jobId, d);
        return;
    }
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
        entries << Zip::Entry{QStringLiteral("dirduo/") + rel, f.readAll()};
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
        // Backups made before the rename keep their files under linopus/.
        QString rel;
        if (e.name.startsWith(QLatin1String("dirduo/")))
            rel = e.name.mid(7);
        else if (e.name.startsWith(QLatin1String("linopus/")))
            rel = e.name.mid(8);
        else
            continue;
        if (rel == QLatin1String("linopus.ini"))
            rel = QStringLiteral("dirduo.ini");
        // Nothing may land outside the settings folder.
        if (rel.isEmpty() || rel.startsWith(QLatin1Char('/')) || rel.split(QLatin1Char('/')).contains(QStringLiteral("..")))
            continue;
        mine << Zip::Entry{rel, e.data};
    }
    if (mine.isEmpty()) {
        if (error)
            *error = QStringLiteral("This zip is not a DirDuo settings backup.");
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
    m_settings = std::make_unique<QSettings>(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
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

namespace {
QString layoutFile(const QString &name)
{
    return Paths::configFile(QStringLiteral("layouts/") + name + QStringLiteral(".ini"));
}
} // namespace

QStringList AppContext::layoutNames() const
{
    QStringList out;
    for (const QFileInfo &fi : QDir(Paths::configFile(QStringLiteral("layouts"))).entryInfoList({QStringLiteral("*.ini")}, QDir::Files,
                                                                                               QDir::Name | QDir::IgnoreCase))
        out << fi.completeBaseName();
    return out;
}

bool AppContext::saveLayout(const QString &name, ListerWindow *from, bool withFolders, QString *error)
{
    if (name.trimmed().isEmpty() || name.contains(QLatin1Char('/')) || !from) {
        if (error)
            *error = QStringLiteral("A layout needs a name without /.");
        return false;
    }
    QDir().mkpath(Paths::configFile(QStringLiteral("layouts")));
    QFile::remove(layoutFile(name.trimmed()));
    QSettings s(layoutFile(name.trimmed()), QSettings::IniFormat);
    from->saveListerState(s);
    s.setValue(QStringLiteral("Layout/withFolders"), withFolders);
    s.sync();
    if (s.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("Could not write %1.").arg(layoutFile(name));
        return false;
    }
    return true;
}

bool AppContext::deleteLayout(const QString &name)
{
    return QFile::remove(layoutFile(name));
}

ListerWindow *AppContext::loadLayout(const QString &name, ListerWindow *replace, QString *error)
{
    if (!QFileInfo::exists(layoutFile(name))) {
        if (error)
            *error = QStringLiteral("There is no layout called %1.").arg(name);
        return nullptr;
    }
    // Build what to restore in a scratch file: the layout, with the current
    // folders (tabs and their views) kept when the layout has none of its own.
    QTemporaryFile scratch(QDir::tempPath() + QStringLiteral("/dirduo-layout-XXXXXX.ini"));
    if (!scratch.open()) {
        if (error)
            *error = QStringLiteral("Could not prepare the layout.");
        return nullptr;
    }
    scratch.close();
    {
        QSettings layout(layoutFile(name), QSettings::IniFormat);
        QSettings out(scratch.fileName(), QSettings::IniFormat);
        const bool withFolders = layout.value(QStringLiteral("Layout/withFolders"), false).toBool();
        if (!withFolders && replace)
            replace->saveListerState(out);
        static const QStringList folderKeys = {QStringLiteral("tabs"), QStringLiteral("views"), QStringLiteral("current"),
                                               QStringLiteral("groups"), QStringLiteral("flat"), QStringLiteral("extras"),
                                               QStringLiteral("columns")};
        for (const QString &key : layout.allKeys()) {
            const bool folderKey = key.startsWith(QLatin1String("display")) && folderKeys.contains(key.section(QLatin1Char('/'), 1));
            if (folderKey && !withFolders && replace)
                continue;
            out.setValue(key, layout.value(key));
        }
        out.sync();
    }
    QSettings in(scratch.fileName(), QSettings::IniFormat);
    auto *w = new ListerWindow(*this);
    if (!w->restoreListerState(in)) {
        delete w;
        if (error)
            *error = QStringLiteral("The layout could not be read.");
        return nullptr;
    }
    m_listers.prepend(w);
    w->show();
    if (replace) {
        // The old window goes quietly; the session is saved from the new one.
        m_listers.removeAll(QPointer<ListerWindow>(replace));
        replace->setAttribute(Qt::WA_DeleteOnClose);
        replace->hide();
        replace->deleteLater();
    }
    w->activateWindow();
    return w;
}
