#include "ui/ListerWindow.h"

#include "core/AliasManager.h"
#include "core/FileOps.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "ui/AppContext.h"
#include "ui/BreadcrumbBar.h"
#include "ui/ButtonBar.h"
#include "ui/FileDisplay.h"
#include "ui/FolderTab.h"
#include "ui/ProgressWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSplitter>
#include <QUrl>

namespace {

bool parseSwitch(const QString &value, bool current, bool &out)
{
    const QString v = value.toLower();
    if (v.isEmpty() || v == QLatin1String("toggle"))
        out = !current;
    else if (v == QLatin1String("on") || v == QLatin1String("true") || v == QLatin1String("yes"))
        out = true;
    else if (v == QLatin1String("off") || v == QLatin1String("false") || v == QLatin1String("no"))
        out = false;
    else
        return false;
    return true;
}

} // namespace

ListerWindow::ListerWindow(AppContext &app, QWidget *parent)
    : QMainWindow(parent)
    , m_app(app)
    , m_split(new QSplitter(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon::fromTheme(QStringLiteral(LINOPUS_APP_ID), QIcon::fromTheme(QStringLiteral("system-file-manager"))));
    resize(1100, 680);

    for (int i = 0; i < 2; ++i) {
        auto *d = new FileDisplay(app.aliases(), m_split);
        m_displays[i] = d;
        m_split->addWidget(d);
        connect(d, &FileDisplay::activated, this, &ListerWindow::makeSource);
        connect(d, &FileDisplay::currentPathChanged, this, [this, i](FileDisplay *from) {
            const QString now = from->currentTab() ? from->currentTab()->path() : QString();
            const QString before = m_lastPath[i];
            m_lastPath[i] = now;
            if (m_navLock && !before.isEmpty())
                mirrorNavigation(from, before, now);
            updateTitle();
            syncViewActions();
        });
        connect(d, &FileDisplay::navigateRequested, this, &ListerWindow::onNavigateRequested);
        connect(d, &FileDisplay::renameRequested, this, &ListerWindow::onRenameRequested);
        connect(d, &FileDisplay::dropRequested, this, &ListerWindow::onDropRequested);
        connect(d, &FileDisplay::openFileRequested, this, &ListerWindow::openFile);
        connect(d, &FileDisplay::contextMenuRequested, this, &ListerWindow::onContextMenu);
        connect(d, &FileDisplay::tabKeyPressed, this, [this](FileDisplay *from) {
            if (!m_dual)
                return;
            FileDisplay *other = from == m_displays[0] ? m_displays[1] : m_displays[0];
            if (auto *t = other->currentTab())
                t->focusView(); // focusing it makes it the source unless it is a locked dest
        });
    }
    m_split->setChildrenCollapsible(false);
    setCentralWidget(m_split);

    buildActions();
    buildMenus();
    buildToolbar();
    updateRoles();
}

// ---------------------------------------------------------------- setup

QAction *ListerWindow::commandAction(const QString &text, const QString &command, const QKeySequence &key,
                                     const QString &icon)
{
    auto *a = new QAction(text, this);
    if (!key.isEmpty())
        a->setShortcut(key);
    if (!icon.isEmpty())
        a->setIcon(QIcon::fromTheme(icon));
    a->setData(command);
    a->setStatusTip(command);
    connect(a, &QAction::triggered, this, [this, command]() { runCommand(command); });
    return a;
}

void ListerWindow::buildActions()
{
    auto *layoutGroup = new QActionGroup(this);
    m_actSingle = commandAction(QStringLiteral("Single Display"), QStringLiteral("Set DUAL=off"));
    m_actDualSide = commandAction(QStringLiteral("Dual Vertical (side by side)"), QStringLiteral("Set DUAL=vertical"),
                                  QKeySequence(QStringLiteral("Ctrl+Shift+V")), QStringLiteral("view-split-left-right"));
    m_actDualStacked = commandAction(QStringLiteral("Dual Horizontal (top and bottom)"), QStringLiteral("Set DUAL=horizontal"),
                                     QKeySequence(QStringLiteral("Ctrl+Shift+H")), QStringLiteral("view-split-top-bottom"));
    m_actSingle->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+1")));
    for (QAction *a : {m_actSingle, m_actDualSide, m_actDualStacked}) {
        a->setCheckable(true);
        layoutGroup->addAction(a);
    }

    auto *viewGroup = new QActionGroup(this);
    m_actDetails = commandAction(QStringLiteral("Details"), QStringLiteral("Set VIEW=details"), QKeySequence(QStringLiteral("Ctrl+1")));
    m_actList = commandAction(QStringLiteral("List"), QStringLiteral("Set VIEW=list"), QKeySequence(QStringLiteral("Ctrl+2")));
    m_actIcons = commandAction(QStringLiteral("Icons"), QStringLiteral("Set VIEW=icons"), QKeySequence(QStringLiteral("Ctrl+3")));
    for (QAction *a : {m_actDetails, m_actList, m_actIcons}) {
        a->setCheckable(true);
        viewGroup->addAction(a);
    }

    m_actHidden = commandAction(QStringLiteral("Show Hidden Files"), QStringLiteral("Set HIDDEN=toggle"), QKeySequence(QStringLiteral("Ctrl+H")));
    m_actHidden->setCheckable(true);
    m_actLock = commandAction(QStringLiteral("Lock Destination"), QStringLiteral("Set DESTLOCK=toggle"));
    m_actLock->setCheckable(true);
    m_actNavLock = commandAction(QStringLiteral("Navigation Lock"), QStringLiteral("Set NAVLOCK=toggle"));
    m_actNavLock->setCheckable(true);
    m_actNavLock->setToolTip(QStringLiteral("The other display follows when you go into or out of folders"));
}

void ListerWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    QAction *newLister = file->addAction(QIcon::fromTheme(QStringLiteral("window-new")), QStringLiteral("New Lister"));
    newLister->setShortcut(QKeySequence::New);
    connect(newLister, &QAction::triggered, this, [this]() { m_app.newLister(sourcePath()); });
    file->addAction(commandAction(QStringLiteral("New Tab"), QStringLiteral("Go {sourcepath} NEWTAB"),
                                  QKeySequence(QStringLiteral("Ctrl+T")), QStringLiteral("tab-new")));
    QAction *closeTab = file->addAction(QIcon::fromTheme(QStringLiteral("tab-close")), QStringLiteral("Close Tab"));
    closeTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    connect(closeTab, &QAction::triggered, this, [this]() {
        FileDisplay *d = sourceDisplay();
        if (d->tabCount() > 1)
            d->closeTab(d->currentIndex());
        else
            close();
    });
    QAction *nextTab = new QAction(QStringLiteral("Next Tab"), this);
    nextTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")));
    connect(nextTab, &QAction::triggered, this, [this]() { sourceDisplay()->cycleTab(1); });
    QAction *prevTab = new QAction(QStringLiteral("Previous Tab"), this);
    prevTab->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")));
    connect(prevTab, &QAction::triggered, this, [this]() { sourceDisplay()->cycleTab(-1); });
    file->addAction(nextTab);
    file->addAction(prevTab);
    file->addSeparator();
    QAction *closeLister = file->addAction(QStringLiteral("Close Lister"));
    closeLister->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(closeLister, &QAction::triggered, this, &QWidget::close);
    QAction *quit = file->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, [this]() { m_app.quitAll(); });

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit->addAction(commandAction(QStringLiteral("Cut"), QStringLiteral("Clipboard CUT"), QKeySequence::Cut, QStringLiteral("edit-cut")));
    edit->addAction(commandAction(QStringLiteral("Copy"), QStringLiteral("Clipboard COPY"), QKeySequence::Copy, QStringLiteral("edit-copy")));
    edit->addAction(commandAction(QStringLiteral("Paste"), QStringLiteral("Clipboard PASTE"), QKeySequence::Paste, QStringLiteral("edit-paste")));
    edit->addSeparator();
    edit->addAction(commandAction(QStringLiteral("Copy to Destination"), QStringLiteral("Copy"), {}, QStringLiteral("edit-copy")));
    edit->addAction(commandAction(QStringLiteral("Copy As..."), QStringLiteral("Copy AS")));
    edit->addAction(commandAction(QStringLiteral("Move to Destination"), QStringLiteral("Copy MOVE"), {}, QStringLiteral("transform-move")));
    edit->addAction(commandAction(QStringLiteral("Move As..."), QStringLiteral("Copy MOVE AS")));
    edit->addSeparator();
    edit->addAction(commandAction(QStringLiteral("Rename"), QStringLiteral("Rename"), QKeySequence(Qt::Key_F2), QStringLiteral("edit-rename")));
    edit->addAction(commandAction(QStringLiteral("New Folder..."), QStringLiteral("CreateFolder"),
                                  QKeySequence(QStringLiteral("Ctrl+Shift+N")), QStringLiteral("folder-new")));
    edit->addAction(commandAction(QStringLiteral("Move to Trash"), QStringLiteral("Delete"), QKeySequence::Delete, QStringLiteral("user-trash")));
    edit->addAction(commandAction(QStringLiteral("Delete Permanently"), QStringLiteral("Delete NORECYCLE"),
                                  QKeySequence(QStringLiteral("Shift+Del")), QStringLiteral("edit-delete")));
    edit->addSeparator();
    edit->addAction(commandAction(QStringLiteral("Select All"), QStringLiteral("Select ALL"), QKeySequence::SelectAll));
    edit->addAction(commandAction(QStringLiteral("Select None"), QStringLiteral("Select NONE"), QKeySequence(QStringLiteral("Ctrl+Shift+A"))));
    edit->addAction(commandAction(QStringLiteral("Invert Selection"), QStringLiteral("Select INVERT"), QKeySequence(QStringLiteral("Ctrl+I"))));
    QAction *pattern = edit->addAction(QStringLiteral("Select by Pattern..."));
    pattern->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(pattern, &QAction::triggered, this, [this]() { runCommand(QStringLiteral("Select PATTERN={dlgstring|Select files matching (for example *.mp3):}")); });

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(m_actSingle);
    view->addAction(m_actDualSide);
    view->addAction(m_actDualStacked);
    view->addSeparator();
    view->addAction(m_actDetails);
    view->addAction(m_actList);
    view->addAction(m_actIcons);
    view->addSeparator();
    view->addAction(m_actHidden);
    view->addAction(m_actLock);
    view->addAction(m_actNavLock);
    view->addAction(commandAction(QStringLiteral("Swap Displays"), QStringLiteral("Go SWAP")));
    view->addAction(commandAction(QStringLiteral("Show Source in Destination"), QStringLiteral("Go {sourcepath} OPENINDEST")));
    view->addSeparator();
    view->addAction(commandAction(QStringLiteral("Refresh"), QStringLiteral("Go REFRESH"), QKeySequence::Refresh, QStringLiteral("view-refresh")));

    QMenu *go = menuBar()->addMenu(QStringLiteral("&Go"));
    go->addAction(commandAction(QStringLiteral("Back"), QStringLiteral("Go BACK"), QKeySequence::Back, QStringLiteral("go-previous")));
    go->addAction(commandAction(QStringLiteral("Forward"), QStringLiteral("Go FORWARD"), QKeySequence::Forward, QStringLiteral("go-next")));
    go->addAction(commandAction(QStringLiteral("Up"), QStringLiteral("Go UP"), QKeySequence(QStringLiteral("Alt+Up")), QStringLiteral("go-up")));
    go->addAction(commandAction(QStringLiteral("Home"), QStringLiteral("Go ~"), QKeySequence(QStringLiteral("Alt+Home")), QStringLiteral("go-home")));
    QAction *location = go->addAction(QStringLiteral("Edit Location"));
    location->setShortcuts({QKeySequence(QStringLiteral("Ctrl+L")), QKeySequence(QStringLiteral("Alt+D"))});
    connect(location, &QAction::triggered, this, [this]() { sourceDisplay()->breadcrumb()->startEditing(); });
    go->addSeparator();
    m_aliasMenu = go->addMenu(QIcon::fromTheme(QStringLiteral("bookmarks")), QStringLiteral("Aliases"));
    connect(m_aliasMenu, &QMenu::aboutToShow, this, &ListerWindow::rebuildAliasMenu);

    QMenu *window = menuBar()->addMenu(QStringLiteral("&Window"));
    QAction *jobs = window->addAction(QIcon::fromTheme(QStringLiteral("view-process-all")), QStringLiteral("File Operations"));
    jobs->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    connect(window, &QMenu::aboutToShow, this, [this, jobs]() { jobs->setEnabled(m_app.progress().count() > 0); });
    connect(jobs, &QAction::triggered, this, [this]() { m_app.progress().showAll(); });

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction *about = help->addAction(QStringLiteral("About Linopus"));
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("About Linopus"),
                           QStringLiteral("<b>Linopus %1</b><p>A source and destination file manager for Linux.</p>"
                                          "<p>Configuration: %2</p>")
                               .arg(QStringLiteral(LINOPUS_VERSION), Paths::configDir().toHtmlEscaped()));
    });
}

