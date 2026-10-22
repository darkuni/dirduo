// Showcase screenshots: a Steam Deck's home folder full of believable things,
// shown off in every theme and the main layouts, at the Deck's 1280 x 800.
//   QT_QPA_PLATFORM=offscreen ./linopus_sizzle out-dir
// Big files (Steam games, disc images) are sparse, so this takes no space.
// Builds the Deck at /home/deck and /run/media/deck (like the real thing) and
// removes them again; run it in a throwaway container, not on a real Deck.

#include "core/Collections.h"
#include "core/FolderSync.h"
#include "core/LabelRules.h"
#include "core/Paths.h"
#include "core/RenameEngine.h"
#include "ui/AdvancedRenameDialog.h"
#include "ui/AppContext.h"
#include "ui/CommandConsole.h"
#include "ui/DrivesBar.h"
#include "ui/DuplicatesDialog.h"
#include "ui/FileDisplay.h"
#include "ui/FolderTab.h"
#include "ui/FolderTree.h"
#include "ui/GroupTreeView.h"
#include "ui/ImageConvertDialog.h"
#include "ui/ListerWindow.h"
#include "ui/Style.h"
#include "ui/SyncDialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

namespace {

QString g_out;
QString g_root;
QStringList g_made;

void sparse(const QString &path, qint64 size, int daysAgo = -1)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    if (size < 64 * 1024)
        f.write(QByteArray(int(size), 'x'));
    else
        f.resize(size); // sparse: shows the size, uses no disk
    if (daysAgo >= 0)
        f.setFileTime(QDateTime::currentDateTime().addDays(-daysAgo).addSecs(-3600 * (daysAgo % 7)),
                      QFileDevice::FileModificationTime);
}

void touchAge(const QString &path, int daysAgo)
{
    QFile f(path);
    if (f.open(QIODevice::ReadWrite))
        f.setFileTime(QDateTime::currentDateTime().addDays(-daysAgo), QFileDevice::FileModificationTime);
}

// ---- Pictures that look like game screenshots -----------------------------

void stars(QPainter &p, const QSize &s, QRandomGenerator &rng, int n)
{
    for (int i = 0; i < n; ++i) {
        const int b = 120 + int(rng.bounded(135));
        p.setPen(QColor(b, b, qMin(255, b + 30)));
        p.drawPoint(int(rng.bounded(s.width())), int(rng.bounded(s.height())));
    }
}

QImage synthwave(const QSize &s, int hue)
{
    hue %= 360;
    QImage img(s, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, s.height() * 0.62);
    sky.setColorAt(0, QColor::fromHsv(hue, 200, 40));
    sky.setColorAt(0.6, QColor::fromHsv((hue + 40) % 360, 220, 140));
    sky.setColorAt(1, QColor::fromHsv((hue + 70) % 360, 200, 255));
    p.fillRect(QRect(0, 0, s.width(), int(s.height() * 0.62)), sky);
    QRandomGenerator rng{quint32(hue)};
    stars(p, QSize(s.width(), s.height() / 3), rng, 160);
    // Sun with bands.
    const QPointF c(s.width() * 0.5, s.height() * 0.5);
    const double r = s.height() * 0.22;
    QLinearGradient sun(0, c.y() - r, 0, c.y() + r);
    sun.setColorAt(0, QColor(255, 230, 90));
    sun.setColorAt(1, QColor(255, 60, 140));
    p.setBrush(sun);
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, r, r);
    for (int i = 0; i < 6; ++i)
        p.fillRect(QRectF(0, c.y() + i * r * 0.16, s.width(), r * 0.05 + i * 1.5), sky);
    // Mountains.
    QPainterPath hills;
    hills.moveTo(0, s.height() * 0.62);
    for (int x = 0; x <= s.width(); x += s.width() / 12)
        hills.lineTo(x, s.height() * (0.5 + 0.1 * std::sin(x * 0.013 + hue)));
    hills.lineTo(s.width(), s.height() * 0.62);
    p.setBrush(QColor::fromHsv((hue + 250) % 360, 160, 50));
    p.drawPath(hills);
    // Grid floor.
    p.fillRect(QRect(0, int(s.height() * 0.62), s.width(), s.height()), QColor(14, 4, 30));
    p.setPen(QPen(QColor::fromHsv((hue + 300) % 360, 255, 255), 2));
    const double horizon = s.height() * 0.62;
    for (int i = 0; i < 14; ++i) {
        const double y = horizon + std::pow(i / 13.0, 2.2) * (s.height() - horizon);
        p.drawLine(QPointF(0, y), QPointF(s.width(), y));
    }
    for (int i = -12; i <= 12; ++i)
        p.drawLine(QPointF(s.width() / 2.0 + i * 10, horizon), QPointF(s.width() / 2.0 + i * s.width() / 8.0, s.height()));
    return img;
}

