#include "ui/ListerWindow.h"

#include "core/AliasManager.h"
#include "core/FileOps.h"
#include "core/JobManager.h"
#include "core/Location.h"
#include "core/Paths.h"
#include "core/RemoteBackend.h"
#include "ui/AdvancedRenameDialog.h"
#include "ui/AppContext.h"
#include "ui/DuplicatesDialog.h"
#include "ui/SyncDialog.h"
#include "ui/BreadcrumbBar.h"
#include "ui/ButtonBar.h"
#include "ui/CommandConsole.h"
#include "ui/CustomizeDialog.h"
#include "core/Condition.h"
#include "ui/DrivesBar.h"
#include "ui/ToolbarStrip.h"
#include "ui/FileDisplay.h"
#include "ui/FolderFormats.h"
#include "ui/FolderTab.h"
#include "ui/FolderTree.h"
#include "ui/Style.h"
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
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
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
    , m_outer(new QSplitter(Qt::Horizontal, this))
    , m_split(new QSplitter(m_outer))
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
            if (m_drives && from == sourceDisplay())
                m_drives->setActivePath(now);
            if (m_sharedTree && from == sourceDisplay())
                m_sharedTree->setPath(now);
        });
        connect(d, &FileDisplay::navigateRequested, this, &ListerWindow::onNavigateRequested);
        connect(d, &FileDisplay::renameRequested, this, &ListerWindow::onRenameRequested);
        connect(d, &FileDisplay::dropRequested, this, &ListerWindow::onDropRequested);
        connect(d, &FileDisplay::openFileRequested, this, &ListerWindow::openFile);
        connect(d, &FileDisplay::contextMenuRequested, this, &ListerWindow::onContextMenu);
        connect(d, &FileDisplay::stateChanged, this, &ListerWindow::scheduleButtonStates);
        connect(d, &FileDisplay::tabKeyPressed, this, [this](FileDisplay *from) {
            if (!m_dual)
                return;
            FileDisplay *other = from == m_displays[0] ? m_displays[1] : m_displays[0];
            if (auto *t = other->currentTab())
                t->focusView(); // focusing it makes it the source unless it is a locked dest
        });
    }
    // Toolbars can live between the two displays.
    m_strip = new ToolbarStrip(m_split);
    m_split->insertWidget(1, m_strip);
    m_split->setStretchFactor(0, 1);
    m_split->setStretchFactor(1, 0);
    m_split->setStretchFactor(2, 1);
    m_split->setChildrenCollapsible(false);
    m_outer->addWidget(m_split);
    m_outer->setChildrenCollapsible(false);
    setCentralWidget(m_outer);

    buildActions();
    buildMenus();
    buildToolbar();
    updateRoles();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ListerWindow::scheduleButtonStates);
    connect(Style::instance(), &Style::changed, this, &ListerWindow::scheduleButtonStates);
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

    m_actFilterBar = commandAction(QStringLiteral("Filter Bar"), QStringLiteral("Set FILTERBAR=toggle"),
                                   QKeySequence(QStringLiteral("Ctrl+F")), QStringLiteral("view-filter"));
    m_actChecks = commandAction(QStringLiteral("Check Boxes"), QStringLiteral("Set CHECKBOXES=toggle"));
    m_actFolderSizes = commandAction(QStringLiteral("Calculate All Folder Sizes"), QStringLiteral("Set FOLDERSIZES=toggle"),
                                     QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    m_actAutoFormat = commandAction(QStringLiteral("Icons for Picture Folders"), QStringLiteral("Set AUTOFORMAT=toggle"));
    for (QAction *a : {m_actFilterBar, m_actChecks, m_actFolderSizes, m_actAutoFormat})
        a->setCheckable(true);
}

