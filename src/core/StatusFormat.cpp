#include "core/StatusFormat.h"

#include <QRegularExpression>
#include <QTextDocumentFragment>

namespace StatusFormat {

QString defaultDefinition(bool counts, bool selection, bool free)
{
    QString d;
    if (counts)
        d += QStringLiteral("{folders}, {files}");
    if (selection)
        d += counts ? QStringLiteral("[ - {selected} ({sb})]") : QStringLiteral("[{selected} ({sb})]");
    d += QStringLiteral("[ - filter \"{filter}\"[, {hidden} hidden]][ - {flat}]");
    if (free)
        d += QStringLiteral("{right}[{df} free of {dt}]");
    return d;
}

QList<QPair<QString, QString>> samples()
{
    return {
        {QStringLiteral("DirDuo standard"), defaultDefinition()},
        {QStringLiteral("Classic"),
         QStringLiteral("{i:folder} {sd} / {td} {|} {i:file} {sf} / {tf} [{|} <#e04040><b>{hidden} hidden</b></#>] {|} {sb} / {tb}"
                        "{right}<b>{role}</b> {|} {i:drive} {df} {bar}")},
        {QStringLiteral("Minimal"), QStringLiteral("{items}[ - {selected}]{right}[{df} free]")},
        {QStringLiteral("Focused item"),
         QStringLiteral("{name}[ - {size}][ - {modified}]{right}[{selected} ({sb}) {|}] {df} free {bar}")},
        {QStringLiteral("Drive details"),
         QStringLiteral("{folders}, {files}[ - {selected} ({sb})]{right}{i:drive} {du} used of {dt} ({dp}%) {bar} {df} free")},
    };
}

QList<CodeInfo> codes()
{
    auto c = [](const char *g, const char *code, const char *t) {
        return CodeInfo{QString::fromUtf8(g), QString::fromUtf8(code), QString::fromUtf8(t)};
    };
    return {
        c("Selection counts", "{sd}", "Selected folders"), c("Selection counts", "{sf}", "Selected files"),
        c("Selection counts", "{si}", "Selected items"), c("Selection counts", "{sb}", "Size of the selected files"),
        c("Selection counts", "{selected}", "\"3 selected\""),
        c("Total counts", "{td}", "Folders in the list"), c("Total counts", "{tf}", "Files in the list"),
        c("Total counts", "{ti}", "Items in the list"), c("Total counts", "{tb}", "Size of the files in the list"),
        c("Total counts", "{folders}", "\"3 folders\""), c("Total counts", "{files}", "\"12 files\""),
        c("Total counts", "{items}", "\"15 items\""),
        c("Hidden and filter", "{hidden}", "Items hidden (filter or hidden files)"),
        c("Hidden and filter", "{filter}", "The filter text"), c("Hidden and filter", "{flat}", "\"flat view\" when on"),
        c("Item in focus", "{name}", "Its name"), c("Item in focus", "{size}", "Its size"),
        c("Item in focus", "{modified}", "When it changed"), c("Item in focus", "{type}", "Its type"),
        c("Disk space", "{df}", "Free space"), c("Disk space", "{dt}", "Size of the drive"), c("Disk space", "{du}", "Used space"),
        c("Disk space", "{dp}", "Percent used"), c("Disk space", "{bar}", "Bar graph of the drive"),
        c("Lister and tab", "{path}", "The folder"), c("Lister and tab", "{folder}", "The folder's name"),
        c("Lister and tab", "{tabs}", "Tabs in this display"), c("Lister and tab", "{role}", "SOURCE or DEST"),
        c("Layout", "{right}", "What follows goes on the right"), c("Layout", "{|}", "A divider"),
        c("Layout", "[ ]", "Shown only when a code inside has a value"),
        c("Icons", "{i:folder}", "Folder"), c("Icons", "{i:file}", "File"), c("Icons", "{i:drive}", "Drive"),
        c("Icons", "{i:info}", "Information"), c("Icons", "{i:hidden}", "Hidden"), c("Icons", "{i:filter}", "Filter"),
        c("Icons", "{i:lock}", "Lock"),
        c("Text styles", "<b></b>", "Bold"), c("Text styles", "<i></i>", "Italic"), c("Text styles", "<u></u>", "Underline"),
        c("Text styles", "<#ff0000></#>", "Color"),
    };
}

namespace {

bool hasValue(const QString &v)
{
    return !v.isEmpty() && v != QLatin1String("0");
}

struct Parser
{
    const QString &def;
    const QHash<QString, QString> &values;
    QList<Segment> out;
    bool right = false;
    QString pending; // rich text not yet in a segment
    int pos = 0;

    void flush()
    {
        if (pending.isEmpty())
            return;
        Segment s;
        s.html = pending;
        s.right = right;
        out << s;
        pending.clear();
    }

