#include "core/Attributes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

int modeOf(const QString &path)
{
    struct stat st;
    ::lstat(QFile::encodeName(path).constData(), &st);
    return int(st.st_mode & 07777);
}

} // namespace

class TestAttributes : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parsesModes()
    {
        Attributes::ModeChange m;
        QVERIFY(Attributes::parseMode(QStringLiteral("644"), m));
        QCOMPARE(*m.exact, 0644);
        QVERIFY(Attributes::parseMode(QStringLiteral("0755"), m));
        QCOMPARE(*m.exact, 0755);
        QVERIFY(Attributes::parseMode(QStringLiteral("rwxr-x---"), m));
        QCOMPARE(*m.exact, 0750);
        QVERIFY(Attributes::parseMode(QStringLiteral("-rw-r--r--"), m));
        QCOMPARE(*m.exact, 0644);
        QVERIFY(Attributes::parseMode(QStringLiteral("u+x,go-w"), m));
        QVERIFY(!m.exact);
        QCOMPARE(m.add, 0100);
        QCOMPARE(m.remove, 022);
        QCOMPARE(m.apply(0666, false), 0744);
        QVERIFY(Attributes::parseMode(QStringLiteral("+x"), m));
        QCOMPARE(m.add, 0111);
        QVERIFY(Attributes::parseMode(QStringLiteral("o=r"), m));
        QCOMPARE(m.apply(0667, false), 0664);
        // Folders get x where r is added, so they still open.
        QVERIFY(Attributes::parseMode(QStringLiteral("go+r"), m));
        QCOMPARE(m.apply(0700, true), 0755);
        QCOMPARE(m.apply(0600, false), 0644);
        QVERIFY(!Attributes::parseMode(QStringLiteral("999"), m));
        QVERIFY(!Attributes::parseMode(QStringLiteral("u+q"), m));
        QCOMPARE(Attributes::modeString(0755, true), QStringLiteral("drwxr-xr-x"));
        QCOMPARE(Attributes::octal(0644), QStringLiteral("644"));
    }

    void parsesDates()
    {
        QCOMPARE(Attributes::parseDate(QStringLiteral("2024-05-01 13:30")), QDateTime(QDate(2024, 5, 1), QTime(13, 30)));
        QCOMPARE(Attributes::parseDate(QStringLiteral("2024-05-01")), QDateTime(QDate(2024, 5, 1), QTime(0, 0)));
        QVERIFY(qAbs(Attributes::parseDate(QStringLiteral("now")).secsTo(QDateTime::currentDateTime())) < 5);
        QVERIFY(!Attributes::parseDate(QStringLiteral("yesterday-ish")).isValid());
    }

    void appliesDatesAndPermissions()
    {
        QTemporaryDir dir;
        const QString f = dir.filePath(QStringLiteral("f/a.txt"));
        const QString g = dir.filePath(QStringLiteral("f/sub/b.txt"));
        touch(f);
        touch(g);
        ::chmod(QFile::encodeName(f).constData(), 0644);
        ::chmod(QFile::encodeName(g).constData(), 0644);
        Attributes::Change c;
        const QDateTime when(QDate(2001, 2, 3), QTime(4, 5, 6));
        c.modified = when;
        QVERIFY(Attributes::parseMode(QStringLiteral("u+x"), c.mode));
        QStringList errors;
        QCOMPARE(Attributes::apply({f}, c, &errors), 1);
        QCOMPARE(errors, QStringList());
        QCOMPARE(modeOf(f), 0744);
        QCOMPARE(QFileInfo(f).lastModified(), when);
        QCOMPARE(modeOf(g), 0644); // not inside unless asked
        c.recurse = true;
        Attributes::apply({dir.filePath(QStringLiteral("f"))}, c, &errors);
        QCOMPARE(modeOf(g), 0744);
        QCOMPARE(QFileInfo(g).lastModified(), when);
        QCOMPARE(QFileInfo(dir.filePath(QStringLiteral("f"))).lastModified(), when);

        const Attributes::Info i = Attributes::read(f);
        QVERIFY(i.exists);
        QCOMPARE(i.mode, 0744);
        QCOMPARE(i.size, qint64(1));
        QCOMPARE(i.modified, when);
        QVERIFY(!i.owner.isEmpty());
        QVERIFY(Attributes::apply({dir.filePath(QStringLiteral("gone"))}, c, &errors) == 0);
        QVERIFY(!errors.isEmpty());
    }

    void makesLinks()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("games/Game.d64"));
        touch(target);
        QDir().mkpath(dir.filePath(QStringLiteral("links")));
        QString error;
        const QString rel = dir.filePath(QStringLiteral("links/Game.d64"));
        QVERIFY(Attributes::makeLink(target, rel, true, &error));
        QCOMPARE(Attributes::read(rel).linkTarget, QStringLiteral("../games/Game.d64"));
        QVERIFY(Attributes::read(rel).isLink);
        QVERIFY(!Attributes::read(rel).brokenLink);
        const QString abs = dir.filePath(QStringLiteral("links/abs.d64"));
        QVERIFY(Attributes::makeLink(target, abs, false, &error));
        QCOMPARE(Attributes::read(abs).linkTarget, target);
        QVERIFY(!Attributes::makeLink(target, abs, false, &error));
        QVERIFY(error.contains(QStringLiteral("already")));
        QFile::remove(target);
        QVERIFY(Attributes::read(abs).brokenLink);
    }
};

QTEST_GUILESS_MAIN(TestAttributes)
#include "tst_attributes.moc"