void ListerWindow::fillViewMenus()
{
    FolderTab *t = sourceTab();
    auto fill = [this](QMenu *menu, const QList<QPair<QString, QString>> &items, const QString &current) {
        menu->clear();
        auto *group = new QActionGroup(menu);
        for (const auto &it : items) {
            QAction *a = menu->addAction(it.first);
            a->setCheckable(true);
            a->setActionGroup(group);
            const QString value = it.second.section(QLatin1Char('='), 1);
            a->setChecked(value == current);
            const QString command = it.second;
            connect(a, &QAction::triggered, this, [this, command]() { runCommand(command); });
        }
    };
    const QString tree = m_treeMode == TreeMode::Off ? QStringLiteral("off")
        : m_treeMode == TreeMode::Shared               ? QStringLiteral("shared")
                                                       : QStringLiteral("separate");
    fill(m_treeMenu,
         {{QStringLiteral("No Folder Tree"), QStringLiteral("Set TREE=off")},
          {QStringLiteral("Shared Tree (follows the source)"), QStringLiteral("Set TREE=shared")},
          {QStringLiteral("Separate Trees (one per display)"), QStringLiteral("Set TREE=separate")}},
         tree);
    fill(m_flatMenu,
         {{QStringLiteral("Off"), QStringLiteral("Set FLATVIEW=off")},
          {QStringLiteral("Grouped by Folder"), QStringLiteral("Set FLATVIEW=grouped")},
          {QStringLiteral("Mixed"), QStringLiteral("Set FLATVIEW=mixed")},
          {QStringLiteral("Mixed, Files Only"), QStringLiteral("Set FLATVIEW=nofolders")}},
         t ? FolderTab::flatModeName(t->flatMode()) : QString());
    using G = FileListModel::GroupBy;
    QList<QPair<QString, QString>> groups;
    for (G g : {G::None, G::Type, G::Name, G::Size, G::Modified, G::Extension, G::Folder}) {
        QString label = FileListModel::groupByName(g);
        label[0] = label[0].toUpper();
        groups << qMakePair(g == G::None ? QStringLiteral("No Grouping") : label,
                            QStringLiteral("Set GROUPBY=%1").arg(FileListModel::groupByName(g)));
    }
    fill(m_groupMenu, groups, t ? FileListModel::groupByName(t->groupBy()) : QString());
    m_groupMenu->addSeparator();
    const bool grouped = t && t->groupBy() != FileListModel::GroupBy::None;
    QAction *collapse = m_groupMenu->addAction(QStringLiteral("Collapse All Groups"));
    collapse->setEnabled(grouped);
    connect(collapse, &QAction::triggered, this, [this]() { runCommand(QStringLiteral("Set GROUPCOLLAPSE=all")); });
    QAction *expand = m_groupMenu->addAction(QStringLiteral("Expand All Groups"));
    expand->setEnabled(grouped);
    connect(expand, &QAction::triggered, this, [this]() { runCommand(QStringLiteral("Set GROUPCOLLAPSE=none")); });
    QList<QPair<QString, QString>> themes;
    for (const QString &name : Style::instance()->themeNames())
        themes << qMakePair(name, QStringLiteral("Set THEME=\"%1\"").arg(name));
    fill(m_themeMenu, themes, QStringLiteral("\"%1\"").arg(Style::instance()->theme().name));
    fill(m_sizeMenu,
         {{QStringLiteral("Normal"), QStringLiteral("Set SIZE=normal")},
          {QStringLiteral("Large"), QStringLiteral("Set SIZE=large")},
          {QStringLiteral("Larger"), QStringLiteral("Set SIZE=larger")}},
         Style::sizeName(Style::instance()->sizeLevel()));
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
    QMenu *print = file->addMenu(QIcon::fromTheme(QStringLiteral("document-print")), QStringLiteral("Print Folder"));
    print->addAction(commandAction(QStringLiteral("Copy Listing to Clipboard"), QStringLiteral("Print FOLDER")));
    print->addAction(commandAction(QStringLiteral("Save as Text..."), QStringLiteral("Print FOLDER AS=text TO=ask")));
    print->addAction(commandAction(QStringLiteral("Save as CSV..."), QStringLiteral("Print FOLDER AS=csv TO=ask")));
    print->addAction(commandAction(QStringLiteral("Save as HTML..."), QStringLiteral("Print FOLDER AS=html TO=ask")));
    print->addSeparator();
    print->addAction(commandAction(QStringLiteral("Selected Items to Clipboard"), QStringLiteral("Print FOLDER SELECTED")));
    QMenu *settingsMenu = file->addMenu(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Settings"));
    settingsMenu->addAction(commandAction(QStringLiteral("Back Up Settings..."), QStringLiteral("Settings BACKUP=ask"),
                                          {}, QStringLiteral("document-save")));
    settingsMenu->addAction(commandAction(QStringLiteral("Restore Settings..."), QStringLiteral("Settings RESTORE=ask"),
                                          {}, QStringLiteral("document-open")));
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
    edit->addAction(commandAction(QStringLiteral("Paste as File..."), QStringLiteral("Clipboard PASTE AS"), {},
                                  QStringLiteral("edit-paste")));
    edit->addSeparator();
    edit->addAction(commandAction(QStringLiteral("Copy to Destination"), QStringLiteral("Copy"), {}, QStringLiteral("edit-copy")));
    edit->addAction(commandAction(QStringLiteral("Copy As..."), QStringLiteral("Copy AS")));
    edit->addAction(commandAction(QStringLiteral("Move to Destination"), QStringLiteral("Copy MOVE"), {}, QStringLiteral("transform-move")));
    edit->addAction(commandAction(QStringLiteral("Move As..."), QStringLiteral("Copy MOVE AS")));
    edit->addSeparator();
    edit->addAction(commandAction(QStringLiteral("Rename"), QStringLiteral("Rename"), QKeySequence(Qt::Key_F2), QStringLiteral("edit-rename")));
    edit->addAction(commandAction(QStringLiteral("Advanced Rename..."), QStringLiteral("Rename ADVANCED"),
                                  QKeySequence(QStringLiteral("Shift+F2")), QStringLiteral("edit-rename")));
    QAction *undoRename = commandAction(QStringLiteral("Undo Rename"), QStringLiteral("Rename UNDO"), {}, QStringLiteral("edit-undo"));
    edit->addAction(undoRename);
    connect(edit, &QMenu::aboutToShow, this, [this, undoRename]() { undoRename->setEnabled(m_app.engine().canUndoRename()); });
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
    m_treeMenu = view->addMenu(QIcon::fromTheme(QStringLiteral("view-list-tree")), QStringLiteral("Folder Tree"));
    m_flatMenu = view->addMenu(QStringLiteral("Flat View"));
    m_groupMenu = view->addMenu(QStringLiteral("Group By"));
    view->addAction(m_actFilterBar);
    view->addAction(m_actChecks);
    view->addAction(m_actFolderSizes);
    QMenu *format = view->addMenu(QStringLiteral("Folder Format"));
    format->addAction(commandAction(QStringLiteral("Save for This Folder"), QStringLiteral("Set FORMAT=save")));
    format->addAction(commandAction(QStringLiteral("Save for This Folder and Subfolders"), QStringLiteral("Set FORMAT=savesub")));
    format->addAction(commandAction(QStringLiteral("Clear This Folder's Format"), QStringLiteral("Set FORMAT=clear")));
    format->addSeparator();
    format->addAction(commandAction(QStringLiteral("Save as Default for All Folders"), QStringLiteral("Set FORMAT=savedefault")));
    format->addAction(commandAction(QStringLiteral("Clear the Default"), QStringLiteral("Set FORMAT=cleardefault")));
    format->addSeparator();
    format->addAction(m_actAutoFormat);
    m_themeMenu = view->addMenu(QIcon::fromTheme(QStringLiteral("color-management")), QStringLiteral("Theme"));
    m_sizeMenu = view->addMenu(QIcon::fromTheme(QStringLiteral("zoom-in")), QStringLiteral("Text and Icon Size"));
    connect(view, &QMenu::aboutToShow, this, [this]() {
        syncViewActions();
        fillViewMenus();
    });
    view->addSeparator();
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

    QMenu *tools = menuBar()->addMenu(QStringLiteral("&Tools"));
    tools->addAction(commandAction(QStringLiteral("Synchronize..."), QStringLiteral("Sync"), {}, QStringLiteral("folder-sync")));
    tools->addAction(commandAction(QStringLiteral("Find Duplicates..."), QStringLiteral("Duplicates"), {}, QStringLiteral("edit-copy")));
    tools->addSeparator();
    tools->addAction(commandAction(QStringLiteral("Advanced Rename..."), QStringLiteral("Rename ADVANCED"), {}, QStringLiteral("edit-rename")));

    QMenu *window = menuBar()->addMenu(QStringLiteral("&Window"));
    m_toolbarsMenu = window->addMenu(QIcon::fromTheme(QStringLiteral("configure-toolbars")), QStringLiteral("Toolbars"));
    connect(m_toolbarsMenu, &QMenu::aboutToShow, this, &ListerWindow::rebuildToolbarsMenu);
    QAction *custom = window->addAction(QIcon::fromTheme(QStringLiteral("configure-toolbars")), QStringLiteral("Customize Toolbars..."));
    connect(custom, &QAction::triggered, this, &ListerWindow::customize);
    QAction *console = window->addAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), QStringLiteral("Command Console"));
    console->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    connect(console, &QAction::triggered, this, &ListerWindow::showConsole);
    window->addSeparator();
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
    ToolbarStore &store = m_app.toolbars();
    for (const QString &id : store.ids())
        createButtonBar(id, true);
    m_buttons = m_buttonBars.value(QLatin1String(ToolbarStore::kMainId));
    connect(&store, &ToolbarStore::added, this, [this](const QString &id) {
        // Shown where it was made (customize mode is open there), hidden elsewhere.
        createButtonBar(id, isCustomizing());
    });
    connect(&store, &ToolbarStore::removed, this, &ListerWindow::removeButtonBar);

    m_drives = new DrivesBar(this);
    connect(m_drives, &DrivesBar::commandTriggered, this, [this](const QString &cmd) { runCommand(cmd); });
    setupToolbarMenu(m_drives);
    // Opus puts the drives between the displays; so does Linopus by default.
    placeToolbar(m_drives, ToolbarPlace::Between);
}