    // Renders from pos until end or a closing ']' (when nested). Returns the
    // segments made and whether any code had a value.
    bool run(bool nested)
    {
        bool any = false;
        while (pos < def.size()) {
            const QChar c = def.at(pos);
            if (c == QLatin1Char(']') && nested) {
                ++pos;
                return any;
            }
            if (c == QLatin1Char('[')) {
                ++pos;
                // A section on its own: kept only if a code in it had a value.
                flush();
                const int savedCount = int(out.size());
                const bool savedRight = right;
                // (A section inside another does not make the outer one show.)
                if (!run(true)) {
                    while (out.size() > savedCount)
                        out.removeLast();
                    pending.clear();
                    right = savedRight;
                }
                continue;
            }
            if (c == QLatin1Char('{')) {
                const int end = def.indexOf(QLatin1Char('}'), pos);
                if (end < 0) {
                    pending += QStringLiteral("{").toHtmlEscaped();
                    ++pos;
                    continue;
                }
                const QString code = def.mid(pos + 1, end - pos - 1);
                pos = end + 1;
                if (code == QLatin1String("right") || code == QLatin1String(">")) {
                    flush();
                    right = true;
                } else if (code == QLatin1String("|")) {
                    pending += QStringLiteral("<span style=\"opacity:0.5\">&nbsp;│&nbsp;</span>");
                } else if (code.startsWith(QLatin1String("i:"))) {
                    flush();
                    Segment s;
                    s.kind = Segment::Kind::Icon;
                    s.icon = code.mid(2);
                    s.right = right;
                    out << s;
                } else if (code == QLatin1String("bar")) {
                    const QString used = values.value(QStringLiteral("dp"));
                    if (!used.isEmpty()) {
                        flush();
                        Segment s;
                        s.kind = Segment::Kind::Bar;
                        s.fraction = qBound(0.0, used.toDouble() / 100.0, 1.0);
                        s.low = values.value(QStringLiteral("low")) == QLatin1String("1");
                        s.right = right;
                        out << s;
                        any = true;
                    }
                } else if (values.contains(code)) {
                    const QString v = values.value(code);
                    any = any || hasValue(v);
                    if (code == QLatin1String("df") && values.value(QStringLiteral("low")) == QLatin1String("1"))
                        pending += QStringLiteral("<span style=\"color:#d04030\">%1</span>").arg(v.toHtmlEscaped());
                    else
                        pending += v.toHtmlEscaped();
                } else {
                    pending += (QLatin1Char('{') + code + QLatin1Char('}')).toHtmlEscaped();
                }
                continue;
            }
            if (c == QLatin1Char('<')) {
                const int end = def.indexOf(QLatin1Char('>'), pos);
                if (end > pos) {
                    const QString tag = def.mid(pos + 1, end - pos - 1).trimmed();
                    static const QRegularExpression color(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
                    QString html;
                    if (tag == QLatin1String("b") || tag == QLatin1String("i") || tag == QLatin1String("u"))
                        html = QLatin1Char('<') + tag + QLatin1Char('>');
                    else if (tag == QLatin1String("/b") || tag == QLatin1String("/i") || tag == QLatin1String("/u"))
                        html = QLatin1Char('<') + tag + QLatin1Char('>');
                    else if (color.match(tag).hasMatch())
                        html = QStringLiteral("<span style=\"color:%1\">").arg(tag);
                    else if (tag == QLatin1String("/#"))
                        html = QStringLiteral("</span>");
                    if (!html.isEmpty()) {
                        pending += html;
                        pos = end + 1;
                        continue;
                    }
                }
            }
            pending += QString(c).toHtmlEscaped();
            ++pos;
        }
        return any;
    }
};

} // namespace

QList<Segment> render(const QString &definition, const QHash<QString, QString> &values)
{
    Parser p{definition, values, {}, false, {}, 0};
    p.run(false);
    p.flush();
    // Neighbouring text on the same side joins up.
    QList<Segment> merged;
    for (const Segment &s : std::as_const(p.out)) {
        if (!merged.isEmpty() && s.kind == Segment::Kind::Text && merged.last().kind == Segment::Kind::Text
            && merged.last().right == s.right)
            merged.last().html += s.html;
        else
            merged << s;
    }
    return merged;
}

QString plainText(const QList<Segment> &segments, bool right)
{
    QString out;
    for (const Segment &s : segments) {
        if (s.right != right || s.kind != Segment::Kind::Text)
            continue;
        out += QTextDocumentFragment::fromHtml(s.html).toPlainText();
    }
    return out.replace(QChar(0xA0), QLatin1Char(' ')).trimmed();
}

bool check(const QString &definition, QString *error)
{
    int depth = 0;
    for (int i = 0; i < definition.size(); ++i) {
        const QChar c = definition.at(i);
        if (c == QLatin1Char('{')) {
            const int end = definition.indexOf(QLatin1Char('}'), i);
            if (end < 0) {
                if (error)
                    *error = QStringLiteral("A { is not closed.");
                return false;
            }
            i = end;
        } else if (c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(']')) {
            if (--depth < 0) {
                if (error)
                    *error = QStringLiteral("A ] has no [ before it.");
                return false;
            }
        }
    }
    if (depth != 0) {
        if (error)
            *error = QStringLiteral("A [ is not closed.");
        return false;
    }
    return true;
}

} // namespace StatusFormat
