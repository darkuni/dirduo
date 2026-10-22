#include "core/AliasManager.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "ui/AppContext.h"
#include "ui/BreadcrumbBar.h"
#include "ui/ButtonBar.h"
#include "ui/AdvancedRenameDialog.h"
#include "ui/ButtonEditor.h"
#include "ui/DuplicatesDialog.h"
#include "ui/FindDialog.h"
#include "core/Collections.h"
#include "core/Favorites.h"
#include "ui/FavoritesDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/ViewerPane.h"
#include "ui/SyncDialog.h"
#include "ui/CommandConsole.h"
#include "ui/CustomizeDialog.h"
#include "core/CommandCatalog.h"
#include "core/Zip.h"
#include "ui/DrivesBar.h"
#include "ui/ToolbarStrip.h"
#include "ui/FileDisplay.h"
#include "ui/FolderFormats.h"
#include "ui/FolderTab.h"
#include "ui/FolderTree.h"
#include "ui/GroupTreeView.h"
#include "ui/Style.h"
#include "ui/ListerWindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QClipboard>
#include <QImage>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QUrl>
#include <QStandardPaths>
#include <QHeaderView>
#include <QTreeView>
#include <QFile>
#include <QLineEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

QLineEdit *openEditor(QAbstractItemView *view)
{
    const auto edits = view->viewport()->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits) {
        if (e->isVisible())
            return e;
    }
    return nullptr;
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        Paths::setConfigDirOverride(m_dir->filePath(QStringLiteral("config")));
        Paths::setDataHomeOverride(m_dir->filePath(QStringLiteral("data")));
        root = m_dir->filePath(QStringLiteral("root"));
        for (const char *n : {"a/one.txt", "a/two.txt", "a/three.txt", "b/keep.txt"})
            touch(root + QLatin1Char('/') + QLatin1String(n));
        FolderFormats::instance().setFile(m_dir->filePath(QStringLiteral("config/formats.ini")));
        m_app = std::make_unique<AppContext>();
        w = m_app->newLister(root + QStringLiteral("/a"));
        w->display(1)->currentTab()->navigate(root + QStringLiteral("/b"));
        QVERIFY(QTest::qWaitForWindowExposed(w));
        waitListed(w->display(0)->currentTab(), 3);
        waitListed(w->display(1)->currentTab(), 1);
    }

    void cleanup()
    {
        if (w)
            w->close();
        QApplication::processEvents();
        m_app.reset();
        Style::instance()->setTheme(QStringLiteral("Follow Desktop"));
        Style::instance()->setSizeLevel(0);
    }

    void inlineRenameTravels()
    {
        FolderTab *t = w->display(0)->currentTab();
        // Sorted naturally: one, three, two.
        t->selectPaths({root + QStringLiteral("/a/one.txt")});
        QVERIFY(t->beginRename());
        QLineEdit *edit = nullptr;
        QTRY_VERIFY((edit = openEditor(t->view())) != nullptr);
        QTRY_COMPARE(edit->selectedText(), QStringLiteral("one")); // extension left alone
        edit->setText(QStringLiteral("uno.txt"));
        QTest::keyClick(edit, Qt::Key_Down); // commit, move on, keep renaming

        QTRY_VERIFY(QFile::exists(root + QStringLiteral("/a/uno.txt")));
        QLineEdit *next = nullptr;
        QTRY_VERIFY((next = openEditor(t->view())) != nullptr && next != edit);
        QTRY_COMPARE(next->text(), QStringLiteral("three.txt"));
        next->setText(QStringLiteral("tres.txt"));
        QTest::keyClick(next, Qt::Key_Return);
        QTRY_VERIFY(QFile::exists(root + QStringLiteral("/a/tres.txt")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/a/three.txt")));
    }

    void renameToExistingNameIsRefused()
    {
        FolderTab *t = w->display(0)->currentTab();
        t->selectPaths({root + QStringLiteral("/a/one.txt")});
        QVERIFY(t->beginRename());
        QLineEdit *edit = nullptr;
        QTRY_VERIFY((edit = openEditor(t->view())) != nullptr);
        edit->setText(QStringLiteral("two.txt"));
        // The refusal shows a message box; close it as soon as it appears.
        QTimer::singleShot(200, []() {
            if (QWidget *box = QApplication::activeModalWidget())
                box->close();
        });
        QTest::keyClick(edit, Qt::Key_Return);
        QTest::qWait(400);
        QVERIFY(QFile::exists(root + QStringLiteral("/a/one.txt")));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/two.txt")));
    }

    void tabSwitchesSourceUnlessDestLocked()
    {
        QCOMPARE(w->sourceDisplay(), w->display(0));
        QTest::keyClick(w->display(0)->currentTab()->view(), Qt::Key_Tab);
        QTRY_COMPARE(w->sourceDisplay(), w->display(1));
        QCOMPARE(w->sourcePath(), root + QStringLiteral("/b"));
        QCOMPARE(w->destPath(), root + QStringLiteral("/a"));

        QVERIFY(w->runCommand(QStringLiteral("Set DESTLOCK=on"))); // locks display 0, the dest
        QTest::keyClick(w->display(1)->currentTab()->view(), Qt::Key_Tab);
        QTest::qWait(100);
        QCOMPARE(w->sourceDisplay(), w->display(1)); // locked dest stayed dest
        QVERIFY(w->display(0)->destLocked());
    }

    void swapAndSame()
    {
        QVERIFY(w->runCommand(QStringLiteral("Go SWAP")));
        QTRY_COMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/b"));
        QCOMPARE(w->display(1)->currentTab()->path(), root + QStringLiteral("/a"));
        QVERIFY(w->runCommand(QStringLiteral("Go {sourcepath} OPENINDEST")));
        QCOMPARE(w->display(1)->currentTab()->path(), root + QStringLiteral("/b"));
    }

    void addressBarUsesAliases()
    {
        QString error;
        QVERIFY(m_app->aliases().setUserAlias(QStringLiteral("bee"), root + QStringLiteral("/b"), &error));
        BreadcrumbBar *bar = w->display(0)->breadcrumb();
        bar->startEditing();
        auto *edit = bar->findChild<QLineEdit *>();
        QVERIFY(edit && edit->isVisible());
        edit->setText(QStringLiteral("/bee"));
        QTest::keyClick(edit, Qt::Key_Return);
        QTRY_COMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/b"));
        QVERIFY(!bar->isEditing());
    }

    void upSelectsFolderWeCameFrom()
    {
        FolderTab *t = w->display(0)->currentTab();
        QVERIFY(t->up());
        QTRY_COMPARE(t->selectedPaths(), QStringList{root + QStringLiteral("/a")});
        QVERIFY(t->back());
        QTRY_COMPARE(t->path(), root + QStringLiteral("/a"));
        QVERIFY(t->canGoForward());
    }

    void copyButtonCopiesSourceToDest()
    {
        FolderTab *t = w->display(0)->currentTab();
        t->selectPaths({root + QStringLiteral("/a/two.txt")});
        QTRY_COMPARE(t->selectedPaths().size(), 1);
        QVERIFY(w->runCommand(QStringLiteral("Copy")));
        QVERIFY(m_app->jobs().waitForIdle(5000));
        QVERIFY(QFile::exists(root + QStringLiteral("/b/two.txt")));
        // The destination display picks up the new file by itself.
        waitListed(w->display(1)->currentTab(), 2);
    }

    void navigationLockMirrors()
    {
        // Same layout under both sides: a/sub/deep and b/sub/deep.
        QDir().mkpath(root + QStringLiteral("/a/sub/deep"));
        QDir().mkpath(root + QStringLiteral("/b/sub/deep"));
        QVERIFY(w->runCommand(QStringLiteral("Set NAVLOCK=on")));
        FolderTab *src = w->display(0)->currentTab();
        FolderTab *dst = w->display(1)->currentTab();
        src->navigate(root + QStringLiteral("/a/sub/deep"));
        QTRY_COMPARE(dst->path(), root + QStringLiteral("/b/sub/deep"));
        src->up();
        QTRY_COMPARE(dst->path(), root + QStringLiteral("/b/sub"));
        QDir().mkpath(root + QStringLiteral("/c"));
        src->navigate(root + QStringLiteral("/c")); // a sideways jump is not mirrored
        QTest::qWait(100);
        QCOMPARE(dst->path(), root + QStringLiteral("/b/sub"));
    }

    void dropOnSameDriveMoves()
    {
        FileDisplay *dest = w->display(1);
        const QList<QUrl> urls{QUrl::fromLocalFile(root + QStringLiteral("/a/one.txt"))};
        Q_EMIT dest->dropRequested(dest, urls, root + QStringLiteral("/b"), Qt::CopyAction);
        QVERIFY(m_app->jobs().waitForIdle(5000));
        QVERIFY(QFile::exists(root + QStringLiteral("/b/one.txt")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/a/one.txt")));
        // Dropping back into the folder it came from does nothing.
        const QList<QUrl> back{QUrl::fromLocalFile(root + QStringLiteral("/b/one.txt"))};
        Q_EMIT dest->dropRequested(dest, back, root + QStringLiteral("/b"), Qt::CopyAction);
        QVERIFY(m_app->jobs().waitForIdle(5000));
        QVERIFY(!QFile::exists(root + QStringLiteral("/b/one (2).txt")));
    }

    void createFoldersSelectsThem()
    {
        QVERIFY(w->runCommand(QStringLiteral("CreateFolder NAME=\"X, Y\"")));
        FolderTab *t = w->display(0)->currentTab();
        QTRY_COMPARE(t->selectedPaths().size(), 2);
        QVERIFY(QFileInfo(root + QStringLiteral("/a/X")).isDir());
    }

    void drivesBarStartsBetweenDisplays()
    {
        QCOMPARE(w->toolbarPlace(w->drivesBar()), ListerWindow::ToolbarPlace::Between);
        QVERIFY(w->toolbarStrip()->isVisible());
        const auto &drives = w->drivesBar()->drives();
        QVERIFY(drives.size() >= 2);
        QCOMPARE(drives.at(0).name, QStringLiteral("Root"));
        QCOMPARE(drives.at(1).name, QStringLiteral("Home"));
    }

    void driveButtonsNavigate()
    {
        QList<DriveEntry> drives = w->drivesBar()->drives().mid(0, 2);
        DriveEntry card;
        card.kind = DriveEntry::Kind::SdCard;
        card.name = QStringLiteral("Card");
        card.path = root + QStringLiteral("/b");
        drives << card;
        w->drivesBar()->setDrivesForTesting(drives);
        auto buttons = w->drivesBar()->findChildren<ThreeButton *>();
        QCOMPARE(buttons.size(), 3);
        QTest::mouseClick(buttons.at(2), Qt::LeftButton);
        QTRY_COMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/b"));
        QTRY_VERIFY(buttons.at(2)->isChecked()); // marks the drive the source is on
        QVERIFY(!buttons.at(0)->isChecked());
        // Right-click opens it in the destination.
        w->display(1)->currentTab()->navigate(root + QStringLiteral("/a"));
        QTest::mouseClick(buttons.at(2), Qt::RightButton);
        QTRY_COMPARE(w->display(1)->currentTab()->path(), root + QStringLiteral("/b"));
    }

    void toolbarsMoveAndStack()
    {
        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=buttons PLACE=between")));
        QCOMPARE(w->toolbarStrip()->bars().size(), 2);
        // Side by side: the strip's bars run vertically.
        QCOMPARE(w->toolbarByName(QStringLiteral("buttons"))->orientation(), Qt::Vertical);
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=horizontal")));
        QCOMPARE(w->toolbarByName(QStringLiteral("buttons"))->orientation(), Qt::Horizontal);

        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=drives PLACE=left\nToolbar NAME=buttons PLACE=left NEWROW")));
        QCOMPARE(w->toolbarPlace(w->drivesBar()), ListerWindow::ToolbarPlace::Left);
        QVERIFY(!w->toolbarStrip()->isVisible()); // nothing left between the displays
        QVERIFY(w->runCommand(QStringLiteral("Toolbar drives STATE=off")));
        QVERIFY(w->drivesBar()->isHidden());
        QTimer::singleShot(200, []() {
            if (QWidget *box = QApplication::activeModalWidget())
                box->close();
        });
        QVERIFY(!w->runCommand(QStringLiteral("Toolbar NAME=nope PLACE=top"))); // error box, closed above
    }

    void toolbarPlacementIsSaved()
    {
        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=buttons PLACE=between\nToolbar NAME=drives PLACE=bottom")));
        m_app->saveSession({w});
        w->close();
        w = nullptr;
        QApplication::processEvents();
        m_app->restoreSession();
        w = m_app->activeLister();
        QVERIFY(w);
        QCOMPARE(w->toolbarPlace(w->toolbarByName(QStringLiteral("buttons"))), ListerWindow::ToolbarPlace::Between);
        QCOMPARE(w->toolbarPlace(w->drivesBar()), ListerWindow::ToolbarPlace::Bottom);
    }

    void groupingAndFilter()
    {
        FolderTab *t = w->display(0)->currentTab();
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=type")));
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::Type);
        auto *view = qobject_cast<GroupTreeView *>(t->view());
        QVERIFY(view && view->grouping());
        QVERIFY(view->isGroupLeader(view->model()->index(0, 0)));
        QVERIFY(!view->isGroupLeader(view->model()->index(1, 0))); // three text files, one group
        QCOMPARE(view->groupRows(view->model()->index(2, 0)).size(), 3);

        QVERIFY(w->runCommand(QStringLiteral("Set FILTER=one*")));
        QVERIFY(t->filterBarVisible());
        QCOMPARE(t->view()->model()->rowCount(), 1);
        QVERIFY(t->statusText().contains(QStringLiteral("2 hidden")));
        QVERIFY(w->runCommand(QStringLiteral("Set FILTER=!one*")));
        QCOMPARE(t->view()->model()->rowCount(), 2);
        QVERIFY(w->runCommand(QStringLiteral("Set FILTERBAR=off")));
        QCOMPARE(t->view()->model()->rowCount(), 3);
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=none")));
        QVERIFY(!view->grouping());
        QVERIFY(!runFailing(QStringLiteral("Set GROUPBY=colour")));
    }

    void columnsHiddenByDefaultAndToggle()
    {
        FolderTab *t = w->display(0)->currentTab();
        QVERIFY(t->isColumnVisible(FileListModel::SizeColumn));
        QVERIFY(!t->isColumnVisible(FileListModel::CreatedColumn));
        QVERIFY(!t->isColumnVisible(FileListModel::LocationColumn));
        QVERIFY(w->runCommand(QStringLiteral("Set COLUMNSTOGGLE=created")));
        QVERIFY(t->isColumnVisible(FileListModel::CreatedColumn));
        t->resetColumns();
        QVERIFY(!t->isColumnVisible(FileListModel::CreatedColumn));
    }

    void flatViewModes()
    {
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(root);
        waitListed(t, 2);
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=mixed")));
        waitListed(t, 6); // a, b and four files
        QVERIFY(t->isColumnVisible(FileListModel::LocationColumn));
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=nofolders")));
        waitListed(t, 4);
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=grouped")));
        waitListed(t, 6);
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::Folder);
        QVERIFY(t->statusText().contains(QStringLiteral("flat view")));
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=off")));
        waitListed(t, 2);
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::None);
        QVERIFY(!t->isColumnVisible(FileListModel::LocationColumn));
    }

    void checkBoxesToggleSelection()
    {
        FolderTab *t = w->display(0)->currentTab();
        QVERIFY(w->runCommand(QStringLiteral("Set CHECKBOXES=on")));
        QVERIFY(t->checkMode());
        t->selectNone();
        auto *view = qobject_cast<QTreeView *>(t->view());
        const QModelIndex first = view->model()->index(0, 0);
        const QModelIndex second = view->model()->index(1, 0);
        const int x = view->header()->sectionViewportPosition(0) + 4 + 6;
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, QPoint(x, view->visualRect(first).center().y()));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, QPoint(x, view->visualRect(second).center().y()));
        QCOMPARE(t->selectedPaths().size(), 2); // boxes add to the selection
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, QPoint(x, view->visualRect(first).center().y()));
        QCOMPARE(t->selectedPaths().size(), 1);
    }

    void folderSizesCalculate()
    {
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(root);
        waitListed(t, 2);
        QVERIFY(w->runCommand(QStringLiteral("Set FOLDERSIZES=on")));
        auto bytesOf = [t](const QString &name) -> qint64 {
            QAbstractItemModel *m = t->view()->model();
            for (int r = 0; r < m->rowCount(); ++r) {
                if (m->index(r, 0).data().toString() == name)
                    return m->index(r, 0).data(FileListModel::FolderBytesRole).toLongLong();
            }
            return -2;
        };
        QTRY_COMPARE_WITH_TIMEOUT(bytesOf(QStringLiteral("a")), qint64(3), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(bytesOf(QStringLiteral("b")), qint64(1), 5000);
    }

    void folderTrees()
    {
        QVERIFY(w->runCommand(QStringLiteral("Set TREE=shared")));
        QCOMPARE(w->treeMode(), ListerWindow::TreeMode::Shared);
        FolderTree *tree = w->sharedTree();
        QVERIFY(tree && tree->isVisible());
        QCOMPARE(tree->currentFolder(), root + QStringLiteral("/a"));
        // Picking a folder in the tree reads it into the source.
        tree->setCurrentIndex(tree->treeModel()->indexForPath(root + QStringLiteral("/b")));
        QTRY_COMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/b"));
        // ...and follows the source when it moves.
        w->display(0)->currentTab()->navigate(root + QStringLiteral("/a"));
        QCOMPARE(tree->currentFolder(), root + QStringLiteral("/a"));

        QVERIFY(w->runCommand(QStringLiteral("Set TREE=separate")));
        QVERIFY(!tree->isVisible());
        QVERIFY(w->display(0)->treeVisible() && w->display(1)->treeVisible());
        QCOMPARE(w->display(1)->tree()->currentFolder(), root + QStringLiteral("/b"));
        QVERIFY(w->runCommand(QStringLiteral("Set TREE=toggle")));
        QCOMPARE(w->treeMode(), ListerWindow::TreeMode::Off);
        QVERIFY(!w->display(0)->treeVisible());
        QVERIFY(w->runCommand(QStringLiteral("Set TREE=toggle")));
        QCOMPARE(w->treeMode(), ListerWindow::TreeMode::Separate); // the last one used
    }

    void folderFormatsFollowFolders()
    {
        FolderTab *t = w->display(0)->currentTab();
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=type\nSet VIEW=list\nSet FORMAT=save")));
        t->navigate(root + QStringLiteral("/b"));
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Details);
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::None);
        t->navigate(root + QStringLiteral("/a"));
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::List);
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::Type);
        QVERIFY(w->runCommand(QStringLiteral("Set FORMAT=clear")));
        QVERIFY(!runFailing(QStringLiteral("Set FORMAT=clear")));

        // Subfolders inherit a format saved "with subfolders".
        QVERIFY(w->runCommand(QStringLiteral("Set VIEW=icons\nSet FORMAT=savesub")));
        QDir().mkpath(root + QStringLiteral("/a/deeper"));
        t->navigate(root + QStringLiteral("/a/deeper"));
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Icons);
        t->navigate(root + QStringLiteral("/b"));
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Details);
    }

    void pictureFoldersSwitchToIcons()
    {
        for (const char *n : {"p/1.png", "p/2.jpg", "p/3.jpg", "p/4.webp", "p/notes.txt"})
            touch(root + QLatin1Char('/') + QLatin1String(n));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(root + QStringLiteral("/p"));
        waitListed(t, 5);
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Icons);
        t->navigate(root + QStringLiteral("/a"));
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Details);
        QVERIFY(w->runCommand(QStringLiteral("Set AUTOFORMAT=off")));
        t->navigate(root + QStringLiteral("/p"));
        waitListed(t, 5);
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Details);
        QVERIFY(w->runCommand(QStringLiteral("Set AUTOFORMAT=on")));
    }

    void themesApplyAndPersist()
    {
        QVERIFY(w->runCommand(QStringLiteral("Set THEME=\"linopus dark\"")));
        QCOMPARE(Style::instance()->theme().name, QStringLiteral("Linopus Dark"));
        QVERIFY(QApplication::palette().color(QPalette::Base).lightness() < 100);
        QCOMPARE(m_app->settings().value(QStringLiteral("Appearance/theme")).toString(), QStringLiteral("Linopus Dark"));
        QVERIFY(w->runCommand(QStringLiteral("Set THEME=Workbench")));
        QCOMPARE(Style::instance()->theme().name, QStringLiteral("Workbench"));
        QVERIFY(!runFailing(QStringLiteral("Set THEME=Nope")));
        QVERIFY(w->runCommand(QStringLiteral("Set THEME=next")));
        QVERIFY(Style::instance()->theme().name != QStringLiteral("Workbench"));
    }

    void viewStateRoundTrip()
    {
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=extension\nSet CHECKBOXES=on\nSet TREE=shared")));
        m_app->saveSession({w});
        w->close();
        w = nullptr;
        QApplication::processEvents();
        m_app->restoreSession();
        w = m_app->activeLister();
        QVERIFY(w);
        QCOMPARE(w->treeMode(), ListerWindow::TreeMode::Shared);
        FolderTab *t = w->display(0)->currentTab();
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::Extension);
        QVERIFY(t->checkMode());
    }

    void treeRootsShowHomeDrivesAndAliases()
    {
        QVERIFY(m_app->aliases().setUserAlias(QStringLiteral("roms"), root + QStringLiteral("/b")));
        QVERIFY(w->runCommand(QStringLiteral("Set TREE=shared")));
        FolderTree *tree = w->sharedTree();
        DriveEntry rootDrive;
        rootDrive.kind = DriveEntry::Kind::Root;
        rootDrive.name = QStringLiteral("/");
        rootDrive.path = QStringLiteral("/");
        DriveEntry sd;
        sd.kind = DriveEntry::Kind::SdCard;
        sd.name = QStringLiteral("Games");
        sd.path = root; // stands in for /run/media/deck/Games
        tree->setDrivesForTesting({rootDrive, sd});
        FolderTreeModel *m = tree->treeModel();
        QStringList top;
        for (int r = 0; r < m->rowCount(); ++r)
            top << m->index(r, 0).data().toString();
        QCOMPARE(top, (QStringList{QStringLiteral("Home"), QStringLiteral("Root"), QStringLiteral("Games"), QStringLiteral("Aliases")}));
        const QModelIndex aliases = m->index(3, 0);
        QCOMPARE(m->rowCount(aliases), 1);
        QCOMPARE(m->index(0, 0, aliases).data().toString(), QStringLiteral("/roms"));
        QVERIFY(!(m->flags(aliases) & Qt::ItemIsSelectable));

        // The source folder lies under the SD card: that root is the one opened.
        const QModelIndex cur = tree->currentIndex();
        QCOMPARE(m->pathOf(cur), root + QStringLiteral("/a"));
        QCOMPARE(cur.parent().data().toString(), QStringLiteral("Games"));

        // New folders appear without a refresh.
        QDir().mkpath(root + QStringLiteral("/fresh"));
        const QModelIndex games = m->index(2, 0);
        QTRY_VERIFY_WITH_TIMEOUT(m->rowCount(games) == 3, 5000);
        QCOMPARE(m->index(1, 0, games).data().toString(), QStringLiteral("b"));
        QDir(root + QStringLiteral("/fresh")).removeRecursively();
        QTRY_VERIFY_WITH_TIMEOUT(m->rowCount(games) == 2, 5000);
    }

    void groupsCollapse()
    {
        touch(root + QStringLiteral("/a/song.mp3"));
        FolderTab *t = w->display(0)->currentTab();
        t->refresh();
        waitListed(t, 4);
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=type")));
        GroupTreeView *view = t->detailsView();
        QCOMPARE(view->groupKeys().size(), 2); // audio, text
        // Text group: three rows, the first stays as the heading.
        int textFirst = -1;
        for (int r = 0; r < view->model()->rowCount(); ++r) {
            if (view->model()->index(r, 0).data().toString().endsWith(QLatin1String(".txt"))) {
                textFirst = r;
                break;
            }
        }
        QVERIFY(textFirst >= 0);
        const QModelIndex lead = view->model()->index(textFirst, 0);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->arrowRect(lead).center());
        QVERIFY(view->isCollapsed(lead));
        QVERIFY(!view->isRowHidden(textFirst, {}));
        QVERIFY(view->isRowHidden(textFirst + 1, {}) && view->isRowHidden(textFirst + 2, {}));
        QCOMPARE(view->visualRect(lead).height(), view->bandHeight());
        // A refresh keeps it folded.
        t->refresh();
        QTest::qWait(300);
        QVERIFY(view->isRowHidden(textFirst + 1, {}));
        // Clicking the heading still selects the whole group.
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->bandRect(lead).center());
        QCOMPARE(t->selectedPaths().size(), 3);
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPCOLLAPSE=none")));
        QVERIFY(!view->isRowHidden(textFirst + 1, {}));
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPCOLLAPSE=all")));
        QVERIFY(view->isRowHidden(textFirst + 1, {}));
        // Left and Right fold and open the group the cursor is in.
        view->setFocus();
        view->setCurrentIndex(lead);
        QTest::keyClick(view, Qt::Key_Right);
        QVERIFY(!view->isCollapsed(lead));
        QTest::keyClick(view, Qt::Key_Left);
        QVERIFY(view->isCollapsed(lead));
        // Grouping off shows everything.
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=none")));
        for (int r = 0; r < view->model()->rowCount(); ++r)
            QVERIFY(!view->isRowHidden(r, {}));
        QVERIFY(!runFailing(QStringLiteral("Set GROUPCOLLAPSE=all")));
    }

    void dateAndSizeBuckets()
    {
        auto setAge = [this](const char *name, int daysAgo) {
            const QString p = root + QStringLiteral("/a/") + QLatin1String(name);
            QFile f(p);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-daysAgo), QFileDevice::FileModificationTime));
        };
        setAge("one.txt", 0);
        setAge("two.txt", 400);
        setAge("three.txt", -30);
        FolderTab *t = w->display(0)->currentTab();
        t->refresh();
        QTest::qWait(300);
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=modified")));
        QTRY_VERIFY(!labels(t).isEmpty());
        QStringList l = labels(t);
        QCOMPARE(l.first(), QStringLiteral("In the future"));
        QVERIFY(l.contains(QStringLiteral("Today")));
        QVERIFY(l.last() == QStringLiteral("Last year") || l.last() == QStringLiteral("Older"));
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=size")));
        QCOMPARE(labels(t), (QStringList{QStringLiteral("Tiny (under 16 KB)")}));
    }

    void buttonStatesFollowConditions()
    {
        ToolbarStore &store = m_app->toolbars();
        ButtonDef d;
        d.label = QStringLiteral("Needs Selection");
        d.left = QStringLiteral("Set VIEW=list");
        d.enableWhen = QStringLiteral("selection && dest");
        d.checkWhen = QStringLiteral("layout=vertical");
        store.insertButton(QStringLiteral("buttons"), 0, d);
        ButtonBar *bar = w->buttonBar(QStringLiteral("buttons"));
        QTRY_VERIFY(bar->buttonAt(0) && bar->buttonAt(0)->text() == QStringLiteral("Needs Selection"));
        FolderTab *t = w->display(0)->currentTab();
        t->selectNone();
        QTRY_VERIFY(!bar->buttonAt(0)->isEnabled());
        t->selectAll();
        QTRY_VERIFY(bar->buttonAt(0)->isEnabled());
        QVERIFY(bar->buttonAt(0)->isChecked());
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=horizontal")));
        QTRY_VERIFY(!bar->buttonAt(0)->isChecked());
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=off")));
        QTRY_VERIFY(!bar->buttonAt(0)->isEnabled()); // no destination
        // A broken condition leaves the button usable.
        QString err;
        QVERIFY(w->evaluateCondition(QStringLiteral("selection &&"), true, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(w->evaluateCondition(QStringLiteral("files && !folders && view=details"), false));
    }

    void menuButtonsRunTheirItems()
    {
        ButtonDef menu;
        menu.label = QStringLiteral("Views");
        ButtonDef a;
        a.label = QStringLiteral("As List");
        a.left = QStringLiteral("Set VIEW=list");
        ButtonDef b;
        b.label = QStringLiteral("As Icons");
        b.left = QStringLiteral("Set VIEW=icons");
        b.checkWhen = QStringLiteral("view=icons");
        menu.children = {a, ButtonDef::makeSeparator(), b};
        m_app->toolbars().insertButton(QStringLiteral("buttons"), 0, menu);
        ButtonBar *bar = w->buttonBar(QStringLiteral("buttons"));
        QTRY_VERIFY(bar->buttonAt(0) && bar->buttonAt(0)->menu());
        QMenu *m = bar->buttonAt(0)->menu();
        QCOMPARE(m->actions().size(), 3);
        m->actions().at(2)->trigger();
        QCOMPARE(w->display(0)->currentTab()->viewMode(), FolderTab::ViewMode::Icons);
        Q_EMIT m->aboutToShow();
        QVERIFY(m->actions().at(2)->isChecked());
    }

    void filesDroppedOnAButton()
    {
        ButtonBar *bar = w->buttonBar(QStringLiteral("buttons"));
        ThreeButton *copy = bar->buttonAt(0);
        QCOMPARE(copy->text(), QStringLiteral("Copy"));
        w->display(0)->currentTab()->selectNone();
        w->updateButtonStates();
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(root + QStringLiteral("/a/two.txt"))});
        QVERIFY(!copy->isEnabled()); // nothing selected, yet it takes dropped files
        const QPoint at = copy->geometry().center();
        QDragEnterEvent enter(at, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bar, &enter);
        QDragMoveEvent move(at, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bar, &move);
        QVERIFY(move.isAccepted());
        QDropEvent drop(QPointF(at), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(bar, &drop);
        m_app->jobs().waitForIdle(5000);
        QTRY_VERIFY(QFile::exists(root + QStringLiteral("/b/two.txt")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/b/one.txt"))); // only what was dropped
        delete mime;
    }

    void buttonHotkeys()
    {
        ButtonDef d;
        d.label = QStringLiteral("Icons");
        d.left = QStringLiteral("Set VIEW=icons");
        d.key = QStringLiteral("Ctrl+Alt+K");
        m_app->toolbars().insertButton(QStringLiteral("buttons"), 0, d);
        QTRY_VERIFY(w->buttonBar(QStringLiteral("buttons"))->buttonAt(0)->text() == QStringLiteral("Icons"));
        w->activateWindow();
        QTest::keyClick(w, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(w->display(0)->currentTab()->viewMode(), FolderTab::ViewMode::Icons);
    }

    void customizeModeMovesButtons()
    {
        ToolbarStore &store = m_app->toolbars();
        QVERIFY(w->runCommand(QStringLiteral("Toolbar CUSTOMIZE")));
        QVERIFY(w->isCustomizing());
        ButtonBar *main = w->buttonBar(QStringLiteral("buttons"));
        QVERIFY(main->editMode());
        // A click in customize mode does not run the button.
        const QString before = w->display(0)->currentTab()->path();
        QTest::mouseClick(main->buttonAt(0), Qt::LeftButton);
        m_app->jobs().waitForIdle(2000);
        QVERIFY(!QFile::exists(root + QStringLiteral("/b/one.txt")));
        QCOMPARE(w->display(0)->currentTab()->path(), before);

        auto dropOn = [](ButtonBar *bar, const QJsonObject &payload, int beforeIndex, Qt::KeyboardModifiers mods = {}) {
            auto *mime = new QMimeData;
            mime->setData(QLatin1String(ButtonBar::kMime), QJsonDocument(payload).toJson());
            QPointF pos(4, 4);
            if (beforeIndex < bar->items().size())
                pos = QPointF(bar->items().at(beforeIndex)->geometry().left() + 1, bar->items().at(beforeIndex)->geometry().center().y());
            else if (!bar->items().isEmpty())
                pos = QPointF(bar->items().last()->geometry().right() - 1, bar->items().last()->geometry().center().y());
            // A real drop always follows a drag entering the widget.
            QDragEnterEvent enter(pos.toPoint(), Qt::MoveAction | Qt::CopyAction, mime, Qt::LeftButton, mods);
            QApplication::sendEvent(bar, &enter);
            QDropEvent drop(pos, Qt::MoveAction | Qt::CopyAction, mime, Qt::LeftButton, mods);
            QApplication::sendEvent(bar, &drop);
            delete mime;
        };
        auto labels = [&store](const QString &id) {
            QStringList l;
            for (const ButtonDef &d : store.toolbar(id).buttons)
                l << (d.separator ? QStringLiteral("|") : d.label);
            return l;
        };
        // Move Copy to after Move within the main bar.
        QJsonObject p;
        p.insert(QStringLiteral("toolbar"), QStringLiteral("buttons"));
        p.insert(QStringLiteral("index"), 0);
        p.insert(QStringLiteral("button"), store.toolbar(QStringLiteral("buttons")).buttons.at(0).toJson());
        dropOn(main, p, 2);
        QCOMPARE(labels(QStringLiteral("buttons")).mid(0, 3), (QStringList{QStringLiteral("Move"), QStringLiteral("Copy"), QStringLiteral("Delete")}));

        // A new toolbar, shown here; move Delete onto it, copy Rename with Ctrl.
        const QString id = store.create(QStringLiteral("My Buttons"));
        ButtonBar *mine = w->buttonBar(id);
        QVERIFY(mine && w->isButtonBarVisible(id) && mine->editMode());
        p.insert(QStringLiteral("index"), 2);
        p.insert(QStringLiteral("button"), store.toolbar(QStringLiteral("buttons")).buttons.at(2).toJson());
        dropOn(mine, p, 0);
        QCOMPARE(labels(id), QStringList{QStringLiteral("Delete")});
        QVERIFY(!labels(QStringLiteral("buttons")).contains(QStringLiteral("Delete")));
        QApplication::processEvents();
        p.insert(QStringLiteral("index"), 2);
        p.insert(QStringLiteral("button"), store.toolbar(QStringLiteral("buttons")).buttons.at(2).toJson());
        QCOMPARE(store.toolbar(QStringLiteral("buttons")).buttons.at(2).label, QStringLiteral("Rename"));
        dropOn(mine, p, 1, Qt::ControlModifier);
        QCOMPARE(labels(id), (QStringList{QStringLiteral("Delete"), QStringLiteral("Rename")}));
        QVERIFY(labels(QStringLiteral("buttons")).contains(QStringLiteral("Rename")));

        // A ready-made button from the dialog's command list.
        CustomizeDialog *dlg = w->customizeDialog();
        QVERIFY(dlg);
        int theme = -1;
        for (int i = 0; i < CommandCatalog::buttons().size(); ++i) {
            if (CommandCatalog::buttons().at(i).label == QLatin1String("Theme"))
                theme = i;
        }
        dlg->toolbarList()->setCurrentRow(1);
        QCOMPARE(dlg->currentToolbar(), id);
        dlg->addCatalogButton(theme);
        QCOMPARE(labels(id).last(), QStringLiteral("Theme"));

        // Hiding a toolbar from the list.
        dlg->toolbarList()->item(1)->setCheckState(Qt::Unchecked);
        QVERIFY(!w->isButtonBarVisible(id));

        // Saved: a fresh store reads the same thing.
        ToolbarStore again(store.fileFor(id).section(QLatin1Char('/'), 0, -2));
        QCOMPARE(again.toolbar(id).name, QStringLiteral("My Buttons"));
        QCOMPARE(again.toolbar(id).buttons.size(), 3);
        QVERIFY(again.isCustomized(QStringLiteral("buttons")));

        dlg->close();
        QApplication::processEvents();
        QTRY_VERIFY(!w->isCustomizing());
        QVERIFY(!main->editMode());

        // Removing it takes it out of the lister; reset brings the standard bar back.
        QVERIFY(store.remove(id));
        QVERIFY(!w->buttonBar(id));
        store.resetToDefault(QStringLiteral("buttons"));
        QCOMPARE(labels(QStringLiteral("buttons")).mid(0, 3), (QStringList{QStringLiteral("Copy"), QStringLiteral("Move"), QStringLiteral("Delete")}));
        QVERIFY(!store.isCustomized(QStringLiteral("buttons")));
    }

    void buttonEditorValidates()
    {
        ButtonDef d;
        d.label = QStringLiteral("Mine");
        d.left = QStringLiteral("Go ~");
        ButtonEditor e(d, w);
        QVERIFY(e.validationError().isEmpty());
        e.labelEdit()->clear();
        QVERIFY(e.validationError().contains(QStringLiteral("label")));
        e.labelEdit()->setText(QStringLiteral("Mine"));
        e.leftEdit()->setPlainText(QStringLiteral("Bogus thing"));
        QVERIFY(e.validationError().contains(QStringLiteral("Bogus")));
        e.leftEdit()->setPlainText(QStringLiteral("Go ~\nSet VIEW=list"));
        e.enableEdit()->setText(QStringLiteral("selection && sellection"));
        QVERIFY(e.validationError().contains(QStringLiteral("sellection")));
        e.enableEdit()->setText(QStringLiteral("selection && !remote"));
        QVERIFY(e.validationError().isEmpty());
        e.menuCheck()->setChecked(true);
        QVERIFY(e.validationError().contains(QStringLiteral("menu")));
        e.menuCheck()->setChecked(false);
        const ButtonDef out = e.button();
        QCOMPARE(out.left, QStringLiteral("Go ~\nSet VIEW=list"));
        QCOMPARE(out.enableWhen, QStringLiteral("selection && !remote"));
        // Round trip through JSON keeps everything.
        ButtonDef menu = out;
        menu.left.clear();
        menu.children = {out, ButtonDef::makeSeparator()};
        menu.key = QStringLiteral("Ctrl+Shift+M");
        QCOMPARE(ButtonDef::fromJson(menu.toJson()), menu);
    }

    void commandConsole()
    {
        w->showConsole();
        CommandConsole *c = w->console();
        QVERIFY(c);
        QVERIFY(c->submit(QStringLiteral("Set VIEW=list")));
        QCOMPARE(w->display(0)->currentTab()->viewMode(), FolderTab::ViewMode::List);
        QVERIFY(!c->submit(QStringLiteral("Set BOGUS=1"))); // no error box: it prints
        QVERIFY(c->output().contains(QStringLiteral("Set does not know BOGUS")));
        QVERIFY(c->submit(QStringLiteral("help Set")));
        QVERIFY(c->output().contains(QStringLiteral("GROUPBY=")));
        // Tab completes; Up brings back the last command.
        c->input()->setText(QStringLiteral("Set GROUPB"));
        QTest::keyClick(c->input(), Qt::Key_Tab);
        QCOMPARE(c->input()->text(), QStringLiteral("Set GROUPBY="));
        c->input()->clear();
        QTest::keyClick(c->input(), Qt::Key_Up);
        QCOMPARE(c->input()->text(), QStringLiteral("help Set"));
        c->close();
    }

    void pasteClipboardAsFile()
    {
        QImage img(40, 30, QImage::Format_RGB32);
        img.fill(Qt::red);
        QGuiApplication::clipboard()->setImage(img);
        QVERIFY(w->runCommand(QStringLiteral("Clipboard PASTE")));
        QStringList pngs = QDir(root + QStringLiteral("/a")).entryList({QStringLiteral("Pasted image *.png")});
        QCOMPARE(pngs.size(), 1);
        QImage back(root + QStringLiteral("/a/") + pngs.first());
        QCOMPARE(back.size(), QSize(40, 30));
        QCOMPARE(back.pixelColor(5, 5), QColor(Qt::red));
        // Pasting again never overwrites.
        QVERIFY(w->runCommand(QStringLiteral("Clipboard PASTE")));
        QVERIFY(QDir(root + QStringLiteral("/a")).entryList({QStringLiteral("Pasted image *.png")}).size() >= 1);

        QGuiApplication::clipboard()->setText(QStringLiteral("Deluxe Galaga high scores"));
        // AS asks for the name.
        QTimer::singleShot(100, []() {
            if (auto *d = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                d->setTextValue(QStringLiteral("scores.txt"));
                d->accept();
            }
        });
        QVERIFY(w->runCommand(QStringLiteral("Clipboard PASTE AS")));
        QFile f(root + QStringLiteral("/a/scores.txt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("Deluxe Galaga high scores"));
        QTRY_VERIFY(w->display(0)->currentTab()->selectedPaths().contains(root + QStringLiteral("/a/scores.txt")));
    }

    void printFolderListings()
    {
        QVERIFY(w->runCommand(QStringLiteral("Print FOLDER")));
        const QString text = QGuiApplication::clipboard()->text();
        QVERIFY(text.startsWith(root + QStringLiteral("/a\n")));
        QVERIFY(text.contains(QStringLiteral("Name")) && text.contains(QStringLiteral("Modified")));
        QVERIFY(text.contains(QStringLiteral("three.txt")));
        QVERIFY(text.trimmed().endsWith(QStringLiteral("0 folders, 3 files")));

        const QString csv = m_dir->filePath(QStringLiteral("list.csv"));
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=type\nPrint FOLDER AS=csv TO=\"%1\"").arg(csv)));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4);
        QVERIFY(lines.first().startsWith(QStringLiteral("Group,Name,Size,Type,Modified")));

        w->display(0)->currentTab()->selectPaths({root + QStringLiteral("/a/two.txt")});
        QVERIFY(w->runCommand(QStringLiteral("Print FOLDER AS=html SELECTED")));
        const QString html = QGuiApplication::clipboard()->text();
        QVERIFY(html.contains(QStringLiteral("<table>")) && html.contains(QStringLiteral("two.txt")));
        QVERIFY(!html.contains(QStringLiteral("one.txt")));
        QVERIFY(html.contains(QStringLiteral("class=\"group\"")));
        QVERIFY(!runFailing(QStringLiteral("Print FOLDER AS=pdf")));
    }

    void settingsBackupAndRestore()
    {
        ToolbarStore &store = m_app->toolbars();
        QVERIFY(m_app->aliases().setUserAlias(QStringLiteral("roms"), root + QStringLiteral("/b")));
        QVERIFY(m_app->aliases().save());
        const QString id = store.create(QStringLiteral("Emulation"));
        ButtonDef d;
        d.label = QStringLiteral("ROMs");
        d.left = QStringLiteral("Go /roms");
        store.insertButton(id, 0, d);
        QVERIFY(w->runCommand(QStringLiteral("Set THEME=Workbench")));
        const QString zip = m_dir->filePath(QStringLiteral("backup.zip"));
        QVERIFY(w->runCommand(QStringLiteral("Settings BACKUP=\"%1\"").arg(zip)));
        QVERIFY(QFile::exists(zip));

        // Change everything, then restore.
        QVERIFY(m_app->aliases().removeUserAlias(QStringLiteral("roms")));
        QVERIFY(m_app->aliases().save());
        QVERIFY(store.remove(id));
        QVERIFY(w->runCommand(QStringLiteral("Set THEME=\"Linopus Dark\"")));
        QVERIFY(w->runCommand(QStringLiteral("Settings RESTORE=\"%1\" QUIET").arg(zip)));
        QCOMPARE(m_app->aliases().aliasPath(QStringLiteral("roms")), root + QStringLiteral("/b"));
        QVERIFY(store.has(id));
        QCOMPARE(store.toolbar(id).buttons.first().label, QStringLiteral("ROMs"));
        QVERIFY(w->buttonBar(id));
        QCOMPARE(Style::instance()->theme().name, QStringLiteral("Workbench"));

        // Not a backup: refused, nothing written.
        const QString other = m_dir->filePath(QStringLiteral("other.zip"));
        QVERIFY(Zip::write(other, {Zip::Entry{QStringLiteral("evil/../../x"), QByteArray("x")}}));
        QVERIFY(!runFailing(QStringLiteral("Settings RESTORE=\"%1\" QUIET").arg(other)));
    }

    void textAndIconSize()
    {
        const qreal before = QApplication::font().pointSizeF();
        FolderTab *t = w->display(0)->currentTab();
        QVERIFY(w->runCommand(QStringLiteral("Set SIZE=larger")));
        QVERIFY(QApplication::font().pointSizeF() > before * 1.25);
        QCOMPARE(t->detailsView()->iconSize(), QSize(21, 21));
        QCOMPARE(w->buttonBar(QStringLiteral("buttons"))->iconSize(), QSize(23, 23));
        QCOMPARE(m_app->settings().value(QStringLiteral("Appearance/size")).toString(), QStringLiteral("larger"));
        QVERIFY(w->runCommand(QStringLiteral("Set SIZE=smaller")));
        QCOMPARE(Style::instance()->sizeLevel(), 1);
        QVERIFY(w->runCommand(QStringLiteral("Set SIZE=normal")));
        QCOMPARE(QApplication::font().pointSizeF(), before);
        QVERIFY(!runFailing(QStringLiteral("Set SIZE=huge")));
    }

    void advancedRenameScriptedAndUndo()
    {
        FolderTab *t = w->display(0)->currentTab();
        t->selectAll();
        QVERIFY(w->runCommand(QStringLiteral("Rename OLDNAME=*.txt NEWNAME=\"Note [##].txt\"")));
        QTRY_VERIFY(QFile::exists(root + QStringLiteral("/a/Note 01.txt")));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/Note 03.txt")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/a/one.txt")));
        // The renamed items stay selected.
        QTRY_COMPARE(t->selectedPaths().size(), 3);
        QVERIFY(w->runCommand(QStringLiteral("Rename UNDO")));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/one.txt")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/a/Note 01.txt")));
        QVERIFY(!runFailing(QStringLiteral("Rename UNDO"))); // only once

        // A clash renames nothing and says why.
        t->selectAll();
        QString err;
        QVERIFY(!w->runCommandReporting(QStringLiteral("Rename OLDNAME=*.txt NEWNAME=same.txt"), &err));
        QVERIFY(err.contains(QStringLiteral("Another item gets this name")));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/one.txt")));
        // A preset.
        RenameOptions up;
        up.nameCase = RenameOptions::Case::Upper;
        QVERIFY(RenamePresets::save(QStringLiteral("Shout"), up));
        t->selectPaths({root + QStringLiteral("/a/two.txt")});
        QTRY_COMPARE(t->selectedPaths().size(), 1);
        QVERIFY(w->runCommand(QStringLiteral("Rename PRESET=Shout")));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/TWO.txt")));
    }

    void advancedRenameDialog()
    {
        const QStringList paths{root + QStringLiteral("/a/one.txt"), root + QStringLiteral("/a/three.txt"), root + QStringLiteral("/a/two.txt")};
        RenameOptions o;
        AdvancedRenameDialog dlg(paths, o, w);
        // Nothing to change yet.
        QVERIFY(!dlg.renameButton()->isEnabled());
        dlg.oldEdit()->setText(QStringLiteral("t*.txt"));
        dlg.newEdit()->setText(QStringLiteral("Disk ?*.d64"));
        dlg.refreshNow();
        QCOMPARE(dlg.previewTree()->topLevelItemCount(), 3);
        QCOMPARE(dlg.previewTree()->topLevelItem(0)->text(1), QStringLiteral("one.txt")); // no match
        QCOMPARE(dlg.previewTree()->topLevelItem(1)->text(1), QStringLiteral("Disk hree.d64"));
        QVERIFY(dlg.renameButton()->isEnabled());
        // Switch to find and replace with a clash: Rename greys out.
        dlg.modeBox()->setCurrentIndex(1);
        dlg.oldEdit()->setText(QStringLiteral("o"));
        dlg.newEdit()->setText(QStringLiteral("x"));
        dlg.refreshNow();
        QCOMPARE(dlg.previewTree()->topLevelItem(0)->text(1), QStringLiteral("xne.txt"));
        dlg.modeBox()->setCurrentIndex(2);
        dlg.oldEdit()->setText(QStringLiteral(".*"));
        dlg.newEdit()->setText(QStringLiteral("all"));
        dlg.refreshNow();
        QVERIFY(!dlg.renameButton()->isEnabled());
        // A real rename through the dialog.
        dlg.modeBox()->setCurrentIndex(0);
        dlg.oldEdit()->setText(QStringLiteral("*.*"));
        dlg.newEdit()->setText(QStringLiteral("[parent] [#] *.*"));
        dlg.refreshNow();
        QCOMPARE(dlg.previewTree()->topLevelItem(2)->text(1), QStringLiteral("a 3 two.txt"));
        dlg.accept();
        QCOMPARE(dlg.result(), int(QDialog::Accepted));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/a 1 one.txt")));
        QCOMPARE(dlg.done().size(), 3);
    }

    void synchronizeDialog()
    {
        // b has keep.txt only; a has three files. Update the right.
        SyncOptions o;
        SyncDialog dlg(m_app->engine(), root + QStringLiteral("/a"), root + QStringLiteral("/b"), o, w);
        dlg.startCompare();
        QTRY_VERIFY(!dlg.isComparing());
        QCOMPARE(dlg.entries().size(), 4);
        QCOMPARE(dlg.tree()->topLevelItemCount(), 4);
        QVERIFY(dlg.summary().contains(QStringLiteral("3 to copy")));
        QVERIFY(dlg.syncButton()->isEnabled());
        // keep.txt is only on the right: Update leaves it; turn it into a copy back.
        int keep = -1;
        for (int i = 0; i < dlg.entries().size(); ++i) {
            if (dlg.entries().at(i).rel == QLatin1String("keep.txt"))
                keep = i;
        }
        QVERIFY(keep >= 0);
        QCOMPARE(dlg.entries().at(keep).action, SyncEntry::Action::None);
        dlg.setAction(keep, SyncEntry::Action::CopyToRight); // not possible: it has no left side
        QCOMPARE(dlg.entries().at(keep).action, SyncEntry::Action::None);
        dlg.setAction(keep, SyncEntry::Action::CopyToLeft);
        QVERIFY(dlg.synchronize() > 0);
        m_app->jobs().waitForIdle(5000);
        QVERIFY(QFile::exists(root + QStringLiteral("/b/one.txt")));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/keep.txt")));
        // Compare again: everything matches.
        dlg.startCompare();
        QTRY_VERIFY(!dlg.isComparing());
        QVERIFY(dlg.summary().startsWith(QStringLiteral("The folders match")));
        QVERIFY(!dlg.syncButton()->isEnabled());
        dlg.showSameBox()->setChecked(true);
        QCOMPARE(dlg.tree()->topLevelItemCount(), 4);
    }

    void synchronizeCommand()
    {
        QVERIFY(runFailing(QStringLiteral("Sync MODE=sideways")) == false);
        // Mirror from the source (a) to the destination (b) without a dialog.
        touch(root + QStringLiteral("/b/extra.txt"));
        QVERIFY(w->runCommand(QStringLiteral("Sync MODE=mirror RUN")));
        m_app->jobs().waitForIdle(5000);
        QVERIFY(QFile::exists(root + QStringLiteral("/b/two.txt")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/b/extra.txt"))); // to the trash
        QVERIFY(!QFile::exists(root + QStringLiteral("/b/keep.txt")));
    }

    void duplicatesDialog()
    {
        const QByteArray data(3000, 'r');
        auto put = [this, &data](const QString &rel) {
            const QString p = root + QLatin1Char('/') + rel;
            QDir().mkpath(QFileInfo(p).absolutePath());
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        put(QStringLiteral("a/game.d64"));
        put(QStringLiteral("b/game copy.d64"));
        put(QStringLiteral("b/old/game.d64"));
        QFile old(root + QStringLiteral("/b/old/game.d64"));
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTime().addDays(-100), QFileDevice::FileModificationTime));
        old.close();
        DuplicateOptions o;
        o.minSize = 2048; // the one-byte test files are duplicates of each other too
        DuplicatesDialog dlg(m_app->engine(), {root}, o, w);
        dlg.startSearch();
        QTRY_VERIFY(!dlg.isSearching());
        QCOMPARE(dlg.groups().size(), 1);
        QCOMPARE(dlg.groups().first().paths.size(), 3);
        dlg.checkAllBut(DuplicatesDialog::Keep::Oldest);
        QStringList checked = dlg.checkedPaths();
        QCOMPARE(checked.size(), 2);
        QVERIFY(!checked.contains(root + QStringLiteral("/b/old/game.d64")));
        // Checking the last copy is refused.
        QTreeWidgetItem *group = dlg.tree()->topLevelItem(0);
        for (int i = 0; i < group->childCount(); ++i)
            group->child(i)->setCheckState(0, Qt::Checked);
        QCOMPARE(dlg.checkedPaths().size(), 2);
        QCOMPARE(dlg.trashChecked(), 2);
        m_app->jobs().waitForIdle(5000);
        QVERIFY(QFile::exists(root + QStringLiteral("/b/old/game.d64")));
        QVERIFY(!QFile::exists(root + QStringLiteral("/a/game.d64")));
        QCOMPARE(dlg.groups().size(), 0); // one left: no longer a duplicate
        QVERIFY(dlg.summary().contains(QStringLiteral("No duplicates")));
    }

    void collectionsInTheLister()
    {
        FolderTab *t = w->display(0)->currentTab();
        t->selectPaths({root + QStringLiteral("/a/one.txt"), root + QStringLiteral("/a/two.txt")});
        QTRY_COMPARE(t->selectedPaths().size(), 2);
        QVERIFY(w->runCommand(QStringLiteral("Collection ADD=Picks")));
        QVERIFY(w->runCommand(QStringLiteral("Go /collections")));
        QTRY_COMPARE(t->path(), QStringLiteral("coll:/"));
        waitListed(t, 1);
        QVERIFY(w->runCommand(QStringLiteral("Collection OPEN=Picks")));
        QTRY_COMPARE(t->path(), Collections::location(QStringLiteral("Picks")));
        waitListed(t, 2);
        QVERIFY(t->isColumnVisible(FileListModel::LocationColumn));
        QCOMPARE(t->title(), QStringLiteral("Picks"));

        // Copy out of a collection copies the real files.
        t->selectPaths({root + QStringLiteral("/a/one.txt")});
        QTRY_COMPARE(t->selectedPaths().size(), 1);
        QVERIFY(w->runCommand(QStringLiteral("Copy")));
        m_app->jobs().waitForIdle(5000);
        QTRY_VERIFY(QFile::exists(root + QStringLiteral("/b/one.txt")));

        // Delete only takes it out of the collection.
        QVERIFY(w->runCommand(QStringLiteral("Delete")));
        waitListed(t, 1);
        QVERIFY(QFile::exists(root + QStringLiteral("/a/one.txt")));

        // Renaming in a collection renames the real file and keeps the link.
        QVERIFY(w->runCommand(QStringLiteral("Rename FROM=\"%1\" TO=deux.txt").arg(root + QStringLiteral("/a/two.txt"))));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/deux.txt")));
        QCOMPARE(Collections::instance()->items(QStringLiteral("Picks")), QStringList{root + QStringLiteral("/a/deux.txt")});
        waitListed(t, 1);

        // Copy into a collection (the destination showing one) adds links.
        w->display(1)->currentTab()->navigate(Collections::location(QStringLiteral("Picks")));
        QVERIFY(w->runCommand(QStringLiteral("Go \"%1\"").arg(root + QStringLiteral("/a"))));
        waitListed(t, 3);
        t->selectPaths({root + QStringLiteral("/a/three.txt")});
        QTRY_COMPARE(t->selectedPaths().size(), 1);
        QVERIFY(w->runCommand(QStringLiteral("Copy")));
        QCOMPARE(Collections::instance()->items(QStringLiteral("Picks")).size(), 2);
        QCOMPARE(QDir(root + QStringLiteral("/a")).entryList(QDir::Files).size(), 3); // nothing copied

        // New collection from the list of collections; delete one.
        t->navigate(QStringLiteral("coll:/"));
        QVERIFY(w->runCommand(QStringLiteral("CreateFolder NAME=\"Amiga, C64\"")));
        QCOMPARE(Collections::instance()->names().size(), 3);
        QVERIFY(w->runCommand(QStringLiteral("Collection DELETE=Amiga")));
        QCOMPARE(Collections::instance()->names().size(), 2);
        Collections::instance()->remove(QStringLiteral("C64"));
        Collections::instance()->remove(QStringLiteral("Picks"));
    }

    void findShowsResultsAsACollection()
    {
        QVERIFY(w->runCommand(QStringLiteral("Find NAME=t*.txt RUN")));
        FileDisplay *d = w->display(0);
        QTRY_COMPARE(d->currentTab()->path(), Collections::location(QStringLiteral("Find Results")));
        waitListed(d->currentTab(), 2); // two.txt, three.txt
        const int tabs = d->tabCount();
        // A second search reuses the results tab.
        QVERIFY(w->runCommand(QStringLiteral("Find NAME=one RUN")));
        QTRY_COMPARE(Collections::instance()->items(QStringLiteral("Find Results")).size(), 1);
        waitListed(d->currentTab(), 1);
        QCOMPARE(d->tabCount(), tabs);
        QVERIFY(!runFailing(QStringLiteral("Find RUN"))); // nothing to look for
        QVERIFY(!runFailing(QStringLiteral("Find NAME=x MINSIZE=lots")));

        // The dialog.
        FindDialog dlg({root}, FindCriteria(), QStringLiteral("Mine"), w);
        dlg.nameEdit()->setText(QStringLiteral("keep"));
        dlg.startSearch();
        QTRY_VERIFY(!dlg.isSearching());
        QCOMPARE(dlg.results(), QStringList{root + QStringLiteral("/b/keep.txt")});
        QCOMPARE(Collections::instance()->items(QStringLiteral("Mine")), dlg.results());
        QVERIFY(dlg.status().startsWith(QStringLiteral("1 found")));
        Collections::instance()->remove(QStringLiteral("Mine"));
        Collections::instance()->remove(QStringLiteral("Find Results"));
    }

    void favouritesAndFrequentFolders()
    {
        Favorites *f = Favorites::instance();
        QVERIFY(f->items().isEmpty());
        QVERIFY(w->runCommand(QStringLiteral("Favorite ADD")));
        QVERIFY(w->runCommand(QStringLiteral("Favorite ADD PATH=\"%1\" NAME=Box GROUP=Mine").arg(root + QStringLiteral("/b"))));
        QCOMPARE(f->items().size(), 2);
        QCOMPARE(f->items().at(0).path, root + QStringLiteral("/a"));
        QVERIFY(f->items().at(1).isGroup());
        QCOMPARE(f->pathFor(QStringLiteral("box")), root + QStringLiteral("/b"));
        QVERIFY(w->evaluateCondition(QStringLiteral("favorite"), false));
        QVERIFY(w->runCommand(QStringLiteral("Favorite GO=Box")));
        QCOMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/b"));
        QVERIFY(!runFailing(QStringLiteral("Favorite GO=Nope")));

        // A self-filling menu button shows them.
        ButtonDef d;
        d.label = QStringLiteral("Favs");
        d.dynamic = QStringLiteral("favorites");
        m_app->toolbars().insertButton(QStringLiteral("buttons"), 0, d);
        ButtonBar *bar = w->buttonBar(QStringLiteral("buttons"));
        QTRY_VERIFY(bar->buttonAt(0) && bar->buttonAt(0)->menu());
        QMenu *menu = bar->buttonAt(0)->menu();
        Q_EMIT menu->aboutToShow();
        QStringList texts;
        for (QAction *a : menu->actions())
            texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("a")));
        QVERIFY(texts.contains(QStringLiteral("Mine")));
        QAction *goA = menu->actions().first();
        goA->trigger();
        QCOMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/a"));

        // Organise: what the dialog holds is what gets saved.
        FavoritesDialog dlg(w);
        QCOMPARE(dlg.favorites(), f->items());
        delete dlg.tree()->topLevelItem(0);
        dlg.accept();
        QCOMPARE(f->items().size(), 1);
        QVERIFY(w->runCommand(QStringLiteral("Favorite REMOVE PATH=\"%1\"").arg(root + QStringLiteral("/b"))));
        QVERIFY(f->items().first().items.isEmpty());

        // Frequent folders: b visited more than a.
        f->clearFrequent();
        FolderTab *t = w->display(0)->currentTab();
        for (int i = 0; i < 3; ++i) {
            t->navigate(root + QStringLiteral("/b"));
            t->navigate(root);
        }
        t->navigate(root + QStringLiteral("/a"));
        const QList<FrequentFolder> top = f->frequent();
        QCOMPARE(top.first().path, root);
        QCOMPARE(top.at(1).path, root + QStringLiteral("/b"));
        QCOMPARE(top.at(1).visits, 3);
        QMenu freq;
        w->fillDynamicMenu(&freq, QStringLiteral("frequent"));
        QVERIFY(freq.actions().size() >= 4); // three folders, separator, clear
        f->setItems({});
        f->clearFrequent();
    }

    void preferencesWindow()
    {
        Style::instance()->setTheme(QStringLiteral("Linopus Light"));
        {
            PreferencesDialog dlg(*m_app, w, QStringLiteral("theme"));
            QCOMPARE(dlg.currentPage(), QStringLiteral("theme"));
            QVERIFY(dlg.search(QStringLiteral("alias")).contains(QStringLiteral("aliases")));
            QCOMPARE(dlg.search(QStringLiteral("bold")), QStringList{QStringLiteral("labels")});
            QCOMPARE(dlg.currentPage(), QStringLiteral("labels")); // lands on the match
            QCOMPARE(dlg.search(QString()).size(), PreferencesDialog::pageNames().size());

            // Colours show at once; Cancel puts the old theme back.
            dlg.setThemeColor(QStringLiteral("sourceAccent"), QColor(Qt::red));
            QCOMPARE(Style::instance()->theme().sourceAccent, QColor(Qt::red));
            dlg.reject();
        }
        QCOMPARE(Style::instance()->theme().name, QStringLiteral("Linopus Light"));
        QVERIFY(Style::instance()->theme().sourceAccent != QColor(Qt::red));

        {
            // Edit a built-in: it is saved as your own copy.
            PreferencesDialog dlg(*m_app, w, QStringLiteral("theme"));
            dlg.setThemeColor(QStringLiteral("type:image"), QColor(Qt::green));
            QVERIFY(dlg.saveTheme());
            QCOMPARE(Style::instance()->theme().name, QStringLiteral("Linopus Light (mine)"));
            QVERIFY(Style::instance()->themeNames().contains(QStringLiteral("Linopus Light (mine)")));
            QCOMPARE(Style::instance()->theme().typeColor(FileKind::Category::Image), QColor(Qt::green));
            QCOMPARE(m_app->settings().value(QStringLiteral("Appearance/theme")).toString(), QStringLiteral("Linopus Light (mine)"));

            // A new name for a fresh one.
            dlg.setThemeName(QStringLiteral("Deck Night"));
            dlg.setThemeColor(QStringLiteral("base"), QColor::fromRgb(0x101010));
            QVERIFY(dlg.saveTheme());
            QVERIFY(QFile::exists(Style::instance()->themesDir() + QStringLiteral("/Deck Night.ini")));

            // Labels and aliases apply with OK.
            LabelRule r;
            r.pattern = QStringLiteral("*.sav");
            r.color = QColor(Qt::magenta);
            r.bold = true;
            dlg.addLabelRule(r);
            const int row = dlg.addAliasRow();
            dlg.setAliasRow(row, QStringLiteral("/saves"), root + QStringLiteral("/b"));
            dlg.accept();
            QCOMPARE(dlg.result(), int(QDialog::Accepted));
        }
        QVERIFY(Style::instance()->labels().match(QStringLiteral("game.sav"), false));
        QCOMPARE(m_app->aliases().aliasPath(QStringLiteral("saves")), root + QStringLiteral("/b"));

        {
            // A bad alias stops OK and shows the page.
            PreferencesDialog dlg(*m_app, w);
            const int row = dlg.addAliasRow();
            dlg.setAliasRow(row, QStringLiteral("usr"), QStringLiteral("/tmp"));
            QString err;
            QVERIFY(!dlg.apply(&err));
            QVERIFY(err.contains(QStringLiteral("usr")));
            QCOMPARE(dlg.currentPage(), QStringLiteral("aliases"));
            dlg.reject();
        }
        Style::instance()->setLabels({});
    }

    void viewerPreviews()
    {
        const QString d = root + QStringLiteral("/v");
        QDir().mkpath(d);
        QImage img(300, 200, QImage::Format_RGB32);
        img.fill(Qt::blue);
        QVERIFY(img.save(d + QStringLiteral("/shot.png")));
        auto put = [](const QString &p, const QByteArray &b) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(b);
        };
        put(d + QStringLiteral("/readme.md"), "# Deluxe Galaga\nShoot everything.");
        put(d + QStringLiteral("/release.nfo"), QByteArray("\xC9\xCD\xCD\xBB\r\n\xBA hi \xBA\r\n\xC8\xCD\xCD\xBC\x1A garbage"));
        put(d + QStringLiteral("/kick13.rom"), QByteArray("\x11\x14\x4E\xF9\x00\xFC\x00\xD2", 8) + QByteArray(100, '\0'));
        QVERIFY(Zip::write(d + QStringLiteral("/pack.zip"), {Zip::Entry{QStringLiteral("c64/Galaga.d64"), QByteArray(10, 'g')}}));

        Preview p = Preview::load(d + QStringLiteral("/shot.png"));
        QCOMPARE(p.kind, Preview::Kind::Image);
        QCOMPARE(p.image.size(), QSize(300, 200));
        QVERIFY(p.info.contains(QStringLiteral("300 x 200")));
        QCOMPARE(Preview::load(d + QStringLiteral("/shot.png"), 100).image.width(), 100); // scaled while loading
        p = Preview::load(d + QStringLiteral("/readme.md"));
        QCOMPARE(p.kind, Preview::Kind::Text);
        QVERIFY(p.text.startsWith(QStringLiteral("# Deluxe Galaga")));
        p = Preview::load(d + QStringLiteral("/release.nfo"));
        QCOMPARE(p.kind, Preview::Kind::Text);
        QVERIFY(p.text.startsWith(QStringLiteral("╔══╗")));
        QVERIFY(!p.text.contains(QStringLiteral("garbage"))); // stops at the DOS end-of-file mark
        p = Preview::load(d + QStringLiteral("/kick13.rom"));
        QCOMPARE(p.kind, Preview::Kind::Hex);
        QVERIFY(p.text.startsWith(QStringLiteral("00000000  11 14 4e f9")));
        p = Preview::load(d + QStringLiteral("/pack.zip"));
        QCOMPARE(p.kind, Preview::Kind::Archive);
        QCOMPARE(p.entries.first().section(QLatin1Char('\t'), 0, 0), QStringLiteral("c64/Galaga.d64"));
        p = Preview::load(d);
        QCOMPARE(p.kind, Preview::Kind::Folder);
        QVERIFY(p.text.contains(QStringLiteral("5 files")));
        QCOMPARE(Preview::load(d + QStringLiteral("/missing")).kind, Preview::Kind::None);
        QCOMPARE(Preview::load(QStringLiteral("sftp://host/x.txt")).kind, Preview::Kind::Info);

        // In the lister: F3's command, following the focus.
        QVERIFY(w->runCommand(QStringLiteral("Set VIEWER=on")));
        ViewerPane *v = w->viewer();
        QVERIFY(v->isVisible());
        QVERIFY(w->evaluateCondition(QStringLiteral("viewer"), false));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(d);
        waitListed(t, 5);
        t->selectPaths({d + QStringLiteral("/readme.md")});
        QTRY_COMPARE(v->path(), d + QStringLiteral("/readme.md"));
        QTRY_VERIFY(v->textShown().contains(QStringLiteral("Shoot everything")));
        t->selectPaths({d + QStringLiteral("/shot.png")});
        QTRY_COMPARE(v->kind(), Preview::Kind::Image);
        QVERIFY(w->runCommand(QStringLiteral("Set VIEWERPOS=bottom")));
        QCOMPARE(w->dockWidgetArea(v), Qt::BottomDockWidgetArea);
        QVERIFY(w->runCommand(QStringLiteral("Set VIEWER=off")));
        QVERIFY(!v->isVisible());
    }

    void savedLayouts()
    {
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=horizontal\nSet TREE=shared\nSet VIEWER=on")));
        QVERIFY(w->runCommand(QStringLiteral("Layout SAVE=Sorting")));
        QVERIFY(w->runCommand(QStringLiteral("Layout SAVE=\"Here too\" FOLDERS")));
        QCOMPARE(m_app->layoutNames(), (QStringList{QStringLiteral("Here too"), QStringLiteral("Sorting")}));

        // Rearrange and move on, then load: the arrangement comes back, the folder stays.
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=vertical\nSet TREE=off\nSet VIEWER=off")));
        w->display(0)->currentTab()->navigate(root);
        QVERIFY(w->runCommand(QStringLiteral("Layout LOAD=Sorting")));
        QApplication::processEvents();
        ListerWindow *loaded = m_app->activeLister();
        QVERIFY(loaded && loaded != w);
        w = loaded;
        QTRY_VERIFY(w->isVisible());
        QVERIFY(w->isDual());
        QVERIFY(!w->display(0)->isHidden());
        QCOMPARE(w->treeMode(), ListerWindow::TreeMode::Shared);
        QVERIFY(w->viewer()->isVisible());
        QCOMPARE(w->display(0)->currentTab()->path(), root);
        QCOMPARE(m_app->listerCount(), 1); // replaced, not added

        // With folders: back to where it was saved.
        QVERIFY(w->runCommand(QStringLiteral("Layout LOAD=\"Here too\"")));
        QApplication::processEvents();
        w = m_app->activeLister();
        QCOMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/a"));
        QVERIFY(w->runCommand(QStringLiteral("Layout DELETE=Sorting")));
        QCOMPARE(m_app->layoutNames(), QStringList{QStringLiteral("Here too")});
        QVERIFY(!runFailing(QStringLiteral("Layout LOAD=Sorting")));
    }

    void sessionRoundTrip()
    {
        w->runCommand(QStringLiteral("Set DUAL=horizontal\nSet VIEW=list"));
        w->display(1)->addTab(root);
        m_app->saveSession({w});
        w->close();
        w = nullptr;
        QApplication::processEvents();

        m_app->restoreSession();
        auto *restored = m_app->activeLister();
        QVERIFY(restored);
        w = restored;
        QVERIFY(w->isDual());
        QCOMPARE(w->display(1)->tabCount(), 2);
        QCOMPARE(w->display(0)->currentTab()->viewMode(), FolderTab::ViewMode::List);
        QCOMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/a"));
    }

private:
    // A failing command shows an error box; close it and report the result.
    bool runFailing(const QString &command)
    {
        QTimer closer;
        connect(&closer, &QTimer::timeout, []() {
            if (QWidget *m = QApplication::activeModalWidget())
                m->close();
        });
        closer.start(50);
        return w->runCommand(command);
    }

    static QStringList labels(FolderTab *t)
    {
        QStringList out;
        QAbstractItemModel *m = t->view()->model();
        for (int r = 0; r < m->rowCount(); ++r) {
            const QString l = m->index(r, 0).data(FileListModel::GroupLabelRole).toString();
            if (out.isEmpty() || out.last() != l)
                out << l;
        }
        return out;
    }

    static void waitListed(FolderTab *t, int expectedRows)
    {
        QTRY_VERIFY_WITH_TIMEOUT(t->view()->model()->rowCount() == expectedRows, 5000);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<AppContext> m_app;
    ListerWindow *w = nullptr;
    QString root;
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
