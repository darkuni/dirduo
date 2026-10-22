// Select by name, kind, size, age, and against the other display.
#include "core/SelectRules.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class TestSelectRules : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString here, there;

    FileItem make(const QString &root, const QString &rel, qint64 size, const QDateTime &when)
    {
        const QString path = root + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot write");
        f.write(QByteArray(int(size), 'x'));
        f.flush(); // or closing would stamp it with now
        f.setFileTime(when, QFileDevice::FileModificationTime);
        f.close();
        return FileItem::fromFileInfo(QFileInfo(path));
    }

private Q_SLOTS:
    void init()
    {
        here = m_dir.filePath(QStringLiteral("here"));
        there = m_dir.filePath(QStringLiteral("there"));
        QDir(here).removeRecursively();
        QDir(there).removeRecursively();
    }

    void compare()
    {
        const QDateTime t0 = QDateTime::currentDateTime().addDays(-10);
        const FileItem newer = make(here, QStringLiteral("newer.d64"), 100, t0.addSecs(3600));
        make(there, QStringLiteral("newer.d64"), 100, t0);
        const FileItem older = make(here, QStringLiteral("older.d64"), 100, t0);
        make(there, QStringLiteral("older.d64"), 100, t0.addSecs(3600));
        const FileItem same = make(here, QStringLiteral("same.d64"), 100, t0);
        make(there, QStringLiteral("same.d64"), 100, t0.addSecs(1)); // FAT: 2 s steps
        const FileItem diff = make(here, QStringLiteral("diff.d64"), 100, t0);
        make(there, QStringLiteral("diff.d64"), 90, t0);
        const FileItem lonely = make(here, QStringLiteral("only/here.d64"), 10, t0);
        QDir().mkpath(there + QStringLiteral("/only"));

        SelectRules r;
        r.thisRoot = here;
        r.otherRoot = there;
        QCOMPARE(r.counterpart(lonely.path), there + QStringLiteral("/only/here.d64")); // nested (expanded folders)
        QVERIFY(r.counterpart(m_dir.filePath(QStringLiteral("elsewhere"))).isEmpty());
        QCOMPARE(r.classify(newer), SelectRules::Compare::Newer);
        QCOMPARE(r.classify(older), SelectRules::Compare::Older);
        QCOMPARE(r.classify(same), SelectRules::Compare::Same);
        QCOMPARE(r.classify(diff), SelectRules::Compare::Different);
        QCOMPARE(r.classify(lonely), SelectRules::Compare::Missing);

        r.compare = SelectRules::Compare::NewerOrMissing;
        QVERIFY(r.matches(newer));
        QVERIFY(r.matches(lonely));
        QVERIFY(!r.matches(same));
        QVERIFY(!r.matches(older));
        // A folder that is in both is not "newer".
        const FileItem folder = FileItem::fromFileInfo(QFileInfo(here + QStringLiteral("/only")));
        r.compare = SelectRules::Compare::Newer;
        QVERIFY(!r.matches(folder));
        r.compare = SelectRules::Compare::Same;
        QVERIFY(r.matches(folder));
    }

    void byWhatTheyAre()
    {
        const QDateTime now = QDateTime::currentDateTime();
        const FileItem pic = make(here, QStringLiteral("Shot.PNG"), 5000, now.addDays(-1));
        const FileItem big = make(here, QStringLiteral("game.chd"), 200000, now.addDays(-40));
        const FileItem txt = make(here, QStringLiteral("notes.txt"), 10, now);
        SelectRules r;
        QVERIFY(r.isEmpty());
        r.pattern = QStringLiteral("*.png;*.chd");
        QVERIFY(r.matches(pic)); // case does not matter
        QVERIFY(r.matches(big));
        QVERIFY(!r.matches(txt));
        r = {};
        r.hasKind = true;
        r.kind = FileKind::Category::Image;
        QVERIFY(r.matches(pic));
        QVERIFY(!r.matches(big));
        r = {};
        r.minSize = 1000;
        QVERIFY(r.matches(pic) && r.matches(big) && !r.matches(txt));
        r.maxSize = 10000;
        QVERIFY(r.matches(pic) && !r.matches(big));
        r = {};
        r.newerThan = now.addDays(-7);
        QVERIFY(r.matches(pic) && !r.matches(big));
        r = {};
        r.olderThan = now.addDays(-7);
        QVERIFY(!r.matches(pic) && r.matches(big));
        r = {};
        r.type = SelectRules::Type::Folders;
        QVERIFY(!r.matches(pic));
        SelectRules::Compare c;
        QVERIFY(SelectRules::compareFromName(QStringLiteral("NewerOrMissing"), c));
        QCOMPARE(c, SelectRules::Compare::NewerOrMissing);
        QVERIFY(SelectRules::compareFromName(QStringLiteral("unique"), c));
        QCOMPARE(c, SelectRules::Compare::Missing);
        QVERIFY(!SelectRules::compareFromName(QStringLiteral("bigger"), c));
    }
};

QTEST_GUILESS_MAIN(TestSelectRules)
#include "tst_selectrules.moc"
