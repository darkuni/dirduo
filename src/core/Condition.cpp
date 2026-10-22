#include "core/Condition.h"

#include <QStringList>

namespace {

class Parser
{
public:
    Parser(const QString &text, const Condition::Resolver &resolve)
        : m_text(text)
        , m_resolve(resolve)
    {
    }

    bool parse(bool &out)
    {
        out = parseOr();
        skipSpace();
        if (m_error.isEmpty() && m_pos < m_text.size())
            fail(QStringLiteral("Unexpected \"%1\".").arg(m_text.mid(m_pos)));
        return m_error.isEmpty();
    }

    QString error() const { return m_error; }

private:
    void skipSpace()
    {
        while (m_pos < m_text.size() && m_text.at(m_pos).isSpace())
            ++m_pos;
    }

    bool take(const QString &token)
    {
        skipSpace();
        if (m_text.mid(m_pos, token.size()) == token) {
            m_pos += token.size();
            return true;
        }
        return false;
    }

    void fail(const QString &msg)
    {
        if (m_error.isEmpty())
            m_error = msg;
    }

    bool parseOr()
    {
        bool v = parseAnd();
        while (m_error.isEmpty() && take(QStringLiteral("||"))) {
            const bool r = parseAnd();
            v = v || r; // evaluate both sides so errors surface either way
        }
        return v;
    }

    bool parseAnd()
    {
        bool v = parseNot();
        while (m_error.isEmpty() && take(QStringLiteral("&&"))) {
            const bool r = parseNot();
            v = v && r;
        }
        return v;
    }

    bool parseNot()
    {
        if (take(QStringLiteral("!")))
            return !parseNot();
        if (take(QStringLiteral("("))) {
            const bool v = parseOr();
            if (!take(QStringLiteral(")")))
                fail(QStringLiteral("Missing \")\"."));
            return v;
        }
        return parseAtom();
    }

    static bool nameChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

    bool parseAtom()
    {
        skipSpace();
        const int start = m_pos;
        while (m_pos < m_text.size() && nameChar(m_text.at(m_pos)))
            ++m_pos;
        const QString name = m_text.mid(start, m_pos - start).toLower();
        if (name.isEmpty()) {
            fail(m_pos < m_text.size() ? QStringLiteral("Unexpected \"%1\".").arg(m_text.mid(m_pos))
                                       : QStringLiteral("The condition ends too early."));
            return false;
        }
        QString value;
        skipSpace();
        if (m_pos < m_text.size() && m_text.at(m_pos) == QLatin1Char('=')) {
            ++m_pos;
            skipSpace();
            const int vs = m_pos;
            while (m_pos < m_text.size() && !m_text.at(m_pos).isSpace() && m_text.at(m_pos) != QLatin1Char(')')
                   && m_text.at(m_pos) != QLatin1Char('&') && m_text.at(m_pos) != QLatin1Char('|'))
                ++m_pos;
            value = m_text.mid(vs, m_pos - vs).toLower();
            if (value.isEmpty()) {
                fail(QStringLiteral("%1= needs a value.").arg(name));
                return false;
            }
        }
        const std::optional<bool> r = m_resolve(name, value);
        if (!r) {
            fail(QStringLiteral("Unknown condition \"%1\".").arg(name));
            return false;
        }
        return *r;
    }

    QString m_text;
    const Condition::Resolver &m_resolve;
    int m_pos = 0;
    QString m_error;
};

} // namespace

namespace Condition {

bool evaluate(const QString &expr, const Resolver &resolve, QString *error)
{
    if (expr.trimmed().isEmpty())
        return true;
    Parser p(expr, resolve);
    bool out = false;
    if (!p.parse(out)) {
        if (error)
            *error = p.error();
        return false;
    }
    return out;
}

bool check(const QString &expr, const QStringList &knownNames, QString *error)
{
    if (expr.trimmed().isEmpty())
        return true;
    const Resolver any = [&knownNames](const QString &name, const QString &) -> std::optional<bool> {
        if (!knownNames.isEmpty() && !knownNames.contains(name))
            return std::nullopt;
        return true;
    };
    Parser p(expr, any);
    bool out = false;
    if (!p.parse(out)) {
        if (error)
            *error = p.error();
        return false;
    }
    return true;
}

} // namespace Condition
