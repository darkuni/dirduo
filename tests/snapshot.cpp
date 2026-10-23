// Builds a lister over a sample folder tree, drives a few commands, and saves
// a PNG of the window at each step. Lets the UI be checked without a desktop:
//   QT_QPA_PLATFORM=offscreen ./linopus_snapshot out-dir

#include "core/AliasManager.h"
#include "core/Collections.h"
#include "core/JobManager.h"
#include "core/Location.h"
#include "core/Paths.h"
#include "core/StatusFormat.h"
#include "core/Prefs.h"
#include "ui/AppContext.h"
#include "ui/CollisionDialog.h"
#include "ui/DrivesBar.h"
#include "ui/ToolbarStrip.h"
#include "ui/FileDisplay.h"
#include "ui/AdvancedRenameDialog.h"
#include "ui/ButtonBar.h"
#include "ui/PreferencesDialog.h"
#include "ui/ViewerPane.h"
#include "ui/ImageConvertDialog.h"
#include "ui/DuplicatesDialog.h"
#include "ui/SyncDialog.h"
#include "ui/ButtonEditor.h"
#include "ui/CommandConsole.h"
#include "ui/CustomizeDialog.h"
#include "ui/FolderTab.h"
#include "ui/FolderTree.h"
#include "ui/GroupTreeView.h"
#include "ui/ListerWindow.h"
#include "ui/ProgressWindow.h"
#include "ui/Style.h"
#ifdef LINOPUS_HAVE_KIO
#include "kio/KioBackend.h"
#endif

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QIcon>
#include <QTemporaryDir>
#include <QTest>
#include <QLineEdit>
#include <QPainter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTimer>

static void writeFile(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QByteArray(static_cast<int>(size), 'x'));
}

