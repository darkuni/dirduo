// Which files double-clicking runs, and how they are run.
#include "core/Executables.h"
#include "core/HostRun.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestExecutables : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString &name, const QByteArray &data, bool exec)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot write");
        f.write(data);
        f.close();
        QFileDevice::Permissions p = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser
            | QFileDevice::ReadGroup | QFileDevice::ReadOther;
        if (exec)
            p |= QFileDevice::ExeOwner | QFileDevice::ExeUser;
        f.setPermissions(p);
        return path;
    }

    QString copyOf(const QString &from, const QString &name)
    {
        const QString to = m_dir.filePath(name);
        QFile::remove(to);
        if (!QFile::copy(from, to))
            qFatal("cannot copy %s", qPrintable(from));
        return to;
    }

private Q_SLOTS:
    void recognizesPrograms()
    {
        // A real program: the test itself.
        const QString self = QCoreApplication::applicationFilePath();
        QCOMPARE(Executables::kindOf(self), Executables::Kind::Binary);
        QVERIFY(Executables::isMarkedExecutable(self));
        // By its bytes, not its name.
        const QString renamed = copyOf(self, QStringLiteral("game.dat"));
        QCOMPARE(Executables::kindOf(renamed), Executables::Kind::Binary);
        QCOMPARE(Executables::kindOf(copyOf(self, QStringLiteral("Tool-x86_64.AppImage"))), Executables::Kind::AppImage);
        // A shared library is not a program.
        const QString lib = QDir(QLibraryInfo::path(QLibraryInfo::LibrariesPath)).filePath(QStringLiteral("libQt6Core.so.6"));
        if (QFileInfo::exists(lib))
            QCOMPARE(Executables::kindOf(QFileInfo(lib).canonicalFilePath()), Executables::Kind::None);
        // Scripts and everything else.
        QCOMPARE(Executables::kindOf(write(QStringLiteral("check.sh"), "#!/bin/bash\necho hi\n", true)), Executables::Kind::Script);
        QCOMPARE(Executables::kindOf(write(QStringLiteral("notes.txt"), "hello", true)), Executables::Kind::None);
        QCOMPARE(Executables::kindOf(write(QStringLiteral("tiny"), "\x7f" "EL", false)), Executables::Kind::None);
        QCOMPARE(Executables::kindOf(m_dir.path()), Executables::Kind::None);
        QCOMPARE(Executables::kindOf(m_dir.filePath(QStringLiteral("missing"))), Executables::Kind::None);
    }

    void makesExecutable()
    {
        const QString path = write(QStringLiteral("download.AppImage"), "x", false);
        QVERIFY(!Executables::isMarkedExecutable(path));
        QString err;
        QVERIFY(Executables::makeExecutable(path, &err));
        QVERIFY(Executables::isMarkedExecutable(path));
        const QFileDevice::Permissions p = QFile(path).permissions();
        QVERIFY(p & QFileDevice::ExeOwner);
        QVERIFY(p & QFileDevice::ExeGroup); // readable by the group: runnable by it too
        QVERIFY(p & QFileDevice::ExeOther);
    }

    void runsInItsFolder()
    {
        QStringList ran;
        QString where;
        HostRun::setStarter([&](const QStringList &line, const QString &dir) {
            ran = line;
            where = dir;
            return true;
        });
        QDir().mkpath(m_dir.filePath(QStringLiteral("sub dir")));
        const QString script = write(QStringLiteral("sub dir/bios check.sh"), "#!/bin/sh\necho ok\n", true);
        QString err;
        QVERIFY(Executables::run(script, &err));
        QCOMPARE(ran.last(), QFileInfo(script).absoluteFilePath());
        QCOMPARE(where, m_dir.filePath(QStringLiteral("sub dir")));
        // In a terminal that stays open.
        QVERIFY(Executables::runInTerminal(script, &err));
        QCOMPARE(ran.value(ran.size() - 2), QStringLiteral("-c"));
        QVERIFY(ran.last().contains(QStringLiteral("konsole --hold")));
        QVERIFY(ran.last().contains(QStringLiteral("'") + QFileInfo(script).absoluteFilePath() + QStringLiteral("'")));
        HostRun::setStarter({});
    }

    void reallyRuns()
    {
        // No starter: it really starts, detached, in its folder.
        const QString out = m_dir.filePath(QStringLiteral("ran.txt"));
        const QString script = write(QStringLiteral("touch.sh"), "#!/bin/sh\npwd > ran.txt\n", true);
        QString err;
        QVERIFY2(Executables::run(script, &err), qPrintable(err));
        auto written = [out]() {
            QFile f(out);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
        };
        QTRY_COMPARE(written(), QFileInfo(m_dir.path()).canonicalFilePath());
    }
};

QTEST_GUILESS_MAIN(TestExecutables)
#include "tst_executables.moc"
