// Button conditions and the command catalog.
#include "core/CommandCatalog.h"
#include "core/CommandEngine.h"
#include "core/Condition.h"

#include <QTest>

class TestCondition : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void evaluates()
    {
        // selection and dual are true, view is details.
        const Condition::Resolver r = [](const QString &n, const QString &v) -> std::optional<bool> {
            if (n == QLatin1String("selection") || n == QLatin1String("dual"))
                return v.isEmpty();
            if (n == QLatin1String("flat"))
                return false;
            if (n == QLatin1String("view"))
                return v == QLatin1String("details");
            return std::nullopt;
        };
        QVERIFY(Condition::evaluate(QString(), r));
        QVERIFY(Condition::evaluate(QStringLiteral("selection"), r));
        QVERIFY(!Condition::evaluate(QStringLiteral("!selection"), r));
        QVERIFY(Condition::evaluate(QStringLiteral("selection && dual"), r));
        QVERIFY(!Condition::evaluate(QStringLiteral("selection && flat"), r));
        QVERIFY(Condition::evaluate(QStringLiteral("flat || dual"), r));
        QVERIFY(Condition::evaluate(QStringLiteral("View=Details"), r));
        QVERIFY(!Condition::evaluate(QStringLiteral("view=icons"), r));
        QVERIFY(Condition::evaluate(QStringLiteral("!(flat || view=icons) && selection"), r));
        // && binds tighter than ||
        QVERIFY(Condition::evaluate(QStringLiteral("dual || flat && flat"), r));

        QString err;
        QVERIFY(!Condition::evaluate(QStringLiteral("selection && bogus"), r, &err));
        QVERIFY(err.contains(QStringLiteral("bogus")));
        QVERIFY(!Condition::evaluate(QStringLiteral("(selection"), r, &err));
        QVERIFY(err.contains(QStringLiteral(")")));
        QVERIFY(!Condition::evaluate(QStringLiteral("selection &&"), r, &err));
        QVERIFY(!Condition::evaluate(QStringLiteral("view="), r, &err));
    }

    void checksAgainstKnownNames()
    {
        const QStringList known = CommandCatalog::conditionNames();
        QVERIFY(Condition::check(QStringLiteral("files && !remote && view=icons"), known));
        QString err;
        QVERIFY(!Condition::check(QStringLiteral("files && remot"), known, &err));
        QVERIFY(err.contains(QStringLiteral("remot")));
    }

    void catalogMatchesEngine()
    {
        QStringList catalog;
        for (const CommandInfo &c : CommandCatalog::commands())
            catalog << c.name;
        QCOMPARE(catalog, CommandEngine::commandNames());
        QVERIFY(CommandCatalog::find(QStringLiteral("set")));
        QVERIFY(CommandCatalog::help(QStringLiteral("Set")).contains(QStringLiteral("GROUPBY=")));
        QVERIFY(CommandCatalog::help().contains(QStringLiteral("CreateFolder")));
    }

    void completes()
    {
        QCOMPARE(CommandCatalog::complete(QStringLiteral("Cr")), QStringList{QStringLiteral("CreateFolder")});
        QCOMPARE(CommandCatalog::complete(QStringLiteral("C")).size(), 6); // Checksum, Clean, Clipboard, Collection, Copy, CreateFolder
        QCOMPARE(CommandCatalog::complete(QStringLiteral("Set GROUPB")), QStringList{QStringLiteral("Set GROUPBY=")});
        QCOMPARE(CommandCatalog::complete(QStringLiteral("Set DUAL=on VIEW=ic")), QStringList{QStringLiteral("Set DUAL=on VIEW=icons")});
        QCOMPARE(CommandCatalog::complete(QStringLiteral("help Se")), (QStringList{QStringLiteral("help Search"), QStringLiteral("help Select"), QStringLiteral("help Server"), QStringLiteral("help Set"), QStringLiteral("help SetAttr"), QStringLiteral("help Settings")}));
        QVERIFY(CommandCatalog::complete(QStringLiteral("Bogus x")).isEmpty());
    }

    void catalogButtonsAreValid()
    {
        for (const CatalogButton &b : CommandCatalog::buttons()) {
            if (!b.dynamic.isEmpty())
                continue; // a self-filling menu has no command
            const QString name = b.left.section(QLatin1Char(' '), 0, 0);
            QVERIFY2(CommandCatalog::find(name), qPrintable(b.label));
        }
    }
};

QTEST_GUILESS_MAIN(TestCondition)
#include "tst_condition.moc"
