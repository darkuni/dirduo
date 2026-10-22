#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

// One parsed command line in Opus style:
//   Copy MOVE TO=/downloads WHENEXISTS=rename
//   Go "/run/media/deck/My Card" NEWTAB
// Bare words are kept in order in `positional`; KEY=value pairs go in `args`
// with upper-case keys. A key may repeat (FILE=a FILE=b).
struct ParsedCommand
{
    QString name;
    QStringList positional;
    QMap<QString, QStringList> args;
    QString raw;

    bool is(const QString &n) const { return name.compare(n, Qt::CaseInsensitive) == 0; }

    // A bare word or a KEY= present, case-insensitive: hasFlag("move").
    bool hasFlag(const QString &flag) const;
    QString value(const QString &key) const;
    QStringList values(const QString &key) const;
    bool hasArg(const QString &key) const { return args.contains(key.toUpper()); }

    // Positional words that are not one of the given flags (paths, mostly).
    QStringList positionalExcept(const QStringList &flags) const;
};

namespace CommandParser {

// Expands one argument code such as "sourcepath" or "filepath$" (without the
// braces) into one or more values. Return false with error set to abort; return
// true with out empty to expand to nothing. Unknown codes should return true
// and leave `handled` false so the braces stay as literal text.
using CodeExpander = std::function<bool(const QString &code, QStringList &out, bool &handled, QString *error)>;

// Splits a line into tokens. Double quotes group spaces and are removed.
// ok is false on an unterminated quote.
QStringList tokenize(const QString &line, bool *ok = nullptr);

// Parses a whole button function: one command per line, blank lines and lines
// starting with "//" ignored.
QVector<ParsedCommand> parse(const QString &script, const CodeExpander &expander, QString *error);

} // namespace CommandParser