QImage shooter(const QSize &s, int seed)
{
    QImage img(s, QImage::Format_RGB32);
    img.fill(QColor(4, 4, 16));
    QPainter p(&img);
    QRandomGenerator rng{quint32(seed)};
    stars(p, s, rng, 400);
    const int px = qMax(4, s.width() / 160);
    auto sprite = [&](int x, int y, const QColor &body, const QColor &trim) {
        static const char *rows[] = {"...#...", "..###..", ".#####.", "##.#.##", "#######", "#.#.#.#"};
        for (int r = 0; r < 6; ++r)
            for (int c = 0; c < 7; ++c)
                if (rows[r][c] == '#')
                    p.fillRect(x + c * px, y + r * px, px, px, (r == 3 ? trim : body));
    };
    // A formation of invaders, an original design.
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 9; ++col)
            sprite(s.width() / 8 + col * px * 10, s.height() / 8 + row * px * 9,
                   QColor::fromHsv((seed * 40 + row * 50) % 360, 200, 255), QColor(255, 255, 255));
    // The player's ship and shots.
    p.fillRect(s.width() / 2 - px * 3, s.height() - px * 12, px * 6, px * 3, QColor(80, 220, 255));
    p.fillRect(s.width() / 2 - px, s.height() - px * 15, px * 2, px * 3, QColor(80, 220, 255));
    for (int i = 0; i < 4; ++i)
        p.fillRect(s.width() / 2 - px / 2, s.height() - px * (22 + i * 9), px, px * 4, QColor(255, 240, 120));
    QFont f(QStringLiteral("monospace"));
    f.setBold(true);
    f.setPixelSize(px * 4);
    p.setFont(f);
    p.setPen(QColor(255, 80, 80));
    p.drawText(px * 4, px * 6, QStringLiteral("1UP  %1").arg(12340 + seed * 1110, 6, 10, QLatin1Char('0')));
    p.setPen(QColor(255, 255, 255));
    p.drawText(s.width() - px * 44, px * 6, QStringLiteral("HI-SCORE  087650"));
    return img;
}

QImage platformer(const QSize &s, int seed)
{
    QImage img(s, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, s.height());
    sky.setColorAt(0, QColor::fromHsv((200 + seed * 7) % 360, 120, 255));
    sky.setColorAt(1, QColor::fromHsv((190 + seed * 7) % 360, 60, 255));
    p.fillRect(img.rect(), sky);
    QRandomGenerator rng{quint32(seed)};
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 230));
    for (int i = 0; i < 5; ++i) {
        const QPointF c(rng.bounded(s.width()), rng.bounded(s.height() / 3));
        for (int k = 0; k < 3; ++k)
            p.drawEllipse(c + QPointF(k * 28, k == 1 ? -12 : 0), 34, 24);
    }
    p.setBrush(QColor::fromHsv((110 + seed * 9) % 360, 150, 160));
    p.drawEllipse(QPointF(s.width() * 0.25, s.height() * 0.95), s.width() * 0.35, s.height() * 0.35);
    p.setBrush(QColor::fromHsv((100 + seed * 9) % 360, 170, 130));
    p.drawEllipse(QPointF(s.width() * 0.8, s.height()), s.width() * 0.4, s.height() * 0.42);
    const int t = s.height() / 14;
    for (int x = 0; x < s.width(); x += t) {
        p.fillRect(x, s.height() - t * 2, t - 2, t - 2, QColor(170, 90, 40));
        p.fillRect(x, s.height() - t, t - 2, t - 2, QColor(130, 70, 30));
    }
    for (int i = 0; i < 5; ++i)
        p.fillRect(s.width() / 3 + i * t, s.height() / 2, t - 2, t - 2, i == 2 ? QColor(250, 200, 40) : QColor(190, 100, 50));
    p.setBrush(QColor(255, 215, 0));
    for (int i = 0; i < 6; ++i)
        p.drawEllipse(QPointF(s.width() * 0.6 + i * t * 0.9, s.height() * 0.62 - std::sin(i * 0.6) * t), t * 0.25, t * 0.32);
    p.fillRect(int(s.width() * 0.18), s.height() - t * 3 - 4, t, t + 4, QColor(220, 40, 60));
    return img;
}

QImage road(const QSize &s, int seed)
{
    QImage img(s, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, s.height() / 2);
    sky.setColorAt(0, QColor::fromHsv((330 + seed * 9) % 360, 160, 110));
    sky.setColorAt(1, QColor::fromHsv((15 + seed * 5) % 50, 200, 255));
    p.fillRect(QRect(0, 0, s.width(), s.height() / 2), sky);
    p.fillRect(QRect(0, s.height() / 2, s.width(), s.height() / 2), QColor::fromHsv((90 + seed * 5) % 360, 120, 110));
    const double h = s.height() / 2.0;
    for (int i = 0; i < 24; ++i) {
        const double y0 = h + std::pow(i / 24.0, 2) * h, y1 = h + std::pow((i + 1) / 24.0, 2) * h;
        const double w0 = 20 + (y0 - h) * 2.2, w1 = 20 + (y1 - h) * 2.2;
        QPolygonF band({QPointF(s.width() / 2 - w0, y0), QPointF(s.width() / 2 + w0, y0), QPointF(s.width() / 2 + w1, y1),
                        QPointF(s.width() / 2 - w1, y1)});
        p.setPen(Qt::NoPen);
        p.setBrush(i % 2 ? QColor(70, 70, 80) : QColor(60, 60, 70));
        p.drawPolygon(band);
        QPolygonF kerb({QPointF(s.width() / 2 - w0 - 10, y0), QPointF(s.width() / 2 - w0, y0), QPointF(s.width() / 2 - w1, y1),
                        QPointF(s.width() / 2 - w1 - 14, y1)});
        p.setBrush(i % 2 ? QColor(230, 30, 40) : QColor(250, 250, 250));
        p.drawPolygon(kerb);
        p.drawPolygon(QTransform().translate(s.width(), 0).scale(-1, 1).map(kerb));
        if (i % 2)
            p.fillRect(QRectF(s.width() / 2 - 3 - i * 0.3, y0, 6 + i * 0.6, y1 - y0), QColor(255, 255, 255));
    }
    p.setBrush(QColor(30, 120, 255));
    p.drawRoundedRect(QRectF(s.width() / 2 - 70, s.height() - 140, 140, 70), 16, 16);
    p.setBrush(QColor(20, 20, 30));
    p.drawRoundedRect(QRectF(s.width() / 2 - 50, s.height() - 128, 100, 24), 6, 6);
    return img;
}

