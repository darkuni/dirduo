#include "core/FindEngine.h"

#include "core/Location.h"
#include "core/Paths.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

bool FindCriteria::isEmpty() const
{
    return name.trimmed().isEmpty() && contains.isEmpty() && minSize < 0 && maxSize < 0 && !newerThan.isValid()
        && !olderThan.isValid();
}

qint64 FindCriteria::parseSize(const QString &raw)
{
    QString t = raw.trimmed().toUpper();
    if (t.endsWith(QLatin1Char('B')))
        t.chop(1);
    qint64 mult = 1;
    if (t.endsWith(QLatin1Char('K')))
        mult = 1024;
    else if (t.endsWith(QLatin1Char('M')))
        mult = 1024 * 1024;
    else if (t.endsWith(QLatin1Char('G')))
        mult = 1024LL * 1024 * 1024;
    if (mult > 1)
        t.chop(1);
    bool ok = false;
    const double v = t.trimmed().toDouble(&ok);
    if (!ok || v < 0)
        return -1;
    return qint64(v * double(mult));
}

QDateTime FindCriteria::parseWhen(const QString &raw)
{
    const QString t = raw.trimmed();
    bool ok = false;
    const int days = t.toInt(&ok);
    if (ok)
        return QDateTime(QDate::currentDate().addDays(-days), QTime(0, 0));
    const QDate d = QDate::fromString(t, Qt::ISODate);
    return d.isValid() ? QDateTime(d, QTime(0, 0)) : QDateTime();
}

bool FindEngine::fileContains(const QString &path, const QString &text, bool caseSensitive, const std::atomic<bool> *cancel)
{
    if (text.isEmpty())
        return true;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray needle = caseSensitive ? text.toUtf8() : text.toUtf8().toLower();
    const qsizetype keep = needle.size() - 1; // overlap so a match across pieces is found
    QByteArray carry;
    while (!f.atEnd()) {
        if (cancel && cancel->load())
            return false;
        QByteArray chunk = carry + f.read(1024 * 1024);
        if (!caseSensitive)
            chunk = chunk.toLower();
        if (chunk.contains(needle))
            return true;
        carry = keep > 0 ? chunk.right(keep) : QByteArray();
    }
    return false;
}

