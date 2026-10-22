// Builds a lister over a sample folder tree, drives a few commands, and saves
// a PNG of the window at each step. Lets the UI be checked without a desktop:
//   QT_QPA_PLATFORM=offscreen ./linopus_snapshot out-dir

#include "core/AliasManager.h"
#include "core/JobManager.h"
#include "core/Location.h"
#include "core/Paths.h"
#include "ui/AppContext.h"
#include "ui/CollisionDialog.h"
#include "ui/DrivesBar.h"
#include "ui/ToolbarStrip.h"
#include "ui/FileDisplay.h"
#include "ui/FolderTab.h"
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
    w->runCommand(QStringLiteral("Set THEME=\"Linopus Light\""));

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

    w->close();
    return 0;
}