static void shot(QWidget *w, const QString &dir, const QString &name)
{
    QTest::qWait(400);
    w->grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    AppContext::setupIconTheme();
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    QDir().mkpath(out);

    QTemporaryDir sandbox;
    Paths::setConfigDirOverride(sandbox.filePath(QStringLiteral("config")));
    Paths::setDataHomeOverride(sandbox.filePath(QStringLiteral("data")));
    const QString root = sandbox.filePath(QStringLiteral("deck"));
    writeFile(root + QStringLiteral("/Music/Joan Jett/Bad Reputation.mp3"), 4200000);
    writeFile(root + QStringLiteral("/Music/Joan Jett/I Love Rock n Roll.mp3"), 3900000);
    writeFile(root + QStringLiteral("/Music/audio1.mp3"), 1000);
    writeFile(root + QStringLiteral("/Music/audio2.mp3"), 1000);
    writeFile(root + QStringLiteral("/Music/audio10.mp3"), 1000);
    writeFile(root + QStringLiteral("/Pictures/IMG0001.JPG"), 2500000);
    writeFile(root + QStringLiteral("/Pictures/IMG0002.JPG"), 2600000);
    writeFile(root + QStringLiteral("/Documents/Dark Unicorn Dispatch 1994.txt"), 12000);
    writeFile(root + QStringLiteral("/Documents/notes.md"), 800);
    writeFile(root + QStringLiteral("/.hidden-config"), 10);
    QDir().mkpath(root + QStringLiteral("/Emulation/roms/c64"));
    QDir().mkpath(root + QStringLiteral("/Backup"));

#ifdef LINOPUS_HAVE_KIO
    KioBackend kio;
#endif
    AppContext context;
    ListerWindow *w = context.newLister(root);
    w->resize(1280, 800); // Steam Deck screen

    // No SD card in a build box: show what the Deck would have.
    QDir().mkpath(root + QStringLiteral("/media/Games/roms"));
    QDir().mkpath(root + QStringLiteral("/media/KINGSTON"));
    QList<DriveEntry> drives = w->drivesBar()->drives().mid(0, 2);
    DriveEntry sd;
    sd.kind = DriveEntry::Kind::SdCard;
    sd.name = QStringLiteral("Games");
    sd.path = root + QStringLiteral("/media/Games");
    sd.bytesFree = 120LL * 1024 * 1024 * 1024;
    sd.bytesTotal = 476LL * 1024 * 1024 * 1024;
    DriveEntry usb = sd;
    usb.kind = DriveEntry::Kind::Usb;
    usb.name = QStringLiteral("KINGSTON");
    usb.path = root + QStringLiteral("/media/KINGSTON");
    drives << sd << usb;
    w->drivesBar()->setDrivesForTesting(drives);
    w->display(1)->currentTab()->navigate(root + QStringLiteral("/Backup"));
    w->display(0)->currentTab()->navigate(root + QStringLiteral("/Music"));
    QTest::qWait(500);
    shot(w, out, QStringLiteral("01-dual"));

    w->display(0)->currentTab()->selectPaths({root + QStringLiteral("/Music/Joan Jett"), root + QStringLiteral("/Music/audio2.mp3")});
    shot(w, out, QStringLiteral("02-selection"));

    w->runCommand(QStringLiteral("Copy"));
    context.jobs().waitForIdle(5000);
    QTest::qWait(500);
    shot(w, out, QStringLiteral("03-after-copy"));

    w->runCommand(QStringLiteral("Go \"") + root + QStringLiteral("/media/Games\""));
    shot(w, out, QStringLiteral("03b-on-sd-card"));
    w->runCommand(QStringLiteral("Go \"") + root + QStringLiteral("/Music\""));

    w->runCommand(QStringLiteral("Set DUAL=horizontal\nSet VIEW=icons"));
    w->display(0)->currentTab()->navigate(root + QStringLiteral("/Pictures"));
    shot(w, out, QStringLiteral("04-stacked-icons"));

    w->runCommand(QStringLiteral("Set DUAL=off\nSet VIEW=details\nSet HIDDEN=on\nGo NEWTAB PATH=") + root);
    w->runCommand(QStringLiteral("Go \"") + root + QStringLiteral("/Documents\" NEWTAB"));
    shot(w, out, QStringLiteral("05-single-tabs"));

    w->runCommand(QStringLiteral("Set DUAL=vertical\nSet DESTLOCK=on"));
    w->display(0)->currentTab()->navigate(root + QStringLiteral("/Music"));
    QTest::qWait(400);
    w->display(0)->currentTab()->selectPaths({root + QStringLiteral("/Music/audio1.mp3")});
    w->runCommand(QStringLiteral("Rename"));
    shot(w, out, QStringLiteral("06-rename-locked"));

    // Both toolbars stacked between the displays.
    w->runCommand(QStringLiteral("Set DESTLOCK=off\nToolbar NAME=buttons PLACE=between"));
    QTest::qWait(300);
    shot(w, out, QStringLiteral("06b-both-bars-between"));
    w->runCommand(QStringLiteral("Set DUAL=horizontal"));
    shot(w, out, QStringLiteral("06c-both-bars-between-horizontal"));
    w->runCommand(QStringLiteral("Set DUAL=vertical\nToolbar NAME=buttons PLACE=top\nToolbar NAME=drives PLACE=top NEWROW"));
    shot(w, out, QStringLiteral("06d-stacked-at-top"));
    w->runCommand(QStringLiteral("Toolbar NAME=drives PLACE=between"));

    // Collision dialog, rendered on its own.
    writeFile(root + QStringLiteral("/Backup/audio1.mp3"), 900);
    CollisionRequest req;
    req.jobId = 1;
    req.source = root + QStringLiteral("/Music/audio1.mp3");
    req.target = root + QStringLiteral("/Backup/audio1.mp3");
    req.suggestedName = QStringLiteral("audio1 (2).mp3");
    CollisionDialog dlg(req, 0, w);
    dlg.adjustSize();
    dlg.show();
    QTest::qWait(300);
    dlg.grab().save(out + QStringLiteral("/07-collision.png"));
    dlg.hide();

    // A 200 MB copy paused partway, plus a second job queued behind it on the
    // same drive. Each gets its own progress window.
    w->runCommand(QStringLiteral("Set DESTLOCK=off"));
    writeFile(root + QStringLiteral("/Videos/episode.mkv"), 200 * 1024 * 1024);
    JobSpec big;
    big.type = JobType::Copy;
    big.sources = {root + QStringLiteral("/Videos/episode.mkv")};
    big.destDir = root + QStringLiteral("/Backup");
    const int first = context.jobs().enqueue(big);
    QTest::qWait(60);
    context.jobs().pause(first);
    JobSpec small = big;
    small.sources = {root + QStringLiteral("/Documents/notes.md")};
    const int second = context.jobs().enqueue(small);
    QTest::qWait(900);
    if (ProgressWindow *pw = context.progress().windowFor(first))
        pw->grab().save(out + QStringLiteral("/08-progress-paused.png"));
    if (ProgressWindow *pw = context.progress().windowFor(second))
        pw->grab().save(out + QStringLiteral("/09-progress-queued.png"));
    for (int id : context.jobs().jobIds())
        context.jobs().cancel(id);
    context.jobs().waitForIdle(5000);
    QTest::qWait(200);

    // Phase 2: themes, trees, grouping, folder sizes, flat view, filter, labels.
    writeFile(root + QStringLiteral("/Emulation/roms/c64/Galaga.d64"), 174848);
    writeFile(root + QStringLiteral("/Emulation/roms/c64/Elite.d64"), 174848);
    writeFile(root + QStringLiteral("/Emulation/roms/amiga/Deluxe Galaga.adf"), 901120);
    writeFile(root + QStringLiteral("/Emulation/roms/amiga/Turrican II.adf"), 901120);
    writeFile(root + QStringLiteral("/Emulation/bios/kick13.rom"), 262144);
    writeFile(root + QStringLiteral("/Emulation/README.md"), 2000);
    writeFile(root + QStringLiteral("/Emulation/emulators.tar.gz"), 3400000);
    writeFile(root + QStringLiteral("/Documents/Budget 2026.ods"), 40000);
    writeFile(root + QStringLiteral("/Documents/backup-old.zip"), 900000);
    writeFile(root + QStringLiteral("/Documents/linopus.cpp"), 30000);
    writeFile(root + QStringLiteral("/Pictures/IMG0003.JPG"), 2700000);
    writeFile(root + QStringLiteral("/Pictures/IMG0004.png"), 900000);
    LabelRule star;
    star.pattern = QStringLiteral("*Joan*;*Galaga*");
    star.color = QColor::fromRgb(0xe0, 0x7a, 0x10);
    star.bold = true;
    LabelRule old;
    old.pattern = QStringLiteral("*backup*;*-old*");
    old.color = QColor::fromRgb(0xc0, 0x30, 0x30);
    old.italic = true;
    Style::instance()->setLabels({star, old});

    w->runCommand(QStringLiteral("Toolbar NAME=buttons PLACE=top\nSet DUAL=vertical\nSet HIDDEN=off\nSet THEME=\"Linopus Light\"\nSet TREE=shared"));
    w->sharedTree()->setDrivesForTesting(drives);
    w->display(0)->currentTab()->navigate(root);
    w->display(1)->currentTab()->navigate(root + QStringLiteral("/Music"));
    QTest::qWait(400);
    w->runCommand(QStringLiteral("Set FOLDERSIZES=on\nSet GROUPBY=type"));
    QTest::qWait(1200);
    shot(w, out, QStringLiteral("12-light-tree-grouped-sizes"));

    w->runCommand(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet FOLDERSIZES=off\nSet GROUPBY=none\nSet FLATVIEW=grouped"));
    QTest::qWait(1200);
    shot(w, out, QStringLiteral("13-dark-flat-grouped"));
    w->runCommand(QStringLiteral("Set FILTER=*.mp3;*.d64"));
    QTest::qWait(500);
    shot(w, out, QStringLiteral("14-dark-flat-filtered"));
    w->runCommand(QStringLiteral("Set FILTERBAR=off\nSet FLATVIEW=off"));

    w->runCommand(QStringLiteral("Set THEME=Workbench\nSet TREE=separate"));
    for (int i = 0; i < 2; ++i)
        w->display(i)->tree()->setDrivesForTesting(drives);
    w->display(0)->currentTab()->navigate(root + QStringLiteral("/Emulation/roms/amiga"));
    w->display(1)->currentTab()->navigate(root + QStringLiteral("/Documents"));
    QTest::qWait(500);
    w->runCommand(QStringLiteral("Set CHECKBOXES=on\nSelect ALL"));
    QTest::qWait(500);
    shot(w, out, QStringLiteral("15-workbench-separate-trees"));

    w->runCommand(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet TREE=off\nSet CHECKBOXES=off"));
    w->display(0)->currentTab()->navigate(root + QStringLiteral("/Pictures"));
    QTest::qWait(800);
    shot(w, out, QStringLiteral("16-dark-pictures-auto-icons"));
    // Grouped by type, all folded but the first group.
    w->runCommand(QStringLiteral("Set THEME=\"Linopus Light\"\nSet TREE=shared"));
    w->sharedTree()->setDrivesForTesting(drives);
    w->display(0)->currentTab()->navigate(root);
    QTest::qWait(300);
    w->runCommand(QStringLiteral("Set FLATVIEW=nofolders\nSet GROUPBY=type"));
    QTest::qWait(1200);
    {
        FolderTab *t = w->display(0)->currentTab();
        t->collapseAllGroups();
        t->detailsView()->toggleGroup(t->detailsView()->model()->index(0, 0));
    }
    QTest::qWait(300);
    shot(w, out, QStringLiteral("17-light-groups-folded"));
    w->runCommand(QStringLiteral("Set GROUPBY=none\nSet FLATVIEW=off\nSet TREE=off"));

    // Phase 3: customize mode, the button editor and the command console.
    w->runCommand(QStringLiteral("Toolbar CUSTOMIZE"));
    QTest::qWait(400);
    shot(w, out, QStringLiteral("18-customize-mode"));
    if (CustomizeDialog *dlg = w->customizeDialog()) {
        dlg->findChild<QTabWidget *>()->setCurrentIndex(1);
        dlg->commandTree()->setCurrentItem(dlg->commandTree()->topLevelItem(0)->child(0));
        QTest::qWait(300);
        dlg->grab().save(out + QStringLiteral("/18b-customize-dialog.png"));
        dlg->close();
    }
    {
        ButtonDef menu;
        menu.label = QStringLiteral("Emulation");
        menu.icon = QStringLiteral("input-gaming");
        menu.tip = QStringLiteral("Jump to emulation folders");
        ButtonDef roms;
        roms.label = QStringLiteral("ROMs");
        roms.left = QStringLiteral("Go \"") + root + QStringLiteral("/Emulation/roms\"");
        menu.children = {roms};
        ButtonEditor editor(menu, w);
        editor.show();
        QTest::qWait(300);
        editor.grab().save(out + QStringLiteral("/19-button-editor-menu.png"));
        ButtonDef copy = w->buttonBar(QStringLiteral("buttons"))->buttonAt(0)->definition();
        copy.enableWhen = QStringLiteral("selection && dest");
        ButtonEditor editor2(copy, w);
        editor2.show();
        QTest::qWait(300);
        editor2.grab().save(out + QStringLiteral("/19b-button-editor.png"));
    }
    w->showConsole();
    if (CommandConsole *c = w->console()) {
        c->submit(QStringLiteral("help"));
        c->submit(QStringLiteral("Set VIEW=list"));
        c->submit(QStringLiteral("Set GROUPBY=color"));
        c->input()->setText(QStringLiteral("Go /downloads NEW"));
        QTest::qWait(300);
        c->grab().save(out + QStringLiteral("/20-command-console.png"));
        c->close();
    }
    w->runCommand(QStringLiteral("Set VIEW=details"));

    // Advanced Rename over the Amiga disk images and the Music folder.
    {
        writeFile(root + QStringLiteral("/Emulation/roms/amiga/turrican_ii_disk1.ADF"), 901120);
        writeFile(root + QStringLiteral("/Emulation/roms/amiga/turrican_ii_disk2.ADF"), 901120);
        writeFile(root + QStringLiteral("/Emulation/roms/amiga/Turrican II disk 1.adf"), 901120);
        QStringList paths;
        for (const QString &n : QDir(root + QStringLiteral("/Emulation/roms/amiga")).entryList(QDir::Files, QDir::Name))
            paths << root + QStringLiteral("/Emulation/roms/amiga/") + n;
        RenameOptions o;
        o.mode = RenameOptions::Mode::FindReplace;
        o.oldPattern = QStringLiteral("_");
        o.newPattern = QStringLiteral(" ");
        o.nameCase = RenameOptions::Case::Title;
        o.extCase = RenameOptions::ExtCase::Lower;
        AdvancedRenameDialog dlg(paths, o, w);
        dlg.show();
        QTest::qWait(400);
        dlg.grab().save(out + QStringLiteral("/21-advanced-rename.png"));
    }

    // Synchronize the ROMs on internal storage with the SD card.
    {
        const QString internal = root + QStringLiteral("/Emulation/roms");
        const QString sd = root + QStringLiteral("/media/Games/roms");
        QDir(sd).removeRecursively();
        writeFile(sd + QStringLiteral("/c64/Galaga.d64"), 174848);
        writeFile(sd + QStringLiteral("/c64/Elite.d64"), 170000);
        writeFile(sd + QStringLiteral("/c64/Paradroid.d64"), 174848);
        writeFile(sd + QStringLiteral("/nes/Contra.nes"), 131088);
        // Make Galaga the same age on both sides, Elite older on the card.
        QFile g(sd + QStringLiteral("/c64/Galaga.d64"));
        if (!g.open(QIODevice::ReadWrite))
            qWarning("cannot open sample file");
        g.setFileTime(QFileInfo(internal + QStringLiteral("/c64/Galaga.d64")).lastModified(), QFileDevice::FileModificationTime);
        g.close();
        QFile e(sd + QStringLiteral("/c64/Elite.d64"));
        if (!e.open(QIODevice::ReadWrite))
            qWarning("cannot open sample file");
        e.setFileTime(QDateTime::currentDateTime().addDays(-30), QFileDevice::FileModificationTime);
        e.close();
        SyncOptions o;
        o.mode = SyncOptions::Mode::TwoWay;
        SyncDialog dlg(context.engine(), internal, sd, o, w);
        dlg.show();
        dlg.startCompare();
        for (int i = 0; i < 50 && dlg.isComparing(); ++i)
            QTest::qWait(100);
        QTest::qWait(300);
        dlg.grab().save(out + QStringLiteral("/22-synchronize.png"));
    }
    // Duplicates across the ROM folders.
    {
        writeFile(root + QStringLiteral("/Backup/roms-old/Galaga.d64"), 174848);
        writeFile(root + QStringLiteral("/Backup/roms-old/Turrican II.adf"), 901120);
        writeFile(root + QStringLiteral("/Downloads/galaga (1).d64"), 174848);
        DuplicateOptions o;
        o.minSize = 100 * 1024;
        DuplicatesDialog dlg(context.engine(), {root + QStringLiteral("/Emulation"), root + QStringLiteral("/Backup"), root + QStringLiteral("/Downloads")}, o, w);
        dlg.show();
        dlg.startSearch();
        for (int i = 0; i < 50 && dlg.isSearching(); ++i)
            QTest::qWait(100);
        dlg.checkAllBut(DuplicatesDialog::Keep::ShortestPath);
        QTest::qWait(300);
        dlg.grab().save(out + QStringLiteral("/23-duplicates.png"));
    }

    // Find: every disk image under the deck, results as a collection in a tab.
    {
        w->runCommand(QStringLiteral("Set DUAL=vertical\nSet VIEW=details\nSet THEME=\"Linopus Dark\""));
        w->display(0)->currentTab()->navigate(root);
        QTest::qWait(300);
        w->runCommand(QStringLiteral("Find NAME=*.d64;*.adf IN=\"%1\" RUN").arg(root));
        for (int i = 0; i < 50 && !Collections::isCollectionLocation(w->display(0)->currentTab()->path()); ++i)
            QTest::qWait(100);
        QTest::qWait(600);
        shot(w, out, QStringLiteral("24-find-results-collection"));
        w->runCommand(QStringLiteral("Set THEME=\"Linopus Light\""));
    }

    // Preferences: the theme editor and the labels page.
    {
        PreferencesDialog dlg(context, w, QStringLiteral("theme"));
        dlg.show();
        QTest::qWait(400);
        dlg.grab().save(out + QStringLiteral("/25-preferences-theme.png"));
        dlg.setPage(QStringLiteral("labels"));
        QTest::qWait(300);
        dlg.grab().save(out + QStringLiteral("/26-preferences-labels.png"));
        dlg.reject();
    }

    // Viewer pane: a picture on the right, then an NFO at the bottom.
    {
        QImage pic(1280, 720, QImage::Format_RGB32);
        QPainter pp(&pic);
        QLinearGradient sky(0, 0, 0, 720);
        sky.setColorAt(0, QColor::fromRgb(0x0b, 0x1d, 0x51));
        sky.setColorAt(1, QColor::fromRgb(0xe0, 0x7a, 0x10));
        pp.fillRect(pic.rect(), sky);
        pp.setPen(Qt::NoPen);
        pp.setBrush(QColor::fromRgb(0xff, 0xd2, 0x4a));
        pp.drawEllipse(QPoint(900, 460), 110, 110);
        pp.setBrush(QColor::fromRgb(0x10, 0x10, 0x18));
        pp.drawRect(0, 560, 1280, 160);
        pp.end();
        pic.save(root + QStringLiteral("/Pictures/Sunset over Glendale.png"));
        QFile nfo(root + QStringLiteral("/Documents/DUD-1994.nfo"));
        if (nfo.open(QIODevice::WriteOnly))
            nfo.write("\xC9\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBB\r\n"
                      "\xBA  DARK UNICORN DISPATCH \xBA\r\n"
                      "\xBA   Issue #1  -  1994    \xBA\r\n"
                      "\xBA \xB0\xB1\xB2\xDB Tombstone BBS \xDB\xB2\xB1\xB0 \xBA\r\n"
                      "\xC8\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBC\r\n");
        nfo.close();
        w->runCommand(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet DUAL=vertical\nSet VIEWER=on\nSet VIEWERPOS=right"));
        w->display(0)->currentTab()->navigate(root + QStringLiteral("/Pictures"));
        QTest::qWait(500);
        w->runCommand(QStringLiteral("Set VIEW=details"));
        w->display(0)->currentTab()->selectPaths({root + QStringLiteral("/Pictures/Sunset over Glendale.png")});
        QTest::qWait(900);
        shot(w, out, QStringLiteral("27-viewer-picture"));
        w->runCommand(QStringLiteral("Set VIEWERPOS=bottom"));
        w->display(0)->currentTab()->navigate(root + QStringLiteral("/Documents"));
        QTest::qWait(500);
        w->display(0)->currentTab()->selectPaths({root + QStringLiteral("/Documents/DUD-1994.nfo")});
        QTest::qWait(900);
        shot(w, out, QStringLiteral("28-viewer-nfo"));
        w->runCommand(QStringLiteral("Set VIEWER=off\nSet THEME=\"Linopus Light\""));
    }

    // Convert Images: screenshots going to JPEG at 1280 wide.
    {
        QStringList shots;
        for (int i = 1; i <= 4; ++i) {
            QImage img(1280 * 2, 800 * 2, QImage::Format_RGB32);
            img.fill(QColor::fromHsv(i * 60, 120, 200));
            const QString p = root + QStringLiteral("/Pictures/Screenshot_2026102%1_181500.png").arg(i);
            img.save(p);
            shots << p;
        }
        ImageOptions o;
        o.format = QStringLiteral("jpg");
        o.quality = 85;
        o.resize = ImageOptions::Resize::Width;
        o.width = 1280;
        ImageConvertDialog dlg(shots, o, root + QStringLiteral("/Documents"), w);
        dlg.show();
        QTest::qWait(500);
        dlg.grab().save(out + QStringLiteral("/29-convert-images.png"));
        dlg.reject();
    }

    // Music columns on a folder of C64 SID tunes, grouped by composer, with
    // the viewer pane showing one tune's details.
    {
        const QString sids = root + QStringLiteral("/Music/C64");
        QDir().mkpath(sids);
        struct Tune { const char *file, *name, *author, *released; int songs; };
        const Tune tunes[] = {
            {"Commando.sid", "Commando", "Rob Hubbard", "1985 Elite", 3},
            {"Monty_on_the_Run.sid", "Monty on the Run", "Rob Hubbard", "1985 Gremlin Graphics", 2},
            {"Sanxion.sid", "Sanxion", "Rob Hubbard", "1986 Thalamus", 2},
            {"Last_Ninja.sid", "The Last Ninja", "Ben Daglish", "1987 System 3", 9},
            {"Deflektor.sid", "Deflektor", "Ben Daglish", "1987 Gremlin Graphics", 1},
            {"Cybernoid_II.sid", "Cybernoid II", "Jeroen Tel", "1988 Hewson", 3},
            {"Turrican.sid", "Turrican", "Chris Huelsbeck", "1990 Rainbow Arts", 9},
            {"Dark_Unicorn_Theme.sid", "Dark Unicorn Theme", "Darkuni", "1994 Dark Unicorn", 1},
        };
        for (const Tune &t : tunes) {
            QByteArray h(0x7c, '\0');
            h.replace(0, 4, "PSID");
            h[5] = 2;
            h[0x0f] = char(t.songs);
            h[0x11] = 1;
            h.replace(0x16, int(qstrlen(t.name)), t.name);
            h.replace(0x36, int(qstrlen(t.author)), t.author);
            h.replace(0x56, int(qstrlen(t.released)), t.released);
            QFile f(sids + QLatin1Char('/') + QLatin1String(t.file));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(h);
                f.write(QByteArray(2048 + 997 * int(qstrlen(t.name)), '\x60'));
            }
        }
        w->runCommand(QStringLiteral("Set THEME=Workbench\nSet DUAL=off\nSet VIEWER=on\nSet VIEWERPOS=right"));
        FolderTab *tab = w->display(0)->currentTab();
        tab->navigate(sids);
        QTest::qWait(500);
        w->runCommand(QStringLiteral("Set VIEW=details\nSet COLUMNSTOGGLE=type\nSet COLUMNSTOGGLE=modified\n"
                                     "Set COLUMNSTOGGLE=title\nSet COLUMNSTOGGLE=year\nSet COLUMNSTOGGLE=album\n"
                                     "Set GROUPBY=artist"));
        QTest::qWait(600);
        tab->selectPaths({sids + QStringLiteral("/Last_Ninja.sid")});
        QTest::qWait(900);
        shot(w, out, QStringLiteral("30-music-columns-sid"));
        w->runCommand(QStringLiteral("Set GROUPBY=none\nSet VIEWER=off\nSet THEME=\"Linopus Light\"\nSet DUAL=vertical"));
    }

#ifdef LINOPUS_HAVE_KIO
    // Servers and archives, when run through tests/remote/run-remote-tests.sh.
    const QString ftp = qEnvironmentVariable("LINOPUS_TEST_FTP");
    const QString ftpDir = qEnvironmentVariable("LINOPUS_TEST_FTP_DIR");
    if (!ftp.isEmpty()) {
        writeFile(ftpDir + QStringLiteral("/pub/podcasts/Passenger Seat Radio 101.mp3"), 3 * 1024 * 1024);
        writeFile(ftpDir + QStringLiteral("/pub/podcasts/Passenger Seat Radio 102.mp3"), 3 * 1024 * 1024);
        writeFile(ftpDir + QStringLiteral("/pub/podcasts/cover.jpg"), 90000);
        QDir().mkpath(ftpDir + QStringLiteral("/pub/podcasts/archive"));
        w->runCommand(QStringLiteral("Set DUAL=vertical"));
        w->display(1)->currentTab()->navigate(root + QStringLiteral("/Music"));
        w->display(0)->currentTab()->navigate(ftp + QStringLiteral("/pub/podcasts"));
        QTest::qWait(2500);
        shot(w, out, QStringLiteral("10-ftp-server"));

        QProcess py;
        py.start(QStringLiteral("python3"), {QStringLiteral("-c"),
                                             QStringLiteral("import zipfile,sys\n"
                                                            "z=zipfile.ZipFile(sys.argv[1],'w')\n"
                                                            "z.writestr('c64/Galaga.d64', b'g'*174848)\n"
                                                            "z.writestr('c64/Elite.d64', b'e'*174848)\n"
                                                            "z.writestr('amiga/Deluxe Galaga.adf', b'a'*901120)\n"
                                                            "z.writestr('readme.txt', b'hello')\n"
                                                            "z.close()"),
                                             root + QStringLiteral("/Emulation/roms.zip")});
        py.waitForFinished(10000);
        w->display(0)->currentTab()->navigate(root + QStringLiteral("/Emulation"));
        QTest::qWait(600);
        w->display(0)->currentTab()->selectPaths({root + QStringLiteral("/Emulation/roms.zip")});
        QTest::qWait(300);
        // Double-click equivalent: open the archive as a folder.
        QTest::keyClick(w->display(0)->currentTab()->view(), Qt::Key_Return);
        QTest::qWait(1500);
        w->display(0)->currentTab()->navigate(Location::join(w->display(0)->currentTab()->path(), QStringLiteral("c64")));
        QTest::qWait(1500);
        shot(w, out, QStringLiteral("11-inside-zip"));
    }
#endif

    // Toolbars arranged like an Opus work setup: split buttons with Help at
    // the far end; the drives and a custom toolbar sharing a row; a filter box;
    // view buttons in the menu bar.
    {
        ToolbarStore &store = context.toolbars();
        auto item = [](const char *label, const char *icon, const char *left) {
            ButtonDef d;
            d.label = QString::fromUtf8(label);
            d.icon = QString::fromUtf8(icon);
            d.left = QString::fromUtf8(left);
            return d;
        };
        ButtonDef copy = item("Copy Files", "edit-copy", "Copy");
        copy.split = true;
        copy.children = {item("Copy As...", "edit-copy", "Copy AS"), item("Copy to...", "folder-open", "Copy TO=ask")};
        ButtonDef move = item("Move", "transform-move", "Copy MOVE");
        move.split = true;
        move.children = {item("Move As...", "transform-move", "Copy MOVE AS")};
        ButtonDef del = item("Delete", "edit-delete", "Delete");
        del.split = true;
        del.children = {item("Delete Permanently", "edit-delete", "Delete NORECYCLE")};
        ButtonDef rename = item("Rename", "edit-rename", "Rename");
        rename.split = true;
        rename.children = {item("Advanced Rename...", "edit-rename", "Rename ADVANCED")};
        store.setButtons(QStringLiteral("buttons"),
                         {copy, move, rename, item("New Folder", "folder-new", "CreateFolder"), del, ButtonDef::makeSeparator(),
                          item("Archive", "archive-insert", "Archive CREATE"), item("Properties", "document-properties", "Properties"),
                          ButtonDef::makeSpacer(), item("Help", "help-contents", "Go ~"), item("Clean Junk", "edit-clear", "Clean")});
        const QString work = store.create(QStringLiteral("Work"));
        ButtonDef servers = item("Servers", "network-server", "");
        servers.children = {item("Build box", "network-server", "Go ~"), item("Web host", "network-server", "Go ~")};
        ButtonDef local = item("Local Files", "folder", "");
        local.children = {item("ROMs", "folder", "Go ~")};
        ButtonDef automation = item("Automation", "system-run", "");
        automation.children = {item("Rebuild index", "system-run", "Go ~")};
        store.setButtons(work, {ButtonDef::makeSeparator(), item("Codex", "", "Go ~"), servers, local, automation, item("RDPs", "", "Go ~"),
                                ButtonDef::makeSpacer(), ButtonDef::makeField(QStringLiteral("filter"))});
        const QString views = store.create(QStringLiteral("Views"));
        store.setButtons(views, {item("Details", "view-list-details", "Set VIEW=details"), item("Icons", "view-list-icons", "Set VIEW=icons"),
                                 item("Thumbs", "view-preview", "Set VIEW=thumbnails")});
        store.setStyle(views, QStringLiteral("icons"), 0);
        QTest::qWait(200);
        w->runCommand(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet DUAL=horizontal\nToolbar NAME=buttons PLACE=between\n"
                                     "Toolbar NAME=drives PLACE=between\nToolbar NAME=work PLACE=between SAMEROW\n"
                                     "Toolbar NAME=views PLACE=menubar"));
        QTest::qWait(500);
        shot(w, out, QStringLiteral("40-work-style-toolbars"));
        w->runCommand(QStringLiteral("Set DUAL=vertical"));
        QTest::qWait(500);
        shot(w, out, QStringLiteral("41-work-style-toolbars-side-by-side"));
        w->runCommand(QStringLiteral("Toolbar NAME=work STATE=off\nToolbar NAME=views STATE=off\nSet DUAL=horizontal\nToolbar ARRANGE=opus"));
        QTest::qWait(500);
        shot(w, out, QStringLiteral("42-opus-arrangement"));
        w->runCommand(QStringLiteral("Toolbar CUSTOMIZE"));
        QTest::qWait(300);
        if (CustomizeDialog *dlg = w->customizeDialog()) {
            dlg->findChild<QTabWidget *>()->setCurrentIndex(2);
            QTest::qWait(300);
            dlg->grab().save(out + QStringLiteral("/43-default-toolbars.png"));
            dlg->close();
        }
        {
            Prefs::Values v = Prefs::instance()->values();
            v.statusDefinition = StatusFormat::samples().at(1).second; // Opus style
            Prefs::instance()->setValues(v);
            w->display(0)->currentTab()->selectPaths({});
            QTest::qWait(400);
            shot(w, out, QStringLiteral("44-status-opus-style"));
            PreferencesDialog prefs(context, w, QStringLiteral("statusbar"));
            prefs.resize(860, 620);
            prefs.show();
            QTest::qWait(400);
            prefs.grab().save(out + QStringLiteral("/45-status-preferences.png"));
            v.statusDefinition.clear();
            Prefs::instance()->setValues(v);
        }
        // Opus-style tree: branch lines and the path to the current folder.
        w->runCommand(QStringLiteral("Set DUAL=vertical\nSet TREE=separate"));
        w->display(0)->currentTab()->navigate(root + QStringLiteral("/Music/C64"));
        w->display(1)->currentTab()->navigate(root + QStringLiteral("/Emulation/roms/c64"));
        QTest::qWait(800);
        shot(w, out, QStringLiteral("46-tree-path-lines"));
        {
            FolderTree tree;
            DriveEntry deck;
            deck.kind = DriveEntry::Kind::SdCard;
            deck.name = QStringLiteral("Deck");
            deck.path = root;
            tree.setDrivesForTesting({deck});
            tree.resize(380, 520);
            tree.setPathColor(Style::instance()->theme().destAccent);
            tree.show();
            for (int i = 0; i < 4; ++i) {
                tree.setPath(root + QStringLiteral("/Emulation/roms/c64"));
                QTest::qWait(400);
            }
            tree.grab().save(out + QStringLiteral("/47-tree-closeup.png"));
        }
        w->runCommand(QStringLiteral("Set TREE=off"));
    }

    w->close();
    return 0;
}