void ListerWindow::buildToolbar()
{
    bool ok = false;
    QList<ButtonDef> defs = ButtonBar::load(Paths::configFile(QStringLiteral("toolbars/default.json")), &ok);
    if (!ok)
        defs = ButtonBar::defaultButtons();
    m_buttons = new ButtonBar(QStringLiteral("Buttons"), defs, this);
    connect(m_buttons, &ButtonBar::commandTriggered, this, [this](const QString &cmd) { runCommand(cmd); });
    addToolBar(Qt::TopToolBarArea, m_buttons);
}

// ---------------------------------------------------------------- layout & roles

void ListerWindow::openDefault(const QString &path)
{
    for (FileDisplay *d : m_displays) {
        if (d->tabCount() == 0)
            d->addTab(path);
    }
    setDual(true);
    setSideBySide(true);
    m_source = 0;
    updateRoles();
    if (auto *t = sourceTab())
        t->focusView();
}

FileDisplay *ListerWindow::sourceDisplay() const
{
    return m_displays[m_source];
}

FileDisplay *ListerWindow::destDisplay() const
{
    return m_dual ? m_displays[1 - m_source] : nullptr;
}

FolderTab *ListerWindow::sourceTab() const
{
    return sourceDisplay()->currentTab();
}

void ListerWindow::setDual(bool on)
{
    m_dual = on;
    // In single mode the source display stays and the other one hides.
    m_displays[1 - m_source]->setVisible(on);
    m_displays[m_source]->setVisible(true);
    if (!on)
        m_displays[1 - m_source]->setDestLocked(false);
    updateRoles();
    syncViewActions();
}

