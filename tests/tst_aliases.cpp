#include "core/AliasManager.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestAliases : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        Paths::setConfigDirOverride(m_tmp.filePath(QStringLiteral("config")));
    }

    void builtInsResolve()
    {
        AliasManager a(m_tmp.filePath(QStringLiteral("a1.ini")));
        QVERIFY(a.hasAlias(QStringLiteral("downloads")));
        QVERIFY(a.hasAlias(QStringLiteral("Downloads"))); // case-insensitive
        const QString dl = a.aliasPath(QStringLiteral("downloads"));
        QCOMPARE(a.resolve(QStringLiteral("/downloads")), dl);
        QCOMPARE(a.resolve(QStringLiteral("/downloads/podcasts/ep1")), dl + QStringLiteral("/podcasts/ep1"));
        QCOMPARE(a.resolve(QStringLiteral("/DOWNLOADS/x")), dl + QStringLiteral("/x"));
    }

    void realRootPathsWin()
    {
        AliasManager a(m_tmp.filePath(QStringLiteral("a2.ini")));
        QCOMPARE(a.resolve(QStringLiteral("/usr/bin")), QStringLiteral("/usr/bin"));
        QCOMPARE(a.resolve(QStringLiteral("/home")), QStringLiteral("/home"));
        QCOMPARE(a.resolve(QStringLiteral("/tmp/../etc")), QStringLiteral("/etc"));
        QVERIFY(AliasManager::isReservedName(QStringLiteral("home")));
        QVERIFY(AliasManager::isReservedName(QStringLiteral("usr")));
        QVERIFY(!AliasManager::isReservedName(QStringLiteral("websites")));
    }

    void tildeAndUrls()
    {
        AliasManager a(m_tmp.filePath(QStringLiteral("a3.ini")));
        QCOMPARE(a.resolve(QStringLiteral("~")), QDir::homePath());
        QCOMPARE(a.resolve(QStringLiteral("~/Music")), QDir::homePath() + QStringLiteral("/Music"));
        QCOMPARE(a.resolve(QStringLiteral("file:///tmp/a%20b")), QStringLiteral("/tmp/a b"));
        QCOMPARE(a.resolve(QStringLiteral("relative/path")), QStringLiteral("relative/path"));
        QCOMPARE(a.resolve(QStringLiteral("  /nosuchaliasxyz/a  ")), QStringLiteral("/nosuchaliasxyz/a"));
    }

    void userAliasesPersistAndOverride()
    {
        const QString file = m_tmp.filePath(QStringLiteral("a4.ini"));
        {
            AliasManager a(file);
            QString error;
            QVERIFY2(a.setUserAlias(QStringLiteral("Websites"), QStringLiteral("/srv/www/sites"), &error), qPrintable(error));
            QVERIFY(a.setUserAlias(QStringLiteral("downloads"), QStringLiteral("/data/dl"), &error));
            QVERIFY(a.save());
        }
        AliasManager b(file);
        QCOMPARE(b.resolve(QStringLiteral("/websites/blog")), QStringLiteral("/srv/www/sites/blog"));
        QCOMPARE(b.resolve(QStringLiteral("/downloads")), QStringLiteral("/data/dl"));
        QVERIFY(b.isUserAlias(QStringLiteral("WEBSITES")));
        QVERIFY(b.removeUserAlias(QStringLiteral("websites")));
        QCOMPARE(b.resolve(QStringLiteral("/websites")), QStringLiteral("/websites"));
    }

    void aliasPointingAtAlias()
    {
        AliasManager a(m_tmp.filePath(QStringLiteral("a5.ini")));
        QString error;
        QVERIFY(a.setUserAlias(QStringLiteral("pods"), QStringLiteral("/downloads/podcasts"), &error));
        QCOMPARE(a.aliasPath(QStringLiteral("pods")), a.aliasPath(QStringLiteral("downloads")) + QStringLiteral("/podcasts"));
    }

    void rejectsBadNames()
    {
        AliasManager a(m_tmp.filePath(QStringLiteral("a6.ini")));
        QString error;
        QVERIFY(!a.setUserAlias(QStringLiteral("usr"), QStringLiteral("/x"), &error));
        QVERIFY(error.contains(QStringLiteral("root")));
        QVERIFY(!a.setUserAlias(QStringLiteral("has space"), QStringLiteral("/x"), &error));
        QVERIFY(!a.setUserAlias(QStringLiteral("ok"), QStringLiteral("relative"), &error));
    }

    void completions()
    {
        AliasManager a(m_tmp.filePath(QStringLiteral("a7.ini")));
        const QStringList c = a.completions(QStringLiteral("/dow"));
        QVERIFY(c.contains(QStringLiteral("/downloads")));
        QVERIFY(a.completions(QStringLiteral("/downloads/x")).isEmpty());
        QVERIFY(a.completions(QStringLiteral("dow")).isEmpty());
    }

    void readsUserDirsFile()
    {
        // XDG_CONFIG_HOME is honored outside Flatpak.
        QTemporaryDir cfg;
        QFile f(cfg.filePath(QStringLiteral("user-dirs.dirs")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("XDG_DOWNLOAD_DIR=\"$HOME/Stuff/Incoming\"\n# comment\nXDG_MUSIC_DIR=\"/mnt/music\"\n");
        f.close();
        qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
        AliasManager a(m_tmp.filePath(QStringLiteral("a8.ini")));
        QCOMPARE(a.aliasPath(QStringLiteral("downloads")), QDir::homePath() + QStringLiteral("/Stuff/Incoming"));
        QCOMPARE(a.aliasPath(QStringLiteral("music")), QStringLiteral("/mnt/music"));
        qunsetenv("XDG_CONFIG_HOME");
    }

private:
    QTemporaryDir m_tmp;
};

QTEST_GUILESS_MAIN(TestAliases)
#include "tst_aliases.moc"
