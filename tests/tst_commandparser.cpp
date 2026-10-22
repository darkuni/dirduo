#include "core/CommandParser.h"

#include <QTest>

class TestCommandParser : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void tokenizeQuotes()
    {
        bool ok = false;
        QCOMPARE(CommandParser::tokenize(QStringLiteral("Go \"/run/media/deck/My Card\" NEWTAB"), &ok),
                 (QStringList{QStringLiteral("Go"), QStringLiteral("/run/media/deck/My Card"), QStringLiteral("NEWTAB")}));
        QVERIFY(ok);
        CommandParser::tokenize(QStringLiteral("Go \"/unterminated"), &ok);
        QVERIFY(!ok);
    }

    void keysAndFlags()
    {
        QString error;
        const auto cmds = CommandParser::parse(QStringLiteral("Copy MOVE TO=\"/a b/c\" WHENEXISTS=rename AS"), {}, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(cmds.size(), 1);
        const ParsedCommand &c = cmds.first();
        QVERIFY(c.is(QStringLiteral("copy")));
        QVERIFY(c.hasFlag(QStringLiteral("move")));
        QVERIFY(c.hasFlag(QStringLiteral("AS")));
        QCOMPARE(c.value(QStringLiteral("to")), QStringLiteral("/a b/c"));
        QCOMPARE(c.value(QStringLiteral("WHENEXISTS")), QStringLiteral("rename"));
        QVERIFY(!c.hasFlag(QStringLiteral("QUIET")));
    }

    void quotedEqualsIsPositional()
    {
        QString error;
        const auto cmds = CommandParser::parse(QStringLiteral("Go \"a=b\""), {}, &error);
        QCOMPARE(cmds.first().positional, QStringList{QStringLiteral("a=b")});
        QVERIFY(cmds.first().args.isEmpty());
    }

    void multipleLinesAndComments()
    {
        QString error;
        const auto cmds = CommandParser::parse(QStringLiteral("// camera dump\nGo /downloads\n\n  Select PATTERN=*.mp3\nCopy\n"), {}, &error);
        QCOMPARE(cmds.size(), 3);
        QCOMPARE(cmds.at(1).value(QStringLiteral("PATTERN")), QStringLiteral("*.mp3"));
    }

    void codesExpandToSeparateTokens()
    {
        auto expander = [](const QString &code, QStringList &out, bool &handled, QString *) {
            handled = true;
            if (code == QLatin1String("filepath"))
                out << QStringLiteral("/x/one file.txt") << QStringLiteral("/x/two.txt");
            else if (code == QLatin1String("destpath"))
                out << QStringLiteral("/dest with space");
            else
                handled = false;
            return true;
        };
        QString error;
        const auto cmds = CommandParser::parse(QStringLiteral("Copy {filepath} TO={destpath} FILE={filepath}"), expander, &error);
        QVERIFY(error.isEmpty());
        const ParsedCommand &c = cmds.first();
        QCOMPARE(c.positional, (QStringList{QStringLiteral("/x/one file.txt"), QStringLiteral("/x/two.txt")}));
        QCOMPARE(c.value(QStringLiteral("TO")), QStringLiteral("/dest with space"));
        QCOMPARE(c.values(QStringLiteral("FILE")).size(), 2);
    }

    void unknownCodesStayLiteral()
    {
        auto expander = [](const QString &, QStringList &, bool &handled, QString *) {
            handled = false;
            return true;
        };
        QString error;
        const auto cmds = CommandParser::parse(QStringLiteral("Go /a/{weird}/b"), expander, &error);
        QCOMPARE(cmds.first().positional.first(), QStringLiteral("/a/{weird}/b"));
    }

    void expanderCanAbort()
    {
        auto expander = [](const QString &, QStringList &, bool &handled, QString *err) {
            handled = true;
            *err = QStringLiteral("nothing selected");
            return false;
        };
        QString error;
        const auto cmds = CommandParser::parse(QStringLiteral("Copy {filepath$}"), expander, &error);
        QVERIFY(cmds.isEmpty());
        QCOMPARE(error, QStringLiteral("nothing selected"));
    }

    void embeddedCodeJoins()
    {
        auto expander = [](const QString &code, QStringList &out, bool &handled, QString *) {
            handled = code == QLatin1String("sourcepath");
            out << QStringLiteral("/src");
            return true;
        };
        QString error;
        const auto cmds = CommandParser::parse(QStringLiteral("Go {sourcepath}/sub"), expander, &error);
        QCOMPARE(cmds.first().positional.first(), QStringLiteral("/src/sub"));
    }
};

QTEST_GUILESS_MAIN(TestCommandParser)
#include "tst_commandparser.moc"
