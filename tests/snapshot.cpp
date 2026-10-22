// Builds a lister over a sample folder tree, drives a few commands, and saves
// a PNG of the window at each step. Lets the UI be checked without a desktop:
//   QT_QPA_PLATFORM=offscreen ./linopus_snapshot out-dir

#include "core/AliasManager.h"
#include "core/JobManager.h"
#include "core/Paths.h"
#include "ui/AppContext.h"
#include "ui/CollisionDialog.h"
#include "ui/DrivesBar.h"
#include "ui/ToolbarStrip.h"
#include "ui/FileDisplay.h"
#include "ui/FolderTab.h"
#include "ui/ListerWindow.h"
#include "ui/ProgressWindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QFile>
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

    w->close();
    return 0;
}