void ListerWindow::setSideBySide(bool sideBySide)
{
    m_sideBySide = sideBySide;
    m_split->setOrientation(sideBySide ? Qt::Horizontal : Qt::Vertical);
    const int total = sideBySide ? m_split->width() : m_split->height();
    if (total > 0)
        m_split->setSizes({total / 2, total - total / 2});
    syncViewActions();
}

void ListerWindow::makeSource(FileDisplay *d)
{
    m_app.listerActivated(this);
    if (!m_dual)
        return;
    const int idx = d == m_displays[0] ? 0 : 1;
    if (idx == m_source)
        return;
    if (m_displays[idx]->destLocked())
        return; // a locked destination stays the destination
    m_source = idx;
    updateRoles();
}

void ListerWindow::updateRoles()
{
    for (int i = 0; i < 2; ++i) {
        FileDisplay *d = m_displays[i];
        d->setShowRoleBadge(m_dual);
        if (!m_dual)
            d->setRole(i == m_source ? FileDisplay::Role::Source : FileDisplay::Role::None);
        else
            d->setRole(i == m_source ? FileDisplay::Role::Source : FileDisplay::Role::Dest);
    }
    updateTitle();
    syncViewActions();
}

void ListerWindow::updateTitle()
{
    FolderTab *t = sourceTab();
    setWindowTitle(t ? QStringLiteral("%1 - Linopus").arg(t->title()) : QStringLiteral("Linopus"));
}

void ListerWindow::syncViewActions()
{
    if (!m_actSingle)
        return;
    m_actSingle->setChecked(!m_dual);
    m_actDualSide->setChecked(m_dual && m_sideBySide);
    m_actDualStacked->setChecked(m_dual && !m_sideBySide);
    if (FolderTab *t = sourceTab()) {
        m_actDetails->setChecked(t->viewMode() == FolderTab::ViewMode::Details);
        m_actList->setChecked(t->viewMode() == FolderTab::ViewMode::List);
        m_actIcons->setChecked(t->viewMode() == FolderTab::ViewMode::Icons);
    }
    m_actHidden->setChecked(sourceDisplay()->showHidden());
    FileDisplay *dest = destDisplay();
    m_actLock->setEnabled(dest != nullptr);
    m_actLock->setChecked(dest && dest->destLocked());
    m_actNavLock->setEnabled(dest != nullptr);
    m_actNavLock->setChecked(m_navLock);
}

void ListerWindow::mirrorNavigation(FileDisplay *from, const QString &oldPath, const QString &newPath)
{
    if (m_mirroring || !m_dual || oldPath == newPath)
        return;
    FileDisplay *other = from == m_displays[0] ? m_displays[1] : m_displays[0];
    FolderTab *t = other->currentTab();
    if (!t)
        return;
    QString target;
    if (newPath.startsWith(oldPath == QLatin1String("/") ? oldPath : oldPath + QLatin1Char('/'))) {
        // Went down: follow into the same relative folder, if it exists there.
        const QString rel = newPath.mid(oldPath == QLatin1String("/") ? 1 : oldPath.size() + 1);
        target = QDir::cleanPath(t->path() + QLatin1Char('/') + rel);
    } else if (oldPath.startsWith(newPath == QLatin1String("/") ? newPath : newPath + QLatin1Char('/'))) {
        // Went up: climb the same number of levels.
        const int levels = QDir::cleanPath(oldPath).count(QLatin1Char('/')) - QDir::cleanPath(newPath).count(QLatin1Char('/'));
        target = t->path();
        for (int i = 0; i < levels && target != QLatin1String("/"); ++i)
            target = QFileInfo(target).absolutePath();
    } else {
        return; // a jump elsewhere is not mirrored
    }
    if (!QFileInfo(target).isDir())
        return;
    m_mirroring = true;
    t->navigate(target);
    m_mirroring = false;
}