QList<QToolBar *> ListerWindow::toolbars() const
{
    QList<QToolBar *> out;
    for (const QString &id : m_app.toolbars().ids()) {
        if (ButtonBar *b = m_buttonBars.value(id))
            out << b;
    }
    out << m_drives;
    return out;
}

QList<ButtonBar *> ListerWindow::buttonBars() const
{
    QList<ButtonBar *> out;
    for (const QString &id : m_app.toolbars().ids()) {
        if (ButtonBar *b = m_buttonBars.value(id))
            out << b;
    }
    return out;
}

void ListerWindow::createButtonBar(const QString &id, bool show)
{
    if (m_buttonBars.contains(id))
        return;
    auto *bar = new ButtonBar(m_app.toolbars(), id, this, this);
    m_buttonBars.insert(id, bar);
    connect(bar, &ButtonBar::commandTriggered, this, [this](const QString &cmd) { runCommand(cmd); });
    connect(bar, &ButtonBar::filesDropped, this, &ListerWindow::runCommandWithFiles);
    bar->setEvaluator([this](const QString &cond, bool fallback) { return evaluateCondition(cond, fallback); });
    setupToolbarMenu(bar);
    const bool main = id == QLatin1String(ToolbarStore::kMainId);
    if (main) {
        addToolBar(Qt::TopToolBarArea, bar);
    } else {
        // Its own row under the others, so it never squeezes the main bar.
        placeToolbar(bar, ToolbarPlace::Top, true);
    }
    bar->setVisible(show);
    if (isCustomizing())
        bar->setEditMode(true);
    m_strip->updateGeometryForBars();
}

void ListerWindow::removeButtonBar(const QString &id)
{
    ButtonBar *bar = m_buttonBars.take(id);
    if (!bar)
        return;
    if (m_strip->contains(bar))
        m_strip->removeBar(bar);
    else
        removeToolBar(bar);
    bar->deleteLater();
    m_strip->updateGeometryForBars();
}

void ListerWindow::setButtonBarVisible(const QString &id, bool visible)
{
    if (ButtonBar *b = m_buttonBars.value(id)) {
        b->setVisible(visible);
        m_strip->updateGeometryForBars();
    }
}

bool ListerWindow::isButtonBarVisible(const QString &id) const
{
    ButtonBar *b = m_buttonBars.value(id);
    return b && !b->isHidden();
}

bool ListerWindow::isCustomizing() const
{
    return !m_customize.isNull();
}

CustomizeDialog *ListerWindow::customizeDialog() const
{
    return m_customize;
}

CommandConsole *ListerWindow::console() const
{
    return m_console;
}

void ListerWindow::customize()
{
    if (m_customize) {
        m_customize->raise();
        m_customize->activateWindow();
        return;
    }
    m_customize = new CustomizeDialog(this, m_app.toolbars());
    m_customize->setAttribute(Qt::WA_DeleteOnClose);
    for (ButtonBar *b : buttonBars())
        b->setEditMode(true);
    connect(m_customize, &QDialog::finished, this, [this]() {
        for (ButtonBar *b : buttonBars())
            b->setEditMode(false);
        updateButtonStates();
    });
    m_customize->show();
}

void ListerWindow::showConsole()
{
    if (!m_console) {
        m_console = new CommandConsole(this);
        m_console->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_console->show();
    m_console->raise();
    m_console->activateWindow();
}

std::optional<bool> ListerWindow::conditionAtom(const QString &name, const QString &value) const
{
    FolderTab *t = sourceTab();
    const QStringList sel = t ? t->selectedPaths() : QStringList();
    auto flag = [&value](bool b) -> std::optional<bool> {
        if (!value.isEmpty())
            return std::nullopt; // these take no value
        return b;
    };
    if (name == QLatin1String("selection"))
        return flag(!sel.isEmpty());
    if (name == QLatin1String("files") || name == QLatin1String("folders")) {
        const bool wantFolders = name == QLatin1String("folders");
        bool any = false;
        for (const QString &p : sel)
            any = any || (t->isFolder(p) == wantFolders);
        return flag(any);
    }
    if (name == QLatin1String("single"))
        return flag(sel.size() == 1);
    if (name == QLatin1String("multiple"))
        return flag(sel.size() > 1);
    if (name == QLatin1String("dual"))
        return flag(m_dual);
    if (name == QLatin1String("dest"))
        return flag(!destPath().isEmpty());
    if (name == QLatin1String("destlocked"))
        return flag(destDisplay() && destDisplay()->destLocked());
    if (name == QLatin1String("navlock"))
        return flag(m_navLock);
    if (name == QLatin1String("remote"))
        return flag(t && !Location::isLocal(t->path()));
    if (name == QLatin1String("canup"))
        return flag(t && !t->path().isEmpty() && !Location::isRoot(t->path()));
    if (name == QLatin1String("back"))
        return flag(t && t->canGoBack());
    if (name == QLatin1String("forward"))
        return flag(t && t->canGoForward());
    if (name == QLatin1String("clipboard")) {
        const QMimeData *m = QGuiApplication::clipboard()->mimeData();
        return flag(m && m->hasUrls());
    }
    if (name == QLatin1String("hidden"))
        return flag(sourceDisplay()->showHidden());
    if (name == QLatin1String("tree"))
        return flag(m_treeMode != TreeMode::Off);
    if (name == QLatin1String("flat"))
        return flag(t && t->flatMode() != DirLister::FlatMode::Off);
    if (name == QLatin1String("grouped"))
        return flag(t && t->groupBy() != FileListModel::GroupBy::None);
    if (name == QLatin1String("filter"))
        return flag(t && t->filterBarVisible());
    if (name == QLatin1String("checkboxes"))
        return flag(t && t->checkMode());
    if (name == QLatin1String("foldersizes"))
        return flag(t && t->liveFolderSizes());
    if (value.isEmpty())
        return std::nullopt; // the rest need a value
    if (name == QLatin1String("view"))
        return t && FolderTab::viewModeName(t->viewMode()) == value;
    if (name == QLatin1String("layout")) {
        const QString now = !m_dual ? QStringLiteral("single") : m_sideBySide ? QStringLiteral("vertical") : QStringLiteral("horizontal");
        return now == value;
    }
    if (name == QLatin1String("groupby"))
        return t && FileListModel::groupByName(t->groupBy()) == value;
    if (name == QLatin1String("treemode")) {
        const QString now = m_treeMode == TreeMode::Off ? QStringLiteral("off")
            : m_treeMode == TreeMode::Shared            ? QStringLiteral("shared")
                                                        : QStringLiteral("separate");
        return now == value;
    }
    if (name == QLatin1String("theme"))
        return Style::instance()->theme().name.toLower().remove(QLatin1Char(' ')) == value;
    return std::nullopt;
}

bool ListerWindow::evaluateCondition(const QString &condition, bool fallback, QString *error) const
{
    QString err;
    const bool v = Condition::evaluate(
        condition, [this](const QString &n, const QString &val) { return conditionAtom(n, val); }, &err);
    if (!err.isEmpty()) {
        if (error)
            *error = err;
        return fallback; // a broken condition never hides a button's use
    }
    return v;
}

void ListerWindow::scheduleButtonStates()
{
    if (m_statesPending)
        return;
    m_statesPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_statesPending = false;
        updateButtonStates();
    });
}