QImage dungeon(const QSize &s, int seed)
{
    QImage img(s, QImage::Format_RGB32);
    img.fill(QColor(12, 10, 14));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const int t = s.width() / 20;
    QRandomGenerator rng{quint32(seed)};
    for (int y = 0; y < s.height(); y += t)
        for (int x = 0; x < s.width(); x += t) {
            const int v = 30 + int(rng.bounded(25));
            p.fillRect(x + 1, y + 1, t - 2, t - 2, QColor(v, v - 4, v + 6));
        }
    for (int i = 0; i < 3; ++i) {
        const QPointF c(s.width() * (0.2 + 0.3 * i), s.height() * (0.35 + 0.2 * (i % 2)));
        QRadialGradient glow(c, s.width() * 0.22);
        glow.setColorAt(0, QColor(255, 170, 60, 170));
        glow.setColorAt(1, QColor(255, 120, 20, 0));
        p.fillRect(img.rect(), glow);
        p.fillRect(QRectF(c.x() - 4, c.y(), 8, 30), QColor(90, 60, 30));
        p.setBrush(QColor(255, 210, 90));
        p.setPen(Qt::NoPen);
        p.drawEllipse(c, 9, 14);
    }
    p.setBrush(QColor::fromHsv((seed * 50) % 360, 200, 220));
    p.drawEllipse(QPointF(s.width() / 2, s.height() * 0.7), t * 0.6, t * 0.6);
    return img;
}

QImage scene(int i, const QSize &s)
{
    switch (i % 5) {
    case 0: return synthwave(s, 260 + i * 17);
    case 1: return shooter(s, i);
    case 2: return platformer(s, i);
    case 3: return road(s, i);
    default: return dungeon(s, i);
    }
}

// ---- The Deck ---------------------------------------------------------------

void sid(const QString &path, const QByteArray &name, const QByteArray &author, const QByteArray &released, int songs)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QByteArray h(0x7c, '\0');
    h.replace(0, 4, "PSID");
    h[5] = 2;
    h[0x0f] = char(songs);
    h[0x11] = 1;
    h.replace(0x16, name.size(), name);
    h.replace(0x36, author.size(), author);
    h.replace(0x56, released.size(), released);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(h);
        f.write(QByteArray(1500 + (name.size() * 977) % 9000, '\x60'));
    }
}