// ---------------------------------------------------------------- commands

bool ListerWindow::runCommand(const QString &script)
{
    return m_app.engine().run(script, *this);
}

QString ListerWindow::sourcePath() const
{
    FolderTab *t = sourceTab();
    return t ? t->path() : QDir::homePath();
}

QString ListerWindow::destPath() const
{
    if (FileDisplay *d = destDisplay())
        return d->currentTab() ? d->currentTab()->path() : QString();
    // Single display: the most recently used other lister is the destination.
    if (ListerWindow *other = m_app.otherLister(this))
        return other->sourcePath();
    return {};
}

QStringList ListerWindow::selectedPaths() const
{
    FolderTab *t = sourceTab();
    return t ? t->selectedPaths() : QStringList();
}

QString ListerWindow::focusedPath() const
{
    FolderTab *t = sourceTab();
    return t ? t->focusedPath() : QString();
}

bool ListerWindow::promptText(const QString &title, const QString &label, QString &value)
{
    bool ok = false;
    const QString result = QInputDialog::getText(this, title, label, QLineEdit::Normal, value, &ok);
    if (!ok)
        return false;
    value = result;
    return true;
}

bool ListerWindow::confirm(const QString &title, const QString &text)
{
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void ListerWindow::showError(const QString &title, const QString &text)
{
    QMessageBox::warning(this, title, text);
}

QString ListerWindow::chooseDestination(const QString &title)
{
    // Qt's own dialog: the portal one would hand back a sandboxed path.
    return QFileDialog::getExistingDirectory(this, title, sourcePath(),
                                             QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
}

bool ListerWindow::go(const ParsedCommand &cmd, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    if (cmd.hasFlag(QStringLiteral("SWAP"))) {
        if (!m_dual)
            return fail(QStringLiteral("Swap needs two displays."));
        FolderTab *a = m_displays[0]->currentTab();
        FolderTab *b = m_displays[1]->currentTab();
        const QString pa = a->path();
        const QString pb = b->path();
        a->navigate(pb);
        b->navigate(pa);
        return true;
    }

    FileDisplay *target = sourceDisplay();
    ListerWindow *targetLister = this;
    if (cmd.hasFlag(QStringLiteral("OPENINDEST"))) {
        if (m_dual) {
            target = destDisplay();
        } else if (ListerWindow *other = m_app.otherLister(this)) {
            targetLister = other;
            target = other->sourceDisplay();
        } else {
            return fail(QStringLiteral("There is no destination to open it in."));
        }
    }
    FolderTab *tab = target->currentTab();

    if (cmd.hasFlag(QStringLiteral("UP")))
        return tab->up() || true;
    if (cmd.hasFlag(QStringLiteral("BACK")))
        return tab->back() || true;
    if (cmd.hasFlag(QStringLiteral("FORWARD")))
        return tab->forward() || true;
    if (cmd.hasFlag(QStringLiteral("REFRESH"))) {
        tab->refresh();
        return true;
    }

    const QString path = cmd.value(QStringLiteral("PATH"));
    if (path.isEmpty()) {
        if (cmd.hasFlag(QStringLiteral("NEWTAB"))) {
            target->addTab(tab->path());
            return true;
        }
        return fail(QStringLiteral("Go needs a folder, or UP, BACK, FORWARD, REFRESH or SWAP."));
    }

    FolderTab *dest = tab;
    if (cmd.hasFlag(QStringLiteral("NEWTAB")))
        dest = target->addTab(path);
    else
        tab->navigate(path);
    const QString select = cmd.value(QStringLiteral("SELECT"));
    if (!select.isEmpty())
        dest->selectPaths({select});
    if (targetLister == this && target == sourceDisplay())
        dest->focusView();
    return true;
}

bool ListerWindow::set(const ParsedCommand &cmd, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (cmd.args.isEmpty())
        return fail(QStringLiteral("Set needs something to set, such as DUAL=toggle."));

    for (auto it = cmd.args.cbegin(); it != cmd.args.cend(); ++it) {
        const QString key = it.key();
        const QString value = it.value().value(0).toLower();
        if (key == QLatin1String("DUAL")) {
            if (value == QLatin1String("vertical") || value == QLatin1String("vert") || value == QLatin1String("sidebyside")) {
                setSideBySide(true);
                setDual(true);
            } else if (value == QLatin1String("horizontal") || value == QLatin1String("horiz") || value == QLatin1String("stacked")) {
                setSideBySide(false);
                setDual(true);
            } else if (value == QLatin1String("flip")) {
                setSideBySide(!m_sideBySide);
                setDual(true);
            } else {
                bool on = m_dual;
                if (!parseSwitch(value, m_dual, on))
                    return fail(QStringLiteral("DUAL takes on, off, toggle, vertical, horizontal or flip."));
                setDual(on);
            }
        } else if (key == QLatin1String("SOURCE")) {
            if (!m_dual)
                return fail(QStringLiteral("There is only one display."));
            m_displays[0]->setDestLocked(false);
            m_displays[1]->setDestLocked(false);
            m_source = 1 - m_source;
            updateRoles();
            sourceTab()->focusView();
        } else if (key == QLatin1String("DESTLOCK")) {
            FileDisplay *dest = destDisplay();
            if (!dest)
                return fail(QStringLiteral("Locking the destination needs two displays."));
            bool on = dest->destLocked();
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("DESTLOCK takes on, off or toggle."));
            dest->setDestLocked(on);
            syncViewActions();
        } else if (key == QLatin1String("NAVLOCK")) {
            if (!m_dual)
                return fail(QStringLiteral("Navigation lock needs two displays."));
            bool on = m_navLock;
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("NAVLOCK takes on, off or toggle."));
            m_navLock = on;
            for (int i = 0; i < 2; ++i)
                m_lastPath[i] = m_displays[i]->currentTab() ? m_displays[i]->currentTab()->path() : QString();
            syncViewActions();
        } else if (key == QLatin1String("VIEW")) {
            FolderTab::ViewMode mode;
            if (!FolderTab::viewModeFromName(value, mode))
                return fail(QStringLiteral("VIEW takes details, list or icons."));
            sourceTab()->setViewMode(mode);
            syncViewActions();
        } else if (key == QLatin1String("HIDDEN")) {
            bool on = sourceDisplay()->showHidden();
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("HIDDEN takes on, off or toggle."));
            for (FileDisplay *d : m_displays)
                d->setShowHidden(on);
            syncViewActions();
        } else {
            return fail(QStringLiteral("Set does not know %1.").arg(key));
        }
    }
    return true;
}