void ListerWindow::updateButtonStates()
{
    for (ButtonBar *b : buttonBars())
        b->updateStates();
}

QToolBar *ListerWindow::toolbarByName(const QString &name) const
{
    const QString n = name.toLower();
    for (QToolBar *bar : toolbars()) {
        if (bar->windowTitle().toLower() == n || bar->objectName() == QStringLiteral("toolbar-") + n)
            return bar;
    }
    return nullptr;
}

ListerWindow::ToolbarPlace ListerWindow::toolbarPlace(QToolBar *bar) const
{
    if (m_strip->contains(bar))
        return ToolbarPlace::Between;
    switch (toolBarArea(bar)) {
    case Qt::BottomToolBarArea:
        return ToolbarPlace::Bottom;
    case Qt::LeftToolBarArea:
        return ToolbarPlace::Left;
    case Qt::RightToolBarArea:
        return ToolbarPlace::Right;
    default:
        return ToolbarPlace::Top;
    }
}

void ListerWindow::placeToolbar(QToolBar *bar, ToolbarPlace place, bool newRow)
{
    if (m_strip->contains(bar)) {
        m_strip->removeBar(bar);
    } else if (toolBarArea(bar) != Qt::NoToolBarArea) {
        removeToolBarBreak(bar);
        removeToolBar(bar);
    }

    if (place == ToolbarPlace::Between) {
        m_strip->addBar(bar);
        bar->setVisible(true);
        m_strip->updateGeometryForBars();
        return;
    }

    Qt::ToolBarArea area = Qt::TopToolBarArea;
    if (place == ToolbarPlace::Bottom)
        area = Qt::BottomToolBarArea;
    else if (place == ToolbarPlace::Left)
        area = Qt::LeftToolBarArea;
    else if (place == ToolbarPlace::Right)
        area = Qt::RightToolBarArea;

    bool areaHasBars = false;
    for (QToolBar *other : toolbars()) {
        if (other != bar && !m_strip->contains(other) && toolBarArea(other) == area && !other->isHidden())
            areaHasBars = true;
    }
    if (newRow && areaHasBars)
        addToolBarBreak(area);
    bar->setParent(this);
    bar->setMovable(true);
    addToolBar(area, bar);
    bar->show();
    m_strip->updateGeometryForBars();
}

void ListerWindow::setupToolbarMenu(QToolBar *bar)
{
    // Right-click on a toolbar's handle or empty space; buttons keep their own
    // right-click function.
    bar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bar, &QWidget::customContextMenuRequested, this, [this, bar](const QPoint &pos) {
        if (auto *bb = qobject_cast<ButtonBar *>(bar); bb && bb->editMode()) {
            bb->showEditMenu(-1, bar->mapToGlobal(pos));
            return;
        }
        QMenu menu(this);
        fillToolbarMenu(&menu, bar);
        menu.exec(bar->mapToGlobal(pos));
    });
}

void ListerWindow::fillToolbarMenu(QMenu *menu, QToolBar *bar)
{
    const ToolbarPlace current = toolbarPlace(bar);
    auto *group = new QActionGroup(menu);
    auto add = [&](const QString &text, ToolbarPlace p) {
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(!bar->isHidden() && current == p);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, bar, p]() { placeToolbar(bar, p); });
    };
    QAction *title = menu->addAction(bar->windowTitle());
    title->setEnabled(false);
    add(QStringLiteral("Between the Displays"), ToolbarPlace::Between);
    add(QStringLiteral("Top"), ToolbarPlace::Top);
    add(QStringLiteral("Bottom"), ToolbarPlace::Bottom);
    add(QStringLiteral("Left"), ToolbarPlace::Left);
    add(QStringLiteral("Right"), ToolbarPlace::Right);
    menu->addSeparator();
    if (current == ToolbarPlace::Between) {
        const int idx = m_strip->bars().indexOf(bar);
        const bool sideBySide = m_split->orientation() == Qt::Horizontal;
        QAction *earlier = menu->addAction(sideBySide ? QStringLiteral("Move Left") : QStringLiteral("Move Up"));
        earlier->setEnabled(idx > 0);
        connect(earlier, &QAction::triggered, this, [this, bar]() { m_strip->moveBar(bar, -1); });
        QAction *later = menu->addAction(sideBySide ? QStringLiteral("Move Right") : QStringLiteral("Move Down"));
        later->setEnabled(idx >= 0 && idx < m_strip->bars().size() - 1);
        connect(later, &QAction::triggered, this, [this, bar]() { m_strip->moveBar(bar, 1); });
    } else {
        QAction *row = menu->addAction(QStringLiteral("Start a New Row"));
        row->setToolTip(QStringLiteral("Stack this toolbar on its own row instead of sharing one"));
        connect(row, &QAction::triggered, this, [this, bar, current]() { placeToolbar(bar, current, true); });
    }
    menu->addSeparator();
    if (qobject_cast<ButtonBar *>(bar)) {
        QAction *custom = menu->addAction(QIcon::fromTheme(QStringLiteral("configure-toolbars")), QStringLiteral("Customize..."));
        connect(custom, &QAction::triggered, this, &ListerWindow::customize);
    }
    QAction *hide = menu->addAction(QStringLiteral("Hide %1").arg(bar->windowTitle()));
    connect(hide, &QAction::triggered, this, [this, bar]() {
        bar->hide();
        m_strip->updateGeometryForBars();
    });
}

void ListerWindow::rebuildToolbarsMenu()
{
    m_toolbarsMenu->clear();
    for (QToolBar *bar : toolbars()) {
        QMenu *sub = m_toolbarsMenu->addMenu(bar->windowTitle());
        QAction *shown = sub->addAction(QStringLiteral("Show"));
        shown->setCheckable(true);
        shown->setChecked(!bar->isHidden());
        connect(shown, &QAction::toggled, this, [this, bar](bool on) {
            bar->setVisible(on);
            m_strip->updateGeometryForBars();
        });
        sub->addSeparator();
        fillToolbarMenu(sub, bar);
    }
    m_toolbarsMenu->addSeparator();
    QAction *custom = m_toolbarsMenu->addAction(QIcon::fromTheme(QStringLiteral("configure-toolbars")), QStringLiteral("Customize..."));
    connect(custom, &QAction::triggered, this, &ListerWindow::customize);
}

