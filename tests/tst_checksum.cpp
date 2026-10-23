#include "core/Checksum.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestChecksum : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void knownValues()
    {
        const QByteArray abc("abc");
        QCOMPARE(Checksum::computeData(abc, Checksum::Algo::Md5), QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));
        QCOMPARE(Checksum::computeData(abc, Checksum::Algo::Sha1), QStringLiteral("a9993e364706816aba3e25717850c26c9cd0d89d"));
        QCOMPARE(Checksum::computeData(abc, Checksum::Algo::Sha256),
                 QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        QCOMPARE(Checksum::computeData("123456789", Checksum::Algo::Crc32), QStringLiteral("CBF43926"));
        QCOMPARE(Checksum::computeData(QByteArray(), Checksum::Algo::Crc32), QStringLiteral("00000000"));
    }

    void fromFileInPieces()
    {
        // Bigger than the read buffer, so it is hashed in several pieces.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("big.bin"));
        QByteArray data(3 * 1024 * 1024 + 17, Qt::Uninitialized);
        for (int i = 0; i < data.size(); ++i)
            data[i] = char((i * 31) ^ (i >> 7));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
        f.close();
        for (Checksum::Algo a : Checksum::all())
            QCOMPARE(Checksum::compute(path, a), Checksum::computeData(data, a));
        int calls = 0;
        QVERIFY(Checksum::compute(path, Checksum::Algo::Md5, [&](qint64) { return ++calls < 2; }).isEmpty()); // stopped
        QString error;
        QVERIFY(Checksum::compute(dir.filePath(QStringLiteral("nope")), Checksum::Algo::Md5, {}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void names()
    {
        Checksum::Algo a;
        QVERIFY(Checksum::fromName(QStringLiteral("SHA-256"), &a));
        QCOMPARE(a, Checksum::Algo::Sha256);
        QVERIFY(Checksum::fromName(QStringLiteral("sfv"), &a));
        QCOMPARE(a, Checksum::Algo::Crc32);
        QVERIFY(!Checksum::fromName(QStringLiteral("sha512"), &a));
        QVERIFY(Checksum::isListFile(QStringLiteral("Game.sfv")));
        QVERIFY(Checksum::isListFile(QStringLiteral("SHA256SUMS")));
        QVERIFY(Checksum::isListFile(QStringLiteral("disk.sha256")));
        QVERIFY(!Checksum::isListFile(QStringLiteral("disk.d64")));
    }

    void parseForms()
    {
        const QString gnu = QStringLiteral("900150983cd24fb0d6963f7d28e17f72  abc.txt\n"
                                           "D41D8CD98F00B204E9800998ECF8427E *sub\\empty.bin\n"
                                           "# a comment\n\n");
        const auto g = Checksum::parse(gnu, QStringLiteral("list.md5"));
        QCOMPARE(g.size(), 2);
        QCOMPARE(g.at(0).name, QStringLiteral("abc.txt"));
        QCOMPARE(g.at(1).name, QStringLiteral("sub/empty.bin"));
        QCOMPARE(g.at(1).hash, QStringLiteral("d41d8cd98f00b204e9800998ecf8427e"));
        QCOMPARE(g.at(1).algo, Checksum::Algo::Md5);

        const auto b = Checksum::parse(QStringLiteral("SHA256 (my file.iso) = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"),
                                       QStringLiteral("CHECKSUMS"));
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.at(0).name, QStringLiteral("my file.iso"));
        QCOMPARE(b.at(0).algo, Checksum::Algo::Sha256);

        const auto s = Checksum::parse(QStringLiteral("; made by something\nTurrican II.d64 cbf43926\nbad line\n"), QStringLiteral("set.sfv"));
        QCOMPARE(s.size(), 1);
        QCOMPARE(s.at(0).name, QStringLiteral("Turrican II.d64"));
        QCOMPARE(s.at(0).hash, QStringLiteral("CBF43926"));

        // By length when the name says nothing.
        const auto l = Checksum::parse(QStringLiteral("a9993e364706816aba3e25717850c26c9cd0d89d  abc\n"), QStringLiteral("hashes.txt"));
        QCOMPARE(l.size(), 1);
        QCOMPARE(l.at(0).algo, Checksum::Algo::Sha1);
    }

    void formatRoundTrip()
    {
        const QList<Checksum::Entry> in = {{QStringLiteral("a b.txt"), QStringLiteral("900150983cd24fb0d6963f7d28e17f72"), Checksum::Algo::Md5},
                                           {QStringLiteral("dir/c"), QStringLiteral("d41d8cd98f00b204e9800998ecf8427e"), Checksum::Algo::Md5}};
        const auto out = Checksum::parse(Checksum::format(in, Checksum::Algo::Md5), QStringLiteral("x.md5"));
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0).name, in.at(0).name);
        QCOMPARE(out.at(1).hash, in.at(1).hash);
        const QList<Checksum::Entry> crc = {{QStringLiteral("game one.d64"), QStringLiteral("CBF43926"), Checksum::Algo::Crc32}};
        const auto back = Checksum::parse(Checksum::format(crc, Checksum::Algo::Crc32), QStringLiteral("x.sfv"));
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.at(0).name, QStringLiteral("game one.d64"));
    }
};

QTEST_GUILESS_MAIN(TestChecksum)
#include "tst_checksum.moc"
