#include "core/Apps.h"
#include "core/HostRun.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

} // namespace

class TestApps : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        Paths::setConfigDirOverride(m_dir->filePath(QStringLiteral("cfg")));
        const QString apps = m_dir->filePath(QStringLiteral("apps"));
        write(apps + QStringLiteral("/org.kde.kate.desktop"),
              "[Desktop Entry]\nType=Application\nName=Kate\nExec=kate -b %U\nIcon=kate\nMimeType=text/plain;text/x-c++src;\n"
              "\n[Desktop Action new]\nName=New Window\nExec=kate -n\n");
        write(apps + QStringLiteral("/vice.desktop"),
              "[Desktop Entry]\nType=Application\nName=VICE C64\nExec=\"/opt/vice/x64sc\" -autostart %f\nMimeType=application/x-d64;\n");
        write(apps + QStringLiteral("/hidden.desktop"), "[Desktop Entry]\nType=Application\nName=Hidden\nExec=hidden\nNoDisplay=true\n");
        write(apps + QStringLiteral("/link.desktop"), "[Desktop Entry]\nType=Link\nName=Site\nURL=https://example.com\n");
        write(apps + QStringLiteral("/kde/sub.desktop"), "[Desktop Entry]\nType=Application\nName=Sub App\nExec=sub\n");
        write(apps + QStringLiteral("/org.flatpak.App.desktop"),
              "[Desktop Entry]\nType=Application\nName=Flatpak App\nExec=/usr/bin/flatpak run --branch=stable org.flatpak.App @@u %U @@\nMimeType=text/plain;\n");
        Apps::setSearchDirsOverride({apps});
        write(m_dir->filePath(QStringLiteral("notes.txt")), "hello");
    }

    void cleanup() { Apps::setSearchDirsOverride({}); }

    void readsDesktopFiles()
    {
        QStringList names;
        for (const AppInfo &a : Apps::all())
            names << a.name;
        QCOMPARE(names, (QStringList{QStringLiteral("Flatpak App"), QStringLiteral("Kate"), QStringLiteral("Sub App"), QStringLiteral("VICE C64")}));
        QCOMPARE(Apps::find(QStringLiteral("kate")).id, QStringLiteral("org.kde.kate.desktop"));
        QCOMPARE(Apps::find(QStringLiteral("org.kde.kate")).name, QStringLiteral("Kate"));
        QCOMPARE(Apps::find(QStringLiteral("kde-sub.desktop")).name, QStringLiteral("Sub App"));
        QCOMPARE(Apps::find(QStringLiteral("Kate")).exec, QStringLiteral("kate -b %U")); // not the action's Exec
        QVERIFY(!Apps::find(QStringLiteral("Hidden")).isValid());
    }

    void fillsFieldCodes()
    {
        const QStringList files{QStringLiteral("/a/b c.txt"), QStringLiteral("/d.txt")};
        QCOMPARE(Apps::commandFor(Apps::find(QStringLiteral("Kate")), files),
                 (QStringList{QStringLiteral("kate"), QStringLiteral("-b"), QStringLiteral("file:///a/b c.txt"), QStringLiteral("file:///d.txt")}));
        QCOMPARE(Apps::commandFor(Apps::find(QStringLiteral("VICE C64")), files),
                 (QStringList{QStringLiteral("/opt/vice/x64sc"), QStringLiteral("-autostart"), QStringLiteral("/a/b c.txt")}));
        QCOMPARE(Apps::commandFor(Apps::find(QStringLiteral("Sub App")), files),
                 (QStringList{QStringLiteral("sub"), QStringLiteral("/a/b c.txt"), QStringLiteral("/d.txt")}));
        QCOMPARE(Apps::commandFor(Apps::find(QStringLiteral("Flatpak App")), {QStringLiteral("/x.txt")}),
                 (QStringList{QStringLiteral("/usr/bin/flatpak"), QStringLiteral("run"), QStringLiteral("--branch=stable"),
                              QStringLiteral("org.flatpak.App"), QStringLiteral("file:///x.txt")}));
    }

    void appsForAFileAndDefaults()
    {
        const QString txt = m_dir->filePath(QStringLiteral("notes.txt"));
        QStringList ids;
        for (const AppInfo &a : Apps::forFile(txt))
            ids << a.id;
        QVERIFY(ids.contains(QStringLiteral("org.kde.kate.desktop")));
        QVERIFY(ids.contains(QStringLiteral("org.flatpak.App.desktop")));
        QVERIFY(!ids.contains(QStringLiteral("vice.desktop")));
        // Linopus's own default comes first and is what Open uses.
        Apps::setDefaultFor(QStringLiteral("text/plain"), QStringLiteral("org.flatpak.App.desktop"));
        QCOMPARE(Apps::forFile(txt).first().id, QStringLiteral("org.flatpak.App.desktop"));
        QCOMPARE(Apps::defaults().value(QStringLiteral("text/plain")), QStringLiteral("org.flatpak.App.desktop"));
        QStringList ran;
        HostRun::setStarter([&](const QStringList &cmd, const QString &) {
            ran = cmd;
            return true;
        });
        QString error;
        QVERIFY(Apps::openWithDefault(txt, &error));
        QVERIFY(ran.contains(QStringLiteral("org.flatpak.App")));
        QVERIFY(ran.contains(QUrl::fromLocalFile(txt).toString()));
        Apps::setDefaultFor(QStringLiteral("text/plain"), QString());
        QVERIFY(!Apps::openWithDefault(txt, &error)); // the desktop decides
        HostRun::setStarter({});
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_GUILESS_MAIN(TestApps)
#include "tst_apps.moc"
