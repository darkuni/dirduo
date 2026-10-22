#include "core/AliasManager.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "ui/AppContext.h"
#include "ui/BreadcrumbBar.h"
#include "ui/ButtonBar.h"
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
#include <QFileSystemModel>
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
        auto *fs = qobject_cast<QFileSystemModel *>(tree->model());
        tree->setCurrentIndex(fs->index(root + QStringLiteral("/b")));
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