void buildDeck(const QString &r, const QString &media)
{
    // Emulation
    const struct { const char *sys; const char *ext; qint64 size; QStringList games; } systems[] = {
        {"c64", "d64", 174848, {QStringLiteral("Paradroid"), QStringLiteral("Impossible Mission"), QStringLiteral("Elite"), QStringLiteral("Boulder Dash"), QStringLiteral("Wizball"), QStringLiteral("Uridium"), QStringLiteral("Archon"), QStringLiteral("Mayhem in Monsterland")}},
        {"amiga", "adf", 901120, {QStringLiteral("Deluxe Galaga"), QStringLiteral("Turrican II"), QStringLiteral("Shadow of the Beast"), QStringLiteral("Speedball 2"), QStringLiteral("Lemmings"), QStringLiteral("Another World"), QStringLiteral("Sensible Soccer")}},
        {"nes", "nes", 262160, {QStringLiteral("Contra"), QStringLiteral("Mega Man 2"), QStringLiteral("Castlevania"), QStringLiteral("Metroid"), QStringLiteral("Kirby's Adventure")}},
        {"snes", "sfc", 3145728, {QStringLiteral("Super Metroid"), QStringLiteral("Chrono Trigger"), QStringLiteral("Contra III"), QStringLiteral("F-Zero"), QStringLiteral("Super Castlevania IV")}},
        {"gba", "gba", 16777216, {QStringLiteral("Metroid Fusion"), QStringLiteral("Advance Wars"), QStringLiteral("Golden Sun")}},
        {"psx", "chd", 512LL * 1024 * 1024, {QStringLiteral("Castlevania - Symphony of the Night"), QStringLiteral("Final Fantasy VII (Disc 1)"), QStringLiteral("Final Fantasy VII (Disc 2)"), QStringLiteral("Ape Escape")}},
        {"dreamcast", "chd", 980LL * 1024 * 1024, {QStringLiteral("Jet Set Radio"), QStringLiteral("Shenmue (Disc 1)"), QStringLiteral("Crazy Taxi")}},
    };
    int age = 3;
    for (const auto &s : systems) {
        for (const QString &g : s.games)
            sparse(QStringLiteral("%1/Emulation/roms/%2/%3.%4").arg(r, QLatin1String(s.sys), g, QLatin1String(s.ext)), s.size + age * 37, age++ % 400);
    }
    {
        QFile crt(r + QStringLiteral("/Emulation/roms/c64/Mayhem in Monsterland.crt"));
        if (crt.open(QIODevice::WriteOnly)) {
            crt.write("C64 CARTRIDGE   ");
            crt.write(QByteArray(524368 - 16, '\0'));
        }
    }
    sparse(r + QStringLiteral("/Emulation/roms/amiga/Deluxe Galaga v2.6.lha"), 412000, 12);
    sparse(r + QStringLiteral("/Emulation/bios/kick13.rom"), 262144, 900);
    sparse(r + QStringLiteral("/Emulation/bios/scph5501.bin"), 524288, 900);
    sparse(r + QStringLiteral("/Emulation/bios/dc_boot.bin"), 2097152, 900);
    sparse(r + QStringLiteral("/Emulation/saves/retroarch/Super Metroid.srm"), 8192, 1);
    sparse(r + QStringLiteral("/Emulation/README.md"), 4200, 60);

    // Screenshots
    for (int i = 0; i < 15; ++i) {
        const QDateTime when = QDateTime(QDate(2026, 10, 22), QTime(18, 10)).addSecs(-i * 86400 / 3 - i * 997);
        const QString name = i % 3 == 0 ? when.toString(QStringLiteral("'Screenshot_'yyyyMMdd_HHmmss'.png'"))
                                        : when.toString(QStringLiteral("yyyyMMddHHmmss'_1.jpg'"));
        const QString path = r + QStringLiteral("/Pictures/Screenshots/") + name;
        QDir().mkpath(QFileInfo(path).absolutePath());
        scene(i, QSize(1280, 800)).save(path, nullptr, 88);
        QFile f(path);
        if (f.open(QIODevice::ReadWrite))
            f.setFileTime(when, QFileDevice::FileModificationTime);
    }
    QDir().mkpath(r + QStringLiteral("/Pictures/Wallpapers"));
    synthwave(QSize(1920, 1080), 280).save(r + QStringLiteral("/Pictures/Wallpapers/Outrun Dusk.png"));
    platformer(QSize(1920, 1080), 11).save(r + QStringLiteral("/Pictures/Wallpapers/Green Hills.png"));
    shooter(QSize(1920, 1080), 5).save(r + QStringLiteral("/Pictures/Wallpapers/Invaders.png"));
    synthwave(QSize(1920, 1080), 200).save(r + QStringLiteral("/Pictures/Wallpapers/Neon Grid.jpg"), nullptr, 90);
    road(QSize(1920, 1080), 3).save(r + QStringLiteral("/Pictures/Wallpapers/Coastline.jpg"), nullptr, 90);
    dungeon(QSize(1920, 1080), 7).save(r + QStringLiteral("/Pictures/Wallpapers/Torchlight.jpg"), nullptr, 90);

    // Music: SID tunes and a few albums.
    const struct { const char *file, *name, *author, *released; int songs; } tunes[] = {
        {"Commando.sid", "Commando", "Rob Hubbard", "1985 Elite", 3},
        {"Monty_on_the_Run.sid", "Monty on the Run", "Rob Hubbard", "1985 Gremlin Graphics", 2},
        {"Sanxion.sid", "Sanxion", "Rob Hubbard", "1986 Thalamus", 2},
        {"Delta.sid", "Delta", "Rob Hubbard", "1987 Thalamus", 12},
        {"Last_Ninja.sid", "The Last Ninja", "Ben Daglish", "1987 System 3", 9},
        {"Deflektor.sid", "Deflektor", "Ben Daglish", "1987 Gremlin Graphics", 1},
        {"Cybernoid_II.sid", "Cybernoid II", "Jeroen Tel", "1988 Hewson", 3},
        {"Myth.sid", "Myth", "Jeroen Tel", "1989 System 3", 4},
        {"Turrican.sid", "Turrican", "Chris Huelsbeck", "1990 Rainbow Arts", 9},
        {"Wizball.sid", "Wizball", "Martin Galway", "1987 Ocean", 3},
        {"Parallax.sid", "Parallax", "Martin Galway", "1986 Ocean", 5},
        {"Dark_Unicorn_Theme.sid", "Dark Unicorn Theme", "Darkuni", "1994 Dark Unicorn", 1},
    };
    for (const auto &t : tunes)
        sid(r + QStringLiteral("/Music/C64 SIDs/") + QLatin1String(t.file), t.name, t.author, t.released, t.songs);
    for (const QString &song : {QStringLiteral("01 - Bad Reputation.flac"), QStringLiteral("02 - Cherry Bomb.flac"),
                                QStringLiteral("03 - Do You Wanna Touch Me.flac"), QStringLiteral("04 - I Love Rock n Roll.flac")})
        sparse(r + QStringLiteral("/Music/Joan Jett/Bad Reputation/") + song, 28 * 1024 * 1024 + song.size() * 80000, 200);
    for (int i = 1; i <= 6; ++i)
        sparse(r + QStringLiteral("/Music/Passenger Seat Radio/PSR %1.mp3").arg(100 + i), 48 * 1024 * 1024 + i * 913, 7 * i);

    // Documents, downloads, projects, videos
    sparse(r + QStringLiteral("/Documents/Dark Unicorn Dispatch 1994.nfo"), 6200, 4000);
    sparse(r + QStringLiteral("/Documents/Tombstone BBS userlist.txt"), 18000, 4100);
    sparse(r + QStringLiteral("/Documents/Retirement checklist.md"), 3100, 5);
    sparse(r + QStringLiteral("/Documents/Budget 2026.ods"), 48000, 30);
    sparse(r + QStringLiteral("/Documents/VCF Southwest notes.md"), 2400, 120);
    sparse(r + QStringLiteral("/Downloads/Linopus-x86_64.flatpak"), 41 * 1024 * 1024, 0);
    sparse(r + QStringLiteral("/Downloads/RetroDeck-cooker.AppImage"), 980 * 1024 * 1024, 0);
    sparse(r + QStringLiteral("/Downloads/hvsc-82.zip"), 87 * 1024 * 1024, 1);
    sparse(r + QStringLiteral("/Downloads/kickstart-roms.zip"), 1200000, 1);
    sparse(r + QStringLiteral("/Downloads/SteamOS-recovery.img.bz2"), 2700LL * 1024 * 1024, 4);
    sparse(r + QStringLiteral("/Downloads/Deluxe Galaga 2026 build.zip"), 18 * 1024 * 1024, 9);
    sparse(r + QStringLiteral("/Downloads/C64 Ultimate manual.pdf"), 6 * 1024 * 1024, 20);
    sparse(r + QStringLiteral("/Downloads/old-backup.tar.gz"), 610 * 1024 * 1024, 200);
    for (const char *f : {"src/main.cpp", "src/Game.cpp", "src/Game.h", "src/Sprites.cpp", "src/Audio.cpp", "CMakeLists.txt",
                          "assets/ships.png", "assets/stage1.png", "README.md", "build.sh"})
        sparse(r + QStringLiteral("/Projects/Deluxe Galaga 2026/") + QLatin1String(f), 3000 + qstrlen(f) * 811, 2);
    shooter(QSize(256, 160), 2).save(r + QStringLiteral("/Projects/Deluxe Galaga 2026/assets/ships.png"));
    sparse(r + QStringLiteral("/Videos/Forgotten Formats ep 12.mkv"), 3200LL * 1024 * 1024, 14);
    sparse(r + QStringLiteral("/Videos/Steam Machine BOM - final.mp4"), 2100LL * 1024 * 1024, 30);
    sparse(r + QStringLiteral("/Videos/Deck b-roll.mov"), 7400LL * 1024 * 1024, 3);

    // The SD card: a Steam library.
    const struct { const char *game; qint64 gb10; } steam[] = {
        {"Baldur's Gate 3", 1220}, {"Cyberpunk 2077", 702}, {"ELDEN RING", 498}, {"Hades II", 112},
        {"Hollow Knight", 92}, {"Celeste", 12}, {"Balatro", 2}, {"Stardew Valley", 6}, {"Vampire Survivors", 7},
        {"DAVE THE DIVER", 103}, {"Dead Cells", 15}, {"Sea of Stars", 58}};
    for (const auto &g : steam) {
        const QString d = media + QStringLiteral("/Games/steamapps/common/") + QLatin1String(g.game);
        sparse(d + QStringLiteral("/data.pak"), g.gb10 * 1024LL * 1024 * 1024 / 10, 10 + int(g.gb10 % 90));
        sparse(d + QStringLiteral("/game.exe"), 90LL * 1024 * 1024, 10);
    }
    for (const char *f : {"Linopus settings 2026-10-20.zip", "Deck saves 2026-10-01.tar.gz", "Screenshots 2026-09.zip"})
        sparse(media + QStringLiteral("/KINGSTON/Backups/") + QLatin1String(f), 40LL * 1024 * 1024 + qstrlen(f) * 100000, 3 + int(qstrlen(f)));
    sparse(media + QStringLiteral("/Games/roms/c64/Paradroid.d64"), 174848, 10);
    sparse(media + QStringLiteral("/Games/roms/amiga/Turrican II.adf"), 901120, 10);
    sparse(media + QStringLiteral("/Games/roms/snes/Super Metroid.sfc"), 3145728, 10);
    QDir().mkpath(media + QStringLiteral("/KINGSTON/Backups"));
    sparse(r + QStringLiteral("/.steam/registry.vdf"), 900, 1);
    sparse(r + QStringLiteral("/.bashrc"), 3700, 300);
}

