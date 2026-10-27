#include "core/RenameEngine.h"

#include "core/FileOps.h"
#include "core/Location.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

// ---------------------------------------------------------------- options

QString RenameOptions::modeName(Mode m)
{
    switch (m) {
    case Mode::FindReplace:
        return QStringLiteral("findreplace");
    case Mode::RegExp:
        return QStringLiteral("regexp");
    default:
        return QStringLiteral("wildcard");
    }
}

bool RenameOptions::modeFromName(const QString &name, Mode &out)
{
    const QString n = name.toLower();
    if (n == QLatin1String("wildcard") || n == QLatin1String("standard"))
        out = Mode::Standard;
    else if (n == QLatin1String("findreplace") || n == QLatin1String("find"))
        out = Mode::FindReplace;
    else if (n == QLatin1String("regexp") || n == QLatin1String("regex"))
        out = Mode::RegExp;
    else
        return false;
    return true;
}

QString RenameOptions::caseName(Case c)
{
    switch (c) {
    case Case::Lower:
        return QStringLiteral("lower");
    case Case::Upper:
        return QStringLiteral("upper");
    case Case::Title:
        return QStringLiteral("title");
    case Case::Sentence:
        return QStringLiteral("sentence");
    default:
        return QStringLiteral("keep");
    }
}

bool RenameOptions::caseFromName(const QString &name, Case &out)
{
    const QString n = name.toLower();
    if (n == QLatin1String("keep") || n.isEmpty())
        out = Case::Keep;
    else if (n == QLatin1String("lower"))
        out = Case::Lower;
    else if (n == QLatin1String("upper"))
        out = Case::Upper;
    else if (n == QLatin1String("title"))
        out = Case::Title;
    else if (n == QLatin1String("sentence"))
        out = Case::Sentence;
    else
        return false;
    return true;
}

// ---------------------------------------------------------------- naming

namespace {

void splitName(const QString &name, QString &stem, QString &ext)
{
    // A leading dot is part of the name (.bashrc has no extension).
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        stem = name;
        ext.clear();
    } else {
        stem = name.left(dot);
        ext = name.mid(dot + 1);
    }
}

QString counterText(int value, int width)
{
    QString s = QString::number(qAbs(value)).rightJustified(qMax(1, width), QLatin1Char('0'));
    return value < 0 ? QLatin1Char('-') + s : s;
}

// Replaces [tokens]; in Standard mode also bare # runs.
QString expandTokens(const QString &pattern, const RenameOptions &o, int counter, const QDateTime &modified,
                     const QString &stem, const QString &ext, const QString &parent, bool bareHashes)
{
    QString out;
    for (int i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        if (c == QLatin1Char('\\') && bareHashes && i + 1 < pattern.size() && pattern.at(i + 1) == QLatin1Char('#')) {
            out += QLatin1Char('#'); // \# is a literal # in Standard mode
            ++i;
            continue;
        }
        if (c == QLatin1Char('[')) {
            const int close = pattern.indexOf(QLatin1Char(']'), i + 1);
            if (close > i) {
                const QString tok = pattern.mid(i + 1, close - i - 1);
                const QString t = tok.toLower();
                bool known = true;
                if (!tok.isEmpty() && tok.count(QLatin1Char('#')) == tok.size())
                    out += counterText(counter, qMax(o.counterWidth, int(tok.size())));
                else if (t == QLatin1String("date"))
                    out += QDate::currentDate().toString(Qt::ISODate);
                else if (t == QLatin1String("mdate"))
                    out += (modified.isValid() ? modified : QDateTime::currentDateTime()).date().toString(Qt::ISODate);
                else if (t == QLatin1String("mtime"))
                    out += (modified.isValid() ? modified : QDateTime::currentDateTime()).time().toString(QStringLiteral("HHmmss"));
                else if (t == QLatin1String("name"))
                    out += stem;
                else if (t == QLatin1String("ext"))
                    out += ext;
                else if (t == QLatin1String("parent"))
                    out += parent;
                else
                    known = false;
                if (known) {
                    i = close;
                    continue;
                }
            }
        }
        if (bareHashes && c == QLatin1Char('#')) {
            int n = 0;
            while (i + n < pattern.size() && pattern.at(i + n) == QLatin1Char('#'))
                ++n;
            out += counterText(counter, qMax(o.counterWidth, n));
            i += n - 1;
            continue;
        }
        out += c;
    }
    return out;
}