bool ListerWindow::toolbar(const ParsedCommand &cmd, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (cmd.hasFlag(QStringLiteral("CUSTOMIZE"))) {
        customize();
        return true;
    }
    QString name = cmd.value(QStringLiteral("NAME"));
    if (name.isEmpty() && !cmd.positional.isEmpty())
        name = cmd.positional.first();
    QToolBar *bar = toolbarByName(name);
    if (!bar)
    {
        QStringList names;
        for (QToolBar *b : toolbars())
            names << b->objectName().mid(8);
        return fail(QStringLiteral("There is no toolbar called \"%1\". Toolbars: %2.").arg(name, names.join(QStringLiteral(", "))));
    }

    if (cmd.hasArg(QStringLiteral("PLACE"))) {
        const QString p = cmd.value(QStringLiteral("PLACE")).toLower();
        ToolbarPlace place;
        if (p == QLatin1String("top"))
            place = ToolbarPlace::Top;
        else if (p == QLatin1String("bottom"))
            place = ToolbarPlace::Bottom;
        else if (p == QLatin1String("left"))
            place = ToolbarPlace::Left;
        else if (p == QLatin1String("right"))
            place = ToolbarPlace::Right;
        else if (p == QLatin1String("between") || p == QLatin1String("middle") || p == QLatin1String("center"))
            place = ToolbarPlace::Between;
        else
            return fail(QStringLiteral("PLACE takes top, bottom, left, right or between."));
        placeToolbar(bar, place, cmd.hasFlag(QStringLiteral("NEWROW")));
    }
    if (cmd.hasArg(QStringLiteral("STATE"))) {
        bool on = !bar->isHidden();
        if (!parseSwitch(cmd.value(QStringLiteral("STATE")), on, on))
            return fail(QStringLiteral("STATE takes on, off or toggle."));
        bar->setVisible(on);
        m_strip->updateGeometryForBars();
    }
    return true;
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
    m_strip->setSplitOrientation(m_split->orientation());
    const int total = sideBySide ? m_split->width() : m_split->height();
    const int strip = m_strip->isVisible() ? (sideBySide ? m_strip->width() : m_strip->height()) : 0;
    if (total > 0) {
        const int rest = total - strip;
        m_split->setSizes({rest / 2, strip, rest - rest / 2});
    }
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
    if (m_drives && sourceTab())
        m_drives->setActivePath(sourceTab()->path());
    if (m_sharedTree && sourceTab())
        m_sharedTree->setPath(sourceTab()->path());
}

void ListerWindow::setTreeMode(TreeMode mode)
{
    m_treeMode = mode;
    if (mode != TreeMode::Off)
        m_lastTreeMode = mode;
    if (mode == TreeMode::Shared && !m_sharedTree) {
        m_sharedTree = new FolderTree(&m_app.aliases(), m_outer);
        m_sharedTree->setShowHidden(sourceDisplay()->showHidden());
        m_outer->insertWidget(0, m_sharedTree);
        m_outer->setStretchFactor(0, 0);
        m_outer->setStretchFactor(1, 1);
        const int w = qMax(180, width() / 6);
        m_outer->setSizes({w, qMax(400, width() - w)});
        connect(m_sharedTree, &FolderTree::folderActivated, this, [this](const QString &path) {
            if (FolderTab *t = sourceTab())
                t->navigate(path);
        });
    }
    if (m_sharedTree) {
        m_sharedTree->setVisible(mode == TreeMode::Shared);
        if (sourceTab())
            m_sharedTree->setPath(sourceTab()->path());
    }
    for (FileDisplay *d : m_displays)
        d->setTreeVisible(mode == TreeMode::Separate);
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
    if (FolderTab *t = sourceTab()) {
        m_actFilterBar->setChecked(t->filterBarVisible());
        m_actChecks->setChecked(t->checkMode());
        m_actFolderSizes->setChecked(t->liveFolderSizes());
    }
    m_actAutoFormat->setChecked(FolderFormats::instance().autoSwitch());
    scheduleButtonStates();
}

void ListerWindow::mirrorNavigation(FileDisplay *from, const QString &oldPath, const QString &newPath)
{
    if (m_mirroring || !m_dual || oldPath == newPath)
        return;
    if (Location::isRemote(oldPath) || Location::isRemote(newPath))
        return; // mirrors local trees only for now
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
    if (Location::isLocal(target) && !QFileInfo(target).isDir())
        return;
    m_mirroring = true;
    t->navigate(target);
    m_mirroring = false;
}

// ---------------------------------------------------------------- commands

bool ListerWindow::runCommand(const QString &script)
{
    const bool ok = m_app.engine().run(script, *this);
    scheduleButtonStates();
    return ok;
}

bool ListerWindow::runCommandWithFiles(const QString &script, const QStringList &paths)
{
    m_selectionOverride = paths;
    m_hasSelectionOverride = true;
    const bool ok = runCommand(script);
    m_hasSelectionOverride = false;
    m_selectionOverride.clear();
    return ok;
}

bool ListerWindow::runCommandReporting(const QString &script, QString *error)
{
    QString captured;
    QString *previous = m_errorSink;
    m_errorSink = &captured;
    const bool ok = runCommand(script);
    m_errorSink = previous;
    if (error)
        *error = captured;
    return ok;
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
    if (m_hasSelectionOverride)
        return m_selectionOverride;
    FolderTab *t = sourceTab();
    return t ? t->selectedPaths() : QStringList();
}