bool ListerWindow::select(const ParsedCommand &cmd, QString *error)
{
    FolderTab *t = sourceTab();
    if (cmd.hasArg(QStringLiteral("PATTERN"))) {
        t->selectPattern(cmd.value(QStringLiteral("PATTERN")));
        return true;
    }
    if (cmd.hasFlag(QStringLiteral("ALL")))
        t->selectAll();
    else if (cmd.hasFlag(QStringLiteral("NONE")))
        t->selectNone();
    else if (cmd.hasFlag(QStringLiteral("INVERT")))
        t->invertSelection();
    else {
        if (error)
            *error = QStringLiteral("Select takes ALL, NONE, INVERT or PATTERN=.");
        return false;
    }
    return true;
}

bool ListerWindow::beginInlineRename()
{
    FolderTab *t = sourceTab();
    return t && t->beginRename();
}

void ListerWindow::afterLocalChange(const QStringList &paths)
{
    FolderTab *t = m_changeTarget ? m_changeTarget.data() : sourceTab();
    if (!t)
        return;
    t->refresh();
    t->selectPaths(paths);
}

// ---------------------------------------------------------------- events from displays

void ListerWindow::onNavigateRequested(FileDisplay *d, const QString &input)
{
    makeSource(d);
    // The address bar is a Go command like any other.
    ParsedCommand cmd;
    cmd.name = QStringLiteral("Go");
    cmd.raw = QStringLiteral("Go ") + input;
    cmd.positional << input;
    if (d != sourceDisplay())
        cmd.positional << QStringLiteral("OPENINDEST"); // a locked dest navigates itself
    QString error;
    if (!m_app.engine().execute(cmd, *this, &error) && !error.isEmpty())
        showError(QStringLiteral("Go"), error);
    if (auto *t = d->currentTab())
        t->focusView();
}

void ListerWindow::onRenameRequested(FileDisplay *d, const QString &path, const QString &newName)
{
    ParsedCommand cmd;
    cmd.name = QStringLiteral("Rename");
    cmd.args[QStringLiteral("FROM")] = QStringList{path};
    cmd.args[QStringLiteral("TO")] = QStringList{newName};
    m_changeTarget = d->currentTab();
    QString error;
    const bool ok = m_app.engine().execute(cmd, *this, &error);
    m_changeTarget.clear();
    if (!ok && !error.isEmpty())
        showError(QStringLiteral("Rename"), error);
}