QString applyCase(const QString &s, RenameOptions::Case c)
{
    switch (c) {
    case RenameOptions::Case::Lower:
        return s.toLower();
    case RenameOptions::Case::Upper:
        return s.toUpper();
    case RenameOptions::Case::Title: {
        QString out = s.toLower();
        bool start = true;
        for (QChar &ch : out) {
            if (ch.isLetter()) {
                if (start)
                    ch = ch.toUpper();
                start = false;
            } else {
                start = ch.isSpace() || ch == QLatin1Char('_') || ch == QLatin1Char('-') || ch == QLatin1Char('(')
                    || ch == QLatin1Char('[') || ch == QLatin1Char('.');
            }
        }
        // Roman numerals stay capitals: Turrican II, Final Fantasy VII.
        static const QRegularExpression roman(QStringLiteral("\\b(Ii|Iii|Iv|Vi|Vii|Viii|Ix|Xi|Xii|Xiii)\\b"));
        QString fixed;
        qsizetype last = 0;
        auto it = roman.globalMatch(out);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            fixed += out.mid(last, m.capturedStart() - last) + m.captured().toUpper();
            last = m.capturedEnd();
        }
        return fixed + out.mid(last);
    }
    case RenameOptions::Case::Sentence: {
        QString out = s.toLower();
        for (QChar &ch : out) {
            if (ch.isLetter()) {
                ch = ch.toUpper();
                break;
            }
        }
        return out;
    }
    default:
        return s;
    }
}

QString wildcardToRegex(const QString &wild, int *groups)
{
    QString re;
    int n = 0;
    for (const QChar c : wild) {
        if (c == QLatin1Char('*')) {
            re += QStringLiteral("(.*)");
            ++n;
        } else if (c == QLatin1Char('?')) {
            re += QStringLiteral("(.)");
            ++n;
        } else {
            re += QRegularExpression::escape(QString(c));
        }
    }
    if (groups)
        *groups = n;
    return QLatin1Char('^') + re + QLatin1Char('$');
}

} // namespace

