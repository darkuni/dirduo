#include "core/FileOps.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace FileOps {

quint64 deviceId(const QString &path)
{
    QString p = QDir::cleanPath(path);
    while (!p.isEmpty()) {
        struct stat st;
        if (::lstat(QFile::encodeName(p).constData(), &st) == 0)
            return static_cast<quint64>(st.st_dev);
        if (p == QLatin1String("/"))
            break;
        const int slash = p.lastIndexOf(QLatin1Char('/'));
        p = slash <= 0 ? QStringLiteral("/") : p.left(slash);
    }
    return 0;
}

bool sameDevice(const QString &a, const QString &b)
{
    const quint64 da = deviceId(a);
    return da != 0 && da == deviceId(b);
}

bool isSameOrInside(const QString &child, const QString &parent)
{
    const QString c = QDir::cleanPath(child);
    const QString p = QDir::cleanPath(parent);
    if (c == p)
        return true;
    if (p == QLatin1String("/"))
        return c.startsWith(QLatin1Char('/'));
    return c.startsWith(p + QLatin1Char('/'));
}

QString uniqueName(const QString &dir, const QString &name)
{
    const QDir d(dir);
    if (!QFileInfo::exists(d.filePath(name)) && !QFileInfo(d.filePath(name)).isSymLink())
        return name;

    // Split off the extension, but keep dotfiles and folders-like names whole.
    QString base = name;
    QString ext;
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && !QFileInfo(d.filePath(name)).isDir()) {
        base = name.left(dot);
        ext = name.mid(dot);
    }
    for (int i = 2; i < 100000; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(ext);
        const QString full = d.filePath(candidate);
        if (!QFileInfo::exists(full) && !QFileInfo(full).isSymLink())
            return candidate;
    }
    return name;
}

bool isValidFileName(const QString &name, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (name.isEmpty())
        return fail(QStringLiteral("The name cannot be empty."));
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return fail(QStringLiteral("\"%1\" is not a valid name.").arg(name));
    if (name.contains(QLatin1Char('/')))
        return fail(QStringLiteral("Names cannot contain \"/\"."));
    if (name.contains(QChar(0)))
        return fail(QStringLiteral("Names cannot contain a NUL character."));
    if (QFile::encodeName(name).size() > 255)
        return fail(QStringLiteral("The name is too long."));
    return true;
}

bool renameEntry(const QString &path, const QString &newName, QString *newPath, QString *error)
{
    if (!isValidFileName(newName, error))
        return false;
    const QFileInfo info(path);
    const QString target = info.absolutePath() + QLatin1Char('/') + newName;
    if (newPath)
        *newPath = target;
    if (info.fileName() == newName)
        return true;

    // A case-only change on a case-insensitive filesystem reports the target as
    // existing; it is the same entry, so let the rename through.
    const bool caseOnly = info.fileName().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && (QFileInfo::exists(target) || QFileInfo(target).isSymLink())) {
        if (error)
            *error = QStringLiteral("\"%1\" already exists.").arg(newName);
        return false;
    }
    if (::rename(QFile::encodeName(path).constData(), QFile::encodeName(target).constData()) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    return true;
}

QStringList createFolders(const QString &parent, const QString &commaSeparatedNames, QString *error)
{
    QStringList created;
    QStringList problems;
    const QStringList names = commaSeparatedNames.split(QLatin1Char(','));
    for (QString name : names) {
        name = name.trimmed();
        if (name.isEmpty())
            continue;
        QString why;
        if (!isValidFileName(name, &why)) {
            problems << QStringLiteral("%1: %2").arg(name, why);
            continue;
        }
        const QString full = QDir(parent).filePath(name);
        const QFileInfo fi(full);
        if (fi.exists() && !fi.isDir()) {
            problems << QStringLiteral("%1: a file with that name already exists").arg(name);
            continue;
        }
        if (!fi.exists() && !QDir().mkdir(full)) {
            problems << QStringLiteral("%1: could not be created").arg(name);
            continue;
        }
        created << full;
    }
    if (error)
        *error = problems.join(QLatin1Char('\n'));
    return created;
}

int naturalCompare(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = a.at(i).isDigit();
        const bool db = b.at(j).isDigit();
        if (da && db) {
            int si = i;
            while (si < a.size() && a.at(si) == QLatin1Char('0'))
                ++si;
            int sj = j;
            while (sj < b.size() && b.at(sj) == QLatin1Char('0'))
                ++sj;
            int ei = si;
            while (ei < a.size() && a.at(ei).isDigit())
                ++ei;
            int ej = sj;
            while (ej < b.size() && b.at(ej).isDigit())
                ++ej;
            const int la = ei - si;
            const int lb = ej - sj;
            if (la != lb)
                return la < lb ? -1 : 1;
            const int c = QStringView(a).mid(si, la).compare(QStringView(b).mid(sj, lb));
            if (c != 0)
                return c < 0 ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        if (da != db) {
            // Digits sort before letters, as in most file managers.
            return da ? -1 : 1;
        }
        int ei = i;
        while (ei < a.size() && !a.at(ei).isDigit())
            ++ei;
        int ej = j;
        while (ej < b.size() && !b.at(ej).isDigit())
            ++ej;
        const QString ra = a.mid(i, ei - i).toCaseFolded();
        const QString rb = b.mid(j, ej - j).toCaseFolded();
        if (ra != rb) {
            const int c = QString::localeAwareCompare(ra, rb);
            if (c != 0)
                return c < 0 ? -1 : 1;
            const int raw = ra.compare(rb);
            if (raw != 0)
                return raw < 0 ? -1 : 1;
        }
        i = ei;
        j = ej;
    }
    if (i < a.size())
        return 1;
    if (j < b.size())
        return -1;
    return a.compare(b); // only case or leading zeros differ: keep the order stable
}

QString formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 bytes").arg(bytes);
    static const char *units[] = {"KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = -1;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(QLocale().toString(value, 'f', value < 10 ? 2 : 1), QLatin1String(units[unit]));
}

} // namespace FileOps