QString ListerWindow::focusedPath() const
{
    if (m_hasSelectionOverride)
        return m_selectionOverride.value(0);
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
    if (m_errorSink) {
        *m_errorSink = text;
        return;
    }
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
            if (m_sharedTree)
                m_sharedTree->setShowHidden(on);
            syncViewActions();
        } else if (key == QLatin1String("THEME")) {
            const QString raw = it.value().value(0);
            const QStringList names = Style::instance()->themeNames();
            QString name;
            if (value == QLatin1String("next") || value == QLatin1String("cycle")) {
                const int at = names.indexOf(Style::instance()->theme().name);
                name = names.value((at + 1) % qMax(1, int(names.size())));
            } else {
                for (const QString &n : names) {
                    if (n.compare(raw, Qt::CaseInsensitive) == 0)
                        name = n;
                }
            }
            if (name.isEmpty() || !Style::instance()->setTheme(name))
                return fail(QStringLiteral("There is no theme called %1. Themes: %2.").arg(raw, names.join(QStringLiteral(", "))));
            m_app.settings().setValue(QStringLiteral("Appearance/theme"), name);
        } else if (key == QLatin1String("SIZE")) {
            int level = Style::sizeFromName(value);
            if (value == QLatin1String("bigger") || value == QLatin1String("next"))
                level = qMin(2, Style::instance()->sizeLevel() + 1);
            else if (value == QLatin1String("smaller"))
                level = qMax(0, Style::instance()->sizeLevel() - 1);
            if (level < 0)
                return fail(QStringLiteral("SIZE takes normal, large, larger, bigger or smaller."));
            Style::instance()->setSizeLevel(level);
            m_app.settings().setValue(QStringLiteral("Appearance/size"), Style::sizeName(level));
        } else if (key == QLatin1String("GROUPBY")) {
            FileListModel::GroupBy g;
            if (!FileListModel::groupByFromName(value, g))
                return fail(QStringLiteral("GROUPBY takes none, type, name, size, modified, extension or folder."));
            sourceTab()->setGroupBy(g);
        } else if (key == QLatin1String("GROUPCOLLAPSE")) {
            FolderTab *t = sourceTab();
            if (t->groupBy() == FileListModel::GroupBy::None)
                return fail(QStringLiteral("Turn grouping on first (Set GROUPBY=type, for example)."));
            if (value == QLatin1String("all") || value == QLatin1String("on"))
                t->collapseAllGroups();
            else if (value == QLatin1String("none") || value == QLatin1String("off"))
                t->expandAllGroups();
            else if (value == QLatin1String("toggle") || value.isEmpty())
                t->toggleCurrentGroup();
            else
                return fail(QStringLiteral("GROUPCOLLAPSE takes all, none or toggle."));
        } else if (key == QLatin1String("FILTERBAR")) {
            bool on = sourceTab()->filterBarVisible();
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("FILTERBAR takes on, off or toggle."));
            sourceTab()->setFilterBarVisible(on);
        } else if (key == QLatin1String("FILTER")) {
            // A leading ! hides what matches instead of showing it.
            QString text = it.value().value(0);
            const bool invert = text.startsWith(QLatin1Char('!'));
            if (invert)
                text.remove(0, 1);
            sourceTab()->setFilter(text, invert, FileSortProxy::FilterScope::Files);
        } else if (key == QLatin1String("CHECKBOXES")) {
            bool on = sourceTab()->checkMode();
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("CHECKBOXES takes on, off or toggle."));
            sourceTab()->setCheckMode(on);
        } else if (key == QLatin1String("FOLDERSIZES")) {
            bool on = sourceTab()->liveFolderSizes();
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("FOLDERSIZES takes on, off or toggle."));
            sourceTab()->setLiveFolderSizes(on);
        } else if (key == QLatin1String("FLATVIEW")) {
            DirLister::FlatMode mode;
            if (value == QLatin1String("toggle") || value.isEmpty())
                mode = sourceTab()->flatMode() == DirLister::FlatMode::Off ? DirLister::FlatMode::Grouped
                                                                           : DirLister::FlatMode::Off;
            else if (!FolderTab::flatModeFromName(value, mode))
                return fail(QStringLiteral("FLATVIEW takes off, grouped, mixed, nofolders or toggle."));
            sourceTab()->setFlatMode(mode);
        } else if (key == QLatin1String("TREE") || key == QLatin1String("FOLDERTREE")) {
            TreeMode mode;
            if (value == QLatin1String("shared"))
                mode = TreeMode::Shared;
            else if (value == QLatin1String("separate"))
                mode = TreeMode::Separate;
            else {
                bool on = m_treeMode != TreeMode::Off;
                if (!parseSwitch(value, on, on))
                    return fail(QStringLiteral("TREE takes off, shared, separate, on or toggle."));
                mode = on ? m_lastTreeMode : TreeMode::Off;
            }
            setTreeMode(mode);
        } else if (key == QLatin1String("FORMAT")) {
            FolderTab *t = sourceTab();
            FolderFormats &formats = FolderFormats::instance();
            FolderFormat f = t->currentFormat();
            if (value == QLatin1String("save") || value == QLatin1String("savesub")) {
                if (t->flatMode() != DirLister::FlatMode::Off)
                    return fail(QStringLiteral("Turn flat view off before saving a folder format."));
                f.subfolders = value == QLatin1String("savesub");
                formats.set(f);
                t->formatSaved();
            } else if (value == QLatin1String("clear")) {
                if (!formats.remove(t->path()))
                    return fail(QStringLiteral("This folder has no format of its own."));
            } else if (value == QLatin1String("savedefault")) {
                formats.setDefault(f);
            } else if (value == QLatin1String("cleardefault")) {
                formats.clearDefault();
            } else {
                return fail(QStringLiteral("FORMAT takes save, savesub, clear, savedefault or cleardefault."));
            }
        } else if (key == QLatin1String("AUTOFORMAT")) {
            bool on = FolderFormats::instance().autoSwitch();
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("AUTOFORMAT takes on, off or toggle."));
            FolderFormats::instance().setAutoSwitch(on);
        } else if (key == QLatin1String("COLUMNSTOGGLE")) {
            int column = -1;
            for (int c = 0; c < FileListModel::ColumnCount; ++c) {
                if (FileListModel::columnName(c).remove(QLatin1Char(' ')).toLower() == QString(value).remove(QLatin1Char(' ')))
                    column = c;
            }
            if (column <= FileListModel::NameColumn)
                return fail(QStringLiteral("COLUMNSTOGGLE takes a column name such as created, extension or owner."));
            FolderTab *t = sourceTab();
            t->setColumnVisible(column, !t->isColumnVisible(column));
        } else {
            return fail(QStringLiteral("Set does not know %1.").arg(key));
        }
    }
    syncViewActions();
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
        if (u.isValid() && !u.scheme().isEmpty())
            sources << Location::fromUrl(u);
    }
    if (sources.isEmpty())
        return;

    const Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    bool move;
    if (mods & Qt::ControlModifier)
        move = false;
    else if (mods & Qt::ShiftModifier)
        move = true;
    else if (Location::isRemote(sources.first()) || Location::isRemote(targetDir))
        move = false; // to or from a server: copy, never move by surprise
    else
        move = FileOps::sameDevice(sources.first(), targetDir); // same drive moves, another drive copies

    // Dropping things back where they already are does nothing (Ctrl forces copies).
    bool allHere = true;
    for (const QString &s : std::as_const(sources))
        allHere = allHere && Location::parent(s) == Location::clean(targetDir);
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
    const bool oneFolder = sel.size() == 1 && t && t->isFolder(sel.first());
    QString archiveInside;
    if (sel.size() == 1 && !oneFolder && Location::isLocal(sel.first()) && RemoteBackend::instance())
        archiveInside = RemoteBackend::instance()->archiveLocation(sel.first());

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
        } else if (!archiveInside.isEmpty()) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("folder-open")), QStringLiteral("Open as Folder"), this,
                           [t, archiveInside]() { t->navigate(archiveInside); });
            menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), QStringLiteral("Open as Folder in New Tab"), this,
                           [d, archiveInside]() { d->addTab(archiveInside, false); });
            const QString p = sel.first();
            menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open in Application"), this,
                           [this, p]() { openFile(p); });
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
    if (Location::isRemote(path)) {
        if (RemoteBackend *backend = RemoteBackend::instance())
            backend->openFile(path);
        else
            showError(QStringLiteral("Open"), QStringLiteral("Remote files need the KIO build of Linopus."));
        return;
    }
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
        a->setEnabled(Location::isRemote(target) || QFileInfo(target).isDir());
        connect(a, &QAction::triggered, this, [this, n]() { runCommand(QStringLiteral("Go /%1").arg(n)); });
    }
    m_aliasMenu->addSeparator();
    QAction *add = m_aliasMenu->addAction(QStringLiteral("Make Alias for This Folder..."));
    connect(add, &QAction::triggered, this, [this]() {
        QString name = Location::title(sourcePath()).toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
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
    s.setValue(QStringLiteral("splitter3"), m_split->saveState()); // display, strip, display
    QStringList between;
    for (QToolBar *bar : m_strip->bars())
        between << bar->objectName();
    QStringList hiddenBars;
    for (QToolBar *bar : toolbars()) {
        if (bar->isHidden())
            hiddenBars << bar->objectName();
    }
    s.setValue(QStringLiteral("toolbarsBetween"), between);
    s.setValue(QStringLiteral("toolbarsHidden"), hiddenBars);
    s.setValue(QStringLiteral("source"), m_source);
    s.setValue(QStringLiteral("tree"), int(m_treeMode));
    s.setValue(QStringLiteral("lastTree"), int(m_lastTreeMode));
    s.setValue(QStringLiteral("outer"), m_outer->saveState());
    s.setValue(QStringLiteral("hidden"), m_displays[0]->showHidden());
    for (int i = 0; i < 2; ++i) {
        const FileDisplay *d = m_displays[i];
        QStringList paths;
        QStringList modes;
        QStringList groups;
        QStringList flats;
        QStringList extras;
        for (int t = 0; t < d->tabCount(); ++t) {
            const FolderTab *tab = d->tab(t);
            paths << tab->path();
            modes << FolderTab::viewModeName(tab->viewMode());
            groups << FileListModel::groupByName(tab->groupBy());
            flats << FolderTab::flatModeName(tab->flatMode());
            QStringList x;
            if (tab->checkMode())
                x << QStringLiteral("checks");
            if (tab->liveFolderSizes())
                x << QStringLiteral("sizes");
            extras << x.join(QLatin1Char('+'));
        }
        s.setValue(QStringLiteral("display%1/groups").arg(i), groups);
        s.setValue(QStringLiteral("display%1/flat").arg(i), flats);
        s.setValue(QStringLiteral("display%1/extras").arg(i), extras);
        s.setValue(QStringLiteral("display%1/body").arg(i), d->bodyState());
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
        const QStringList groups = s.value(prefix + QStringLiteral("groups")).toStringList();
        const QStringList flats = s.value(prefix + QStringLiteral("flat")).toStringList();
        const QStringList extras = s.value(prefix + QStringLiteral("extras")).toStringList();
        FileDisplay *d = m_displays[i];
        d->setShowHidden(hidden);
        const QByteArray columns = s.value(prefix + QStringLiteral("columns")).toByteArray();
        for (int t = 0; t < paths.size(); ++t) {
            // A folder that is gone (card removed) falls back to its nearest parent.
            QString p = paths.at(t);
            while (Location::isLocal(p) && !p.isEmpty() && p != QLatin1String("/") && !QFileInfo(p).isDir())
                p = QFileInfo(p).absolutePath();
            if (p.isEmpty())
                p = QDir::homePath();
            FolderTab *tab = d->addTab(p, false);
            FolderTab::ViewMode mode;
            if (FolderTab::viewModeFromName(modes.value(t), mode))
                tab->setViewMode(mode);
            tab->restoreHeaderState(columns);
            FileListModel::GroupBy g;
            if (FileListModel::groupByFromName(groups.value(t), g))
                tab->setGroupBy(g);
            DirLister::FlatMode flat;
            if (FolderTab::flatModeFromName(flats.value(t), flat))
                tab->setFlatMode(flat);
            const QStringList x = extras.value(t).split(QLatin1Char('+'), Qt::SkipEmptyParts);
            tab->setCheckMode(x.contains(QLatin1String("checks")));
            tab->setLiveFolderSizes(x.contains(QLatin1String("sizes")));
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
    // Edge toolbars come back through QMainWindow; it looks for every toolbar it
    // saved, so take the between-displays ones out of the strip first.
    for (QToolBar *bar : m_strip->bars())
        placeToolbar(bar, ToolbarPlace::Top);
    QMainWindow::restoreState(s.value(QStringLiteral("windowState")).toByteArray());
    const QStringList between = s.contains(QStringLiteral("toolbarsBetween"))
        ? s.value(QStringLiteral("toolbarsBetween")).toStringList()
        : QStringList{m_drives->objectName()}; // sessions from before the strip existed
    for (const QString &name : between) {
        for (QToolBar *bar : toolbars()) {
            if (bar->objectName() == name)
                placeToolbar(bar, ToolbarPlace::Between);
        }
    }
    const QStringList hiddenBars = s.value(QStringLiteral("toolbarsHidden")).toStringList();
    for (QToolBar *bar : toolbars())
        bar->setVisible(!hiddenBars.contains(bar->objectName()));
    m_strip->updateGeometryForBars();
    m_split->restoreState(s.value(QStringLiteral("splitter3")).toByteArray());
    m_lastTreeMode = TreeMode(qBound(1, s.value(QStringLiteral("lastTree"), 1).toInt(), 2));
    setTreeMode(TreeMode(qBound(0, s.value(QStringLiteral("tree"), 0).toInt(), 2)));
    if (m_sharedTree)
        m_outer->restoreState(s.value(QStringLiteral("outer")).toByteArray());
    for (int i = 0; i < 2; ++i)
        m_displays[i]->restoreBodyState(s.value(QStringLiteral("display%1/body").arg(i)).toByteArray());
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

bool ListerWindow::printFolder(const ParsedCommand &cmd, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    FolderTab *t = sourceTab();
    if (!t)
        return fail(QStringLiteral("There is no folder to print."));
    const QString as = cmd.value(QStringLiteral("AS")).toLower();
    FolderTab::ExportFormat format = FolderTab::ExportFormat::Text;
    QString ext = QStringLiteral("txt");
    if (as == QLatin1String("csv")) {
        format = FolderTab::ExportFormat::Csv;
        ext = QStringLiteral("csv");
    } else if (as == QLatin1String("html") || as == QLatin1String("htm")) {
        format = FolderTab::ExportFormat::Html;
        ext = QStringLiteral("html");
    } else if (!as.isEmpty() && as != QLatin1String("text") && as != QLatin1String("txt")) {
        return fail(QStringLiteral("AS takes text, csv or html."));
    }
    const QString text = t->exportListing(format, cmd.hasFlag(QStringLiteral("SELECTED")));
    QString to = cmd.value(QStringLiteral("TO"));
    if (to.isEmpty() || to.compare(QLatin1String("clipboard"), Qt::CaseInsensitive) == 0) {
        auto *mime = new QMimeData;
        mime->setText(text);
        if (format == FolderTab::ExportFormat::Html)
            mime->setHtml(text);
        QGuiApplication::clipboard()->setMimeData(mime);
        return true;
    }
    if (to.compare(QLatin1String("ask"), Qt::CaseInsensitive) == 0) {
        const QString suggested = QDir(QDir::homePath()).filePath(Location::title(t->path()) + QLatin1Char('.') + ext);
        to = QFileDialog::getSaveFileName(this, QStringLiteral("Save Folder Listing"), suggested);
        if (to.isEmpty())
            return fail(CommandEngine::kCancelled);
    } else {
        to = m_app.engine().resolvePath(to, t->path());
    }
    QSaveFile f(to);
    if (!f.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Could not write %1: %2").arg(to, f.errorString()));
    f.write(text.toUtf8());
    if (!f.commit())
        return fail(QStringLiteral("Could not write %1: %2").arg(to, f.errorString()));
    afterLocalChange({to});
    return true;
}

bool ListerWindow::settings(const ParsedCommand &cmd, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QString zipFilter = QStringLiteral("Zip files (*.zip)");
    if (cmd.hasArg(QStringLiteral("BACKUP"))) {
        QString file = cmd.value(QStringLiteral("BACKUP"));
        if (file.isEmpty() || file.compare(QLatin1String("ask"), Qt::CaseInsensitive) == 0) {
            const QString suggested = QDir(QDir::homePath())
                                          .filePath(QStringLiteral("Linopus settings %1.zip").arg(QDate::currentDate().toString(Qt::ISODate)));
            file = QFileDialog::getSaveFileName(this, QStringLiteral("Back Up Settings"), suggested, zipFilter);
            if (file.isEmpty())
                return fail(CommandEngine::kCancelled);
        } else {
            file = m_app.engine().resolvePath(file, sourcePath());
        }
        QString err;
        if (!m_app.backupSettings(file, &err))
            return fail(QStringLiteral("The backup failed: %1").arg(err));
        afterLocalChange({file});
        return true;
    }
    if (cmd.hasArg(QStringLiteral("RESTORE"))) {
        QString file = cmd.value(QStringLiteral("RESTORE"));
        if (file.isEmpty() || file.compare(QLatin1String("ask"), Qt::CaseInsensitive) == 0) {
            file = QFileDialog::getOpenFileName(this, QStringLiteral("Restore Settings"), QDir::homePath(), zipFilter);
            if (file.isEmpty())
                return fail(CommandEngine::kCancelled);
        } else {
            file = m_app.engine().resolvePath(file, sourcePath());
        }
        if (!cmd.hasFlag(QStringLiteral("QUIET"))
            && !confirm(QStringLiteral("Restore Settings"),
                        QStringLiteral("Replace your toolbars, aliases, themes, labels and folder formats with the ones in %1?")
                            .arg(QFileInfo(file).fileName())))
            return fail(CommandEngine::kCancelled);
        QString err;
        if (!m_app.restoreSettings(file, &err))
            return fail(QStringLiteral("The restore failed: %1").arg(err));
        return true;
    }
    return fail(QStringLiteral("Settings takes BACKUP= or RESTORE=."));
}

bool ListerWindow::advancedRename(const QStringList &paths, const RenameOptions &initial, QString *error)
{
    for (const QString &p : paths) {
        if (!Location::isLocal(p)) {
            if (error)
                *error = QStringLiteral("Advanced Rename works in local folders for now.");
            return false;
        }
    }
    AdvancedRenameDialog dlg(paths, initial, this);
    if (dlg.exec() != QDialog::Accepted) {
        if (error)
            *error = CommandEngine::kCancelled;
        return false;
    }
    if (!dlg.done().isEmpty())
        m_app.engine().recordRename(dlg.done());
    QStringList renamed;
    for (const auto &d : dlg.done())
        renamed << d.first;
    afterLocalChange(renamed);
    return true;
}

bool ListerWindow::tool(const ParsedCommand &cmd, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    CommandEngine &engine = m_app.engine();
    if (cmd.is(QStringLiteral("Sync"))) {
        SyncOptions o;
        if (cmd.hasArg(QStringLiteral("MODE")) && !SyncOptions::modeFromName(cmd.value(QStringLiteral("MODE")), o.mode))
            return fail(QStringLiteral("MODE takes update, mirror or twoway."));
        if (cmd.hasArg(QStringLiteral("COMPARE"))
            && !SyncOptions::compareFromName(cmd.value(QStringLiteral("COMPARE")), o.compareBy))
            return fail(QStringLiteral("COMPARE takes datesize, date, size or contents."));
        if (cmd.hasArg(QStringLiteral("EXCLUDE")))
            o.exclude = cmd.value(QStringLiteral("EXCLUDE")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        const QString left = cmd.hasArg(QStringLiteral("LEFT")) ? engine.resolvePath(cmd.value(QStringLiteral("LEFT")), sourcePath()) : sourcePath();
        const QString right = cmd.hasArg(QStringLiteral("RIGHT")) ? engine.resolvePath(cmd.value(QStringLiteral("RIGHT")), sourcePath()) : destPath();
        if (!Location::isLocal(left) || !Location::isLocal(right))
            return fail(QStringLiteral("Synchronize works with local folders (including SD cards and mounted drives) for now."));
        if (cmd.hasFlag(QStringLiteral("RUN"))) {
            if (!QFileInfo(left).isDir() || !QFileInfo(right).isDir())
                return fail(QStringLiteral("Both folders need to exist."));
            if (QFileInfo(left).canonicalFilePath() == QFileInfo(right).canonicalFilePath())
                return fail(QStringLiteral("Left and right are the same folder."));
            FolderSync::run(FolderSync::plan(FolderSync::compare(left, right, o), left, right), engine);
            return true;
        }
        auto *dlg = new SyncDialog(engine, left, right, o, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        if (!right.isEmpty())
            dlg->startCompare();
        return true;
    }
    // Duplicates
    DuplicateOptions o;
    const QString by = cmd.value(QStringLiteral("BY")).toLower();
    if (by == QLatin1String("name"))
        o.by = DuplicateOptions::By::Name;
    else if (by == QLatin1String("namesize") || by == QLatin1String("nameandsize"))
        o.by = DuplicateOptions::By::NameAndSize;
    else if (!by.isEmpty() && by != QLatin1String("contents"))
        return fail(QStringLiteral("BY takes contents, namesize or name."));
    if (cmd.hasArg(QStringLiteral("MINSIZE")))
        o.minSize = qMax<qint64>(1, cmd.value(QStringLiteral("MINSIZE")).toLongLong());
    o.includeHidden = cmd.hasFlag(QStringLiteral("HIDDEN"));
    QStringList folders;
    for (const QString &f : cmd.values(QStringLiteral("IN")))
        folders << engine.resolvePath(f, sourcePath());
    if (folders.isEmpty())
        folders << sourcePath();
    for (const QString &f : std::as_const(folders)) {
        if (!Location::isLocal(f))
            return fail(QStringLiteral("Finding duplicates works with local folders for now."));
    }
    auto *dlg = new DuplicatesDialog(engine, folders, o, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &DuplicatesDialog::showRequested, this, [this](const QString &path) {
        if (FolderTab *t = sourceTab()) {
            t->navigate(Location::parent(path));
            t->selectPaths({path});
        }
    });
    dlg->show();
    dlg->startSearch();
    return true;
}