void ListerWindow::onDropRequested(FileDisplay *d, const QList<QUrl> &urls, const QString &targetDir, Qt::DropAction)
{
    QStringList sources;
    for (const QUrl &u : urls) {
        if (u.isLocalFile())
            sources << QDir::cleanPath(u.toLocalFile());
    }
    if (sources.isEmpty())
        return;

    const Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    bool move;
    if (mods & Qt::ControlModifier)
        move = false;
    else if (mods & Qt::ShiftModifier)
        move = true;
    else
        move = FileOps::sameDevice(sources.first(), targetDir); // same drive moves, another drive copies

    // Dropping things back where they already are does nothing (Ctrl forces copies).
    bool allHere = true;
    for (const QString &s : std::as_const(sources))
        allHere = allHere && QFileInfo(s).absolutePath() == QDir::cleanPath(targetDir);
    if (allHere && !(mods & Qt::ControlModifier))
        return;

    makeSource(d);
    m_app.engine().copyItems(sources, targetDir, move);
}

void ListerWindow::onContextMenu(FileDisplay *d, const QPoint &globalPos, bool onItem)
{
    makeSource(d);
    QMenu menu(this);
    FolderTab *t = d->currentTab();
    const QStringList sel = t ? t->selectedPaths() : QStringList();
    const bool oneFolder = sel.size() == 1 && QFileInfo(sel.first()).isDir();

    if (onItem && !sel.isEmpty()) {
        if (oneFolder) {
            const QString p = sel.first();
            menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), QStringLiteral("Open"), this, [t, p]() { t->navigate(p); });
            menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), QStringLiteral("Open in New Tab"), this,
                           [d, p]() { d->addTab(p, false); });
            if (m_dual)
                menu.addAction(QStringLiteral("Open in Destination"), this, [this, p]() {
                    runCommand(QStringLiteral("Go \"%1\" OPENINDEST").arg(p));
                });
        } else {
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open"), this, [this, sel]() {
                for (const QString &p : sel)
                    openFile(p);
            });
        }
        menu.addSeparator();
        menu.addAction(commandAction(QStringLiteral("Cut"), QStringLiteral("Clipboard CUT"), {}, QStringLiteral("edit-cut")));
        menu.addAction(commandAction(QStringLiteral("Copy"), QStringLiteral("Clipboard COPY"), {}, QStringLiteral("edit-copy")));
    }
    QAction *paste = commandAction(QStringLiteral("Paste"), QStringLiteral("Clipboard PASTE"), {}, QStringLiteral("edit-paste"));
    const QMimeData *clip = QApplication::clipboard()->mimeData();
    paste->setEnabled(clip && clip->hasUrls());
    menu.addAction(paste);

    if (onItem && !sel.isEmpty()) {
        menu.addSeparator();
        QAction *copyTo = commandAction(QStringLiteral("Copy to Destination"), QStringLiteral("Copy"));
        QAction *moveTo = commandAction(QStringLiteral("Move to Destination"), QStringLiteral("Copy MOVE"));
        menu.addAction(copyTo);
        menu.addAction(moveTo);
        menu.addSeparator();
        menu.addAction(commandAction(QStringLiteral("Rename"), QStringLiteral("Rename"), {}, QStringLiteral("edit-rename")));
        menu.addAction(commandAction(QStringLiteral("Move to Trash"), QStringLiteral("Delete"), {}, QStringLiteral("user-trash")));
        menu.addAction(commandAction(QStringLiteral("Delete Permanently"), QStringLiteral("Delete NORECYCLE"), {}, QStringLiteral("edit-delete")));
    }
    menu.addSeparator();
    menu.addAction(commandAction(QStringLiteral("New Folder..."), QStringLiteral("CreateFolder"), {}, QStringLiteral("folder-new")));
    menu.addAction(m_actHidden);
    menu.exec(globalPos);
}

void ListerWindow::openFile(const QString &path)
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        showError(QStringLiteral("Open"), QStringLiteral("No application is set up to open \"%1\".").arg(QFileInfo(path).fileName()));
}

void ListerWindow::rebuildAliasMenu()
{
    m_aliasMenu->clear();
    AliasManager &aliases = m_app.aliases();
    const QStringList names = aliases.names();
    for (const QString &n : names) {
        const QString target = aliases.aliasPath(n);
        QAction *a = m_aliasMenu->addAction(QStringLiteral("/%1").arg(n));
        a->setToolTip(target);
        a->setEnabled(QFileInfo(target).isDir());
        connect(a, &QAction::triggered, this, [this, n]() { runCommand(QStringLiteral("Go /%1").arg(n)); });
    }
    m_aliasMenu->addSeparator();
    QAction *add = m_aliasMenu->addAction(QStringLiteral("Make Alias for This Folder..."));
    connect(add, &QAction::triggered, this, [this]() {
        QString name = QFileInfo(sourcePath()).fileName().toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
        if (!promptText(QStringLiteral("New Alias"), QStringLiteral("Alias name for %1:").arg(sourcePath()), name))
            return;
        QString error;
        AliasManager &al = m_app.aliases();
        if (!al.setUserAlias(name.trimmed(), sourcePath(), &error)) {
            showError(QStringLiteral("New Alias"), error);
            return;
        }
        al.save();
    });
}

