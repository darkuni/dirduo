#include "core/CommandParser.h"

#include <QRegularExpression>

bool ParsedCommand::hasFlag(const QString &flag) const
{
    if (args.contains(flag.toUpper()))
        return true;
    for (const QString &p : positional) {
        if (p.compare(flag, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString ParsedCommand::value(const QString &key) const
{
    const QStringList v = args.value(key.toUpper());
    return v.isEmpty() ? QString() : v.first();
}

QStringList ParsedCommand::values(const QString &key) const
{
    return args.value(key.toUpper());
}

QStringList ParsedCommand::positionalExcept(const QStringList &flags) const
{
    QStringList out;
    for (const QString &p : positional) {
        bool isFlag = false;
        for (const QString &f : flags) {
            if (p.compare(f, Qt::CaseInsensitive) == 0) {
                isFlag = true;
                break;
            }
        }
        if (!isFlag)
            out << p;
    }
    return out;
}

namespace CommandParser {

namespace {

// A token as typed: its text and whether any part was quoted (quoted text is
// never split into KEY=value).
struct RawToken
{
    QString text;
    int keyEnd = -1; // index of '=' when the token looks like KEY=value
};

QVector<RawToken> splitRaw(const QString &line, bool *ok)
{
    QVector<RawToken> tokens;
    QString current;
    bool inQuotes = false;
    bool any = false;
    int keyEnd = -1;
    bool quotedSoFar = false;
    static const QRegularExpression keyRe(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));

    auto flush = [&]() {
        if (any) {
            RawToken t;
            t.text = current;
            t.keyEnd = keyEnd;
            tokens.append(t);
        }
        current.clear();
        any = false;
        keyEnd = -1;
        quotedSoFar = false;
    };

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            any = true;
            quotedSoFar = true;
            continue;
        }
        if (!inQuotes && c.isSpace()) {
            flush();
            continue;
        }
        if (!inQuotes && c == QLatin1Char('=') && keyEnd == -1 && !quotedSoFar && keyRe.match(current).hasMatch())
            keyEnd = current.size();
        current += c;
        any = true;
    }
    if (ok)
        *ok = !inQuotes;
    flush();
    return tokens;
}

// Expands every {code} inside text. When the whole text is a single code, every
// value is returned separately; otherwise values are joined with spaces.
bool expandText(const QString &text, const CodeExpander &expander, QStringList &out, QString *error)
{
    static const QRegularExpression codeRe(QStringLiteral(R"(\{([A-Za-z][A-Za-z0-9_]*\$?(?:\|[^{}]*)?)\})"));
    if (!expander) {
        out << text;
        return true;
    }
    const auto whole = codeRe.match(text);
    if (whole.hasMatch() && whole.capturedStart() == 0 && whole.capturedLength() == text.size()) {
        QStringList values;
        bool handled = false;
        if (!expander(whole.captured(1), values, handled, error))
            return false;
        if (!handled)
            out << text;
        else
            out << values;
        return true;
    }

    QString result;
    int last = 0;
    auto it = codeRe.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        result += text.mid(last, m.capturedStart() - last);
        QStringList values;
        bool handled = false;
        if (!expander(m.captured(1), values, handled, error))
            return false;
        result += handled ? values.join(QLatin1Char(' ')) : m.captured(0);
        last = m.capturedEnd();
    }
    result += text.mid(last);
    out << result;
    return true;
}

} // namespace

QStringList tokenize(const QString &line, bool *ok)
{
    QStringList out;
    const auto raw = splitRaw(line, ok);
    for (const auto &t : raw)
        out << t.text;
    return out;
}

QVector<ParsedCommand> parse(const QString &script, const CodeExpander &expander, QString *error)
{
    QVector<ParsedCommand> commands;
    const QStringList lines = script.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("//")))
            continue;

        bool ok = true;
        const QVector<RawToken> tokens = splitRaw(line, &ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("Unterminated quote in: %1").arg(line);
            return {};
        }
        if (tokens.isEmpty())
            continue;

        ParsedCommand cmd;
        cmd.raw = line;
        cmd.name = tokens.first().text;
        for (int i = 1; i < tokens.size(); ++i) {
            const RawToken &t = tokens.at(i);
            if (t.keyEnd > 0) {
                const QString key = t.text.left(t.keyEnd).toUpper();
                QStringList values;
                if (!expandText(t.text.mid(t.keyEnd + 1), expander, values, error))
                    return {};
                if (values.isEmpty())
                    cmd.args[key]; // present, no value
                for (const QString &v : std::as_const(values))
                    cmd.args[key].append(v);
            } else {
                if (!expandText(t.text, expander, cmd.positional, error))
                    return {};
            }
        }
        commands.append(cmd);
    }
    return commands;
}

} // namespace CommandParser
