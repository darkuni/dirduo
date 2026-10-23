#include "core/StatusFormat.h"

#include <QTest>

class TestStatusFormat : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void codesAndSections()
    {
        const QHash<QString, QString> v = {{QStringLiteral("td"), QStringLiteral("3")},  {QStringLiteral("tf"), QStringLiteral("12")},
                                           {QStringLiteral("si"), QStringLiteral("0")},  {QStringLiteral("hidden"), QStringLiteral("2")},
                                           {QStringLiteral("filter"), QString()},        {QStringLiteral("df"), QStringLiteral("2 GB")},
                                           {QStringLiteral("dp"), QStringLiteral("75")}, {QStringLiteral("low"), QStringLiteral("0")}};
        const auto segs = StatusFormat::render(QStringLiteral("{td} / {tf}[ - {si} selected][ - \"{filter}\"][, {hidden} hidden] {nope}{right}{df} free {bar}"), v);
        QCOMPARE(StatusFormat::plainText(segs, false), QStringLiteral("3 / 12, 2 hidden {nope}"));
        QCOMPARE(StatusFormat::plainText(segs, true), QStringLiteral("2 GB free"));
        bool bar = false;
        for (const auto &s : segs) {
            if (s.kind == StatusFormat::Segment::Kind::Bar) {
                bar = true;
                QVERIFY(s.right);
                QCOMPARE(s.fraction, 0.75);
            }
        }
        QVERIFY(bar);
    }

    void stylesAndIcons()
    {
        const auto segs = StatusFormat::render(QStringLiteral("{i:folder} <b>{td}</b> <#ff0000>x</#> <script> a<b"),
                                               {{QStringLiteral("td"), QStringLiteral("<4>")}});
        QCOMPARE(segs.first().kind, StatusFormat::Segment::Kind::Icon);
        QCOMPARE(segs.first().icon, QStringLiteral("folder"));
        QVERIFY(segs.at(1).html.contains(QStringLiteral("<b>&lt;4&gt;</b>")));
        QVERIFY(segs.at(1).html.contains(QStringLiteral("color:#ff0000")));
        QVERIFY(segs.at(1).html.contains(QStringLiteral("&lt;script&gt;"))); // not a tag it knows
        QCOMPARE(StatusFormat::plainText(segs, false), QStringLiteral("<4> x <script> a<b"));
    }

    void nestedAndLow()
    {
        const QHash<QString, QString> v = {{QStringLiteral("a"), QStringLiteral("1")}, {QStringLiteral("b"), QString()},
                                           {QStringLiteral("df"), QStringLiteral("1 MB")}, {QStringLiteral("low"), QStringLiteral("1")}};
        QCOMPARE(StatusFormat::plainText(StatusFormat::render(QStringLiteral("x[ a{a}[ b{b}]]y"), v), false), QStringLiteral("x a1y"));
        QCOMPARE(StatusFormat::plainText(StatusFormat::render(QStringLiteral("x[ b{b}[ a{a}]]y"), v), false), QStringLiteral("xy"));
        QVERIFY(StatusFormat::render(QStringLiteral("{df}"), v).first().html.contains(QStringLiteral("#d04030")));
        QString err;
        QVERIFY(StatusFormat::check(QStringLiteral("[{a}]"), &err));
        QVERIFY(!StatusFormat::check(QStringLiteral("[{a}"), &err));
        QVERIFY(!StatusFormat::check(QStringLiteral("{a"), &err));
        QVERIFY(!StatusFormat::check(QStringLiteral("a]"), &err));
        for (const auto &s : StatusFormat::samples())
            QVERIFY2(StatusFormat::check(s.second, &err), qPrintable(s.first));
        QCOMPARE(StatusFormat::defaultDefinition(false, false, false).contains(QStringLiteral("{folders}")), false);
    }
};

QTEST_MAIN(TestStatusFormat)
#include "tst_statusformat.moc"