// ---------------------------------------------------------------- session

void ListerWindow::saveListerState(QSettings &s) const
{
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("windowState"), QMainWindow::saveState());
    s.setValue(QStringLiteral("dual"), m_dual);
    s.setValue(QStringLiteral("sideBySide"), m_sideBySide);
    s.setValue(QStringLiteral("splitter"), m_split->saveState());
    s.setValue(QStringLiteral("source"), m_source);
    s.setValue(QStringLiteral("hidden"), m_displays[0]->showHidden());
    for (int i = 0; i < 2; ++i) {
        const FileDisplay *d = m_displays[i];
        QStringList paths;
        QStringList modes;
        for (int t = 0; t < d->tabCount(); ++t) {
            paths << d->tab(t)->path();
            modes << FolderTab::viewModeName(d->tab(t)->viewMode());
        }
        const QString prefix = QStringLiteral("display%1/").arg(i);
        s.setValue(prefix + QStringLiteral("tabs"), paths);
        s.setValue(prefix + QStringLiteral("views"), modes);
        s.setValue(prefix + QStringLiteral("current"), d->currentIndex());
        s.setValue(prefix + QStringLiteral("destLocked"), d->destLocked());
        if (d->currentTab())
            s.setValue(prefix + QStringLiteral("columns"), d->currentTab()->headerState());
    }
}

bool ListerWindow::restoreListerState(QSettings &s)
{
    const bool hidden = s.value(QStringLiteral("hidden"), false).toBool();
    for (int i = 0; i < 2; ++i) {
        const QString prefix = QStringLiteral("display%1/").arg(i);
        const QStringList paths = s.value(prefix + QStringLiteral("tabs")).toStringList();
        const QStringList modes = s.value(prefix + QStringLiteral("views")).toStringList();
        FileDisplay *d = m_displays[i];
        d->setShowHidden(hidden);
        const QByteArray columns = s.value(prefix + QStringLiteral("columns")).toByteArray();
        for (int t = 0; t < paths.size(); ++t) {
            // A folder that is gone (card removed) falls back to its nearest parent.
            QString p = paths.at(t);
            while (!p.isEmpty() && p != QLatin1String("/") && !QFileInfo(p).isDir())
                p = QFileInfo(p).absolutePath();
            if (p.isEmpty())
                p = QDir::homePath();
            FolderTab *tab = d->addTab(p, false);
            FolderTab::ViewMode mode;
            if (FolderTab::viewModeFromName(modes.value(t), mode))
                tab->setViewMode(mode);
            tab->restoreHeaderState(columns);
        }
        if (d->tabCount() == 0)
            d->addTab(QDir::homePath());
        d->setCurrentIndex(s.value(prefix + QStringLiteral("current"), 0).toInt());
    }
    m_source = qBound(0, s.value(QStringLiteral("source"), 0).toInt(), 1);
    setSideBySide(s.value(QStringLiteral("sideBySide"), true).toBool());
    setDual(s.value(QStringLiteral("dual"), true).toBool());
    for (int i = 0; i < 2; ++i) {
        if (m_dual && i != m_source)
            m_displays[i]->setDestLocked(s.value(QStringLiteral("display%1/destLocked").arg(i), false).toBool());
    }
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    QMainWindow::restoreState(s.value(QStringLiteral("windowState")).toByteArray());
    m_split->restoreState(s.value(QStringLiteral("splitter")).toByteArray());
    updateRoles();
    if (auto *t = sourceTab())
        t->focusView();
    return true;
}

void ListerWindow::closeEvent(QCloseEvent *event)
{
    if (m_app.isQuitting()) {
        m_app.listerClosing(this);
        event->accept();
        return;
    }
    const bool last = m_app.listerCount() <= 1;
    if (last && m_app.jobs().hasActiveJobs()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Jobs still running"),
            QStringLiteral("File operations are still running. Quitting cancels them; partly copied files are cleaned up. Quit anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        for (int id : m_app.jobs().jobIds())
            m_app.jobs().cancel(id);
        m_app.jobs().waitForIdle(5000);
    }
    if (last)
        m_app.saveSession({this});
    m_app.listerClosing(this);
    event->accept();
}

void ListerWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        m_app.listerActivated(this);
    QMainWindow::changeEvent(event);
}