// ---- Capturing --------------------------------------------------------------

void shot(QWidget *w, const QString &name, int wait = 700)
{
    QTest::qWait(wait);
    const QString file = g_out + QLatin1Char('/') + name + QStringLiteral(".png");
    w->grab().save(file);
    g_made << file;
}

// A dialog over its window, the way it looks on screen: the window dimmed a
// little, the dialog centred with a soft shadow.
void shotWithDialog(QWidget *w, QWidget *dlg, const QString &name, QPoint offset = {})
{
    QTest::qWait(700);
    QImage base = w->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage d = dlg->grab().toImage();
    QPainter p(&base);
    p.fillRect(base.rect(), QColor(0, 0, 0, 70));
    const QPoint at = QPoint((base.width() - d.width()) / 2, (base.height() - d.height()) / 2) + offset;
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 14; i > 0; --i) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 7));
        p.drawRoundedRect(QRectF(at.x() - i, at.y() - i + 8, d.width() + 2 * i, d.height() + 2 * i), 10 + i, 10 + i);
    }
    p.drawImage(at, d);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(at, d.size()).adjusted(-0.5, -0.5, 0.5, 0.5));
    p.end();
    const QString file = g_out + QLatin1Char('/') + name + QStringLiteral(".png");
    base.save(file);
    g_made << file;
}

