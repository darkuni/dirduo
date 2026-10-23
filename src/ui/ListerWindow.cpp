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
#include "ui/FindDialog.h"
#include "ui/SelectDialog.h"
#include "core/SelectRules.h"
#include "core/FindEngine.h"
#include "ui/HistoryDialog.h"
#include "ui/ArchiveDialog.h"
#include "ui/TagsDialog.h"
#include "ui/ChecksumDialog.h"
#include "core/Checksum.h"
#include "core/FileTags.h"
#include "ui/ImageViewer.h"
#include "ui/OpenWithDialog.h"
#include "core/Apps.h"
#include "ui/ServerDialog.h"
#include "core/Servers.h"
#include "ui/PropertiesDialog.h"
#include "ui/ImageConvertDialog.h"
#include "core/FileKind.h"
#include "core/Archive.h"
#include "core/ImageConvert.h"
#include "core/Collections.h"
#include "core/Favorites.h"
#include "ui/FavoritesDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/ViewerPane.h"
#include "ui/MetadataPane.h"
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
#include <QPushButton>
#include <QPainter>
#include <QVBoxLayout>
#include <QFontDatabase>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
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
#include <QCursor>
#include <QToolTip>
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
        d->setMenuProvider([this](QMenu *menu, const QString &kind) { fillDynamicMenu(menu, kind); });
        connect(d, &FileDisplay::copyPathRequested, this, [this](FileDisplay *from) {
            FileDisplay *other = from == m_displays[0] ? m_displays[1] : m_displays[0];
            if (m_dual && from->currentTab() && other->currentTab())
                other->currentTab()->navigate(from->currentTab()->path());
        });
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
        connect(d, &FileDisplay::stateChanged, this, [this](FileDisplay *from) {
            if (from == sourceDisplay())
                updateViewer();
        });
        connect(d, &FileDisplay::zoomRequested, this, [this](FileDisplay *from, int steps) { zoomBy(steps, true, from); });
        connect(d, &FileDisplay::commandRequested, this, [this](FileDisplay *from, const QString &c) {
            makeSource(from);
            runCommand(c);
        });
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

    // The viewer pane starts hidden on the right; F3 or Set VIEWER shows it.
    m_viewer = new ViewerPane(this);
    addDockWidget(Qt::RightDockWidgetArea, m_viewer);
    m_viewer->hide();
    connect(m_viewer, &QDockWidget::visibilityChanged, this, [this](bool on) {
        if (on)
            updateViewer();
        scheduleButtonStates();
    });
    // The Metadata pane, also on the right (Alt+Enter, Metadata PANE=toggle).
    m_metaPane = new MetadataPane(m_app.engine(), this);
    addDockWidget(Qt::RightDockWidgetArea, m_metaPane);
    m_metaPane->hide();
    connect(m_metaPane, &QDockWidget::visibilityChanged, this, [this](bool on) {
        if (on)
            updateViewer();
        scheduleButtonStates();
    });
    connect(m_metaPane, &MetadataPane::applied, this, [this](const QStringList &paths) {
        if (FolderTab *t = sourceTab())
            t->refresh();
        Q_UNUSED(paths)
    });

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
    m_actThumbs = commandAction(QStringLiteral("Thumbnails"), QStringLiteral("Set VIEW=thumbnails"), QKeySequence(QStringLiteral("Ctrl+4")));
    for (QAction *a : {m_actDetails, m_actList, m_actIcons, m_actThumbs}) {
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
          {QStringLiteral("Ungrouped"), QStringLiteral("Set FLATVIEW=mixed")},
          {QStringLiteral("Ungrouped, Files Only"), QStringLiteral("Set FLATVIEW=nofolders")}},
         t ? FolderTab::flatModeName(t->flatMode()) : QString());
    using G = FileListModel::GroupBy;
    QList<QPair<QString, QString>> groups;
    for (G g : FileListModel::allGroupings())
        groups << qMakePair(FileListModel::groupByLabel(g), QStringLiteral("Set GROUPBY=%1").arg(FileListModel::groupByName(g)));
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
    file->addAction(commandAction(QStringLiteral("Properties..."), QStringLiteral("Properties"), QKeySequence(QStringLiteral("Alt+Return")),
                                  QStringLiteral("document-properties")));
    file->addAction(commandAction(QStringLiteral("Make Link in Destination"), QStringLiteral("MakeLink DEST"), {},
                                  QStringLiteral("insert-link")));
    file->addSeparator();
    file->addAction(commandAction(QStringLiteral("Preferences..."), QStringLiteral("Settings"), QKeySequence(QStringLiteral("Ctrl+,")),
                                  QStringLiteral("configure")));
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
    {
        QMenu *labels = edit->addMenu(QIcon::fromTheme(QStringLiteral("tag")), QStringLiteral("Labels and Tags"));
        for (int i = 5; i >= 0; --i)
            labels->addAction(commandAction(i == 0 ? QStringLiteral("Not Rated") : QStringLiteral("Rate %1").arg(FileTags::stars(i)),
                                            QStringLiteral("Tag RATING=%1").arg(i), QKeySequence(QStringLiteral("Alt+%1").arg(i))));
        labels->addSeparator();
        labels->addAction(commandAction(QStringLiteral("Tags and Description..."), QStringLiteral("Tag"), QKeySequence(QStringLiteral("Ctrl+Shift+T")),
                                        QStringLiteral("tag-edit")));
    }
    {
        // Undo anything: copies, moves, new folders, the trash, renames.
        QAction *undo = commandAction(QStringLiteral("Undo"), QStringLiteral("Undo"), QKeySequence::Undo, QStringLiteral("edit-undo"));
        edit->addAction(undo);
        QMenu *recent = edit->addMenu(QIcon::fromTheme(QStringLiteral("edit-undo-history")), QStringLiteral("Undo List"));
        connect(edit, &QMenu::aboutToShow, this, [this, undo, recent]() {
            const UndoEntry *last = m_app.engine().history().latest();
            undo->setEnabled(last != nullptr);
            undo->setText(last ? QStringLiteral("Undo %1").arg(last->title) : QStringLiteral("Undo"));
            recent->clear();
            const QList<UndoEntry> entries = m_app.engine().history().entries();
            for (int i = 0; i < entries.size() && i < 10; ++i) {
                const int id = entries.at(i).id;
                recent->addAction(entries.at(i).title, this, [this, id]() { runCommand(QStringLiteral("Undo ID=%1").arg(id)); });
            }
            if (entries.isEmpty())
                recent->addAction(QStringLiteral("Nothing to undo yet"))->setEnabled(false);
            recent->addSeparator();
            recent->addAction(QStringLiteral("More..."), this, [this]() { runCommand(QStringLiteral("Undo LIST")); });
        });
        edit->addSeparator();
    }
    edit->addAction(commandAction(QStringLiteral("Cut"), QStringLiteral("Clipboard CUT"), QKeySequence::Cut, QStringLiteral("edit-cut")));
    edit->addAction(commandAction(QStringLiteral("Copy"), QStringLiteral("Clipboard COPY"), QKeySequence::Copy, QStringLiteral("edit-copy")));
    edit->addAction(commandAction(QStringLiteral("Paste"), QStringLiteral("Clipboard PASTE"), QKeySequence::Paste, QStringLiteral("edit-paste")));
    QMenu *addTo = edit->addMenu(QIcon::fromTheme(QStringLiteral("folder-favorites")), QStringLiteral("Add to Collection"));
    connect(addTo, &QMenu::aboutToShow, this, [this, addTo]() {
        addTo->clear();
        for (const QString &n : Collections::instance()->names()) {
            QString quoted = n;
            quoted.replace(QLatin1Char('"'), QString());
            addTo->addAction(commandAction(n, QStringLiteral("Collection ADD=\"%1\"").arg(quoted)));
        }
        if (!addTo->isEmpty())
            addTo->addSeparator();
        addTo->addAction(commandAction(QStringLiteral("New Collection..."), QStringLiteral("Collection ADD=ask")));
    });
    edit->addAction(commandAction(QStringLiteral("Remove from Collection"), QStringLiteral("Collection REMOVE")));
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
    edit->addAction(commandAction(QStringLiteral("New Folder..."), QStringLiteral("CreateFolder"),
                                  QKeySequence(QStringLiteral("Ctrl+Shift+N")), QStringLiteral("folder-new")));
    edit->addAction(commandAction(QStringLiteral("Move to Trash"), QStringLiteral("Delete"), QKeySequence::Delete, QStringLiteral("user-trash")));
    edit->addAction(commandAction(QStringLiteral("Delete Permanently"), QStringLiteral("Delete NORECYCLE"),
                                  QKeySequence(QStringLiteral("Shift+Del")), QStringLiteral("edit-delete")));
    edit->addSeparator();
    edit->addAction(commandAction(QStringLiteral("Select All"), QStringLiteral("Select ALL"), QKeySequence::SelectAll));
    edit->addAction(commandAction(QStringLiteral("Select None"), QStringLiteral("Select NONE"), QKeySequence(QStringLiteral("Ctrl+Shift+A"))));
    edit->addAction(commandAction(QStringLiteral("Invert Selection"), QStringLiteral("Select INVERT"), QKeySequence(QStringLiteral("Ctrl+I"))));
    {
        QMenu *tags = edit->addMenu(QIcon::fromTheme(QStringLiteral("document-properties")), QStringLiteral("Tags"));
        tags->addAction(commandAction(QStringLiteral("Copy Tags"), QStringLiteral("Metadata COPY")));
        tags->addAction(commandAction(QStringLiteral("Paste Tags (artist, album, genre, year)"), QStringLiteral("Metadata PASTE")));
        tags->addAction(commandAction(QStringLiteral("Paste All Tags"), QStringLiteral("Metadata PASTE ALL")));
        tags->addSeparator();
        tags->addAction(commandAction(QStringLiteral("Number Tracks in List Order"), QStringLiteral("Metadata TRACK=auto")));
        tags->addAction(commandAction(QStringLiteral("Photos Taken an Hour Later"), QStringLiteral("Metadata SHIFT=+1h")));
        tags->addAction(commandAction(QStringLiteral("Photos Taken an Hour Earlier"), QStringLiteral("Metadata SHIFT=-1h")));
    }
    edit->addAction(commandAction(QStringLiteral("Select..."), QStringLiteral("Select DIALOG"), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                                  QStringLiteral("edit-select")));
    {
        QMenu *cmp = edit->addMenu(QStringLiteral("Select by Comparing"));
        cmp->addAction(commandAction(QStringLiteral("Newer Here, or Not There"), QStringLiteral("Select COMPARE=newerormissing")));
        cmp->addAction(commandAction(QStringLiteral("Not in the Other Display"), QStringLiteral("Select COMPARE=missing")));
        cmp->addAction(commandAction(QStringLiteral("Newer Than the Other Display"), QStringLiteral("Select COMPARE=newer")));
        cmp->addAction(commandAction(QStringLiteral("Older Than the Other Display"), QStringLiteral("Select COMPARE=older")));
        cmp->addAction(commandAction(QStringLiteral("Same in Both (both displays)"), QStringLiteral("Select COMPARE=same BOTH")));
        cmp->addAction(commandAction(QStringLiteral("Different Size, Same Date (both displays)"), QStringLiteral("Select COMPARE=different BOTH")));
    }
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
    view->addAction(m_actThumbs);
    view->addSeparator();
    view->addAction(m_actHidden);
    m_treeMenu = view->addMenu(QIcon::fromTheme(QStringLiteral("view-list-tree")), QStringLiteral("Folder Tree"));
    m_flatMenu = view->addMenu(QStringLiteral("Flat View"));
    m_groupMenu = view->addMenu(QStringLiteral("Group By"));
    view->addAction(commandAction(QStringLiteral("Viewer Pane"), QStringLiteral("Set VIEWER=toggle"), QKeySequence(Qt::Key_F3),
                                  QStringLiteral("view-preview")));
    view->addAction(commandAction(QStringLiteral("Metadata Pane"), QStringLiteral("Metadata PANE=toggle"),
                                  QKeySequence(QStringLiteral("Shift+Alt+Return")), QStringLiteral("document-properties")));
    QMenu *viewerPos = view->addMenu(QStringLiteral("Viewer Position"));
    viewerPos->addAction(commandAction(QStringLiteral("Right"), QStringLiteral("Set VIEWERPOS=right")));
    viewerPos->addAction(commandAction(QStringLiteral("Bottom"), QStringLiteral("Set VIEWERPOS=bottom")));
    viewerPos->addAction(commandAction(QStringLiteral("Left"), QStringLiteral("Set VIEWERPOS=left")));
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
    {
        // Ctrl+= and Ctrl+- as in browsers; Shift+wheel over a list does the same.
        QAction *in = commandAction(QStringLiteral("Zoom In"), QStringLiteral("Set ZOOM=in"), QKeySequence(QStringLiteral("Ctrl+=")),
                                    QStringLiteral("zoom-in"));
        in->setShortcuts({QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++"))});
        view->addAction(in);
        view->addAction(commandAction(QStringLiteral("Zoom Out"), QStringLiteral("Set ZOOM=out"), QKeySequence(QStringLiteral("Ctrl+-")),
                                      QStringLiteral("zoom-out")));
        m_actZoomReset = commandAction(QStringLiteral("Reset Zoom (100%)"), QStringLiteral("Set ZOOM=reset"),
                                       QKeySequence(QStringLiteral("Ctrl+0")), QStringLiteral("zoom-original"));
        view->addAction(m_actZoomReset);
    }
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
    // The network: machines found on it, Windows and Samba shares (KIO's smb
    // worker), and the saved servers.
    QMenu *servers = go->addMenu(QIcon::fromTheme(QStringLiteral("network-workgroup")), QStringLiteral("Network and Servers"));
    connect(servers, &QMenu::aboutToShow, this, [this, servers]() {
        servers->clear();
        fillDynamicMenu(servers, QStringLiteral("servers"));
    });
    go->addAction(commandAction(QStringLiteral("Collections"), QStringLiteral("Collection OPEN"), {}, QStringLiteral("folder-favorites")));
    m_aliasMenu = go->addMenu(QIcon::fromTheme(QStringLiteral("bookmarks")), QStringLiteral("Aliases"));
    connect(m_aliasMenu, &QMenu::aboutToShow, this, &ListerWindow::rebuildAliasMenu);

    QMenu *favs = menuBar()->addMenu(QStringLiteral("F&avorites"));
    connect(favs, &QMenu::aboutToShow, this, [this, favs]() {
        favs->clear();
        fillDynamicMenu(favs, QStringLiteral("favorites"));
        favs->addSeparator();
        QMenu *frequent = favs->addMenu(QIcon::fromTheme(QStringLiteral("document-open-recent")), QStringLiteral("Frequent Folders"));
        fillDynamicMenu(frequent, QStringLiteral("frequent"));
        QMenu *colls = favs->addMenu(QIcon::fromTheme(QStringLiteral("folder-favorites")), QStringLiteral("Collections"));
        fillDynamicMenu(colls, QStringLiteral("collections"));
    });
    // Present from the start so its shortcut works before the menu is opened.
    auto *addFav = new QAction(QStringLiteral("Add to Favorites"), this);
    addFav->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(addFav, &QAction::triggered, this, [this]() { runCommand(QStringLiteral("Favorite ADD")); });
    addAction(addFav);

    QMenu *tools = menuBar()->addMenu(QStringLiteral("&Tools"));
    tools->addAction(commandAction(QStringLiteral("Find..."), QStringLiteral("Find"), QKeySequence(QStringLiteral("Ctrl+Shift+F")),
                                   QStringLiteral("edit-find")));
    tools->addAction(commandAction(QStringLiteral("Synchronize..."), QStringLiteral("Sync"), {}, QStringLiteral("folder-sync")));
    tools->addAction(commandAction(QStringLiteral("Find Duplicates..."), QStringLiteral("Duplicates"), {}, QStringLiteral("edit-copy")));
    tools->addSeparator();
    tools->addAction(commandAction(QStringLiteral("Open Konsole Here"), QStringLiteral("Terminal"), QKeySequence(QStringLiteral("Shift+F4")),
                                   QStringLiteral("utilities-terminal")));
    tools->addAction(commandAction(QStringLiteral("Add to Archive..."), QStringLiteral("Archive CREATE"), {}, QStringLiteral("archive-insert")));
    tools->addAction(commandAction(QStringLiteral("Extract Here"), QStringLiteral("Archive EXTRACT"), {}, QStringLiteral("archive-extract")));
    tools->addAction(commandAction(QStringLiteral("Convert Images..."), QStringLiteral("Image"), {}, QStringLiteral("image-x-generic")));
    tools->addAction(commandAction(QStringLiteral("Checksums..."), QStringLiteral("Checksum"), {}, QStringLiteral("document-encrypt")));
    tools->addAction(commandAction(QStringLiteral("Split File..."), QStringLiteral("Split"), {}, QStringLiteral("split")));
    tools->addAction(commandAction(QStringLiteral("Join Pieces"), QStringLiteral("Join"), {}, QStringLiteral("merge")));
    tools->addAction(commandAction(QStringLiteral("Clean Junk Files..."), QStringLiteral("Clean"), {}, QStringLiteral("edit-clear")));
    tools->addAction(commandAction(QStringLiteral("File Log..."), QStringLiteral("Undo LOG"), {}, QStringLiteral("view-history")));
    tools->addAction(commandAction(QStringLiteral("Advanced Rename..."), QStringLiteral("Rename ADVANCED"), {}, QStringLiteral("edit-rename")));

    QMenu *window = menuBar()->addMenu(QStringLiteral("&Window"));
    QMenu *layouts = window->addMenu(QIcon::fromTheme(QStringLiteral("view-split-left-right")), QStringLiteral("Layouts"));
    connect(layouts, &QMenu::aboutToShow, this, [this, layouts]() {
        layouts->clear();
        for (const QString &name : m_app.layoutNames()) {
            QString q = name;
            q.remove(QLatin1Char('"'));
            layouts->addAction(commandAction(name, QStringLiteral("Layout LOAD=\"%1\"").arg(q)));
        }
        if (!layouts->isEmpty())
            layouts->addSeparator();
        layouts->addAction(commandAction(QStringLiteral("Save This Layout..."), QStringLiteral("Layout SAVE=ask"), {}, QStringLiteral("document-save")));
        QMenu *del = layouts->addMenu(QStringLiteral("Delete"));
        del->setEnabled(!m_app.layoutNames().isEmpty());
        for (const QString &name : m_app.layoutNames()) {
            QString q = name;
            q.remove(QLatin1Char('"'));
            del->addAction(commandAction(name, QStringLiteral("Layout DELETE=\"%1\"").arg(q)));
        }
    });
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

    m_menuCorner = new QWidget(menuBar());
    m_menuCorner->setObjectName(QStringLiteral("menubar-toolbars"));
    auto *cornerLayout = new QHBoxLayout(m_menuCorner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(0);
    menuBar()->setCornerWidget(m_menuCorner, Qt::TopRightCorner);
    m_menuCorner->hide();
    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction *about = help->addAction(QStringLiteral("About Linopus"));
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QStringLiteral("About Linopus"),
                           QStringLiteral("<b>Linopus %1</b><p>Build %2</p><p>A source and destination file manager for Linux.</p>"
                                          "<p>Configuration: %3</p>")
                               .arg(QStringLiteral(LINOPUS_VERSION), QStringLiteral(LINOPUS_BUILD_ID).toHtmlEscaped(),
                                    Paths::configDir().toHtmlEscaped()));
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
    m_drives->setNetworkMenuProvider([this](QMenu *menu) { fillDynamicMenu(menu, QStringLiteral("servers")); });
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
    bar->setMenuProvider([this](QMenu *menu, const QString &kind) { fillDynamicMenu(menu, kind); });
    bar->setFieldProvider([this](const QString &kind) {
        FolderTab *t = sourceTab();
        return kind == QLatin1String("filter") && t ? t->filterText() : QString();
    });
    connect(bar, &ButtonBar::fieldEdited, this, &ListerWindow::onToolbarField);
    connect(bar, &ButtonBar::fillChanged, this, [this, bar]() {
        if (m_strip->contains(bar))
            m_strip->refreshLayout();
    });
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
    if (name == QLatin1String("images")) {
        bool any = false;
        for (const QString &p : sel)
            any = any || (Location::isLocal(p) && FileKind::categoryFor(p, t->isFolder(p)) == FileKind::Category::Image);
        return flag(any);
    }
    if (name == QLatin1String("archives")) {
        bool any = false;
        for (const QString &p : sel)
            any = any || (Location::isLocal(p) && !t->isFolder(p) && Archive::isArchive(p));
        return flag(any && Archive::available());
    }
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
    if (name == QLatin1String("viewer"))
        return flag(m_viewer && m_viewer->isVisible());
    if (name == QLatin1String("favorite"))
        return flag(t && Favorites::instance()->contains(t->path()));
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
    if (name == QLatin1String("flatgrouped"))
        return flag(t && t->flatMode() == DirLister::FlatMode::Grouped);
    if (name == QLatin1String("flatungrouped"))
        return flag(t && t->flatMode() == DirLister::FlatMode::Mixed);
    if (name == QLatin1String("flatfiles"))
        return flag(t && t->flatMode() == DirLister::FlatMode::MixedNoFolders);
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
    if (m_menuBars.contains(bar))
        return ToolbarPlace::MenuBar;
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

void ListerWindow::placeToolbar(QToolBar *bar, ToolbarPlace place, bool newRow, bool joinRow)
{
    if (m_strip->contains(bar)) {
        m_strip->removeBar(bar);
    } else if (m_menuBars.removeOne(bar)) {
        m_menuCorner->layout()->removeWidget(bar);
        m_menuCorner->setVisible(!m_menuBars.isEmpty());
    } else if (toolBarArea(bar) != Qt::NoToolBarArea) {
        removeToolBarBreak(bar);
        removeToolBar(bar);
    }

    if (place == ToolbarPlace::MenuBar) {
        // At the right end of the menu bar, as Opus lets its menu bar hold buttons.
        m_menuBars << bar;
        bar->setParent(m_menuCorner);
        bar->setMovable(false);
        bar->setOrientation(Qt::Horizontal);
        m_menuCorner->layout()->addWidget(bar);
        bar->setVisible(true);
        m_menuCorner->show();
        menuBar()->adjustSize();
        menuBar()->updateGeometry();
        m_strip->updateGeometryForBars();
        return;
    }

    if (place == ToolbarPlace::Between) {
        m_strip->addBar(bar, -1, joinRow);
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

void ListerWindow::onToolbarField(const QString &kind, const QString &text, const QString &command, bool entered)
{
    const QString t = text.trimmed();
    if (kind == QLatin1String("filter")) {
        if (FolderTab *tab = sourceTab()) {
            QString f = t;
            const bool invert = f.startsWith(QLatin1Char('!'));
            if (invert)
                f.remove(0, 1);
            tab->setFilter(f, invert, FileSortProxy::FilterScope::Files);
            if (entered)
                tab->focusView();
        }
        return;
    }
    if (!entered || t.isEmpty())
        return;
    QString value = t;
    value.remove(QLatin1Char('"'));
    const QString quoted = QLatin1Char('"') + value + QLatin1Char('"');
    if (kind == QLatin1String("find"))
        runCommand(QStringLiteral("Find NAME=%1 RUN").arg(quoted));
    else if (kind == QLatin1String("go"))
        runCommand(QStringLiteral("Go %1").arg(quoted));
    else if (!command.isEmpty())
        runCommand(QString(command).replace(QLatin1String("{text}"), value.contains(QLatin1Char(' ')) ? quoted : value));
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
    add(QStringLiteral("In the Menu Bar"), ToolbarPlace::MenuBar);
    menu->addSeparator();
    if (current == ToolbarPlace::Between) {
        const bool sideBySide = m_split->orientation() == Qt::Horizontal;
        bool shared = false;
        for (const auto &line : m_strip->lines())
            shared = shared || (line.contains(bar) && line.size() > 1);
        // Along a shared line, or line by line.
        const bool upDown = shared ? sideBySide : !sideBySide;
        QAction *earlier = menu->addAction(upDown ? QStringLiteral("Move Up") : QStringLiteral("Move Left"));
        earlier->setEnabled(m_strip->canMove(bar, -1));
        connect(earlier, &QAction::triggered, this, [this, bar]() { m_strip->moveBar(bar, -1); });
        QAction *later = menu->addAction(upDown ? QStringLiteral("Move Down") : QStringLiteral("Move Right"));
        later->setEnabled(m_strip->canMove(bar, 1));
        connect(later, &QAction::triggered, this, [this, bar]() { m_strip->moveBar(bar, 1); });
        const QString lineWord = sideBySide ? QStringLiteral("Column") : QStringLiteral("Row");
        QAction *join = menu->addAction(QStringLiteral("Same %1 as the Toolbar Before").arg(lineWord));
        join->setCheckable(true);
        join->setChecked(m_strip->isJoined(bar));
        join->setEnabled(m_strip->isJoined(bar) || m_strip->bars().indexOf(bar) > 0);
        join->setToolTip(QStringLiteral("Run on after the toolbar before it instead of starting a %1 of its own (or drag it by an empty spot)")
                             .arg(lineWord.toLower()));
        connect(join, &QAction::toggled, this, [this, bar](bool on) { m_strip->setJoined(bar, on); });
    } else if (current != ToolbarPlace::MenuBar) {
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
        else if (p == QLatin1String("menubar") || p == QLatin1String("menu"))
            place = ToolbarPlace::MenuBar;
        else
            return fail(QStringLiteral("PLACE takes top, bottom, left, right, between or menubar."));
        placeToolbar(bar, place, cmd.hasFlag(QStringLiteral("NEWROW")), cmd.hasFlag(QStringLiteral("SAMEROW")));
    } else if (cmd.hasFlag(QStringLiteral("SAMEROW")) || cmd.hasFlag(QStringLiteral("NEWROW"))) {
        if (!m_strip->contains(bar))
            return fail(QStringLiteral("SAMEROW and NEWROW without PLACE= work on toolbars between the displays."));
        m_strip->setJoined(bar, cmd.hasFlag(QStringLiteral("SAMEROW")));
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
    zoomChanged(); // the tree and the menu follow the source's zoom
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
        m_sharedTree->setZoom(zoom());
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
        m_actThumbs->setChecked(t->viewMode() == FolderTab::ViewMode::Thumbnails);
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
    FileDisplay *other = from == m_displays[0] ? m_displays[1] : m_displays[0];
    FolderTab *t = other->currentTab();
    if (!t)
        return;
    // Works on local folders and servers alike (either side can be either).
    const QString oldClean = Location::clean(oldPath);
    const QString newClean = Location::clean(newPath);
    auto under = [](const QString &child, const QString &parent) {
        return child.startsWith(parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/'));
    };
    QString target;
    if (under(newClean, oldClean)) {
        // Went down: follow into the same relative folder.
        const QString rel = newClean.mid(oldClean.endsWith(QLatin1Char('/')) ? oldClean.size() : oldClean.size() + 1);
        target = Location::clean(t->path());
        for (const QString &part : rel.split(QLatin1Char('/'), Qt::SkipEmptyParts))
            target = Location::join(target, part);
    } else if (under(oldClean, newClean)) {
        // Went up: climb the same number of levels.
        int levels = 0;
        for (QString p = oldClean; p != newClean && levels < 64; p = Location::parent(p))
            ++levels;
        target = Location::clean(t->path());
        for (int i = 0; i < levels; ++i) {
            const QString up = Location::parent(target);
            if (up.isEmpty() || up == target)
                break;
            target = up;
        }
    } else {
        return; // a jump elsewhere is not mirrored
    }
    // A local folder that is not there is not followed; a server folder is
    // tried (the tab says so if it is missing).
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

void ListerWindow::scriptFinished(const ScriptResult &result, const ScriptOptions &options)
{
    if (options.refresh) {
        for (FileDisplay *d : m_displays) {
            if (FolderTab *t = d->currentTab(); t && Location::isLocal(t->path()))
                t->refresh();
        }
    }
    const bool failed = result.crashed || result.exitCode != 0;
    if (!failed && !options.showOutput)
        return;
    if (options.terminal && !failed)
        return;
    // What it printed, in a window of its own (not modal: keep working).
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setObjectName(QStringLiteral("scriptOutput"));
    dlg->setWindowTitle(failed ? QStringLiteral("Script failed (exit %1)").arg(result.exitCode) : QStringLiteral("Script output"));
    auto *layout = new QVBoxLayout(dlg);
    auto *text = new QPlainTextEdit(dlg);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(result.output.isEmpty() ? QStringLiteral("(no output)") : result.output);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    layout->addWidget(buttons);
    dlg->resize(fontMetrics().averageCharWidth() * 90, fontMetrics().height() * 24);
    dlg->show();
}

bool ListerWindow::promptPassword(const QString &title, const QString &label, QString &value)
{
    bool ok = false;
    const QString result = QInputDialog::getText(this, title, label, QLineEdit::Password, QString(), &ok);
    if (!ok)
        return false;
    value = result;
    return true;
}

int ListerWindow::choose(const QString &title, const QString &text, const QStringList &choices)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, this);
    QList<QAbstractButton *> buttons;
    for (const QString &c : choices)
        buttons << box.addButton(c, QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    if (!buttons.isEmpty())
        box.setDefaultButton(qobject_cast<QPushButton *>(buttons.first()));
    box.exec();
    return int(buttons.indexOf(box.clickedButton()));
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
        } else if (key == QLatin1String("VIEWER")) {
            bool on = m_viewer->isVisible();
            if (!parseSwitch(value, on, on))
                return fail(QStringLiteral("VIEWER takes on, off or toggle."));
            m_viewer->setVisible(on);
            if (on)
                updateViewer();
        } else if (key == QLatin1String("VIEWERPOS")) {
            Qt::DockWidgetArea area;
            if (value == QLatin1String("right"))
                area = Qt::RightDockWidgetArea;
            else if (value == QLatin1String("left"))
                area = Qt::LeftDockWidgetArea;
            else if (value == QLatin1String("bottom"))
                area = Qt::BottomDockWidgetArea;
            else
                return fail(QStringLiteral("VIEWERPOS takes right, left or bottom."));
            addDockWidget(area, m_viewer);
            m_viewer->show();
            updateViewer();
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
        } else if (key == QLatin1String("EXPAND")) {
            // The selected folders (or the one in focus) open in place.
            FolderTab *t = sourceTab();
            if (value == QLatin1String("none") || value == QLatin1String("collapseall")) {
                t->collapseAllFolders();
                continue;
            }
            if (value != QLatin1String("toggle") && value != QLatin1String("on") && value != QLatin1String("off"))
                return fail(QStringLiteral("EXPAND takes toggle, on, off or none."));
            if (t->viewMode() != FolderTab::ViewMode::Details || t->flatMode() != DirLister::FlatMode::Off)
                return fail(QStringLiteral("Folders open in place in the Details view, with flat view off."));
            QStringList folders;
            for (const QString &p : t->selectedPaths()) {
                if (t->isFolder(p))
                    folders << p;
            }
            if (folders.isEmpty())
                return fail(QStringLiteral("Select a folder to open in place."));
            for (const QString &p : std::as_const(folders)) {
                const bool open = t->isFolderExpanded(p);
                if (value == QLatin1String("on") || (value == QLatin1String("toggle") && !open))
                    t->expandFolder(p);
                else
                    t->collapseFolder(p);
            }
        } else if (key == QLatin1String("ZOOM")) {
            QString v = value;
            v.remove(QLatin1Char('%'));
            bool isNumber = false;
            const int percent = v.toInt(&isNumber);
            if (v == QLatin1String("in") || v == QLatin1String("bigger"))
                zoomBy(1);
            else if (v == QLatin1String("out") || v == QLatin1String("smaller"))
                zoomBy(-1);
            else if (v == QLatin1String("reset") || v == QLatin1String("normal"))
                setZoom(100);
            else if (isNumber && percent >= 50 && percent <= 300)
                setZoom(percent);
            else
                return fail(QStringLiteral("ZOOM takes in, out, reset or a percentage from 50 to 300."));
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
            // Toggling turns on the kind of flat view used last.
            if (value == QLatin1String("toggle") || value.isEmpty())
                mode = sourceTab()->flatMode() == DirLister::FlatMode::Off ? m_lastFlat : DirLister::FlatMode::Off;
            else if (!FolderTab::flatModeFromName(value, mode))
                return fail(QStringLiteral("FLATVIEW takes off, grouped, ungrouped, nofolders or toggle."));
            if (mode != DirLister::FlatMode::Off)
                m_lastFlat = mode;
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
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    FolderTab *t = sourceTab();
    if (cmd.hasFlag(QStringLiteral("DIALOG")) || cmd.hasFlag(QStringLiteral("ASK"))) {
        auto *dlg = new SelectDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &SelectDialog::commandRequested, this, [this](const QString &c) { runCommand(c); });
        dlg->show();
        return true;
    }
    // Anything besides ALL/NONE/INVERT is a rule: by name, kind, size, age,
    // or against the other display.
    const QStringList ruleKeys = {QStringLiteral("COMPARE"), QStringLiteral("KIND"), QStringLiteral("TYPE"),
                                  QStringLiteral("MINSIZE"), QStringLiteral("MAXSIZE"), QStringLiteral("NEWER"),
                                  QStringLiteral("OLDER"), QStringLiteral("PATTERN"), QStringLiteral("LABEL"),
                                  QStringLiteral("RATING"), QStringLiteral("TAG")};
    bool rules = false;
    for (const QString &k : ruleKeys)
        rules = rules || cmd.hasArg(k);
    if (rules) {
        SelectRules r;
        r.pattern = cmd.values(QStringLiteral("PATTERN")).join(QLatin1Char(';'));
        if (cmd.hasArg(QStringLiteral("KIND"))) {
            if (!FileKind::categoryFromKey(cmd.value(QStringLiteral("KIND")).toLower(), r.kind))
                return fail(QStringLiteral("KIND takes image, audio, video, archive, diskimage, document, code, program or folder."));
            r.hasKind = true;
        }
        const QString type = cmd.value(QStringLiteral("TYPE")).toLower();
        if (type == QLatin1String("files"))
            r.type = SelectRules::Type::Files;
        else if (type == QLatin1String("folders") || type == QLatin1String("dirs"))
            r.type = SelectRules::Type::Folders;
        else if (!type.isEmpty() && type != QLatin1String("both"))
            return fail(QStringLiteral("TYPE takes files, folders or both."));
        auto size = [&](const char *key, qint64 &out) {
            if (!cmd.hasArg(QLatin1String(key)))
                return true;
            out = FindCriteria::parseSize(cmd.value(QLatin1String(key)));
            return out >= 0;
        };
        if (!size("MINSIZE", r.minSize) || !size("MAXSIZE", r.maxSize))
            return fail(QStringLiteral("Sizes look like 500, 700K, 10M or 2G."));
        auto when = [&](const char *key, QDateTime &out) {
            if (!cmd.hasArg(QLatin1String(key)))
                return true;
            out = FindCriteria::parseWhen(cmd.value(QLatin1String(key)));
            return out.isValid();
        };
        if (!when("NEWER", r.newerThan) || !when("OLDER", r.olderThan))
            return fail(QStringLiteral("NEWER and OLDER take a number of days or a date like 2024-01-31."));
        // Labels, ratings, tags: LABEL=Red|any|none, RATING=3 (at least), 3-5,
        // =0 (not rated), TAG=name|none.
        r.label = cmd.value(QStringLiteral("LABEL"));
        if (r.label.compare(QLatin1String("any"), Qt::CaseInsensitive) == 0)
            r.label = QStringLiteral("*");
        r.tag = cmd.value(QStringLiteral("TAG"));
        if (cmd.hasArg(QStringLiteral("RATING"))) {
            const QString v = cmd.value(QStringLiteral("RATING")).trimmed();
            bool ok1 = false, ok2 = true;
            if (v.contains(QLatin1Char('-'))) {
                r.minRating = v.section(QLatin1Char('-'), 0, 0).toInt(&ok1);
                r.maxRating = v.section(QLatin1Char('-'), 1).toInt(&ok2);
            } else {
                r.minRating = v.toInt(&ok1);
                if (r.minRating == 0)
                    r.maxRating = 0;
            }
            if (!ok1 || !ok2 || r.minRating < 0 || r.minRating > 5 || r.maxRating > 5)
                return fail(QStringLiteral("RATING takes 0 to 5 stars (at least that many), or a range like 3-5."));
        }
        if (cmd.hasArg(QStringLiteral("COMPARE"))) {
            if (!SelectRules::compareFromName(cmd.value(QStringLiteral("COMPARE")), r.compare))
                return fail(QStringLiteral("COMPARE takes newer, older, missing, same, different or newerormissing."));
            FolderTab *other = destDisplay() && destDisplay() != sourceDisplay() ? destDisplay()->currentTab() : nullptr;
            if (!other)
                return fail(QStringLiteral("Comparing needs the other display open (dual display)."));
            if (!Location::isLocal(t->path()) || !Location::isLocal(other->path())
                || Collections::isCollectionLocation(t->path()))
                return fail(QStringLiteral("Comparing works between local folders (SD cards and drives included) for now."));
            r.thisRoot = t->path();
            r.otherRoot = other->path();
        }
        const FolderTab::SelectMode mode = cmd.hasFlag(QStringLiteral("DESELECT")) ? FolderTab::SelectMode::Remove
            : cmd.hasFlag(QStringLiteral("ADD"))                                 ? FolderTab::SelectMode::Add
                                                                                 : FolderTab::SelectMode::Replace;
        const int n = t->selectWhere([&r](const FileItem &it) { return r.matches(it); }, mode);
        // BOTH: the same items in the other display too (what matched there).
        if (cmd.hasFlag(QStringLiteral("BOTH")) && r.compare != SelectRules::Compare::None) {
            QStringList there;
            for (const QString &p : t->selectedPaths()) {
                const QString c = r.counterpart(p);
                if (!c.isEmpty())
                    there << c;
            }
            destDisplay()->currentTab()->selectPaths(there);
        }
        Q_UNUSED(n)
        scheduleButtonStates();
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
            *error = QStringLiteral("Select takes ALL, NONE, INVERT, DIALOG, PATTERN=, COMPARE=, KIND=, TYPE=, MINSIZE=, MAXSIZE=, NEWER=, OLDER=, LABEL=, RATING= or TAG=.");
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
    // Dragged out of a flat view: keep the folders the items came from?
    QString flatRoot;
    for (FileDisplay *from : m_displays) {
        FolderTab *t = from->currentTab();
        if (t && t->flatMode() != DirLister::FlatMode::Off && CommandEngine::fromSubfolders(sources, t->path())) {
            flatRoot = t->path();
            break;
        }
    }
    if (flatRoot.isEmpty()) {
        m_app.engine().copyItems(sources, targetDir, move);
        return;
    }
    QString error;
    if (!m_app.engine().copyFromFlat(sources, flatRoot, targetDir, move, CollisionAction::Ask, *this, &error)
        && error != CommandEngine::kCanceled && !error.isEmpty())
        showError(move ? QStringLiteral("Move") : QStringLiteral("Copy"), error);
}

void ListerWindow::onContextMenu(FileDisplay *d, const QPoint &globalPos, bool onItem)
{
    makeSource(d);
    QMenu menu(this);
    fillContextMenu(menu, d, onItem);
    menu.exec(globalPos);
}

void ListerWindow::fillContextMenu(QMenu &menu, FileDisplay *d, bool onItem)
{
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
        // Open With: the applications for the type, then any other.
        bool files = true;
        for (const QString &p : sel)
            files = files && Location::isLocal(p) && !t->isFolder(p);
        if (files && !sel.isEmpty()) {
            QMenu *with = menu.addMenu(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Open With"));
            int shown = 0;
            for (const AppInfo &a : Apps::forFile(sel.first())) {
                if (++shown > 12)
                    break;
                const QString id = a.id;
                QAction *act = with->addAction(OpenWithDialog::iconFor(a), a.name);
                act->setData(id);
                connect(act, &QAction::triggered, this, [this, id, sel]() {
                    QString err;
                    if (!Apps::open(Apps::find(id), sel, &err))
                        showError(QStringLiteral("Open With"), err);
                });
            }
            if (shown > 0)
                with->addSeparator();
            with->addAction(commandAction(QStringLiteral("Other Application..."), QStringLiteral("OpenWith")));
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
        // Pictures: convert them (all of the selection must be pictures).
        bool pictures = sel.size() <= 2000;
        for (const QString &p : sel) {
            if (!pictures)
                break;
            pictures = Location::isLocal(p) && !t->isFolder(p) && ImageConvert::canRead(p);
        }
        // Archives: make one from anything local; extract when all are archives.
        bool local = true;
        bool archives = Archive::available();
        for (const QString &p : sel) {
            local = local && Location::isLocal(p);
            archives = archives && Location::isLocal(p) && !t->isFolder(p) && Archive::isArchive(p);
        }
        if (Archive::available() && local) {
            menu.addSeparator();
            if (archives) {
                menu.addAction(commandAction(QStringLiteral("Extract Here"), QStringLiteral("Archive EXTRACT"), {},
                                             QStringLiteral("archive-extract")));
                menu.addAction(commandAction(QStringLiteral("Extract Here and Trash Archive"), QStringLiteral("Archive EXTRACT TRASH"), {},
                                             QStringLiteral("archive-extract")));
                if (!destPath().isEmpty() && Location::isLocal(destPath()))
                    menu.addAction(commandAction(QStringLiteral("Extract to Destination"), QStringLiteral("Archive EXTRACT DEST"), {},
                                                 QStringLiteral("archive-extract")));
                menu.addAction(commandAction(sel.size() == 1 ? QStringLiteral("Extract to \"%1\"").arg(Archive::baseName(sel.first()))
                                                             : QStringLiteral("Extract Each to Its Own Folder"),
                                             QStringLiteral("Archive EXTRACT MODE=folder"), {}, QStringLiteral("archive-extract")));
            }
            menu.addAction(commandAction(QStringLiteral("Add to Archive..."), QStringLiteral("Archive CREATE"), {},
                                         QStringLiteral("archive-insert")));
            menu.addAction(commandAction(QStringLiteral("Add to \"%1\"").arg(CommandEngine::suggestedArchiveName(sel, t->path(), QStringLiteral("zip"))),
                                         QStringLiteral("Archive CREATE QUICK FORMAT=zip"), {}, QStringLiteral("archive-insert")));
        }
        if (pictures) {
            menu.addSeparator();
            menu.addAction(commandAction(QStringLiteral("View"), QStringLiteral("Show"), {}, QStringLiteral("view-preview")));
            menu.addAction(commandAction(sel.size() == 1 ? QStringLiteral("Convert Image...") : QStringLiteral("Convert Images..."),
                                         QStringLiteral("Image"), {}, QStringLiteral("image-x-generic")));
        }
    }
    menu.addSeparator();
    menu.addAction(commandAction(QStringLiteral("New Folder..."), QStringLiteral("CreateFolder"), {}, QStringLiteral("folder-new")));
    menu.addAction(m_actHidden);
    if (!onItem && t && Location::isLocal(t->path()))
        menu.addAction(commandAction(QStringLiteral("Open Konsole in This Folder"), QStringLiteral("Terminal"), {},
                                     QStringLiteral("utilities-terminal")));
    // Links and properties, for items on this computer.
    bool localSel = !sel.isEmpty();
    for (const QString &p : sel)
        localSel = localSel && Location::isLocal(p);
    if (onItem && localSel) {
        menu.addSeparator();
        if (!destPath().isEmpty() && Location::isLocal(destPath()))
            menu.addAction(commandAction(QStringLiteral("Make Link in Destination"), QStringLiteral("MakeLink DEST"), {},
                                         QStringLiteral("insert-link")));
        menu.addAction(commandAction(QStringLiteral("Make Link Here"), QStringLiteral("MakeLink"), {}, QStringLiteral("insert-link")));
    }
    if (onItem && localSel) {
        // Labels, ratings and tags, as in Opus.
        menu.addSeparator();
        QMenu *labelMenu = menu.addMenu(QIcon::fromTheme(QStringLiteral("tag")), QStringLiteral("Label"));
        for (const LabelDef &d : FileTags::labels()) {
            QPixmap dot(14, 14);
            dot.fill(Qt::transparent);
            QPainter painter(&dot);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(d.color);
            painter.setPen(d.color.darker(130));
            painter.drawEllipse(QRectF(1, 1, 12, 12));
            painter.end();
            QAction *a = labelMenu->addAction(QIcon(dot), d.name);
            const QString name = d.name;
            connect(a, &QAction::triggered, this, [this, name]() { runCommand(QStringLiteral("Tag LABEL=\"%1\"").arg(name)); });
        }
        labelMenu->addSeparator();
        labelMenu->addAction(commandAction(QStringLiteral("No Label"), QStringLiteral("Tag LABEL=none")));
        QMenu *rateMenu = menu.addMenu(QStringLiteral("Rating"));
        for (int i = 5; i >= 1; --i)
            rateMenu->addAction(commandAction(FileTags::stars(i), QStringLiteral("Tag RATING=%1").arg(i)));
        rateMenu->addSeparator();
        rateMenu->addAction(commandAction(QStringLiteral("Not Rated"), QStringLiteral("Tag RATING=0")));
        menu.addAction(commandAction(QStringLiteral("Tags and Description..."), QStringLiteral("Tag"), {}, QStringLiteral("tag-edit")));
        bool lists = true;
        for (const QString &p : sel)
            lists = lists && Checksum::isListFile(Location::fileName(p));
        if (lists)
            menu.addAction(commandAction(QStringLiteral("Verify Checksums"), QStringLiteral("Checksum VERIFY"), {}, QStringLiteral("document-encrypt")));
        static const QRegularExpression piece(QStringLiteral("\\.\\d{3,}$"));
        if (sel.size() >= 1 && piece.match(sel.first()).hasMatch())
            menu.addAction(commandAction(QStringLiteral("Join Pieces"), QStringLiteral("Join"), {}, QStringLiteral("merge")));
        QMenu *sums = menu.addMenu(QIcon::fromTheme(QStringLiteral("document-encrypt")), QStringLiteral("Checksums"));
        for (Checksum::Algo a : Checksum::all())
            sums->addAction(commandAction(Checksum::label(a) + QStringLiteral("..."), QStringLiteral("Checksum ALGO=%1").arg(Checksum::name(a))));
        sums->addSeparator();
        sums->addAction(commandAction(QStringLiteral("Copy MD5"), QStringLiteral("Checksum ALGO=md5 CLIPBOARD")));
        sums->addAction(commandAction(QStringLiteral("Copy SHA-256"), QStringLiteral("Checksum ALGO=sha256 CLIPBOARD")));
        sums->addAction(commandAction(QStringLiteral("Save SHA-256 List"), QStringLiteral("Checksum ALGO=sha256 SAVE")));
    }
    if ((onItem && localSel) || (!onItem && t && Location::isLocal(t->path()))) {
        menu.addSeparator();
        menu.addAction(commandAction(onItem ? QStringLiteral("Properties...") : QStringLiteral("Folder Properties..."),
                                     onItem ? QStringLiteral("Properties") : QStringLiteral("Properties FILE={sourcepath}"), {},
                                     QStringLiteral("document-properties")));
    }
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
    // Linopus's own default for the type first, then the desktop's.
    QString why;
    if (Apps::openWithDefault(path, &why))
        return;
    if (!why.isEmpty()) {
        showError(QStringLiteral("Open"), why);
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
    QStringList joined;
    for (QToolBar *bar : m_strip->bars()) {
        between << bar->objectName();
        if (m_strip->isJoined(bar))
            joined << bar->objectName();
    }
    s.setValue(QStringLiteral("toolbarsBetweenJoined"), joined);
    QStringList inMenu;
    for (QToolBar *bar : m_menuBars)
        inMenu << bar->objectName();
    s.setValue(QStringLiteral("toolbarsMenuBar"), inMenu);
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
    s.remove(QStringLiteral("zoom"));
    for (int i = 0; i < 2; ++i) {
        const FileDisplay *d = m_displays[i];
        s.setValue(QStringLiteral("display%1/zoom").arg(i), d->zoom());
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
    // Older sessions kept one zoom for the whole lister.
    const int oldZoom = s.value(QStringLiteral("zoom"), 100).toInt();
    for (int i = 0; i < 2; ++i)
        setZoom(s.value(QStringLiteral("display%1/zoom").arg(i), oldZoom).toInt(), m_displays[i]);
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
    for (QToolBar *bar : QList<QToolBar *>(m_menuBars))
        placeToolbar(bar, ToolbarPlace::Top);
    QMainWindow::restoreState(s.value(QStringLiteral("windowState")).toByteArray());
    const QStringList between = s.contains(QStringLiteral("toolbarsBetween"))
        ? s.value(QStringLiteral("toolbarsBetween")).toStringList()
        : QStringList{m_drives->objectName()}; // sessions from before the strip existed
    const QStringList joined = s.value(QStringLiteral("toolbarsBetweenJoined")).toStringList();
    for (const QString &name : between) {
        for (QToolBar *bar : toolbars()) {
            if (bar->objectName() == name)
                placeToolbar(bar, ToolbarPlace::Between, false, joined.contains(name));
        }
    }
    for (const QString &name : s.value(QStringLiteral("toolbarsMenuBar")).toStringList()) {
        for (QToolBar *bar : toolbars()) {
            if (bar->objectName() == name)
                placeToolbar(bar, ToolbarPlace::MenuBar);
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
            return fail(CommandEngine::kCanceled);
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
                return fail(CommandEngine::kCanceled);
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
                return fail(CommandEngine::kCanceled);
        } else {
            file = m_app.engine().resolvePath(file, sourcePath());
        }
        if (!cmd.hasFlag(QStringLiteral("QUIET"))
            && !confirm(QStringLiteral("Restore Settings"),
                        QStringLiteral("Replace your toolbars, aliases, themes, labels and folder formats with the ones in %1?")
                            .arg(QFileInfo(file).fileName())))
            return fail(CommandEngine::kCanceled);
        QString err;
        if (!m_app.restoreSettings(file, &err))
            return fail(QStringLiteral("The restore failed: %1").arg(err));
        return true;
    }
    // Anything else: the Preferences window, on a page when asked.
    const QString page = cmd.value(QStringLiteral("PAGE"));
    if (!page.isEmpty() && !PreferencesDialog::pageNames().contains(page.toLower()))
        return fail(QStringLiteral("PAGE takes %1.").arg(PreferencesDialog::pageNames().join(QStringLiteral(", "))));
    PreferencesDialog dlg(m_app, this, page);
    dlg.exec();
    return true;
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
            *error = CommandEngine::kCanceled;
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
    if (cmd.is(QStringLiteral("Find"))) {
        FindCriteria c;
        QStringList roots;
        if (cmd.hasArg(QStringLiteral("PRESET")) && !FindPresets::load(cmd.value(QStringLiteral("PRESET")), c, &roots))
            return fail(QStringLiteral("There is no saved search called \"%1\".").arg(cmd.value(QStringLiteral("PRESET"))));
        if (cmd.hasArg(QStringLiteral("NAME")))
            c.name = cmd.value(QStringLiteral("NAME"));
        if (cmd.hasFlag(QStringLiteral("REGEX")))
            c.nameRegex = true;
        if (cmd.hasFlag(QStringLiteral("CASE")))
            c.caseSensitive = true;
        if (cmd.hasArg(QStringLiteral("CONTAINS")))
            c.contains = cmd.value(QStringLiteral("CONTAINS"));
        auto size = [&](const char *key, qint64 &out) {
            if (!cmd.hasArg(QLatin1String(key)))
                return true;
            out = FindCriteria::parseSize(cmd.value(QLatin1String(key)));
            return out >= 0;
        };
        if (!size("MINSIZE", c.minSize) || !size("MAXSIZE", c.maxSize))
            return fail(QStringLiteral("Sizes look like 500, 700K, 10M or 2G."));
        auto when = [&](const char *key, QDateTime &out) {
            if (!cmd.hasArg(QLatin1String(key)))
                return true;
            out = FindCriteria::parseWhen(cmd.value(QLatin1String(key)));
            return out.isValid();
        };
        if (!when("NEWER", c.newerThan) || !when("OLDER", c.olderThan))
            return fail(QStringLiteral("NEWER and OLDER take a number of days or a date like 2024-01-31."));
        const QString type = cmd.value(QStringLiteral("TYPE")).toLower();
        if (type == QLatin1String("folders"))
            c.type = FindCriteria::Type::Folders;
        else if (type == QLatin1String("both"))
            c.type = FindCriteria::Type::Both;
        else if (!type.isEmpty() && type != QLatin1String("files"))
            return fail(QStringLiteral("TYPE takes files, folders or both."));
        if (cmd.hasFlag(QStringLiteral("HIDDEN")))
            c.includeHidden = true;
        if (cmd.hasFlag(QStringLiteral("NORECURSE")))
            c.recurse = false;
        for (const QString &f : cmd.values(QStringLiteral("IN")))
            roots << engine.resolvePath(f, sourcePath());
        if (roots.isEmpty())
            roots = Location::isLocal(sourcePath()) ? QStringList{sourcePath()} : m_lastFindRoots; // from a results tab: search again
        for (const QString &r : std::as_const(roots)) {
            if (!Location::isLocal(r))
                return fail(QStringLiteral("Find works in local folders for now."));
        }
        if (cmd.hasFlag(QStringLiteral("RUN")) && roots.isEmpty())
            return fail(QStringLiteral("Find needs a folder to look in (IN=...)."));
        if (!roots.isEmpty())
            m_lastFindRoots = roots;
        const QString collection = cmd.value(QStringLiteral("COLLECTION"));
        auto *dlg = new FindDialog(roots, c, collection, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &FindDialog::resultsReady, this, &ListerWindow::showFindResults);
        if (cmd.hasFlag(QStringLiteral("RUN"))) {
            if (c.isEmpty())
                return fail(QStringLiteral("Find RUN needs something to look for (NAME=, CONTAINS=, a size or a date)."));
            // No dialog: it searches out of sight and closes itself when done.
            connect(dlg, &FindDialog::resultsReady, dlg, &QDialog::close);
            dlg->startSearch();
            return true;
        }
        dlg->show();
        return true;
    }
    if (cmd.is(QStringLiteral("Image")))
        return imageCommand(cmd, error);
    if (cmd.is(QStringLiteral("Checksum"))) {
        QStringList paths;
        for (const QString &f : cmd.values(QStringLiteral("FILE")))
            paths << m_app.engine().resolvePath(f, sourcePath());
        if (paths.isEmpty())
            paths = selectedPaths();
        if (paths.isEmpty() && !focusedPath().isEmpty())
            paths << focusedPath();
        if (paths.isEmpty())
            return fail(QStringLiteral("Select something first."));
        for (const QString &p : std::as_const(paths)) {
            if (!Location::isLocal(p))
                return fail(QStringLiteral("Checksums work with items on this computer and its cards for now."));
        }
        // Lists selected (and nothing else): check them, unless told to make.
        bool allLists = true;
        for (const QString &p : std::as_const(paths))
            allLists = allLists && Checksum::isListFile(Location::fileName(p)) && QFileInfo(p).isFile();
        const bool verify = cmd.hasFlag(QStringLiteral("VERIFY"))
            || (allLists && !cmd.hasArg(QStringLiteral("ALGO")) && !cmd.hasArg(QStringLiteral("SAVE")) && !cmd.hasFlag(QStringLiteral("CLIPBOARD")));
        if (verify && !allLists)
            return fail(QStringLiteral("VERIFY needs .md5, .sha1, .sha256 or .sfv lists selected."));
        Checksum::Algo algo = Checksum::Algo::Md5;
        if (cmd.hasArg(QStringLiteral("ALGO")))
            Checksum::fromName(cmd.value(QStringLiteral("ALGO")), &algo);
        auto *dlg = new ChecksumDialog(verify ? ChecksumDialog::Mode::Verify : ChecksumDialog::Mode::Make, paths, algo, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &ChecksumDialog::listSaved, this, [this](const QString &path) { afterLocalChange({path}); });
        const bool quiet = !verify && (cmd.hasFlag(QStringLiteral("CLIPBOARD")) || cmd.hasArg(QStringLiteral("SAVE")));
        if (quiet) {
            QString saveTo;
            if (cmd.hasArg(QStringLiteral("SAVE"))) {
                saveTo = cmd.value(QStringLiteral("SAVE"));
                if (saveTo.compare(QLatin1String("ask"), Qt::CaseInsensitive) == 0) {
                    dlg->show(); // the window, then Save List
                    dlg->start();
                    return true;
                }
                if (!saveTo.isEmpty())
                    saveTo = m_app.engine().resolvePath(saveTo, sourcePath());
            }
            dlg->setThen(cmd.hasFlag(QStringLiteral("CLIPBOARD")) ? ChecksumDialog::Then::Copy : ChecksumDialog::Then::Save, saveTo);
            dlg->start(); // out of sight; shows itself if something goes wrong
            return true;
        }
        dlg->show();
        dlg->start();
        return true;
    }
    if (cmd.is(QStringLiteral("Tag"))) {
        QStringList paths;
        for (const QString &f : cmd.values(QStringLiteral("FILE")))
            paths << m_app.engine().resolvePath(f, sourcePath());
        if (paths.isEmpty())
            paths = selectedPaths();
        auto *dlg = new TagsDialog(paths, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, [this, dlg, paths]() {
            QStringList problems;
            m_app.engine().applyFileTags(dlg->changes(),
                                          paths.size() == 1 ? QStringLiteral("Tag: %1").arg(Location::fileName(paths.first()))
                                                            : QStringLiteral("Tag: %1 items").arg(paths.size()),
                                          &problems);
            afterLocalChange(paths);
            if (!problems.isEmpty())
                showError(QStringLiteral("Tags"), problems.mid(0, 10).join(QLatin1Char('\n')));
        });
        dlg->open();
        return true;
    }
    if (cmd.is(QStringLiteral("Show"))) {
        QStringList picked;
        for (const QString &f : cmd.values(QStringLiteral("FILE")))
            picked << m_app.engine().resolvePath(f, sourcePath());
        if (picked.isEmpty())
            picked = selectedPaths();
        if (picked.isEmpty() && !focusedPath().isEmpty())
            picked << focusedPath();
        QStringList pictures;
        for (const QString &p : std::as_const(picked)) {
            if (Location::isLocal(p) && ImageConvert::canRead(p))
                pictures << p;
        }
        if (pictures.isEmpty())
            return fail(QStringLiteral("Select a picture to view."));
        // Several picked: step through those; one: through its folder.
        auto *viewer = new ImageViewer(pictures.first(), pictures.size() > 1 ? pictures : QStringList(), nullptr);
        viewer->show();
        viewer->raise();
        viewer->activateWindow();
        return true;
    }
    if (cmd.is(QStringLiteral("OpenWith"))) {
        QStringList files;
        for (const QString &f : cmd.values(QStringLiteral("FILE")))
            files << m_app.engine().resolvePath(f, sourcePath());
        if (files.isEmpty())
            files = selectedPaths();
        auto *dlg = new OpenWithDialog(files, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, [this, dlg, files]() {
            const AppInfo app = dlg->chosen();
            if (dlg->alwaysUse())
                Apps::setDefaultFor(dlg->mimeType(), app.id);
            QString err;
            if (!Apps::open(app, files, &err))
                showError(QStringLiteral("Open With"), err);
        });
        dlg->open();
        return true;
    }
    if (cmd.is(QStringLiteral("Server"))) {
        if (cmd.hasFlag(QStringLiteral("ADD"))) {
            // Starting from the server folder being shown, with HERE (or when it is one).
            Server start;
            if (Servers::isServerUrl(sourcePath()))
                start.url = sourcePath();
            auto *dlg = new ServerDialog(start, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &QDialog::accepted, this, [this, dlg]() {
                QString err;
                if (!Servers::instance()->add(dlg->server(), &err))
                    showError(QStringLiteral("Add Server"), err);
            });
            dlg->open();
            return true;
        }
        auto *dlg = new ServersDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &ServersDialog::openRequested, this, [this](const QString &url) {
            runCommand(QStringLiteral("Go \"%1\"").arg(url));
        });
        dlg->open();
        return true;
    }
    if (cmd.is(QStringLiteral("Properties")) || cmd.is(QStringLiteral("SetAttr"))) {
        QStringList paths;
        for (const QString &f : cmd.values(QStringLiteral("FILE")))
            paths << m_app.engine().resolvePath(f, sourcePath());
        if (paths.isEmpty())
            paths = selectedPaths();
        if (paths.isEmpty() && Location::isLocal(sourcePath()))
            paths = {sourcePath()}; // nothing selected: the folder itself
        if (paths.isEmpty())
            return fail(QStringLiteral("Select something first."));
        for (const QString &p : std::as_const(paths)) {
            if (!Location::isLocal(p))
                return fail(QStringLiteral("Properties work with items on this computer and its cards for now."));
        }
        auto *dlg = new PropertiesDialog(paths, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &PropertiesDialog::changed, this, [this]() {
            for (FileDisplay *d : m_displays) {
                if (FolderTab *t = d->currentTab())
                    t->refresh();
            }
        });
        dlg->open();
        return true;
    }
    if (cmd.is(QStringLiteral("Archive"))) {
        // Add to Archive without a name: ask. Runs the command again with one.
        const QStringList sel = selectedPaths();
        if (sel.isEmpty())
            return fail(QStringLiteral("Select the files and folders to put in the archive first."));
        auto *dlg = new ArchiveDialog(sel, sourcePath(), destPath(), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, [this, dlg]() {
            QString err;
            if (!m_app.engine().execute(dlg->parsedCommand(), *this, &err) && !err.isEmpty() && err != CommandEngine::kCanceled)
                showError(QStringLiteral("Add to Archive"), err);
        });
        dlg->open();
        return true;
    }
    if (cmd.is(QStringLiteral("Metadata"))) {
        const QString v = cmd.value(QStringLiteral("PANE")).toLower();
        bool on = m_metaPane->isHidden();
        if (v == QLatin1String("on"))
            on = true;
        else if (v == QLatin1String("off"))
            on = false;
        else if (v != QLatin1String("toggle") && !v.isEmpty())
            return fail(QStringLiteral("PANE takes on, off or toggle."));
        m_metaPane->setVisible(on);
        if (on) {
            m_metaPane->raise();
            updateViewer();
        }
        return true;
    }
    if (cmd.is(QStringLiteral("Undo"))) {
        auto *dlg = new HistoryDialog(engine, cmd.hasFlag(QStringLiteral("LOG")) ? HistoryDialog::Page::Log : HistoryDialog::Page::Undo, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        if (cmd.hasArg(QStringLiteral("FIND")))
            dlg->setLogFilter(cmd.value(QStringLiteral("FIND")));
        connect(dlg, &HistoryDialog::undone, this, [this](const QStringList &paths) { afterLocalChange(paths); });
        dlg->show();
        return true;
    }
    if (cmd.is(QStringLiteral("Layout"))) {
        if (cmd.hasArg(QStringLiteral("SAVE"))) {
            QString name = cmd.value(QStringLiteral("SAVE"));
            bool withFolders = cmd.hasFlag(QStringLiteral("FOLDERS"));
            if (name.isEmpty() || name.compare(QLatin1String("ask"), Qt::CaseInsensitive) == 0) {
                name = QStringLiteral("My Layout");
                if (!promptText(QStringLiteral("Save Layout"), QStringLiteral("Layout name:"), name))
                    return fail(CommandEngine::kCanceled);
                withFolders = withFolders
                    || QMessageBox::question(this, QStringLiteral("Save Layout"),
                                             QStringLiteral("Keep the folders and tabs in the layout too?\n"
                                                            "(No: loading it keeps whatever folders are showing.)"))
                        == QMessageBox::Yes;
            }
            QString err;
            if (!m_app.saveLayout(name, this, withFolders, &err))
                return fail(err);
            return true;
        }
        if (cmd.hasArg(QStringLiteral("LOAD"))) {
            QString err;
            if (!m_app.loadLayout(cmd.value(QStringLiteral("LOAD")), this, &err))
                return fail(err);
            return true;
        }
        if (cmd.hasArg(QStringLiteral("DELETE"))) {
            if (!m_app.deleteLayout(cmd.value(QStringLiteral("DELETE"))))
                return fail(QStringLiteral("There is no layout called %1.").arg(cmd.value(QStringLiteral("DELETE"))));
            return true;
        }
        return fail(QStringLiteral("Layout takes SAVE=, LOAD= or DELETE=."));
    }
    if (cmd.is(QStringLiteral("Favorite"))) {
        Favorites *f = Favorites::instance();
        if (cmd.hasArg(QStringLiteral("GO"))) {
            const QString path = f->pathFor(cmd.value(QStringLiteral("GO")));
            if (path.isEmpty())
                return fail(QStringLiteral("There is no favorite called %1.").arg(cmd.value(QStringLiteral("GO"))));
            if (FolderTab *t = sourceTab())
                t->navigate(path);
            return true;
        }
        if (cmd.hasFlag(QStringLiteral("ORGANIZE")) || cmd.hasFlag(QStringLiteral("ORGANISE"))) {
            FavoritesDialog dlg(this);
            dlg.exec();
            return true;
        }
        const QString path = cmd.hasArg(QStringLiteral("PATH")) ? engine.resolvePath(cmd.value(QStringLiteral("PATH")), sourcePath()) : sourcePath();
        if (path.isEmpty())
            return fail(QStringLiteral("There is no folder to add."));
        if (cmd.hasFlag(QStringLiteral("REMOVE"))) {
            if (!f->removePath(path))
                return fail(QStringLiteral("That folder is not a favorite."));
            scheduleButtonStates();
            return true;
        }
        if (cmd.hasFlag(QStringLiteral("ADD")) || cmd.positional.isEmpty()) {
            if (f->contains(path) && !cmd.hasArg(QStringLiteral("NAME")))
                return true; // already there
            f->add(path, cmd.value(QStringLiteral("NAME")), cmd.value(QStringLiteral("GROUP")));
            scheduleButtonStates();
            return true;
        }
        return fail(QStringLiteral("Favorite takes ADD, REMOVE, GO= or ORGANIZE."));
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

void ListerWindow::showFindResults(const QString &collectionLocation, int count)
{
    Q_UNUSED(count)
    // Reuse a tab already showing these results; otherwise a new one.
    FileDisplay *d = sourceDisplay();
    for (int i = 0; i < d->tabCount(); ++i) {
        if (d->tab(i)->path() == collectionLocation) {
            d->setCurrentIndex(i);
            d->tab(i)->refresh();
            return;
        }
    }
    d->addTab(collectionLocation, true);
}

void ListerWindow::fillDynamicMenu(QMenu *menu, const QString &kind)
{
    auto goAction = [this](QMenu *m, const QString &label, const QString &command, const QString &tip, const QString &icon) {
        QAction *a = m->addAction(QIcon::fromTheme(icon), label);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, [this, command]() { runCommand(command); });
        return a;
    };
    auto quoted = [](const QString &path) { return QLatin1Char('"') + path + QLatin1Char('"'); };
    menu->setToolTipsVisible(true);
    if (kind == QLatin1String("favorites")) {
        std::function<void(QMenu *, const QList<Favorite> &)> add = [&](QMenu *m, const QList<Favorite> &list) {
            for (const Favorite &f : list) {
                if (f.separator)
                    m->addSeparator();
                else if (f.isGroup())
                    add(m->addMenu(QIcon::fromTheme(QStringLiteral("folder-favorites")), f.name), f.items);
                else
                    goAction(m, f.name, QStringLiteral("Go ") + quoted(f.path), Location::display(f.path),
                             Location::isLocal(f.path) ? QStringLiteral("folder") : QStringLiteral("folder-remote"));
            }
        };
        add(menu, Favorites::instance()->items());
        if (!menu->isEmpty())
            menu->addSeparator();
        // The window owns Ctrl+D; a second action with it would make both ambiguous.
        QAction *addHere = menu->addAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), QStringLiteral("Add This Folder\tCtrl+D"));
        addHere->setEnabled(sourceTab() && !Favorites::instance()->contains(sourceTab()->path()));
        connect(addHere, &QAction::triggered, this, [this]() { runCommand(QStringLiteral("Favorite ADD")); });
        connect(menu->addAction(QStringLiteral("Organize Favorites...")), &QAction::triggered, this,
                [this]() { runCommand(QStringLiteral("Favorite ORGANIZE")); });
    } else if (kind == QLatin1String("frequent")) {
        for (const FrequentFolder &f : Favorites::instance()->frequent())
            goAction(menu, Location::title(f.path), QStringLiteral("Go ") + quoted(f.path),
                     QStringLiteral("%1 (%2 visits)").arg(Location::display(f.path)).arg(f.visits),
                     Location::isLocal(f.path) ? QStringLiteral("folder") : QStringLiteral("folder-remote"));
        if (!menu->isEmpty()) {
            menu->addSeparator();
            connect(menu->addAction(QStringLiteral("Clear This List")), &QAction::triggered, this,
                    []() { Favorites::instance()->clearFrequent(); });
        }
    } else if (kind == QLatin1String("aliases")) {
        AliasManager &aliases = m_app.aliases();
        for (const QString &name : aliases.names())
            goAction(menu, QLatin1Char('/') + name, QStringLiteral("Go /") + name, aliases.aliasPath(name), QStringLiteral("folder-bookmark"));
    } else if (kind == QLatin1String("servers")) {
        goAction(menu, QStringLiteral("Windows Shares"), QStringLiteral("Go smb:/"), QStringLiteral("Windows and Samba machines (SMB)"),
                 QStringLiteral("network-workgroup"));
        const QList<Server> saved = Servers::instance()->items();
        if (!saved.isEmpty())
            menu->addSeparator();
        for (const Server &sv : saved)
            goAction(menu, sv.name, QStringLiteral("Go ") + quoted(sv.url), sv.url, QStringLiteral("network-server"));
        menu->addSeparator();
        connect(menu->addAction(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("Add Server...")), &QAction::triggered, this,
                [this]() { runCommand(QStringLiteral("Server ADD")); });
        if (sourceTab() && Servers::isServerUrl(sourceTab()->path()))
            connect(menu->addAction(QStringLiteral("Add This Location...")), &QAction::triggered, this,
                    [this]() { runCommand(QStringLiteral("Server ADD HERE")); });
        connect(menu->addAction(QStringLiteral("Manage Servers...")), &QAction::triggered, this, [this]() { runCommand(QStringLiteral("Server")); });
    } else if (kind == QLatin1String("collections")) {
        for (const QString &name : Collections::instance()->names())
            goAction(menu, name, QStringLiteral("Collection OPEN=") + quoted(name), Collections::location(name),
                     QStringLiteral("folder-favorites"));
        if (!menu->isEmpty())
            menu->addSeparator();
        goAction(menu, QStringLiteral("All Collections"), QStringLiteral("Collection OPEN"), QString(), QStringLiteral("folder-favorites"));
    }
}

void ListerWindow::updateViewer()
{
    FolderTab *t = sourceTab();
    if (m_metaPane && m_metaPane->isVisible()) {
        QStringList sel = t ? t->selectedPaths() : QStringList();
        if (sel.isEmpty() && t && !t->focusedPath().isEmpty())
            sel << t->focusedPath();
        m_metaPane->setFiles(sel);
    }
    if (!m_viewer || !m_viewer->isVisible())
        return;
    m_viewer->showPath(t ? t->focusedPath() : QString());
}

bool ListerWindow::imageCommand(const ParsedCommand &cmd, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    CommandEngine &engine = m_app.engine();
    QStringList candidates;
    for (const QString &f : cmd.values(QStringLiteral("FILE")))
        candidates << engine.resolvePath(f, sourcePath());
    if (candidates.isEmpty())
        candidates = selectedPaths();
    QStringList files;
    bool remote = false;
    for (const QString &p : std::as_const(candidates)) {
        if (!Location::isLocal(p))
            remote = true;
        else if (ImageConvert::canRead(p))
            files << p;
    }
    if (files.isEmpty()) {
        return fail(remote ? QStringLiteral("Convert Images works on pictures in local folders (including SD cards) for now.")
                           : QStringLiteral("Select some pictures to convert first."));
    }

    ImageOptions o = ImageConvertDialog::savedOptions();
    if (cmd.hasArg(QStringLiteral("CONVERT"))) {
        const QString f = cmd.value(QStringLiteral("CONVERT"));
        if (f.compare(QLatin1String("keep"), Qt::CaseInsensitive) == 0) {
            o.format.clear();
        } else {
            o.format = ImageConvert::normalizeFormat(f);
            if (o.format.isEmpty()) {
                QStringList names;
                for (const QString &w : ImageConvert::writableFormats())
                    names << w;
                return fail(QStringLiteral("CONVERT takes keep or one of: %1.").arg(names.join(QStringLiteral(", "))));
            }
        }
    }
    auto number = [&](const char *key, int lo, int hi, int &out) {
        if (!cmd.hasArg(QLatin1String(key)))
            return true;
        bool ok = false;
        const int v = cmd.value(QLatin1String(key)).toInt(&ok);
        if (!ok || v < lo || v > hi)
            return false;
        out = v;
        return true;
    };
    if (!number("QUALITY", 1, 100, o.quality))
        return fail(QStringLiteral("QUALITY takes 1 to 100."));
    if (!number("WIDTH", 1, 65535, o.width) || !number("HEIGHT", 1, 65535, o.height))
        return fail(QStringLiteral("WIDTH and HEIGHT take a number of pixels."));
    if (!number("PERCENT", 1, 1000, o.percent))
        return fail(QStringLiteral("PERCENT takes 1 to 1000."));
    const bool w = cmd.hasArg(QStringLiteral("WIDTH")), h = cmd.hasArg(QStringLiteral("HEIGHT"));
    if (cmd.hasArg(QStringLiteral("PERCENT")))
        o.resize = ImageOptions::Resize::Percent;
    else if (w && h)
        o.resize = cmd.hasFlag(QStringLiteral("STRETCH")) ? ImageOptions::Resize::Exact : ImageOptions::Resize::Fit;
    else if (w)
        o.resize = ImageOptions::Resize::Width;
    else if (h)
        o.resize = ImageOptions::Resize::Height;
    else if (cmd.hasFlag(QStringLiteral("RUN")))
        o.resize = ImageOptions::Resize::None; // a button that only converts keeps the size
    if (cmd.hasFlag(QStringLiteral("ENLARGE")))
        o.noEnlarge = false;
    if (cmd.hasArg(QStringLiteral("ROTATE"))) {
        bool ok = false;
        const int r = ((cmd.value(QStringLiteral("ROTATE")).toInt(&ok) % 360) + 360) % 360;
        if (!ok || r % 90 != 0)
            return fail(QStringLiteral("ROTATE takes 90, 180 or 270."));
        o.rotate = r;
    }
    if (cmd.hasArg(QStringLiteral("FLIP"))) {
        const QString f = cmd.value(QStringLiteral("FLIP")).toLower();
        o.flipH = f == QLatin1String("h") || f == QLatin1String("both");
        o.flipV = f == QLatin1String("v") || f == QLatin1String("both");
        if (!o.flipH && !o.flipV)
            return fail(QStringLiteral("FLIP takes h, v or both."));
    }
    if (cmd.hasArg(QStringLiteral("WHENEXISTS"))
        && !ImageOptions::whenExistsFromName(cmd.value(QStringLiteral("WHENEXISTS")), o.whenExists))
        return fail(QStringLiteral("WHENEXISTS takes rename, replace or skip."));
    const QString dest = Location::isLocal(destPath()) ? destPath() : QString();
    if (cmd.hasArg(QStringLiteral("TO"))) {
        const QString to = cmd.value(QStringLiteral("TO"));
        o.destFolder = to.compare(QLatin1String("dest"), Qt::CaseInsensitive) == 0 ? dest : engine.resolvePath(to, sourcePath());
        if (o.destFolder.isEmpty() || !Location::isLocal(o.destFolder))
            return fail(QStringLiteral("TO needs a local folder (there is no destination to use)."));
    }

    auto *dlg = new ImageConvertDialog(files, o, dest, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (cmd.hasFlag(QStringLiteral("RUN"))) {
        // Out of sight; speaks up only when something went wrong.
        connect(dlg, &ImageConvertDialog::converted, this, [this, dlg](const QList<ImageResult> &results) {
            QStringList failed;
            for (const ImageResult &r : results) {
                if (r.target.isEmpty() && !r.skipped)
                    failed << QStringLiteral("%1: %2").arg(QFileInfo(r.source).fileName(), r.error);
            }
            dlg->close();
            if (!failed.isEmpty()) {
                auto *box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Convert Images"),
                                            QStringLiteral("%1 pictures could not be converted.").arg(failed.size()),
                                            QMessageBox::Ok, this);
                box->setDetailedText(failed.join(QLatin1Char('\n')));
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->show();
            }
        });
        dlg->startConvert();
        if (!dlg->isConverting()) {
            const QString why = dlg->summary();
            dlg->close();
            return fail(QStringLiteral("Nothing to convert: %1").arg(why));
        }
        return true;
    }
    dlg->show();
    return true;
}

// ---------------------------------------------------------------- zoom

QList<int> ListerWindow::zoomLevels()
{
    return {50, 60, 70, 80, 90, 100, 110, 125, 140, 160, 180, 200, 225, 250, 300};
}

int ListerWindow::zoom() const
{
    return sourceDisplay()->zoom();
}

void ListerWindow::setZoom(int percent, FileDisplay *display)
{
    if (!display)
        display = sourceDisplay();
    display->setZoom(qBound(50, percent, 300));
    zoomChanged();
}

void ListerWindow::zoomChanged()
{
    const int percent = zoom();
    if (m_sharedTree)
        m_sharedTree->setZoom(percent);
    if (m_actZoomReset)
        m_actZoomReset->setText(QStringLiteral("Reset Zoom (%1%)").arg(percent));
    scheduleButtonStates();
}

void ListerWindow::zoomBy(int steps, bool showLevel, FileDisplay *display)
{
    if (!display)
        display = sourceDisplay();
    const int current = display->zoom();
    const QList<int> levels = zoomLevels();
    // The nearest level at or past the current zoom, then steps along.
    int at = 0;
    while (at < levels.size() - 1 && levels.at(at) < current)
        ++at;
    if (steps < 0 && levels.at(at) > current && at > 0)
        --at; // between two levels: zooming out goes to the lower one first
    if (levels.at(at) != current)
        steps -= steps > 0 ? 1 : (steps < 0 ? -1 : 0);
    at = qBound(0, int(at + steps), int(levels.size() - 1));
    setZoom(levels.at(at), display);
    if (showLevel)
        QToolTip::showText(QCursor::pos(), QStringLiteral("%1%").arg(display->zoom()), this, {}, 900);
}