QString RenameEngine::newNameFor(const QString &name, const RenameOptions &o, int counter, const QDateTime &modified,
                                 const QString &parent, bool *matched)
{
    if (matched)
        *matched = true;
    QString stem;
    QString ext;
    splitName(name, stem, ext);
    // What the pattern works on: the whole name, or the name without extension.
    const QString subject = o.ignoreExtension ? stem : name;
    QString result;

    switch (o.mode) {
    case RenameOptions::Mode::Standard: {
        const QString oldPat = o.oldPattern.isEmpty() ? QStringLiteral("*") : o.oldPattern;
        int groups = 0;
        const QRegularExpression re(wildcardToRegex(oldPat, &groups),
                                    o.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = re.match(subject);
        if (!m.hasMatch()) {
            if (matched)
                *matched = false;
            return name;
        }
        if (o.newPattern.isEmpty()) {
            result = subject;
            break;
        }
        // * and ? in the new name take what the old ones matched, in order;
        // with both patterns ending in .*, the last one is the extension
        // (*.* -> "Holiday #.*" keeps each file's own extension).
        const QString expanded = expandTokens(o.newPattern, o, counter, modified, stem, ext, parent, true);
        int wilds = 0;
        for (const QChar c : expanded)
            wilds += (c == QLatin1Char('*') || c == QLatin1Char('?')) ? 1 : 0;
        const bool extTail = oldPat.endsWith(QLatin1String(".*")) && expanded.endsWith(QLatin1String(".*")) && groups > 0;
        int g = 1;
        int seen = 0;
        for (const QChar c : expanded) {
            if (c == QLatin1Char('*') || c == QLatin1Char('?')) {
                ++seen;
                if (extTail && seen == wilds)
                    result += m.captured(groups);
                else
                    result += g <= groups ? m.captured(g++) : QString();
            } else {
                result += c;
            }
        }
        break;
    }
    case RenameOptions::Mode::FindReplace: {
        if (o.oldPattern.isEmpty()) {
            result = expandTokens(o.newPattern.isEmpty() ? subject : o.newPattern, o, counter, modified, stem, ext, parent, false);
            break;
        }
        const Qt::CaseSensitivity cs = o.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (!subject.contains(o.oldPattern, cs)) {
            if (matched)
                *matched = false;
            return name;
        }
        result = subject;
        result.replace(o.oldPattern, expandTokens(o.newPattern, o, counter, modified, stem, ext, parent, false), cs);
        break;
    }
    case RenameOptions::Mode::RegExp: {
        const QRegularExpression re(o.oldPattern,
                                    o.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid() || !re.match(subject).hasMatch()) {
            if (matched)
                *matched = false;
            return name;
        }
        // $1 works as well as \1.
        QString rep = expandTokens(o.newPattern, o, counter, modified, stem, ext, parent, false);
        QString fixed;
        for (int i = 0; i < rep.size(); ++i) {
            if (rep.at(i) == QLatin1Char('$') && i + 1 < rep.size() && rep.at(i + 1).isDigit())
                fixed += QLatin1Char('\\');
            else
                fixed += rep.at(i);
        }
        rep = fixed;
        result = subject;
        result.replace(re, rep);
        break;
    }
    }

    // Case, then the extension back on.
    QString newStem;
    QString newExt;
    if (o.ignoreExtension) {
        newStem = result;
        newExt = ext;
    } else {
        splitName(result, newStem, newExt);
    }
    newStem = applyCase(newStem, o.nameCase);
    if (o.extCase == RenameOptions::ExtCase::Lower)
        newExt = newExt.toLower();
    else if (o.extCase == RenameOptions::ExtCase::Upper)
        newExt = newExt.toUpper();
    if (o.trimSpaces) {
        newStem = newStem.simplified();
        newExt = newExt.trimmed();
    }
    return newExt.isEmpty() ? newStem : newStem + QLatin1Char('.') + newExt;
}

QList<RenameItem> RenameEngine::preview(const QStringList &paths, const RenameOptions &o)
{
    QList<RenameItem> items;
    int counter = o.counterStart;
    for (const QString &p : paths) {
        RenameItem it;
        it.path = Location::clean(p);
        it.oldName = Location::fileName(it.path);
        const QFileInfo fi(it.path);
        bool matched = true;
        it.newName = newNameFor(it.oldName, o, counter, fi.lastModified(), Location::fileName(Location::parent(it.path)), &matched);
        if (!matched) {
            it.status = RenameItem::Status::NoMatch;
            it.newName = it.oldName;
        } else {
            counter += o.counterStep;
            QString why;
            if (it.newName == it.oldName)
                it.status = RenameItem::Status::Unchanged;
            else if (!FileOps::isValidFileName(it.newName, &why)) {
                it.status = RenameItem::Status::Invalid;
                it.note = why;
            } else
                it.status = RenameItem::Status::Ok;
        }
        items << it;
    }

    // Conflicts: two items ending up with the same name in one folder, or a
    // name taken by something that is not itself being renamed away.
    QHash<QString, int> finalNames; // dir/newname -> count
    QSet<QString> leaving;          // paths whose name changes
    for (const RenameItem &it : std::as_const(items)) {
        finalNames[Location::join(Location::parent(it.path), it.newName)] += 1;
        if (it.status == RenameItem::Status::Ok)
            leaving.insert(it.path);
    }
    for (RenameItem &it : items) {
        if (it.status != RenameItem::Status::Ok)
            continue;
        const QString target = Location::join(Location::parent(it.path), it.newName);
        if (finalNames.value(target) > 1) {
            it.status = RenameItem::Status::Conflict;
            it.note = QStringLiteral("Another item gets this name too");
        } else if (QFileInfo::exists(target) && !leaving.contains(target)
                   && QFileInfo(target).canonicalFilePath() != QFileInfo(it.path).canonicalFilePath()) {
            it.status = RenameItem::Status::Conflict;
            it.note = QStringLiteral("Something here already has this name");
        }
    }
    return items;
}

bool RenameEngine::hasConflicts(const QList<RenameItem> &items)
{
    for (const RenameItem &it : items) {
        if (it.status == RenameItem::Status::Conflict || it.status == RenameItem::Status::Invalid)
            return true;
    }
    return false;
}

int RenameEngine::changeCount(const QList<RenameItem> &items)
{
    int n = 0;
    for (const RenameItem &it : items)
        n += it.willChange() ? 1 : 0;
    return n;
}

namespace {

// Two passes through temporary names, so A->B with B->A, and case-only
// changes on FAT-formatted SD cards, work. Rolls back on failure.
bool renameAll(const QList<QPair<QString, QString>> &moves, QString *error)
{
    QList<QPair<QString, QString>> toTemp;   // (temp, original)
    const QString tag = QString::number(QRandomGenerator::global()->generate(), 16);
    for (int i = 0; i < moves.size(); ++i) {
        const QString from = moves.at(i).first;
        const QString temp = Location::join(Location::parent(from), QStringLiteral(".dirduo-rename-%1-%2").arg(tag).arg(i));
        if (!QFile::rename(from, temp) && !QDir().rename(from, temp)) {
            for (auto it = toTemp.crbegin(); it != toTemp.crend(); ++it)
                QDir().rename(it->first, it->second);
            if (error)
                *error = QStringLiteral("Could not rename %1.").arg(Location::fileName(from));
            return false;
        }
        toTemp << qMakePair(temp, from);
    }
    QList<QPair<QString, QString>> finished; // (final, temp)
    for (int i = 0; i < moves.size(); ++i) {
        const QString temp = toTemp.at(i).first;
        const QString to = moves.at(i).second;
        if (QFileInfo::exists(to) || !QDir().rename(temp, to)) {
            for (auto it = finished.crbegin(); it != finished.crend(); ++it)
                QDir().rename(it->first, it->second);
            for (const auto &t : toTemp)
                QDir().rename(t.first, t.second);
            if (error)
                *error = QStringLiteral("Could not rename %1 to %2.").arg(Location::fileName(moves.at(i).first), Location::fileName(to));
            return false;
        }
        finished << qMakePair(to, temp);
    }
    return true;
}

} // namespace

bool RenameEngine::apply(const QList<RenameItem> &items, QList<QPair<QString, QString>> *done, QString *error)
{
    if (hasConflicts(items)) {
        if (error)
            *error = QStringLiteral("Some new names clash or are not allowed; nothing was renamed.");
        return false;
    }
    QList<QPair<QString, QString>> moves;
    for (const RenameItem &it : items) {
        if (!it.willChange())
            continue;
        if (!Location::isLocal(it.path)) {
            if (error)
                *error = QStringLiteral("Advanced Rename works in local folders for now.");
            return false;
        }
        moves << qMakePair(it.path, Location::join(Location::parent(it.path), it.newName));
    }
    if (moves.isEmpty())
        return true;
    if (!renameAll(moves, error))
        return false;
    if (done) {
        done->clear();
        for (const auto &m : moves)
            *done << qMakePair(m.second, m.first);
    }
    return true;
}

bool RenameEngine::undo(const QList<QPair<QString, QString>> &done, QString *error)
{
    for (const auto &d : done) {
        if (!QFileInfo::exists(d.first)) {
            if (error)
                *error = QStringLiteral("%1 is no longer there, so the rename cannot be undone.").arg(Location::fileName(d.first));
            return false;
        }
        if (QFileInfo::exists(d.second)) {
            bool alsoMoving = false;
            for (const auto &o : done)
                alsoMoving = alsoMoving || o.first == d.second;
            if (!alsoMoving) {
                if (error)
                    *error = QStringLiteral("Something is already called %1, so the rename cannot be undone.").arg(Location::fileName(d.second));
                return false;
            }
        }
    }
    return renameAll(done, error);
}

// ---------------------------------------------------------------- presets

namespace {

QString presetFile(const QString &file)
{
    return file.isEmpty() ? Paths::configFile(QStringLiteral("rename.ini")) : file;
}

} // namespace

QStringList RenamePresets::names(const QString &file)
{
    QSettings s(presetFile(file), QSettings::IniFormat);
    QStringList out;
    for (const QString &g : s.childGroups()) {
        s.beginGroup(g);
        out << s.value(QStringLiteral("name"), g).toString();
        s.endGroup();
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

static QString presetGroup(const QString &name)
{
    return QStringLiteral("preset-") + QString::fromLatin1(name.toUtf8().toHex());
}

bool RenamePresets::load(const QString &name, RenameOptions &o, const QString &file)
{
    QSettings s(presetFile(file), QSettings::IniFormat);
    const QString g = presetGroup(name);
    if (!s.childGroups().contains(g))
        return false;
    s.beginGroup(g);
    RenameOptions::modeFromName(s.value(QStringLiteral("mode")).toString(), o.mode);
    o.oldPattern = s.value(QStringLiteral("old")).toString();
    o.newPattern = s.value(QStringLiteral("new")).toString();
    o.caseSensitive = s.value(QStringLiteral("caseSensitive"), false).toBool();
    o.ignoreExtension = s.value(QStringLiteral("ignoreExtension"), false).toBool();
    RenameOptions::caseFromName(s.value(QStringLiteral("case")).toString(), o.nameCase);
    const QString ec = s.value(QStringLiteral("extCase")).toString();
    o.extCase = ec == QLatin1String("lower") ? RenameOptions::ExtCase::Lower
        : ec == QLatin1String("upper")       ? RenameOptions::ExtCase::Upper
                                             : RenameOptions::ExtCase::Keep;
    o.counterStart = s.value(QStringLiteral("start"), 1).toInt();
    o.counterStep = s.value(QStringLiteral("step"), 1).toInt();
    o.counterWidth = s.value(QStringLiteral("width"), 0).toInt();
    o.trimSpaces = s.value(QStringLiteral("trim"), false).toBool();
    s.endGroup();
    return true;
}

bool RenamePresets::save(const QString &name, const RenameOptions &o, const QString &file)
{
    if (name.trimmed().isEmpty())
        return false;
    QSettings s(presetFile(file), QSettings::IniFormat);
    s.beginGroup(presetGroup(name.trimmed()));
    s.setValue(QStringLiteral("name"), name.trimmed());
    s.setValue(QStringLiteral("mode"), RenameOptions::modeName(o.mode));
    s.setValue(QStringLiteral("old"), o.oldPattern);
    s.setValue(QStringLiteral("new"), o.newPattern);
    s.setValue(QStringLiteral("caseSensitive"), o.caseSensitive);
    s.setValue(QStringLiteral("ignoreExtension"), o.ignoreExtension);
    s.setValue(QStringLiteral("case"), RenameOptions::caseName(o.nameCase));
    s.setValue(QStringLiteral("extCase"), o.extCase == RenameOptions::ExtCase::Lower   ? QStringLiteral("lower")
                                              : o.extCase == RenameOptions::ExtCase::Upper ? QStringLiteral("upper")
                                                                                           : QStringLiteral("keep"));
    s.setValue(QStringLiteral("start"), o.counterStart);
    s.setValue(QStringLiteral("step"), o.counterStep);
    s.setValue(QStringLiteral("width"), o.counterWidth);
    s.setValue(QStringLiteral("trim"), o.trimSpaces);
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}

bool RenamePresets::remove(const QString &name, const QString &file)
{
    QSettings s(presetFile(file), QSettings::IniFormat);
    const QString g = presetGroup(name);
    if (!s.childGroups().contains(g))
        return false;
    s.remove(g);
    s.sync();
    return true;
}
