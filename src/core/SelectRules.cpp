#include "core/SelectRules.h"

#include "core/Location.h"

#include <QFileInfo>

bool SelectRules::compareFromName(const QString &name, Compare &out)
{
    const QString n = name.toLower();
    for (Compare c : {Compare::None, Compare::Newer, Compare::Older, Compare::Missing, Compare::Same, Compare::Different,
                      Compare::NewerOrMissing}) {
        if (n == compareName(c)) {
            out = c;
            return true;
        }
    }
    if (n == QLatin1String("unique")) {
        out = Compare::Missing;
        return true;
    }
    if (n == QLatin1String("identical") || n == QLatin1String("similar")) {
        out = Compare::Same;
        return true;
    }
    if (n == QLatin1String("changed")) {
        out = Compare::Different;
        return true;
    }
    return false;
}

QString SelectRules::compareName(Compare c)
{
    switch (c) {
    case Compare::None: return QStringLiteral("none");
    case Compare::Newer: return QStringLiteral("newer");
    case Compare::Older: return QStringLiteral("older");
    case Compare::Missing: return QStringLiteral("missing");
    case Compare::Same: return QStringLiteral("same");
    case Compare::Different: return QStringLiteral("different");
    case Compare::NewerOrMissing: return QStringLiteral("newerormissing");
    }
    return {};
}

bool SelectRules::isEmpty() const
{
    return pattern.trimmed().isEmpty() && !hasKind && type == Type::Both && minSize < 0 && maxSize < 0
        && !newerThan.isValid() && !olderThan.isValid() && compare == Compare::None;
}

QString SelectRules::counterpart(const QString &path) const
{
    if (otherRoot.isEmpty() || thisRoot.isEmpty() || !Location::isSameOrInside(path, thisRoot) || path == thisRoot)
        return {};
    QString rel = path.mid(thisRoot.size());
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    return Location::join(otherRoot, rel);
}

SelectRules::Compare SelectRules::classify(const FileItem &item) const
{
    const QString other = counterpart(item.path);
    if (other.isEmpty())
        return Compare::None;
    const QFileInfo fi(other);
    if (!fi.exists() && !fi.isSymLink())
        return Compare::Missing;
    if (item.isDir || fi.isDir())
        return item.isDir == fi.isDir() ? Compare::Same : Compare::Different;
    const qint64 delta = fi.lastModified().secsTo(item.modified); // > 0: this one is newer
    const bool sameTime = qAbs(delta) <= kDateSlackSecs;
    if (sameTime)
        return item.size == fi.size() ? Compare::Same : Compare::Different;
    return delta > 0 ? Compare::Newer : Compare::Older;
}

bool SelectRules::matches(const FileItem &item) const
{
    if (type == Type::Files && item.isDir)
        return false;
    if (type == Type::Folders && !item.isDir)
        return false;
    if (!pattern.trimmed().isEmpty()) {
        bool hit = false;
        for (const QString &p : pattern.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(p.trimmed()),
                                        QRegularExpression::CaseInsensitiveOption);
            hit = hit || re.match(item.name).hasMatch();
        }
        if (!hit)
            return false;
    }
    if (hasKind && FileKind::categoryFor(item.name, item.isDir) != kind)
        return false;
    if (!item.isDir) {
        if (minSize >= 0 && item.size < minSize)
            return false;
        if (maxSize >= 0 && item.size > maxSize)
            return false;
    } else if (minSize >= 0 || maxSize >= 0) {
        return false; // a size asks for files
    }
    if (newerThan.isValid() && !(item.modified > newerThan))
        return false;
    if (olderThan.isValid() && !(item.modified < olderThan))
        return false;
    if (compare != Compare::None) {
        const Compare c = classify(item);
        if (compare == Compare::NewerOrMissing)
            return c == Compare::Newer || c == Compare::Missing;
        if ((compare == Compare::Newer || compare == Compare::Older || compare == Compare::Different) && item.isDir)
            return false; // dates of folders say little
        return c == compare;
    }
    return true;
}