QStringList FindEngine::find(const QStringList &roots, const FindCriteria &c, ScanProgress *progress, int maxResults, bool *truncated)
{
    if (truncated)
        *truncated = false;
    // Names: wildcards (several, ';' between) or one regular expression.
    QList<QRegularExpression> names;
    const auto opt = c.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
    if (c.nameRegex && !c.name.isEmpty()) {
        names << QRegularExpression(c.name, opt);
        if (!names.first().isValid())
            return {};
    } else {
        for (const QString &part : c.name.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            QString w = part.trimmed();
            if (w.isEmpty())
                continue;
            // A plain word finds names containing it, as people expect.
            if (!w.contains(QLatin1Char('*')) && !w.contains(QLatin1Char('?')))
                w = QLatin1Char('*') + w + QLatin1Char('*');
            names << QRegularExpression(QRegularExpression::wildcardToRegularExpression(w), opt);
        }
    }

    QStringList out;
    for (const QString &root : roots) {
        QDir::Filters f = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks;
        if (c.includeHidden)
            f |= QDir::Hidden;
        QDirIterator it(Location::clean(root), f, c.recurse ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            if (progress && progress->cancel.load())
                return out;
            const QString path = it.next();
            const QFileInfo fi = it.fileInfo();
            if (progress)
                progress->files.fetch_add(1);
            const bool isDir = fi.isDir();
            if ((c.type == FindCriteria::Type::Files && isDir) || (c.type == FindCriteria::Type::Folders && !isDir))
                continue;
            if (!names.isEmpty()) {
                bool any = false;
                for (const QRegularExpression &re : std::as_const(names))
                    any = any || re.match(fi.fileName()).hasMatch();
                if (!any)
                    continue;
            }
            if (!isDir) {
                if (c.minSize >= 0 && fi.size() < c.minSize)
                    continue;
                if (c.maxSize >= 0 && fi.size() > c.maxSize)
                    continue;
            }
            if (c.newerThan.isValid() && fi.lastModified() < c.newerThan)
                continue;
            if (c.olderThan.isValid() && fi.lastModified() >= c.olderThan)
                continue;
            if (!c.contains.isEmpty()) {
                if (isDir)
                    continue;
                if (progress)
                    progress->phase.store(1);
                if (!fileContains(path, c.contains, c.caseSensitive, progress ? &progress->cancel : nullptr))
                    continue;
            }
            out << path;
            if (out.size() >= maxResults) {
                if (truncated)
                    *truncated = true;
                return out;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- presets

namespace {

QString presetFile(const QString &file)
{
    return file.isEmpty() ? Paths::configFile(QStringLiteral("find.ini")) : file;
}

QString group(const QString &name)
{
    return QStringLiteral("search-") + QString::fromLatin1(name.trimmed().toUtf8().toHex());
}

} // namespace

QStringList FindPresets::names(const QString &file)
{
    QSettings s(presetFile(file), QSettings::IniFormat);
    QStringList out;
    for (const QString &g : s.childGroups()) {
        s.beginGroup(g);
        out << s.value(QStringLiteral("name")).toString();
        s.endGroup();
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

bool FindPresets::load(const QString &name, FindCriteria &c, QStringList *roots, const QString &file)
{
    QSettings s(presetFile(file), QSettings::IniFormat);
    if (!s.childGroups().contains(group(name)))
        return false;
    s.beginGroup(group(name));
    c.name = s.value(QStringLiteral("name_pattern")).toString();
    c.nameRegex = s.value(QStringLiteral("regex"), false).toBool();
    c.caseSensitive = s.value(QStringLiteral("caseSensitive"), false).toBool();
    c.contains = s.value(QStringLiteral("contains")).toString();
    c.minSize = s.value(QStringLiteral("minSize"), -1).toLongLong();
    c.maxSize = s.value(QStringLiteral("maxSize"), -1).toLongLong();
    // Relative ages stay relative: "last 7 days" means the last 7 days next week too.
    const int newerDays = s.value(QStringLiteral("newerDays"), -1).toInt();
    const int olderDays = s.value(QStringLiteral("olderDays"), -1).toInt();
    c.newerThan = newerDays >= 0 ? FindCriteria::parseWhen(QString::number(newerDays)) : s.value(QStringLiteral("newer")).toDateTime();
    c.olderThan = olderDays >= 0 ? FindCriteria::parseWhen(QString::number(olderDays)) : s.value(QStringLiteral("older")).toDateTime();
    c.type = static_cast<FindCriteria::Type>(s.value(QStringLiteral("type"), 0).toInt());
    c.includeHidden = s.value(QStringLiteral("hidden"), false).toBool();
    c.recurse = s.value(QStringLiteral("recurse"), true).toBool();
    if (roots)
        *roots = s.value(QStringLiteral("roots")).toStringList();
    s.endGroup();
    return true;
}

bool FindPresets::save(const QString &name, const FindCriteria &c, const QStringList &roots, const QString &file)
{
    if (name.trimmed().isEmpty())
        return false;
    QSettings s(presetFile(file), QSettings::IniFormat);
    s.beginGroup(group(name));
    s.setValue(QStringLiteral("name"), name.trimmed());
    s.setValue(QStringLiteral("name_pattern"), c.name);
    s.setValue(QStringLiteral("regex"), c.nameRegex);
    s.setValue(QStringLiteral("caseSensitive"), c.caseSensitive);
    s.setValue(QStringLiteral("contains"), c.contains);
    s.setValue(QStringLiteral("minSize"), c.minSize);
    s.setValue(QStringLiteral("maxSize"), c.maxSize);
    // Dates at midnight N days back are stored as "N days".
    auto days = [](const QDateTime &d) {
        if (!d.isValid() || d.time() != QTime(0, 0))
            return -1;
        return int(d.date().daysTo(QDate::currentDate()));
    };
    s.setValue(QStringLiteral("newerDays"), days(c.newerThan));
    s.setValue(QStringLiteral("olderDays"), days(c.olderThan));
    s.setValue(QStringLiteral("newer"), c.newerThan);
    s.setValue(QStringLiteral("older"), c.olderThan);
    s.setValue(QStringLiteral("type"), int(c.type));
    s.setValue(QStringLiteral("hidden"), c.includeHidden);
    s.setValue(QStringLiteral("recurse"), c.recurse);
    s.setValue(QStringLiteral("roots"), roots);
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}

bool FindPresets::remove(const QString &name, const QString &file)
{
    QSettings s(presetFile(file), QSettings::IniFormat);
    if (!s.childGroups().contains(group(name)))
        return false;
    s.remove(group(name));
    s.sync();
    return true;
}