// Four shots in a 2 x 2 grid, each with its caption under it.
void montage(const QStringList &files, const QStringList &captions, const QString &name, const QString &title)
{
    const int cw = 640, ch = 400, pad = 28, capH = 34, titleH = 70;
    QImage sheet(pad + 2 * (cw + pad), titleH + 2 * (ch + capH + pad) + pad / 2, QImage::Format_RGB32);
    sheet.fill(QColor(18, 20, 28));
    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont tf = QApplication::font();
    tf.setPixelSize(30);
    tf.setBold(true);
    p.setFont(tf);
    p.setPen(QColor(240, 240, 245));
    p.drawText(QRect(0, 0, sheet.width(), titleH), Qt::AlignCenter, title);
    QFont cf = QApplication::font();
    cf.setPixelSize(17);
    p.setFont(cf);
    for (int i = 0; i < files.size() && i < 4; ++i) {
        const int x = pad + (i % 2) * (cw + pad), y = titleH + (i / 2) * (ch + capH + pad);
        const QImage img = QImage(files[i]).scaled(cw, ch, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRoundedRect(QRectF(x + 4, y + 6, cw, ch), 6, 6);
        p.drawImage(x, y, img);
        p.setPen(QColor(200, 204, 215));
        p.drawText(QRect(x, y + ch + 4, cw, capH), Qt::AlignCenter, captions.value(i));
    }
    p.end();
    const QString file = g_out + QLatin1Char('/') + name + QStringLiteral(".png");
    sheet.save(file);
    g_made << file;
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("HOME", "/home/deck"); // the Home root and ~ in the breadcrumbs
    QApplication app(argc, argv);
    AppContext::setupIconTheme();
    g_out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    QDir().mkpath(g_out);

    QTemporaryDir sandbox;
    Paths::setConfigDirOverride(sandbox.filePath(QStringLiteral("config")));
    Paths::setDataHomeOverride(sandbox.filePath(QStringLiteral("data")));
    Paths::setCacheHomeOverride(sandbox.filePath(QStringLiteral("cache")));
    g_root = QStringLiteral("/home/deck");
    const QString r = g_root;
    const QString media = QStringLiteral("/run/media/deck");
    QDir(r).removeRecursively();
    QDir(media).removeRecursively();
    buildDeck(r, media);

    LabelRule star;
    star.pattern = QStringLiteral("*Galaga*;*Turrican*;Dark_Unicorn*");
    star.color = QColor::fromRgb(0xe8, 0x80, 0x18);
    star.bold = true;
    LabelRule old;
    old.pattern = QStringLiteral("old-*;*-old*");
    old.color = QColor::fromRgb(0xc8, 0x38, 0x38);
    old.italic = true;
    Style::instance()->setLabels({star, old});

    AppContext context;
    ListerWindow *w = context.newLister(r);
    w->resize(1280, 800); // the Deck's screen
    w->show();
    QTest::qWait(300);

    // Drives as the Deck has them: internal, the SD card, a USB stick.
    QList<DriveEntry> drives = w->drivesBar()->drives().mid(0, 2);
    DriveEntry sd;
    sd.kind = DriveEntry::Kind::SdCard;
    sd.name = QStringLiteral("Games");
    sd.path = media + QStringLiteral("/Games");
    sd.bytesFree = 118LL * 1024 * 1024 * 1024;
    sd.bytesTotal = 476LL * 1024 * 1024 * 1024;
    DriveEntry usb = sd;
    usb.kind = DriveEntry::Kind::Usb;
    usb.name = QStringLiteral("KINGSTON");
    usb.path = media + QStringLiteral("/KINGSTON");
    usb.bytesFree = 51LL * 1024 * 1024 * 1024;
    usb.bytesTotal = 64LL * 1024 * 1024 * 1024;
    drives << sd << usb;
    w->drivesBar()->setDrivesForTesting(drives);
    auto treeDrives = [&]() {
        if (w->sharedTree())
            w->sharedTree()->setDrivesForTesting(drives);
        for (int i = 0; i < 2; ++i)
            if (w->display(i)->tree())
                w->display(i)->tree()->setDrivesForTesting(drives);
    };
    FolderTab *left = nullptr, *right = nullptr;
    auto go = [&](const QString &l, const QString &rt) {
        left = w->display(0)->currentTab();
        right = w->display(1)->currentTab();
        if (!l.isEmpty())
            left->navigate(l);
        if (!rt.isEmpty())
            right->navigate(rt);
        QTest::qWait(500);
    };
    auto run = [&](const QString &c) {
        w->runCommand(c);
        QTest::qWait(150);
    };
    auto source = [&](int i) {
        if (w->sourceDisplay() != w->display(i))
            run(QStringLiteral("Set SOURCE=toggle"));
    };

    // 1. The hero: dark, dual, shared tree; ROMs on the left, the Steam
    // library on the SD card on the right with live folder sizes.
    run(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet DUAL=vertical\nSet TREE=shared\nSet VIEW=details"));
    treeDrives();
    go(r + QStringLiteral("/Emulation/roms"), media + QStringLiteral("/Games/steamapps/common"));
    source(1);
    run(QStringLiteral("Set FOLDERSIZES=on"));
    QTest::qWait(1500);
    right->detailsView()->sortByColumn(FileListModel::SizeColumn, Qt::DescendingOrder);
    source(0);
    run(QStringLiteral("Set FOLDERSIZES=on"));
    QTest::qWait(1200);
    left->selectPaths({r + QStringLiteral("/Emulation/roms/amiga")});
    shot(w, QStringLiteral("01-dark-dual-steam-library-sizes"), 900);

    // 2. Screenshots as thumbnails in Linopus Light, the viewer on the right.
    run(QStringLiteral("Set FOLDERSIZES=off\nSet TREE=off\nSet DUAL=off\nSet THEME=\"Linopus Light\""));
    go(r + QStringLiteral("/Pictures/Screenshots"), QString());
    run(QStringLiteral("Set VIEW=thumbnails\nSet VIEWER=on\nSet VIEWERPOS=right"));
    {
        const QStringList shots = QDir(r + QStringLiteral("/Pictures/Screenshots")).entryList(QDir::Files, QDir::Name);
        left->selectPaths({r + QStringLiteral("/Pictures/Screenshots/") + shots.value(4)});
    }
    shot(w, QStringLiteral("02-light-screenshot-thumbnails-viewer"), 2500);

    // 3. Workbench: C64 SIDs grouped by composer, ROMs with labels beside them.
    run(QStringLiteral("Set VIEWER=off\nSet THEME=Workbench\nSet DUAL=vertical\nSet VIEW=details"));
    go(r + QStringLiteral("/Music/C64 SIDs"), r + QStringLiteral("/Emulation/roms/amiga"));
    source(0);
    run(QStringLiteral("Set COLUMNSTOGGLE=modified\nSet COLUMNSTOGGLE=type\nSet COLUMNSTOGGLE=title\nSet COLUMNSTOGGLE=year\nSet GROUPBY=artist"));
    QTest::qWait(900);
    left->selectPaths({r + QStringLiteral("/Music/C64 SIDs/Wizball.sid")});
    shot(w, QStringLiteral("03-workbench-sids-by-composer"), 900);
    run(QStringLiteral("Set GROUPBY=none\nSet COLUMNSTOGGLE=modified\nSet COLUMNSTOGGLE=type\nSet COLUMNSTOGGLE=title\nSet COLUMNSTOGGLE=year"));

    // 4. Downloads by date in Linopus Dark at Larger text for arm's length.
    run(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet DUAL=off\nSet SIZE=larger"));
    go(r + QStringLiteral("/Downloads"), QString());
    run(QStringLiteral("Set GROUPBY=modified"));
    left->selectPaths({r + QStringLiteral("/Downloads/Linopus-x86_64.flatpak")});
    shot(w, QStringLiteral("04-dark-larger-text-downloads-by-date"), 900);
    run(QStringLiteral("Set GROUPBY=none\nSet SIZE=normal"));

    // 5. Linopus Light, stacked displays: every disc and disk image under
    // Emulation in one flat list, grouped by system and filtered; the SD
    // card's ROMs below.
    run(QStringLiteral("Set THEME=\"Linopus Light\"\nSet DUAL=horizontal"));
    go(r + QStringLiteral("/Emulation"), media + QStringLiteral("/Games/roms"));
    source(0);
    run(QStringLiteral("Set FLATVIEW=grouped\nSet FILTER=*.d64;*.adf;*.sfc;*.chd"));
    QTest::qWait(900);
    left->detailsView()->collapseAll();
    {
        GroupTreeView *v = left->detailsView();
        for (int row = 0; row < v->model()->rowCount(); ++row) {
            const QModelIndex i = v->model()->index(row, 0);
            const QString g = i.data(FileListModel::GroupLabelRole).toString();
            if (g.endsWith(QLatin1String("amiga")) || g.endsWith(QLatin1String("psx")))
                v->toggleGroup(i);
        }
    }
    shot(w, QStringLiteral("05-light-stacked-flat-filtered"), 900);
    run(QStringLiteral("Set FILTERBAR=off\nSet FLATVIEW=off"));
    left->detailsView()->expandAll();

    // 6. Workbench with a tree per display.
    run(QStringLiteral("Set THEME=Workbench\nSet DUAL=vertical\nSet TREE=separate"));
    treeDrives();
    go(r + QStringLiteral("/Projects/Deluxe Galaga 2026/src"), media + QStringLiteral("/KINGSTON/Backups"));
    left->selectPaths({r + QStringLiteral("/Projects/Deluxe Galaga 2026/src/Game.cpp"),
                       r + QStringLiteral("/Projects/Deluxe Galaga 2026/src/Game.h")});
    shot(w, QStringLiteral("06-workbench-separate-trees"), 900);
    run(QStringLiteral("Set TREE=off"));

    // 7. Find results as a collection, in Linopus Dark.
    run(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet DUAL=vertical"));
    go(r, media + QStringLiteral("/Games/roms"));
    source(0);
    run(QStringLiteral("Find NAME=*.d64;*.adf;*.crt;*.lha IN=\"%1\" RUN").arg(r));
    for (int i = 0; i < 60 && !Collections::isCollectionLocation(w->display(0)->currentTab()->path()); ++i)
        QTest::qWait(100);
    shot(w, QStringLiteral("07-dark-find-results"), 1000);

    // 8. Synchronize, over the window.
    run(QStringLiteral("Set THEME=\"Linopus Dark\""));
    go(r + QStringLiteral("/Emulation/roms"), media + QStringLiteral("/Games/roms"));
    {
        SyncOptions o;
        o.mode = SyncOptions::Mode::Update;
        SyncDialog dlg(context.engine(), r + QStringLiteral("/Emulation/roms"), media + QStringLiteral("/Games/roms"), o, w);
        dlg.resize(900, 600);
        dlg.show();
        dlg.startCompare();
        for (int i = 0; i < 60 && dlg.isComparing(); ++i)
            QTest::qWait(100);
        shotWithDialog(w, &dlg, QStringLiteral("08-dark-synchronize-to-sd"));
        dlg.hide();
    }

    // 9. Advanced Rename over the screenshots, Linopus Light.
    run(QStringLiteral("Set THEME=\"Linopus Light\"\nSet DUAL=off\nSet VIEW=details"));
    go(r + QStringLiteral("/Pictures/Screenshots"), QString());
    {
        QStringList paths;
        for (const QString &n : QDir(r + QStringLiteral("/Pictures/Screenshots")).entryList({QStringLiteral("*.jpg")}, QDir::Files, QDir::Name))
            paths << r + QStringLiteral("/Pictures/Screenshots/") + n;
        run(QStringLiteral("Select PATTERN=*.jpg"));
        RenameOptions o;
        o.mode = RenameOptions::Mode::Standard;
        o.oldPattern = QStringLiteral("*");
        o.newPattern = QStringLiteral("Deck capture ###");
        o.ignoreExtension = true;
        AdvancedRenameDialog dlg(paths, o, w);
        dlg.resize(900, 620);
        dlg.show();
        shotWithDialog(w, &dlg, QStringLiteral("09-light-advanced-rename"));
    }

    // 10. Convert Images in Workbench.
    run(QStringLiteral("Set THEME=Workbench\nSet VIEW=thumbnails"));
    go(r + QStringLiteral("/Pictures/Screenshots"), QString());
    {
        QStringList pngs;
        for (const QString &n : QDir(r + QStringLiteral("/Pictures/Screenshots")).entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name))
            pngs << r + QStringLiteral("/Pictures/Screenshots/") + n;
        left->selectPaths(pngs);
        ImageOptions o;
        o.format = QStringLiteral("jpg");
        o.quality = 85;
        o.resize = ImageOptions::Resize::Width;
        o.width = 1280;
        o.destFolder = media + QStringLiteral("/KINGSTON/Backups");
        ImageConvertDialog dlg(pngs, o, media + QStringLiteral("/KINGSTON/Backups"), w);
        dlg.show();
        shotWithDialog(w, &dlg, QStringLiteral("10-workbench-convert-images"));
    }

    // 11. Customize mode and the command console, Linopus Dark.
    run(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet VIEW=details\nSet DUAL=vertical"));
    go(r + QStringLiteral("/Emulation/roms/snes"), media + QStringLiteral("/Games/roms/snes"));
    w->showConsole();
    if (CommandConsole *c = w->console()) {
        c->submit(QStringLiteral("Select PATTERN=Super*"));
        c->submit(QStringLiteral("Set COLUMNSTOGGLE=extension"));
        c->submit(QStringLiteral("Find NAME=*.chd MINSIZE=500M IN=/home/deck/Emulation RUN"));
        c->input()->setText(QStringLiteral("Copy TO=dest WHENEXISTS=newer"));
        shotWithDialog(w, c, QStringLiteral("11-dark-command-console"), QPoint(0, 110));
        c->close();
    }
    run(QStringLiteral("Set COLUMNSTOGGLE=extension"));

    // 12b. Thumbnails both sides, dark.
    run(QStringLiteral("Set THEME=\"Linopus Dark\"\nSet DUAL=vertical\nSet TREE=off"));
    go(r + QStringLiteral("/Pictures/Screenshots"), r + QStringLiteral("/Pictures/Wallpapers"));
    run(QStringLiteral("Set VIEW=thumbnails"));
    source(1);
    run(QStringLiteral("Set VIEW=thumbnails"));
    source(0);
    shot(w, QStringLiteral("13-dark-thumbnails-dual"), 2500);
    run(QStringLiteral("Set VIEW=details"));
    source(1);
    run(QStringLiteral("Set VIEW=details"));
    source(0);
    run(QStringLiteral("Set VIEW=details"));

    // 12. One view, four themes.
    QStringList themeShots, themeNames;
    run(QStringLiteral("Set DUAL=vertical\nSet TREE=shared\nSet VIEW=details"));
    treeDrives();
    go(r + QStringLiteral("/Emulation/roms/c64"), r + QStringLiteral("/Music"));
    left->selectPaths({r + QStringLiteral("/Emulation/roms/c64/Paradroid.d64"), r + QStringLiteral("/Emulation/roms/c64/Elite.d64")});
    for (const QString &t : {QStringLiteral("Linopus Light"), QStringLiteral("Linopus Dark"), QStringLiteral("Workbench"),
                             QStringLiteral("Follow Desktop")}) {
        run(QStringLiteral("Set THEME=\"%1\"").arg(t));
        treeDrives();
        const QString n = QStringLiteral("12-theme-") + t.toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
        shot(w, n, 800);
        themeShots << g_made.last();
        themeNames << t;
    }
    montage(themeShots, themeNames, QStringLiteral("00-four-themes"), QStringLiteral("Linopus - one lister, four themes"));

    // The highlights on one sheet.
    montage({g_made.at(0), g_made.at(1), g_made.at(2), g_made.at(4)},
            {QStringLiteral("Linopus Dark: Steam library on the SD card, live folder sizes"),
             QStringLiteral("Linopus Light: screenshots as thumbnails, viewer pane"),
             QStringLiteral("Workbench: C64 SIDs grouped by composer"),
             QStringLiteral("Stacked displays: flat view of every disk image")},
            QStringLiteral("00-highlights"), QStringLiteral("Linopus on the Steam Deck"));

    for (const QString &f : std::as_const(g_made))
        printf("%s\n", qPrintable(f));

    QDir(r).removeRecursively();
    QDir(media).removeRecursively();
    return 0;
}
