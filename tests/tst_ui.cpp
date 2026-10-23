#include "core/AliasManager.h"
#include "core/Archive.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "ui/AppContext.h"
#include "ui/BreadcrumbBar.h"
#include "ui/ButtonBar.h"
#include "ui/AdvancedRenameDialog.h"
#include "ui/ButtonEditor.h"
#include "ui/DuplicatesDialog.h"
#include "ui/ImageConvertDialog.h"
#include "ui/HistoryDialog.h"
#include "ui/SelectDialog.h"
#include "ui/Launch.h"
#include "ui/MetadataPane.h"
#include "core/Metadata.h"
#include <QtConcurrent>
#include <QImageReader>
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
#include "core/Location.h"
#include "ui/ArchiveDialog.h"
#include "ui/TagsDialog.h"
#include "core/Condition.h"
#include "ui/ChecksumDialog.h"
#include "core/Checksum.h"
#include <QTreeWidget>
#include "core/FileTags.h"
#include "core/Prefs.h"
#include "ui/ImageViewer.h"
#include "ui/OpenWithDialog.h"
#include "core/Apps.h"
#include "core/HostRun.h"
#include "ui/DrivesBar.h"
#include "ui/ServerDialog.h"
#include "core/Servers.h"
#include "ui/PropertiesDialog.h"
#include "ui/ListerWindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QClipboard>
#include <QIcon>
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
#include <QWheelEvent>
#include <QSignalSpy>
#include <QMessageBox>
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

    void toolbarsShareRowsInTheMiddle()
    {
        ToolbarStore &store = m_app->toolbars();
        const QString id = store.create(QStringLiteral("Work"));
        QTRY_VERIFY(w->buttonBar(id));
        ButtonBar *work = w->buttonBar(id);
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=horizontal")));
        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=drives PLACE=between\nToolbar NAME=work PLACE=between SAMEROW")));
        ToolbarStrip *strip = w->toolbarStrip();
        int line = -1;
        for (int i = 0; i < strip->lines().size(); ++i) {
            if (strip->lines().at(i).contains(work))
                line = i;
        }
        QVERIFY(line >= 0);
        QVERIFY(strip->lines().at(line).contains(w->drivesBar())); // one row, drives then Work
        QVERIFY(strip->isJoined(work));
        QCOMPARE(work->orientation(), Qt::Horizontal);
        // Its own row again, then back by moving it there.
        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=work NEWROW")));
        QVERIFY(!strip->isJoined(work));
        const int drivesLine = [&] {
            for (int i = 0; i < strip->lines().size(); ++i)
                if (strip->lines().at(i).contains(w->drivesBar()))
                    return i;
            return -1;
        }();
        strip->placeBar(work, drivesLine, 0, false);
        QCOMPARE(strip->lines().at(drivesLine).first(), work); // before the drives now
        strip->moveBar(work, 1);
        QCOMPARE(strip->lines().at(drivesLine).last(), work);
        // A spacer makes the bar take the rest of the row.
        store.setButtons(id, {ButtonDef::makeSpacer(), ButtonDef::makeField(QStringLiteral("go"))});
        QTRY_VERIFY(work->hasSpacer());
        QVERIFY(work->fieldAt(1));
        // Saved with the session.
        m_app->saveSession({w});
        w->close();
        w = nullptr;
        QApplication::processEvents();
        m_app->restoreSession();
        w = m_app->activeLister();
        QVERIFY(w);
        work = w->buttonBar(id);
        QVERIFY(work);
        QVERIFY(w->toolbarStrip()->isJoined(work));
        // The Go To box goes there on Enter.
        QLineEdit *go = work->fieldAt(1);
        QVERIFY(go);
        go->setText(root + QStringLiteral("/b"));
        QTest::keyClick(go, Qt::Key_Return);
        QTRY_COMPARE(w->sourceTabForTesting()->path(), root + QStringLiteral("/b"));
        QVERIFY(store.remove(id));
        QTRY_VERIFY(!w->buttonBar(id));
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=vertical")));
    }

    void splitButtonsFieldsAndStyles()
    {
        ToolbarStore &store = m_app->toolbars();
        const QString id = store.create(QStringLiteral("Pieces"));
        QTRY_VERIFY(w->buttonBar(id));
        ButtonBar *bar = w->buttonBar(id);
        ButtonDef split;
        split.label = QStringLiteral("Lists");
        split.icon = QStringLiteral("view-list-details");
        split.left = QStringLiteral("Set VIEW=list");
        split.split = true;
        ButtonDef icons;
        icons.label = QStringLiteral("Icons");
        icons.left = QStringLiteral("Set VIEW=icons");
        split.children = {icons};
        // Round trip through JSON keeps all of it.
        const ButtonDef back = ButtonDef::fromJson(split.toJson());
        QVERIFY(back.isSplit());
        QCOMPARE(back.left, split.left);
        QCOMPARE(back.children.size(), 1);
        QVERIFY(ButtonDef::fromJson(ButtonDef::makeSpacer().toJson()).spacer);
        QCOMPARE(ButtonDef::fromJson(ButtonDef::makeField(QStringLiteral("find")).toJson()).field, QStringLiteral("find"));
        store.setButtons(id, {split, ButtonDef::makeField(QStringLiteral("filter"))});
        QTRY_VERIFY(bar->buttonAt(0));
        ThreeButton *b = bar->buttonAt(0);
        QCOMPARE(b->popupMode(), QToolButton::MenuButtonPopup);
        // A click on the button itself runs it; the menu has the rest.
        QSignalSpy ran(bar, &ButtonBar::commandTriggered);
        QTest::mouseClick(b, Qt::LeftButton, {}, QPoint(4, b->height() / 2));
        QCOMPARE(ran.size(), 1);
        QCOMPARE(ran.first().first().toString(), QStringLiteral("Set VIEW=list"));
        QTRY_COMPARE(w->sourceTabForTesting()->viewMode(), FolderTab::ViewMode::List);
        b->menu()->actions().first()->trigger();
        QCOMPARE(ran.size(), 2);
        // The filter box filters the source as you type.
        QLineEdit *filter = bar->fieldAt(1);
        QVERIFY(filter);
        QTest::keyClicks(filter, QStringLiteral("*.txt"));
        QTRY_COMPARE(w->sourceTabForTesting()->filterText(), QStringLiteral("*.txt"));
        filter->clear();
        Q_EMIT filter->textEdited(QString());
        QTRY_COMPARE(w->sourceTabForTesting()->filterText(), QString());
        // Style: icons only, bigger icons.
        store.setStyle(id, QStringLiteral("icons"), 32);
        // (Icons only; a button with no icon here keeps its label.)
        QTRY_VERIFY(bar->buttonAt(0)
                    && bar->buttonAt(0)->toolButtonStyle()
                        == (bar->buttonAt(0)->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly));
        store.setStyle(id, QStringLiteral("text"), 32);
        QTRY_VERIFY(bar->buttonAt(0) && bar->buttonAt(0)->toolButtonStyle() == Qt::ToolButtonTextOnly);
        store.setStyle(id, QStringLiteral("icons"), 32);
        QCOMPARE(bar->iconSize().width() >= 32, true);
        QCOMPARE(store.toolbar(id).style, QStringLiteral("icons"));
        QString name;
        bool ok = false;
        ToolbarDef read;
        ToolbarStore::loadFile(store.fileFor(id), &name, &ok, &read);
        QVERIFY(ok);
        QCOMPARE(read.style, QStringLiteral("icons"));
        QCOMPARE(read.iconSize, 32);
        // In the menu bar.
        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=pieces PLACE=menubar")));
        QCOMPARE(w->toolbarPlace(bar), ListerWindow::ToolbarPlace::MenuBar);
        QVERIFY(w->menuBarCorner()->isVisibleTo(w));
        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=pieces PLACE=top")));
        QVERIFY(!w->menuBarCorner()->isVisibleTo(w));
        QVERIFY(store.remove(id));
        QTRY_VERIFY(!w->buttonBar(id));
    }

    void defaultToolbarsAndOpusArrangement()
    {
        ToolbarStore &store = m_app->toolbars();
        // Every ready-made button runs a real command.
        for (const ToolbarDef &p : ToolbarStore::presets()) {
            QVERIFY2(!p.buttons.isEmpty(), qPrintable(p.id));
            std::function<void(const QList<ButtonDef> &)> check = [&](const QList<ButtonDef> &list) {
                for (const ButtonDef &b : list) {
                    if (b.isItem() && b.field != QLatin1String("command"))
                        continue;
                    if (!b.left.isEmpty()) {
                        const QString first = b.left.section(QLatin1Char(' '), 0, 0);
                        QVERIFY2(CommandCatalog::find(first), qPrintable(p.id + QStringLiteral(": ") + b.left));
                    }
                    QString err;
                    QVERIFY2(Condition::check(b.enableWhen, CommandCatalog::conditionNames(), &err), qPrintable(b.enableWhen + err));
                    QVERIFY2(Condition::check(b.checkWhen, CommandCatalog::conditionNames(), &err), qPrintable(b.checkWhen + err));
                    check(b.children);
                }
            };
            check(p.buttons);
        }
        const QString id = store.addPreset(QStringLiteral("tools"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(store.addPreset(QStringLiteral("tools")), id); // only once
        QCOMPARE(store.toolbar(id).buttons.size(), ToolbarStore::preset(QStringLiteral("tools")).buttons.size());
        QVERIFY(store.remove(id));

        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=horizontal\nToolbar ARRANGE=opus")));
        QToolBar *ops = w->toolbarByName(QStringLiteral("operations"));
        QToolBar *favs = w->toolbarByName(QStringLiteral("favorites"));
        QToolBar *views = w->toolbarByName(QStringLiteral("views"));
        QVERIFY(ops && favs && views);
        QCOMPARE(w->toolbarPlace(ops), ListerWindow::ToolbarPlace::Between);
        QCOMPARE(w->toolbarPlace(w->drivesBar()), ListerWindow::ToolbarPlace::Between);
        QVERIFY(w->toolbarStrip()->isJoined(favs));
        QCOMPARE(w->toolbarPlace(views), ListerWindow::ToolbarPlace::MenuBar);
        QVERIFY(w->toolbarByName(QStringLiteral("buttons"))->isHidden());
        // Back as it was for the tests after this one.
        for (const QString &n : {QStringLiteral("Operations"), QStringLiteral("Favorites"), QStringLiteral("Views")}) {
            for (const QString &tid : store.ids()) {
                if (store.toolbar(tid).name == n)
                    store.remove(tid);
            }
        }
        QVERIFY(w->runCommand(QStringLiteral("Toolbar NAME=buttons PLACE=top STATE=on\nToolbar NAME=drives PLACE=between\nSet DUAL=vertical")));
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
        QVERIFY(!runFailing(QStringLiteral("Set GROUPBY=color")));
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

    void pictureFoldersSwitchToThumbnails()
    {
        for (const char *n : {"p/1.png", "p/2.jpg", "p/3.jpg", "p/4.webp", "p/notes.txt"})
            touch(root + QLatin1Char('/') + QLatin1String(n));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(root + QStringLiteral("/p"));
        waitListed(t, 5);
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Thumbnails);
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
        QCOMPARE(top, (QStringList{QStringLiteral("Home"), QStringLiteral("Root"), QStringLiteral("Games"), QStringLiteral("Aliases"), QStringLiteral("Network")}));
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
        // Switch to find and replace with a clash: Rename grays out.
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

    void favoritesAndFrequentFolders()
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

        // Organize: what the dialog holds is what gets saved.
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

    void labelsRatingsAndTags()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        touch(d + QStringLiteral("/one.d64"));
        touch(d + QStringLiteral("/two.d64"));
        touch(d + QStringLiteral("/three.d64"));
        if (!FileTags::storedOnFile(d + QStringLiteral("/one.d64")))
            QSKIP("No extended attributes here");
        FolderTab *t = w->display(0)->currentTab();
        const QString back = t->path();
        t->navigate(d);
        waitListed(t, 3);
        t->selectPaths({d + QStringLiteral("/one.d64")});
        // The right-click menu has Label, Rating and the Tags window.
        QMenu menu;
        w->fillContextMenu(menu, w->display(0), true);
        QStringList texts;
        for (QAction *a : menu.actions())
            texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Label")));
        QVERIFY(texts.contains(QStringLiteral("Rating")));
        QVERIFY(texts.contains(QStringLiteral("Tags and Description...")));
        QVERIFY(w->runCommand(QStringLiteral("Tag LABEL=Green RATING=5")));
        // Columns show it; the row takes the label's color.
        t->setColumnVisible(FileListModel::RatingColumn, true);
        t->setColumnVisible(FileListModel::LabelColumn, true);
        auto *model = t->fileModel();
        QTRY_VERIFY([&] {
            const int r = model->rowForPath(d + QStringLiteral("/one.d64"));
            return r >= 0 && model->data(model->index(r, FileListModel::RatingColumn), Qt::DisplayRole).toString() == FileTags::stars(5);
        }());
        const int row = model->rowForPath(d + QStringLiteral("/one.d64"));
        QCOMPARE(model->data(model->index(row, FileListModel::LabelColumn), Qt::DisplayRole).toString(), QStringLiteral("Green"));
        QCOMPARE(model->data(model->index(row, FileListModel::NameColumn), Qt::ForegroundRole).value<QColor>(),
                 FileTags::colorOf(QStringLiteral("Green")));
        // The Tags window for several items: only what is changed is written.
        t->selectPaths({d + QStringLiteral("/one.d64"), d + QStringLiteral("/two.d64")});
        QVERIFY(w->runCommand(QStringLiteral("Tag")));
        TagsDialog *dlg = nullptr;
        QTRY_VERIFY((dlg = w->findChild<TagsDialog *>()));
        QCOMPARE(dlg->labelBox()->currentText(), QStringLiteral("(several labels)"));
        dlg->newTagEdit()->setText(QStringLiteral("cart, demo"));
        QTest::keyClick(dlg->newTagEdit(), Qt::Key_Return);
        dlg->commentEdit()->setPlainText(QStringLiteral("From the shoebox"));
        dlg->accept();
        QCOMPARE(FileTags::read(d + QStringLiteral("/one.d64")).label, QStringLiteral("Green")); // left alone
        QCOMPARE(FileTags::read(d + QStringLiteral("/one.d64")).rating, 5);
        QCOMPARE(FileTags::read(d + QStringLiteral("/two.d64")).tags, (QStringList{QStringLiteral("cart"), QStringLiteral("demo")}));
        QCOMPARE(FileTags::read(d + QStringLiteral("/two.d64")).comment, QStringLiteral("From the shoebox"));
        QTRY_VERIFY(!w->findChild<TagsDialog *>());
        // Select and group by them.
        QVERIFY(w->runCommand(QStringLiteral("Select RATING=4")));
        QTRY_COMPARE(t->selectedPaths(), QStringList{d + QStringLiteral("/one.d64")});
        QVERIFY(w->runCommand(QStringLiteral("Select TAG=demo")));
        QTRY_COMPARE(t->selectedPaths().size(), 2);
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=label")));
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::Label);
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=none")));
        t->setColumnVisible(FileListModel::RatingColumn, false);
        t->setColumnVisible(FileListModel::LabelColumn, false);
        t->navigate(back);
        waitListed(t, QDir(back).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
    }

    void checksums()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write(d + QStringLiteral("/abc.txt"), "abc");
        QDir(d).mkdir(QStringLiteral("disk"));
        write(d + QStringLiteral("/disk/side b.d64"), "123456789");
        FolderTab *t = w->display(0)->currentTab();
        const QString back = t->path();
        t->navigate(d);
        waitListed(t, 2);
        // To the clipboard without a window.
        t->selectPaths({d + QStringLiteral("/abc.txt")});
        QApplication::clipboard()->clear();
        QVERIFY(w->runCommand(QStringLiteral("Checksum ALGO=md5 CLIPBOARD")));
        QTRY_COMPARE(QApplication::clipboard()->text(), QStringLiteral("900150983cd24fb0d6963f7d28e17f72  abc.txt"));
        QTRY_VERIFY(!w->findChild<ChecksumDialog *>());
        // A list of both (the folder walked), saved next to them.
        t->selectPaths({d + QStringLiteral("/abc.txt"), d + QStringLiteral("/disk")});
        QVERIFY(w->runCommand(QStringLiteral("Checksum ALGO=crc32 SAVE=all.sfv")));
        QTRY_VERIFY(QFile::exists(d + QStringLiteral("/all.sfv")));
        QTRY_VERIFY(!w->findChild<ChecksumDialog *>());
        const auto entries = Checksum::readList(d + QStringLiteral("/all.sfv"));
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(1).name, QStringLiteral("disk/side b.d64"));
        QCOMPARE(entries.at(1).hash, QStringLiteral("CBF43926"));
        // Change a file, then check the list: selecting a list verifies it.
        write(d + QStringLiteral("/abc.txt"), "abd");
        QTRY_VERIFY(t->fileModel()->rowForPath(d + QStringLiteral("/all.sfv")) >= 0);
        t->selectPaths({d + QStringLiteral("/all.sfv")});
        QMenu menu;
        w->fillContextMenu(menu, w->display(0), true);
        QStringList texts;
        for (QAction *a : menu.actions())
            texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Verify Checksums")));
        QVERIFY(texts.contains(QStringLiteral("Checksums")));
        QVERIFY(w->runCommand(QStringLiteral("Checksum")));
        ChecksumDialog *dlg = nullptr;
        QTRY_VERIFY((dlg = w->findChild<ChecksumDialog *>()));
        QTRY_VERIFY(!dlg->isRunning() && dlg->okCount() + dlg->badCount() == 2);
        QCOMPARE(dlg->okCount(), 1);
        QCOMPARE(dlg->badCount(), 1);
        QCOMPARE(dlg->list()->topLevelItem(0)->text(1), QStringLiteral("Does not match"));
        dlg->close();
        QTRY_VERIFY(!w->findChild<ChecksumDialog *>());
        QVERIFY(!runFailing(QStringLiteral("Checksum ALGO=sha512")));
        // The MD5 column fills in.
        t->setColumnVisible(FileListModel::Md5Column, true);
        auto *model = t->fileModel();
        QTRY_VERIFY([&] {
            const int r = model->rowForPath(d + QStringLiteral("/abc.txt"));
            return r >= 0 && model->data(model->index(r, FileListModel::Md5Column), Qt::DisplayRole).toString()
                == Checksum::computeData("abd", Checksum::Algo::Md5);
        }());
        t->setColumnVisible(FileListModel::Md5Column, false);
        t->navigate(back);
        waitListed(t, QDir(back).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
    }

    void junkCountsAsHidden()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        touch(d + QStringLiteral("/game.d64"));
        touch(d + QStringLiteral("/Thumbs.db"));
        touch(d + QStringLiteral("/desktop.ini"));
        FolderTab *t = w->display(0)->currentTab();
        const QString back = t->path();
        const bool hidden = t->showHidden();
        QVERIFY(w->runCommand(QStringLiteral("Set HIDDEN=off")));
        t->navigate(d);
        waitListed(t, 1);
        QVERIFY(w->runCommand(QStringLiteral("Set HIDDEN=on")));
        QTRY_COMPARE(t->view()->model()->rowCount(), 3);
        QVERIFY(w->runCommand(QStringLiteral("Set HIDDEN=off")));
        Prefs::Values v = Prefs::instance()->values();
        v.junkHidden = false;
        Prefs::instance()->setValues(v);
        QTRY_COMPARE(t->view()->model()->rowCount(), 3);
        v.junkHidden = true;
        Prefs::instance()->setValues(v);
        QTRY_COMPARE(t->view()->model()->rowCount(), 1);
        QVERIFY(w->runCommand(hidden ? QStringLiteral("Set HIDDEN=on") : QStringLiteral("Set HIDDEN=off")));
        t->navigate(back);
        waitListed(t, QDir(back).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
    }

    void preferencesGeneralAndFileTypes()
    {
        FolderTab *t = w->display(0)->currentTab();
        QVERIFY(t->statusText().contains(QStringLiteral("folder")));
        Apps::setDefaultFor(QStringLiteral("text/plain"), QStringLiteral("nothing-installed.desktop"));
        {
            PreferencesDialog dlg(*m_app, w, QStringLiteral("general"));
            QCOMPARE(dlg.currentPage(), QStringLiteral("general"));
            QCOMPARE(dlg.search(QStringLiteral("terminal")), QStringList{QStringLiteral("general")});
            dlg.generalBox(QStringLiteral("counts"))->setChecked(false);
            dlg.generalBox(QStringLiteral("free"))->setChecked(false);
            dlg.terminalBox()->setCurrentIndex(dlg.terminalBox()->findData(QStringLiteral("kitty")));
            // File types: Linopus's own choices, and back to the desktop's.
            QTableWidget *types = dlg.fileTypesTable();
            QCOMPARE(types->rowCount(), 1);
            QCOMPARE(types->item(0, 0)->text(), QStringLiteral("text/plain"));
            QVERIFY(types->item(0, 2)->text().contains(QStringLiteral("not installed")));
            types->selectRow(0);
            for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
                if (b->text() == QStringLiteral("Use the Desktop's Choice"))
                    b->click();
            }
            QCOMPARE(types->rowCount(), 0);
            QVERIFY(Apps::defaultFor(QStringLiteral("text/plain")).isEmpty());
            QVERIFY(dlg.apply());
        }
        QVERIFY(!Prefs::instance()->values().statusCounts);
        QCOMPARE(Prefs::instance()->values().terminal, QStringLiteral("kitty"));
        QTRY_VERIFY(!w->display(0)->freeSpaceText().contains(QStringLiteral("free")));
        QVERIFY(!t->statusText().contains(QStringLiteral("folder")));
        Prefs::Values v = Prefs::instance()->values();
        v.statusCounts = v.statusFree = true;
        v.terminal.clear();
        Prefs::instance()->setValues(v);
        QTRY_VERIFY(w->display(0)->freeSpaceText().contains(QStringLiteral("free")));
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

            // Colors show at once; Cancel puts the old theme back.
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

    void viewerExtras()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        // Archives of any kind list what is inside.
        if (Archive::available()) {
            touch(d + QStringLiteral("/src/Game/disk.d64"));
            touch(d + QStringLiteral("/src/notes.txt"));
            QString err;
            QVERIFY(Archive::pack({d + QStringLiteral("/src/Game"), d + QStringLiteral("/src/notes.txt")}, d + QStringLiteral("/pack.7z"),
                                  Archive::Format::SevenZip, {}, &err));
            const Preview p = Preview::load(d + QStringLiteral("/pack.7z"));
            QCOMPARE(p.kind, Preview::Kind::Archive);
            QCOMPARE(p.entries.size(), 2);
            QVERIFY(p.info.contains(QStringLiteral("2 files")));
        }
        // Music plays in the pane (a short silent WAV).
        {
            QFile f(d + QStringLiteral("/beep.wav"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            const int rate = 8000, samples = 8000;
            QByteArray data(samples, char(128));
            QByteArray h;
            auto le32 = [&h](quint32 v) { for (int i = 0; i < 4; ++i) h.append(char((v >> (8 * i)) & 0xff)); };
            auto le16 = [&h](quint16 v) { h.append(char(v & 0xff)); h.append(char(v >> 8)); };
            h.append("RIFF"); le32(36 + samples); h.append("WAVEfmt "); le32(16); le16(1); le16(1); le32(rate); le32(rate); le16(1); le16(8);
            h.append("data"); le32(samples);
            f.write(h + data);
        }
        const Preview wav = Preview::load(d + QStringLiteral("/beep.wav"));
        if (ViewerPane::canPlay()) {
            QCOMPARE(wav.kind, Preview::Kind::Media);
            QVERIFY(w->runCommand(QStringLiteral("Set VIEWER=on")));
            w->viewer()->showNow(d + QStringLiteral("/beep.wav"));
            QCOMPARE(w->viewer()->kind(), Preview::Kind::Media);
            QVERIFY(w->viewer()->mediaLoaded());
            QVERIFY(!w->viewer()->isPlaying()); // nothing plays until asked
            w->viewer()->showNow(d + QStringLiteral("/src/notes.txt"));
            QVERIFY(!w->viewer()->mediaLoaded()); // moving on stops it
            QVERIFY(w->runCommand(QStringLiteral("Set VIEWER=off")));
        }

        // The picture viewer steps through the folder.
        QDir().mkpath(d + QStringLiteral("/pics"));
        for (const char *n : {"a1.png", "a2.png", "a10.png"}) {
            QImage img(40, 20, QImage::Format_RGB32);
            img.fill(Qt::blue);
            QVERIFY(img.save(d + QStringLiteral("/pics/") + QLatin1String(n)));
        }
        touch(d + QStringLiteral("/pics/readme.txt"));
        QCOMPARE(ImageViewer::picturesIn(d + QStringLiteral("/pics")).size(), 3);
        QVERIFY(w->runCommand(QStringLiteral("Show FILE=\"%1\"").arg(d + QStringLiteral("/pics/a2.png"))));
        ImageViewer *viewer = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (auto *iv = qobject_cast<ImageViewer *>(top))
                viewer = iv;
        }
        QVERIFY(viewer);
        QCOMPARE(viewer->count(), 3);
        QCOMPARE(viewer->index(), 1); // natural order: a1, a2, a10
        QCOMPARE(viewer->imageSize(), QSize(40, 20));
        QTest::keyClick(viewer, Qt::Key_Right);
        QVERIFY(viewer->current().endsWith(QStringLiteral("a10.png")));
        QTest::keyClick(viewer, Qt::Key_Right); // stays on the last
        QCOMPARE(viewer->index(), 2);
        QTest::keyClick(viewer, Qt::Key_Home);
        QCOMPARE(viewer->index(), 0);
        QTest::keyClick(viewer, Qt::Key_1);
        QCOMPARE(viewer->zoom(), 1.0);
        QTest::keyClick(viewer, Qt::Key_0);
        QCOMPARE(viewer->zoom(), 0.0);
        QPointer<ImageViewer> gone(viewer);
        QTest::keyClick(viewer, Qt::Key_Escape);
        QTRY_VERIFY(!gone);
        QVERIFY(!runFailing(QStringLiteral("Show FILE=\"%1\"").arg(d + QStringLiteral("/pics/readme.txt"))));
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

    void imageConvert()
    {
        const QString pics = root + QStringLiteral("/pics");
        QDir().mkpath(pics);
        for (const char *n : {"one.png", "two.png"}) {
            QImage img(80, 40, QImage::Format_RGB32);
            img.fill(Qt::green);
            QVERIFY(img.save(pics + QLatin1Char('/') + QLatin1String(n)));
        }
        touch(pics + QStringLiteral("/notes.txt"));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(pics);
        waitListed(t, 3);

        t->selectPaths({pics + QStringLiteral("/notes.txt")});
        QVERIFY(!w->evaluateCondition(QStringLiteral("images"), true));
        // The right-click menu offers Convert only when every pick is a picture.
        auto menuHas = [this](const QString &text) {
            QMenu menu;
            w->fillContextMenu(menu, w->display(0), true);
            for (QAction *a : menu.actions()) {
                if (a->text() == text)
                    return true;
            }
            return false;
        };
        QVERIFY(!menuHas(QStringLiteral("Convert Image...")));
        t->selectPaths({pics + QStringLiteral("/one.png"), pics + QStringLiteral("/notes.txt")});
        QVERIFY(!menuHas(QStringLiteral("Convert Images...")));
        t->selectPaths({pics + QStringLiteral("/one.png")});
        QVERIFY(menuHas(QStringLiteral("Convert Image...")));
        t->selectPaths({pics + QStringLiteral("/one.png"), pics + QStringLiteral("/two.png")});
        QVERIFY(menuHas(QStringLiteral("Convert Images...")));
        t->selectPaths({pics + QStringLiteral("/notes.txt")});
        QVERIFY(!runFailing(QStringLiteral("Image RUN"))); // no pictures in the selection

        t->selectPaths({pics + QStringLiteral("/one.png"), pics + QStringLiteral("/two.png"), pics + QStringLiteral("/notes.txt")});
        QVERIFY(w->evaluateCondition(QStringLiteral("images"), false));
        QVERIFY(!runFailing(QStringLiteral("Image CONVERT=doc RUN")));
        QVERIFY(!runFailing(QStringLiteral("Image ROTATE=45 RUN")));

        // Straight to the destination display, no window.
        QVERIFY(w->runCommand(QStringLiteral("Image CONVERT=jpg WIDTH=20 TO=dest RUN")));
        const QString dest = root + QStringLiteral("/b");
        QTRY_VERIFY(QFileInfo::exists(dest + QStringLiteral("/one.jpg")) && QFileInfo::exists(dest + QStringLiteral("/two.jpg")));
        QTRY_COMPARE(QImageReader(dest + QStringLiteral("/two.jpg")).size(), QSize(20, 10));
        QVERIFY(!QFileInfo::exists(dest + QStringLiteral("/notes.jpg")));

        // The window: a row per picture showing what it becomes.
        ImageOptions o;
        o.format = QStringLiteral("bmp");
        o.resize = ImageOptions::Resize::Percent;
        o.percent = 50;
        o.rotate = 90;
        ImageConvertDialog dlg({pics + QStringLiteral("/one.png"), pics + QStringLiteral("/two.png")}, o, dest, w);
        QCOMPARE(dlg.tree()->topLevelItemCount(), 2);
        QCOMPARE(dlg.tree()->topLevelItem(0)->text(2), QStringLiteral("one.bmp"));
        QCOMPARE(dlg.tree()->topLevelItem(0)->text(3), QStringLiteral("20 \u00d7 40"));
        QVERIFY(dlg.summary().startsWith(QStringLiteral("2 pictures")));
        // Changing a setting updates the list.
        ImageOptions o2 = dlg.options();
        QCOMPARE(o2.rotate, 90);
        o2.rotate = 0;
        o2.destFolder = dest;
        o2.format = QStringLiteral("jpg");
        o2.whenExists = ImageOptions::WhenExists::Skip;
        dlg.setOptions(o2);
        QTRY_VERIFY(dlg.summary().contains(QStringLiteral("left out")));
        QVERIFY(dlg.tree()->topLevelItem(0)->text(2).contains(QStringLiteral("already exists")));
        o2.whenExists = ImageOptions::WhenExists::Rename;
        dlg.setOptions(o2);
        QTRY_VERIFY(dlg.tree()->topLevelItem(0)->text(2).endsWith(QStringLiteral("one (2).jpg")));
        QSignalSpy done(&dlg, &ImageConvertDialog::converted);
        dlg.startConvert();
        QTRY_VERIFY(!dlg.isConverting());
        QCOMPARE(done.size(), 1);
        QCOMPARE(dlg.results().size(), 2);
        QVERIFY(QFileInfo::exists(dest + QStringLiteral("/one (2).jpg")));
        QCOMPARE(QImageReader(dest + QStringLiteral("/one (2).jpg")).size(), QSize(40, 20));
        // Remembered for next time, apart from the folder.
        const ImageOptions saved = ImageConvertDialog::savedOptions();
        QCOMPARE(saved.format, QStringLiteral("jpg"));
        QCOMPARE(saved.resize, ImageOptions::Resize::Percent);
        QVERIFY(saved.destFolder.isEmpty());
    }

    void metadataColumns()
    {
        // SID tunes carry their own name, author and year: no library needed.
        const QString music = root + QStringLiteral("/music");
        QDir().mkpath(music);
        auto sid = [&music](const char *file, const QByteArray &name, const QByteArray &author, const QByteArray &released) {
            QByteArray h(0x7c, '\0');
            h.replace(0, 4, "PSID");
            h[5] = 2;
            h[0x0f] = 1;
            h[0x11] = 1;
            h.replace(0x16, name.size(), name);
            h.replace(0x36, author.size(), author);
            h.replace(0x56, released.size(), released);
            QFile f(music + QLatin1Char('/') + QLatin1String(file));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(h);
        };
        sid("commando.sid", "Commando", "Rob Hubbard", "1985 Elite");
        sid("monty.sid", "Monty on the Run", "Rob Hubbard", "1985 Gremlin");
        sid("lastninja.sid", "The Last Ninja", "Ben Daglish", "1987 System 3");
        touch(music + QStringLiteral("/readme.txt"));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(music);
        waitListed(t, 4);

        // Hidden until asked for; they fill in once read.
        QVERIFY(!t->isColumnVisible(FileListModel::ArtistColumn));
        QVERIFY(w->runCommand(QStringLiteral("Set COLUMNSTOGGLE=artist\nSet COLUMNSTOGGLE=year")));
        QVERIFY(t->isColumnVisible(FileListModel::ArtistColumn));
        QAbstractItemModel *m = t->view()->model();
        auto cell = [m](const QString &name, int column) {
            for (int r = 0; r < m->rowCount(); ++r) {
                if (m->index(r, 0).data().toString() == name)
                    return m->index(r, column).data().toString();
            }
            return QStringLiteral("(missing)");
        };
        QTRY_COMPARE(cell(QStringLiteral("lastninja.sid"), FileListModel::ArtistColumn), QStringLiteral("Ben Daglish"));
        QTRY_COMPARE(cell(QStringLiteral("commando.sid"), FileListModel::YearColumn), QStringLiteral("1985"));
        QCOMPARE(cell(QStringLiteral("readme.txt"), FileListModel::ArtistColumn), QString());

        // Grouped by artist.
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=artist")));
        QVERIFY(w->evaluateCondition(QStringLiteral("groupby=artist"), false));
        QTRY_COMPARE(labels(t), (QStringList{QStringLiteral("Ben Daglish"), QStringLiteral("Rob Hubbard"), QStringLiteral("No artist")}));
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=year")));
        QTRY_COMPARE(labels(t), (QStringList{QStringLiteral("1985"), QStringLiteral("1987"), QStringLiteral("No year")}));
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=none")));

        // Sorted by artist, files with one first.
        auto *details = qobject_cast<QTreeView *>(t->view());
        QVERIFY(details);
        details->sortByColumn(FileListModel::ArtistColumn, Qt::AscendingOrder);
        QTRY_COMPARE(m->index(0, 0).data().toString(), QStringLiteral("lastninja.sid"));
        QCOMPARE(m->index(3, 0).data().toString(), QStringLiteral("readme.txt"));

        // A layout saved before these columns existed keeps them hidden;
        // one saved now keeps them as they were.
        const QByteArray now = t->headerState();
        QVERIFY(now.startsWith("LNH1"));
        const QByteArray old = now.mid(now.indexOf(':') + 1); // no tag: saved by an older Linopus
        t->restoreHeaderState(old);
        QVERIFY(!t->isColumnVisible(FileListModel::ArtistColumn));
        QVERIFY(t->isColumnVisible(FileListModel::SizeColumn));
        t->restoreHeaderState(now);
        QVERIFY(t->isColumnVisible(FileListModel::ArtistColumn));
        QVERIFY(t->isColumnVisible(FileListModel::YearColumn));
    }

    void thumbnailsView()
    {
        Paths::setCacheHomeOverride(m_dir->filePath(QStringLiteral("cache")));
        const QString pics = root + QStringLiteral("/shots");
        QDir().mkpath(pics);
        for (int i = 0; i < 5; ++i) {
            QImage img(640, 400, QImage::Format_RGB32);
            img.fill(QColor::fromHsv(i * 60, 200, 220));
            QVERIFY(img.save(pics + QStringLiteral("/shot%1.png").arg(i)));
        }
        // A C64 cartridge is not a certificate.
        QFile crt(pics + QStringLiteral("/Mayhem.crt"));
        QVERIFY(crt.open(QIODevice::WriteOnly));
        crt.write("C64 CARTRIDGE   ");
        crt.write(QByteArray(64, '\0'));
        crt.close();
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(pics);
        waitListed(t, 6);
        // Mostly pictures: thumbnails by themselves.
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Thumbnails);
        QVERIFY(w->evaluateCondition(QStringLiteral("view=thumbnails"), false));
        FileListModel *model = t->fileModel();
        QVERIFY(model);
        QVERIFY(model->thumbnails());
        const int row = model->rowForPath(pics + QStringLiteral("/shot2.png"));
        QVERIFY(row >= 0);
        auto thumbShown = [model, row]() {
            const QIcon icon = qvariant_cast<QIcon>(model->index(row, 0).data(Qt::DecorationRole));
            return icon.availableSizes().contains(QSize(256, 256));
        };
        QTRY_VERIFY(thumbShown());
        QCOMPARE(model->typeName(model->rowForPath(pics + QStringLiteral("/Mayhem.crt"))), QStringLiteral("C64 cartridge image"));
        QVERIFY(w->runCommand(QStringLiteral("Set VIEW=details")));
        QVERIFY(!model->thumbnails());
        QVERIFY(w->runCommand(QStringLiteral("Set VIEW=thumbnails")));
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::Thumbnails);
        Paths::setCacheHomeOverride(QString());
    }

    void zoomPerLister()
    {
        FolderTab *t = w->display(0)->currentTab();
        const qreal base = t->view()->font().pointSizeF();
        QVERIFY(base > 0);
        QCOMPARE(w->zoom(), 100);

        // Shift+wheel over the list: one notch, one step.
        QWidget *vp = t->view()->viewport();
        const QPoint at = vp->rect().center();
        QWheelEvent up(at, vp->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(vp, &up);
        QCOMPARE(w->zoom(), 110);
        QVERIFY(t->view()->font().pointSizeF() > base);
        // Some systems turn Shift+wheel into a sideways scroll: still a zoom.
        QWheelEvent side(at, vp->mapToGlobal(at), QPoint(), QPoint(120, 0), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(vp, &side);
        QCOMPARE(w->zoom(), 125);
        // A trackpad's small steps add up.
        for (int i = 0; i < 4; ++i) {
            QWheelEvent small(at, vp->mapToGlobal(at), QPoint(0, -10), QPoint(0, -30), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(vp, &small);
        }
        QCOMPARE(w->zoom(), 110);
        // A plain wheel still scrolls.
        QWheelEvent plain(at, vp->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(vp, &plain);
        QCOMPARE(w->zoom(), 110);

        // Each display zooms on its own: Shift+wheel over the other one
        // leaves this one alone, and commands work on the source.
        FolderTab *right = w->display(1)->currentTab();
        if (right) {
            QWidget *rvp = right->view()->viewport();
            const QPoint rat = rvp->rect().center();
            QWheelEvent rup(rat, rvp->mapToGlobal(rat), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(rvp, &rup);
            QCOMPARE(w->display(1)->zoom(), 125);
            QCOMPARE(w->display(0)->zoom(), 110);
            QCOMPARE(t->zoom(), 110);
            QCOMPARE(right->zoom(), 125);
        }
        QCOMPARE(w->sourceDisplay(), w->display(0));

        // New tabs and the tree follow the display; other listers do not.
        const int sizeWidth = t->detailsView()->header()->sectionSize(FileListModel::SizeColumn);
        QVERIFY(w->runCommand(QStringLiteral("Set ZOOM=150")));
        QCOMPARE(t->detailsView()->header()->sectionSize(FileListModel::SizeColumn), qRound(sizeWidth * 150.0 / 110));
        if (right)
            QCOMPARE(right->zoom(), 125);
        FolderTab *fresh = w->display(0)->addTab(root + QStringLiteral("/b"));
        QCOMPARE(fresh->zoom(), 150);
        QVERIFY(w->runCommand(QStringLiteral("Set TREE=shared")));
        QVERIFY(w->sharedTree()->font().pointSizeF() > base * 1.4);
        QCOMPARE(t->zoomed(16), Style::instance()->scaled(24));
        ListerWindow *other = m_app->newLister(root);
        QCOMPARE(other->zoom(), 100);
        other->close();

        QVERIFY(w->runCommand(QStringLiteral("Set ZOOM=in")));
        QCOMPARE(w->zoom(), 160);
        QVERIFY(!runFailing(QStringLiteral("Set ZOOM=900")));
        QVERIFY(w->runCommand(QStringLiteral("Set ZOOM=reset")));
        QCOMPARE(w->zoom(), 100);
        QCOMPARE(t->view()->font().pointSizeF(), base);
        // At 100% the lists follow the app's text size again.
        QVERIFY(w->runCommand(QStringLiteral("Set SIZE=larger")));
        QVERIFY(t->view()->font().pointSizeF() > base);
        QVERIFY(w->runCommand(QStringLiteral("Set SIZE=normal\nSet TREE=off")));
        w->setZoom(100, w->display(1));
    }

    void flatViewCopyStructure()
    {
        QTemporaryDir own;
        const QString base = own.path();
        const QString src = base + QStringLiteral("/src");
        touch(src + QStringLiteral("/top.txt"));
        touch(src + QStringLiteral("/sub/x.txt"));
        touch(src + QStringLiteral("/sub/deep/y.txt"));
        for (const char *d : {"keep", "same", "asked", "cancel"})
            QDir().mkpath(base + QLatin1Char('/') + QLatin1String(d));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(src);
        waitListed(t, 2);
        // Ungrouped: one list, no folder headings; toggling brings it back.
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=ungrouped")));
        waitListed(t, 5); // sub, deep and three files
        QCOMPARE(t->groupBy(), FileListModel::GroupBy::None);
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=toggle")));
        QCOMPARE(t->flatMode(), DirLister::FlatMode::Off);
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=toggle")));
        QCOMPARE(t->flatMode(), DirLister::FlatMode::Mixed);
        waitListed(t, 5);

        const QStringList files = {src + QStringLiteral("/top.txt"), src + QStringLiteral("/sub/x.txt"),
                                   src + QStringLiteral("/sub/deep/y.txt")};
        t->selectPaths(files);
        QVERIFY(w->runCommand(QStringLiteral("Copy TO=\"%1/keep\" STRUCTURE=keep").arg(base)));
        m_app->jobs().waitForIdle(5000);
        QTRY_VERIFY(QFile::exists(base + QStringLiteral("/keep/sub/deep/y.txt")));
        QVERIFY(QFile::exists(base + QStringLiteral("/keep/sub/x.txt")));
        QVERIFY(QFile::exists(base + QStringLiteral("/keep/top.txt")));

        QVERIFY(w->runCommand(QStringLiteral("Copy TO=\"%1/same\" STRUCTURE=flat").arg(base)));
        m_app->jobs().waitForIdle(5000);
        QTRY_VERIFY(QFile::exists(base + QStringLiteral("/same/y.txt")));
        QVERIFY(QFile::exists(base + QStringLiteral("/same/x.txt")));
        QVERIFY(!QFile::exists(base + QStringLiteral("/same/sub")));

        // A folder picked with things inside it: the folder carries them.
        t->selectPaths({src + QStringLiteral("/sub"), src + QStringLiteral("/sub/deep/y.txt")});
        QVERIFY(w->runCommand(QStringLiteral("Copy TO=\"%1/keep\" STRUCTURE=keep WHENEXISTS=replace").arg(base)));
        m_app->jobs().waitForIdle(5000);
        QVERIFY(!QFile::exists(base + QStringLiteral("/keep/deep")));

        // Without STRUCTURE it asks.
        auto answer = [](const QString &button) {
            QTimer::singleShot(0, [button]() {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                QVERIFY(box);
                for (QAbstractButton *b : box->buttons()) {
                    if (b->text().remove(QLatin1Char('&')) == button) {
                        b->click();
                        return;
                    }
                }
                box->reject();
            });
        };
        t->selectPaths(files);
        answer(QStringLiteral("Same Folder"));
        QVERIFY(w->runCommand(QStringLiteral("Copy TO=\"%1/asked\"").arg(base)));
        m_app->jobs().waitForIdle(5000);
        QTRY_VERIFY(QFile::exists(base + QStringLiteral("/asked/y.txt")));
        answer(QStringLiteral("Cancel"));
        w->runCommand(QStringLiteral("Copy TO=\"%1/cancel\"").arg(base));
        m_app->jobs().waitForIdle(5000);
        QVERIFY(QDir(base + QStringLiteral("/cancel")).isEmpty());
        // Files all from the folder itself: no question.
        t->selectPaths({src + QStringLiteral("/top.txt")});
        QVERIFY(w->runCommand(QStringLiteral("Copy TO=\"%1/cancel\"").arg(base)));
        m_app->jobs().waitForIdle(5000);
        QTRY_VERIFY(QFile::exists(base + QStringLiteral("/cancel/top.txt")));
        QVERIFY(w->runCommand(QStringLiteral("Set FLATVIEW=off")));
        t->navigate(root);
        waitListed(t, QDir(root).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
    }

    void archives()
    {
        if (!Archive::available())
            QSKIP("Built without libarchive");
        QTemporaryDir own;
        const QString base = own.path();
        touch(base + QStringLiteral("/Game/disk.d64"));
        touch(base + QStringLiteral("/notes.txt"));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(base);
        waitListed(t, 2);
        auto menuTexts = [this]() {
            QMenu menu;
            w->fillContextMenu(menu, w->display(0), true);
            QStringList out;
            for (QAction *a : menu.actions())
                out << a->text();
            return out;
        };
        t->selectPaths({base + QStringLiteral("/Game")});
        QStringList texts = menuTexts();
        QVERIFY(texts.contains(QStringLiteral("Add to Archive...")));
        QVERIFY(texts.contains(QStringLiteral("Add to \"Game.zip\"")));
        QVERIFY(!texts.contains(QStringLiteral("Extract Here")));

        // The dialog: switching the format swaps the extension; Create makes it.
        QVERIFY(w->runCommand(QStringLiteral("Archive CREATE")));
        auto *dlg = w->findChild<ArchiveDialog *>();
        QVERIFY(dlg);
        QCOMPARE(dlg->nameEdit()->text(), QStringLiteral("Game.") + ArchiveDialog::savedFormat());
        dlg->formatBox()->setCurrentIndex(dlg->formatBox()->findData(QStringLiteral("tar.gz")));
        QCOMPARE(dlg->nameEdit()->text(), QStringLiteral("Game.tar.gz"));
        QVERIFY(!dlg->passwordEdit()->isEnabled()); // zip only
        dlg->formatBox()->setCurrentIndex(dlg->formatBox()->findData(QStringLiteral("zip")));
        QVERIFY(dlg->passwordEdit()->isEnabled());
        dlg->passwordEdit()->setText(QStringLiteral("a \"b\" {c}"));
        QCOMPARE(dlg->parsedCommand().value(QStringLiteral("PASSWORD")), QStringLiteral("a \"b\" {c}"));
        QVERIFY(!dlg->command().contains(QStringLiteral("PASSWORD")));
        dlg->passwordEdit()->clear();
        dlg->formatBox()->setCurrentIndex(dlg->formatBox()->findData(QStringLiteral("tar.gz")));
        dlg->nameEdit()->setText(QStringLiteral("Saved Game.tar.gz"));
        dlg->accept();
        m_app->jobs().waitForIdle(10000);
        QTRY_VERIFY(QFile::exists(base + QStringLiteral("/Saved Game.tar.gz")));
        QCOMPARE(ArchiveDialog::savedFormat(), QStringLiteral("tar.gz"));
        waitListed(t, 3);

        t->selectPaths({base + QStringLiteral("/Saved Game.tar.gz")});
        QVERIFY(w->evaluateCondition(QStringLiteral("archives"), true));
        texts = menuTexts();
        QVERIFY(texts.contains(QStringLiteral("Extract Here")));
        QVERIFY(texts.contains(QStringLiteral("Extract to \"Saved Game\"")));
        QVERIFY(w->runCommand(QStringLiteral("Archive EXTRACT MODE=folder")));
        m_app->jobs().waitForIdle(10000);
        QTRY_VERIFY(QFile::exists(base + QStringLiteral("/Saved Game/Game/disk.d64")));
        t->selectPaths({base + QStringLiteral("/notes.txt")});
        QVERIFY(!w->evaluateCondition(QStringLiteral("archives"), true));
        t->navigate(root);
        waitListed(t, QDir(root).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
    }

    void propertiesAndLinks()
    {
        QTemporaryDir own;
        const QString base = own.path();
        touch(base + QStringLiteral("/one.txt"));
        touch(base + QStringLiteral("/Folder/a.txt"));
        touch(base + QStringLiteral("/Folder/sub/b.txt"));
        QFile::setPermissions(base + QStringLiteral("/one.txt"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
        FolderTab *t = w->display(0)->currentTab();
        const QString back = t->path();
        t->navigate(base);
        waitListed(t, 2);
        t->selectPaths({base + QStringLiteral("/one.txt")});
        QMenu menu;
        w->fillContextMenu(menu, w->display(0), true);
        QStringList texts;
        for (QAction *a : menu.actions())
            texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Properties...")));
        QVERIFY(texts.contains(QStringLiteral("Make Link Here")));

        QVERIFY(w->runCommand(QStringLiteral("Properties")));
        auto *dlg = w->findChild<PropertiesDialog *>();
        QVERIFY(dlg);
        QVERIFY(dlg->sizeText().startsWith(QStringLiteral("1 byte")) || dlg->sizeText().contains(QStringLiteral("1 B")));
        QCOMPARE(dlg->permissionBox(1, 0)->checkState(), Qt::Checked);   // group read
        QCOMPARE(dlg->permissionBox(1, 1)->checkState(), Qt::Unchecked); // group write
        QVERIFY(dlg->change().isEmpty());
        dlg->permissionBox(1, 1)->setChecked(true);
        dlg->modifiedEdit()->setDateTime(QDateTime(QDate(2020, 6, 7), QTime(8, 9, 10)));
        QCOMPARE(dlg->change().mode.add, 020);
        dlg->accept();
        QVERIFY(QFileInfo(base + QStringLiteral("/one.txt")).permissions() & QFileDevice::WriteGroup);
        QCOMPARE(QFileInfo(base + QStringLiteral("/one.txt")).lastModified(), QDateTime(QDate(2020, 6, 7), QTime(8, 9, 10)));
        QTRY_VERIFY(!w->findChild<PropertiesDialog *>());

        // A folder is measured in the background; mixed permissions show as mixed.
        t->selectPaths({base + QStringLiteral("/Folder"), base + QStringLiteral("/one.txt")});
        QVERIFY(w->runCommand(QStringLiteral("Properties")));
        dlg = w->findChild<PropertiesDialog *>();
        QVERIFY(dlg);
        QTRY_VERIFY(!dlg->measuring());
        QTRY_VERIFY(dlg->sizeText().contains(QStringLiteral("3 files")));
        QVERIFY(dlg->sizeText().contains(QStringLiteral("2 folders")));
        QVERIFY(dlg->recurseBox()->isVisible() || !dlg->isVisible());
        QCOMPARE(dlg->permissionBox(0, 2)->checkState(), Qt::PartiallyChecked); // folder x, file not
        dlg->reject();
        QTRY_VERIFY(!w->findChild<PropertiesDialog *>());

        // A link shows where it points.
        t->selectPaths({base + QStringLiteral("/one.txt")});
        QVERIFY(w->runCommand(QStringLiteral("MakeLink")));
        QVERIFY(QFileInfo(base + QStringLiteral("/one (link).txt")).isSymLink());
        QVERIFY(w->runCommand(QStringLiteral("Properties FILE=\"%1\"").arg(base + QStringLiteral("/one (link).txt"))));
        QTRY_VERIFY(w->findChild<PropertiesDialog *>());
        dlg = w->findChild<PropertiesDialog *>();
        QCOMPARE(dlg->linkText(), base + QStringLiteral("/one.txt"));
        dlg->reject();
        QTRY_VERIFY(!w->findChild<PropertiesDialog *>());
        t->navigate(back);
        waitListed(t, QDir(back).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
    }

    void flatButtonLongPress()
    {
        ButtonDef def;
        def.label = QStringLiteral("Flat");
        def.left = QStringLiteral("Set FLATVIEW=toggle");
        ThreeButton b(def, nullptr);
        QVERIFY(b.menu());
        QCOMPARE(b.popupMode(), QToolButton::DelayedPopup);
        QStringList texts;
        for (QAction *a : b.menu()->actions())
            texts << a->text();
        QCOMPARE(texts, (QStringList{QStringLiteral("Off"), QStringLiteral("Grouped by Folder"), QStringLiteral("Ungrouped"),
                                     QStringLiteral("Ungrouped, Files Only")}));
        // Other buttons are unchanged.
        def.left = QStringLiteral("Copy");
        ThreeButton plain(def, nullptr);
        QVERIFY(!plain.menu());
    }

    void locationBar()
    {
        FileDisplay *d = w->display(0);
        FolderTab *t = d->currentTab();
        const QString start = t->path();
        QVERIFY(d->refreshButton() && d->homeButton() && d->favoritesButton() && d->recentButton());
        // The bar wears the role color.
        w->runCommand(QStringLiteral("Set DUAL=on"));
        QTRY_COMPARE(d->locationBar()->palette().color(QPalette::Window),
                     d->role() == FileDisplay::Role::Source ? Style::instance()->theme().sourceAccent
                                                             : Style::instance()->theme().destAccent);
        // Recent folders: where this tab has been.
        t->navigate(root + QStringLiteral("/a"));
        QTRY_COMPARE(t->path(), root + QStringLiteral("/a"));
        t->navigate(root + QStringLiteral("/b"));
        QTRY_COMPARE(t->path(), root + QStringLiteral("/b"));
        QMenu recent;
        d->fillRecentMenu(&recent);
        QStringList texts;
        for (QAction *a : recent.actions())
            texts << a->text();
        // Full paths, the current folder first, then the newest.
        QCOMPARE(texts.value(0), Location::display(root + QStringLiteral("/b")));
        QCOMPARE(texts.value(1), Location::display(root + QStringLiteral("/a")));
        QVERIFY(FileDisplay::history().contains(root + QStringLiteral("/a")));
        for (QAction *a : recent.actions()) {
            if (a->text() == Location::display(root + QStringLiteral("/a")))
                a->trigger();
        }
        QTRY_COMPARE(t->path(), root + QStringLiteral("/a"));
        // The star lists favorites and offers to add this folder.
        QMenu *fav = d->favoritesButton()->menu();
        QVERIFY(fav);
        QMetaObject::invokeMethod(fav, "aboutToShow");
        bool hasAdd = false;
        for (QAction *a : fav->actions())
            hasAdd = hasAdd || a->text().startsWith(QStringLiteral("Add This Folder"));
        QVERIFY(hasAdd);
        // Copy this folder to the other display; swap the two.
        FolderTab *o = w->display(1)->currentTab();
        const QString otherStart = o->path();
        QVERIFY(d->copyPathButton()->isVisibleTo(d));
        d->copyPathButton()->click();
        QTRY_COMPARE(o->path(), root + QStringLiteral("/a"));
        o->navigate(root + QStringLiteral("/b"));
        QTRY_COMPARE(o->path(), root + QStringLiteral("/b"));
        d->swapPathsButton()->click();
        QTRY_COMPARE(t->path(), root + QStringLiteral("/b"));
        QTRY_COMPARE(o->path(), root + QStringLiteral("/a"));
        o->navigate(otherStart);
        QTRY_COMPARE(o->path(), otherStart);
        d->homeButton()->click();
        QTRY_COMPARE(t->path(), QDir::homePath());
        // Home is a best favorite: a middle click makes this folder it.
        t->navigate(root + QStringLiteral("/b"));
        QTRY_COMPARE(t->path(), root + QStringLiteral("/b"));
        QTest::mouseClick(d->homeButton(), Qt::MiddleButton);
        QCOMPARE(FileDisplay::homeFolder(), root + QStringLiteral("/b"));
        QCOMPARE(t->path(), root + QStringLiteral("/b")); // the middle click does not go anywhere
        t->navigate(root + QStringLiteral("/a"));
        QTRY_COMPARE(t->path(), root + QStringLiteral("/a"));
        w->display(1)->homeButton()->click(); // every display shares it
        QTRY_COMPARE(w->display(1)->currentTab()->path(), root + QStringLiteral("/b"));
        d->homeButton()->click();
        QTRY_COMPARE(t->path(), root + QStringLiteral("/b"));
        d->setHomeFolder(QString());
        QCOMPARE(FileDisplay::homeFolder(), QDir::homePath());
        w->display(1)->currentTab()->navigate(otherStart);
        QTRY_COMPARE(w->display(1)->currentTab()->path(), otherStart);
        t->navigate(start);
        QTRY_COMPARE(t->path(), start);
    }

    void konsoleOnEmptyArea()
    {
        QMenu menu;
        w->fillContextMenu(menu, w->display(0), false);
        QStringList texts;
        for (QAction *a : menu.actions())
            texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Open Konsole in This Folder")));
        QMenu onItem;
        w->display(0)->currentTab()->selectPaths({});
        w->fillContextMenu(onItem, w->display(0), true);
        for (QAction *a : onItem.actions())
            QVERIFY(a->text() != QStringLiteral("Open Konsole in This Folder"));
    }

    void networkAndServers()
    {
        Servers::instance()->setItems({Server{QStringLiteral("NAS"), QStringLiteral("sftp://deck@nas/media")}});
        // The Go menu, the drives bar and buttons share one menu.
        QMenu menu;
        w->fillDynamicMenu(&menu, QStringLiteral("servers"));
        QStringList texts;
        for (QAction *a : menu.actions())
            texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Windows Shares")));
        QVERIFY(texts.contains(QStringLiteral("NAS")));
        QVERIFY(texts.contains(QStringLiteral("Add Server...")));
        QVERIFY(texts.contains(QStringLiteral("Manage Servers...")));
        // The drives bar has a Network button.
        auto *drives = w->findChild<DrivesBar *>();
        QVERIFY(drives);
        QTRY_VERIFY(drives->networkButton());
        // The add dialog builds the address and a name from the parts.
        ServerDialog dlg(Server{}, nullptr);
        dlg.kindBox()->setCurrentIndex(dlg.kindBox()->findData(QStringLiteral("smb")));
        dlg.hostEdit()->setText(QStringLiteral("box"));
        dlg.folderEdit()->setText(QStringLiteral("Games"));
        QCOMPARE(dlg.server().url, QStringLiteral("smb://box/Games"));
        QCOMPARE(dlg.nameEdit()->text(), QStringLiteral("box Games (Windows share)"));
        ServerDialog edit(Server{QStringLiteral("NAS"), QStringLiteral("sftp://deck@nas:2222/media")}, nullptr);
        QCOMPARE(edit.hostEdit()->text(), QStringLiteral("nas"));
        QCOMPARE(edit.portBox()->value(), 2222);
        QCOMPARE(edit.userEdit()->text(), QStringLiteral("deck"));
        QCOMPARE(edit.server(), (Server{QStringLiteral("NAS"), QStringLiteral("sftp://deck@nas:2222/media")}));
        Servers::instance()->setItems({});
    }

    void scriptButtonsShowFailures()
    {
        QTemporaryDir cache;
        Paths::setCacheHomeOverride(cache.path());
        QVERIFY(w->runCommand(QStringLiteral("#!/bin/bash\necho something broke\nexit 3")));
        QDialog *out = nullptr;
        QTRY_VERIFY((out = w->findChild<QDialog *>(QStringLiteral("scriptOutput"))));
        QVERIFY(out->windowTitle().contains(QStringLiteral("exit 3")));
        QVERIFY(out->findChild<QPlainTextEdit *>()->toPlainText().contains(QStringLiteral("something broke")));
        out->close();
        QTRY_VERIFY(!w->findChild<QDialog *>(QStringLiteral("scriptOutput")));
        // Success, quietly: no window.
        QVERIFY(w->runCommand(QStringLiteral("#!/bin/bash\ntrue")));
        QTest::qWait(500);
        QVERIFY(!w->findChild<QDialog *>(QStringLiteral("scriptOutput")));
        Paths::setCacheHomeOverride(QString());
    }

    void openWith()
    {
        QTemporaryDir apps;
        auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write(apps.filePath(QStringLiteral("kate.desktop")),
              "[Desktop Entry]\nType=Application\nName=Kate\nExec=kate %U\nMimeType=text/plain;\n");
        write(apps.filePath(QStringLiteral("gimp.desktop")),
              "[Desktop Entry]\nType=Application\nName=GIMP\nExec=gimp %F\nMimeType=image/png;\n");
        Apps::setSearchDirsOverride({apps.path()});
        QStringList ran;
        HostRun::setStarter([&](const QStringList &cmd, const QString &) {
            ran = cmd;
            return true;
        });
        FolderTab *t = w->display(0)->currentTab();
        const QString file = root + QStringLiteral("/a/one.txt");
        t->navigate(root + QStringLiteral("/a"));
        QTRY_COMPARE(t->path(), root + QStringLiteral("/a"));
        QTRY_VERIFY(t->view()->model()->rowCount() > 0);
        t->selectPaths({file});
        QMenu menu;
        w->fillContextMenu(menu, w->display(0), true);
        QMenu *with = nullptr;
        for (QAction *a : menu.actions()) {
            if (a->menu() && a->text() == QStringLiteral("Open With"))
                with = a->menu();
        }
        QVERIFY(with);
        QStringList texts;
        for (QAction *a : with->actions())
            texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Kate")));
        QVERIFY(!texts.contains(QStringLiteral("GIMP")));
        QVERIFY(texts.contains(QStringLiteral("Other Application...")));
        for (QAction *a : with->actions()) {
            if (a->text() == QStringLiteral("Kate"))
                a->trigger();
        }
        QCOMPARE(ran.value(0), QStringLiteral("kate"));
        // Other Application: every app, with "always use".
        QVERIFY(w->runCommand(QStringLiteral("OpenWith")));
        OpenWithDialog *dlg = nullptr;
        QTRY_VERIFY((dlg = w->findChild<OpenWithDialog *>()));
        dlg->search()->setText(QStringLiteral("gim"));
        QTRY_COMPARE(dlg->chosen().name, QStringLiteral("GIMP"));
        dlg->alwaysBox()->setChecked(true);
        dlg->accept();
        QCOMPARE(ran.value(0), QStringLiteral("gimp"));
        QCOMPARE(Apps::defaultFor(QStringLiteral("text/plain")), QStringLiteral("gimp.desktop"));
        // Opening the file now uses Linopus's default.
        ran.clear();
        w->openFile(file);
        QCOMPARE(ran.value(0), QStringLiteral("gimp"));
        QVERIFY(w->runCommand(QStringLiteral("OpenWith APP=Kate DEFAULT")));
        QCOMPARE(Apps::defaultFor(QStringLiteral("text/plain")), QStringLiteral("kate.desktop"));
        Apps::setDefaultFor(QStringLiteral("text/plain"), QString());
        HostRun::setStarter({});
        Apps::setSearchDirsOverride({});
    }

    void freeSpaceShown()
    {
        FileDisplay *d = w->display(0);
        QTRY_VERIFY(d->freeSpaceText().contains(QStringLiteral(" free of ")));
    }

    void columnsResizable()
    {
        FolderTab *t = w->display(0)->currentTab();
        t->setViewMode(FolderTab::ViewMode::Details);
        QHeaderView *h = t->detailsView()->header();
        for (int c = 0; c < FileListModel::ColumnCount; ++c)
            QCOMPARE(h->sectionResizeMode(c), QHeaderView::Interactive);
        QVERIFY(t->nameColumnFills());
        QVERIFY(h->style()->pixelMetric(QStyle::PM_HeaderGripMargin, nullptr, h) >= Style::instance()->scaled(7));

        // Widening another column takes the room from the name.
        const int name = h->sectionSize(FileListModel::NameColumn);
        const int size = h->sectionSize(FileListModel::SizeColumn);
        h->resizeSection(FileListModel::SizeColumn, size + 40);
        if (name - 40 >= t->detailsView()->fontMetrics().averageCharWidth() * 18)
            QCOMPARE(h->sectionSize(FileListModel::NameColumn), name - 40);

        // Dragging the name's edge sizes it by hand; that sticks.
        QWidget *hw = h->viewport();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), hw->mapToGlobal(QPointF(5, 5)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(h, &press);
        h->resizeSection(FileListModel::NameColumn, 150);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(5, 5), hw->mapToGlobal(QPointF(5, 5)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(h, &release);
        QVERIFY(!t->nameColumnFills());
        h->resizeSection(FileListModel::SizeColumn, size);
        QCOMPARE(h->sectionSize(FileListModel::NameColumn), 150);
        const QByteArray state = t->headerState();
        FolderTab *other = w->display(0)->addTab(root);
        other->restoreHeaderState(state);
        QVERIFY(!other->nameColumnFills());
        QCOMPARE(other->detailsView()->header()->sectionSize(FileListModel::NameColumn), 150);
        w->display(0)->closeTab(w->display(0)->tabCount() - 1);

        // Double-clicking its edge makes it fill again.
        Q_EMIT h->sectionHandleDoubleClicked(FileListModel::NameColumn);
        QTRY_VERIFY(t->nameColumnFills());
        QVERIFY(h->sectionSize(FileListModel::NameColumn) != 150 || t->detailsView()->viewport()->width() < 400);
    }

    void expandableFolders()
    {
        const QString a = root + QStringLiteral("/a");
        touch(a + QStringLiteral("/sub/x.txt"));
        touch(a + QStringLiteral("/sub/deep/y.txt"));
        FolderTab *t = w->display(0)->currentTab();
        t->refresh();
        waitListed(t, 4);
        QAbstractItemModel *m = t->view()->model();
        auto names = [m]() {
            QStringList out;
            for (int r = 0; r < m->rowCount(); ++r)
                out << m->index(r, 0).data().toString();
            return out;
        };
        auto rowOf = [m](const QString &name) {
            for (int r = 0; r < m->rowCount(); ++r) {
                if (m->index(r, 0).data().toString() == name)
                    return m->index(r, 0);
            }
            return QModelIndex();
        };
        QCOMPARE(rowOf(QStringLiteral("sub")).data(FileListModel::ExpandRole).toInt(), 0);
        QCOMPARE(rowOf(QStringLiteral("one.txt")).data(FileListModel::ExpandRole).toInt(), -1);

        // Open in place: the contents sit under the folder, indented.
        t->selectPaths({a + QStringLiteral("/sub")});
        QVERIFY(w->runCommand(QStringLiteral("Set EXPAND=toggle")));
        QTRY_COMPARE(m->rowCount(), 6);
        QCOMPARE(names(), (QStringList{QStringLiteral("sub"), QStringLiteral("deep"), QStringLiteral("x.txt"),
                                       QStringLiteral("one.txt"), QStringLiteral("three.txt"), QStringLiteral("two.txt")}));
        QCOMPARE(rowOf(QStringLiteral("x.txt")).data(FileListModel::DepthRole).toInt(), 1);
        QCOMPARE(rowOf(QStringLiteral("sub")).data(FileListModel::ExpandRole).toInt(), 1);
        QVERIFY(t->expandFolder(a + QStringLiteral("/sub/deep")));
        QTRY_COMPARE(m->rowCount(), 7);
        QCOMPARE(rowOf(QStringLiteral("y.txt")).data(FileListModel::DepthRole).toInt(), 2);

        // Sorting the other way keeps everything under its folder.
        auto *details = t->detailsView();
        details->sortByColumn(FileListModel::NameColumn, Qt::DescendingOrder);
        QCOMPARE(names(), (QStringList{QStringLiteral("sub"), QStringLiteral("deep"), QStringLiteral("y.txt"), QStringLiteral("x.txt"),
                                       QStringLiteral("two.txt"), QStringLiteral("three.txt"), QStringLiteral("one.txt")}));
        details->sortByColumn(FileListModel::NameColumn, Qt::AscendingOrder);

        // Changes on disk inside an opened folder show up.
        touch(a + QStringLiteral("/sub/new.txt"));
        QTRY_VERIFY(rowOf(QStringLiteral("new.txt")).isValid());
        QCOMPARE(rowOf(QStringLiteral("new.txt")).data(FileListModel::DepthRole).toInt(), 1);
        // A refresh of the folder itself keeps them open.
        touch(a + QStringLiteral("/four.txt"));
        QTRY_VERIFY(rowOf(QStringLiteral("four.txt")).isValid());
        QVERIFY(rowOf(QStringLiteral("y.txt")).isValid());
        // Commands see the real paths.
        t->selectPaths({a + QStringLiteral("/sub/deep/y.txt")});
        QCOMPARE(t->selectedPaths(), QStringList{a + QStringLiteral("/sub/deep/y.txt")});

        // Left goes to the folder an item is in, then closes it.
        details->setFocus();
        QTest::keyClick(details, Qt::Key_Left);
        QCOMPARE(t->selectedPaths(), QStringList{a + QStringLiteral("/sub/deep")});
        QTest::keyClick(details, Qt::Key_Left);
        QTRY_VERIFY(!rowOf(QStringLiteral("y.txt")).isValid());
        QVERIFY(!t->isFolderExpanded(a + QStringLiteral("/sub/deep")));
        QTest::keyClick(details, Qt::Key_Right);
        QTRY_VERIFY(rowOf(QStringLiteral("y.txt")).isValid());

        // The triangle: opens and closes, without opening the folder.
        const QModelIndex sub = rowOf(QStringLiteral("sub"));
        const QRect r = details->visualRect(sub);
        const QPoint tri(r.left() + details->iconSize().width() / 2, r.center().y());
        QTest::mouseClick(details->viewport(), Qt::LeftButton, {}, tri);
        QTRY_COMPARE(m->rowCount(), 5); // sub closed (with deep inside it)
        QVERIFY(!t->isFolderExpanded(a + QStringLiteral("/sub/deep")));
        QTest::mouseDClick(details->viewport(), Qt::LeftButton, {}, tri);
        QCOMPARE(t->path(), a); // a double-click on the triangle does not go in
        QVERIFY(!t->isFolderExpanded(a + QStringLiteral("/sub"))); // two clicks: open, then closed again
        QTest::mouseClick(details->viewport(), Qt::LeftButton, {}, tri);
        QTRY_VERIFY(t->isFolderExpanded(a + QStringLiteral("/sub")));

        // Grouped: contents stay with their folder.
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=type")));
        QTRY_VERIFY(rowOf(QStringLiteral("x.txt")).isValid());
        QCOMPARE(rowOf(QStringLiteral("x.txt")).data(FileListModel::GroupLabelRole).toString(),
                 rowOf(QStringLiteral("sub")).data(FileListModel::GroupLabelRole).toString());
        QVERIFY(w->runCommand(QStringLiteral("Set GROUPBY=none")));

        // Other views, flat view and a new folder close them all.
        QVERIFY(w->runCommand(QStringLiteral("Set VIEW=list")));
        QVERIFY(t->expandedFolders().isEmpty());
        QCOMPARE(m->rowCount(), 5);
        QVERIFY(w->runCommand(QStringLiteral("Set VIEW=details")));
        QVERIFY(!runFailing(QStringLiteral("Set EXPAND=toggle"))); // nothing selected that is a folder? one.txt
        t->selectPaths({a + QStringLiteral("/sub")});
        QVERIFY(w->runCommand(QStringLiteral("Set EXPAND=on")));
        QTRY_VERIFY(rowOf(QStringLiteral("x.txt")).isValid());
        t->navigate(root + QStringLiteral("/b"));
        waitListed(t, 1);
        QVERIFY(t->expandedFolders().isEmpty());
    }

    void undoAndFileLog()
    {
        FolderTab *t = w->display(0)->currentTab();
        // Copy to the destination, then Ctrl+Z.
        t->selectPaths({root + QStringLiteral("/a/one.txt")});
        QVERIFY(w->runCommand(QStringLiteral("Copy")));
        m_app->jobs().waitForIdle(5000);
        QCoreApplication::processEvents();
        QVERIFY(QFile::exists(root + QStringLiteral("/b/one.txt")));
        QVERIFY(m_app->engine().history().latest());
        QTest::keyClick(w, Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY(!QFile::exists(root + QStringLiteral("/b/one.txt")));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/one.txt")));

        // New folders come out again (to the trash).
        QVERIFY(w->runCommand(QStringLiteral("CreateFolder NAME=\"Made, Also\"")));
        QVERIFY(QFileInfo(root + QStringLiteral("/a/Made")).isDir());
        QCOMPARE(m_app->engine().history().latest()->title, QStringLiteral("New folders (2)"));
        QVERIFY(w->runCommand(QStringLiteral("Undo")));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/a/Made")));
        QVERIFY(!QFileInfo::exists(root + QStringLiteral("/a/Also")));

        // Inline renames are undoable too, from the Undo List.
        QVERIFY(w->runCommand(QStringLiteral("Rename FROM=\"%1\" TO=uno.txt").arg(root + QStringLiteral("/a/one.txt"))));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/uno.txt")));
        const int id = m_app->engine().history().latest()->id;
        QVERIFY(w->runCommand(QStringLiteral("Undo ID=%1").arg(id)));
        QVERIFY(QFile::exists(root + QStringLiteral("/a/one.txt")));
        QVERIFY(!runFailing(QStringLiteral("Undo ID=424242")));

        // The History window: the list, and the log with a filter.
        QVERIFY(w->runCommand(QStringLiteral("Undo LOG FIND=one.txt")));
        HistoryDialog *dlg = w->findChild<HistoryDialog *>();
        QVERIFY(dlg);
        QVERIFY(dlg->visibleLogRows() >= 3); // copy, its undo, the rename and its undo
        dlg->setLogFilter(QStringLiteral("no-such-file"));
        QCOMPARE(dlg->visibleLogRows(), 0);
        QVERIFY(dlg->undoList()->count() >= 3);
        // Undo from the window: the newest (the undone rename) comes back.
        dlg->undoList()->item(0)->setSelected(true);
        const auto buttons = dlg->findChildren<QPushButton *>();
        for (QPushButton *b : buttons) {
            if (b->text() == QStringLiteral("Undo Selected"))
                b->click();
        }
        QTRY_VERIFY(QFile::exists(root + QStringLiteral("/a/uno.txt")));
        dlg->close();
    }

    void selectByComparing()
    {
        // a: one, two, three. b: keep, plus an older two and the same three.
        const QString a = root + QStringLiteral("/a"), b = root + QStringLiteral("/b");
        auto age = [](const QString &p, int days) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(QDateTime::currentDateTime().addDays(-days), QFileDevice::FileModificationTime));
        };
        touch(b + QStringLiteral("/two.txt"));
        QVERIFY(QFile::copy(a + QStringLiteral("/three.txt"), b + QStringLiteral("/three.txt")));
        age(b + QStringLiteral("/two.txt"), 5);
        age(a + QStringLiteral("/three.txt"), 3);
        age(b + QStringLiteral("/three.txt"), 3);
        FolderTab *t = w->display(0)->currentTab();
        FolderTab *other = w->display(1)->currentTab();
        t->refresh();
        other->refresh();
        waitListed(other, 3);
        auto names = [](const QStringList &paths) {
            QStringList out;
            for (const QString &p : paths)
                out << QFileInfo(p).fileName();
            out.sort();
            return out;
        };
        // The refreshed listings (with the new dates) can land a moment later.
        QTRY_COMPARE((w->runCommand(QStringLiteral("Select COMPARE=newerormissing")), names(t->selectedPaths())),
                     (QStringList{QStringLiteral("one.txt"), QStringLiteral("two.txt")}));
        QVERIFY(w->runCommand(QStringLiteral("Select COMPARE=missing")));
        QCOMPARE(names(t->selectedPaths()), QStringList{QStringLiteral("one.txt")});
        QVERIFY(w->runCommand(QStringLiteral("Select COMPARE=same BOTH")));
        QCOMPARE(names(t->selectedPaths()), QStringList{QStringLiteral("three.txt")});
        QCOMPARE(names(other->selectedPaths()), QStringList{QStringLiteral("three.txt")});
        // Add and deselect.
        QVERIFY(w->runCommand(QStringLiteral("Select PATTERN=one* ADD")));
        QCOMPARE(names(t->selectedPaths()), (QStringList{QStringLiteral("one.txt"), QStringLiteral("three.txt")}));
        QVERIFY(w->runCommand(QStringLiteral("Select PATTERN=three* DESELECT")));
        QCOMPARE(names(t->selectedPaths()), QStringList{QStringLiteral("one.txt")});
        QVERIFY(w->runCommand(QStringLiteral("Select OLDER=2")));
        QCOMPARE(names(t->selectedPaths()), QStringList{QStringLiteral("three.txt")});
        QVERIFY(!runFailing(QStringLiteral("Select COMPARE=bigger")));
        QVERIFY(!runFailing(QStringLiteral("Select KIND=spaceship")));
        // Not without the other display.
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=off")));
        QVERIFY(!runFailing(QStringLiteral("Select COMPARE=newer")));
        QVERIFY(w->runCommand(QStringLiteral("Set DUAL=on")));

        // The window builds the same commands.
        QVERIFY(w->runCommand(QStringLiteral("Select DIALOG")));
        SelectDialog *dlg = w->findChild<SelectDialog *>();
        QVERIFY(dlg);
        dlg->patternEdit()->setText(QStringLiteral("*.txt; my file*"));
        dlg->compareBox()->setCurrentIndex(dlg->compareBox()->findData(QStringLiteral("newer")));
        dlg->bothBox()->setChecked(true);
        QCOMPARE(dlg->command(QStringLiteral("ADD")), QStringLiteral("Select PATTERN=\"*.txt; my file*\" COMPARE=newer BOTH ADD"));
        dlg->close();
    }

    void findAsYouType()
    {
        FolderTab *t = w->display(0)->currentTab();
        QAbstractItemView *v = t->view();
        v->setFocus();
        // As a keyboard would: the first key to the list, the rest to the field it opens.
        auto type = [&t](const QString &text) {
            QTest::keyClicks(t->view(), text.left(1));
            if (text.size() > 1)
                QTest::keyClicks(t->typeAheadEdit(), text.mid(1));
        };
        // Typing jumps to the first name that starts with it.
        QTest::keyClicks(v, QStringLiteral("t"));
        QVERIFY(t->typeAheadOpen());
        QLineEdit *edit = t->typeAheadEdit();
        QTest::keyClicks(edit, QStringLiteral("h"));
        QCOMPARE(t->selectedPaths(), QStringList{root + QStringLiteral("/a/three.txt")});
        // Down: the next match ("t" alone would also match two.txt).
        QTest::keyClick(edit, Qt::Key_Backspace);
        QCOMPARE(edit->text(), QStringLiteral("t"));
        const QString first = t->selectedPaths().value(0);
        QTest::keyClick(edit, Qt::Key_Down);
        QVERIFY(t->selectedPaths().value(0) != first);
        QTest::keyClick(edit, Qt::Key_Escape);
        QVERIFY(!t->typeAheadOpen());

        // Contains, when nothing starts with it.
        type(QStringLiteral("wo"));
        QCOMPARE(t->selectedPaths(), QStringList{root + QStringLiteral("/a/two.txt")});
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_Escape);

        // Wildcards select every match.
        type(QStringLiteral("*e*"));
        QCOMPARE(t->typeAheadMode(), QStringLiteral("select"));
        QCOMPARE(t->selectedPaths().size(), 2); // one, three
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_Return);
        QVERIFY(!t->typeAheadOpen());
        QCOMPARE(t->selectedPaths().size(), 2);

        // ">" runs a command.
        type(QStringLiteral(">Set VIEW=list"));
        QCOMPARE(t->typeAheadMode(), QStringLiteral("command"));
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_Return);
        QCOMPARE(t->viewMode(), FolderTab::ViewMode::List);
        QVERIFY(w->runCommand(QStringLiteral("Set VIEW=details")));
        // Tab completes a command name.
        type(QStringLiteral(">Syn"));
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_Tab);
        QVERIFY(t->typeAheadEdit()->text().startsWith(QStringLiteral(">Sync")));
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_Escape);

        // "/" or "~" goes to an alias or a path.
        QVERIFY(m_app->aliases().setUserAlias(QStringLiteral("bee"), root + QStringLiteral("/b")));
        type(QStringLiteral("/bee"));
        QCOMPARE(t->typeAheadMode(), QStringLiteral("go"));
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_Return);
        QTRY_COMPARE(w->display(0)->currentTab()->path(), root + QStringLiteral("/b"));
        t = w->display(0)->currentTab();
        t->navigate(root + QStringLiteral("/a"));
        waitListed(t, 3);

        // Ctrl+F turns it into the filter bar.
        type(QStringLiteral("one"));
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!t->typeAheadOpen());
        QVERIFY(t->filterBarVisible());
        QCOMPARE(t->filterText(), QStringLiteral("*one*"));
        QCOMPARE(t->view()->model()->rowCount(), 1);
        t->setFilterBarVisible(false);

        // Enter opens what was found.
        touch(root + QStringLiteral("/a/zfolder/inside.txt"));
        t->refresh();
        waitListed(t, 4);
        t->focusView();
        type(QStringLiteral("zf"));
        QTest::keyClick(t->typeAheadEdit(), Qt::Key_Return);
        QTRY_COMPARE(t->path(), root + QStringLiteral("/a/zfolder"));
    }

    void launchTargets()
    {
        const QString a = root + QStringLiteral("/a");
        Launch::Target t = Launch::resolve(a, QString(), &m_app->aliases());
        QCOMPARE(t.folder, a);
        QVERIFY(t.select.isEmpty());
        // A file: its folder, with it selected ("Show in folder").
        t = Launch::resolve(QStringLiteral("file://") + a + QStringLiteral("/one.txt"), QString(), &m_app->aliases());
        QCOMPARE(t.folder, a);
        QCOMPARE(t.select, a + QStringLiteral("/one.txt"));
        // Relative to where it was started, and percent-encoded links.
        touch(root + QStringLiteral("/with space/x.txt"));
        t = Launch::resolve(QStringLiteral("with space"), root, &m_app->aliases());
        QCOMPARE(t.folder, root + QStringLiteral("/with space"));
        t = Launch::resolve(QUrl::fromLocalFile(root + QStringLiteral("/with space")).toString(), QString(), &m_app->aliases());
        QCOMPARE(t.folder, root + QStringLiteral("/with space"));
        // Aliases, and something that is not there: the nearest folder that is.
        QVERIFY(m_app->aliases().setUserAlias(QStringLiteral("roms"), a));
        QCOMPARE(Launch::resolve(QStringLiteral("/roms"), QString(), &m_app->aliases()).folder, a);
        QCOMPARE(Launch::resolve(a + QStringLiteral("/not/there"), QString(), &m_app->aliases()).folder, a);
        // Servers stay as they are.
        QCOMPARE(Launch::resolve(QStringLiteral("sftp://deck@unraid/mnt/user"), QString(), nullptr).folder,
                 QStringLiteral("sftp://deck@unraid/mnt/user"));
        QCOMPARE(Launch::resolve(QStringLiteral("~"), QString(), nullptr).folder, QDir::homePath());
    }

    void secondLaunchHandsOver()
    {
        QTemporaryDir run;
        const QByteArray oldRuntime = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", run.path().toUtf8());
        qunsetenv("FLATPAK_ID");
        QCOMPARE(Launch::socketPath(), run.path() + QStringLiteral("/linopus.sock"));
        // Nobody running yet.
        QVERIFY(!Launch::sendToRunning(Launch::Request(), 300));

        auto server = std::make_unique<InstanceServer>();
        QVERIFY(server->listen());
        QList<Launch::Request> got;
        connect(server.get(), &InstanceServer::requested, this, [this, &got](const Launch::Request &r) {
            got << r;
            m_app->handleLaunch(r);
        });
        auto send = [](const Launch::Request &r) {
            // The client waits for an answer, so it cannot share the server's thread.
            QFuture<bool> f = QtConcurrent::run([r]() { return Launch::sendToRunning(r, 3000); });
            while (!f.isFinished())
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return f.result();
        };

        // A file: a new lister on its folder, the file selected.
        const int before = m_app->listerCount();
        Launch::Request r;
        r.args = {QStringLiteral("a/two.txt")};
        r.cwd = root;
        QVERIFY(send(r));
        QTRY_COMPARE(got.size(), 1);
        QCOMPARE(got.first().cwd, root);
        QCOMPARE(m_app->listerCount(), before + 1);
        ListerWindow *opened = m_app->activeLister();
        QTRY_COMPARE(opened->sourceDisplay()->currentTab()->selectedPaths(), QStringList{root + QStringLiteral("/a/two.txt")});

        // --tab: a tab in that lister. --cmd: run in it.
        r = {};
        r.args = {root + QStringLiteral("/b")};
        r.tab = true;
        r.commands = {QStringLiteral("Set VIEW=list")};
        QVERIFY(send(r));
        QTRY_COMPARE(got.size(), 2);
        QCOMPARE(m_app->listerCount(), before + 1);
        QCOMPARE(opened->sourceDisplay()->tabCount(), 2);
        QCOMPARE(opened->sourceDisplay()->currentTab()->path(), root + QStringLiteral("/b"));
        QCOMPARE(opened->sourceDisplay()->currentTab()->viewMode(), FolderTab::ViewMode::List);
        opened->close();

        // A second server cannot take over the running one's socket; a
        // stale one (after a crash) is taken.
        InstanceServer other;
        QVERIFY(!other.listen());
        server.reset();
        QFile stale(run.path() + QStringLiteral("/linopus.sock"));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        QVERIFY(other.listen());
        qputenv("XDG_RUNTIME_DIR", oldRuntime);
    }

    void metadataEditing()
    {
        // Three podcast episodes as WAV files, and a photo.
        const QString dir = root + QStringLiteral("/psr");
        QDir().mkpath(dir);
        auto wav = [](const QString &path) {
            const quint32 rate = 8000, bytes = rate * 2;
            QByteArray b("RIFF");
            auto u32 = [&b](quint32 v) { for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xff)); };
            auto u16 = [&b](quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); };
            u32(36 + bytes);
            b.append("WAVEfmt ");
            u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
            b.append("data");
            u32(bytes);
            b.append(QByteArray(int(bytes), '\0'));
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(b);
        };
        for (int i = 1; i <= 3; ++i)
            wav(dir + QStringLiteral("/PSR %1.wav").arg(100 + i));
        QImage img(16, 16, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(dir + QStringLiteral("/car.jpg"), "JPEG"));
        FolderTab *t = w->display(0)->currentTab();
        t->navigate(dir);
        waitListed(t, 4);
        if (!Metadata::canWrite(dir + QStringLiteral("/PSR 101.wav")))
            QSKIP("Built without TagLib");

        // The command: fields on every selected file, tracks in list order.
        t->selectPaths({dir + QStringLiteral("/PSR 101.wav"), dir + QStringLiteral("/PSR 102.wav"), dir + QStringLiteral("/PSR 103.wav")});
        QVERIFY(w->runCommand(QStringLiteral("Metadata ALBUM=\"Passenger Seat Radio\" GENRE=Podcast YEAR=2026 TRACK=auto START=101")));
        QCOMPARE(Metadata::read(dir + QStringLiteral("/PSR 103.wav")).track, 103);
        QCOMPARE(Metadata::read(dir + QStringLiteral("/PSR 101.wav")).album, QStringLiteral("Passenger Seat Radio"));
        QVERIFY(m_app->engine().history().latest()->title.startsWith(QStringLiteral("Set album, genre, year, track on 3 files")));
        // Undo puts them back.
        QVERIFY(w->runCommand(QStringLiteral("Undo")));
        QVERIFY(Metadata::read(dir + QStringLiteral("/PSR 102.wav")).album.isEmpty());
        QCOMPARE(Metadata::read(dir + QStringLiteral("/PSR 102.wav")).track, 0);
        QVERIFY(w->runCommand(QStringLiteral("Undo"))); // and redo
        QCOMPARE(Metadata::read(dir + QStringLiteral("/PSR 102.wav")).track, 102);

        // Copy one episode's tags, paste onto a new one.
        wav(dir + QStringLiteral("/PSR 104.wav"));
        t->selectPaths({dir + QStringLiteral("/PSR 101.wav")});
        QVERIFY(w->runCommand(QStringLiteral("Metadata COPY")));
        QVERIFY(w->runCommand(QStringLiteral("Metadata PASTE FILE=\"%1\"").arg(dir + QStringLiteral("/PSR 104.wav"))));
        const MediaInfo pasted = Metadata::read(dir + QStringLiteral("/PSR 104.wav"));
        QCOMPARE(pasted.album, QStringLiteral("Passenger Seat Radio"));
        QCOMPARE(pasted.track, 0); // not without ALL

        // Nothing writable selected.
        touch(dir + QStringLiteral("/notes.txt"));
        QVERIFY(!runFailing(QStringLiteral("Metadata ALBUM=x FILE=\"%1\"").arg(dir + QStringLiteral("/notes.txt"))));
        QVERIFY(!runFailing(QStringLiteral("Metadata YEAR=soon")));

        // The pane: shared values show, differing ones say so; typing writes to all.
        t->refresh();
        waitListed(t, 6);
        QVERIFY(w->runCommand(QStringLiteral("Metadata PANE=on")));
        MetadataPane *pane = w->metadataPane();
        QVERIFY(pane->isVisible());
        t->selectPaths({dir + QStringLiteral("/PSR 101.wav"), dir + QStringLiteral("/PSR 102.wav")});
        QTRY_COMPARE(pane->files().size(), 2);
        pane->loadNow();
        QCOMPARE(pane->field(MetadataPane::Album)->text(), QStringLiteral("Passenger Seat Radio"));
        QVERIFY(pane->field(MetadataPane::Track)->text().isEmpty());
        QCOMPARE(pane->field(MetadataPane::Track)->placeholderText(), QStringLiteral("(several values)"));
        QVERIFY(pane->field(MetadataPane::Track)->isReadOnly()); // one number for two files makes no sense
        QVERIFY(!pane->isFieldShown(MetadataPane::Taken));
        QTest::keyClicks(pane->field(MetadataPane::Artist), QStringLiteral("Shane"));
        QVERIFY(pane->hasChanges());
        QCOMPARE(pane->apply(), 2);
        QCOMPARE(Metadata::read(dir + QStringLiteral("/PSR 102.wav")).artist, QStringLiteral("Shane"));
        QCOMPARE(Metadata::read(dir + QStringLiteral("/PSR 102.wav")).track, 102); // untouched fields stay
        if (Metadata::canWrite(dir + QStringLiteral("/car.jpg"))) {
            // A photo: its own fields; SHIFT moves the date taken.
            t->selectPaths({dir + QStringLiteral("/car.jpg")});
            QTRY_COMPARE(pane->files(), QStringList{dir + QStringLiteral("/car.jpg")});
            pane->loadNow();
            QVERIFY(pane->isFieldShown(MetadataPane::Taken));
            QVERIFY(!pane->isFieldShown(MetadataPane::Album));
            QTest::keyClicks(pane->field(MetadataPane::Taken), QStringLiteral("2026-10-21 18:15:00"));
            QCOMPARE(pane->apply(), 1);
            QVERIFY(w->runCommand(QStringLiteral("Metadata SHIFT=-1h")));
            QCOMPARE(Metadata::read(dir + QStringLiteral("/car.jpg")).taken, QDateTime(QDate(2026, 10, 21), QTime(17, 15)));
        }
        QVERIFY(w->runCommand(QStringLiteral("Metadata PANE=off")));
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
